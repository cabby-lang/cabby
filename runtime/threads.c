/* threads.c — the C side of the threads library (lib/threads.hby): a
 * worker's `work` method, run on a thread of its own. Part of Cabby's
 * runtime, so the interpreter and compiled programs both have it.
 *
 * Compiled, each worker is a real thread, and they run side by side on as
 * many cores as there are. Interpreted, the interpreter lets one thread at
 * a time run Cabby code (the lock below, as Python's): a thread lets the
 * others in while it waits, while it's in a library (downloading, reading a
 * database, sleeping), and every so often in between, so they take turns,
 * and a window stays awake while a worker works.
 *
 * What makes threads safe is Cabby's, not this file's: a worker's fields
 * belong to it while it works (the checker sees to that), and a library's
 * last answer (a pattern's matches, a database's rows) is kept for each
 * thread on its own. */
#include "rt.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#ifdef __wasi__
int pipe(int fd[2]);   /* (wasi-sdk 33 and older don't declare it: wasm/stubs.c) */
#endif

/* ------------------------------------------------------------ the interpreter's lock
 *
 * Fair: whoever asked first goes next (a ticket each, as at a counter), so
 * a thread that keeps asking can't starve the others. Off until the first
 * worker starts: a program without workers pays nothing. */

static pthread_mutex_t gil_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gil_turn = PTHREAD_COND_INITIALIZER;
static uint64_t gil_next, gil_serving;
static atomic_int gil_waiting;
static atomic_bool gil_on;
static _Thread_local bool gil_held;   /* this thread is running Cabby code */

static void gil_lock(void)
{
	pthread_mutex_lock(&gil_mutex);
	uint64_t mine = gil_next++;
	atomic_fetch_add(&gil_waiting, 1);
	while (gil_serving != mine)
		pthread_cond_wait(&gil_turn, &gil_mutex);
	atomic_fetch_sub(&gil_waiting, 1);
	pthread_mutex_unlock(&gil_mutex);
}

static void gil_unlock(void)
{
	pthread_mutex_lock(&gil_mutex);
	gil_serving++;
	pthread_cond_broadcast(&gil_turn);
	pthread_mutex_unlock(&gil_mutex);
}

/* Cabby code starts running on this thread (a program's main, a handler);
 * gives what to hand rt_gil_leave */
bool rt_gil_enter(void)
{
	bool was = gil_held;
	if (!was) {
		if (atomic_load(&gil_on))
			gil_lock();
		gil_held = true;
	}
	return was;
}

void rt_gil_leave(bool was)
{
	if (!was) {
		if (atomic_load(&gil_on))
			gil_unlock();
		gil_held = false;
	}
}

/* Before a library call, which may wait: the others may run meanwhile */
bool rt_gil_drop(void)
{
	bool was = gil_held;
	if (was) {
		if (atomic_load(&gil_on))
			gil_unlock();
		gil_held = false;
	}
	return was;
}

void rt_gil_take(bool was)
{
	if (was) {
		if (atomic_load(&gil_on))
			gil_lock();
		gil_held = true;
	}
}

/* Every so often: if another thread is waiting, it has a turn */
void rt_gil_yield(void)
{
	if (gil_held && atomic_load(&gil_waiting) > 0) {
		gil_unlock();
		gil_lock();
	}
}

bool rt_gil_is_on(void) { return atomic_load(&gil_on); }
bool rt_gil_held(void) { return gil_held; }

/* ------------------------------------------------------------ at a thread's end
 *
 * A library that keeps something for each thread (its last answer) says
 * how to let it go, and it's let go when a worker's thread ends. */

static void (*enders[16])(void);
static int nenders;

void rt_on_thread_end(void (*fn)(void))
{
	if (nenders < 16)
		enders[nenders++] = fn;
}

void rt_thread_ends(void)
{
	for (int i = 0; i < nenders; i++)
		enders[i]();
}

/* ------------------------------------------------------------ workers */

enum { NEW, WORKING, DONE };

typedef struct {
	void *obj;
	void *fn;
} Handler;

typedef struct {
	pthread_t thread;
	int state;
	bool joined;
	void *obj, *fn;    /* the worker, and its method that calls `work` */
	/* on_done: the worker's method that calls the handler, once it's done,
	 * from the program's own event loop (a window's, a game's, events.run),
	 * which a byte down this pipe wakes */
	bool has_done, told;
	Handler done;
	int pipe_fd[2];
	int watch;
	/* on_message: what `tell` sends, kept in order until the main program's
	 * loop hands each to the handler (a byte 'm' down the pipe for each;
	 * 'd' when it's done, after them) */
	bool has_message;
	Handler deliver;
	char **queue;      /* (a picture's message: NULL, its picture beside it) */
	int64_t *queue_pictures;
	int nqueue, capqueue, front;
	pthread_mutex_t queue_lock;
	char *message;     /* the one being handed to the handler */
	int64_t picture;   /* and its picture, if it's one (until `picture` takes it) */
	atomic_bool stop;  /* ask_to_stop */
} Worker;

static pthread_mutex_t table = PTHREAD_MUTEX_INITIALIZER;
static Worker **workers;
static int nworkers;
static atomic_int working;   /* how many are working now */

static Worker *worker(int64_t id)
{
	pthread_mutex_lock(&table);
	Worker *w = id >= 1 && id <= nworkers ? workers[id - 1] : NULL;
	pthread_mutex_unlock(&table);
	if (!w)
		rt_panic(NULL, 0, 0, "that worker is gone");
	return w;
}

int64_t cby_thread_new(void)
{
	Worker *w = calloc(1, sizeof *w);
	pthread_mutex_init(&w->queue_lock, NULL);
	w->watch = -1;
	pthread_mutex_lock(&table);
	int i = 0;
	while (i < nworkers && workers[i])
		i++;
	if (i == nworkers) {
		workers = realloc(workers, sizeof *workers * (size_t)(nworkers + 1));
		nworkers++;
	}
	workers[i] = w;
	pthread_mutex_unlock(&table);
	return i + 1;
}

static _Thread_local Worker *me;   /* the worker whose thread this is */

static void *run(void *p)
{
	Worker *w = p;
	me = w;
	rt_invoke_hook(w->obj, w->fn);
	rt_flush_files();
	rt_thread_ends();
	pthread_mutex_lock(&table);
	w->state = DONE;
	atomic_fetch_sub(&working, 1);
	pthread_mutex_unlock(&table);
	if (w->has_done || w->has_message) {
		char done = 'd';
		ssize_t wrote = write(w->pipe_fd[1], &done, 1);   /* (the pipe has room: nothing else is written) */
		(void)wrote;
	}
	return NULL;
}

/* how much room a worker has for calls inside calls: as much as a program's
 * main has, near enough */
#define WORKER_STACK ((size_t)256 << 20)

static void join(Worker *w)
{
	pthread_mutex_lock(&table);
	bool started = w->state != NEW;   /* (read with the lock: the worker sets DONE as it ends) */
	pthread_mutex_unlock(&table);
	if (started && !w->joined) {
		bool was = rt_gil_drop();
		pthread_join(w->thread, NULL);
		rt_gil_take(was);
		w->joined = true;
	}
}

/* Cabby's event loop (in rt.c) */
extern int cby_events_watch(int fd, void (*fn)(void *data), void *data);
extern void cby_events_unwatch(int id);
extern void cby_image_free(int64_t id);   /* (images.c: a picture handed on, let go of) */

/* no longer watched: the pipe closed */
static void unwatch(Worker *w)
{
	if (w->watch >= 0) {
		cby_events_unwatch(w->watch);
		close(w->pipe_fd[0]);
		close(w->pipe_fd[1]);
		w->watch = -1;
	}
}

/* the next message, handed to the handler (main thread) */
static void hand_on(Worker *w)
{
	pthread_mutex_lock(&w->queue_lock);
	int any = w->front < w->nqueue;
	char *m = any ? w->queue[w->front] : NULL;
	int64_t pic = any ? w->queue_pictures[w->front] : 0;
	if (any)
		w->front++;
	if (w->front == w->nqueue)
		w->front = w->nqueue = 0;
	pthread_mutex_unlock(&w->queue_lock);
	if (!any)
		return;
	free(w->message);
	w->message = m;
	cby_image_free(w->picture);   /* (one the handler didn't take) */
	w->picture = pic;
	if (w->has_message)
		rt_invoke_hook(w->deliver.obj, w->deliver.fn);
}

/* it's done: its thread let go of, then the handler called (once) */
static void tell(Worker *w)
{
	if (w->told)
		return;
	w->told = true;
	unwatch(w);
	join(w);
	if (w->has_done)
		rt_invoke_hook(w->done.obj, w->done.fn);
}

/* what's come down the pipe: messages, in order, and then 'd' */
static void pipe_ready(void *data)
{
	Worker *w = data;
	char b[256];
	ssize_t n;
	while (w->watch >= 0 && (n = read(w->pipe_fd[0], b, sizeof b)) > 0)
		for (ssize_t i = 0; i < n && !w->told; i++) {
			if (b[i] == 'm')
				hand_on(w);
			else
				tell(w);
		}
}

/* (each handler as two pointers, the object and its method, as Cabby
 * passes them: not a struct, which ARM64 passes differently, on the stack,
 * once the registers run out) */
void cby_thread_start(int64_t id, void *work_obj, void *work_fn, int has_done, void *done_obj, void *done_fn,
		      int has_message, void *deliver_obj, void *deliver_fn)
{
	Handler work = { work_obj, work_fn }, done = { done_obj, done_fn }, deliver = { deliver_obj, deliver_fn };
	Worker *w = worker(id);
	if (w->state == WORKING)
		rt_panic(NULL, 0, 0, "that worker is still working: 'wait' for it before starting it again");
	join(w);   /* (one started before, and done: its thread's let go) */
	w->obj = work.obj;
	w->fn = work.fn;
	unwatch(w);   /* (a handler from before, never called: the worker was started again) */
	w->has_done = has_done != 0;
	w->has_message = has_message != 0;
	w->told = !w->has_done && !w->has_message;
	w->done = done;
	w->deliver = deliver;
	w->watch = -1;
	atomic_store(&w->stop, false);
	pthread_mutex_lock(&w->queue_lock);
	for (int i = w->front; i < w->nqueue; i++) {
		free(w->queue[i]);
		cby_image_free(w->queue_pictures[i]);
	}
	w->front = w->nqueue = 0;
	pthread_mutex_unlock(&w->queue_lock);
	if (!w->told) {
		if (me)
			rt_panic(NULL, 0, 0, "'on_done' and 'on_message' are for the main program: a worker's own "
					     "thread can't be told (it can 'wait' for the other worker instead)");
		if (pipe(w->pipe_fd) != 0)
			rt_panic(NULL, 0, 0, "a worker couldn't be started: no pipe for its messages");
		fcntl(w->pipe_fd[0], F_SETFL, fcntl(w->pipe_fd[0], F_GETFL) | O_NONBLOCK);
		w->watch = cby_events_watch(w->pipe_fd[0], pipe_ready, w);
	}
	pthread_mutex_lock(&table);
	w->state = WORKING;
	atomic_fetch_add(&working, 1);
	pthread_mutex_unlock(&table);
	w->joined = false;
	/* The lock is off until the first worker: until then the main thread
	 * runs without taking it, though it counts as holding it. Turning it
	 * on, the thread starting the worker takes it for real first, or the
	 * worker would take it at once and both would run together. */
	if (!atomic_load(&gil_on)) {
		if (gil_held)
			gil_lock();
		atomic_store(&gil_on, true);
	}
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, WORKER_STACK);
	int made = pthread_create(&w->thread, &attr, run, w);
	pthread_attr_destroy(&attr);
	if (made != 0) {
		pthread_mutex_lock(&table);
		w->state = NEW;
		atomic_fetch_sub(&working, 1);
		pthread_mutex_unlock(&table);
		rt_panic(NULL, 0, 0, "a worker's thread couldn't be started (too many at once?)");
	}
}

/* waiting hands on what's come meanwhile, and calls the done handler,
 * if the event loop hasn't yet: so a worker sending messages never waits
 * for room in the pipe while the program waits for it */
void cby_thread_wait(int64_t id)
{
	Worker *w = worker(id);
	while (!w->told && w->watch >= 0) {
		struct pollfd p = { w->pipe_fd[0], POLLIN, 0 };
		bool was = rt_gil_drop();
		poll(&p, 1, -1);
		rt_gil_take(was);
		pipe_ready(w);
	}
	join(w);
}

/* from inside `work`: a line for the main program's on_message handler
 * (none: it isn't kept) */
/* a message onto the queue, and a byte down the pipe for it */
static void send_message(Worker *w, char *text, int64_t picture)
{
	pthread_mutex_lock(&w->queue_lock);
	if (w->nqueue == w->capqueue) {
		w->capqueue = w->capqueue ? w->capqueue * 2 : 16;
		w->queue = realloc(w->queue, sizeof *w->queue * (size_t)w->capqueue);
		w->queue_pictures = realloc(w->queue_pictures, sizeof *w->queue_pictures * (size_t)w->capqueue);
	}
	w->queue[w->nqueue] = text;
	w->queue_pictures[w->nqueue++] = picture;
	pthread_mutex_unlock(&w->queue_lock);
	char m = 'm';
	bool was = rt_gil_drop();   /* (a full pipe: it waits for the main program to read) */
	ssize_t wrote = write(w->pipe_fd[1], &m, 1);
	(void)wrote;
	rt_gil_take(was);
}

void cby_thread_tell(int64_t id, const char *text)
{
	Worker *w = worker(id);
	if (!w->has_message || w->state != WORKING)
		return;
	send_message(w, strdup(text), 0);
}

/* from inside `work`: a picture, moved to the main program as it is (no
 * pixel copied); none to take it, and it's let go of */
void cby_thread_tell_picture(int64_t id, int64_t picture)
{
	Worker *w = worker(id);
	if (!w->has_message || w->state != WORKING) {
		cby_image_free(picture);
		return;
	}
	send_message(w, NULL, picture);
}

/* in the on_message handler: the picture sent, taken (0: it wasn't one,
 * or it's been taken) */
int64_t cby_thread_take_picture(int64_t id)
{
	Worker *w = worker(id);
	if (me)
		return 0;
	int64_t p = w->picture;
	w->picture = 0;
	return p;
}

int cby_thread_message_is_picture(int64_t id)
{
	Worker *w = worker(id);
	return !me && w->picture != 0;
}

/* the message being handed on (on the main thread; "" anywhere else) */
int64_t cby_thread_message_length(int64_t id)
{
	Worker *w = worker(id);
	return me || !w->message ? 0 : (int64_t)strlen(w->message);
}

void cby_thread_message_take(int64_t id, char *dst)
{
	Worker *w = worker(id);
	if (!me && w->message)
		memcpy(dst, w->message, strlen(w->message));
}

void cby_thread_ask_to_stop(int64_t id) { atomic_store(&worker(id)->stop, true); }
int cby_thread_stopping(int64_t id) { return atomic_load(&worker(id)->stop); }

int cby_thread_is_done(int64_t id)
{
	Worker *w = worker(id);
	pthread_mutex_lock(&table);
	int done = w->state != WORKING;
	pthread_mutex_unlock(&table);
	return done;
}

/* While a worker works, its fields and methods are its thread's: from
 * anywhere else, a panic. (Its thread's own calls are fine: `work`, and
 * whatever it calls.) */
void rt_worker_check(void *obj, const char *file, int line, int col)
{
	if (atomic_load(&working) == 0)
		return;
	bool busy = false;
	pthread_mutex_lock(&table);
	for (int i = 0; i < nworkers && !busy; i++) {
		Worker *w = workers[i];
		busy = w && w->obj == obj && w->state == WORKING && w != me;
	}
	pthread_mutex_unlock(&table);
	if (busy)
		rt_panic(file, line, col, "that worker is still working, and while it works what's in it is its own: "
					  "'wait' for it first (or check 'is_done')");
}

void cby_thread_free(int64_t id)
{
	if (id < 1 || id > nworkers)
		return;
	Worker *w = worker(id);
	atomic_store(&w->stop, true);   /* (dropped while working: it's asked to stop, then waited for) */
	if (w->watch >= 0) {
		/* messages it's still sending aren't for anyone now: read and let go of */
		while (!cby_thread_is_done(id)) {
			struct pollfd p = { w->pipe_fd[0], POLLIN, 0 };
			bool was = rt_gil_drop();
			poll(&p, 1, 50);
			rt_gil_take(was);
			char b[256];
			while (read(w->pipe_fd[0], b, sizeof b) > 0)
				;
		}
	}
	join(w);
	unwatch(w);   /* (dropped before it was told: it isn't) */
	pthread_mutex_lock(&table);
	workers[id - 1] = NULL;
	pthread_mutex_unlock(&table);
	for (int i = w->front; i < w->nqueue; i++) {
		free(w->queue[i]);
		cby_image_free(w->queue_pictures[i]);
	}
	free(w->queue);
	free(w->queue_pictures);
	free(w->message);
	cby_image_free(w->picture);
	pthread_mutex_destroy(&w->queue_lock);
	free(w);
}

int64_t cby_thread_cores(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? n : 1;
}

__attribute__((destructor)) static void threads_end(void)
{
	free(workers);
}
