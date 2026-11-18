/* processes.c — the C side of Cabby's `processes` library: programs this
 * program starts and talks to.
 *
 * lib/processes.hby wraps these functions in the `process` object. Each
 * process is a small integer id, an index into the table below. A process
 * is run by /bin/sh -c, with its input and output connected to this program
 * through pipes; what it prints to stderr goes where this program's does,
 * unless errors_apart was asked for: then stderr is a pipe of its own, cut
 * into lines of its own.
 *
 * Its output is read in pieces and cut into lines, which wait in a queue
 * until the program reads them (read_line) or, if the process object has a
 * line handler, are handed to it. That happens while the program waits:
 * in `wait`, or in Cabby's event loop (events.run, or a gui or tui window
 * waiting), which watches the output of every process with handlers.
 *
 * Handlers are called through cby_invoke_hook, as in gui.c: compiled
 * programs call the method directly; the interpreter points the hook at a
 * function that interprets it. The process object's destructor calls
 * cby_process_free, so nothing here ever calls an object that's gone.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------- handlers */

static void call_directly(void *obj, void *fn) { ((void (*)(void *))fn)(obj); }

/* The interpreter replaces this when it loads the library. (Weak, so a
 * program can link this with the gui and tui libraries.) */
__attribute__((weak)) void (*cby_invoke_hook)(void *obj, void *fn) = call_directly;

/* Cabby's runtime, when the program is compiled: files written out after a
 * handler, as windows do */
extern void rt_flush_files(void) __attribute__((weak));

/* Cabby's event loop (in its runtime) */
extern int cby_events_watch(int fd, void (*fn)(void *data), void *data);
extern void cby_events_unwatch(int id);

typedef struct Handler {
	void *obj;
	void *fn;
} Handler;

/* ------------------------------------------------------------- the table */

/* one of its outputs (stdout, or stderr kept apart), cut into lines */
typedef struct Stream {
	int fd;               /* our end (-1: closed, or never opened) */
	bool ended;           /* everything it printed there has been read */
	/* what it printed, not yet cut into lines */
	char *part;
	size_t part_len, part_cap;
	/* whole lines, waiting to be read */
	char **lines;
	size_t nlines, first, cap;
	/* the line just taken */
	char *current;
	size_t current_len;
	int watch;            /* the event loop's watch on it, or 0 */
} Stream;

typedef struct Proc {
	bool used;
	char *command;
	pid_t pid;
	int in_fd;            /* our end of its input (-1: closed) */
	bool detached;
	bool errors_apart;    /* its stderr is err, not this program's */
	bool reaped;          /* its exit status is known */
	int status;           /* exit status, as a shell gives it */
	Stream out, err;
	bool end_told;        /* its end handler has been called */
	Handler deliver;      /* the object's method that hands lines to its handler */
	Handler deliver_err;  /* ... error lines to its error line handler */
	Handler ended;        /* ... and the one that says it has ended */
	bool delivering;      /* inside deliver: don't start it again */
} Proc;

static Proc *procs;
static int nprocs;

/* Cabby's panic, when the program has its runtime (it always does), so
 * an `else` can catch it */
extern _Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...) __attribute__((weak));

static _Noreturn void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static _Noreturn void panic(const char *fmt, ...)
{
	fflush(stdout);
	va_list ap;
	if (rt_panic) {
		char msg[1024];
		va_start(ap, fmt);
		vsnprintf(msg, sizeof msg, fmt, ap);
		va_end(ap);
		rt_panic(NULL, 0, 0, "%s", msg);
	}
	va_start(ap, fmt);
	fputs("panic: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(101);
}

static Proc *proc(int64_t id)
{
	if (id <= 0 || id > nprocs || !procs[id - 1].used)
		panic("processes: a process that was never started was used");
	return &procs[id - 1];
}

static void *grow(void *p, size_t size)
{
	void *q = realloc(p, size);
	if (!q)
		panic("out of memory");
	return q;
}

static char *dupn(const char *s, size_t n)
{
	char *d = grow(NULL, n + 1);
	memcpy(d, s, n);
	d[n] = '\0';
	return d;
}

/* ------------------------------------------------------------- output */

static void push_line(Stream *s, const char *t, size_t n)
{
	if (n > 0 && t[n - 1] == '\r')
		n--;
	if (s->first + s->nlines == s->cap) {
		if (s->first > 0) {
			memmove(s->lines, s->lines + s->first, sizeof *s->lines * s->nlines);
			s->first = 0;
		}
		if (s->nlines == s->cap) {
			s->cap = s->cap ? s->cap * 2 : 16;
			s->lines = grow(s->lines, sizeof *s->lines * s->cap);
		}
	}
	s->lines[s->first + s->nlines++] = dupn(t, n);
}

/* everything it printed has been read: false when it's ended */
static bool all_ended(const Proc *p) { return p->out.ended && (!p->errors_apart || p->err.ended); }

/* reads what's in one stream now (it's ready: read won't wait) */
static void read_stream(Stream *s)
{
	char buf[65536];
	ssize_t got;
	do
		got = read(s->fd, buf, sizeof buf);
	while (got < 0 && errno == EINTR);
	if (got <= 0) {
		/* the end: a last line without its line end still counts */
		if (s->part_len)
			push_line(s, s->part, s->part_len);
		s->part_len = 0;
		if (s->watch) {   /* nothing more to watch for */
			cby_events_unwatch(s->watch);
			s->watch = 0;
		}
		close(s->fd);
		s->fd = -1;
		s->ended = true;
		return;
	}
	size_t start = 0;
	for (size_t i = 0; i < (size_t)got; i++)
		if (buf[i] == '\n') {
			if (s->part_len) {
				size_t need = s->part_len + (i - start);
				if (need > s->part_cap) {
					s->part_cap = need * 2;
					s->part = grow(s->part, s->part_cap);
				}
				memcpy(s->part + s->part_len, buf + start, i - start);
				push_line(s, s->part, need);
				s->part_len = 0;
			} else {
				push_line(s, buf + start, i - start);
			}
			start = i + 1;
		}
	size_t rest = (size_t)got - start;
	if (rest) {
		if (s->part_len + rest > s->part_cap) {
			s->part_cap = (s->part_len + rest) * 2;
			s->part = grow(s->part, s->part_cap);
		}
		memcpy(s->part + s->part_len, buf + start, rest);
		s->part_len += rest;
	}
}

/* Reads what's there in each of its outputs (waiting for something if
 * `block`); false when `want` (its stdout or stderr, or NULL: either)
 * has ended and nothing more will come there. Both are read whichever is
 * wanted, so a program that fills one while this one waits for the other
 * never gets stuck. */
static bool read_some(Proc *p, Stream *want, bool block)
{
	struct pollfd pf[2];
	Stream *of[2];
	int n = 0;
	if (p->out.fd >= 0) {
		pf[n] = (struct pollfd){ p->out.fd, POLLIN, 0 };
		of[n++] = &p->out;
	}
	if (p->err.fd >= 0) {
		pf[n] = (struct pollfd){ p->err.fd, POLLIN, 0 };
		of[n++] = &p->err;
	}
	if (n == 0)
		return false;
	int r;
	do
		r = poll(pf, (nfds_t)n, block ? -1 : 0);
	while (r < 0 && errno == EINTR);
	for (int i = 0; r > 0 && i < n; i++)
		if (pf[i].revents)
			read_stream(of[i]);
	return want ? want->fd >= 0 : !all_ended(p);
}

static int shell_status(int st)
{
	if (WIFEXITED(st))
		return WEXITSTATUS(st);
	if (WIFSIGNALED(st))
		return 128 + WTERMSIG(st);
	return st;
}

static void reap(Proc *p, bool block)
{
	if (p->reaped || p->detached || p->pid <= 0)
		return;
	int st;
	pid_t r;
	do
		r = waitpid(p->pid, &st, block ? 0 : WNOHANG);
	while (r < 0 && errno == EINTR);
	if (r == p->pid) {
		p->reaped = true;
		p->status = shell_status(st);
	} else if (r < 0) {
		p->reaped = true;   /* not ours any more */
		p->status = -1;
	}
}

static void call(Handler h)
{
	if (!h.fn)
		return;
	fflush(stdout);
	cby_invoke_hook(h.obj, h.fn);
	if (rt_flush_files)
		rt_flush_files();
}

/* lines still to hand to a handler */
static bool lines_to_hand(const Proc *p)
{
	return (p->out.nlines && p->deliver.fn) || (p->err.nlines && p->deliver_err.fn);
}

/* Hands waiting lines to the object (which passes them to its handlers),
 * and says when the process has ended. */
static void deliver(int64_t id)
{
	Proc *p = &procs[id - 1];
	if (p->delivering)
		return;
	if (p->err.nlines && p->deliver_err.fn) {
		p->delivering = true;
		call(p->deliver_err);
		p = &procs[id - 1];   /* the table may have grown */
		p->delivering = false;
	}
	if (p->used && p->out.nlines && p->deliver.fn) {
		p->delivering = true;
		call(p->deliver);
		p = &procs[id - 1];
		p->delivering = false;
	}
	if (!p->used || !all_ended(p) || p->end_told)
		return;
	reap(p, true);
	if (lines_to_hand(p))
		return;   /* lines still to hand over first */
	p->end_told = true;
	call(p->ended);
}

/* ------------------------------------------------------------- starting */

static void child_setup(const char *dev_null_for_all)
{
	signal(SIGPIPE, SIG_DFL);
	if (dev_null_for_all) {
		int fd = open(dev_null_for_all, O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
	}
}

int64_t cby_process_new(void)
{
	static bool once;
	if (!once) {
		once = true;
		signal(SIGPIPE, SIG_IGN);   /* writing to a process that ended: an error, not death */
	}
	int i = 0;
	while (i < nprocs && procs[i].used)
		i++;
	if (i == nprocs)
		procs = grow(procs, sizeof *procs * (size_t)++nprocs);
	memset(&procs[i], 0, sizeof procs[i]);
	procs[i].used = true;
	procs[i].in_fd = procs[i].out.fd = procs[i].err.fd = -1;
	procs[i].err.ended = true;
	return i + 1;
}

void cby_process_set_detached(int64_t id)
{
	Proc *p = proc(id);
	if (p->pid)
		panic("can't detach '%s' now: detach a process before starting it", p->command);
	p->detached = true;
}

void cby_process_set_errors_apart(int64_t id)
{
	Proc *p = proc(id);
	if (p->pid && !p->reaped)
		panic("can't keep the errors of '%s' apart now: ask before starting it", p->command);
	p->errors_apart = true;
}

static bool watched(const Proc *p) { return p->deliver.fn || p->deliver_err.fn || p->ended.fn; }

static void update_watch(int64_t id);

/* the event loop: its output has something (or has ended) */
static void on_output(void *data)
{
	int64_t id = (int64_t)(intptr_t)data;
	if (id <= 0 || id > nprocs || !procs[id - 1].used)
		return;
	read_some(&procs[id - 1], NULL, false);
	deliver(id);
	update_watch(id);
}

static void watch_stream(Stream *s, bool want, int64_t id)
{
	want = want && s->fd >= 0;
	if (want && !s->watch)
		s->watch = cby_events_watch(s->fd, on_output, (void *)(intptr_t)id);
	else if (!want && s->watch) {
		cby_events_unwatch(s->watch);
		s->watch = 0;
	}
}

/* The event loop watches a process with handlers, until it has ended */
static void update_watch(int64_t id)
{
	Proc *p = &procs[id - 1];
	bool want = p->used && watched(p) && !p->end_told;
	watch_stream(&p->out, want, id);
	watch_stream(&p->err, want, id);
	p = &procs[id - 1];
	if (p->used && watched(p) && all_ended(p) && !p->end_told && !p->delivering)
		deliver(id);   /* it ended between watches: say so */
}

/* The object's own methods to call: one that hands lines on to its line
 * handler (if it has one), one for error lines, and one that says it has
 * ended (likewise). */
void cby_process_watch(int64_t id, int has_line, void *dobj, void *dfn, int has_end, void *eobj, void *efn)
{
	Proc *p = proc(id);
	p->deliver = has_line ? (Handler){ dobj, dfn } : (Handler){ 0 };
	p->ended = has_end ? (Handler){ eobj, efn } : (Handler){ 0 };
	update_watch(id);
}

void cby_process_watch_errors(int64_t id, int has_line, void *dobj, void *dfn)
{
	Proc *p = proc(id);
	p->deliver_err = has_line ? (Handler){ dobj, dfn } : (Handler){ 0 };
	update_watch(id);
}

static void stream_reset(Stream *s, int fd)
{
	s->fd = fd;
	s->ended = fd < 0;
	s->part_len = 0;
}

void cby_process_start(int64_t id, const char *command)
{
	Proc *p = proc(id);
	if (p->pid && !p->reaped && !p->detached)
		panic("can't start '%s': this process is still running '%s'", command, p->command);
	free(p->command);
	p->command = dupn(command, strlen(command));
	fflush(stdout);
	fflush(stderr);
	if (p->detached) {
		/* a daemon: its own session, no input or output, nobody waits */
		pid_t c = fork();
		if (c < 0)
			panic("can't start '%s': %s", command, strerror(errno));
		if (c == 0) {
			setsid();
			if (fork() != 0)
				_exit(0);
			child_setup("/dev/null");
			execl("/bin/sh", "sh", "-c", command, (char *)NULL);
			_exit(127);
		}
		int st;
		while (waitpid(c, &st, 0) < 0 && errno == EINTR)
			;
		p->pid = -1;
		p->reaped = true;
		p->out.ended = p->err.ended = true;
		p->end_told = true;
		return;
	}
	int in[2], out[2], err[2] = { -1, -1 };
	if (pipe(in) != 0 || pipe(out) != 0 || (p->errors_apart && pipe(err) != 0))
		panic("can't start '%s': %s", command, strerror(errno));
	pid_t c = fork();
	if (c < 0)
		panic("can't start '%s': %s", command, strerror(errno));
	if (c == 0) {
		child_setup(NULL);
		dup2(in[0], 0);
		dup2(out[1], 1);
		if (err[1] >= 0)
			dup2(err[1], 2);
		close(in[0]);
		close(in[1]);
		close(out[0]);
		close(out[1]);
		if (err[0] >= 0) {
			close(err[0]);
			close(err[1]);
		}
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
	close(in[0]);
	close(out[1]);
	if (err[1] >= 0)
		close(err[1]);
	fcntl(in[1], F_SETFD, FD_CLOEXEC);
	fcntl(out[0], F_SETFD, FD_CLOEXEC);
	if (err[0] >= 0)
		fcntl(err[0], F_SETFD, FD_CLOEXEC);
	p->pid = c;
	p->in_fd = in[1];
	stream_reset(&p->out, out[0]);
	stream_reset(&p->err, err[0]);
	p->reaped = p->end_told = false;
	p->status = 0;
	update_watch(id);
}

/* ------------------------------------------------------------- talking */

static void send(int64_t id, const char *text, bool line_end)
{
	Proc *p = proc(id);
	if (!p->pid || p->detached)
		panic("can't send to a process that %s", p->detached ? "is detached" : "hasn't been started");
	if (p->in_fd < 0)
		panic("can't send to '%s': its input has been ended", p->command);
	size_t n = strlen(text), all = n + (line_end ? 1 : 0);
	char *line = grow(NULL, all + 1);
	memcpy(line, text, n);
	line[n] = '\n';
	size_t done = 0;
	while (done < all) {
		ssize_t w = write(p->in_fd, line + done, all - done);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			free(line);
			panic("can't send to '%s': it has stopped reading (%s)", p->command,
			      errno == EPIPE ? "it has ended" : strerror(errno));
		}
		done += (size_t)w;
	}
	free(line);
}

void cby_process_send(int64_t id, const char *text) { send(id, text, true); }

/* the text as it is, with no line end after it */
void cby_process_write(int64_t id, const char *text) { send(id, text, false); }

void cby_process_end_input(int64_t id)
{
	Proc *p = proc(id);
	if (p->in_fd >= 0) {
		close(p->in_fd);
		p->in_fd = -1;
	}
}

static Stream *stream(int64_t id, int errors)
{
	Proc *p = proc(id);
	if (errors && !p->errors_apart)
		panic("'%s' has no error lines of its own: call keep_errors_apart before starting it", p->command ? p->command : "");
	return errors ? &p->err : &p->out;
}

/* The next line: true if there is one (it becomes the current line) */
int cby_process_take_line_of(int64_t id, int errors)
{
	Stream *s = stream(id, errors);
	if (!s->nlines)
		return 0;
	free(s->current);
	s->current = s->lines[s->first++];
	s->current_len = strlen(s->current);
	s->nlines--;
	if (!s->nlines)
		s->first = 0;
	return 1;
}

int cby_process_take_line(int64_t id) { return cby_process_take_line_of(id, 0); }

/* Waits for a line, or the end: true if there is one to take */
int cby_process_line_ready_of(int64_t id, int errors)
{
	Proc *p = proc(id);
	if (!p->pid && !p->detached)
		panic("can't read from a process that hasn't been started");
	Stream *s = stream(id, errors);
	while (!s->nlines && read_some(p, s, true)) {
		p = proc(id);
		s = errors ? &p->err : &p->out;
	}
	return s->nlines > 0;
}

int cby_process_line_ready(int64_t id) { return cby_process_line_ready_of(id, 0); }

long cby_process_line_length_of(int64_t id, int errors) { return (long)stream(id, errors)->current_len; }
char cby_process_line_char_of(int64_t id, int errors, long i) { return stream(id, errors)->current[i]; }
long cby_process_line_length(int64_t id) { return cby_process_line_length_of(id, 0); }
char cby_process_line_char(int64_t id, long i) { return cby_process_line_char_of(id, 0, i); }

/* clears the current line, at the end */
void cby_process_no_line_of(int64_t id, int errors)
{
	Stream *s = stream(id, errors);
	free(s->current);
	s->current = NULL;
	s->current_len = 0;
}

void cby_process_no_line(int64_t id) { cby_process_no_line_of(id, 0); }

/* ------------------------------------------------------------- the end */

int cby_process_is_running(int64_t id)
{
	Proc *p = proc(id);
	if (!p->pid || p->detached)
		return 0;
	reap(p, false);
	return !p->reaped;
}

int cby_process_wait(int64_t id)
{
	Proc *p = proc(id);
	if (!p->pid)
		panic("can't wait for a process that hasn't been started");
	if (p->detached)
		panic("can't wait for '%s': it is detached", p->command);
	if (!p->end_told) {
		/* nothing more will be sent: it may be waiting for that */
		cby_process_end_input(id);
		while (read_some(proc(id), NULL, true))
			deliver(id);
		deliver(id);
		p = proc(id);
	}
	reap(p, true);
	update_watch(id);
	return p->status;
}

int cby_process_status(int64_t id)
{
	Proc *p = proc(id);
	reap(p, false);
	return p->reaped ? p->status : -1;
}

void cby_process_stop(int64_t id)
{
	Proc *p = proc(id);
	if (!p->pid || p->detached || p->reaped)
		return;
	cby_process_end_input(id);
	kill(p->pid, SIGTERM);
	/* a moment to tidy up, then no more */
	for (int i = 0; i < 200; i++) {
		reap(p, false);
		if (p->reaped)
			break;
		struct timespec ts = { 0, 10 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
	if (!p->reaped) {
		kill(p->pid, SIGKILL);
		reap(p, true);
	}
}

static void stream_free(Stream *s)
{
	if (s->fd >= 0)
		close(s->fd);
	for (size_t i = 0; i < s->nlines; i++)
		free(s->lines[s->first + i]);
	free(s->lines);
	free(s->part);
	free(s->current);
}

/* The object is going: it waits for its process (unless detached), and
 * nothing here will call it again. */
void cby_process_free(int64_t id)
{
	if (id <= 0 || id > nprocs || !procs[id - 1].used)
		return;
	Proc *p = &procs[id - 1];
	p->deliver = p->deliver_err = p->ended = (Handler){ 0 };
	update_watch(id);
	p = &procs[id - 1];
	if (p->pid > 0 && !p->detached) {
		cby_process_end_input(id);
		while (read_some(p, NULL, true))
			;
		reap(p, true);
	}
	if (p->in_fd >= 0)
		close(p->in_fd);
	stream_free(&p->out);
	stream_free(&p->err);
	free(p->command);
	memset(p, 0, sizeof *p);
}
