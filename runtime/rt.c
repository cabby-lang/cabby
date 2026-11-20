#include "rt.h"
#include <pthread.h>

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>

void (*rt_panic_hook)(int line, int col) = NULL;
void (*rt_before_panic)(void) = NULL;   /* a library's tidying first: tui gives the terminal back */
void (*rt_panic_map)(const char **file, int *line, int *col) = NULL;
_Thread_local void *rt_panic_jmp = NULL;

/* ------------------------------------------------------------- printing */

static int finish_float_text(char *buf, int n)
{
	if (!strpbrk(buf, ".eEn"))
		n += snprintf(buf + n, (size_t)(40 - n), ".0");
	return n;
}

int rt_format_double(double v, char *buf)
{
	if (isnan(v))
		return snprintf(buf, 40, "NaN");
	if (isinf(v))
		return snprintf(buf, 40, v < 0 ? "-inf" : "inf");
	/* Shortest precision that round-trips; prefer plain notation when an
	 * exact plain form exists (1000.0 rather than 1e+03). */
	char first_exp[40] = "";
	int n = 0;
	for (int prec = 1; prec <= 17; prec++) {
		n = snprintf(buf, 40, "%.*g", prec, v);
		if (strtod(buf, NULL) != v)
			continue;
		if (!strpbrk(buf, "eE"))
			break;
		if (!first_exp[0])
			memcpy(first_exp, buf, (size_t)n + 1);
	}
	if (strpbrk(buf, "eE") && first_exp[0])
		n = snprintf(buf, 40, "%s", first_exp);
	return finish_float_text(buf, n);
}

int rt_format_float(float v, char *buf)
{
	if (isnan(v))
		return snprintf(buf, 40, "NaN");
	if (isinf(v))
		return snprintf(buf, 40, v < 0 ? "-inf" : "inf");
	char first_exp[40] = "";
	int n = 0;
	for (int prec = 1; prec <= 9; prec++) {
		n = snprintf(buf, 40, "%.*g", prec, (double)v);
		if (strtof(buf, NULL) != v)
			continue;
		if (!strpbrk(buf, "eE"))
			break;
		if (!first_exp[0])
			memcpy(first_exp, buf, (size_t)n + 1);
	}
	if (strpbrk(buf, "eE") && first_exp[0])
		n = snprintf(buf, 40, "%s", first_exp);
	return finish_float_text(buf, n);
}

/* A whole number in decimal, without printf's general machinery. */
static int format_uint(char *buf, uint64_t v, bool neg)
{
	char tmp[24];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	int k = 0;
	if (neg)
		buf[k++] = '-';
	while (n)
		buf[k++] = tmp[--n];
	buf[k] = '\0';
	return k;
}

int rt_format_text(char *buf, int what, int64_t i, double d)
{
	switch (what) {
	case RT_TEXT_INT:    return i < 0 ? format_uint(buf, 0 - (uint64_t)i, true) : format_uint(buf, (uint64_t)i, false);
	case RT_TEXT_UINT:   return format_uint(buf, (uint64_t)i, false);
	case RT_TEXT_DOUBLE: return rt_format_double(d, buf);
	case RT_TEXT_FLOAT:  return rt_format_float((float)d, buf);
	case RT_TEXT_BOOL:   return snprintf(buf, 48, "%s", i ? "true" : "false");
	default:             buf[0] = (char)i; buf[1] = '\0'; return 1;
	}
}

bool rt_line_open = false;

/* Where print and println write: stdout, unless `stderr some_call` has
 * sent the call's output elsewhere. */
static FILE *out_stream(void);
static _Thread_local int out_depth = 0;   /* (each thread's own) */
static _Thread_local int out_ids[256];
static _Thread_local FILE *out_files[256];      /* for RT_TO_FILE */
static _Thread_local bool out_line_open[256];   /* rt_line_open before a file took over */

static FILE *out_stream(void)
{
	if (out_depth > 0 && out_depth <= 256 && out_ids[out_depth - 1] == RT_TO_FILE)
		return out_files[out_depth - 1];
	return out_depth > 0 && out_ids[out_depth - 1] == RT_STDERR ? stderr : stdout;
}

/* Before a call: send its output to stream `id`. Whatever was printed
 * before is written out first, so the two streams stay in order on a
 * terminal. */
void rt_out_push(int id)
{
	fflush(out_stream());
	if (out_depth < 256)
		out_ids[out_depth] = id;
	out_depth++;
}

void rt_out_pop(void)
{
	/* a file needn't be written out yet: only the terminal's two streams
	 * must stay in order */
	if (!(out_depth > 0 && out_depth <= 256 && out_ids[out_depth - 1] == RT_TO_FILE))
		fflush(out_stream());
	if (out_depth > 0) {
		out_depth--;
		if (out_depth < 256 && out_ids[out_depth] == RT_TO_FILE)
			rt_line_open = out_line_open[out_depth];   /* the terminal's line is as it was */
	}
}

void rt_out_reset(void) { out_depth = 0; }

void rt_print_int(int64_t v)
{
	char buf[24];
	int n = v < 0 ? format_uint(buf, 0 - (uint64_t)v, true) : format_uint(buf, (uint64_t)v, false);
	fwrite(buf, 1, (size_t)n, out_stream());
	rt_line_open = true;
}

void rt_print_uint(uint64_t v)
{
	char buf[24];
	fwrite(buf, 1, (size_t)format_uint(buf, v, false), out_stream());
	rt_line_open = true;
}

void rt_print_double(double v)
{
	char buf[40];
	fwrite(buf, 1, (size_t)rt_format_double(v, buf), out_stream());
	rt_line_open = true;
}

void rt_print_float(float v)
{
	char buf[40];
	fwrite(buf, 1, (size_t)rt_format_float(v, buf), out_stream());
	rt_line_open = true;
}

void rt_print_bool(bool v) { fputs(v ? "true" : "false", out_stream()); rt_line_open = true; }
void rt_print_char(uint8_t c) { putc(c, out_stream()); rt_line_open = c != '\n'; }
void rt_print_str(const char *data, int64_t len)
{
	fwrite(data, 1, (size_t)len, out_stream());
	if (len > 0)
		rt_line_open = data[len - 1] != '\n';
}
void rt_print_sep(void) { putc(' ', out_stream()); rt_line_open = true; }
void rt_print_newline(void) { putc('\n', out_stream()); rt_line_open = false; }
/* a whole list: [1, 2, 3] */
void rt_print_list_open(void) { putc('[', out_stream()); rt_line_open = true; }
void rt_print_list_sep(int64_t i) { if (i) fputs(", ", out_stream()); }
void rt_print_list_close(void) { putc(']', out_stream()); }
void rt_print_lookup_open(void) { putc('{', out_stream()); rt_line_open = true; }
void rt_print_lookup_colon(void) { fputs(": ", out_stream()); }
void rt_print_lookup_close(void) { putc('}', out_stream()); }

/* ------------------------------------------------------------- panics */

/* Where the program called into a library (a .hby header's function): a
 * panic from C inside it, which knows no place of its own, is reported
 * there, in the program's own code. */
_Thread_local const char *rt_lib_call_file;
_Thread_local int rt_lib_call_line, rt_lib_call_col;
bool rt_panic_at_lib_call;   /* the panic being shown is at the program's call into a library */

void rt_set_lib_call(const char *file, int line, int col)
{
	rt_lib_call_file = file;
	rt_lib_call_line = line;
	rt_lib_call_col = col;
}

static void tmp_free_all(void);
static _Thread_local RtTry *try_top;
static _Thread_local char *recovered;   /* the last caught panic's message */

_Thread_local int cby_trying;
_Thread_local RtFrame *cby_frame_top;

void cby_frame_enter(RtFrame *f)
{
	f->prev = cby_frame_top;
	cby_frame_top = f;
}

void rt_try_push(RtTry *t)
{
	cby_trying++;
	t->frames = cby_frame_top;
	t->prev = try_top;
	t->lib_file = rt_lib_call_file;
	t->lib_line = rt_lib_call_line;
	t->lib_col = rt_lib_call_col;
	t->out_depth = out_depth;
	t->gil = rt_gil_held();
	try_top = t;
}

void rt_try_pop(RtTry *t)
{
	if (try_top == t) {
		try_top = t->prev;
		cby_trying--;
	}
}

const char *rt_recovered_message(void) { return recovered ? recovered : ""; }

static void recovered_gone(void)
{
	free(recovered);
	recovered = NULL;
}

__attribute__((destructor)) static void recovered_end(void) { recovered_gone(); }

/* compiled programs' side: the RtTry made for them, its jb first (so it's
 * what _setjmp is given) */
void *cby_try_begin(void)
{
	RtTry *t = malloc(sizeof *t);
	rt_try_push(t);
	return t;
}

void cby_try_end(void *t)
{
	rt_try_pop(t);
	free(t);
}

void cby_try_failed(void *t) { free(t); }   /* (popped already, by the panic) */

void cby_recovered(cby_str *dst)
{
	const char *m = rt_recovered_message();
	size_t n = strlen(m);
	dst->data = rt_alloc(n + 1);
	memcpy(dst->data, m, n + 1);
	dst->len = (int64_t)n;
}

/* a panic, caught by an `else`: nothing shown; back to its statement */
static _Noreturn void panic_caught(const char *fmt, va_list ap)
{
	RtTry *t = try_top;
	try_top = t->prev;
	cby_trying--;
	static _Thread_local int hooked;
	if (!hooked) {
		hooked = 1;
		rt_on_thread_end(recovered_gone);
	}
	char *m = NULL;
	va_list copy;
	va_copy(copy, ap);
	int n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	m = malloc((size_t)(n > 0 ? n : 0) + 1);
	vsnprintf(m, (size_t)(n > 0 ? n : 0) + 1, fmt, ap);
	free(recovered);
	recovered = m;
	tmp_free_all();
	rt_lib_call_file = t->lib_file;
	rt_lib_call_line = t->lib_line;
	rt_lib_call_col = t->lib_col;
	rt_panic_at_lib_call = false;
	out_depth = t->out_depth;
	if (t->gil && !rt_gil_held())
		rt_gil_take(true);   /* (it panicked while letting other threads run) */
	/* what the compiled functions it leaves had made: dropped, their
	 * destructors run (the last made first) */
	while (cby_frame_top && cby_frame_top != t->frames) {
		RtFrame *f = cby_frame_top;
		cby_frame_top = f->prev;
		for (int64_t i = f->n - 1; i >= 0; i--)
			if (*f->items[i].flag) {
				*f->items[i].flag = 0;
				f->items[i].drop(f->items[i].slot);
			}
	}
	_longjmp(t->jb, 1);
}

_Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...)
{
	if (try_top) {
		va_list ap;
		va_start(ap, fmt);
		panic_caught(fmt, ap);
	}
	if (rt_before_panic)
		rt_before_panic();
	fflush(stdout);
	if (!file && rt_lib_call_file) {
		rt_panic_at_lib_call = true;
		file = rt_lib_call_file;
		line = rt_lib_call_line;
		col = rt_lib_call_col;
	}
	int mline = line, mcol = col;
	if (file && rt_panic_map)
		rt_panic_map(&file, &mline, &mcol);
	if (file)
		fprintf(stderr, "panic at %s:%d:%d: ", file, mline, mcol);
	else
		fprintf(stderr, "panic: ");
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	if (rt_panic_hook)
		rt_panic_hook(line, col);
	fflush(stderr);
	if (rt_panic_jmp)
		longjmp(*(jmp_buf *)rt_panic_jmp, 1);
	_exit(RT_PANIC_EXIT);
}

/* ------------------------------------------------------------- checks */

void rt_check_div(int64_t a, int64_t b, int bits, bool is_signed, bool is_rem,
		  const char *file, int line, int col)
{
	if (b == 0)
		rt_panic(file, line, col, is_rem ? "remainder by zero" : "division by zero");
	if (is_signed && b == -1 && !is_rem) {
		int64_t min = bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1));
		if (a == min)
			rt_panic(file, line, col, "overflow: %lld / -1 is too big for %d bits", (long long)a, bits);
	}
}

void rt_check_shift(int64_t amount, int bits, const char *file, int line, int col)
{
	if (amount < 0 || amount >= bits)
		rt_panic(file, line, col, "can't shift by %lld: it must be 0 to %d", (long long)amount, bits - 1);
}

void rt_str_bounds(int64_t index, int64_t len, const char *file, int line, int col)
{
	if (len == 0)
		rt_panic(file, line, col, "index %lld is outside an empty string", (long long)index);
	rt_panic(file, line, col, "index %lld is outside a string of length %lld (positions 0 to %lld)",
		 (long long)index, (long long)len, (long long)(len - 1));
}

void rt_bounds_check(int64_t index, int64_t len, const char *file, int line, int col)
{
	if (index < 0 || index >= len)
		rt_panic(file, line, col, "index %lld is outside an array of length %lld (positions 0 to %lld)",
			 (long long)index, (long long)len, (long long)(len - 1));
}

int64_t rt_float_to_int(double v, int bits, bool is_signed)
{
	if (isnan(v))
		return 0;
	if (is_signed) {
		double lo = bits == 64 ? -9223372036854775808.0 : -ldexp(1.0, bits - 1);
		double hi = bits == 64 ? 9223372036854775807.0 : ldexp(1.0, bits - 1) - 1;
		if (v <= lo)
			return bits == 64 ? INT64_MIN : (int64_t)lo;
		if (v >= hi)
			return bits == 64 ? INT64_MAX : (int64_t)hi;
		return (int64_t)v;
	}
	if (v <= 0)
		return 0;
	if (bits == 64) {
		if (v >= 18446744073709551615.0)
			return (int64_t)UINT64_MAX;
		return (int64_t)(uint64_t)v;
	}
	double hi = ldexp(1.0, bits) - 1;
	if (v >= hi)
		return (int64_t)hi;
	return (int64_t)v;
}

/* ------------------------------------------------------------- memory */

void *rt_alloc(size_t size)
{
	void *p = malloc(size ? size : 1);
	if (!p)
		rt_panic(NULL, 0, 0, "out of memory");
	return p;
}

void *rt_alloc_zeroed(size_t size)
{
	void *p = calloc(1, size ? size : 1);
	if (!p)
		rt_panic(NULL, 0, 0, "out of memory");
	return p;
}

void rt_free(void *p) { free(p); }

/* ---- links (link T): pointing at an object without owning it ----
 * An object that's been linked to has a place in this table, which says
 * where it is, and which object of that place's it is: the place's
 * generation, which changes when the object is dropped. A link is the
 * place's number and the generation it had: 28 bits and 36, in 8 bytes,
 * and 0 for nowhere. A link whose generation isn't the place's any more
 * points at something that's gone; that's a panic, never a read of
 * memory that's been freed. A place that's been through every generation
 * is retired, not used again. The table is in blocks that never move, so
 * a place's address stays good. A link is used only on the thread whose
 * object it is (a worker's objects are its own). */
#define LINK_PLACE_BITS 28
#define LINK_PLACE_MASK ((UINT64_C(1) << LINK_PLACE_BITS) - 1)
#define LINK_GEN_MASK ((UINT64_C(1) << (64 - LINK_PLACE_BITS)) - 1)
#define LINK_BLOCK 4096
typedef struct {
	void *obj;          /* the object's handle (NULL: gone, or free) */
	uint64_t gen;
	uint64_t thread;
} LinkPlace;
static LinkPlace *link_blocks[(LINK_PLACE_MASK + 1) / LINK_BLOCK];
static uint64_t link_used = 1;          /* places handed out so far (0 is nowhere) */
static uint64_t *link_free;             /* places to use again */
static int64_t link_nfree, link_capfree;
static pthread_mutex_t link_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t link_threads;
static __thread uint64_t link_thread;

static uint64_t this_thread(void)
{
	if (!link_thread)
		link_thread = ++link_threads;
	return link_thread;
}

static LinkPlace *link_place(uint64_t i)
{
	LinkPlace *b = i <= LINK_PLACE_MASK ? link_blocks[i / LINK_BLOCK] : NULL;
	return b ? &b[i % LINK_BLOCK] : NULL;
}

/* A link to the object obj, whose place (0 for none yet) is kept at *life */
int64_t cby_link_to(void *obj, uint64_t *life)
{
	if (!obj)
		return 0;
	if (!*life) {
		pthread_mutex_lock(&link_lock);
		uint64_t i;
		if (link_nfree) {
			i = link_free[--link_nfree];
		} else {
			if (link_used > LINK_PLACE_MASK) {
				pthread_mutex_unlock(&link_lock);
				rt_panic(NULL, 0, 0, "too many objects linked to at once (%llu)", (unsigned long long)LINK_PLACE_MASK);
			}
			i = link_used++;
			if (!link_blocks[i / LINK_BLOCK]) {
				link_blocks[i / LINK_BLOCK] = calloc(LINK_BLOCK, sizeof(LinkPlace));
				if (!link_blocks[i / LINK_BLOCK]) {
					pthread_mutex_unlock(&link_lock);
					rt_panic(NULL, 0, 0, "out of memory");
				}
			}
		}
		LinkPlace *p = link_place(i);
		p->obj = obj;
		p->thread = this_thread();
		if (!p->gen)
			p->gen = 1;
		pthread_mutex_unlock(&link_lock);
		*life = i;
	}
	LinkPlace *p = link_place(*life);
	return (int64_t)((p->gen & LINK_GEN_MASK) << LINK_PLACE_BITS | *life);
}

/* The object a link points at, as a place holding its handle; a panic,
 * naming the link (name) and its kind of object (kind), if it's gone. */
void *cby_link_get(int64_t link, const char *name, const char *kind, const char *file, int line, int col)
{
	uint64_t l = (uint64_t)link, i = l & LINK_PLACE_MASK, gen = l >> LINK_PLACE_BITS;
	if (!link)
		rt_panic(file, line, col, "'%s' doesn't point at anything: it was never set, or it was cleared (check "
			 "first with .is_set)", name);
	LinkPlace *p = link_place(i);
	if (!p || !p->obj || (p->gen & LINK_GEN_MASK) != gen)
		rt_panic(file, line, col, "the %s '%s' pointed at is gone: it was dropped (check first with .is_set)",
			 kind, name);
	if (p->thread != this_thread())
		rt_panic(file, line, col, "'%s' points at a %s of another worker's (or of the program's, from a worker); "
			 "a link is only used where its object is", name, kind);
	return &p->obj;
}

/* whether it points at an object that's still there */
bool cby_link_is_set(int64_t link)
{
	uint64_t l = (uint64_t)link;
	LinkPlace *p = link ? link_place(l & LINK_PLACE_MASK) : NULL;
	return p && p->obj && (p->gen & LINK_GEN_MASK) == l >> LINK_PLACE_BITS && p->thread == this_thread();
}

/* the object at this place is being dropped: every link to it is gone */
void cby_link_end(uint64_t life)
{
	if (!life)
		return;
	pthread_mutex_lock(&link_lock);
	LinkPlace *p = link_place(life);
	p->obj = NULL;
	p->gen++;
	if ((p->gen & LINK_GEN_MASK) != 0) {   /* (else every generation's been used: retired) */
		if (link_nfree == link_capfree) {
			link_capfree = link_capfree ? link_capfree * 2 : 256;
			uint64_t *f = realloc(link_free, sizeof *f * (size_t)link_capfree);
			if (!f) {
				pthread_mutex_unlock(&link_lock);
				rt_panic(NULL, 0, 0, "out of memory");
			}
			link_free = f;
		}
		link_free[link_nfree++] = life;
	}
	pthread_mutex_unlock(&link_lock);
}

/* A compiled program's objects keep their place 16 bytes before the
 * handle (16, so the object stays aligned as malloc's are). */
void *rt_obj_alloc(size_t size)
{
	char *p = malloc(size + 16);
	if (!p)
		rt_panic(NULL, 0, 0, "out of memory");
	*(uint64_t *)p = 0;
	return p + 16;
}

int64_t cby_link_obj(void *h) { return cby_link_to(h, (uint64_t *)((char *)h - 16)); }

/* (after its destructors, before its fields are dropped: a field's own
 * destructor that follows a link back to it finds it gone) */
void rt_obj_unlink(void *h)
{
	uint64_t *life = (uint64_t *)((char *)h - 16);
	cby_link_end(*life);
	*life = 0;
}

void rt_obj_free(void *h) { free((char *)h - 16); }

void *rt_realloc(void *p, size_t size)
{
	void *n = realloc(p, size ? size : 1);
	if (!n)
		rt_panic(NULL, 0, 0, "out of memory");
	return n;
}

/* How many bytes a block from malloc really holds (at least what was asked
 * for; often a little more). */
#if defined(__APPLE__)
#include <malloc/malloc.h>
size_t rt_usable_size(void *p) { return malloc_size(p); }
#else
#include <malloc.h>
size_t rt_usable_size(void *p) { return malloc_usable_size(p); }
#endif

/* ------------------------------------------------------------- input */

bool rt_input_ended = false;

/* Reads one line from stdin, without its Enter (and without a '\r' before
 * it). Returns a malloc'd buffer (zero-terminated) and its length. At the
 * end of the input it returns "" and sets rt_input_ended. */
char *rt_read_line(int64_t *len, const char *file, int line, int col)
{
	/* Reading again after the end has been reported can only give "" again:
	 * a loop that forgot to check end_of_input would never stop. */
	if (rt_input_ended)
		rt_panic(file, line, col, "read_line after the end of the input (stop when end_of_input is true)");
	fflush(stdout);
	rt_flush_files();     /* while it waits, what's been written is in the files */
	size_t cap = 64, n = 0;
	char *buf = malloc(cap);
	if (!buf)
		rt_panic(NULL, 0, 0, "out of memory");
	int c;
	bool any = false;
	bool held = rt_gil_drop();   /* (workers carry on while it waits for typing) */
	while ((c = getchar()) != EOF) {
		any = true;
		if (c == '\n')
			break;
		if (n + 2 > cap) {
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb)
				rt_panic(NULL, 0, 0, "out of memory");
			buf = nb;
		}
		buf[n++] = (char)c;
	}
	rt_gil_take(held);
	if (!any)
		rt_input_ended = true;
	if (n > 0 && buf[n - 1] == '\r')
		n--;
	buf[n] = '\0';
	/* For tests: show what was "typed", as a terminal would, when the
	 * input comes from a pipe. */
	static int echo = -1;
	if (echo < 0)
		echo = getenv("CABBY_ECHO_INPUT") && !isatty(0);
	if (echo && any) {
		fwrite(buf, 1, n, stdout);
		putchar('\n');
	}
	rt_line_open = false;     /* the Enter ended the line on screen */
	*len = (int64_t)n;
	return buf;
}

/* read_char: the first character of the next line typed (the rest of the
 * line, and its Enter, are read too, so nothing is left for the next read).
 * An empty line gives '\n'; the end of the input gives '\0'. */
char rt_read_char(const char *file, int line, int col)
{
	if (rt_input_ended)
		rt_panic(file, line, col, "read_char after the end of the input (stop when end_of_input is true)");
	int64_t n;
	char *p = rt_read_line(&n, file, line, col);
	char c = n > 0 ? p[0] : rt_input_ended ? '\0' : '\n';
	free(p);
	return c;
}

#include <termios.h>
#include <poll.h>

/* read_key: one key, the moment it's pressed, without Enter. A letter (or
 * any character) as itself; other keys by name: "enter", "tab",
 * "backspace", "escape", "up", "down", "left", "right", "home", "end",
 * "insert", "delete", "page_up", "page_down", "f1"..."f12". "" at the end of
 * the input. When the input isn't a terminal, characters are read one at a
 * time from it. */
static int key_byte_now(bool term, int wait_ms)
{
	if (!term)
		return getchar();
	if (wait_ms >= 0) {
		struct pollfd p = { 0, POLLIN, 0 };
		if (poll(&p, 1, wait_ms) <= 0)
			return -1;
	}
	unsigned char c;
	return read(0, &c, 1) == 1 ? c : -1;
}

static int key_byte(bool term, int wait_ms)
{
	bool held = rt_gil_drop();   /* (workers carry on while it waits for a key) */
	int c = key_byte_now(term, wait_ms);
	rt_gil_take(held);
	return c;
}

int64_t rt_read_key(char *out, const char *file, int line, int col)
{
	if (rt_input_ended)
		rt_panic(file, line, col, "read_key after the end of the input (stop when end_of_input is true)");
	fflush(stdout);
	rt_flush_files();
	bool term = isatty(0);
	struct termios was, raw;
	if (term) {
		tcgetattr(0, &was);
		raw = was;
		raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);   /* Ctrl-C still stops the program */
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &raw);
	}
	int c = key_byte(term, -1);
	const char *name = NULL;
	int n = 0;
	if (c < 0 || (term && c == 4)) {   /* the end of the input, or Ctrl-D at a terminal */
		rt_input_ended = true;
	} else if (c == '\n' || c == '\r') {
		name = "enter";
	} else if (c == '\t') {
		name = "tab";
	} else if (c == 127 || c == 8) {
		name = "backspace";
	} else if (c == 27) {
		/* a key that sends several bytes (ESC [ A for up), or Escape alone */
		int c1 = key_byte(term, term ? 30 : -1);
		if (c1 != '[' && c1 != 'O') {
			name = "escape";
		} else {
			char seq[8];
			int k = 0, x;
			while (k < 7 && (x = key_byte(term, 30)) >= 0) {
				seq[k++] = (char)x;
				if ((x >= 'A' && x <= 'Z') || x == '~')
					break;
			}
			seq[k] = '\0';
			static const struct { const char *seq, *name; } keys[] = {
				{ "A", "up" }, { "B", "down" }, { "C", "right" }, { "D", "left" }, { "H", "home" }, { "F", "end" },
				{ "1~", "home" }, { "2~", "insert" }, { "3~", "delete" }, { "4~", "end" }, { "5~", "page_up" },
				{ "6~", "page_down" }, { "P", "f1" }, { "Q", "f2" }, { "R", "f3" }, { "S", "f4" },
				{ "11~", "f1" }, { "12~", "f2" }, { "13~", "f3" }, { "14~", "f4" }, { "15~", "f5" },
				{ "17~", "f6" }, { "18~", "f7" }, { "19~", "f8" }, { "20~", "f9" }, { "21~", "f10" },
				{ "23~", "f11" }, { "24~", "f12" },
			};
			name = "escape";
			for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
				if (strcmp(seq, keys[i].seq) == 0)
					name = keys[i].name;
		}
	} else {
		/* a character as itself; in UTF-8, all of its bytes */
		out[n++] = (char)c;
		int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		for (int i = 0; i < more; i++) {
			int b = key_byte(term, term ? 30 : -1);
			if (b < 0)
				break;
			out[n++] = (char)b;
		}
	}
	if (term)
		tcsetattr(0, TCSANOW, &was);
	if (name) {
		n = (int)strlen(name);
		memcpy(out, name, (size_t)n);
	}
	out[n] = '\0';
	return n;
}

void cby_read_key(cby_str *dst, const char *file, int line, int col)
{
	char buf[16];
	int64_t n = rt_read_key(buf, file, line, col);
	cby_str_dup(dst, buf, n);
}

int8_t cby_read_char(const char *file, int line, int col) { return (int8_t)rt_read_char(file, line, col); }

/* Text to numbers. The whole text must be the number, apart from spaces
 * around it: "42", " -7 ", "2.5e3". RT_NUM_INT and RT_NUM_LONG take whole
 * numbers in decimal; RT_NUM_DOUBLE also takes a fraction and an exponent.
 * Returns 1 if the text is such a number and fits, 0 if it isn't a number,
 * -1 if it is but is too big. */
int rt_parse_number(const char *data, int64_t len, int what, int64_t *iv, double *dv)
{
	int64_t a = 0, b = len;
	while (a < b && (data[a] == ' ' || data[a] == '\t'))
		a++;
	while (b > a && (data[b - 1] == ' ' || data[b - 1] == '\t'))
		b--;
	int64_t i = a;
	if (i < b && (data[i] == '+' || data[i] == '-'))
		i++;
	int64_t digits = 0;
	while (i < b && data[i] >= '0' && data[i] <= '9')
		i++, digits++;
	if (what == RT_NUM_DOUBLE) {
		int64_t frac = 0;
		if (i < b && data[i] == '.') {
			i++;
			while (i < b && data[i] >= '0' && data[i] <= '9')
				i++, frac++;
		}
		if (digits + frac == 0)
			return 0;
		if (i < b && (data[i] == 'e' || data[i] == 'E')) {
			i++;
			if (i < b && (data[i] == '+' || data[i] == '-'))
				i++;
			int64_t ed = 0;
			while (i < b && data[i] >= '0' && data[i] <= '9')
				i++, ed++;
			if (ed == 0)
				return 0;
		}
		if (i != b)
			return 0;
		char tmp[512];
		char *buf = (size_t)(b - a) < sizeof tmp ? tmp : malloc((size_t)(b - a) + 1);
		memcpy(buf, data + a, (size_t)(b - a));
		buf[b - a] = '\0';
		double v = strtod(buf, NULL);
		if (buf != tmp)
			free(buf);
		if (isinf(v))
			return -1;
		if (dv)
			*dv = v;
		return 1;
	}
	if (digits == 0 || i != b)
		return 0;
	bool neg = data[a] == '-';
	uint64_t v = 0;
	for (int64_t k = b - digits; k < b; k++) {
		unsigned d = (unsigned)(data[k] - '0');
		if (v > (UINT64_MAX - d) / 10)
			return -1;
		v = v * 10 + d;
	}
	uint64_t lim = what == RT_NUM_INT ? (uint64_t)INT32_MAX : (uint64_t)INT64_MAX;
	if (v > lim + (neg ? 1 : 0))
		return -1;
	if (iv)
		*iv = neg ? (int64_t)(0 - v) : (int64_t)v;
	return 1;
}

/* The same, panicking (at file:line:col) when the text isn't a number. */
int64_t rt_text_to_int(const char *data, int64_t len, int what, const char *file, int line, int col)
{
	int64_t v = 0;
	int r = rt_parse_number(data, len, what, &v, NULL);
	if (r == 1)
		return v;
	const char *t = what == RT_NUM_INT ? "int" : "long";
	if (r < 0)
		rt_panic(file, line, col, "\"%.*s\" is too big for %s %s%s", len > 60 ? 60 : (int)len, data,
			 what == RT_NUM_INT ? "an" : "a", t, what == RT_NUM_INT ? " (.to_long takes bigger numbers)" : "");
	rt_panic(file, line, col, "\"%.*s\" is not a whole number (check first with .is_%s)",
		 len > 60 ? 60 : (int)len, data, t);
}

double rt_text_to_double(const char *data, int64_t len, const char *file, int line, int col)
{
	double v = 0;
	int r = rt_parse_number(data, len, RT_NUM_DOUBLE, NULL, &v);
	if (r == 1)
		return v;
	if (r < 0)
		rt_panic(file, line, col, "\"%.*s\" is too big for a double", len > 60 ? 60 : (int)len, data);
	rt_panic(file, line, col, "\"%.*s\" is not a number (check first with .is_number)",
		 len > 60 ? 60 : (int)len, data);
}

/* ------------------------------------------------------------- string tools */

/* Where `n` first appears in `h`, or -1. An empty `n` is found at 0. */
int64_t rt_str_find(const char *h, int64_t hn, const char *n, int64_t nn)
{
	if (nn == 0)
		return 0;
	if (nn > hn)
		return -1;
	const char *end = h + hn - nn;
	for (const char *p = h; p <= end; p++) {
		p = memchr(p, n[0], (size_t)(end - p + 1));
		if (!p)
			return -1;
		if (memcmp(p, n, (size_t)nn) == 0)
			return p - h;
	}
	return -1;
}

/* RT_TEST_CONTAINS, _STARTS, _ENDS */
bool rt_str_test(const char *h, int64_t hn, const char *n, int64_t nn, int what)
{
	if (what == RT_TEST_STARTS)
		return nn <= hn && memcmp(h, n, (size_t)nn) == 0;
	if (what == RT_TEST_ENDS)
		return nn <= hn && memcmp(h + hn - nn, n, (size_t)nn) == 0;
	return rt_str_find(h, hn, n, nn) >= 0;
}

/* Dictionary order, byte by byte: below 0, 0 or above 0. */
int rt_str_compare(const char *a, int64_t an, const char *b, int64_t bn)
{
	int64_t n = an < bn ? an : bn;
	int c = n ? memcmp(a, b, (size_t)n) : 0;
	if (c)
		return c < 0 ? -1 : 1;
	return an < bn ? -1 : an > bn ? 1 : 0;
}

/* The part of the text without spaces, tabs, Enters ('\n', '\r') at either end. */
void rt_str_trim(const char *d, int64_t n, int64_t *start, int64_t *count)
{
	int64_t a = 0, b = n;
	while (a < b && (d[a] == ' ' || d[a] == '\t' || d[a] == '\n' || d[a] == '\r'))
		a++;
	while (b > a && (d[b - 1] == ' ' || d[b - 1] == '\t' || d[b - 1] == '\n' || d[b - 1] == '\r'))
		b--;
	*start = a;
	*count = b - a;
}

static char *alloc_text(int64_t n)
{
	char *p = malloc((size_t)n + 1);
	if (!p)
		rt_panic(NULL, 0, 0, "out of memory");
	p[n] = '\0';
	return p;
}

/* ---- upper and lower case, beyond A to Z ----
 * Latin (with Latin-1, Extended-A and -B, and the Extended Additional
 * block: æ þ ð ƿ ȝ, ā ē ī ō ū, Vietnamese), Greek (with its accents and
 * the polytonic letters of Ancient Greek), Cyrillic, Armenian, and the
 * fullwidth letters. Upper case of ß is SS; of ς (a final sigma), Σ.
 * Anything else, and bytes that aren't UTF-8, are left as they are. */

/* pairs in a run: upper at even (or odd) code points, lower the next */
static bool in_pairs(uint32_t c, uint32_t from, uint32_t to, bool upper_even, uint32_t *upper, uint32_t *lower)
{
	if (c < from || c > to)
		return false;
	bool is_up = ((c & 1) == 0) == upper_even;
	*upper = is_up ? c : c - 1;
	*lower = is_up ? c + 1 : c;
	return *lower <= to;
}

/* the other case of c: upper (or lower), or c itself */
static uint32_t case_of(uint32_t c, bool to_upper)
{
	uint32_t up = c, lo = c;
	if (c >= 'A' && c <= 'Z') { up = c; lo = c + 32; }
	else if (c >= 'a' && c <= 'z') { up = c - 32; lo = c; }
	else if (c < 0xC0) return c;
	/* Latin-1: À-Þ and à-þ, but not × and ÷; ÿ and Ÿ */
	else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) { up = c; lo = c + 32; }
	else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) { up = c - 32; lo = c; }
	else if (c == 0xFF || c == 0x178) { up = 0x178; lo = 0xFF; }
	else if (c == 0xB5) return c;
	/* Latin Extended-A */
	else if (c == 0x130) return to_upper ? c : 'i';
	else if (c == 0x131) return to_upper ? 'I' : c;
	else if (c == 0x17F) return to_upper ? 'S' : c;   /* long s */
	else if (in_pairs(c, 0x100, 0x12F, true, &up, &lo) || in_pairs(c, 0x132, 0x137, true, &up, &lo) ||
		 in_pairs(c, 0x139, 0x148, false, &up, &lo) || in_pairs(c, 0x14A, 0x177, true, &up, &lo) ||
		 in_pairs(c, 0x179, 0x17E, false, &up, &lo)) { }
	/* Latin Extended-B: the letters in pairs, wynn and yogh among them */
	else if (c == 0x1BF || c == 0x1F7) { up = 0x1F7; lo = 0x1BF; }   /* ƿ Ƿ */
	else if (c >= 0x1C4 && c <= 0x1CC) {   /* Ǆ ǅ ǆ, Ǉ ǈ ǉ, Ǌ ǋ ǌ */
		uint32_t base = c - (c - 0x1C4) % 3;
		up = base;
		lo = base + 2;
		if (c == base + 1)
			return to_upper ? up : lo;
	}
	else if (in_pairs(c, 0x1CD, 0x1DC, false, &up, &lo) || in_pairs(c, 0x1DE, 0x1EF, true, &up, &lo) ||
		 in_pairs(c, 0x1F4, 0x1F5, true, &up, &lo) || in_pairs(c, 0x1F8, 0x21F, true, &up, &lo) ||
		 in_pairs(c, 0x222, 0x233, true, &up, &lo) || in_pairs(c, 0x246, 0x24F, true, &up, &lo)) { }
	else if (c == 0x1F1 || c == 0x1F2 || c == 0x1F3) { up = 0x1F1; lo = 0x1F3; if (c == 0x1F2) return to_upper ? up : lo; }
	/* Greek: the letters, and those with an accent */
	else if (c == 0x3C2) return to_upper ? 0x3A3 : c;   /* final sigma */
	else if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) { up = c; lo = c + 32; }
	else if (c >= 0x3B1 && c <= 0x3CB) { up = c - 32; lo = c; }
	else if (c == 0x386 || c == 0x3AC) { up = 0x386; lo = 0x3AC; }
	else if (c >= 0x388 && c <= 0x38A) { up = c; lo = c + 37; }
	else if (c >= 0x3AD && c <= 0x3AF) { up = c - 37; lo = c; }
	else if (c == 0x38C || c == 0x3CC) { up = 0x38C; lo = 0x3CC; }
	else if (c == 0x38E || c == 0x38F) { up = c; lo = c + 63; }
	else if (c == 0x3CD || c == 0x3CE) { up = c - 63; lo = c; }
	else if (in_pairs(c, 0x3D8, 0x3EF, true, &up, &lo)) { }
	/* Cyrillic */
	else if (c >= 0x400 && c <= 0x40F) { up = c; lo = c + 80; }
	else if (c >= 0x410 && c <= 0x42F) { up = c; lo = c + 32; }
	else if (c >= 0x430 && c <= 0x44F) { up = c - 32; lo = c; }
	else if (c >= 0x450 && c <= 0x45F) { up = c - 80; lo = c; }
	else if (c == 0x4C0 || c == 0x4CF) { up = 0x4C0; lo = 0x4CF; }
	else if (in_pairs(c, 0x460, 0x481, true, &up, &lo) || in_pairs(c, 0x48A, 0x4BF, true, &up, &lo) ||
		 in_pairs(c, 0x4C1, 0x4CE, false, &up, &lo) || in_pairs(c, 0x4D0, 0x52F, true, &up, &lo)) { }
	/* Armenian */
	else if (c >= 0x531 && c <= 0x556) { up = c; lo = c + 48; }
	else if (c >= 0x561 && c <= 0x586) { up = c - 48; lo = c; }
	/* Latin Extended Additional (ẞ is SS's capital: its lower case is ß) */
	else if (c == 0x1E9E) return to_upper ? c : 0xDF;
	else if (in_pairs(c, 0x1E00, 0x1E95, true, &up, &lo) || in_pairs(c, 0x1EA0, 0x1EFF, true, &up, &lo)) { }
	/* Greek Extended: Ancient Greek's breathings and accents */
	else if (c >= 0x1F00 && c <= 0x1F6F) {
		uint32_t row = c & ~0xFu, col = c & 0xF;
		/* rows of eight lower, then eight upper; some have only some */
		static const uint16_t has[7] = { 0xFFFF, 0x3F3F, 0xFFFF, 0xFFFF, 0x3F3F, 0xAAFF, 0xFFFF };
		uint32_t pair = col < 8 ? col + 8 : col - 8;
		if (!(has[(row - 0x1F00) >> 4] >> col & 1) || !(has[(row - 0x1F00) >> 4] >> pair & 1))
			return c;
		up = row + (col | 8);
		lo = row + (col & 7);
	}
	else if (c >= 0x1F80 && c <= 0x1FAF) {   /* with iota below: as their capitals have it */
		uint32_t row = c & ~0xFu, col = c & 0xF;
		up = row + (col | 8);
		lo = row + (col & 7);
	}
	else {
		static const uint16_t pairs[][2] = {   /* upper, lower */
			{ 0x1FB8, 0x1FB0 }, { 0x1FB9, 0x1FB1 }, { 0x1FBA, 0x1F70 }, { 0x1FBB, 0x1F71 }, { 0x1FBC, 0x1FB3 },
			{ 0x1FC8, 0x1F72 }, { 0x1FC9, 0x1F73 }, { 0x1FCA, 0x1F74 }, { 0x1FCB, 0x1F75 }, { 0x1FCC, 0x1FC3 },
			{ 0x1FD8, 0x1FD0 }, { 0x1FD9, 0x1FD1 }, { 0x1FDA, 0x1F76 }, { 0x1FDB, 0x1F77 },
			{ 0x1FE8, 0x1FE0 }, { 0x1FE9, 0x1FE1 }, { 0x1FEA, 0x1F7A }, { 0x1FEB, 0x1F7B }, { 0x1FEC, 0x1FE5 },
			{ 0x1FF8, 0x1F78 }, { 0x1FF9, 0x1F79 }, { 0x1FFA, 0x1F7C }, { 0x1FFB, 0x1F7D }, { 0x1FFC, 0x1FF3 },
		};
		bool found = false;
		for (size_t i = 0; i < sizeof pairs / sizeof *pairs && !found; i++)
			if (c == pairs[i][0] || c == pairs[i][1]) {
				up = pairs[i][0];
				lo = pairs[i][1];
				found = true;
			}
		if (!found) {
			if (c >= 0xFF21 && c <= 0xFF3A) { up = c; lo = c + 32; }        /* fullwidth */
			else if (c >= 0xFF41 && c <= 0xFF5A) { up = c - 32; lo = c; }
			else return c;
		}
	}
	return to_upper ? up : lo;
}

/* the code point at p (n bytes left), and how many bytes it takes; a byte
 * that doesn't start UTF-8 properly is itself (*len 1, and 0 said) */
static uint32_t utf8_at(const unsigned char *p, int64_t n, int *len)
{
	unsigned char b = p[0];
	int k = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
	if (k == 0 || k > n) {
		*len = 1;
		return 0xFFFFFFFF;
	}
	uint32_t c = k == 1 ? b : k == 2 ? b & 0x1F : k == 3 ? b & 0x0F : b & 0x07;
	for (int i = 1; i < k; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*len = 1;
			return 0xFFFFFFFF;
		}
		c = c << 6 | (p[i] & 0x3F);
	}
	*len = k;
	return c;
}

static int utf8_put(char *out, uint32_t c)
{
	if (c < 0x80) { out[0] = (char)c; return 1; }
	if (c < 0x800) { out[0] = (char)(0xC0 | c >> 6); out[1] = (char)(0x80 | (c & 0x3F)); return 2; }
	if (c < 0x10000) {
		out[0] = (char)(0xE0 | c >> 12); out[1] = (char)(0x80 | (c >> 6 & 0x3F)); out[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | c >> 18); out[1] = (char)(0x80 | (c >> 12 & 0x3F));
	out[2] = (char)(0x80 | (c >> 6 & 0x3F)); out[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}

/* Greek letters with no capital of their own: the capital, then the
 * accent as a mark of its own (U+0342 is the circumflex, perispomeni),
 * and Ι for an iota below, as Unicode's SpecialCasing has them */
static const struct { uint16_t c; uint16_t to[3]; } greek_upper[] = {
	{ 0x1F50, { 0x3A5, 0x313 } }, { 0x1F52, { 0x3A5, 0x313, 0x300 } }, { 0x1F54, { 0x3A5, 0x313, 0x301 } },
	{ 0x1F56, { 0x3A5, 0x313, 0x342 } },
	{ 0x1FB2, { 0x1FBA, 0x399 } }, { 0x1FB4, { 0x386, 0x399 } }, { 0x1FB6, { 0x391, 0x342 } },
	{ 0x1FB7, { 0x391, 0x342, 0x399 } }, { 0x1FC2, { 0x1FCA, 0x399 } }, { 0x1FC4, { 0x389, 0x399 } },
	{ 0x1FC6, { 0x397, 0x342 } }, { 0x1FC7, { 0x397, 0x342, 0x399 } }, { 0x1FD2, { 0x3AA, 0x300 } },
	{ 0x1FD3, { 0x3AA, 0x301 } }, { 0x1FD6, { 0x399, 0x342 } }, { 0x1FD7, { 0x3AA, 0x342 } },
	{ 0x1FE2, { 0x3AB, 0x300 } }, { 0x1FE3, { 0x3AB, 0x301 } }, { 0x1FE4, { 0x3A1, 0x313 } },
	{ 0x1FE6, { 0x3A5, 0x342 } }, { 0x1FE7, { 0x3AB, 0x342 } }, { 0x1FF2, { 0x1FFA, 0x399 } },
	{ 0x1FF4, { 0x38F, 0x399 } }, { 0x1FF6, { 0x3A9, 0x342 } }, { 0x1FF7, { 0x3A9, 0x342, 0x399 } },
};

/* a letter with a case (marks between letters, U+0300 to U+036F, are
 * part of the letter before) */
static bool is_cased(uint32_t c)
{
	return case_of(c, true) != c || case_of(c, false) != c || c == 0xDF || c == 0x3C2;
}

/* whether a letter follows at u (marks skipped): for Σ at a word's end */
static bool letter_follows(const unsigned char *u, int64_t n)
{
	for (int64_t i = 0; i < n;) {
		int len;
		uint32_t c = utf8_at(u + i, n - i, &len);
		if (c >= 0x300 && c <= 0x36F) {
			i += len;
			continue;
		}
		return c != 0xFFFFFFFF && is_cased(c);
	}
	return false;
}

/* A copy in upper (or lower) case; *outn its length, which can differ
 * (ß is SS, and ı's I is a byte shorter). */
char *rt_str_case(const char *d, int64_t n, bool upper, int64_t *outn)
{
	/* (at most twice as long: SS for ß, and no letter's other case
	 * takes more than one byte more) */
	char *p = alloc_text(n * 2);
	int64_t o = 0;
	const unsigned char *u = (const unsigned char *)d;
	bool after_letter = false;   /* (for Σ, which is ς at the end of a word) */
	for (int64_t i = 0; i < n;) {
		unsigned char b = u[i];
		if (b < 0x80) {
			p[o++] = upper && b >= 'a' && b <= 'z' ? (char)(b - 32) : !upper && b >= 'A' && b <= 'Z' ? (char)(b + 32)
											          : (char)b;
			after_letter = (b | 32) >= 'a' && (b | 32) <= 'z';
			i++;
			continue;
		}
		int len;
		uint32_t c = utf8_at(u + i, n - i, &len);
		if (c == 0xFFFFFFFF) {
			p[o++] = (char)b;
			i++;
			continue;
		}
		bool mark = c >= 0x300 && c <= 0x36F;
		bool done = false;
		if (upper && c == 0xDF) {
			p[o++] = 'S';
			p[o++] = 'S';
			done = true;
		} else if (upper && c >= 0x1F50 && c <= 0x1FF7) {
			for (size_t k = 0; k < sizeof greek_upper / sizeof *greek_upper; k++)
				if (greek_upper[k].c == c) {
					for (int m = 0; m < 3 && greek_upper[k].to[m]; m++)
						o += utf8_put(p + o, greek_upper[k].to[m]);
					done = true;
					break;
				}
		} else if (!upper && c == 0x3A3 && after_letter && !letter_follows(u + i + len, n - i - len)) {
			o += utf8_put(p + o, 0x3C2);
			done = true;
		}
		if (!done)
			o += utf8_put(p + o, case_of(c, upper));
		if (!mark)
			after_letter = is_cased(c);
		i += len;
	}
	p[o] = '\0';
	*outn = o;
	return p;
}

/* Every `from` replaced by `to`, left to right, not overlapping. An empty
 * `from` changes nothing. */
char *rt_str_replace(const char *d, int64_t n, const char *from, int64_t fn, const char *to, int64_t tn,
		     int64_t *outn)
{
	int64_t count = 0;
	if (fn > 0) {
		int64_t at = 0, k;
		while ((k = rt_str_find(d + at, n - at, from, fn)) >= 0) {
			count++;
			at += k + fn;
		}
	}
	int64_t m = n + count * (tn - fn);
	char *p = alloc_text(m);
	if (count == 0) {
		memcpy(p, d, (size_t)n);
	} else {
		int64_t at = 0, o = 0, k;
		while ((k = rt_str_find(d + at, n - at, from, fn)) >= 0) {
			memcpy(p + o, d + at, (size_t)k);
			o += k;
			memcpy(p + o, to, (size_t)tn);
			o += tn;
			at += k + fn;
		}
		memcpy(p + o, d + at, (size_t)(n - at));
	}
	*outn = m;
	return p;
}

char *rt_str_repeat(const char *d, int64_t n, int64_t times, int64_t *outn, const char *file, int line, int col)
{
	if (times < 0)
		rt_panic(file, line, col, "can't repeat text %lld times", (long long)times);
	if (n > 0 && times > (INT64_MAX / 2) / n)
		rt_panic(file, line, col, "repeating %lld characters %lld times is too long", (long long)n,
			 (long long)times);
	int64_t m = n * times;
	char *p = alloc_text(m);
	for (int64_t i = 0; i < times; i++)
		memcpy(p + i * n, d, (size_t)n);
	*outn = m;
	return p;
}

/* The text with spaces added to make it width characters (letters, not
 * bytes: "é" is one): in front (how 0), after (1), or either side, the
 * odd one after (2). Text that's as wide already is as it is. */
char *rt_str_pad(const char *d, int64_t n, int64_t width, int how, int64_t *outn)
{
	int64_t chars = 0;
	for (int64_t i = 0; i < n; i++)
		if (((unsigned char)d[i] & 0xC0) != 0x80)
			chars++;
	int64_t add = width > chars ? width - chars : 0;
	int64_t before = how == 0 ? add : how == 1 ? 0 : add / 2, after = add - before;
	int64_t m = n + add;
	char *p = alloc_text(m);
	memset(p, ' ', (size_t)before);
	memcpy(p + before, d, (size_t)n);
	memset(p + before + n, ' ', (size_t)after);
	*outn = m;
	return p;
}

static void slice_check(const char *what, int64_t len, int64_t from, int64_t to, int flags, int64_t *start,
			int64_t *count, const char *file, int line, int col)
{
	if (flags & RT_SLICE_NO_FROM)
		from = 0;
	if (flags & RT_SLICE_NO_TO)
		to = len - 1;
	if (from < 0 || from > len)
		rt_panic(file, line, col, "slice start %lld is outside a %s of length %lld", (long long)from, what,
			 (long long)len);
	if (to >= len)
		rt_panic(file, line, col, "slice end %lld is outside a %s of length %lld", (long long)to, what,
			 (long long)len);
	if (to < from - 1)
		rt_panic(file, line, col, "slice [%lld to %lld] runs backwards", (long long)from, (long long)to);
	*start = from;
	*count = to - from + 1;
}

void rt_list_slice(int64_t len, int64_t from, int64_t to, int flags, int64_t *start, int64_t *count,
		   const char *file, int line, int col)
{
	slice_check("list", len, from, to, flags, start, count, file, line, col);
}

/* A position in a list: 0 to len - 1, or up to len for .insert. */
_Noreturn void rt_list_bounds(int64_t index, int64_t len, bool insert, const char *file, int line, int col)
{
	if (len == 0 && !insert)
		rt_panic(file, line, col, "index %lld is outside an empty list", (long long)index);
	rt_panic(file, line, col, "index %lld is outside a list of length %lld (positions 0 to %lld%s)",
		 (long long)index, (long long)len, (long long)(insert ? len : len - 1), insert ? ", for .insert" : "");
}

/* Checks a slice s[from to to] of a string of length `len`; a missing end
 * (RT_SLICE_NO_FROM / NO_TO) is the start or the end. Gives where it
 * starts and how long it is. `to` may be one before `from` (an empty
 * slice); anything else outside the string panics. */
void rt_str_slice(int64_t len, int64_t from, int64_t to, int flags, int64_t *start, int64_t *count,
		  const char *file, int line, int col)
{
	slice_check("string", len, from, to, flags, start, count, file, line, col);
}

/* ------------------------------------------------------------- compiled-program helpers */

#include <signal.h>

static int64_t str_len(const cby_str *s) { return s->len & ~CBY_STR_STATIC; }

static void on_segv(int sig)
{
	(void)sig;
	static const char msg[] = "panic: stack overflow (or invalid memory access)\n";
	fflush(stdout);
	if (write(2, msg, sizeof msg - 1) < 0) { /* nothing more we can do */ }
	_exit(RT_PANIC_EXIT);
}

void cby_rt_init(void)
{
	static char altstack[64 * 1024];
	stack_t ss;
	ss.ss_sp = altstack;
	ss.ss_size = sizeof altstack;
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_segv;
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
}

int cby_rt_finish(int status)
{
	fflush(stdout);
	return status & 0xFF;
}

void cby_str_dup(cby_str *dst, const char *data, int64_t len)
{
	len &= ~CBY_STR_STATIC;
	char *p = rt_alloc((size_t)len + 1);
	memcpy(p, data, (size_t)len);
	p[len] = '\0';
	dst->data = p;
	dst->len = len;
}

void cby_str_drop(cby_str *s)
{
	if (!(s->len & CBY_STR_STATIC))
		free(s->data);
	s->data = NULL;
	s->len = 0;
}

bool cby_str_eq(const cby_str *a, const cby_str *b)
{
	int64_t n = str_len(a);
	return n == str_len(b) && (n == 0 || memcmp(a->data, b->data, (size_t)n) == 0);
}

const char *cby_str_cstr(const cby_str *s) { return s->data ? s->data : ""; }

/* Dropping a chain (an object holding a list of its own kind, holding
 * another, ...) goes one list inside another. Past a few hundred levels,
 * a list is set aside instead (its header copied, as its owner is going
 * anyway), and the outermost drop works through those set aside one at a
 * time, so a chain of any length drops in a little stack. */
#define DROP_DEPTH 256
typedef struct { unsigned char bytes[64]; void (*fn)(void *); } SetAside;
static _Thread_local int drop_depth;
static _Thread_local SetAside *set_aside;
static _Thread_local int64_t n_aside, cap_aside;

void cby_drop_nested(void *p, int64_t n, void (*fn)(void *))
{
	if (drop_depth >= DROP_DEPTH) {
		if (n_aside == cap_aside) {
			cap_aside = cap_aside ? cap_aside * 2 : 64;
			set_aside = rt_realloc(set_aside, sizeof *set_aside * (size_t)cap_aside);
		}
		memcpy(set_aside[n_aside].bytes, p, (size_t)n);
		set_aside[n_aside++].fn = fn;
		return;
	}
	drop_depth++;
	fn(p);
	if (drop_depth == 1) {
		while (n_aside) {
			SetAside s = set_aside[--n_aside];
			s.fn(s.bytes);
		}
		free(set_aside);
		set_aside = NULL;
		cap_aside = 0;
	}
	drop_depth--;
}

/* addresses.of_text: the string's own characters, which may be changed
 * through the address, so a literal (or the empty string) gets a private
 * copy first, as for s[i] = c */
char *cby_text_address(cby_str *s)
{
	if (!s->data || (s->len & CBY_STR_STATIC))
		cby_str_dup(s, s->data ? s->data : "", str_len(s));
	return s->data;
}

int64_t cby_str_len(const cby_str *s) { return str_len(s); }

void cby_text(cby_str *dst, int what, int64_t i, double d)
{
	char buf[48];
	int n = rt_format_text(buf, what, i, d);
	cby_str_dup(dst, buf, n);
}

void cby_str_copy(cby_str *dst, const cby_str *src)
{
	cby_str_dup(dst, src->data ? src->data : "", str_len(src));
}

/* a and b may be the same string, or dst's own (dst is written last) */
void cby_str_concat(cby_str *dst, const cby_str *a, const cby_str *b)
{
	int64_t na = str_len(a), nb = str_len(b);
	char *p = rt_alloc((size_t)(na + nb) + 1);
	if (na)
		memcpy(p, a->data, (size_t)na);
	if (nb)
		memcpy(p + na, b->data, (size_t)nb);
	p[na + nb] = '\0';
	dst->data = p;
	dst->len = na + nb;
}

/* a + b + c + ...: every part joined in one allocation, rather than one
 * per `+` with the in-between strings thrown away. */
void cby_str_concat_n(cby_str *dst, const cby_str *const *parts, int n)
{
	int64_t total = 0;
	for (int i = 0; i < n; i++)
		total += str_len(parts[i]);
	char *p = rt_alloc((size_t)total + 1), *o = p;
	for (int i = 0; i < n; i++) {
		int64_t k = str_len(parts[i]);
		if (k)
			memcpy(o, parts[i]->data, (size_t)k);
		o += k;
	}
	*o = '\0';
	dst->data = p;
	dst->len = total;
}

/* s += b. A string's buffer may be bigger than its text: the allocator
 * knows the real size, so the text is added in place while it fits, and
 * otherwise the buffer is regrown to double the size. Building a string a
 * piece at a time then costs time in proportion to its length, not its
 * length squared. A literal is copied first; `s += s` takes the plain
 * path, which never reads from a buffer it has just moved. */
void cby_str_append(cby_str *s, const cby_str *b)
{
	int64_t na = str_len(s), nb = str_len(b);
	if (nb == 0)
		return;
	bool alias = s->data && b->data >= s->data && b->data <= s->data + na;
	if (s->data && !(s->len & CBY_STR_STATIC) && !alias) {
		size_t need = (size_t)(na + nb) + 1;
		size_t have = rt_usable_size(s->data);
		if (have < need) {
			size_t cap = have * 2;
			if (cap < need)
				cap = need;
			if (cap < 32)
				cap = 32;
			char *p = realloc(s->data, cap);
			if (!p)
				rt_panic(NULL, 0, 0, "out of memory");
			s->data = p;
		}
		memcpy(s->data + na, b->data, (size_t)nb);
		s->data[na + nb] = '\0';
		s->len = na + nb;
		return;
	}
	cby_str r;
	cby_str_concat(&r, s, b);
	cby_str_drop(s);
	*s = r;
}

int8_t cby_str_get(const cby_str *s, int64_t i, const char *file, int line, int col)
{
	int64_t n = str_len(s);
	if (i < 0 || i >= n)
		rt_str_bounds(i, n, file, line, col);
	return (int8_t)s->data[i];
}

void cby_str_set(cby_str *s, int64_t i, int8_t c, const char *file, int line, int col)
{
	int64_t n = str_len(s);
	if (i < 0 || i >= n)
		rt_str_bounds(i, n, file, line, col);
	if (s->len & CBY_STR_STATIC)   /* a literal is never changed: take a private copy first */
		cby_str_dup(s, s->data, n);
	s->data[i] = (char)c;
}

void cby_print_str(const cby_str *s)
{
	if (s->data)
		rt_print_str(s->data, str_len(s));
}

cby_str *cby_args(int argc, char **argv)
{
	cby_str *a = rt_alloc(sizeof *a * (size_t)(argc > 0 ? argc : 1));
	for (int i = 0; i < argc; i++) {
		a[i].data = argv[i];
		a[i].len = (int64_t)strlen(argv[i]) | CBY_STR_STATIC;
	}
	return a;
}

void cby_args_free(cby_str *args) { free(args); }

/* ------------------------------------------------------------- input, for compiled programs */

void cby_read_line(cby_str *dst, const char *file, int line, int col)
{
	int64_t n;
	char *p = rt_read_line(&n, file, line, col);
	dst->data = p;
	dst->len = n;
}

void cby_input(cby_str *dst, const cby_str *prompt, const char *file, int line, int col)
{
	rt_print_str(prompt->data ? prompt->data : "", str_len(prompt));
	cby_read_line(dst, file, line, col);
}

bool cby_end_of_input(void) { return rt_input_ended; }

bool cby_str_is_number(const cby_str *s, int what)
{
	return rt_parse_number(s->data ? s->data : "", str_len(s), what, NULL, NULL) == 1;
}

int64_t cby_str_to_int(const cby_str *s, int what, const char *file, int line, int col)
{
	return rt_text_to_int(s->data ? s->data : "", str_len(s), what, file, line, col);
}

double cby_str_to_double(const cby_str *s, const char *file, int line, int col)
{
	return rt_text_to_double(s->data ? s->data : "", str_len(s), file, line, col);
}

/* ------------------------------------------------------------- string tools, for compiled programs */

#define DATA(s) ((s)->data ? (s)->data : "")

int64_t cby_str_find(const cby_str *h, const cby_str *n)
{
	return rt_str_find(DATA(h), str_len(h), DATA(n), str_len(n));
}

bool cby_str_test(const cby_str *h, const cby_str *n, int what)
{
	return rt_str_test(DATA(h), str_len(h), DATA(n), str_len(n), what);
}

int cby_str_compare(const cby_str *a, const cby_str *b)
{
	return rt_str_compare(DATA(a), str_len(a), DATA(b), str_len(b));
}

void cby_str_trim(cby_str *dst, const cby_str *s)
{
	int64_t a, n;
	rt_str_trim(DATA(s), str_len(s), &a, &n);
	cby_str_dup(dst, DATA(s) + a, n);
}

void cby_str_case(cby_str *dst, const cby_str *s, bool upper)
{
	int64_t n;
	dst->data = rt_str_case(DATA(s), str_len(s), upper, &n);
	dst->len = n;
}

void cby_str_replace(cby_str *dst, const cby_str *s, const cby_str *from, const cby_str *to)
{
	int64_t n;
	dst->data = rt_str_replace(DATA(s), str_len(s), DATA(from), str_len(from), DATA(to), str_len(to), &n);
	dst->len = n;
}

void cby_str_repeat(cby_str *dst, const cby_str *s, int64_t times, const char *file, int line, int col)
{
	int64_t n;
	dst->data = rt_str_repeat(DATA(s), str_len(s), times, &n, file, line, col);
	dst->len = n;
}

void cby_str_pad(cby_str *dst, const cby_str *s, int64_t width, int how)
{
	int64_t n;
	dst->data = rt_str_pad(DATA(s), str_len(s), width, how, &n);
	dst->len = n;
}

void cby_str_slice(cby_str *dst, const cby_str *s, int64_t from, int64_t to, int flags,
		   const char *file, int line, int col)
{
	int64_t a, n;
	rt_str_slice(str_len(s), from, to, flags, &a, &n, file, line, col);
	cby_str_dup(dst, DATA(s) + a, n);
}

/* ------------------------------------------------------------- lists, for compiled programs
 * A list is { data, len, cap }; elements are stored inline, `size` bytes
 * each. The generated code drops, copies and compares elements itself;
 * these only move bytes and check positions. */

typedef struct { char *data; int64_t len, cap; } cby_list;

void cby_list_reserve(cby_list *l, int64_t size, int64_t more)
{
	if (l->len + more <= l->cap)
		return;
	int64_t cap = l->cap ? l->cap * 2 : 4;
	while (cap < l->len + more)
		cap *= 2;
	char *n = realloc(l->data, (size_t)(cap * size));
	if (!n)
		rt_panic(NULL, 0, 0, "out of memory");
	l->data = n;
	l->cap = cap;
}

/* Room for one more at the end: gives its (uninitialized) slot. */
void *cby_list_push(cby_list *l, int64_t size)
{
	cby_list_reserve(l, size, 1);
	return l->data + (l->len++) * size;
}

void *cby_list_insert_slot(cby_list *l, int64_t size, int64_t at, const char *file, int line, int col)
{
	if (at < 0 || at > l->len)
		rt_list_bounds(at, l->len, true, file, line, col);
	cby_list_reserve(l, size, 1);
	memmove(l->data + (at + 1) * size, l->data + at * size, (size_t)((l->len - at) * size));
	l->len++;
	return l->data + at * size;
}

/* The element at `at`, checked. */
void *cby_list_at(const cby_list *l, int64_t size, int64_t at, const char *file, int line, int col)
{
	if (at < 0 || at >= l->len)
		rt_list_bounds(at, l->len, false, file, line, col);
	return l->data + at * size;
}

/* After the element at `at` has been dropped: close the gap. */
void cby_list_close_gap(cby_list *l, int64_t size, int64_t at)
{
	memmove(l->data + at * size, l->data + (at + 1) * size, (size_t)((l->len - at - 1) * size));
	l->len--;
}

/* .take_last: the last slot, which the caller moves out of. */
void *cby_list_take_last(cby_list *l, int64_t size, const char *file, int line, int col)
{
	if (l->len == 0)
		rt_panic(file, line, col, "take_last on an empty list");
	return l->data + (--l->len) * size;
}

void cby_list_free(cby_list *l)
{
	free(l->data);
	l->data = NULL;
	l->len = l->cap = 0;
}

/* ------------------------------------------------------------- lookups */

uint64_t rt_hash_u64(uint64_t x)
{
	/* Fibonacci hashing: the key times 2^64 / the golden ratio, whose top
	 * half (where every bit of the key has moved it) is turned to the
	 * bottom, where the index takes its slot from. Keys near each other
	 * (counts, ids) land evenly spread, and one multiply is quick. */
	x *= 0x9E3779B97F4A7C15ULL;
	return (x >> 32) | (x << 32);
}

uint64_t rt_hash_bytes(const char *p, int64_t n)
{
	uint64_t h = 0xcbf29ce484222325ULL;   /* FNV-1a */
	for (int64_t i = 0; i < n; i++) {
		h ^= (unsigned char)p[i];
		h *= 0x100000001b3ULL;
	}
	return rt_hash_u64(h);
}

int64_t rt_index_find(const int32_t *index, int64_t icap, uint64_t hash, bool (*same)(const void *ctx, int64_t pos),
		      const void *ctx)
{
	if (icap == 0)
		return -1;
	uint64_t mask = (uint64_t)icap - 1;
	for (uint64_t i = hash & mask;; i = (i + 1) & mask) {
		int32_t s = index[i];
		if (s == 0)
			return -1;
		if (s > 0 && same(ctx, s - 1))
			return s - 1;
	}
}

void rt_index_remove(int32_t *index, int64_t icap, uint64_t hash, int64_t pos)
{
	uint64_t mask = (uint64_t)icap - 1;
	for (uint64_t i = hash & mask; index[i]; i = (i + 1) & mask)
		if (index[i] == pos + 1) {
			index[i] = -1;
			return;
		}
}

void rt_index_add(int32_t *index, int64_t icap, uint64_t hash, int64_t pos)
{
	uint64_t mask = (uint64_t)icap - 1;
	uint64_t i = hash & mask;
	while (index[i])
		i = (i + 1) & mask;
	index[i] = (int32_t)(pos + 1);
}

int64_t rt_index_size(int64_t entries)
{
	if (entries >= INT32_MAX / 2)
		rt_panic(NULL, 0, 0, "a lookup can hold at most %d entries", INT32_MAX / 2 - 1);
	int64_t n = 8;
	while (n < entries * 2)
		n *= 2;
	return n;
}

_Noreturn void rt_lookup_missing_int(int64_t key, int kind, const char *file, int line, int col)
{
	char shown[48];
	if (kind == RT_KEY_CHAR)
		snprintf(shown, sizeof shown, "'%c'", (char)key);
	else if (kind == RT_KEY_BOOL)
		snprintf(shown, sizeof shown, "%s", key ? "true" : "false");
	else if (kind == RT_KEY_UINT)
		snprintf(shown, sizeof shown, "%llu", (unsigned long long)key);
	else
		snprintf(shown, sizeof shown, "%lld", (long long)key);
	rt_panic(file, line, col, "the key %s isn't in the lookup (check with .contains first)", shown);
}

_Noreturn void rt_lookup_missing_str(const char *key, int64_t len, const char *file, int line, int col)
{
	if (len > 60)
		rt_panic(file, line, col, "the key \"%.57s...\" isn't in the lookup (check with .contains first)", key);
	rt_panic(file, line, col, "the key \"%.*s\" isn't in the lookup (check with .contains first)", (int)len, key);
}

/* Compiled programs' lookups: { keys, vals, dead, index, n, len, cap, icap }.
 * Keys and values are stored in place, ksize and vsize bytes each; n
 * positions are in use or dead (removed), len of them in use. */
typedef struct {
	char *keys, *vals;
	uint8_t *dead;
	int32_t *index;   /* position + 1; 0 empty; -1 removed */
	int64_t n, len, cap, icap;
} cby_lookup;


/* A whole-number key's bits (bool and char too), zero-extended */
static inline uint64_t key_bits(const void *p, int64_t ksize)
{
	switch (ksize) {
	case 1: return *(const uint8_t *)p;
	case 2: { uint16_t v; memcpy(&v, p, 2); return v; }
	case 4: { uint32_t v; memcpy(&v, p, 4); return v; }
	default: { uint64_t v; memcpy(&v, p, 8); return v; }
	}
}

static inline __attribute__((always_inline)) uint64_t key_hash(int kind, int64_t ksize, const void *key)
{
	if (kind == RT_KEY_STR) {
		const cby_str *s = key;
		return rt_hash_bytes(DATA(s), str_len(s));
	}
	return rt_hash_u64(key_bits(key, ksize));
}

/* Removed keys are marked in the index (-1), so a search never needs the
 * dead flags; whole-number keys are compared as numbers, in place. */
/* The key's position, or -1; then *empty (if given) is the index slot where
 * the search ended, where the key can be added. */
static inline __attribute__((always_inline)) int64_t lookup_find_h(const cby_lookup *l, int kind, int64_t ksize, const void *key, uint64_t h,
			     int64_t *empty)
{
	if (l->icap == 0)
		return -1;
	uint64_t mask = (uint64_t)l->icap - 1;
	const int32_t *index = l->index;
	if (kind != RT_KEY_STR) {
		/* a loop for each size of key, so each turn is a compare of two
		 * numbers, as in a table written for the one kind */
#define FIND_NUMBER(T) { \
			T want; \
			memcpy(&want, key, sizeof want); \
			const T *keys = (const T *)(const void *)l->keys; \
			for (uint64_t i = h & mask;; i = (i + 1) & mask) { \
				int32_t s = index[i]; \
				if (s > 0) { \
					if (keys[s - 1] == want) \
						return s - 1; \
				} else if (s == 0) { \
					if (empty) \
						*empty = (int64_t)i; \
					return -1; \
				} \
			} \
		}
		switch (ksize) {
		case 1: FIND_NUMBER(uint8_t)
		case 2: FIND_NUMBER(uint16_t)
		case 4: FIND_NUMBER(uint32_t)
		default: FIND_NUMBER(uint64_t)
		}
#undef FIND_NUMBER
	}
	const cby_str *k = key;
	const char *kd = DATA(k);
	int64_t kn = str_len(k);
	for (uint64_t i = h & mask;; i = (i + 1) & mask) {
		int32_t s = l->index[i];
		if (s == 0) {
			if (empty)
				*empty = (int64_t)i;
			return -1;
		}
		if (s > 0) {
			const cby_str *e = (const cby_str *)(l->keys + (int64_t)(s - 1) * ksize);
			if (str_len(e) == kn && (kn == 0 || memcmp(e->data, kd, (size_t)kn) == 0))
				return s - 1;
		}
	}
}

/* Where a key was last found, on this thread: `if L.contains(K) { L[K] +=
 * 1 }` looks for K twice, and the second time it's checked there first.
 * It's only a guess, checked each time (the position still in use, and
 * holding that key), so nothing that changes a lookup needs to forget it. */
static _Thread_local const cby_lookup *last_lookup;
static _Thread_local int64_t last_pos;

static inline __attribute__((always_inline)) bool last_holds(const cby_lookup *l, int kind, int64_t ksize, const void *key)
{
	if (l != last_lookup || last_pos >= l->n || l->dead[last_pos])
		return false;
	const char *at = l->keys + last_pos * ksize;
	if (kind != RT_KEY_STR)
		return key_bits(at, ksize) == key_bits(key, ksize);
	const cby_str *e = (const cby_str *)at, *k = key;
	int64_t kn = str_len(k);
	return str_len(e) == kn && (kn == 0 || memcmp(e->data, DATA(k), (size_t)kn) == 0);
}

/* Whole-number keys, the usual kind, each size its own loop: the hash,
 * the guess and the search in one place, so each look is a few
 * instructions (and the processor can be waiting on several at once). */
#define FIND_NUMBER_FN(NAME, T) \
static inline int64_t NAME(const cby_lookup *l, const void *key) \
{ \
	T want; \
	memcpy(&want, key, sizeof want); \
	const T *keys = (const T *)(const void *)l->keys; \
	if (l == last_lookup && last_pos < l->n && keys[last_pos] == want && !l->dead[last_pos]) \
		return last_pos; \
	uint64_t mask = (uint64_t)l->icap - 1; \
	const int32_t *index = l->index; \
	for (uint64_t i = rt_hash_u64((uint64_t)want) & mask;; i = (i + 1) & mask) { \
		int32_t s = index[i]; \
		if (s > 0) { \
			if (keys[s - 1] == want) { \
				last_lookup = l; \
				last_pos = s - 1; \
				return s - 1; \
			} \
		} else if (s == 0) { \
			return -1; \
		} \
	} \
}
FIND_NUMBER_FN(find_number1, uint8_t)
FIND_NUMBER_FN(find_number2, uint16_t)
FIND_NUMBER_FN(find_number4, uint32_t)
FIND_NUMBER_FN(find_number8, uint64_t)
#undef FIND_NUMBER_FN

int64_t cby_lookup_find(const cby_lookup *l, int kind, int64_t ksize, const void *key)
{
	if (l->len == 0)
		return -1;
	if (kind != RT_KEY_STR)
		switch (ksize) {
		case 1: return find_number1(l, key);
		case 2: return find_number2(l, key);
		case 4: return find_number4(l, key);
		default: return find_number8(l, key);
		}
	if (last_holds(l, kind, ksize, key))
		return last_pos;
	int64_t pos = lookup_find_h(l, kind, ksize, key, key_hash(kind, ksize, key), NULL);
	if (pos >= 0) {
		last_lookup = l;
		last_pos = pos;
	}
	return pos;
}

/* Lookup[key], for reading or changing a value that must be there */
int64_t cby_lookup_at(const cby_lookup *l, int kind, int64_t ksize, const void *key, const char *file, int line,
		      int col)
{
	int64_t pos = cby_lookup_find(l, kind, ksize, key);
	if (pos >= 0)
		return pos;
	if (kind == RT_KEY_STR)
		rt_lookup_missing_str(DATA((const cby_str *)key), str_len(key), file, line, col);
	int64_t v = 0;
	if (kind == RT_KEY_INT) {
		switch (ksize) {
		case 1: v = *(const int8_t *)key; break;
		case 2: v = *(const int16_t *)key; break;
		case 4: v = *(const int32_t *)key; break;
		default: v = *(const int64_t *)key; break;
		}
	} else {
		memcpy(&v, key, (size_t)ksize);
	}
	rt_lookup_missing_int(v, kind, file, line, col);
}

static void lookup_reindex(cby_lookup *l, int kind, int64_t ksize)
{
	int64_t icap = rt_index_size(l->cap);
	if (icap != l->icap) {
		rt_free(l->index);
		l->index = rt_alloc((size_t)icap * sizeof(int32_t));
		l->icap = icap;
	}
	memset(l->index, 0, (size_t)icap * sizeof(int32_t));
	for (int64_t p = 0; p < l->n; p++)
		if (!l->dead[p])
			rt_index_add(l->index, icap, key_hash(kind, ksize, l->keys + p * ksize), p);
}

/* Moves the entries in use together, closing the gaps removals left. */
static void lookup_compact(cby_lookup *l, int kind, int64_t ksize, int64_t vsize)
{
	int64_t to = 0;
	for (int64_t p = 0; p < l->n; p++) {
		if (l->dead[p])
			continue;
		if (to != p) {
			memcpy(l->keys + to * ksize, l->keys + p * ksize, (size_t)ksize);
			memcpy(l->vals + to * vsize, l->vals + p * vsize, (size_t)vsize);
			l->dead[to] = 0;
		}
		to++;
	}
	l->n = to;
	lookup_reindex(l, kind, ksize);
}

/* Lookup[key] = value: the position for the value, adding the key (a copy)
 * with a zeroed value when it is new (*is_new is then 1). */
static inline __attribute__((always_inline)) int64_t put_body(cby_lookup *l, int kind, int64_t ksize, int64_t vsize, const void *key, uint8_t *is_new)
{
	if (l->len && last_holds(l, kind, ksize, key)) {
		*is_new = 0;
		return last_pos;
	}
	uint64_t h = key_hash(kind, ksize, key);
	int64_t empty = -1;
	int64_t pos = l->icap == 0 ? -1 : lookup_find_h(l, kind, ksize, key, h, &empty);
	*is_new = pos < 0;
	if (pos >= 0)
		return pos;
	if (l->n == l->cap) {
		empty = -1;   /* the index is rebuilt */
		if (l->n > l->len && l->n - l->len >= l->n / 2) {   /* at least half removed */
			lookup_compact(l, kind, ksize, vsize);
		} else {
			int64_t cap = l->cap ? l->cap * 2 : 8;
			l->keys = rt_realloc(l->keys, (size_t)(cap * ksize));
			l->vals = rt_realloc(l->vals, (size_t)(cap * vsize));
			l->dead = rt_realloc(l->dead, (size_t)cap);
			l->cap = cap;
			lookup_reindex(l, kind, ksize);
		}
	}
	pos = l->n++;
	char *k = l->keys + pos * ksize;
	switch (kind == RT_KEY_STR ? 0 : ksize) {
	case 0: cby_str_copy((cby_str *)k, key); break;
	case 1: *k = *(const char *)key; break;
	case 2: memcpy(k, key, 2); break;
	case 4: memcpy(k, key, 4); break;
	default: memcpy(k, key, 8); break;
	}
	char *v = l->vals + pos * vsize;
	switch (vsize) {
	case 1: *v = 0; break;
	case 2: memset(v, 0, 2); break;
	case 4: memset(v, 0, 4); break;
	case 8: memset(v, 0, 8); break;
	case 16: memset(v, 0, 16); break;
	default: memset(v, 0, (size_t)vsize); break;
	}
	l->dead[pos] = 0;
	l->len++;
	if (empty >= 0)
		l->index[empty] = (int32_t)(pos + 1);
	else
		rt_index_add(l->index, l->icap, h, pos);
	last_lookup = l;
	last_pos = pos;
	return pos;
}

/* (only whether keys are text matters inside, so numbers say RT_KEY_INT) */
int64_t cby_lookup_put(cby_lookup *l, int kind, int64_t ksize, int64_t vsize, const void *key, uint8_t *is_new)
{
	if (kind == RT_KEY_STR)
		return put_body(l, RT_KEY_STR, ksize, vsize, key, is_new);
	switch (ksize) {
	case 1: return put_body(l, RT_KEY_INT, 1, vsize, key, is_new);
	case 2: return put_body(l, RT_KEY_INT, 2, vsize, key, is_new);
	case 4: return put_body(l, RT_KEY_INT, 4, vsize, key, is_new);
	default: return put_body(l, RT_KEY_INT, 8, vsize, key, is_new);
	}
}

/* After the value at pos has been dropped: the key goes too. */
void cby_lookup_kill(cby_lookup *l, int kind, int64_t ksize, int64_t pos)
{
	char *k = l->keys + pos * ksize;
	rt_index_remove(l->index, l->icap, key_hash(kind, ksize, k), pos);
	if (kind == RT_KEY_STR)
		cby_str_drop((cby_str *)k);
	l->dead[pos] = 1;
	l->len--;
	if (l->len == 0) {
		l->n = 0;
		memset(l->index, 0, (size_t)l->icap * sizeof(int32_t));
	}
}

int64_t cby_lookup_next(const cby_lookup *l, int64_t pos)
{
	for (; pos < l->n; pos++)
		if (!l->dead[pos])
			return pos;
	return -1;
}

/* After every value has been dropped: the keys, then empty (clear) or freed. */
void cby_lookup_clear(cby_lookup *l, int kind, int64_t ksize)
{
	if (kind == RT_KEY_STR)
		for (int64_t p = 0; p < l->n; p++)
			if (!l->dead[p])
				cby_str_drop((cby_str *)(l->keys + p * ksize));
	l->n = l->len = 0;
	if (l->index)
		memset(l->index, 0, (size_t)l->icap * sizeof(int32_t));
}

void cby_lookup_free(cby_lookup *l, int kind, int64_t ksize)
{
	cby_lookup_clear(l, kind, ksize);
	rt_free(l->keys);
	rt_free(l->vals);
	rt_free(l->dead);
	rt_free(l->index);
	memset(l, 0, sizeof *l);
}

/* .copy: dst gets copies of src's keys, in order with no gaps, and zeroed
 * values for the caller to fill (position k of dst is the k-th in use). */
void cby_lookup_copy_keys(cby_lookup *dst, const cby_lookup *src, int kind, int64_t ksize, int64_t vsize)
{
	memset(dst, 0, sizeof *dst);
	if (src->len == 0)
		return;
	int64_t cap = src->len;
	dst->keys = rt_alloc((size_t)(cap * ksize));
	dst->vals = rt_alloc_zeroed((size_t)(cap * vsize));
	dst->dead = rt_alloc_zeroed((size_t)cap);
	dst->cap = cap;
	int64_t to = 0;
	for (int64_t p = 0; p < src->n; p++) {
		if (src->dead[p])
			continue;
		char *k = dst->keys + to * ksize;
		if (kind == RT_KEY_STR)
			cby_str_copy((cby_str *)k, (const cby_str *)(src->keys + p * ksize));
		else
			memcpy(k, src->keys + p * ksize, (size_t)ksize);
		to++;
	}
	dst->n = dst->len = to;
	lookup_reindex(dst, kind, ksize);
}

/* .keys: a list of copies of the keys, in order */
void cby_lookup_keys(cby_list *dst, const cby_lookup *l, int kind, int64_t ksize)
{
	memset(dst, 0, sizeof *dst);
	if (l->len == 0)
		return;
	cby_list_reserve(dst, ksize, l->len);
	for (int64_t p = 0; p < l->n; p++) {
		if (l->dead[p])
			continue;
		char *k = dst->data + dst->len * ksize;
		if (kind == RT_KEY_STR)
			cby_str_copy((cby_str *)k, (const cby_str *)(l->keys + p * ksize));
		else
			memcpy(k, l->keys + p * ksize, (size_t)ksize);
		dst->len++;
	}
}

/* println of a lookup: one key, shown as println shows it */
void cby_lookup_print_key(const cby_lookup *l, int kind, int64_t ksize, int64_t pos)
{
	const char *k = l->keys + pos * ksize;
	if (kind == RT_KEY_STR) {
		cby_print_str((const cby_str *)k);
		return;
	}
	if (kind == RT_KEY_BOOL) {
		rt_print_bool(*(const uint8_t *)k != 0);
		return;
	}
	if (kind == RT_KEY_CHAR) {
		rt_print_char(*(const uint8_t *)k);
		return;
	}
	int64_t v = 0;
	if (kind == RT_KEY_INT) {
		switch (ksize) {
		case 1: v = *(const int8_t *)k; break;
		case 2: v = *(const int16_t *)k; break;
		case 4: v = *(const int32_t *)k; break;
		default: v = *(const int64_t *)k; break;
		}
		rt_print_int(v);
	} else {
		memcpy(&v, k, (size_t)ksize);
		rt_print_uint((uint64_t)v);
	}
}

/* L.sort: smallest first. kind is one of RT_SORT_*; strings go in
 * dictionary order (byte by byte), and a float that isn't a number goes last. */
#define CMP_NUM(name, T) \
	static int name(const void *a, const void *b) \
	{ T x = *(const T *)a, y = *(const T *)b; return (x > y) - (x < y); }
CMP_NUM(cmp_i8, int8_t)  CMP_NUM(cmp_i16, int16_t) CMP_NUM(cmp_i32, int32_t) CMP_NUM(cmp_i64, int64_t)
CMP_NUM(cmp_u8, uint8_t) CMP_NUM(cmp_u16, uint16_t) CMP_NUM(cmp_u32, uint32_t) CMP_NUM(cmp_u64, uint64_t)
#define CMP_FLOAT(name, T) \
	static int name(const void *a, const void *b) \
	{ \
		T x = *(const T *)a, y = *(const T *)b; \
		if (isnan(x) || isnan(y)) \
			return isnan(x) - isnan(y); \
		return (x > y) - (x < y); \
	}
CMP_FLOAT(cmp_f32, float) CMP_FLOAT(cmp_f64, double)
static int cmp_str(const void *a, const void *b)
{
	const cby_str *x = a, *y = b;
	return rt_str_compare(DATA(x), str_len(x), DATA(y), str_len(y));
}

void cby_list_sort(cby_list *l, int kind)
{
	static int (*const cmp[])(const void *, const void *) = {
		[RT_SORT_I8] = cmp_i8, [RT_SORT_I16] = cmp_i16, [RT_SORT_I32] = cmp_i32, [RT_SORT_I64] = cmp_i64,
		[RT_SORT_U8] = cmp_u8, [RT_SORT_U16] = cmp_u16, [RT_SORT_U32] = cmp_u32, [RT_SORT_U64] = cmp_u64,
		[RT_SORT_F32] = cmp_f32, [RT_SORT_F64] = cmp_f64, [RT_SORT_STR] = cmp_str,
	};
	static const size_t size[] = {
		[RT_SORT_I8] = 1, [RT_SORT_I16] = 2, [RT_SORT_I32] = 4, [RT_SORT_I64] = 8,
		[RT_SORT_U8] = 1, [RT_SORT_U16] = 2, [RT_SORT_U32] = 4, [RT_SORT_U64] = 8,
		[RT_SORT_F32] = 4, [RT_SORT_F64] = 8, [RT_SORT_STR] = sizeof(cby_str),
	};
	if (l->len > 1)
		qsort(l->data, (size_t)l->len, size[kind], cmp[kind]);
}

/* L.sort_by and L.sort_with: a merge sort of the positions, so equals
 * keep their order; before(b, a) says whether the element at b goes
 * before the one at a. Then the elements are put in that order. */
typedef bool (*cby_before)(const void *b, const void *a);

static void sort_by_merge(const char *data, int64_t size, cby_before before, int64_t *idx, int64_t *tmp, int64_t n)
{
	if (n < 2)
		return;
	int64_t h = n / 2;
	sort_by_merge(data, size, before, idx, tmp, h);
	sort_by_merge(data, size, before, idx + h, tmp, n - h);
	int64_t i = 0, j = h, k = 0;
	while (i < h && j < n)
		tmp[k++] = before(data + idx[j] * size, data + idx[i] * size) ? idx[j++] : idx[i++];
	while (i < h)
		tmp[k++] = idx[i++];
	while (j < n)
		tmp[k++] = idx[j++];
	memcpy(idx, tmp, sizeof *idx * (size_t)n);
}

void cby_list_sort_by(cby_list *l, int64_t size, cby_before before)
{
	int64_t n = l->len;
	if (n < 2)
		return;
	int64_t *idx = malloc(sizeof *idx * (size_t)n * 2);
	char *was = malloc((size_t)(n * size));
	if (!idx || !was)
		rt_panic(NULL, 0, 0, "out of memory");
	for (int64_t i = 0; i < n; i++)
		idx[i] = i;
	sort_by_merge(l->data, size, before, idx, idx + n, n);
	char *data = l->data;
	memcpy(was, data, (size_t)(n * size));
	for (int64_t i = 0; i < n; i++)
		memcpy(data + i * size, was + idx[i] * size, (size_t)size);
	free(was);
	free(idx);
}

/* L.reverse */
void cby_list_reverse(cby_list *l, int64_t size)
{
	if (l->len < 2)
		return;
	char tmp[64];
	char *a = l->data, *b = l->data + (l->len - 1) * size;
	for (; a < b; a += size, b -= size)
		for (int64_t done = 0; done < size; done += (int64_t)sizeof tmp) {
			size_t n = (size_t)(size - done < (int64_t)sizeof tmp ? size - done : (int64_t)sizeof tmp);
			memcpy(tmp, a + done, n);
			memcpy(a + done, b + done, n);
			memcpy(b + done, tmp, n);
		}
}

/* Words.join Sep */
void cby_list_join(cby_str *dst, const cby_list *l, const cby_str *sep)
{
	const cby_str *items = (const cby_str *)l->data;
	int64_t ns = str_len(sep), total = 0;
	for (int64_t i = 0; i < l->len; i++)
		total += str_len(&items[i]) + (i ? ns : 0);
	char *p = rt_alloc((size_t)total + 1), *o = p;
	for (int64_t i = 0; i < l->len; i++) {
		if (i && ns) {
			memcpy(o, sep->data, (size_t)ns);
			o += ns;
		}
		int64_t k = str_len(&items[i]);
		if (k)
			memcpy(o, items[i].data, (size_t)k);
		o += k;
	}
	*o = '\0';
	dst->data = p;
	dst->len = total;
}

/* Text.split Sep: the pieces between each Sep (an empty Sep: the whole text). */
void cby_str_split(cby_list *dst, const cby_str *s, const cby_str *sep)
{
	dst->data = NULL;
	dst->len = dst->cap = 0;
	const char *a = DATA(s), *b = DATA(sep);
	int64_t na = str_len(s), nb = str_len(sep), at = 0;
	for (;;) {
		int64_t k = nb ? rt_str_find(a + at, na - at, b, nb) : -1;
		cby_str *piece = cby_list_push(dst, (int64_t)sizeof(cby_str));
		if (k < 0) {
			cby_str_dup(piece, a + at, na - at);
			break;
		}
		cby_str_dup(piece, a + at, k);
		at += k + nb;
	}
}

/* ------------------------------------------------- files and directories */

/* A path as a C string (paths are rarely long; the copy is freed by the caller) */
static char *path_z(const char *p, int64_t n)
{
	char *z = rt_alloc((size_t)n + 1);
	if (n)
		memcpy(z, p, (size_t)n);
	z[n] = '\0';
	return z;
}

/* the same, kept for its function's own use while it works: freed by it
 * (tmp_free), or by a panic that's caught (an `else`), which leaves it
 * no other way to be */
static _Thread_local char *tmp_live[16];

static char *tmp_z(const char *p, int64_t n)
{
	char *z = path_z(p, n);
	for (int i = 0; i < 16; i++)
		if (!tmp_live[i]) {
			tmp_live[i] = z;
			break;
		}
	return z;
}

static void tmp_free(char *z)
{
	for (int i = 0; i < 16; i++)
		if (tmp_live[i] == z)
			tmp_live[i] = NULL;
	rt_free(z);
}

/* kept, past its function: no longer the panic's to free */
static void tmp_keep(char *z)
{
	for (int i = 0; i < 16; i++)
		if (tmp_live[i] == z)
			tmp_live[i] = NULL;
}

static void tmp_free_all(void)
{
	for (int i = 0; i < 16; i++) {
		rt_free(tmp_live[i]);
		tmp_live[i] = NULL;
	}
}

/* The system's reason, as Cabby's messages write it: "no such file or directory" */
static const char *reason(int err)
{
	static char buf[128];
	snprintf(buf, sizeof buf, "%s", strerror(err));
	if (buf[0] >= 'A' && buf[0] <= 'Z' && !(buf[1] >= 'A' && buf[1] <= 'Z'))
		buf[0] = (char)(buf[0] - 'A' + 'a');
	return buf;
}

static _Noreturn void io_fail(const char *what, const char *path, int err, const char *file, int line, int col)
{
	rt_panic(file, line, col, "can't %s '%s': %s", what, path, reason(err));
}

static bool is_dir_z(const char *z)
{
	struct stat st;
	return stat(z, &st) == 0 && S_ISDIR(st.st_mode);
}

/* What's been printed into open files is written out before any file is
 * read, so a program always sees what it wrote. */
static void flush_open_files(void);

char *rt_files_read(const char *p, int64_t n, int64_t *len, const char *file, int line, int col)
{
	flush_open_files();
	char *z = tmp_z(p, n);
	if (is_dir_z(z))
		rt_panic(file, line, col, "can't read '%s': it is a directory (directories.list reads one)", z);
	FILE *f = fopen(z, "rb");
	if (!f)
		io_fail("read", z, errno, file, line, col);
	size_t cap = 4096, used = 0;
	char *buf = rt_alloc(cap);
	for (;;) {
		if (used + 1 >= cap) {
			cap *= 2;
			buf = rt_realloc(buf, cap);
		}
		size_t got = fread(buf + used, 1, cap - used - 1, f);
		used += got;
		if (got == 0)
			break;
	}
	bool bad = ferror(f);
	int err = errno;
	fclose(f);
	if (bad)
		io_fail("read", z, err, file, line, col);
	tmp_free(z);
	buf[used] = '\0';
	*len = (int64_t)used;
	return buf;
}

rt_text *rt_files_lines(const char *p, int64_t n, int64_t *count, const char *file, int line, int col)
{
	int64_t len;
	char *all = rt_files_read(p, n, &len, file, line, col);
	int64_t lines = 0, cap = 16;
	rt_text *out = rt_alloc(sizeof *out * (size_t)cap);
	for (int64_t at = 0; at < len;) {
		const char *nl = memchr(all + at, '\n', (size_t)(len - at));
		int64_t end = nl ? (int64_t)(nl - all) : len, k = end - at;
		if (k > 0 && all[at + k - 1] == '\r')   /* a Windows line end */
			k--;
		if (lines == cap) {
			cap *= 2;
			out = rt_realloc(out, sizeof *out * (size_t)cap);
		}
		out[lines].data = rt_alloc((size_t)k + 1);
		memcpy(out[lines].data, all + at, (size_t)k);
		out[lines].data[k] = '\0';
		out[lines++].len = k;
		at = end + 1;   /* a last line without a line end still counts */
	}
	free(all);
	*count = lines;
	return out;
}

void rt_files_write(const char *p, int64_t n, const char *t, int64_t tn, bool append, const char *file, int line,
		    int col)
{
	char *z = tmp_z(p, n);
	if (is_dir_z(z))
		rt_panic(file, line, col, "can't write '%s': it is a directory", z);
	FILE *f = fopen(z, append ? "ab" : "wb");
	if (!f)
		io_fail("write", z, errno, file, line, col);
	bool bad = tn > 0 && fwrite(t, 1, (size_t)tn, f) != (size_t)tn;
	int err = errno;
	if (fclose(f) != 0 && !bad) {
		bad = true;
		err = errno;
	}
	if (bad)
		io_fail("write", z, err, file, line, col);
	tmp_free(z);
}

bool rt_files_exists(const char *p, int64_t n)
{
	char *z = tmp_z(p, n);
	struct stat st;
	bool yes = stat(z, &st) == 0 && !S_ISDIR(st.st_mode);
	tmp_free(z);
	return yes;
}

int64_t rt_files_size(const char *p, int64_t n, const char *file, int line, int col)
{
	flush_open_files();
	char *z = tmp_z(p, n);
	struct stat st;
	if (stat(z, &st) != 0)
		io_fail("find the size of", z, errno, file, line, col);
	if (S_ISDIR(st.st_mode))
		rt_panic(file, line, col, "can't find the size of '%s': it is a directory", z);
	tmp_free(z);
	return (int64_t)st.st_size;
}

static void copy_z(const char *from, const char *to, const char *file, int line, int col)
{
	flush_open_files();
	if (is_dir_z(from))
		rt_panic(file, line, col, "can't copy '%s': it is a directory", from);
	FILE *in = fopen(from, "rb");
	if (!in)
		io_fail("copy", from, errno, file, line, col);
	FILE *out = fopen(to, "wb");
	if (!out) {
		int err = errno;
		fclose(in);
		io_fail("copy to", to, err, file, line, col);
	}
	char buf[65536];
	size_t got;
	bool bad = false;
	while (!bad && (got = fread(buf, 1, sizeof buf, in)) > 0)
		bad = fwrite(buf, 1, got, out) != got;
	int err = errno;
	bad = bad || ferror(in);
	fclose(in);
	if (fclose(out) != 0)
		bad = true;
	if (bad)
		io_fail("copy to", to, err ? err : EIO, file, line, col);
}

void rt_files_copy(const char *a, int64_t an, const char *b, int64_t bn, const char *file, int line, int col)
{
	char *from = tmp_z(a, an), *to = tmp_z(b, bn);
	copy_z(from, to, file, line, col);
	tmp_free(from);
	tmp_free(to);
}

void rt_files_rename(const char *a, int64_t an, const char *b, int64_t bn, const char *file, int line, int col)
{
	char *from = tmp_z(a, an), *to = tmp_z(b, bn);
	if (is_dir_z(from))
		rt_panic(file, line, col, "can't rename '%s' with files.rename: it is a directory (use directories.rename)",
			 from);
	if (rename(from, to) != 0) {
		if (errno != EXDEV)
			io_fail("rename", from, errno, file, line, col);
		copy_z(from, to, file, line, col);   /* to another disk: copy, then delete */
		if (unlink(from) != 0)
			io_fail("delete", from, errno, file, line, col);
	}
	tmp_free(from);
	tmp_free(to);
}

void rt_files_delete(const char *p, int64_t n, const char *file, int line, int col)
{
	char *z = tmp_z(p, n);
	if (is_dir_z(z))
		rt_panic(file, line, col, "can't delete '%s': it is a directory (use directories.remove)", z);
	if (unlink(z) != 0)
		io_fail("delete", z, errno, file, line, col);
	tmp_free(z);
}

typedef struct { rt_text *items; int64_t len, cap; } TextVec;

static void texts_add(TextVec *v, const char *s, size_t n)
{
	if (v->len == v->cap) {
		v->cap = v->cap ? v->cap * 2 : 16;
		v->items = rt_realloc(v->items, sizeof *v->items * (size_t)v->cap);
	}
	char *d = rt_alloc(n + 1);
	memcpy(d, s, n);
	d[n] = '\0';
	v->items[v->len].data = d;
	v->items[v->len++].len = (int64_t)n;
}

static int text_order(const void *a, const void *b)
{
	const rt_text *x = a, *y = b;
	return rt_str_compare(x->data, x->len, y->data, y->len);
}

/* every file below dir, as paths from the top (prefix is the path so far) */
static bool list_files_below(const char *dir, const char *prefix, TextVec *v, int *err)
{
	DIR *d = opendir(dir);
	if (!d) {
		*err = errno;
		return false;
	}
	struct dirent *e;
	bool ok = true;
	while (ok && (e = readdir(d))) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		size_t dl = strlen(dir), nl = strlen(e->d_name), pl = strlen(prefix);
		char *full = rt_alloc(dl + nl + 2), *rel = rt_alloc(pl + nl + 2);
		snprintf(full, dl + nl + 2, "%s/%s", dir, e->d_name);
		snprintf(rel, pl + nl + 2, "%s%s%s", prefix, pl ? "/" : "", e->d_name);
		struct stat st;
		if (lstat(full, &st) == 0) {
			if (S_ISDIR(st.st_mode))
				ok = list_files_below(full, rel, v, err);   /* (a link to a directory isn't followed) */
			else if (S_ISREG(st.st_mode) || (S_ISLNK(st.st_mode) && stat(full, &st) == 0 && S_ISREG(st.st_mode)))
				texts_add(v, rel, strlen(rel));
		}
		free(full);
		free(rel);
	}
	closedir(d);
	return ok;
}

rt_text *rt_dirs_list(const char *p, int64_t n, bool all_files, int64_t *count, const char *file, int line, int col)
{
	char *z = tmp_z(p, n);
	TextVec v = { 0 };
	int err = 0;
	if (!is_dir_z(z) && access(z, F_OK) == 0)
		rt_panic(file, line, col, "can't list '%s': it is a file, not a directory", z);
	if (all_files) {
		if (!list_files_below(z, "", &v, &err)) {
			rt_texts_free(v.items, v.len);
			io_fail("list", z, err, file, line, col);
		}
	} else {
		DIR *d = opendir(z);
		if (!d)
			io_fail("list", z, errno, file, line, col);
		struct dirent *e;
		while ((e = readdir(d)))
			if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
				texts_add(&v, e->d_name, strlen(e->d_name));
		closedir(d);
	}
	tmp_free(z);
	if (v.len > 1)
		qsort(v.items, (size_t)v.len, sizeof *v.items, text_order);
	*count = v.len;
	return v.items;
}

bool rt_dirs_exists(const char *p, int64_t n)
{
	char *z = tmp_z(p, n);
	bool yes = is_dir_z(z);
	tmp_free(z);
	return yes;
}

void rt_dirs_make(const char *p, int64_t n, const char *file, int line, int col)
{
	char *z = tmp_z(p, n);
	if (n == 0)
		rt_panic(file, line, col, "can't make a directory without a name");
	/* each missing directory on the way, as mkdir -p does */
	for (char *s = z + 1;; s++) {
		bool last = *s == '\0';
		if (*s == '/' || last) {
			char c = *s;
			*s = '\0';
			if (mkdir(z, 0777) != 0) {
				if (errno != EEXIST)
					io_fail("make the directory", z, errno, file, line, col);
				if (!is_dir_z(z))
					rt_panic(file, line, col, "can't make the directory '%s': a file has that name", z);
			}
			*s = c;
			if (last)
				break;
		}
	}
	tmp_free(z);
}

void rt_dirs_remove(const char *p, int64_t n, const char *file, int line, int col)
{
	char *z = tmp_z(p, n);
	if (!is_dir_z(z) && access(z, F_OK) == 0)
		rt_panic(file, line, col, "can't remove '%s': it is a file (use files.delete)", z);
	if (rmdir(z) != 0) {
		if (errno == ENOTEMPTY || errno == EEXIST)
			rt_panic(file, line, col, "can't remove '%s': it isn't empty (delete what's in it first)", z);
		io_fail("remove", z, errno, file, line, col);
	}
	tmp_free(z);
}

void rt_dirs_rename(const char *a, int64_t an, const char *b, int64_t bn, const char *file, int line, int col)
{
	char *from = tmp_z(a, an), *to = tmp_z(b, bn);
	if (!is_dir_z(from) && access(from, F_OK) == 0)
		rt_panic(file, line, col, "can't rename '%s' with directories.rename: it is a file (use files.rename)", from);
	if (rename(from, to) != 0)
		io_fail("rename", from, errno, file, line, col);
	tmp_free(from);
	tmp_free(to);
}

char *rt_dirs_current(int64_t *len)
{
	size_t cap = 256;
	char *buf = rt_alloc(cap);
	while (!getcwd(buf, cap)) {
		if (errno != ERANGE) {
			buf[0] = '\0';
			break;
		}
		cap *= 2;
		buf = rt_realloc(buf, cap);
	}
	*len = (int64_t)strlen(buf);
	return buf;
}

void rt_dirs_change_to(const char *p, int64_t n, const char *file, int line, int col)
{
	char *z = tmp_z(p, n);
	if (chdir(z) != 0)
		io_fail("change to", z, errno, file, line, col);
	tmp_free(z);
}

char *rt_dirs_home(int64_t *len)
{
	const char *h = getenv("HOME");
	if (!h || !*h) {
		struct passwd *pw = getpwuid(getuid());
		h = pw && pw->pw_dir ? pw->pw_dir : "/";
	}
	*len = (int64_t)strlen(h);
	return path_z(h, *len);
}

/* Where a program keeps what it saves (a game's saved games, its options),
 * by its name: the place each system has for that, made if it isn't
 * there. ~/.local/share/<Name> on Linux (or under $XDG_DATA_HOME), and
 * ~/Library/Application Support/<Name> on a Mac. (CABBY_SAVE_FOLDERS, when
 * it's set, is where they all go instead: the tests set it.) */
char *rt_dirs_save_folder(const char *p, int64_t n, int64_t *len, const char *file, int line, int col)
{
	char *name = tmp_z(p, n);
	if (n == 0 || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, ".."))
		rt_panic(file, line, col, "'%s' can't be a save folder's name: give the program's name, as \"My Game\"", name);
	int64_t hn;
	char *home = rt_dirs_home(&hn);
	const char *xdg = getenv("XDG_DATA_HOME"), *tests = getenv("CABBY_SAVE_FOLDERS");
	size_t size = strlen(home) + (xdg ? strlen(xdg) : 0) + (tests ? strlen(tests) : 0) + strlen(name) + 64;
	char *path = malloc(size);
	if (tests && tests[0]) {
		snprintf(path, size, "%s/%s", tests, name);   /* (tests: never someone's own saves) */
	} else {
#if defined(__APPLE__)
		snprintf(path, size, "%s/Library/Application Support/%s", home, name);
#else
		if (xdg && xdg[0] == '/')
			snprintf(path, size, "%s/%s", xdg, name);
		else
			snprintf(path, size, "%s/.local/share/%s", home, name);
#endif
	}
	free(home);
	tmp_free(name);
	rt_dirs_make(path, (int64_t)strlen(path), file, line, col);
	*len = (int64_t)strlen(path);
	return path;
}

void rt_texts_free(rt_text *t, int64_t count)
{
	for (int64_t i = 0; i < count; i++)
		free(t[i].data);
	free(t);
}

/* ------------------------------------------------------------- the shell */

#include <sys/wait.h>

/* how a command ended, for a panic: "exit status 1" */
static void describe_end(int st, char *buf, size_t size)
{
	if (WIFEXITED(st))
		snprintf(buf, size, "exit status %d%s", WEXITSTATUS(st),
			 WEXITSTATUS(st) == 127 ? " (command not found)" : WEXITSTATUS(st) == 126 ? " (not allowed to run it)" : "");
	else if (WIFSIGNALED(st))
		snprintf(buf, size, "stopped by signal %d (%s)", WTERMSIG(st), strsignal(WTERMSIG(st)));
	else
		snprintf(buf, size, "status %d", st);
}

/* the exit status as a shell gives it: 128 + the signal if it was stopped */
static int32_t exit_status(int st)
{
	if (WIFEXITED(st))
		return WEXITSTATUS(st);
	if (WIFSIGNALED(st))
		return 128 + WTERMSIG(st);
	return st;
}

/* Everything printed so far is written out first, so the command's output
 * comes after it, and files the program wrote are complete. */
static void before_command(void)
{
	flush_open_files();
	fflush(stdout);
	fflush(stderr);
}

char *rt_shell_run(const char *c, int64_t n, int64_t *len, const char *file, int line, int col)
{
	char *z = tmp_z(c, n);
	before_command();
	FILE *p = popen(z, "r");
	if (!p)
		rt_panic(file, line, col, "can't run '%s': %s", z, reason(errno));
	size_t cap = 256, used = 0;
	char *out = rt_alloc(cap);
	size_t got;
	while ((got = fread(out + used, 1, cap - used - 1, p)) > 0) {
		used += got;
		if (cap - used < 2) {
			cap *= 2;
			out = rt_realloc(out, cap);
		}
	}
	int st = pclose(p);
	if (st == -1)
		rt_panic(file, line, col, "can't run '%s': %s", z, reason(errno));
	if (st != 0) {
		char how[96];
		describe_end(st, how, sizeof how);
		free(out);
		rt_panic(file, line, col, "the command '%s' failed: %s (shell.status gives the status instead)", z, how);
	}
	while (used > 0 && out[used - 1] == '\n')   /* as $(...) does in a shell */
		used--;
	out[used] = '\0';
	tmp_free(z);
	*len = (int64_t)used;
	return out;
}

int32_t rt_shell_status(const char *c, int64_t n, const char *file, int line, int col)
{
	char *z = tmp_z(c, n);
	before_command();
	int st = system(z);
	if (st == -1)
		rt_panic(file, line, col, "can't run '%s': %s", z, reason(errno));
	tmp_free(z);
	return exit_status(st);
}

static char *env_name(const char *name, int64_t n, const char *what, const char *file, int line, int col)
{
	char *z = tmp_z(name, n);
	if (n == 0)
		rt_panic(file, line, col, "can't %s an environment variable with no name", what);
	if (strchr(z, '=') || (int64_t)strlen(z) != n)
		rt_panic(file, line, col, "can't %s the environment variable '%s': a name can't have '=' in it", what, z);
	return z;
}

char *rt_shell_get(const char *name, int64_t n, int64_t *len, const char *file, int line, int col)
{
	char *z = env_name(name, n, "get", file, line, col);
	const char *v = getenv(z);
	if (!v)
		rt_panic(file, line, col, "the environment variable '%s' isn't set (check first with shell.has \"%s\")", z, z);
	tmp_free(z);
	*len = (int64_t)strlen(v);
	return path_z(v, *len);
}

bool rt_shell_has(const char *name, int64_t n)
{
	char *z = tmp_z(name, n);
	bool yes = n > 0 && (int64_t)strlen(z) == n && getenv(z) != NULL;
	tmp_free(z);
	return yes;
}

void rt_shell_set(const char *name, int64_t n, const char *v, int64_t vn, const char *file, int line, int col)
{
	char *z = env_name(name, n, "set", file, line, col);
	char *zv = tmp_z(v, vn);
	if (setenv(z, zv, 1) != 0)
		rt_panic(file, line, col, "can't set the environment variable '%s': %s", z, reason(errno));
	tmp_free(z);
	tmp_free(zv);
}

void rt_shell_unset(const char *name, int64_t n, const char *file, int line, int col)
{
	char *z = env_name(name, n, "unset", file, line, col);
	unsetenv(z);
	tmp_free(z);
}

/* ---- a program run with its arguments as they are (no shell, so nothing
 * needs quoting), or a shell command given input: for shell.hby's
 * run_with, run_program and program_status. The arguments come as one
 * text with \x1f between them. What it printed is kept for
 * cby_shell_take; its exit status for cby_shell_last_status. */

static _Thread_local char *shell_out;
static _Thread_local size_t shell_out_len;
static _Thread_local int32_t shell_last;

int64_t cby_shell_program(const char *joined, int through_shell, const char *input, int has_input, int capture, int must_work)
{
	char *copy = strdup(joined);
	size_t cap = 8, n = 0;
	char **argv = malloc(sizeof *argv * cap);
	if (through_shell) {
		argv[n++] = (char *)"/bin/sh";
		argv[n++] = (char *)"-c";
	}
	for (char *p = copy;;) {
		if (n + 2 > cap)
			argv = realloc(argv, sizeof *argv * (cap *= 2));
		argv[n++] = p;
		char *sep = strchr(p, '\x1f');
		if (!sep)
			break;
		*sep = 0;
		p = sep + 1;
	}
	argv[n] = NULL;
	const char *name = argv[through_shell ? 2 : 0];
	if (!*name) {
		free(argv);
		free(copy);
		rt_panic(NULL, 0, 0, "%s", through_shell ? "the command is empty" : "the program to run is \"\": the list's first is the program");
	}
	before_command();
	/* the input from a file of its own, as stdin: no pipe to fill and
	 * deadlock on */
	int in = -1;
	if (has_input) {
		const char *dir = getenv("TMPDIR");
		char tmpl[4096];
		snprintf(tmpl, sizeof tmpl, "%s/cabby-input-XXXXXX", dir && *dir ? dir : "/tmp");
		in = mkstemp(tmpl);
		if (in < 0)
			rt_panic(NULL, 0, 0, "the input for '%s' couldn't be kept: %s", name, reason(errno));
		unlink(tmpl);
		size_t len = strlen(input), done = 0;
		while (done < len) {
			ssize_t w = write(in, input + done, len - done);
			if (w <= 0)
				break;
			done += (size_t)w;
		}
		lseek(in, 0, SEEK_SET);
	}
	int out[2] = { -1, -1 }, err[2];
	if ((capture && pipe(out) != 0) || pipe(err) != 0)
		rt_panic(NULL, 0, 0, "'%s' couldn't be run: %s", name, reason(errno));
	fcntl(err[1], F_SETFD, FD_CLOEXEC);
	pid_t pid = fork();
	if (pid == 0) {
		if (in >= 0)
			dup2(in, 0);
		if (capture)
			dup2(out[1], 1);
		execvp(argv[0], argv);
		int e = errno;
		if (write(err[1], &e, sizeof e) < 0)
			_exit(127);
		_exit(127);
	}
	close(err[1]);
	if (capture)
		close(out[1]);
	if (in >= 0)
		close(in);
	size_t used = 0, room = 256;
	char *text = malloc(room);
	if (capture && pid > 0) {
		ssize_t got;
		while ((got = read(out[0], text + used, room - used - 1)) != 0) {
			if (got < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			used += (size_t)got;
			if (room - used < 2)
				text = realloc(text, room *= 2);
		}
	}
	if (capture)
		close(out[0]);
	int e = 0;
	bool not_run = pid < 0 || read(err[0], &e, sizeof e) == (ssize_t)sizeof e;
	close(err[0]);
	if (pid < 0)
		e = errno;
	int st = 0;
	if (pid > 0)
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
		}
	if (not_run) {
		free(text);
		if (must_work || e != ENOENT) {
			char *why = strdup(e == ENOENT ? "there's no such program" : reason(e));
			char *named = strdup(name);
			free(argv);
			free(copy);
			rt_panic(NULL, 0, 0, "'%s' couldn't be run: %s", named, why);
		}
		free(argv);
		free(copy);
		shell_last = 127;
		return 0;
	}
	shell_last = exit_status(st);
	if (st != 0 && must_work) {
		char how[96];
		describe_end(st, how, sizeof how);
		char *named = strdup(name);
		free(text);
		free(argv);
		free(copy);
		rt_panic(NULL, 0, 0, "'%s' failed: %s (%s gives the status instead)", named, how,
			 through_shell ? "shell.status" : "shell.program_status");
	}
	free(argv);
	free(copy);
	while (used > 0 && text[used - 1] == '\n')   /* as $(...) does in a shell */
		used--;
	text[used] = 0;
	free(shell_out);
	shell_out = text;
	shell_out_len = used;
	return (int64_t)used;
}

void cby_shell_take(char *dst)
{
	if (shell_out && shell_out_len)
		memcpy(dst, shell_out, shell_out_len);
	free(shell_out);
	shell_out = NULL;
	shell_out_len = 0;
}

int32_t cby_shell_last_status(void) { return shell_last; }

/* compiled programs' side: paths are cby_str; results become cby_str and
 * list str (cby_list of cby_str) */
static void texts_to_list(cby_list *dst, rt_text *t, int64_t count)
{
	dst->data = NULL;
	dst->len = dst->cap = 0;
	if (count) {
		cby_list_reserve(dst, (int64_t)sizeof(cby_str), count);
		cby_str *items = (cby_str *)dst->data;
		for (int64_t i = 0; i < count; i++) {
			items[i].data = t[i].data;   /* the text moves into the list */
			items[i].len = t[i].len;
		}
		dst->len = count;
	}
	free(t);
}

void cby_files_read(cby_str *dst, const cby_str *p, const char *file, int line, int col)
{
	dst->data = rt_files_read(DATA(p), str_len(p), &dst->len, file, line, col);
}
void cby_files_lines(cby_list *dst, const cby_str *p, const char *file, int line, int col)
{
	int64_t n;
	rt_text *t = rt_files_lines(DATA(p), str_len(p), &n, file, line, col);
	texts_to_list(dst, t, n);
}
void cby_files_write(const cby_str *p, const cby_str *t, bool append, const char *file, int line, int col)
{
	rt_files_write(DATA(p), str_len(p), DATA(t), str_len(t), append, file, line, col);
}
bool cby_files_exists(const cby_str *p) { return rt_files_exists(DATA(p), str_len(p)); }
int64_t cby_files_size(const cby_str *p, const char *file, int line, int col)
{
	return rt_files_size(DATA(p), str_len(p), file, line, col);
}
void cby_files_copy(const cby_str *a, const cby_str *b, const char *file, int line, int col)
{
	rt_files_copy(DATA(a), str_len(a), DATA(b), str_len(b), file, line, col);
}
void cby_files_rename(const cby_str *a, const cby_str *b, const char *file, int line, int col)
{
	rt_files_rename(DATA(a), str_len(a), DATA(b), str_len(b), file, line, col);
}
void cby_files_delete(const cby_str *p, const char *file, int line, int col)
{
	rt_files_delete(DATA(p), str_len(p), file, line, col);
}
void cby_dirs_list(cby_list *dst, const cby_str *p, bool all_files, const char *file, int line, int col)
{
	int64_t n;
	rt_text *t = rt_dirs_list(DATA(p), str_len(p), all_files, &n, file, line, col);
	texts_to_list(dst, t, n);
}
bool cby_dirs_exists(const cby_str *p) { return rt_dirs_exists(DATA(p), str_len(p)); }
void cby_dirs_make(const cby_str *p, const char *file, int line, int col)
{
	rt_dirs_make(DATA(p), str_len(p), file, line, col);
}
void cby_dirs_remove(const cby_str *p, const char *file, int line, int col)
{
	rt_dirs_remove(DATA(p), str_len(p), file, line, col);
}
void cby_dirs_rename(const cby_str *a, const cby_str *b, const char *file, int line, int col)
{
	rt_dirs_rename(DATA(a), str_len(a), DATA(b), str_len(b), file, line, col);
}
void cby_dirs_change_to(const cby_str *p, const char *file, int line, int col)
{
	rt_dirs_change_to(DATA(p), str_len(p), file, line, col);
}
void cby_dirs_current(cby_str *dst) { dst->data = rt_dirs_current(&dst->len); }
void cby_dirs_home(cby_str *dst) { dst->data = rt_dirs_home(&dst->len); }
void cby_dirs_save_folder(cby_str *dst, const cby_str *p, const char *file, int line, int col)
{
	dst->data = rt_dirs_save_folder(DATA(p), str_len(p), &dst->len, file, line, col);
}
void cby_shell_run(cby_str *dst, const cby_str *c, const char *file, int line, int col)
{
	dst->data = rt_shell_run(DATA(c), str_len(c), &dst->len, file, line, col);
}
void cby_shell_get(cby_str *dst, const cby_str *name, const char *file, int line, int col)
{
	dst->data = rt_shell_get(DATA(name), str_len(name), &dst->len, file, line, col);
}
int32_t cby_shell_status(const cby_str *c, const char *file, int line, int col)
{
	return rt_shell_status(DATA(c), str_len(c), file, line, col);
}
bool cby_shell_has(const cby_str *name) { return rt_shell_has(DATA(name), str_len(name)); }
void cby_shell_set(const cby_str *name, const cby_str *v, const char *file, int line, int col)
{
	rt_shell_set(DATA(name), str_len(name), DATA(v), str_len(v), file, line, col);
}
void cby_shell_unset(const cby_str *name, const char *file, int line, int col)
{
	rt_shell_unset(DATA(name), str_len(name), file, line, col);
}

/* ---- open files: `file` (use files) ---- */

struct rt_file {
	FILE *f;
	char *path;
	bool writing;
	bool reading;   /* (both, for files.open_to_change) */
	int last;       /* what was done last: 1 read, 2 written (C needs a seek between them) */
	bool ended;   /* read_line found nothing left */
	rt_file *next_open;   /* the files open for writing, so reads can see what they hold */
};

static rt_file *open_for_writing;

rt_file *rt_file_open(const char *p, int64_t n, int mode, const char *file, int line, int col)
{
	flush_open_files();
	char *z = tmp_z(p, n);
	if (is_dir_z(z))
		rt_panic(file, line, col, "can't open '%s': it is a directory", z);
	FILE *f = fopen(z, mode == RT_OPEN_READ ? "rb" : mode == RT_OPEN_WRITE ? "wb" : mode == RT_OPEN_ADD ? "ab" : "r+b");
	if (!f)
		io_fail(mode == RT_OPEN_READ ? "open" : mode == RT_OPEN_CHANGE ? "change" : "open for writing", z, errno,
			file, line, col);
	tmp_keep(z);
	rt_file *r = rt_alloc(sizeof *r);
	r->f = f;
	r->path = z;
	r->writing = mode != RT_OPEN_READ;
	r->reading = mode == RT_OPEN_READ || mode == RT_OPEN_CHANGE;
	r->last = 0;
	r->ended = false;
	r->next_open = NULL;
	if (r->writing) {
		r->next_open = open_for_writing;
		open_for_writing = r;
	}
	return r;
}

void rt_file_close(rt_file *r)
{
	if (!r)
		return;
	for (rt_file **p = &open_for_writing; *p; p = &(*p)->next_open)
		if (*p == r) {
			*p = r->next_open;
			break;
		}
	fclose(r->f);
	free(r->path);
	free(r);
}

static rt_file *open_file(rt_file *r, bool writing, const char *file, int line, int col)
{
	if (!r)
		rt_panic(file, line, col, "the file isn't open (open it with files.open, files.open_to_write, "
			 "files.open_to_add or files.open_to_change)");
	if (writing && !r->writing)
		rt_panic(file, line, col, "can't write to '%s': it was opened for reading (files.open; "
			 "files.open_to_change opens a file to read and write)", r->path);
	if (!writing && !r->reading)
		rt_panic(file, line, col, "can't read from '%s': it was opened for writing (files.open_to_change opens "
			 "a file to read and write)", r->path);
	/* C's rule for a file opened both ways: a seek between reading and writing */
	int now = writing ? 2 : 1;
	if (r->last && r->last != now)
		fseek(r->f, 0, SEEK_CUR);
	r->last = now;
	return r;
}

char *rt_file_read_line(rt_file *r, int64_t *len, const char *file, int line, int col)
{
	open_file(r, false, file, line, col);
	if (r->ended)
		rt_panic(file, line, col, "read_line after the end of '%s' (stop when at_end is true)", r->path);
	size_t cap = 128, n = 0;
	char *buf = rt_alloc(cap);
	int c;
	bool any = false;
	while ((c = getc(r->f)) != EOF) {
		any = true;
		if (c == '\n')
			break;
		if (n + 2 > cap) {
			cap *= 2;
			buf = rt_realloc(buf, cap);
		}
		buf[n++] = (char)c;
	}
	if (!any)
		r->ended = true;
	if (n > 0 && buf[n - 1] == '\r')   /* a Windows line end */
		n--;
	buf[n] = '\0';
	*len = (int64_t)n;
	return buf;
}

bool rt_file_at_end(rt_file *r, const char *file, int line, int col)
{
	open_file(r, false, file, line, col);
	if (r->ended)
		return true;
	int c = getc(r->f);
	if (c == EOF)
		return true;
	ungetc(c, r->f);
	return false;
}

/* Before `F some_call`: send the call's output into the file */
void rt_out_push_file(rt_file *r, const char *file, int line, int col)
{
	open_file(r, true, file, line, col);
	fflush(out_stream());
	if (out_depth < 256) {
		out_ids[out_depth] = RT_TO_FILE;
		out_files[out_depth] = r->f;
		out_line_open[out_depth] = rt_line_open;
	}
	out_depth++;
}

/* compiled programs: a file is { rt_file * } */
void cby_files_open(rt_file **dst, const cby_str *p, int mode, const char *file, int line, int col)
{
	*dst = rt_file_open(DATA(p), str_len(p), mode, file, line, col);
}
void cby_file_drop(rt_file **slot)
{
	rt_file_close(*slot);
	*slot = NULL;
}
void cby_file_read_line(cby_str *dst, rt_file *const *slot, const char *file, int line, int col)
{
	dst->data = rt_file_read_line(*slot, &dst->len, file, line, col);
}
bool cby_file_at_end(rt_file *const *slot, const char *file, int line, int col)
{
	return rt_file_at_end(*slot, file, line, col);
}
void cby_out_push_file(rt_file *const *slot, const char *file, int line, int col)
{
	rt_out_push_file(*slot, file, line, col);
}

/* ---- bytes in files (lib/files.hby): the file comes as F.handle, a number */

int64_t cby_files_read_bytes(const char *path)
{
	int64_t len;
	char *all = rt_files_read(path, (int64_t)strlen(path), &len, NULL, 0, 0);
	int64_t b = rt_bytes_make(all, len);
	free(all);
	return b;
}

void cby_files_write_bytes(const char *path, int64_t bytes, int append)
{
	int64_t n;
	const uint8_t *data = rt_bytes_data(bytes, &n);
	rt_files_write(path, (int64_t)strlen(path), (const char *)data, n, append != 0, NULL, 0, 0);
}

static rt_file *handle_file(int64_t h)
{
	if (!h)
		rt_panic(NULL, 0, 0, "the file isn't open (open it with files.open, files.open_to_write, "
			 "files.open_to_add or files.open_to_change)");
	return (rt_file *)(intptr_t)h;
}

/* up to `count` bytes from where the file is; fewer at the end, none after it */
int64_t cby_file_read_bytes(int64_t h, int64_t count)
{
	rt_file *r = open_file(handle_file(h), false, NULL, 0, 0);
	if (count < 0)
		rt_panic(NULL, 0, 0, "can't read %lld bytes", (long long)count);
	char *buf = rt_alloc(count > 0 ? (size_t)count : 1);
	size_t got = fread(buf, 1, (size_t)count, r->f);
	if (ferror(r->f))
		rt_panic(NULL, 0, 0, "can't read '%s': %s", r->path, strerror(errno));
	int64_t b = rt_bytes_make(buf, (int64_t)got);
	free(buf);
	return b;
}

void cby_file_write_bytes(int64_t h, int64_t bytes)
{
	rt_file *r = open_file(handle_file(h), true, NULL, 0, 0);
	int64_t n;
	const uint8_t *data = rt_bytes_data(bytes, &n);
	if (n > 0 && fwrite(data, 1, (size_t)n, r->f) != (size_t)n)
		rt_panic(NULL, 0, 0, "can't write to '%s': %s", r->path, strerror(errno));
}

int64_t cby_file_size(int64_t h)
{
	rt_file *r = handle_file(h);
	fflush(r->f);
	struct stat st;
	if (fstat(fileno(r->f), &st) != 0)
		return 0;
	return (int64_t)st.st_size;
}

int64_t cby_file_position(int64_t h)
{
	rt_file *r = handle_file(h);
	long p = ftell(r->f);
	return p < 0 ? 0 : (int64_t)p;
}

void cby_file_move_to(int64_t h, int64_t pos)
{
	rt_file *r = handle_file(h);
	int64_t size = cby_file_size(h);
	if (pos < 0 || pos > size)
		rt_panic(NULL, 0, 0, "can't move to byte %lld of '%s': it has %lld bytes (0 is the start, %lld the end)",
			 (long long)pos, r->path, (long long)size, (long long)size);
	fseek(r->f, (long)pos, SEEK_SET);
	r->last = 0;
	r->ended = false;
}

void cby_file_move_to_end(int64_t h)
{
	rt_file *r = handle_file(h);
	fseek(r->f, 0, SEEK_END);
	r->last = 0;
	r->ended = false;
}

static void flush_open_files(void)
{
	for (rt_file *r = open_for_writing; r; r = r->next_open)
		fflush(r->f);
}

void rt_flush_files(void) { flush_open_files(); }

/* ------------------------------------------------------------- the event loop
 *
 * One loop for everything a program waits for: timers, and files a library
 * watches (a process's output, later network connections). `events.run`
 * runs it until nothing is left to wait for; a gui or tui window runs it
 * between its own events, so everything works together. Handlers are
 * called through rt_invoke_hook: directly when compiled, through the
 * interpreter when interpreted. */

#include <poll.h>
#include <time.h>

static void call_method_directly(void *obj, void *fn)
{
	void (*f)(void *);
	memcpy(&f, &fn, sizeof f);
	f(obj);
}
void (*rt_invoke_hook)(void *obj, void *fn) = call_method_directly;

/* Called every so often while the interactive session waits for typing:
 * the window libraries set it, so the window system still hears from the
 * program (on macOS, a program that doesn't is shown as busy, with the
 * spinning wheel). */
void (*rt_idle_hook)(void);

typedef struct EvTimer {
	bool used, running, repeat;
	int64_t ms, due;      /* due: on the monotonic clock, in ms */
	void *obj, *fn;       /* the timer object's method that calls its handler */
} EvTimer;

typedef struct EvWatch {
	bool used;
	int fd;
	void (*fn)(void *data);
	void *data;
	bool quiet;           /* (it doesn't keep events.run waiting: os's signals) */
} EvWatch;

/* (tables that never move, as a worker may add to them while the main
 * thread's loop goes through them; a slot is claimed with ev_slots held) */
#define EV_MOST 4096
static EvTimer ev_timers[EV_MOST];
static _Atomic int ev_ntimers;
static EvWatch ev_watches[EV_MOST];
static _Atomic int ev_nwatches;
static pthread_mutex_t ev_slots = PTHREAD_MUTEX_INITIALIZER;
static bool ev_stopping;

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t cby_timer_new(void)
{
	pthread_mutex_lock(&ev_slots);
	int i = 0;
	while (i < ev_ntimers && ev_timers[i].used)
		i++;
	if (i == EV_MOST) {
		pthread_mutex_unlock(&ev_slots);
		rt_panic(NULL, 0, 0, "too many timers at once (%d)", EV_MOST);
	}
	memset(&ev_timers[i], 0, sizeof ev_timers[i]);
	ev_timers[i].used = true;
	if (i == ev_ntimers)
		ev_ntimers++;
	pthread_mutex_unlock(&ev_slots);
	return i + 1;
}

static EvTimer *ev_timer(int64_t id)
{
	if (id <= 0 || id > ev_ntimers || !ev_timers[id - 1].used)
		rt_panic(NULL, 0, 0, "events: a timer that was never made was used");
	return &ev_timers[id - 1];
}

/* the timer object's own method to call when it fires */
void cby_timer_set(int64_t id, void *obj, void *fn)
{
	EvTimer *t = ev_timer(id);
	t->obj = obj;
	t->fn = fn;
}

void cby_timer_start(int64_t id, int64_t ms, int repeat)
{
	EvTimer *t = ev_timer(id);
	if (ms < 0)
		rt_panic(NULL, 0, 0, "a timer can't wait %lld ms: it must be 0 or more", (long long)ms);
	t->ms = ms;
	t->repeat = repeat != 0;
	t->running = true;
	t->due = now_ms() + ms;
}

void cby_timer_stop(int64_t id) { ev_timer(id)->running = false; }
int cby_timer_running(int64_t id) { return ev_timer(id)->running; }

void cby_timer_free(int64_t id)
{
	if (id > 0 && id <= ev_ntimers)
		memset(&ev_timers[id - 1], 0, sizeof ev_timers[id - 1]);
}

/* Watching a file (a pipe): fn(data) when there is something to read */
static int watch_with(int fd, void (*fn)(void *data), void *data, bool quiet)
{
	pthread_mutex_lock(&ev_slots);
	int i = 0;
	while (i < ev_nwatches && ev_watches[i].used)
		i++;
	if (i == EV_MOST) {
		pthread_mutex_unlock(&ev_slots);
		rt_panic(NULL, 0, 0, "too many files watched at once (%d)", EV_MOST);
	}
	ev_watches[i] = (EvWatch){ true, fd, fn, data, quiet };
	if (i == ev_nwatches)
		ev_nwatches++;
	pthread_mutex_unlock(&ev_slots);
	return i + 1;
}

int cby_events_watch(int fd, void (*fn)(void *data), void *data) { return watch_with(fd, fn, data, false); }

/* the same, without keeping events.run waiting for it: something that may
 * or may not ever come (a signal), while there's nothing else */
int cby_events_watch_quietly(int fd, void (*fn)(void *data), void *data) { return watch_with(fd, fn, data, true); }

void cby_events_unwatch(int id)
{
	if (id > 0 && id <= ev_nwatches)
		ev_watches[id - 1].used = false;
}

/* What a window adds to its own waiting: the files to watch (at most max)
 * and how long until the next timer (-1: none) */
int cby_events_fds(int *fds, int max)
{
	int n = 0;
	for (int i = 0; i < ev_nwatches && n < max; i++)
		if (ev_watches[i].used)
			fds[n++] = ev_watches[i].fd;
	return n;
}

int cby_events_next_ms(void)
{
	int64_t best = -1, now = now_ms();
	for (int i = 0; i < ev_ntimers; i++)
		if (ev_timers[i].used && ev_timers[i].running) {
			int64_t left = ev_timers[i].due - now;
			if (left < 0)
				left = 0;
			if (best < 0 || left < best)
				best = left;
		}
	return best > INT32_MAX ? INT32_MAX : (int)best;
}

/* Is anything left to wait for? */
int cby_events_waiting(void)
{
	for (int i = 0; i < ev_ntimers; i++)
		if (ev_timers[i].used && ev_timers[i].running)
			return 1;
	for (int i = 0; i < ev_nwatches; i++)
		if (ev_watches[i].used && !ev_watches[i].quiet)
			return 1;
	return 0;
}

/* Waits up to `wait_ms` (-1: until something happens) for a watched file
 * or a timer, and calls what's due. */
void cby_events_step(int wait_ms)
{
	int next = cby_events_next_ms();
	if (next >= 0 && (wait_ms < 0 || next < wait_ms))
		wait_ms = next;
	struct pollfd *p = NULL;
	int n = 0;
	int *ids = NULL;
	if (ev_nwatches) {
		p = rt_alloc(sizeof *p * (size_t)ev_nwatches);
		ids = rt_alloc(sizeof *ids * (size_t)ev_nwatches);
		for (int i = 0; i < ev_nwatches; i++)
			if (ev_watches[i].used) {
				p[n] = (struct pollfd){ ev_watches[i].fd, POLLIN, 0 };
				ids[n++] = i;
			}
	}
	fflush(stdout);
	flush_open_files();
	if (n || wait_ms != 0) {
		bool held = rt_gil_drop();   /* (workers carry on while it waits) */
		int r = poll(p, (nfds_t)n, wait_ms);
		rt_gil_take(held);
		for (int k = 0; r > 0 && k < n; k++)
			if (p[k].revents) {
				EvWatch w = ev_watches[ids[k]];
				if (w.used)
					w.fn(w.data);
			}
	}
	free(p);
	free(ids);
	/* timers that are due, each once (a slow handler doesn't make a
	 * repeating timer fire twice in a row) */
	int64_t now = now_ms();
	for (int i = 0; i < ev_ntimers; i++) {
		EvTimer *t = &ev_timers[i];
		if (!t->used || !t->running || t->due > now)
			continue;
		if (t->repeat) {
			t->due += t->ms > 0 ? t->ms : 1;
			if (t->due <= now)
				t->due = now + (t->ms > 0 ? t->ms : 1);
		} else {
			t->running = false;
		}
		void *obj = t->obj, *fn = t->fn;
		if (fn) {
			rt_invoke_hook(obj, fn);
			fflush(stdout);
		}
	}
}

/* events.run: until nothing is left to wait for, or events.stop */
void cby_events_run(void)
{
	ev_stopping = false;
	while (!ev_stopping && cby_events_waiting())
		cby_events_step(-1);
	ev_stopping = false;
}

void cby_events_stop(void) { ev_stopping = true; }

/* ---- memory by hand (use addresses): compiled programs; the interpreter
 * keeps its own record of blocks, to check them */
void *cby_allocate(int64_t n, int64_t size, const char *file, int line, int col)
{
	if (n < 0)
		rt_panic(file, line, col, "can't allocate room for %lld values", (long long)n);
	void *p = calloc(n > 0 ? (size_t)n : 1, (size_t)size);
	if (!p)
		rt_panic(file, line, col, "out of memory: can't allocate %lld values", (long long)n);
	return p;
}

void *cby_reallocate(void *old, int64_t n, int64_t size, const char *file, int line, int col)
{
	if (n < 0)
		rt_panic(file, line, col, "can't reallocate to room for %lld values", (long long)n);
	void *p = realloc(old, (size_t)(n > 0 ? n : 1) * (size_t)size);
	if (!p)
		rt_panic(file, line, col, "out of memory: can't reallocate to %lld values", (long long)n);
	return p;
}

/* ------------------------------------------------------------- abs, random, exit, .text places */

/* random: xoshiro256** (fast, and good enough for games, shuffles and
 * dice; secrets.random_number is for what no one must guess), seeded from
 * the system's own random numbers the first time, or by random_seed. Each
 * thread has its own. */
#if defined(__APPLE__)
#include <sys/random.h>
#endif
static _Thread_local uint64_t rnd[4];
static _Thread_local bool rnd_ready;

static uint64_t splitmix(uint64_t *x)
{
	uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

void rt_random_seed(int64_t seed)
{
	uint64_t x = (uint64_t)seed;
	for (int i = 0; i < 4; i++)
		rnd[i] = splitmix(&x);
	rnd_ready = true;
}

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t rnd_next(void)
{
	if (!rnd_ready) {
		uint64_t seed;
		if (getentropy(&seed, sizeof seed) != 0)
			seed = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&seed;
		rt_random_seed((int64_t)seed);
	}
	uint64_t r = rotl(rnd[1] * 5, 7) * 9, t = rnd[1] << 17;
	rnd[2] ^= rnd[0];
	rnd[3] ^= rnd[1];
	rnd[1] ^= rnd[2];
	rnd[0] ^= rnd[3];
	rnd[2] ^= t;
	rnd[3] = rotl(rnd[3], 45);
	return r;
}

/* a whole number from lo to hi, both included, each as likely */
int64_t rt_random_int(int64_t lo, int64_t hi)
{
	if (hi < lo) {
		int64_t t = lo;
		lo = hi;
		hi = t;
	}
	uint64_t span = (uint64_t)hi - (uint64_t)lo + 1;
	if (span == 0)
		return (int64_t)rnd_next();   /* (every long there is) */
	uint64_t limit = UINT64_MAX - UINT64_MAX % span, r;
	do
		r = rnd_next();
	while (r >= limit);
	return (int64_t)((uint64_t)lo + r % span);
}

/* a double from lo up to (not including) hi */
/* close_to: how's 1 bit says a and b were floats, its 2 bit that a limit
 * was given. With one, within `limit` of each other (a limit below 0: never);
 * otherwise the same to about 9 significant digits (a float's, 5), or both
 * within 1e-12 (1e-6) of zero. NaN is close to nothing. */
int32_t rt_close_to(double a, double b, double limit, int32_t how)
{
	bool floats = how & 1;
	if (a == b)
		return 1;   /* and infinities */
	double gap = fabs(a - b);
	if (gap != gap)
		return 0;
	if (how & 2)
		return gap <= limit;
	double rel = floats ? 1e-5 : 1e-9, near0 = floats ? 1e-6 : 1e-12;
	double big = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
	double allowed = rel * big;
	return gap <= (allowed > near0 ? allowed : near0);
}

double rt_random_double(double lo, double hi)
{
	double u = (double)(rnd_next() >> 11) * 0x1.0p-53;
	return lo + (hi - lo) * u;
}

void cby_exit(int32_t status)
{
	flush_open_files();
	fflush(stdout);
	fflush(stderr);
	exit(status & 0xFF);
}

/* a number with so many decimal places: 4.5 with 2 is "4.50" */
static int64_t check_places(int64_t places)
{
	if (places < 0 || places > 60)
		rt_panic(NULL, 0, 0, "%lld decimal places can't be shown (0 to 60 can)", (long long)places);
	return places;
}

int rt_text_decimals(char *buf, size_t size, double v, int64_t places)
{
	int n = snprintf(buf, size, "%.*f", (int)check_places(places), v);
	if (n > 1 && buf[0] == '-' && strspn(buf + 1, "0.") == (size_t)(n - 1))
		memmove(buf, buf + 1, (size_t)n--);   /* (-0.00 is 0.00) */
	return n;
}

int rt_text_decimals_int(char *buf, size_t size, int64_t v, bool is_signed, int64_t places)
{
	int n = is_signed ? snprintf(buf, size, "%lld", (long long)v) : snprintf(buf, size, "%llu", (unsigned long long)v);
	places = check_places(places);
	if (places > 0 && (size_t)n + (size_t)places + 2 < size) {
		buf[n++] = '.';
		for (int64_t i = 0; i < places; i++)
			buf[n++] = '0';
		buf[n] = 0;
	}
	return n;
}

void cby_text_decimals(cby_str *dst, double v, int64_t places)
{
	char buf[400];
	int n = rt_text_decimals(buf, sizeof buf, v, places);
	cby_str_dup(dst, buf, n);
}

void cby_text_decimals_int(cby_str *dst, int64_t v, bool is_signed, int64_t places)
{
	char buf[400];
	int n = rt_text_decimals_int(buf, sizeof buf, v, is_signed, places);
	cby_str_dup(dst, buf, n);
}

/* C's printf, as `use standard` declares it: printed where Cabby's own
 * printing goes now (stderr, inside `stderr ...`; a file, in front of a
 * file's call), so the two never come out of order */
int cby_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(out_stream(), fmt, ap);
	va_end(ap);
	size_t fl = strlen(fmt);
	if (fl)
		rt_line_open = fmt[fl - 1] != '\n';
	return n;
}
