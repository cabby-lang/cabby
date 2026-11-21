/* os.c — the os library's C side: what every system has, the same way on
 * Linux and macOS. Files and folders watched for changes, signals (Ctrl+C,
 * and a program being asked to stop), the disks and their space, how busy
 * the processors and memory are, and the programs that are running.
 *
 * Watching: on Linux, inotify, which says what changed; on a Mac, kqueue,
 * which says only that something in a folder did, so the folder is looked
 * at again and compared with how it was (CABBY_WATCH_BY_LOOKING=1 does
 * that on Linux too, so the tests try the Mac's way everywhere). Either
 * way the changes are told to the program's handler from the event loop:
 * "created", "changed", "deleted" or "renamed", and which file.
 *
 * Signals are caught by a handler that writes their number down a pipe,
 * and the pipe is watched by the event loop, so the program's own handler
 * runs there, as any other, rather than in the middle of whatever it was
 * doing. */
#include "rt.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#ifndef __wasi__
#include <pwd.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/attr.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>
#elif !defined(__wasi__)
#include <sys/inotify.h>
#include <sys/statvfs.h>
#endif

extern int cby_events_watch(int fd, void (*fn)(void *data), void *data);
extern int cby_events_watch_quietly(int fd, void (*fn)(void *data), void *data);
extern void cby_events_unwatch(int id);

typedef struct {
	void *obj, *fn;
} Hook;

/* ------------------------------------------------------------ text, to Cabby */

static _Thread_local char *given;

static void given_ends(void)
{
	free(given);
	given = NULL;
}

static int64_t give(char *t)
{
	static _Thread_local int hooked;
	if (!hooked) {
		hooked = 1;
		rt_on_thread_end(given_ends);
	}
	free(given);
	given = t ? t : strdup("");
	return (int64_t)strlen(given);
}

static int64_t give_copy(const char *t) { return give(strdup(t ? t : "")); }

void cby_os_take(char *dst)
{
	if (given)
		memcpy(dst, given, strlen(given));
}

static char *joined(const char *a, const char *b)
{
	size_t n = strlen(a) + strlen(b) + 2;
	char *p = malloc(n);
	snprintf(p, n, "%s%s%s", a, strcmp(a, "/") == 0 ? "" : "/", b);
	return p;
}

#ifndef __wasi__   /* (WebAssembly, in a browser: only a directory held open, below) */
/* ============================================================ watching */

typedef struct {
	char *name;
	int dir;
	long long mtime_ns, size, ino;
} Entry;

typedef struct {
	char *path;
	int wd;               /* inotify's (Linux); the folder's own file (kqueue) */
	int file;             /* a file watched by itself, not a folder */
	int alive;
	Entry *e;             /* how it was (watching by looking) */
	int n;
} Dir;

typedef struct {
	char *change, *path, *old;
} Change;

typedef struct {
	int fd;               /* inotify's, or kqueue's */
	int watch;            /* the event loop's */
	int all;              /* folders inside folders too */
	int looking;          /* by looking again, and comparing (a Mac's way) */
	Hook on_change;
	Dir *dirs;
	int ndirs;
	Change *queue;
	int nqueue, cqueue;
	Change now;           /* the change being told of */
#ifdef __APPLE__
	int *files;           /* (kqueue: each file in a watched folder, opened, so a change to it is heard) */
	int *file_dir;
	int nfiles;
#endif
} Watcher;

static Watcher **watchers;
static int64_t nwatchers;

static Watcher *watcher(int64_t id)
{
	if (id < 1 || id > nwatchers || !watchers[id - 1])
		rt_panic(NULL, 0, 0, "%s", "that watcher has stopped");
	return watchers[id - 1];
}

static void queue(Watcher *w, const char *change, const char *path, const char *old)
{
	/* (a file changed many times at once is told of once) */
	if (strcmp(change, "changed") == 0)
		for (int i = 0; i < w->nqueue; i++)
			if (strcmp(w->queue[i].path, path) == 0 &&
			    (strcmp(w->queue[i].change, "changed") == 0 || strcmp(w->queue[i].change, "created") == 0))
				return;
	if (w->nqueue == w->cqueue) {
		w->cqueue = w->cqueue ? w->cqueue * 2 : 16;
		w->queue = realloc(w->queue, sizeof *w->queue * (size_t)w->cqueue);
	}
	w->queue[w->nqueue++] = (Change){ strdup(change), strdup(path), strdup(old ? old : "") };
}

static void change_free(Change *c)
{
	free(c->change);
	free(c->path);
	free(c->old);
	*c = (Change){ 0 };
}

/* each change queued, to the handler (which may stop the watcher) */
static void tell(int64_t id)
{
	for (;;) {
		if (id < 1 || id > nwatchers || !watchers[id - 1])
			return;
		Watcher *w = watchers[id - 1];
		if (!w->nqueue)
			return;
		change_free(&w->now);
		w->now = w->queue[0];
		memmove(w->queue, w->queue + 1, sizeof *w->queue * (size_t)(w->nqueue - 1));
		w->nqueue--;
		if (w->on_change.fn)
			rt_invoke_hook(w->on_change.obj, w->on_change.fn);
	}
}

static int dir_index(Watcher *w, const char *path)
{
	for (int i = 0; i < w->ndirs; i++)
		if (w->dirs[i].alive && strcmp(w->dirs[i].path, path) == 0)
			return i;
	return -1;
}

/* ---- by looking: how a folder is now, and how that differs from before */

static int is_folder(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int by_name(const void *a, const void *b) { return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name); }

static Entry *look(const char *path, int file, int *n)
{
	*n = 0;
	Entry *e = NULL;
	int cap = 0;
	struct stat st;
	if (file) {
		if (stat(path, &st) != 0)
			return NULL;
		e = malloc(sizeof *e);
		const char *slash = strrchr(path, '/');
		e[0] = (Entry){ strdup(slash ? slash + 1 : path), 0, 0, (long long)st.st_size, (long long)st.st_ino };
#ifdef __APPLE__
		e[0].mtime_ns = (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
		e[0].mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
		*n = 1;
		return e;
	}
	DIR *d = opendir(path);
	if (!d)
		return NULL;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		char *full = joined(path, de->d_name);
		if (lstat(full, &st) == 0) {
			if (*n == cap) {
				cap = cap ? cap * 2 : 32;
				e = realloc(e, sizeof *e * (size_t)cap);
			}
			e[*n] = (Entry){ strdup(de->d_name), S_ISDIR(st.st_mode), 0, (long long)st.st_size, (long long)st.st_ino };
#ifdef __APPLE__
			e[*n].mtime_ns = (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
			e[*n].mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
			(*n)++;
		}
		free(full);
	}
	closedir(d);
	qsort(e, (size_t)*n, sizeof *e, by_name);
	return e;
}

static void entries_free(Entry *e, int n)
{
	for (int i = 0; i < n; i++)
		free(e[i].name);
	free(e);
}

static int add_dir(Watcher *w, const char *path, int file, int tell_inside);
static void dir_end(Watcher *w, Dir *d);
static void remove_dirs_under(Watcher *w, const char *path);

/* (a watched folder gone: what was in it, as it was last seen, is gone
 * too, each told of first, as inotify tells of them) */
static void gone_inside(Watcher *w, const char *path)
{
	int di = dir_index(w, path);
	if (di < 0)
		return;
	int n = w->dirs[di].n;
	char **names = malloc(sizeof *names * (size_t)(n + 1));
	int *dirs = malloc(sizeof *dirs * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		names[i] = joined(path, w->dirs[di].e[i].name);
		dirs[i] = w->dirs[di].e[i].dir;
	}
	for (int i = 0; i < n; i++) {
		if (dirs[i])
			gone_inside(w, names[i]);
		queue(w, "deleted", names[i], NULL);
		free(names[i]);
	}
	free(names);
	free(dirs);
	di = dir_index(w, path);
	if (di >= 0) {
		entries_free(w->dirs[di].e, w->dirs[di].n);
		w->dirs[di].e = NULL;
		w->dirs[di].n = 0;
	}
}

/* (a folder that's come: what's in it is new too) */
static void created_inside(Watcher *w, const char *path)
{
	int n;
	Entry *e = look(path, 0, &n);
	for (int i = 0; i < n; i++) {
		char *full = joined(path, e[i].name);
		queue(w, "created", full, NULL);
		if (e[i].dir && w->all && dir_index(w, full) < 0)
			add_dir(w, full, 0, 1);
		free(full);
	}
	entries_free(e, n);
}

/* the folder looked at again: what's come, gone and changed since */
static void look_again(Watcher *w, int di)
{
	Dir *d = &w->dirs[di];
	int n;
	Entry *now = look(d->path, d->file, &n);
	if (!now && (d->file || !is_folder(d->path))) {
		/* it's gone: a file watched by itself, or a folder (one inside a
		 * watched folder is told of by that folder) */
		char *parent = strdup(d->path);
		char *slash = strrchr(parent, '/');
		if (slash)
			*slash = 0;
		if (!d->file) {
			gone_inside(w, d->path);
			d = &w->dirs[di];
		}
		if ((d->file && d->n) || (!d->file && dir_index(w, parent) < 0))
			queue(w, "deleted", d->path, NULL);
		free(parent);
		dir_end(w, d);
		return;
	}
	char *path = strdup(d->path);
	int file = d->file;
	Entry *was = d->e;
	int nwas = d->n;
	d->e = now;
	d->n = n;
	if (file) {
		if (nwas && n && (was[0].mtime_ns != now[0].mtime_ns || was[0].size != now[0].size))
			queue(w, "changed", path, NULL);
		else if (!nwas && n)
			queue(w, "created", path, NULL);
		entries_free(was, nwas);
		free(path);
		return;
	}
	/* (both sorted by name: walked together) */
	int *gone = calloc((size_t)nwas + 1, sizeof *gone), *came = calloc((size_t)n + 1, sizeof *came);
	int i = 0, j = 0;
	while (i < nwas || j < n) {
		int c = i >= nwas ? 1 : j >= n ? -1 : strcmp(was[i].name, now[j].name);
		if (c < 0) {
			gone[i++] = 1;
		} else if (c > 0) {
			came[j++] = 1;
		} else {
			if (!now[j].dir && (was[i].mtime_ns != now[j].mtime_ns || was[i].size != now[j].size)) {
				char *full = joined(path, now[j].name);
				queue(w, "changed", full, NULL);
				free(full);
			}
			i++;
			j++;
		}
	}
	/* one gone and one come that are the same file: renamed */
	for (i = 0; i < nwas; i++) {
		if (!gone[i])
			continue;
		for (j = 0; j < n; j++)
			if (came[j] && now[j].ino == was[i].ino && now[j].dir == was[i].dir) {
				char *from = joined(path, was[i].name), *to = joined(path, now[j].name);
				queue(w, "renamed", to, from);
				if (now[j].dir && w->all) {
					remove_dirs_under(w, from);
					add_dir(w, to, 0, 0);
				}
				free(from);
				free(to);
				gone[i] = came[j] = 0;
				break;
			}
	}
	for (i = 0; i < nwas; i++)
		if (gone[i]) {
			char *full = joined(path, was[i].name);
			if (was[i].dir)
				gone_inside(w, full);
			queue(w, "deleted", full, NULL);
			if (was[i].dir)
				remove_dirs_under(w, full);
			free(full);
		}
	for (j = 0; j < n; j++)
		if (came[j]) {
			char *full = joined(path, now[j].name);
			queue(w, "created", full, NULL);
			if (now[j].dir && w->all) {
				add_dir(w, full, 0, 0);
				created_inside(w, full);
			}
			free(full);
		}
	free(gone);
	free(came);
	entries_free(was, nwas);
	free(path);
}

#ifdef __APPLE__
/* ---- kqueue (a Mac's): each folder, and each file in it, opened to be
 * heard from; what's heard says which folder to look at again */

static void kq_add(Watcher *w, int fd, intptr_t which)
{
	struct kevent k;
	EV_SET(&k, (uintptr_t)fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
	       NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME, 0, (void *)which);
	kevent(w->fd, &k, 1, NULL, 0, NULL);
}

/* the files in the watched folders, opened again (after a change) */
static void kq_files(Watcher *w)
{
	for (int i = 0; i < w->nfiles; i++)
		close(w->files[i]);
	w->nfiles = 0;
	for (int di = 0; di < w->ndirs; di++) {
		Dir *d = &w->dirs[di];
		if (!d->alive || d->file)
			continue;
		for (int k = 0; k < d->n; k++) {
			if (d->e[k].dir)
				continue;
			char *full = joined(d->path, d->e[k].name);
			int fd = open(full, O_EVTONLY);
			free(full);
			if (fd < 0)
				continue;
			w->files = realloc(w->files, sizeof *w->files * (size_t)(w->nfiles + 1));
			w->file_dir = realloc(w->file_dir, sizeof *w->file_dir * (size_t)(w->nfiles + 1));
			w->files[w->nfiles] = fd;
			w->file_dir[w->nfiles++] = di;
			kq_add(w, fd, di);
		}
	}
}
#endif

/* starts watching a folder (or a file): its index */
static int add_dir(Watcher *w, const char *path, int file, int tell_inside)
{
	(void)tell_inside;
	int wd = -1;
#ifdef __APPLE__
	wd = open(path, O_EVTONLY);
	if (wd < 0)
		return -1;
#else
	uint32_t mask = file ? IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB
			     : IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF |
				       IN_ATTRIB;
	wd = inotify_add_watch(w->fd, path, mask);
	if (wd < 0)
		return -1;
#endif
	w->dirs = realloc(w->dirs, sizeof *w->dirs * (size_t)(w->ndirs + 1));
	int di = w->ndirs++;
	w->dirs[di] = (Dir){ strdup(path), wd, file, 1, NULL, 0 };
	if (w->looking)
		w->dirs[di].e = look(path, file, &w->dirs[di].n);
#ifdef __APPLE__
	kq_add(w, wd, di);
#endif
	if (!file && w->all) {
		DIR *d = opendir(path);
		struct dirent *de;
		while (d && (de = readdir(d))) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			char *full = joined(path, de->d_name);
			struct stat st;
			if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode))
				add_dir(w, full, 0, 0);
			free(full);
		}
		if (d)
			closedir(d);
	}
	return di;
}

static void dir_end(Watcher *w, Dir *d)
{
	if (!d->alive)
		return;
#ifdef __APPLE__
	close(d->wd);
#else
	inotify_rm_watch(w->fd, d->wd);
#endif
	(void)w;
	d->alive = 0;
	entries_free(d->e, d->n);
	d->e = NULL;
	d->n = 0;
}

/* a folder gone (or moved): it and the folders inside it */
static void remove_dirs_under(Watcher *w, const char *path)
{
	size_t n = strlen(path);
	for (int i = 0; i < w->ndirs; i++) {
		Dir *d = &w->dirs[i];
		if (d->alive && strncmp(d->path, path, n) == 0 && (d->path[n] == 0 || d->path[n] == '/'))
			dir_end(w, d);
	}
}

#ifndef __APPLE__
static int dir_of_wd(Watcher *w, int wd)
{
	for (int i = 0; i < w->ndirs; i++)
		if (w->dirs[i].alive && w->dirs[i].wd == wd)
			return i;
	return -1;
}

/* inotify says what changed: each change, from that */
static void heard_inotify(Watcher *w)
{
	char buf[16384] __attribute__((aligned(__alignof__(struct inotify_event))));
	struct { uint32_t cookie; char *path; int dir; } moved[64];
	int nmoved = 0;
	int *again = NULL, nagain = 0;
	for (;;) {
		ssize_t len = read(w->fd, buf, sizeof buf);
		if (len <= 0)
			break;
		for (char *p = buf; p < buf + len;) {
			struct inotify_event *ev = (struct inotify_event *)p;
			p += sizeof *ev + ev->len;
			int di = dir_of_wd(w, ev->wd);
			if (di < 0)
				continue;
			if (w->looking) {
				/* (the Mac's way: only which folder, and look again) */
				int seen = 0;
				for (int k = 0; k < nagain; k++)
					seen |= again[k] == di;
				if (!seen) {
					again = realloc(again, sizeof *again * (size_t)(nagain + 1));
					again[nagain++] = di;
				}
				continue;
			}
			Dir *d = &w->dirs[di];
			char *path = ev->len && ev->name[0] ? joined(d->path, ev->name) : strdup(d->path);
			int isdir = (ev->mask & IN_ISDIR) != 0;
			if (d->file) {
				if (ev->mask & (IN_MODIFY | IN_CLOSE_WRITE))
					queue(w, "changed", path, NULL);
				else if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
					queue(w, "deleted", path, NULL);
					dir_end(w, &w->dirs[di]);
				}
			} else if (ev->mask & IN_CREATE) {
				queue(w, "created", path, NULL);
				if (isdir && w->all && dir_index(w, path) < 0) {
					add_dir(w, path, 0, 0);
					created_inside(w, path);   /* (what came before it was watched) */
				}
			} else if (ev->mask & IN_DELETE) {
				queue(w, "deleted", path, NULL);
				if (isdir)
					remove_dirs_under(w, path);
			} else if (ev->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
				if (!isdir)
					queue(w, "changed", path, NULL);
			} else if (ev->mask & IN_MOVED_FROM) {
				if (nmoved < 64) {
					moved[nmoved].cookie = ev->cookie;
					moved[nmoved].path = path;
					moved[nmoved++].dir = isdir;
					path = NULL;
				} else {
					queue(w, "deleted", path, NULL);
				}
			} else if (ev->mask & IN_MOVED_TO) {
				int k = 0;
				while (k < nmoved && moved[k].cookie != ev->cookie)
					k++;
				if (k < nmoved) {
					queue(w, "renamed", path, moved[k].path);
					if (isdir && w->all) {
						remove_dirs_under(w, moved[k].path);
						add_dir(w, path, 0, 0);
					}
					free(moved[k].path);
					moved[k] = moved[--nmoved];
				} else {
					queue(w, "created", path, NULL);   /* (moved here from outside) */
					if (isdir && w->all) {
						add_dir(w, path, 0, 0);
						created_inside(w, path);
					}
				}
			} else if (ev->mask & IN_DELETE_SELF) {
				/* a folder watched for itself, gone (one inside another is told
				 * of by the folder it was in) */
				char *parent = strdup(d->path);
				char *slash = strrchr(parent, '/');
				if (slash)
					*slash = 0;
				if (dir_index(w, parent) < 0)
					queue(w, "deleted", d->path, NULL);
				free(parent);
				dir_end(w, &w->dirs[di]);
			}
			free(path);
		}
	}
	/* moved away, out of what's watched: gone */
	for (int k = 0; k < nmoved; k++) {
		queue(w, "deleted", moved[k].path, NULL);
		if (moved[k].dir)
			remove_dirs_under(w, moved[k].path);
		free(moved[k].path);
	}
	for (int k = 0; k < nagain; k++)
		if (w->dirs[again[k]].alive)
			look_again(w, again[k]);
	free(again);
}
#endif

static void heard(void *data)
{
	int64_t id = (int64_t)(intptr_t)data;
	if (id < 1 || id > nwatchers || !watchers[id - 1])
		return;
	Watcher *w = watchers[id - 1];
#ifdef __APPLE__
	struct kevent got[64];
	struct timespec none = { 0, 0 };
	int n = kevent(w->fd, NULL, 0, got, 64, &none);
	int *again = NULL, nagain = 0;
	for (int i = 0; i < n; i++) {
		int di = (int)(intptr_t)got[i].udata;
		int seen = 0;
		for (int k = 0; k < nagain; k++)
			seen |= again[k] == di;
		if (!seen && di >= 0 && di < w->ndirs) {
			again = realloc(again, sizeof *again * (size_t)(nagain + 1));
			again[nagain++] = di;
		}
	}
	for (int k = 0; k < nagain; k++)
		if (w->dirs[again[k]].alive)
			look_again(w, again[k]);
	free(again);
	if (nagain)
		kq_files(w);
#else
	heard_inotify(w);
#endif
	/* (nothing left to watch: it no longer keeps events.run waiting) */
	int any = 0;
	for (int i = 0; i < w->ndirs; i++)
		any |= w->dirs[i].alive;
	if (!any && w->watch) {
		cby_events_unwatch(w->watch);
		w->watch = 0;
	}
	tell(id);
}

int64_t cby_os_watcher_new(void)
{
	Watcher *w = calloc(1, sizeof *w);
#ifdef __APPLE__
	w->fd = kqueue();
	w->looking = 1;
#else
	w->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	const char *how = getenv("CABBY_WATCH_BY_LOOKING");
	w->looking = how && *how && strcmp(how, "0") != 0;
#endif
	if (w->fd < 0) {
		free(w);
		rt_panic(NULL, 0, 0, "%s", "files can't be watched here: the system said no (too many watched already?)");
	}
	fcntl(w->fd, F_SETFD, FD_CLOEXEC);
	int64_t i = 0;
	while (i < nwatchers && watchers[i])
		i++;
	if (i == nwatchers) {
		watchers = realloc(watchers, sizeof *watchers * (size_t)(nwatchers + 1));
		nwatchers++;
	}
	watchers[i] = w;
	return i + 1;
}

/* a file, or a folder and what's in it (all: and the folders inside it) */
void cby_os_watch(int64_t id, const char *path, int all)
{
	Watcher *w = watcher(id);
	struct stat st;
	if (stat(path, &st) != 0)
		rt_panic(NULL, 0, 0, "can't watch '%s': it isn't there", path);
	if (all && !S_ISDIR(st.st_mode))
		rt_panic(NULL, 0, 0, "can't watch everything in '%s': it isn't a folder", path);
	if (all)
		w->all = 1;
	/* (without a trailing slash, so paths told are as given) */
	char *clean = strdup(path);
	size_t n = strlen(clean);
	while (n > 1 && clean[n - 1] == '/')
		clean[--n] = 0;
	if (dir_index(w, clean) < 0 && add_dir(w, clean, !S_ISDIR(st.st_mode), 0) < 0) {
		free(clean);
		rt_panic(NULL, 0, 0, "can't watch '%s': %s", path, strerror(errno));
	}
	free(clean);
#ifdef __APPLE__
	kq_files(w);
#endif
	if (!w->watch)
		w->watch = cby_events_watch(w->fd, heard, (void *)(intptr_t)id);
}

void cby_os_watcher_on_change(int64_t id, void *obj, void *fn) { watcher(id)->on_change = (Hook){ obj, fn }; }

/* 0 what changed, 1 the file, 2 what it was called (renamed) */
int64_t cby_os_watcher_text(int64_t id, int64_t what)
{
	Watcher *w = watcher(id);
	return give_copy(what == 0 ? w->now.change : what == 1 ? w->now.path : w->now.old);
}

/* waits for the next change (as the event loop would): it's told by then */
void cby_os_watcher_stop(int64_t id)
{
	if (id < 1 || id > nwatchers || !watchers[id - 1])
		return;
	Watcher *w = watchers[id - 1];
	if (w->watch)
		cby_events_unwatch(w->watch);
	for (int i = 0; i < w->ndirs; i++) {
		dir_end(w, &w->dirs[i]);
		free(w->dirs[i].path);
	}
	free(w->dirs);
#ifdef __APPLE__
	for (int i = 0; i < w->nfiles; i++)
		close(w->files[i]);
	free(w->files);
	free(w->file_dir);
#endif
	close(w->fd);
	for (int i = 0; i < w->nqueue; i++)
		change_free(&w->queue[i]);
	free(w->queue);
	change_free(&w->now);
	free(w);
	watchers[id - 1] = NULL;
}

/* ============================================================ signals */

static int sig_pipe[2] = { -1, -1 };
static int sig_watch;
static Hook sig_hooks[65];
static int64_t sig_owner[65];   /* (the os.signals object's number) */
static int64_t sig_owners;
static int sig_came[65];
static int sig_now;

static const struct {
	const char *name;
	int sig;
	int catchable;
} SIGNALS[] = {
	{ "interrupt", SIGINT, 1 }, { "terminate", SIGTERM, 1 }, { "hangup", SIGHUP, 1 }, { "quit", SIGQUIT, 1 },
	{ "user1", SIGUSR1, 1 }, { "user2", SIGUSR2, 1 }, { "resize", SIGWINCH, 1 }, { "child", SIGCHLD, 1 },
	{ "alarm", SIGALRM, 1 }, { "pause", SIGSTOP, 0 }, { "resume", SIGCONT, 1 }, { "kill", SIGKILL, 0 },
};

static int signal_of(const char *name, int catching)
{
	for (size_t i = 0; i < sizeof SIGNALS / sizeof *SIGNALS; i++)
		if (strcmp(SIGNALS[i].name, name) == 0) {
			if (catching && !SIGNALS[i].catchable)
				rt_panic(NULL, 0, 0, "the '%s' signal can't be caught: the system does it, whatever the program says", name);
			return SIGNALS[i].sig;
		}
	rt_panic(NULL, 0, 0, "there's no signal called '%s': there's interrupt (Ctrl+C), terminate, hangup, quit, user1, user2, resize, child, alarm, resume, and (to send) pause and kill", name);
	return 0;
}

static const char *name_of(int sig)
{
	for (size_t i = 0; i < sizeof SIGNALS / sizeof *SIGNALS; i++)
		if (SIGNALS[i].sig == sig)
			return SIGNALS[i].name;
	return "";
}

static void on_any_signal(int sig)
{
	int saved = errno;
	unsigned char b = (unsigned char)sig;
	if (write(sig_pipe[1], &b, 1) < 0) {
	}
	errno = saved;
}

/* what's come down the pipe: counted, and told to the handlers */
static void signals_heard(void *unused)
{
	(void)unused;
	unsigned char got[64];
	ssize_t n;
	while ((n = read(sig_pipe[0], got, sizeof got)) > 0)
		for (ssize_t i = 0; i < n; i++) {
			int sig = got[i];
			if (sig < 1 || sig > 64)
				continue;
			sig_came[sig]++;
			if (sig_hooks[sig].fn) {
				sig_now = sig;
				rt_invoke_hook(sig_hooks[sig].obj, sig_hooks[sig].fn);
			}
		}
}

static void catch_it(int sig)
{
	if (sig_pipe[0] < 0) {
		if (pipe(sig_pipe) != 0)
			rt_panic(NULL, 0, 0, "%s", "signals can't be caught: the system wouldn't make a pipe");
		for (int i = 0; i < 2; i++) {
			fcntl(sig_pipe[i], F_SETFL, fcntl(sig_pipe[i], F_GETFL) | O_NONBLOCK);
			fcntl(sig_pipe[i], F_SETFD, FD_CLOEXEC);
		}
		sig_watch = cby_events_watch_quietly(sig_pipe[0], signals_heard, NULL);
	}
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_any_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(sig, &sa, NULL);
}

/* an os.signals object: its number */
int64_t cby_os_signals_new(void) { return ++sig_owners; }

/* the object's: the signal caught, and told to its method */
void cby_os_on_signal(int64_t owner, const char *name, void *obj, void *fn)
{
	int sig = signal_of(name, 1);
	sig_hooks[sig] = (Hook){ obj, fn };
	sig_owner[sig] = owner;
	catch_it(sig);
}

/* the object's gone: its signals are as they were */
void cby_os_signals_drop(int64_t owner)
{
	for (int sig = 1; sig <= 64; sig++)
		if (sig_hooks[sig].fn && sig_owner[sig] == owner) {
			sig_hooks[sig] = (Hook){ 0 };
			sig_owner[sig] = 0;
			signal(sig, SIG_DFL);
		}
}

void cby_os_catch_signal(const char *name) { catch_it(signal_of(name, 1)); }

/* whether it's come since last asked (caught from the first time asked) */
int cby_os_got_signal(const char *name)
{
	int sig = signal_of(name, 1);
	if (sig_pipe[0] < 0 || !sig_hooks[sig].fn) {
		struct sigaction now;
		sigaction(sig, NULL, &now);
		if (now.sa_handler != on_any_signal)
			catch_it(sig);
	}
	signals_heard(NULL);
	int came = sig_came[sig] > 0;
	sig_came[sig] = 0;
	return came;
}

void cby_os_ignore_signal(const char *name)
{
	int sig = signal_of(name, 1);
	sig_hooks[sig] = (Hook){ 0 };
	signal(sig, SIG_IGN);
}

/* as it was: Ctrl+C ends the program again */
void cby_os_usual_signal(const char *name)
{
	int sig = signal_of(name, 1);
	sig_hooks[sig] = (Hook){ 0 };
	signal(sig, SIG_DFL);
}

int64_t cby_os_signal_name(void) { return give_copy(name_of(sig_now)); }

int cby_os_send_signal(int64_t pid, const char *name)
{
	if (pid <= 0)
		rt_panic(NULL, 0, 0, "%s", "a program's number is 1 or more");
	return kill((pid_t)pid, signal_of(name, 0)) == 0;
}

int64_t cby_os_pid(int64_t which) { return which ? (int64_t)getppid() : (int64_t)getpid(); }

/* ============================================================ disks */

typedef struct {
	char *path, *device, *kind, *name;
	long long size, free_, used;
	int read_only, removable;
} Disk;

static Disk *disks;
static int ndisks;

static void disks_forget(void)
{
	for (int i = 0; i < ndisks; i++) {
		free(disks[i].path);
		free(disks[i].device);
		free(disks[i].kind);
		free(disks[i].name);
	}
	free(disks);
	disks = NULL;
	ndisks = 0;
}

/* (false if it can't be looked at: errno says why) */
static int space_of(const char *path, long long *size, long long *free_, long long *used, int *ro)
{
	*size = *free_ = *used = 0;
#ifdef __APPLE__
	struct statfs s;
	if (statfs(path, &s) != 0)
		return 0;
	*size = (long long)s.f_blocks * (long long)s.f_bsize;
	*free_ = (long long)s.f_bavail * (long long)s.f_bsize;
	*used = (long long)(s.f_blocks - s.f_bfree) * (long long)s.f_bsize;
	if (ro)
		*ro = (s.f_flags & MNT_RDONLY) != 0;
#else
	struct statvfs s;
	if (statvfs(path, &s) != 0)
		return 0;
	*size = (long long)s.f_blocks * (long long)s.f_frsize;
	*free_ = (long long)s.f_bavail * (long long)s.f_frsize;
	*used = (long long)(s.f_blocks - s.f_bfree) * (long long)s.f_frsize;
	if (ro)
		*ro = (s.f_flag & ST_RDONLY) != 0;
#endif
	return 1;
}

static void add_disk(const char *path, const char *device, const char *kind, const char *name, int removable)
{
	disks = realloc(disks, sizeof *disks * (size_t)(ndisks + 1));
	Disk *d = &disks[ndisks++];
	d->path = strdup(path);
	d->device = strdup(device);
	d->kind = strdup(kind);
	d->name = strdup(name);
	d->removable = removable;
	d->read_only = 0;
	if (!space_of(path, &d->size, &d->free_, &d->used, &d->read_only) && (errno == EACCES || errno == EPERM)) {
		/* someone else's: a share mounted in another person's home, which
		 * this person can't look into (as df leaves it out) */
		free(d->path);
		free(d->device);
		free(d->kind);
		free(d->name);
		ndisks--;
	}
}

#ifndef __APPLE__
/* /proc/self/mounts writes spaces (and the like) as \040 */
static void unescape(char *s)
{
	char *o = s;
	for (char *p = s; *p; p++) {
		if (p[0] == '\\' && p[1] >= '0' && p[1] <= '7' && p[2] >= '0' && p[2] <= '7' && p[3] >= '0' && p[3] <= '7') {
			*o++ = (char)((p[1] - '0') * 64 + (p[2] - '0') * 8 + (p[3] - '0'));
			p += 3;
		} else {
			*o++ = *p;
		}
	}
	*o = 0;
}

/* a device's label (/dev/disk/by-label), or "" */
static char *label_of(const char *device)
{
	char real[PATH_MAX];
	if (!realpath(device, real))
		return strdup("");
	DIR *d = opendir("/dev/disk/by-label");
	struct dirent *de;
	char *found = NULL;
	while (d && !found && (de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		char link[PATH_MAX + 32], to[PATH_MAX];
		snprintf(link, sizeof link, "/dev/disk/by-label/%s", de->d_name);
		if (realpath(link, to) && strcmp(to, real) == 0) {
			/* ("\x20" for a space, as udev writes it) */
			found = strdup(de->d_name);
			char *o = found;
			for (char *p = found; *p; p++) {
				if (p[0] == '\\' && p[1] == 'x' && p[2] && p[3]) {
					char hex[3] = { p[2], p[3], 0 };
					*o++ = (char)strtol(hex, NULL, 16);
					p += 3;
				} else {
					*o++ = *p;
				}
			}
			*o = 0;
		}
	}
	if (d)
		closedir(d);
	return found ? found : strdup("");
}

/* whether its disk can be taken out: a USB stick, an SD card */
static int removable_of(const char *device)
{
	char real[PATH_MAX];
	if (!realpath(device, real) || strncmp(real, "/dev/", 5) != 0)
		return 0;
	char sys[PATH_MAX + 64], where[PATH_MAX];
	snprintf(sys, sizeof sys, "/sys/class/block/%s", real + 5);
	if (!realpath(sys, where))
		return 0;
	if (strstr(where, "/usb") || strstr(where, "/mmc"))
		return 1;
	/* (a partition's disk is the folder above it) */
	for (int up = 0; up < 2; up++) {
		char f[PATH_MAX + 32];
		snprintf(f, sizeof f, "%s/%sremovable", where, up ? "../" : "");
		FILE *fp = fopen(f, "r");
		if (fp) {
			int c = fgetc(fp);
			fclose(fp);
			if (c == '1')
				return 1;
		}
	}
	return 0;
}
#endif

/* the disks: a snapshot, its count */
int64_t cby_os_disks(void)
{
	disks_forget();
#ifdef __APPLE__
	struct statfs *m;
	int n = getmntinfo(&m, MNT_NOWAIT);
	for (int i = 0; i < n; i++) {
		if (strncmp(m[i].f_mntfromname, "/dev/disk", 9) != 0 && strncmp(m[i].f_fstypename, "smbfs", 5) != 0 &&
		    strncmp(m[i].f_fstypename, "nfs", 3) != 0 && strncmp(m[i].f_fstypename, "afpfs", 5) != 0)
			continue;
		if (m[i].f_flags & MNT_DONTBROWSE)
			continue;   /* (the system's own: Preboot, VM, Update ...) */
		char name[256] = "";
		struct {
			uint32_t len;
			attrreference_t ref;
			char buf[MAXPATHLEN];
		} __attribute__((packed)) got;
		struct attrlist al = { ATTR_BIT_MAP_COUNT, 0, 0, ATTR_VOL_INFO | ATTR_VOL_NAME, 0, 0, 0 };
		if (getattrlist(m[i].f_mntonname, &al, &got, sizeof got, 0) == 0)
			snprintf(name, sizeof name, "%s", (char *)&got.ref + got.ref.attr_dataoffset);
		const char *slash = strrchr(m[i].f_mntonname, '/');
		if (!*name)
			snprintf(name, sizeof name, "%s", slash && slash[1] ? slash + 1 : m[i].f_mntonname);
		add_disk(m[i].f_mntonname, m[i].f_mntfromname, m[i].f_fstypename, name,
			 strncmp(m[i].f_mntonname, "/Volumes/", 9) == 0 && !(m[i].f_flags & MNT_ROOTFS));
	}
#else
	FILE *f = fopen("/proc/self/mounts", "r");
	char line[4096];
	while (f && fgets(line, sizeof line, f)) {
		char dev[1024], mnt[2048], kind[256];
		if (sscanf(line, "%1023s %2047s %255s", dev, mnt, kind) != 3)
			continue;
		unescape(dev);
		unescape(mnt);
		/* (a container's own "/" is an overlay, or the like: it's still the
		 * disk its files are on) */
		int real = dev[0] == '/' || strcmp(mnt, "/") == 0 || strstr(kind, "nfs") || strstr(kind, "cifs") || strstr(kind, "smb") ||
			   (strncmp(kind, "fuse.", 5) == 0 && strcmp(kind, "fuse.portal") != 0 && strcmp(kind, "fuse.gvfsd-fuse") != 0);
		if (!real || strcmp(kind, "squashfs") == 0 || strncmp(dev, "/dev/loop", 9) == 0)
			continue;   /* (the system's own: /proc, tmpfs, snaps ...) */
		int seen = 0;
		for (int i = 0; i < ndisks && !seen; i++)
			seen = strcmp(disks[i].path, mnt) == 0;
		if (seen)
			continue;
		char *label = dev[0] == '/' ? label_of(dev) : strdup("");
		const char *slash = strrchr(mnt, '/');
		add_disk(mnt, dev, kind, *label ? label : strcmp(mnt, "/") == 0 ? "/" : slash ? slash + 1 : mnt,
			 dev[0] == '/' ? removable_of(dev) : 0);
		free(label);
	}
	if (f)
		fclose(f);
#endif
	return ndisks;
}

static Disk *disk_at(int64_t i)
{
	if (i < 0 || i >= ndisks)
		rt_panic(NULL, 0, 0, "%s", "there's no disk there");
	return &disks[i];
}

/* 0 where it's mounted, 1 its device, 2 its kind, 3 its name */
int64_t cby_os_disk_text(int64_t i, int64_t what)
{
	Disk *d = disk_at(i);
	return give_copy(what == 0 ? d->path : what == 1 ? d->device : what == 2 ? d->kind : d->name);
}

/* 0 size, 1 free, 2 used, 3 read only, 4 removable */
int64_t cby_os_disk_number(int64_t i, int64_t what)
{
	Disk *d = disk_at(i);
	switch (what) {
	case 0: return d->size;
	case 1: return d->free_;
	case 2: return d->used;
	case 3: return d->read_only;
	case 4: return d->removable;
	}
	return 0;
}

/* the space where a path is: 0 size, 1 free (for this person), 2 used */
int64_t cby_os_space(const char *path, int64_t what)
{
	struct stat st;
	if (stat(path, &st) != 0)
		rt_panic(NULL, 0, 0, "there's no '%s', to say how much space is there", path);
	long long size, free_, used;
	space_of(path, &size, &free_, &used, NULL);
	return what == 0 ? size : what == 1 ? free_ : used;
}

/* ============================================================ processors and memory */

/* all the processors' time so far: busy, and in all */
static void cpu_times(unsigned long long *busy, unsigned long long *total)
{
	*busy = *total = 0;
#ifdef __APPLE__
	host_cpu_load_info_data_t info;
	mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
	if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&info, &count) != KERN_SUCCESS)
		return;
	unsigned long long user = info.cpu_ticks[CPU_STATE_USER], sys = info.cpu_ticks[CPU_STATE_SYSTEM];
	unsigned long long idle = info.cpu_ticks[CPU_STATE_IDLE], nice = info.cpu_ticks[CPU_STATE_NICE];
	*busy = user + sys + nice;
	*total = user + sys + nice + idle;
#else
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return;
	unsigned long long v[10] = { 0 };
	int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
	fclose(f);
	if (n < 4)
		return;
	for (int i = 0; i < 8; i++)
		*total += v[i];
	*busy = *total - v[3] - v[4];   /* (less idle, and waiting for disks) */
#endif
}

/* how busy the processors are, all together, from 0 to 100: since it was
 * last asked (the first time, over a tenth of a second) */
double cby_os_cpu_use(void)
{
	static unsigned long long last_busy, last_total;
	static int asked;
	unsigned long long busy, total;
	if (!asked) {
		cpu_times(&last_busy, &last_total);
		bool was = rt_gil_drop();
		struct timespec t = { 0, 100000000 };
		nanosleep(&t, NULL);
		rt_gil_take(was);
		asked = 1;
	}
	cpu_times(&busy, &total);
	double use = total > last_total ? 100.0 * (double)(busy - last_busy) / (double)(total - last_total) : 0;
	last_busy = busy;
	last_total = total;
	return use < 0 ? 0 : use > 100 ? 100 : use;
}

/* 0 memory in all, 1 in use, 2 available, 3 swap in all, 4 swap in use */
int64_t cby_os_memory(int64_t what)
{
#ifdef __APPLE__
	uint64_t total = 0;
	size_t len = sizeof total;
	sysctlbyname("hw.memsize", &total, &len, NULL, 0);
	if (what == 0)
		return (int64_t)total;
	if (what >= 3) {
		struct xsw_usage sw;
		len = sizeof sw;
		if (sysctlbyname("vm.swapusage", &sw, &len, NULL, 0) != 0)
			return 0;
		return (int64_t)(what == 3 ? sw.xsu_total : sw.xsu_used);
	}
	vm_statistics64_data_t vm;
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &count) != KERN_SUCCESS)
		return 0;
	vm_size_t page;
	host_page_size(mach_host_self(), &page);
	/* in use as Activity Monitor says: apps', wired, and compressed */
	uint64_t used = ((uint64_t)vm.internal_page_count - vm.purgeable_count + vm.wire_count + vm.compressor_page_count) * page;
	if (used > total)
		used = total;
	return (int64_t)(what == 1 ? used : total - used);
#else
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return 0;
	long long total = 0, avail = -1, free_ = 0, buffers = 0, cached = 0, swap_total = 0, swap_free = 0;
	char name[64];
	long long kb;
	while (fscanf(f, "%63s %lld kB\n", name, &kb) == 2) {
		if (strcmp(name, "MemTotal:") == 0) total = kb;
		else if (strcmp(name, "MemAvailable:") == 0) avail = kb;
		else if (strcmp(name, "MemFree:") == 0) free_ = kb;
		else if (strcmp(name, "Buffers:") == 0) buffers = kb;
		else if (strcmp(name, "Cached:") == 0) cached = kb;
		else if (strcmp(name, "SwapTotal:") == 0) swap_total = kb;
		else if (strcmp(name, "SwapFree:") == 0) swap_free = kb;
	}
	fclose(f);
	if (avail < 0)
		avail = free_ + buffers + cached;
	switch (what) {
	case 0: return total * 1024;
	case 1: return (total - avail) * 1024;
	case 2: return avail * 1024;
	case 3: return swap_total * 1024;
	case 4: return (swap_total - swap_free) * 1024;
	}
	return 0;
#endif
}

/* how many are waiting for a processor, on average: over the last 0 one
 * minute, 1 five, 2 fifteen */
double cby_os_load(int64_t which)
{
	double l[3] = { 0, 0, 0 };
	if (getloadavg(l, 3) < 0 || which < 0 || which > 2)
		return 0;
	return l[which];
}

/* ============================================================ programs running */

typedef struct {
	long long pid, ppid, uid, memory;
	double cpu;
	char *name, *command, *user;
} Program;

static Program *programs;
static int nprograms;

static void programs_forget(void)
{
	for (int i = 0; i < nprograms; i++) {
		free(programs[i].name);
		free(programs[i].command);
		free(programs[i].user);
	}
	free(programs);
	programs = NULL;
	nprograms = 0;
}

static char *user_of(long long uid)
{
	struct passwd *p = getpwuid((uid_t)uid);
	if (p)
		return strdup(p->pw_name);
	char n[32];
	snprintf(n, sizeof n, "%lld", uid);
	return strdup(n);
}

static void add_program(long long pid, long long ppid, long long uid, long long memory, double cpu, const char *name,
			const char *command)
{
	programs = realloc(programs, sizeof *programs * (size_t)(nprograms + 1));
	programs[nprograms++] = (Program){ pid, ppid, uid, memory, cpu, strdup(name), strdup(command), user_of(uid) };
}

#ifndef __APPLE__
static char *read_small(const char *path, size_t *len)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;
	size_t cap = 4096, n = 0, r;
	char *s = malloc(cap);
	while ((r = fread(s + n, 1, cap - n - 1, f)) > 0) {
		n += r;
		if (cap - n < 2) {
			cap *= 2;
			s = realloc(s, cap);
		}
	}
	fclose(f);
	s[n] = 0;
	if (len)
		*len = n;
	return s;
}

static void read_program(long long pid)
{
	char path[64];
	snprintf(path, sizeof path, "/proc/%lld/stat", pid);
	char *stat_ = read_small(path, NULL);
	if (!stat_)
		return;
	/* "123 (its name) S 1 ...": the name may have spaces, or parentheses */
	char *open_ = strchr(stat_, '('), *close_ = strrchr(stat_, ')');
	if (!open_ || !close_ || close_ < open_) {
		free(stat_);
		return;
	}
	*close_ = 0;
	char *name = open_ + 1;
	char state;
	long long ppid = 0, rss = 0;
	unsigned long long utime = 0, stime = 0;
	/* (after the name: state ppid pgrp session tty tpgid flags minflt
	 * cminflt majflt cmajflt utime stime cutime cstime priority nice
	 * threads itrealvalue starttime vsize rss) */
	sscanf(close_ + 2, "%c %lld %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %*d %*d %*u %*u %lld", &state,
	       &ppid, &utime, &stime, &rss);
	snprintf(path, sizeof path, "/proc/%lld/cmdline", pid);
	size_t len = 0;
	char *cmd = read_small(path, &len);
	if (!cmd || !len) {
		/* (the kernel's own threads: no command) */
		free(cmd);
		free(stat_);
		return;
	}
	while (len && cmd[len - 1] == 0)
		len--;
	for (size_t i = 0; i < len; i++)
		if (cmd[i] == 0)
			cmd[i] = ' ';
	cmd[len] = 0;
	snprintf(path, sizeof path, "/proc/%lld", pid);
	struct stat st;
	long long uid = stat(path, &st) == 0 ? (long long)st.st_uid : -1;
	double ticks = (double)sysconf(_SC_CLK_TCK);
	add_program(pid, ppid, uid, rss * (long long)sysconf(_SC_PAGESIZE), (double)(utime + stime) / (ticks > 0 ? ticks : 100), name,
		    cmd);
	free(cmd);
	free(stat_);
}
#endif

#ifdef __APPLE__
/* how a program was started, its arguments joined by spaces, as Linux's
 * /proc/<pid>/cmdline gives it: the kernel's KERN_PROCARGS2 (the number
 * of arguments, the program's path, then each argument). NULL when it
 * can't be read (another person's program). */
static char *mac_arguments(pid_t pid)
{
	int mib[3] = { CTL_KERN, KERN_ARGMAX, 0 };
	int argmax = 0;
	size_t size = sizeof argmax;
	if (sysctl(mib, 2, &argmax, &size, NULL, 0) != 0 || argmax <= 0)
		return NULL;
	char *buf = malloc((size_t)argmax);
	mib[1] = KERN_PROCARGS2;
	mib[2] = pid;
	size = (size_t)argmax;
	if (sysctl(mib, 3, buf, &size, NULL, 0) != 0 || size < sizeof(int)) {
		free(buf);
		return NULL;
	}
	int argc;
	memcpy(&argc, buf, sizeof argc);
	char *p = buf + sizeof argc, *end = buf + size;
	while (p < end && *p)   /* (the program's path) */
		p++;
	while (p < end && !*p)
		p++;
	size_t cap = 256, n = 0;
	char *out = malloc(cap);
	for (int a = 0; a < argc && p < end; a++) {
		size_t len = strnlen(p, (size_t)(end - p));
		if (n + len + 2 > cap)
			out = realloc(out, cap = (n + len + 2) * 2);
		if (a)
			out[n++] = ' ';
		memcpy(out + n, p, len);
		n += len;
		p += len + 1;
	}
	out[n] = 0;
	free(buf);
	if (!n) {
		free(out);
		return NULL;
	}
	return out;
}
#endif

/* the programs running: a snapshot, its count */
int64_t cby_os_programs(void)
{
	programs_forget();
#ifdef __APPLE__
	int n = proc_listallpids(NULL, 0);
	if (n <= 0)
		return 0;
	pid_t *pids = calloc((size_t)n * 2, sizeof *pids);
	n = proc_listallpids(pids, (int)(sizeof *pids * (size_t)n * 2));
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	for (int i = 0; i < n; i++) {
		if (pids[i] <= 0)
			continue;
		struct proc_bsdinfo bsd;
		if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &bsd, sizeof bsd) != (int)sizeof bsd)
			continue;
		long long memory = 0;
		double cpu = 0;
		struct proc_taskinfo task;
		if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &task, sizeof task) == (int)sizeof task) {
			memory = (long long)task.pti_resident_size;
			/* (in the processor's own time units, on Apple's processors) */
			double ns = (double)(task.pti_total_user + task.pti_total_system) * tb.numer / tb.denom;
			cpu = ns / 1e9;
		}
		char path[PROC_PIDPATHINFO_MAXSIZE] = "";
		proc_pidpath(pids[i], path, sizeof path);
		const char *name = bsd.pbi_name[0] ? bsd.pbi_name : bsd.pbi_comm;
		char *args = mac_arguments(pids[i]);
		add_program(pids[i], bsd.pbi_ppid, bsd.pbi_uid, memory, cpu, name, args ? args : *path ? path : name);
		free(args);
	}
	free(pids);
#else
	DIR *d = opendir("/proc");
	struct dirent *de;
	while (d && (de = readdir(d))) {
		char *end;
		long long pid = strtoll(de->d_name, &end, 10);
		if (*end || pid <= 0)
			continue;
		read_program(pid);
	}
	if (d)
		closedir(d);
#endif
	return nprograms;
}

static Program *program_at(int64_t i)
{
	if (i < 0 || i >= nprograms)
		rt_panic(NULL, 0, 0, "%s", "there's no program there");
	return &programs[i];
}

/* 0 its number, 1 its parent's, 2 its user's number, 3 its memory */
int64_t cby_os_program_number(int64_t i, int64_t what)
{
	Program *p = program_at(i);
	return what == 0 ? p->pid : what == 1 ? p->ppid : what == 2 ? p->uid : p->memory;
}

double cby_os_program_cpu(int64_t i) { return program_at(i)->cpu; }

/* 0 its name, 1 its command, 2 its user */
int64_t cby_os_program_text(int64_t i, int64_t what)
{
	Program *p = program_at(i);
	return give_copy(what == 0 ? p->name : what == 1 ? p->command : p->user);
}

/* whether a program with that number is running */
int cby_os_running(int64_t pid) { return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM); }

__attribute__((destructor)) static void os_end(void)
{
	for (int64_t i = 0; i < nwatchers; i++)
		cby_os_watcher_stop(i + 1);
	free(watchers);
	watchers = NULL;
	nwatchers = 0;
	disks_forget();
	programs_forget();
}

#endif   /* __wasi__ */

/* ------------------------------------------------------------ a directory held open
 *
 * directories.hby's `directory` object: the directory is opened once (a
 * file descriptor, as a file is), so it's the same directory however it's
 * renamed or moved, and wherever the program changes directory to; the
 * names in it are opened from it (openat and its kind), not from a path.
 * It's closed when the object is dropped. */

typedef struct {
	int fd;          /* -1: a free place */
	DIR *walk;       /* going through it entry by entry (next) */
	char *name;      /* the entry next stopped at */
} Held;

static Held *held;
static int nheld;

static _Thread_local char **dnames;
static _Thread_local int64_t ndnames;

static Held *held_dir(int64_t id)
{
	if (id < 1 || id > nheld || held[id - 1].fd < 0)
		rt_panic(NULL, 0, 0, "%s", "that directory isn't open (open it first)");
	return &held[id - 1];
}

/* a name in it: one entry, not a path (so nothing reaches outside it by
 * accident) */
static const char *entry(const char *name, const char *doing)
{
	if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
		rt_panic(NULL, 0, 0, "can't %s '%s': give the name of something in the directory", doing, name);
	if (strchr(name, '/'))
		rt_panic(NULL, 0, 0, "can't %s '%s': give just a name in the directory, not a path (directory.open_inside opens one below it)",
			 doing, name);
	return name;
}

int64_t cby_dir_open(const char *path, int64_t inside)
{
	int base = AT_FDCWD;
	if (inside) {
		base = held_dir(inside)->fd;
		entry(path, "open");
	}
	int fd = openat(base, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		rt_panic(NULL, 0, 0, "can't open the directory '%s': %s", path,
			 errno == ENOENT ? "it isn't there" : errno == ENOTDIR ? "it isn't a directory" : strerror(errno));
	int i = 0;
	while (i < nheld && held[i].fd >= 0)
		i++;
	if (i == nheld) {
		held = realloc(held, sizeof *held * (size_t)(nheld + 1));
		nheld++;
	}
	held[i] = (Held){ fd, NULL, NULL };
	return i + 1;
}

void cby_dir_close(int64_t id)
{
	if (id < 1 || id > nheld || held[id - 1].fd < 0)
		return;
	Held *h = &held[id - 1];
	if (h->walk)
		closedir(h->walk);
	close(h->fd);
	free(h->name);
	*h = (Held){ -1, NULL, NULL };
}

/* where it is now (wherever it's been moved to) */
int64_t cby_dir_path(int64_t id)
{
	Held *h = held_dir(id);
	char path[PATH_MAX + 1];
#ifdef __APPLE__
	if (fcntl(h->fd, F_GETPATH, path) == -1)
		return give_copy("");
#else
	char link[64];
	snprintf(link, sizeof link, "/proc/self/fd/%d", h->fd);
	ssize_t n = readlink(link, path, PATH_MAX);
	if (n < 0)
		return give_copy("");
	path[n] = 0;
#endif
	return give_copy(path);
}

/* the next entry (true), or the end (false); "." and ".." are left out */
int cby_dir_next(int64_t id)
{
	Held *h = held_dir(id);
	if (!h->walk) {
		int again = openat(h->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		h->walk = again >= 0 ? fdopendir(again) : NULL;
		if (!h->walk)
			rt_panic(NULL, 0, 0, "the directory can't be read: %s", strerror(errno));
	}
	struct dirent *e;
	while ((e = readdir(h->walk)) && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")))
		;
	free(h->name);
	h->name = e ? strdup(e->d_name) : NULL;
	return e != NULL;
}

/* back to the start, for next (what's in it now) */
void cby_dir_rewind(int64_t id)
{
	Held *h = held_dir(id);
	if (h->walk)
		closedir(h->walk);
	h->walk = NULL;
	free(h->name);
	h->name = NULL;
}

int64_t cby_dir_name(int64_t id)
{
	Held *h = held_dir(id);
	return give_copy(h->name ? h->name : "");
}

/* 0: nothing there, 1: a file, 2: a directory, 3: something else (a
 * device, a socket); a link is what it points to */
int cby_dir_kind(int64_t id, const char *name)
{
	struct stat st;
	if (fstatat(held_dir(id)->fd, entry(name, "look at"), &st, 0) != 0)
		return 0;
	return S_ISREG(st.st_mode) ? 1 : S_ISDIR(st.st_mode) ? 2 : 3;
}

int64_t cby_dir_size(int64_t id, const char *name)
{
	struct stat st;
	if (fstatat(held_dir(id)->fd, entry(name, "look at"), &st, 0) != 0)
		rt_panic(NULL, 0, 0, "can't give the size of '%s': it isn't there", name);
	return (int64_t)st.st_size;
}

/* when it was last changed, in seconds since 1970 */
int64_t cby_dir_changed(int64_t id, const char *name)
{
	struct stat st;
	if (fstatat(held_dir(id)->fd, entry(name, "look at"), &st, 0) != 0)
		rt_panic(NULL, 0, 0, "can't say when '%s' was changed: it isn't there", name);
	return (int64_t)st.st_mtime;
}

static int dir_by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void dnames_clear(void)
{
	for (int64_t i = 0; i < ndnames; i++)
		free(dnames[i]);
	free(dnames);
	dnames = NULL;
	ndnames = 0;
}

/* the names in it, sorted: how many (cby_dir_listed gives each) */
int64_t cby_dir_names(int64_t id)
{
	Held *h = held_dir(id);
	dnames_clear();
	int again = openat(h->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	DIR *d = again >= 0 ? fdopendir(again) : NULL;
	if (!d)
		rt_panic(NULL, 0, 0, "the directory can't be read: %s", strerror(errno));
	struct dirent *e;
	int64_t cap = 0;
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
			if (ndnames == cap)
				dnames = realloc(dnames, sizeof *dnames * (size_t)(cap = cap ? cap * 2 : 16));
			dnames[ndnames++] = strdup(e->d_name);
		}
	closedir(d);
	if (ndnames)
		qsort(dnames, (size_t)ndnames, sizeof *dnames, dir_by_name);
	return ndnames;
}

int64_t cby_dir_listed(int64_t i) { return give_copy(i >= 0 && i < ndnames ? dnames[i] : ""); }

/* a file in it, read whole */
int64_t cby_dir_read(int64_t id, const char *name)
{
	int fd = openat(held_dir(id)->fd, entry(name, "read"), O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", name, errno == ENOENT ? "it isn't there" : strerror(errno));
	size_t cap = 4096, n = 0;
	char *t = malloc(cap);
	ssize_t got;
	while ((got = read(fd, t + n, cap - n - 1)) != 0) {
		if (got < 0) {
			if (errno == EINTR)
				continue;
			int e = errno;
			close(fd);
			free(t);
			rt_panic(NULL, 0, 0, "can't read '%s': %s", name, strerror(e));
		}
		n += (size_t)got;
		if (cap - n < 2)
			t = realloc(t, cap *= 2);
	}
	close(fd);
	t[n] = 0;
	return give(t);
}

/* a file in it written (or added to) */
void cby_dir_write(int64_t id, const char *name, const char *text, int64_t len, int add)
{
	int fd = openat(held_dir(id)->fd, entry(name, "write"), O_WRONLY | O_CREAT | O_CLOEXEC | (add ? O_APPEND : O_TRUNC), 0666);
	if (fd < 0)
		rt_panic(NULL, 0, 0, "can't write '%s': %s", name, strerror(errno));
	int64_t done = 0;
	while (done < len) {
		ssize_t w = write(fd, text + done, (size_t)(len - done));
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			int e = errno;
			close(fd);
			rt_panic(NULL, 0, 0, "can't write '%s': %s", name, strerror(e));
		}
		done += w;
	}
	close(fd);
}

void cby_dir_make(int64_t id, const char *name)
{
	if (mkdirat(held_dir(id)->fd, entry(name, "make"), 0777) != 0 && errno != EEXIST)
		rt_panic(NULL, 0, 0, "can't make the directory '%s': %s", name, strerror(errno));
}

/* a file, or an empty directory */
void cby_dir_remove(int64_t id, const char *name)
{
	int fd = held_dir(id)->fd;
	int kind = cby_dir_kind(id, name);
	if (kind == 0)
		rt_panic(NULL, 0, 0, "can't remove '%s': it isn't there", name);
	if (unlinkat(fd, name, kind == 2 ? AT_REMOVEDIR : 0) != 0)
		rt_panic(NULL, 0, 0, "can't remove '%s': %s", name,
			 errno == ENOTEMPTY || errno == EEXIST ? "it isn't empty (remove what's in it first)" : strerror(errno));
}

void cby_dir_rename(int64_t id, const char *from, const char *into)
{
	int fd = held_dir(id)->fd;
	if (renameat(fd, entry(from, "rename"), fd, entry(into, "rename to")) != 0)
		rt_panic(NULL, 0, 0, "can't rename '%s' to '%s': %s", from, into, errno == ENOENT ? "it isn't there" : strerror(errno));
}

__attribute__((destructor)) static void dirs_end(void)
{
	for (int i = 0; i < nheld; i++)
		cby_dir_close(i + 1);
	free(held);
	dnames_clear();
}
