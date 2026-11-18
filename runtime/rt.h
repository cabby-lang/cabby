/* rt.h — Cabby runtime support, shared by the interpreter and compiled programs.
 *
 * Both back ends call these functions so that a program prints exactly the
 * same output and fails with exactly the same panic message whether it is
 * run with --interpret or built with --compile.
 *
 * Output conventions (print / println):
 *   integers  decimal (unsigned types as unsigned)          -42  4294967295
 *   char      the byte itself (signed/unsigned char too)    A
 *   double    shortest form that reads back exactly,        2.5  1.0  1e+20  0.1  inf  NaN
 *             with ".0" when it would look like an integer
 *   float     the same, at float precision                  0.1  (not 0.10000000149011612)
 *   bool      true / false
 *   str       its bytes
 *   print separates arguments with one space; println also ends the line.
 *
 * Panics print "panic at FILE:LINE:COL: MESSAGE" to stderr after flushing
 * stdout, then exit with status 101. No destructors run.
 */
#ifndef CABBY_RT_H
#define CABBY_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RT_PANIC_EXIT 101

/* Input (read_line, input, end_of_input) and text to numbers. */
extern bool rt_input_ended;
char   *rt_read_line(int64_t *len, const char *file, int line, int col);   /* malloc'd, without the Enter */
char    rt_read_char(const char *file, int line, int col);                  /* a line's first character */
int64_t rt_read_key(char *out, const char *file, int line, int col);        /* one key: out has room for 16 */
enum { RT_NUM_INT, RT_NUM_LONG, RT_NUM_DOUBLE };
int     rt_parse_number(const char *data, int64_t len, int what, int64_t *iv, double *dv);
int64_t rt_text_to_int(const char *data, int64_t len, int what, const char *file, int line, int col);
double  rt_text_to_double(const char *data, int64_t len, const char *file, int line, int col);

/* String tools (s.find, s.contains, s[a to b], ...). Those that make text
 * return a malloc'd, zero-terminated buffer. */
enum { RT_TEST_CONTAINS, RT_TEST_STARTS, RT_TEST_ENDS };
enum { RT_SLICE_NO_FROM = 1, RT_SLICE_NO_TO = 2 };
int64_t rt_str_find(const char *h, int64_t hn, const char *n, int64_t nn);
bool    rt_str_test(const char *h, int64_t hn, const char *n, int64_t nn, int what);
int     rt_str_compare(const char *a, int64_t an, const char *b, int64_t bn);
void    rt_str_trim(const char *d, int64_t n, int64_t *start, int64_t *count);
char   *rt_str_case(const char *d, int64_t n, bool upper);
char   *rt_str_replace(const char *d, int64_t n, const char *from, int64_t fn, const char *to, int64_t tn,
		       int64_t *outn);
char   *rt_str_repeat(const char *d, int64_t n, int64_t times, int64_t *outn, const char *file, int line, int col);
char   *rt_str_pad(const char *d, int64_t n, int64_t width, int how, int64_t *outn);
void    rt_list_slice(int64_t len, int64_t from, int64_t to, int flags, int64_t *start, int64_t *count,
		      const char *file, int line, int col);
_Noreturn void rt_list_bounds(int64_t index, int64_t len, bool insert, const char *file, int line, int col);
void    rt_str_slice(int64_t len, int64_t from, int64_t to, int flags, int64_t *start, int64_t *count,
		     const char *file, int line, int col);

/* Where print and println write (`stderr some_call`). */
enum { RT_STDOUT = 1, RT_STDERR = 2, RT_TO_FILE = 3 };
void rt_out_push(int id);
void rt_out_pop(void);
void rt_out_reset(void);      /* after a panic in the interactive session */

/* Printing. */
void rt_print_int(int64_t v);
void rt_print_uint(uint64_t v);
void rt_print_double(double v);
void rt_print_float(float v);
void rt_print_bool(bool v);
void rt_print_char(uint8_t c);
void rt_print_str(const char *data, int64_t len);
void rt_print_sep(void);
void rt_print_newline(void);
void rt_print_list_open(void);     /* "[" */
void rt_print_list_sep(int64_t i);  /* ", " before every element but the first */
void rt_print_list_close(void);    /* "]" */
void rt_print_lookup_open(void);   /* "{" */
void rt_print_lookup_colon(void);  /* ": " */
void rt_print_lookup_close(void);  /* "}" */

/* ---- lookups: a hash index shared by both modes -----------------------
 * Entries live in arrays in the order they were added; the index maps a
 * key's hash to its position. Each index slot holds position + 1, or 0 when
 * empty, or -1 where a removed key was; the index is a power of two in size
 * and never more than half full (removed marks included).
 * `same(ctx, pos)` says whether the entry at pos holds the wanted key (and
 * is still in use). */
uint64_t rt_hash_u64(uint64_t x);
uint64_t rt_hash_bytes(const char *p, int64_t n);
int64_t  rt_index_find(const int32_t *index, int64_t icap, uint64_t hash, bool (*same)(const void *ctx, int64_t pos),
		       const void *ctx);
void     rt_index_add(int32_t *index, int64_t icap, uint64_t hash, int64_t pos);
void     rt_index_remove(int32_t *index, int64_t icap, uint64_t hash, int64_t pos);   /* marks it -1 */
int64_t  rt_index_size(int64_t entries);   /* the index size for this many entries */
/* how a lookup's keys are compared and shown */
enum { RT_KEY_INT, RT_KEY_UINT, RT_KEY_CHAR, RT_KEY_BOOL, RT_KEY_STR };
_Noreturn void rt_lookup_missing_int(int64_t key, int kind, const char *file, int line, int col);
_Noreturn void rt_lookup_missing_str(const char *key, int64_t len, const char *file, int line, int col);

/* ---- files and directories (use files, use directories) ---------------
 * Paths are (data, len); everything that can fail panics with the reason,
 * at file:line:col. Results are allocated with malloc and NUL-terminated;
 * a list of texts is an array of rt_text, freed with rt_texts_free. */
typedef struct { char *data; int64_t len; } rt_text;
char    *rt_files_read(const char *p, int64_t n, int64_t *len, const char *file, int line, int col);
rt_text *rt_files_lines(const char *p, int64_t n, int64_t *count, const char *file, int line, int col);
void     rt_files_write(const char *p, int64_t n, const char *t, int64_t tn, bool append, const char *file, int line,
			int col);
bool     rt_files_exists(const char *p, int64_t n);
int64_t  rt_files_size(const char *p, int64_t n, const char *file, int line, int col);
void     rt_files_copy(const char *a, int64_t an, const char *b, int64_t bn, const char *file, int line, int col);
void     rt_files_rename(const char *a, int64_t an, const char *b, int64_t bn, const char *file, int line, int col);
void     rt_files_delete(const char *p, int64_t n, const char *file, int line, int col);
rt_text *rt_dirs_list(const char *p, int64_t n, bool all_files, int64_t *count, const char *file, int line, int col);
bool     rt_dirs_exists(const char *p, int64_t n);
void     rt_dirs_make(const char *p, int64_t n, const char *file, int line, int col);
void     rt_dirs_remove(const char *p, int64_t n, const char *file, int line, int col);
void     rt_dirs_rename(const char *a, int64_t an, const char *b, int64_t bn, const char *file, int line, int col);
char    *rt_dirs_current(int64_t *len);
void     rt_dirs_change_to(const char *p, int64_t n, const char *file, int line, int col);
char    *rt_dirs_home(int64_t *len);
char    *rt_dirs_save_folder(const char *p, int64_t n, int64_t *len, const char *file, int line, int col);
void     rt_texts_free(rt_text *t, int64_t count);

/* ---- the shell (use shell): commands run by /bin/sh; names and values
 * are (data, len), and results are malloc'd and NUL-terminated */
char    *rt_shell_run(const char *c, int64_t n, int64_t *len, const char *file, int line, int col);
int32_t  rt_shell_status(const char *c, int64_t n, const char *file, int line, int col);
char    *rt_shell_get(const char *name, int64_t n, int64_t *len, const char *file, int line, int col);
bool     rt_shell_has(const char *name, int64_t n);
void     rt_shell_set(const char *name, int64_t n, const char *v, int64_t vn, const char *file, int line, int col);
void     rt_shell_unset(const char *name, int64_t n, const char *file, int line, int col);

/* an open file (use files): closed by rt_file_close, which takes NULL too */
typedef struct rt_file rt_file;
enum { RT_OPEN_READ, RT_OPEN_WRITE, RT_OPEN_ADD, RT_OPEN_CHANGE };
rt_file *rt_file_open(const char *p, int64_t n, int mode, const char *file, int line, int col);
void     rt_file_close(rt_file *f);
char    *rt_file_read_line(rt_file *f, int64_t *len, const char *file, int line, int col);
bool     rt_file_at_end(rt_file *f, const char *file, int line, int col);
void     rt_out_push_file(rt_file *f, const char *file, int line, int col);   /* then rt_out_pop */
void     rt_flush_files(void);   /* writes out what's been printed into open files */

/* ---- the event loop (use events; windows and processes use it too) ---- */
extern void (*rt_invoke_hook)(void *obj, void *fn);   /* calls an object's method */
int64_t cby_timer_new(void);
void    cby_timer_set(int64_t id, void *obj, void *fn);
void    cby_timer_start(int64_t id, int64_t ms, int repeat);
void    cby_timer_stop(int64_t id);
int     cby_timer_running(int64_t id);
void    cby_timer_free(int64_t id);
int     cby_events_watch(int fd, void (*fn)(void *data), void *data);
void    cby_events_unwatch(int id);
int     cby_events_fds(int *fds, int max);
int     cby_events_next_ms(void);
int     cby_events_waiting(void);
void    cby_events_step(int wait_ms);
void    cby_events_run(void);
void    cby_events_stop(void);

/* L.sort: how to compare the elements */
enum { RT_SORT_I8, RT_SORT_I16, RT_SORT_I32, RT_SORT_I64, RT_SORT_U8, RT_SORT_U16, RT_SORT_U32, RT_SORT_U64,
       RT_SORT_F32, RT_SORT_F64, RT_SORT_STR };
/* true when print left a line unfinished (the session starts its prompt
 * on a new line then) */
extern bool rt_line_open;

/* Canonical text of a double / float into buf (>= 40 bytes); returns length. */
int rt_format_double(double v, char *buf);
int rt_format_float(float v, char *buf);

/* n.text: a value as print shows it, into buf (>= 48 bytes); returns the
 * length. `what` is one of the RT_TEXT_ kinds; integers come in `i`,
 * floating-point values in `d`. */
enum { RT_TEXT_INT, RT_TEXT_UINT, RT_TEXT_DOUBLE, RT_TEXT_FLOAT, RT_TEXT_BOOL, RT_TEXT_CHAR };
int rt_format_text(char *buf, int what, int64_t i, double d);

/* Panics. `file` may be NULL. */
extern _Thread_local const char *rt_lib_call_file;
extern _Thread_local int rt_lib_call_line, rt_lib_call_col;
void rt_set_lib_call(const char *file, int line, int col);
extern bool rt_panic_at_lib_call;
_Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));
/* Called (if set) after the panic message, before exiting — the interpreter
 * uses it to show the offending source line. */
extern void (*rt_panic_hook)(int line, int col);
extern void (*rt_before_panic)(void);
/* Interactive session only: rewrites the location printed in the message,
 * and, if set, a panic longjmps here with value 1 instead of exiting. */
extern void (*rt_panic_map)(const char **file, int *line, int *col);
extern _Thread_local void *rt_panic_jmp;   /* a jmp_buf * (each thread's own) */

/* `... else`: a panic caught. rt_try_push before the statement, then
 * _setjmp(T->jb): a panic while it's pushed isn't shown; it pops it, keeps
 * its message (rt_recovered_message) and _longjmps there (with 1). The
 * statement done, rt_try_pop. Each thread has its own. */
#include <setjmp.h>
typedef struct RtTry {
	jmp_buf jb;
	struct RtTry *prev;
	const char *lib_file;   /* what's put back when it's caught */
	int lib_line, lib_col, out_depth;
	bool gil;               /* this thread held the interpreter's lock (threads) */
	void *frames;           /* cby_frame_top when it began */
} RtTry;

/* A compiled function's frame, told to the runtime while an else waits
 * (cby_trying): what it has to drop, if a panic the else catches leaves
 * it, as slot, drop flag (dropped if it's not 0) and how to drop it. */
typedef struct RtFrame {
	struct RtFrame *prev;
	int64_t n;
	struct { void *slot; char *flag; void (*drop)(void *); } items[];
} RtFrame;
extern _Thread_local int cby_trying;
extern _Thread_local RtFrame *cby_frame_top;
void cby_frame_enter(RtFrame *f);

/* abs, random, exit and .text places: Cabby's own (runtime/rt.c) */
void rt_random_seed(int64_t seed);
int64_t rt_random_int(int64_t lo, int64_t hi);
double rt_random_double(double lo, double hi);
int rt_text_decimals(char *buf, size_t size, double v, int64_t places);
int rt_text_decimals_int(char *buf, size_t size, int64_t v, bool is_signed, int64_t places);
void rt_try_push(RtTry *t);
void rt_try_pop(RtTry *t);
const char *rt_recovered_message(void);

/* Threads (runtime/threads.c): the interpreter's lock, so one thread at a
 * time runs Cabby code there, and what a library lets go of at a thread's
 * end. */
bool rt_gil_enter(void);
void rt_gil_leave(bool was);
bool rt_gil_drop(void);
void rt_gil_take(bool was);
void rt_gil_yield(void);
bool rt_gil_is_on(void);
bool rt_gil_held(void);
void rt_on_thread_end(void (*fn)(void));
void rt_thread_ends(void);   /* what a thread kept let go of: at its end */
/* a worker's field or method, used from outside it: a panic while it works */
void rt_worker_check(void *obj, const char *file, int line, int col);

/* Checks for the operations that can fail. Integer + - * wrap around. */
void rt_check_div(int64_t a, int64_t b, int bits, bool is_signed, bool is_rem,
		  const char *file, int line, int col);
void rt_check_shift(int64_t amount, int bits, const char *file, int line, int col);
void rt_bounds_check(int64_t index, int64_t len, const char *file, int line, int col);
_Noreturn void rt_str_bounds(int64_t index, int64_t len, const char *file, int line, int col);

/* Float -> integer conversion: truncates toward zero, saturates at the
 * target's limits, NaN -> 0. Returns the value sign- or zero-extended. */
int64_t rt_float_to_int(double v, int bits, bool is_signed);

/* Memory: panics on exhaustion. */
/* bytes.c: a bytes value (lib/bytes.hby) from C, and its bytes */
int64_t rt_bytes_make(const void *data, int64_t len);
const uint8_t *rt_bytes_data(int64_t h, int64_t *len);

void *rt_alloc(size_t size);
void *rt_alloc_zeroed(size_t size);
void *rt_realloc(void *p, size_t size);
void  rt_free(void *p);
size_t rt_usable_size(void *p);    /* the real size of a block from malloc */

/* ---- helpers used only by compiled programs ----------------------------
 * A str is { char *data; int64_t len } (16 bytes). Bit 62 of len marks
 * a static string (a literal) that must never be freed; the zero value
 * { NULL, 0 } is the empty string. */
#define CBY_STR_STATIC ((int64_t)1 << 62)
typedef struct { char *data; int64_t len; } cby_str;

void    cby_rt_init(void);                                  /* stack-overflow handler */
int     cby_rt_finish(int status);                          /* flush output; returns status */
void    cby_str_dup(cby_str *dst, const char *data, int64_t len);   /* owned copy */
void    cby_str_drop(cby_str *s);
bool    cby_str_eq(const cby_str *a, const cby_str *b);
const char *cby_str_cstr(const cby_str *s);                 /* never NULL */
char   *cby_text_address(cby_str *s);                       /* addresses.of_text */
void    cby_print_str(const cby_str *s);
int64_t cby_str_len(const cby_str *s);                      /* s.length */
void    cby_str_copy(cby_str *dst, const cby_str *src);     /* s.copy */
void    cby_str_concat(cby_str *dst, const cby_str *a, const cby_str *b);   /* a + b */
void    cby_str_append(cby_str *s, const cby_str *b);       /* s += b */
int8_t  cby_str_get(const cby_str *s, int64_t i, const char *file, int line, int col);       /* s[i] */
void    cby_str_set(cby_str *s, int64_t i, int8_t c, const char *file, int line, int col);   /* s[i] = c */
void    cby_text(cby_str *dst, int what, int64_t i, double d);   /* n.text */
cby_str *cby_args(int argc, char **argv);                  /* argv as an array of str */
void    cby_args_free(cby_str *args);

#endif
