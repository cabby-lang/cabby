/* dbus.c — D-Bus, how much of the Linux desktop talks (use dbus).
 *
 * Spoken directly, over the bus's own socket, as networks.c speaks HTTP:
 * no libdbus, so nothing more to install. A connection signs in (SASL
 * EXTERNAL: the bus knows who we are from the socket), says Hello, and
 * then sends and receives messages: method calls and their answers, and
 * signals ("a notification was closed").
 *
 * A message is a header (who it's for, what it is) and a body of values,
 * each laid out at its own alignment, with a signature saying their types
 * ("su" is a string then a 32-bit unsigned number). Answers come back to
 * Cabby as json values (json.c): numbers, text, true or false, lists, and
 * lookups for D-Bus's dictionaries (a{sv}), with variants unwrapped.
 *
 * A program's own objects can have properties, answered by themselves
 * (Get and GetAll), with PropertiesChanged sent when one changes; and open
 * files can be passed over the bus (h, as logind's Inhibit gives one),
 * alongside a message, as the socket allows (SCM_RIGHTS).
 *
 * The wire format: https://dbus.freedesktop.org/doc/dbus-specification.html
 */
#include "rt.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* json.c's, for the answers */
extern int64_t cby_json_new(int64_t kind);
extern int64_t cby_json_from_double(double d);
extern int64_t cby_json_from_bool(int b);
extern void cby_json_release(int64_t h);
extern void cby_json_set(int64_t h, const char *key, int64_t value);
extern void cby_json_add(int64_t h, int64_t value);
extern int64_t cby_json_from_str(const char *s);
enum { J_NULL, J_BOOL, J_NUMBER, J_STR, J_LIST, J_LOOKUP };

/* Cabby's event loop (rt.c) */
extern int cby_events_watch(int fd, void (*fn)(void *data), void *data);
extern void cby_events_unwatch(int id);

enum { M_CALL = 1, M_RETURN = 2, M_ERROR = 3, M_SIGNAL = 4 };
enum { F_PATH = 1, F_INTERFACE, F_MEMBER, F_ERROR_NAME, F_REPLY_SERIAL, F_DESTINATION, F_SENDER, F_SIGNATURE, F_UNIX_FDS };

#ifndef MSG_NOSIGNAL   /* (macOS: SO_NOSIGPIPE on the socket instead) */
#define MSG_NOSIGNAL 0
#endif

#define CALL_WAIT_MS 25000   /* as libdbus: how long a call waits for its answer */

/* ------------------------------------------------------------ bytes */

typedef struct {
	unsigned char *d;
	size_t len, cap;
} Buf;

static void buf_room(Buf *b, size_t n)
{
	if (b->len + n <= b->cap)
		return;
	size_t cap = b->cap ? b->cap * 2 : 256;
	while (cap < b->len + n)
		cap *= 2;
	b->d = rt_realloc(b->d, cap);
	b->cap = cap;
}

static void buf_put(Buf *b, const void *p, size_t n)
{
	buf_room(b, n);
	memcpy(b->d + b->len, p, n);
	b->len += n;
}

static void buf_align(Buf *b, size_t a)
{
	static const unsigned char zeros[8];
	size_t pad = (a - b->len % a) % a;
	if (pad)
		buf_put(b, zeros, pad);
}

static void buf_u32(Buf *b, uint32_t v) { buf_align(b, 4); buf_put(b, &v, 4); }

static void buf_string(Buf *b, const char *s)   /* s or o: length, the text, a nul */
{
	uint32_t n = (uint32_t)strlen(s);
	buf_u32(b, n);
	buf_put(b, s, n + 1);
}

static void buf_signature(Buf *b, const char *s)   /* g: a byte's length */
{
	unsigned char n = (unsigned char)strlen(s);
	buf_put(b, &n, 1);
	buf_put(b, s, (size_t)n + 1);
}

/* how each type is aligned */
static size_t align_of(char t)
{
	switch (t) {
	case 'y': case 'g': case 'v': return 1;
	case 'n': case 'q': return 2;
	case 'x': case 't': case 'd': case '(': case '{': return 8;
	default: return 4;   /* b i u h s o a */
	}
}

/* ------------------------------------------------------------ messages Cabby builds */

typedef struct {
	char kind;          /* 'a' a list, 'd' a lookup (a{..}), 'v' a variant, '(' a struct */
	size_t len_at;      /* a list's: where its length goes */
	size_t start;       /* and where its items start */
	char want[64];      /* what each item must be (a lookup: key then value) */
	int count;          /* items so far */
	bool quiet;         /* the signature's already said what's inside */
	size_t sig_at;
} Frame;

typedef struct {
	char *dest, *path, *iface, *member;
	int type;           /* M_CALL, M_SIGNAL, or M_RETURN (an answer, to a call being handled) */
	Buf body;
	char sig[256];
	Frame stack[32];
	int depth;
	int *fds;           /* files it carries (h), its own copies */
	int nfds;
} Out;

static Out *out_of(int64_t id) { return (Out *)(intptr_t)id; }

static char *dup_s(const char *s) { size_t n = strlen(s) + 1; char *d = rt_alloc(n); memcpy(d, s, n); return d; }

int64_t cby_dbus_message_new(const char *dest, const char *path, const char *iface, const char *member)
{
	if (!path[0] || path[0] != '/')
		rt_panic(NULL, 0, 0, "a D-Bus object's path starts with '/' (\"/org/freedesktop/Notifications\"), not \"%s\"",
			 path);
	if (!member[0])
		rt_panic(NULL, 0, 0, "a D-Bus call needs the method's name");
	Out *o = rt_alloc_zeroed(sizeof *o);
	o->type = M_CALL;
	o->dest = dup_s(dest);
	o->path = dup_s(path);
	o->iface = dup_s(iface);
	o->member = dup_s(member);
	return (int64_t)(intptr_t)o;
}

/* a signal, sent from the object at path */
int64_t cby_dbus_signal_new(const char *path, const char *iface, const char *member)
{
	if (!iface[0])
		rt_panic(NULL, 0, 0, "a D-Bus signal needs its interface (\"org.example.Thing\")");
	int64_t id = cby_dbus_message_new("", path, iface, member);
	out_of(id)->type = M_SIGNAL;
	return id;
}

/* an answer, to the call being handled */
int64_t cby_dbus_answer_new(void)
{
	Out *o = rt_alloc_zeroed(sizeof *o);
	o->type = M_RETURN;
	o->dest = dup_s("");
	o->path = dup_s("");
	o->iface = dup_s("");
	o->member = dup_s("");
	return (int64_t)(intptr_t)o;
}

void cby_dbus_message_free(int64_t id)
{
	Out *o = out_of(id);
	if (!o)
		return;
	free(o->dest);
	free(o->path);
	free(o->iface);
	free(o->member);
	free(o->body.d);
	for (int i = 0; i < o->nfds; i++)
		close(o->fds[i]);
	free(o->fds);
	free(o);
}

static void sig_add(Out *o, const char *s)
{
	if (strlen(o->sig) + strlen(s) >= sizeof o->sig)
		rt_panic(NULL, 0, 0, "this D-Bus message has too many values (its signature is over 255 characters)");
	strcat(o->sig, s);
}

/* a value of type t is about to be written: said in the signature (unless
 * a list or variant already has), checked against what a list wants, and
 * a lookup's key aligned as its entry */
static void item(Out *o, char t)
{
	Frame *f = o->depth ? &o->stack[o->depth - 1] : NULL;
	if (!f || !f->quiet) {
		char s[2] = { t, 0 };
		if (t != '(')
			sig_add(o, s);
		return;
	}
	if (f->kind == 'v') {
		if (f->count++ > 0)
			rt_panic(NULL, 0, 0, "a variant holds one value: end it (end_variant) before adding another");
		if (f->want[0] && f->want[0] != t)
			rt_panic(NULL, 0, 0, "this variant was begun as '%s', so its value can't be a '%c'", f->want, t);
		return;
	}
	if (f->kind == 'd') {
		if (f->count % 2 == 0)
			buf_align(&o->body, 8);   /* each entry is a struct */
		char w = f->want[f->count % 2 == 0 ? 0 : 1];
		if (w && w != t && !(t == '(' && w == '('))
			rt_panic(NULL, 0, 0, "this lookup was begun with %s '%c', so it can't be given a '%c'",
				 f->count % 2 == 0 ? "keys of type" : "values of type", w, t);
		f->count++;
		return;
	}
	if (f->kind == 'a') {
		if (f->want[0] && f->want[0] != t)
			rt_panic(NULL, 0, 0, "this list was begun as a list of '%s', so it can't be given a '%c'", f->want, t);
		f->count++;
	}
}

static bool is_basic(char t) { return t && strchr("ybnqiuxtdsogh", t) != NULL; }

/* checks a type given as text ("s", "a{sv}", "(si)") is one, whole */
static const char *one_type(const char *s, int depth)
{
	if (depth > 32 || !*s)
		return NULL;
	if (is_basic(*s) || *s == 'v')
		return s + 1;
	if (*s == 'a') {
		if (s[1] == '{') {
			if (!is_basic(s[2]))
				return NULL;
			const char *v = one_type(s + 3, depth + 1);
			return v && *v == '}' ? v + 1 : NULL;
		}
		return one_type(s + 1, depth + 1);
	}
	if (*s == '(') {
		s++;
		if (*s == ')')
			return NULL;
		while (*s && *s != ')')
			if (!(s = one_type(s, depth + 1)))
				return NULL;
		return *s == ')' ? s + 1 : NULL;
	}
	return NULL;
}

static void check_type(const char *t, const char *what)
{
	const char *end = one_type(t, 0);
	if (!end || *end)
		rt_panic(NULL, 0, 0, "\"%s\" isn't a D-Bus type for %s (one type, such as \"s\", \"u\", \"a{sv}\" or "
				     "\"(si)\")", t, what);
}

static Frame *push(Out *o, char kind)
{
	if (o->depth == 32)
		rt_panic(NULL, 0, 0, "a D-Bus message can only nest 32 deep");
	Frame *f = &o->stack[o->depth++];
	memset(f, 0, sizeof *f);
	f->kind = kind;
	return f;
}

static Frame *pop(Out *o, char kind, const char *name)
{
	if (!o->depth || o->stack[o->depth - 1].kind != kind)
		rt_panic(NULL, 0, 0, "'%s' with nothing of that kind begun", name);
	return &o->stack[--o->depth];
}

static void ensure_closed(Out *o)
{
	if (o->depth) {
		static const char *const names[] = { ['a'] = "end_list", ['d'] = "end_lookup", ['v'] = "end_variant",
						     ['('] = "end_struct" };
		rt_panic(NULL, 0, 0, "this D-Bus message has a %s still begun: '%s' it first",
			 o->stack[o->depth - 1].kind == 'a' ? "list" : o->stack[o->depth - 1].kind == 'd' ? "lookup" :
			 o->stack[o->depth - 1].kind == 'v' ? "variant" : "struct", names[(int)o->stack[o->depth - 1].kind]);
	}
}

void cby_dbus_add_str(int64_t id, const char *s)      { Out *o = out_of(id); item(o, 's'); buf_string(&o->body, s); }
void cby_dbus_add_path(int64_t id, const char *s)
{
	if (s[0] != '/')
		rt_panic(NULL, 0, 0, "a D-Bus object's path starts with '/', not \"%s\"", s);
	Out *o = out_of(id);
	item(o, 'o');
	buf_string(&o->body, s);
}
void cby_dbus_add_bool(int64_t id, int b)             { Out *o = out_of(id); item(o, 'b'); buf_u32(&o->body, b != 0); }
void cby_dbus_add_byte(int64_t id, int64_t v)         { Out *o = out_of(id); item(o, 'y'); unsigned char c = (unsigned char)v; buf_put(&o->body, &c, 1); }
void cby_dbus_add_int(int64_t id, int64_t v)          { Out *o = out_of(id); item(o, 'i'); buf_u32(&o->body, (uint32_t)(int32_t)v); }
void cby_dbus_add_uint(int64_t id, int64_t v)         { Out *o = out_of(id); item(o, 'u'); buf_u32(&o->body, (uint32_t)v); }
void cby_dbus_add_long(int64_t id, int64_t v)         { Out *o = out_of(id); item(o, 'x'); buf_align(&o->body, 8); buf_put(&o->body, &v, 8); }
void cby_dbus_add_ulong(int64_t id, int64_t v)        { Out *o = out_of(id); item(o, 't'); buf_align(&o->body, 8); buf_put(&o->body, &v, 8); }
void cby_dbus_add_double(int64_t id, double v)        { Out *o = out_of(id); item(o, 'd'); buf_align(&o->body, 8); buf_put(&o->body, &v, 8); }

/* an open file (h): the message carries a copy of it, and the program
 * keeps its own */
void cby_dbus_add_file(int64_t id, int64_t fd)
{
	Out *o = out_of(id);
	int copy = fd >= 0 ? fcntl((int)fd, F_DUPFD_CLOEXEC, 3) : -1;
	if (copy < 0)
		rt_panic(NULL, 0, 0, "%lld isn't an open file, to send over D-Bus", (long long)fd);
	item(o, 'h');
	o->fds = rt_realloc(o->fds, sizeof *o->fds * (size_t)(o->nfds + 1));
	buf_u32(&o->body, (uint32_t)o->nfds);   /* (the place in the files the message carries) */
	o->fds[o->nfds++] = copy;
}

/* a file a message brought (from a value of type h), closed */
void cby_dbus_close_file(int64_t fd)
{
	if (fd >= 0)
		close((int)fd);
}

void cby_dbus_begin_list(int64_t id, const char *t)
{
	check_type(t, "a list's items");
	Out *o = out_of(id);
	item(o, 'a');
	bool quiet = o->depth && o->stack[o->depth - 1].quiet;
	if (!quiet) {
		sig_add(o, t);   /* ("a" was added by item) */
	}
	buf_u32(&o->body, 0);
	Frame *f = push(o, 'a');
	f->len_at = o->body.len - 4;
	buf_align(&o->body, align_of(t[0]));
	f->start = o->body.len;
	snprintf(f->want, sizeof f->want, "%s", t);
	f->quiet = true;
}

void cby_dbus_end_list(int64_t id)
{
	Out *o = out_of(id);
	Frame f = *pop(o, 'a', "end_list");
	uint32_t n = (uint32_t)(o->body.len - f.start);
	memcpy(o->body.d + f.len_at, &n, 4);
}

void cby_dbus_begin_lookup(int64_t id, const char *k, const char *v)
{
	if (!is_basic(k[0]) || k[1])
		rt_panic(NULL, 0, 0, "a D-Bus lookup's keys are one plain type (\"s\", \"u\"...), not \"%s\"", k);
	check_type(v, "a lookup's values");
	Out *o = out_of(id);
	item(o, 'a');
	if (!(o->depth && o->stack[o->depth - 1].quiet)) {
		sig_add(o, "{");
		sig_add(o, k);
		sig_add(o, v);
		sig_add(o, "}");
	}
	buf_u32(&o->body, 0);
	Frame *f = push(o, 'd');
	f->len_at = o->body.len - 4;
	buf_align(&o->body, 8);
	f->start = o->body.len;
	snprintf(f->want, sizeof f->want, "%c%s", k[0], v);
	f->want[2] = 0;   /* (the value's first character is enough to check) */
	f->quiet = true;
}

void cby_dbus_end_lookup(int64_t id)
{
	Out *o = out_of(id);
	Frame f = *pop(o, 'd', "end_lookup");
	if (f.count % 2)
		rt_panic(NULL, 0, 0, "this lookup has a key with no value after it");
	uint32_t n = (uint32_t)(o->body.len - f.start);
	memcpy(o->body.d + f.len_at, &n, 4);
}

void cby_dbus_begin_variant(int64_t id, const char *t)
{
	check_type(t, "a variant's value");
	Out *o = out_of(id);
	item(o, 'v');
	buf_signature(&o->body, t);
	Frame *f = push(o, 'v');
	snprintf(f->want, sizeof f->want, "%s", t);
	f->quiet = true;
}

void cby_dbus_end_variant(int64_t id)
{
	Out *o = out_of(id);
	Frame f = *pop(o, 'v', "end_variant");
	if (!f.count)
		rt_panic(NULL, 0, 0, "this variant was ended with no value in it");
}

void cby_dbus_begin_struct(int64_t id)
{
	Out *o = out_of(id);
	item(o, '(');
	bool quiet = o->depth && o->stack[o->depth - 1].quiet;
	if (!quiet)
		sig_add(o, "(");
	buf_align(&o->body, 8);
	Frame *f = push(o, '(');
	f->quiet = quiet;
	f->sig_at = strlen(o->sig);
}

void cby_dbus_end_struct(int64_t id)
{
	Out *o = out_of(id);
	Frame f = *pop(o, '(', "end_struct");
	if (!f.quiet) {
		if (strlen(o->sig) == f.sig_at)
			rt_panic(NULL, 0, 0, "a D-Bus struct can't be empty");
		sig_add(o, ")");
	}
}

/* ------------------------------------------------------------ reading values, into json */

typedef struct {
	const unsigned char *d;
	size_t len, pos;
	bool swap;    /* sent big-endian */
	bool bad;
	int *fds;     /* the files the message brought (h: which of them), taken as they're read */
	int nfds;
} In;

static bool in_align(In *r, size_t a)
{
	size_t p = (r->pos + a - 1) / a * a;
	if (p > r->len)
		return r->bad = true, false;
	r->pos = p;
	return true;
}

static bool in_take(In *r, void *dst, size_t n)
{
	if (r->pos + n > r->len)
		return r->bad = true, false;
	memcpy(dst, r->d + r->pos, n);
	if (r->swap && n > 1) {
		unsigned char *b = dst;
		for (size_t i = 0; i < n / 2; i++) {
			unsigned char t = b[i];
			b[i] = b[n - 1 - i];
			b[n - 1 - i] = t;
		}
	}
	r->pos += n;
	return true;
}

static uint32_t in_u32(In *r)
{
	uint32_t v = 0;
	if (in_align(r, 4))
		in_take(r, &v, 4);
	return v;
}

/* s or o (or g, with a byte's length): the text, which stays in the message */
static const char *in_text(In *r, bool sig, uint32_t *n_out)
{
	uint32_t n;
	if (sig) {
		unsigned char b = 0;
		in_take(r, &b, 1);
		n = b;
	} else {
		n = in_u32(r);
	}
	if (r->bad || r->pos + n + 1 > r->len || r->d[r->pos + n] != 0) {
		r->bad = true;
		return "";
	}
	const char *s = (const char *)r->d + r->pos;
	r->pos += n + 1;
	if (n_out)
		*n_out = n;
	return s;
}

/* the end of the one type at s */
static const char *type_end(const char *s)
{
	const char *e = one_type(s, 0);
	return e ? e : s + strlen(s);
}

static int64_t text_node(const char *s, uint32_t n)
{
	char *c = rt_alloc((size_t)n + 1);
	memcpy(c, s, n);
	c[n] = 0;
	int64_t h = cby_json_from_str(c);
	free(c);
	return h;
}

static int64_t read_value(In *r, const char **sig, int depth);

static int64_t read_basic(In *r, char t)
{
	switch (t) {
	case 'y': { uint8_t v = 0; in_take(r, &v, 1); return cby_json_from_double(v); }
	case 'b': return cby_json_from_bool(in_u32(r) != 0);
	case 'n': { int16_t v = 0; if (in_align(r, 2)) in_take(r, &v, 2); return cby_json_from_double(v); }
	case 'q': { uint16_t v = 0; if (in_align(r, 2)) in_take(r, &v, 2); return cby_json_from_double(v); }
	case 'i': return cby_json_from_double((int32_t)in_u32(r));
	case 'u': return cby_json_from_double(in_u32(r));
	case 'h': {
		/* a file: the program's from now on (dbus.close_file), -1 if it didn't come */
		uint32_t i = in_u32(r);
		int fd = -1;
		if (r->fds && i < (uint32_t)r->nfds) {
			fd = r->fds[i];
			r->fds[i] = -1;
		}
		return cby_json_from_double(fd);
	}
	case 'x': { int64_t v = 0; if (in_align(r, 8)) in_take(r, &v, 8); return cby_json_from_double((double)v); }
	case 't': { uint64_t v = 0; if (in_align(r, 8)) in_take(r, &v, 8); return cby_json_from_double((double)v); }
	case 'd': { double v = 0; if (in_align(r, 8)) in_take(r, &v, 8); return cby_json_from_double(v); }
	case 's': case 'o': case 'g': {
		uint32_t n = 0;
		const char *s = in_text(r, t == 'g', &n);
		return r->bad ? cby_json_new(J_NULL) : text_node(s, n);
	}
	}
	r->bad = true;
	return cby_json_new(J_NULL);
}

/* the key of a dictionary entry, written as text */
static void read_key(In *r, char t, char *buf, size_t size)
{
	switch (t) {
	case 's': case 'o': case 'g': {
		uint32_t n = 0;
		const char *s = in_text(r, t == 'g', &n);
		snprintf(buf, size, "%.*s", (int)n, s);
		return;
	}
	case 'y': { uint8_t v = 0; in_take(r, &v, 1); snprintf(buf, size, "%u", v); return; }
	case 'b': snprintf(buf, size, "%s", in_u32(r) ? "true" : "false"); return;
	case 'n': { int16_t v = 0; if (in_align(r, 2)) in_take(r, &v, 2); snprintf(buf, size, "%d", v); return; }
	case 'q': { uint16_t v = 0; if (in_align(r, 2)) in_take(r, &v, 2); snprintf(buf, size, "%u", v); return; }
	case 'i': snprintf(buf, size, "%d", (int32_t)in_u32(r)); return;
	case 'u': case 'h': snprintf(buf, size, "%u", in_u32(r)); return;
	case 'x': { int64_t v = 0; if (in_align(r, 8)) in_take(r, &v, 8); snprintf(buf, size, "%lld", (long long)v); return; }
	case 't': { uint64_t v = 0; if (in_align(r, 8)) in_take(r, &v, 8); snprintf(buf, size, "%llu", (unsigned long long)v); return; }
	case 'd': { double v = 0; if (in_align(r, 8)) in_take(r, &v, 8); snprintf(buf, size, "%g", v); return; }
	}
	r->bad = true;
	buf[0] = 0;
}

static int64_t read_value(In *r, const char **sig, int depth)
{
	char t = **sig;
	if (depth > 64 || !t) {
		r->bad = true;
		return cby_json_new(J_NULL);
	}
	if (is_basic(t)) {
		(*sig)++;
		return read_basic(r, t);
	}
	if (t == 'v') {   /* unwrapped: the value it holds */
		(*sig)++;
		uint32_t n = 0;
		const char *inner = in_text(r, true, &n);
		if (r->bad)
			return cby_json_new(J_NULL);
		char held[256];
		snprintf(held, sizeof held, "%.*s", (int)n, inner);
		const char *p = held;
		return read_value(r, &p, depth + 1);
	}
	if (t == '(') {
		(*sig)++;
		in_align(r, 8);
		int64_t list = cby_json_new(J_LIST);
		while (**sig && **sig != ')' && !r->bad) {
			int64_t v = read_value(r, sig, depth + 1);
			cby_json_add(list, v);
			cby_json_release(v);
		}
		if (**sig == ')')
			(*sig)++;
		return list;
	}
	if (t == 'a') {
		const char *elem = *sig + 1;
		uint32_t n = in_u32(r);
		if (r->bad || n > 64u << 20)
			return r->bad = true, cby_json_new(J_NULL);
		if (*elem == '{') {
			char kt = elem[1];
			const char *vt = elem + 2;
			const char *after = type_end(vt);
			in_align(r, 8);
			size_t end = r->pos + n;
			int64_t lk = cby_json_new(J_LOOKUP);
			while (r->pos < end && !r->bad) {
				in_align(r, 8);
				char key[256];
				read_key(r, kt, key, sizeof key);
				const char *p = vt;
				int64_t v = read_value(r, &p, depth + 1);
				cby_json_set(lk, key, v);
				cby_json_release(v);
			}
			*sig = after + 1;   /* (past the '}') */
			return lk;
		}
		const char *after = type_end(elem);
		in_align(r, align_of(*elem));
		size_t end = r->pos + n;
		if (end > r->len)
			return r->bad = true, cby_json_new(J_NULL);
		int64_t list = cby_json_new(J_LIST);
		while (r->pos < end && !r->bad) {
			const char *p = elem;
			int64_t v = read_value(r, &p, depth + 1);
			cby_json_add(list, v);
			cby_json_release(v);
		}
		*sig = after;
		return list;
	}
	r->bad = true;
	return cby_json_new(J_NULL);
}

/* the whole body: a list, one item for each value */
static int64_t read_body(const unsigned char *d, size_t len, bool swap, const char *sig, int *fds, int nfds)
{
	In r = { d, len, 0, swap, false, fds, nfds };
	int64_t list = cby_json_new(J_LIST);
	const char *p = sig;
	while (*p && !r.bad) {
		int64_t v = read_value(&r, &p, 0);
		cby_json_add(list, v);
		cby_json_release(v);
	}
	if (r.bad) {
		cby_json_release(list);
		return 0;
	}
	return list;
}

/* ------------------------------------------------------------ a connection */

typedef struct {
	unsigned char *raw;   /* the whole message */
	size_t len;
	int type;
	uint32_t serial, reply_to;
	bool swap;
	bool no_reply;        /* a call that wants no answer */
	const char *path, *iface, *member, *error, *sender, *sig;   /* (in raw) */
	size_t body_at;
	uint32_t body_len;
	int *fds;             /* the files it brought (h), until the program takes them */
	int nfds;
} Msg;

typedef struct {
	char *iface, *member;
} Sub;

typedef struct {
	int fd;
	uint32_t serial;
	char *name;            /* its unique name, ":1.42" */
	Msg **queue;           /* signals that came while a call waited */
	int nqueue, capqueue;
	Sub *subs;
	int nsubs;
	int watch, wake[2], wake_watch;
	char *error;           /* try_call's: "" when it worked */
	Msg *signal;           /* the signal being handled */
	int64_t signal_args;
	bool dispatching;
	void *obj, *fn;        /* the bus's own say_signal, which calls the program's handler */
	char **served;         /* the paths of the objects it has ("/org/example/Thing") */
	char **described;      /* and for each, its interfaces in D-Bus's XML ("" if not said) */
	int nserved;
	void *call_obj, *call_fn;   /* the bus's say_call, for each call to one of them */
	Msg *current;          /* the call being handled */
	int64_t current_args;
	bool answered;         /* and whether it's been answered */
	bool fds_ok;           /* the bus passes open files (h) */
	int *inbox;            /* files that came, for the messages they came with */
	int ninbox;
	struct Prop *props;    /* the program's objects' properties */
	int nprops;
	/* a tree of objects served from C (the gui library's, for screen
	 * readers): every path under the prefix, answered at once */
	char *tree_prefix;
	void (*tree_fn)(void *ctx, int64_t bus, const char *path, const char *iface, const char *member, int64_t args);
	void *tree_ctx;
	int wait_ms;           /* how long a call waits for its answer (0: CALL_WAIT_MS) */
} Bus;

/* one property of one of the program's objects: its value, as a
 * message's body of one value */
typedef struct Prop {
	char *path, *iface, *name, *sig;
	unsigned char *body;
	size_t len;
} Prop;

static Bus *bus_of(int64_t id) { return (Bus *)(intptr_t)id; }

static void msg_free(Msg *m)
{
	if (m) {
		for (int i = 0; i < m->nfds; i++)
			if (m->fds[i] >= 0)
				close(m->fds[i]);
		free(m->fds);
		free(m->raw);
		free(m);
	}
}

/* all of n bytes, or false (the bus went away) */
static bool write_all(int fd, const void *p, size_t n)
{
	const char *c = p;
	while (n) {
		ssize_t w = send(fd, c, n, MSG_NOSIGNAL);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return false;
		c += w;
		n -= (size_t)w;
	}
	return true;
}

/* a whole message, and the files it carries (out_fds) alongside its
 * first byte; out_fds is cleared */
static bool send_raw(Bus *bus, const void *p, size_t n);

static bool read_all(int fd, void *p, size_t n)
{
	char *c = p;
	while (n) {
		ssize_t r = read(fd, c, n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		c += r;
		n -= (size_t)r;
	}
	return true;
}

/* a line of the sign-in conversation, "\r\n" ended */
static bool read_line(int fd, char *buf, size_t size)
{
	size_t n = 0;
	while (n + 1 < size) {
		char c;
		if (!read_all(fd, &c, 1))
			return false;
		buf[n++] = c;
		if (n >= 2 && buf[n - 2] == '\r' && buf[n - 1] == '\n') {
			buf[n - 2] = 0;
			return true;
		}
	}
	return false;
}

/* a socket's address: "unix:path=/run/user/1000/bus", or abstract= */
static int connect_to(const char *address, char *why, size_t why_size)
{
	char one[512];
	while (address && *address) {
		const char *semi = strchr(address, ';');
		size_t n = semi ? (size_t)(semi - address) : strlen(address);
		snprintf(one, sizeof one, "%.*s", (int)n, address);
		address = semi ? semi + 1 : NULL;
		if (strncmp(one, "unix:", 5) != 0)
			continue;
		struct sockaddr_un sa;
		memset(&sa, 0, sizeof sa);
		sa.sun_family = AF_UNIX;
		socklen_t len = 0;
		char *path = strstr(one, "path="), *abs = strstr(one, "abstract=");
		if (path) {
			path += 5;
			char *comma = strchr(path, ',');
			if (comma)
				*comma = 0;
			snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
			len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(sa.sun_path) + 1);
		} else if (abs) {
			abs += 9;
			char *comma = strchr(abs, ',');
			if (comma)
				*comma = 0;
			size_t an = strlen(abs);
			if (an + 1 > sizeof sa.sun_path)
				continue;
			memcpy(sa.sun_path + 1, abs, an);   /* (a nul first: Linux's abstract names) */
			len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + an);
		} else {
			continue;
		}
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			continue;
		if (connect(fd, (struct sockaddr *)&sa, len) == 0) {
			fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
			int on = 1;
			setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
			return fd;
		}
		snprintf(why, why_size, "%s", strerror(errno));
		close(fd);
	}
	return -1;
}

/* a message's whole bytes: the header, its fields, then the body */
/* the files the next message built and sent carries (h), set just before */
static _Thread_local const int *out_fds;
static _Thread_local int nout_fds;

static void build(Buf *b, int type, uint32_t serial, const char *dest, const char *path, const char *iface,
		  const char *member, const char *sig, const Buf *body, uint32_t reply_to, const char *error)
{
	unsigned char head[4] = { 'l', (unsigned char)type, 0, 1 };
	buf_put(b, head, 4);
	uint32_t blen = body ? (uint32_t)body->len : 0;
	buf_put(b, &blen, 4);
	buf_put(b, &serial, 4);
	size_t fields_len_at = b->len;
	buf_u32(b, 0);
	size_t fields_start = b->len;   /* (already 8-aligned: 16) */
	struct { int code; char t; const char *s; } fs[] = {
		{ F_PATH, 'o', path }, { F_INTERFACE, 's', iface }, { F_MEMBER, 's', member },
		{ F_ERROR_NAME, 's', error }, { F_DESTINATION, 's', dest }, { F_SIGNATURE, 'g', sig },
	};
	for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
		if (!fs[i].s || !fs[i].s[0])
			continue;
		buf_align(b, 8);
		unsigned char code = (unsigned char)fs[i].code;
		buf_put(b, &code, 1);
		char t[2] = { fs[i].t, 0 };
		buf_signature(b, t);
		if (fs[i].t == 'g')
			buf_signature(b, fs[i].s);
		else
			buf_string(b, fs[i].s);
	}
	if (reply_to) {
		buf_align(b, 8);
		unsigned char code = F_REPLY_SERIAL;
		buf_put(b, &code, 1);
		buf_signature(b, "u");
		buf_u32(b, reply_to);
	}
	if (nout_fds) {
		buf_align(b, 8);
		unsigned char code = F_UNIX_FDS;
		buf_put(b, &code, 1);
		buf_signature(b, "u");
		buf_u32(b, (uint32_t)nout_fds);
	}
	uint32_t flen = (uint32_t)(b->len - fields_start);
	memcpy(b->d + fields_len_at, &flen, 4);
	buf_align(b, 8);
	if (body && body->len)
		buf_put(b, body->d, body->len);
}

/* the next whole message from the bus (waiting for it), or NULL: gone */
/* n bytes from the bus, keeping any files that come with them (in the
 * inbox, for the message they belong to) */
static bool bus_read(Bus *bus, void *p, size_t n)
{
	char *c = p;
	while (n) {
		struct iovec io = { c, n };
		union {
			struct cmsghdr h;
			char room[CMSG_SPACE(sizeof(int) * 16)];
		} ctl;
		struct msghdr mh = { 0 };
		mh.msg_iov = &io;
		mh.msg_iovlen = 1;
		mh.msg_control = ctl.room;
		mh.msg_controllen = sizeof ctl.room;
#ifdef MSG_CMSG_CLOEXEC
		ssize_t r = recvmsg(bus->fd, &mh, MSG_CMSG_CLOEXEC);
#else
		ssize_t r = recvmsg(bus->fd, &mh, 0);
#endif
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return false;
		for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm)) {
			if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS)
				continue;
			int k = (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int));
			bus->inbox = rt_realloc(bus->inbox, sizeof *bus->inbox * (size_t)(bus->ninbox + k));
			memcpy(bus->inbox + bus->ninbox, CMSG_DATA(cm), sizeof(int) * (size_t)k);
			bus->ninbox += k;
		}
		c += r;
		n -= (size_t)r;
	}
	return true;
}

static bool send_raw(Bus *bus, const void *p, size_t n)
{
	if (!nout_fds)
		return write_all(bus->fd, p, n);
	int k = nout_fds;
	nout_fds = 0;
	if (!bus->fds_ok)
		rt_panic(NULL, 0, 0, "this bus doesn't pass open files (it didn't agree to, when this program signed in)");
	union {
		struct cmsghdr h;
		char room[CMSG_SPACE(sizeof(int) * 64)];
	} ctl;
	if (k > 64)
		rt_panic(NULL, 0, 0, "a D-Bus message can carry at most 64 open files");
	memset(&ctl, 0, sizeof ctl);
	struct iovec io = { (void *)p, 1 };
	struct msghdr mh = { 0 };
	mh.msg_iov = &io;
	mh.msg_iovlen = 1;
	mh.msg_control = ctl.room;
	mh.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)k);
	struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)k);
	memcpy(CMSG_DATA(cm), out_fds, sizeof(int) * (size_t)k);
	ssize_t w;
	do
		w = sendmsg(bus->fd, &mh, MSG_NOSIGNAL);
	while (w < 0 && errno == EINTR);
	if (w != 1)
		return false;
	return n == 1 || write_all(bus->fd, (const char *)p + 1, n - 1);   /* (the files go with the first byte) */
}

static Msg *receive(Bus *bus)
{
	unsigned char head[16];
	if (!bus_read(bus, head, 16))
		return NULL;
	bool swap = head[0] == 'B';
	In r = { head, 16, 4, swap, false, NULL, 0 };
	uint32_t blen = 0, serial = 0, flen = 0;
	in_take(&r, &blen, 4);
	in_take(&r, &serial, 4);
	in_take(&r, &flen, 4);
	if ((head[0] != 'l' && head[0] != 'B') || flen > (64u << 20) || blen > (128u << 20))
		return NULL;
	size_t fields_end = 16 + (size_t)flen;
	size_t body_at = (fields_end + 7) / 8 * 8;
	size_t total = body_at + blen;
	Msg *m = rt_alloc_zeroed(sizeof *m);
	m->raw = rt_alloc(total + 1);
	memcpy(m->raw, head, 16);
	if (!bus_read(bus, m->raw + 16, total - 16)) {
		msg_free(m);
		return NULL;
	}
	m->raw[total] = 0;
	m->len = total;
	m->type = head[1];
	m->no_reply = (head[2] & 1) != 0;
	m->serial = serial;
	m->swap = swap;
	m->body_at = body_at;
	m->body_len = blen;
	m->sig = m->path = m->iface = m->member = m->error = m->sender = "";
	In f = { m->raw, fields_end, 16, swap, false, NULL, 0 };
	while (f.pos < fields_end && !f.bad) {
		if (!in_align(&f, 8) || f.pos >= fields_end)
			break;
		uint8_t code = 0;
		in_take(&f, &code, 1);
		const char *t = in_text(&f, true, NULL);
		if (f.bad)
			break;
		if (t[0] == 'u' && !t[1]) {
			uint32_t v = in_u32(&f);
			if (code == F_REPLY_SERIAL)
				m->reply_to = v;
			if (code == F_UNIX_FDS && v <= (uint32_t)bus->ninbox) {
				/* the files that came with it */
				m->fds = rt_alloc(sizeof *m->fds * (v ? v : 1));
				memcpy(m->fds, bus->inbox, sizeof *m->fds * v);
				m->nfds = (int)v;
				memmove(bus->inbox, bus->inbox + v, sizeof *bus->inbox * (size_t)(bus->ninbox - (int)v));
				bus->ninbox -= (int)v;
			}
		} else if ((t[0] == 's' || t[0] == 'o' || t[0] == 'g') && !t[1]) {
			const char *s = in_text(&f, t[0] == 'g', NULL);
			switch (code) {
			case F_PATH: m->path = s; break;
			case F_INTERFACE: m->iface = s; break;
			case F_MEMBER: m->member = s; break;
			case F_ERROR_NAME: m->error = s; break;
			case F_SENDER: m->sender = s; break;
			case F_SIGNATURE: m->sig = s; break;
			}
		} else {
			break;   /* (a field of a kind no one sends) */
		}
	}
	return m;
}

static void queue_signal(Bus *bus, Msg *m)
{
	if (bus->nqueue == bus->capqueue) {
		bus->capqueue = bus->capqueue ? bus->capqueue * 2 : 8;
		bus->queue = rt_realloc(bus->queue, sizeof *bus->queue * (size_t)bus->capqueue);
	}
	bus->queue[bus->nqueue++] = m;
}

/* an error, as the answer to m */
static void answer_error(Bus *bus, const Msg *m, const char *name, const char *text)
{
	if (!m->sender[0] || m->no_reply)
		return;
	Buf body = { 0 }, out = { 0 };
	buf_string(&body, text);
	nout_fds = 0;
	build(&out, M_ERROR, ++bus->serial, m->sender, NULL, NULL, NULL, "s", &body, m->serial, name);
	send_raw(bus, out.d, out.len);
	free(body.d);
	free(out.d);
}

/* an answer to m: sig and body (NULL: none) */
static void answer_with(Bus *bus, const Msg *m, const char *sig, const Buf *body)
{
	if (!m->sender[0] || m->no_reply) {
		nout_fds = 0;
		return;
	}
	Buf out = { 0 };
	build(&out, M_RETURN, ++bus->serial, m->sender, NULL, NULL, NULL, sig, body, m->serial, NULL);
	send_raw(bus, out.d, out.len);
	free(out.d);
}

static bool serves(const Bus *bus, const char *path)
{
	for (int i = 0; i < bus->nserved; i++)
		if (strcmp(bus->served[i], path) == 0)
			return true;
	return false;
}

static void queue_signal(Bus *bus, Msg *m);

/* ---- properties of the program's own objects */

/* Copies one value of type *sig from src (as it was laid out there) onto
 * dst, laid out again for where it lands there: each value's alignment
 * is from the start of a message's body, so a value can't be copied as
 * bytes into the middle of another. */
static bool copy_value(In *r, Buf *dst, const char **sig, int depth)
{
	char t = **sig;
	if (depth > 64 || !t)
		return false;
	(*sig)++;
	switch (t) {
	case 'y': { unsigned char v = 0; in_take(r, &v, 1); buf_put(dst, &v, 1); return !r->bad; }
	case 'n': case 'q': { uint16_t v = 0; in_align(r, 2); in_take(r, &v, 2); buf_align(dst, 2); buf_put(dst, &v, 2); return !r->bad; }
	case 'b': case 'i': case 'u': case 'h': buf_u32(dst, in_u32(r)); return !r->bad;
	case 'x': case 't': case 'd': { uint64_t v = 0; in_align(r, 8); in_take(r, &v, 8); buf_align(dst, 8); buf_put(dst, &v, 8); return !r->bad; }
	case 's': case 'o': case 'g': {
		uint32_t n = 0;
		const char *text = in_text(r, t == 'g', &n);
		if (r->bad)
			return false;
		char *c = rt_alloc((size_t)n + 1);
		memcpy(c, text, n);
		c[n] = 0;
		if (t == 'g')
			buf_signature(dst, c);
		else
			buf_string(dst, c);
		free(c);
		return true;
	}
	case 'v': {
		uint32_t n = 0;
		const char *inner = in_text(r, true, &n);
		if (r->bad || n > 255)
			return false;
		char held[256];
		snprintf(held, sizeof held, "%.*s", (int)n, inner);
		buf_signature(dst, held);
		const char *p = held;
		return copy_value(r, dst, &p, depth + 1);
	}
	case '(': case '{': {
		in_align(r, 8);
		buf_align(dst, 8);
		char close = t == '(' ? ')' : '}';
		while (**sig && **sig != close)
			if (!copy_value(r, dst, sig, depth + 1))
				return false;
		if (**sig == close)
			(*sig)++;
		return !r->bad;
	}
	case 'a': {
		const char *elem = *sig;
		const char *after = type_end(elem);
		uint32_t n = in_u32(r);
		size_t a = align_of(*elem);
		in_align(r, a);
		size_t end = r->pos + n;
		if (r->bad || end > r->len)
			return false;
		buf_u32(dst, 0);
		size_t len_at = dst->len - 4;
		buf_align(dst, a);
		size_t start = dst->len;
		while (r->pos < end && !r->bad) {
			const char *p = elem;
			if (!copy_value(r, dst, &p, depth + 1))
				return false;
		}
		uint32_t got = (uint32_t)(dst->len - start);
		memcpy(dst->d + len_at, &got, 4);
		*sig = after;
		return !r->bad;
	}
	}
	return false;
}

static Prop *find_prop(Bus *bus, const char *path, const char *iface, const char *name)
{
	for (int i = 0; i < bus->nprops; i++) {
		Prop *p = &bus->props[i];
		if (strcmp(p->path, path) == 0 && strcmp(p->iface, iface) == 0 && strcmp(p->name, name) == 0)
			return p;
	}
	return NULL;
}

/* a property's value, as a variant (its signature, then the value) */
static void put_variant(Buf *dst, const Prop *p)
{
	buf_signature(dst, p->sig);
	In r = { p->body, p->len, 0, false, false, NULL, 0 };
	const char *sig = p->sig;
	copy_value(&r, dst, &sig, 0);
}

/* a{sv} of an interface's properties (each, or just one) */
static void put_props(Buf *dst, Bus *bus, const char *path, const char *iface, const Prop *only)
{
	buf_u32(dst, 0);
	size_t len_at = dst->len - 4;
	buf_align(dst, 8);
	size_t start = dst->len;
	for (int i = 0; i < bus->nprops; i++) {
		const Prop *p = &bus->props[i];
		if ((only && p != only) || strcmp(p->path, path) != 0 || strcmp(p->iface, iface) != 0)
			continue;
		buf_align(dst, 8);
		buf_string(dst, p->name);
		put_variant(dst, p);
	}
	uint32_t got = (uint32_t)(dst->len - start);
	memcpy(dst->d + len_at, &got, 4);
}

/* a property of one of the program's objects, set to the one value in a
 * message (dbus.value's): Get and GetAll answer with it from now on, and
 * those listening are told it changed (PropertiesChanged) */
void cby_dbus_set_property(int64_t id, const char *path, const char *iface, const char *name, int64_t msg)
{
	Bus *bus = bus_of(id);
	Out *o = out_of(msg);
	ensure_closed(o);
	const char *end = one_type(o->sig, 0);
	if (!o->sig[0] || !end || *end)
		rt_panic(NULL, 0, 0, "a property's value is one value (it has %s: \"%s\")",
			 o->sig[0] ? "more than one" : "none", o->sig);
	if (o->nfds)
		rt_panic(NULL, 0, 0, "a property can't be an open file");
	Prop *p = find_prop(bus, path, iface, name);
	bool same = p && strcmp(p->sig, o->sig) == 0 && p->len == o->body.len && memcmp(p->body, o->body.d, o->body.len) == 0;
	if (same)
		return;
	if (!p) {
		bus->props = rt_realloc(bus->props, sizeof *bus->props * (size_t)(bus->nprops + 1));
		p = &bus->props[bus->nprops++];
		p->path = dup_s(path);
		p->iface = dup_s(iface);
		p->name = dup_s(name);
		p->sig = NULL;
		p->body = NULL;
	}
	free(p->sig);
	free(p->body);
	p->sig = dup_s(o->sig);
	p->len = o->body.len;
	p->body = rt_alloc(p->len ? p->len : 1);
	memcpy(p->body, o->body.d, p->len);
	if (!serves(bus, path))
		return;
	/* "org.freedesktop.DBus.Properties", "PropertiesChanged": the interface,
	 * what changed, and what changed without saying to what (none) */
	Buf body = { 0 };
	buf_string(&body, iface);
	put_props(&body, bus, path, iface, p);
	buf_u32(&body, 0);
	Buf out = { 0 };
	nout_fds = 0;
	build(&out, M_SIGNAL, ++bus->serial, NULL, path, "org.freedesktop.DBus.Properties", "PropertiesChanged", "sa{sv}as",
	      &body, 0, NULL);
	send_raw(bus, out.d, out.len);
	free(out.d);
	free(body.d);
}

/* Get and GetAll, for a property the program has set; false: it isn't
 * one (so the program's call handler hears the call) */
static bool answer_property(Bus *bus, Msg *m)
{
	In r = { m->raw + m->body_at, m->body_len, 0, m->swap, false, NULL, 0 };
	uint32_t n = 0;
	const char *iface = in_text(&r, false, &n);
	if (r.bad)
		return false;
	char ifc[256];
	snprintf(ifc, sizeof ifc, "%.*s", (int)n, iface);
	if (strcmp(m->member, "GetAll") == 0 && strcmp(m->sig, "s") == 0) {
		Buf body = { 0 };
		put_props(&body, bus, m->path, ifc, NULL);
		answer_with(bus, m, "a{sv}", &body);
		free(body.d);
		return true;
	}
	if (strcmp(m->member, "Get") == 0 && strcmp(m->sig, "ss") == 0) {
		const char *name = in_text(&r, false, &n);
		if (r.bad)
			return false;
		char nm[256];
		snprintf(nm, sizeof nm, "%.*s", (int)n, name);
		Prop *p = find_prop(bus, m->path, ifc, nm);
		if (!p)
			return false;
		Buf body = { 0 };
		put_variant(&body, p);
		answer_with(bus, m, "v", &body);
		free(body.d);
		return true;
	}
	return false;
}

/* a method someone calls on us: Ping and Introspect are answered here; a
 * call to one of our objects waits for the event loop, and the program's
 * handler; anything else isn't there */
static bool in_tree(const Bus *bus, const char *path)
{
	size_t n = bus->tree_prefix ? strlen(bus->tree_prefix) : 0;
	return n && strncmp(path, bus->tree_prefix, n) == 0 && (path[n] == 0 || path[n] == '/');
}

static void incoming_call(Bus *bus, Msg *m)
{
	if (in_tree(bus, m->path) && bus->tree_fn) {
		/* (C's own: answered now, by its function) */
		int64_t args = read_body(m->raw + m->body_at, m->body_len, m->swap, m->sig, m->fds, m->nfds);
		if (!args) {
			answer_error(bus, m, "org.freedesktop.DBus.Error.InvalidArgs", "its values didn't match its signature");
			msg_free(m);
			return;
		}
		Msg *was = bus->current;
		int64_t was_args = bus->current_args;
		bool was_answered = bus->answered;
		bus->current = m;
		bus->current_args = args;
		bus->answered = false;
		bus->tree_fn(bus->tree_ctx, (int64_t)(intptr_t)bus, m->path, m->iface, m->member, args);
		if (!bus->answered)
			answer_error(bus, m, "org.freedesktop.DBus.Error.UnknownMethod", "there's no such method here");
		cby_json_release(args);
		msg_free(m);
		bus->current = was;
		bus->current_args = was_args;
		bus->answered = was_answered;
		return;
	}
	if (strcmp(m->iface, "org.freedesktop.DBus.Peer") == 0 && strcmp(m->member, "Ping") == 0) {
		answer_with(bus, m, NULL, NULL);
	} else if (strcmp(m->iface, "org.freedesktop.DBus.Introspectable") == 0 && strcmp(m->member, "Introspect") == 0) {
		const char *said = "";
		for (int i = 0; i < bus->nserved; i++)
			if (strcmp(bus->served[i], m->path) == 0)
				said = bus->described[i];
		const char *head = "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
				   "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n"
				   "  <interface name=\"org.freedesktop.DBus.Peer\"><method name=\"Ping\"/></interface>\n";
		char *xml = rt_alloc(strlen(head) + strlen(said) + 16);
		sprintf(xml, "%s%s</node>\n", head, said);
		Buf body = { 0 };
		buf_string(&body, xml);
		answer_with(bus, m, "s", &body);
		free(body.d);
		free(xml);
	} else if (serves(bus, m->path) && strcmp(m->iface, "org.freedesktop.DBus.Properties") == 0 &&
		   answer_property(bus, m)) {
		/* (answered: a property the program set) */
	} else if (serves(bus, m->path) && bus->call_fn) {
		queue_signal(bus, m);   /* (calls and signals wait in the same line, in order) */
		return;
	} else if (serves(bus, m->path) && strcmp(m->iface, "org.freedesktop.DBus.Properties") == 0) {
		answer_error(bus, m, strcmp(m->member, "Set") == 0 ? "org.freedesktop.DBus.Error.PropertyReadOnly"
								    : "org.freedesktop.DBus.Error.UnknownProperty",
			     strcmp(m->member, "Set") == 0 ? "its properties can't be set" : "it has no such property");
	} else {
		answer_error(bus, m, "org.freedesktop.DBus.Error.UnknownObject", "there's no object at that path");
	}
	msg_free(m);
}

static void wake_loop(Bus *bus);

/* sends, and waits for the answer to it (signals meanwhile are kept, for
 * the event loop); NULL: the bus went, or it took too long (*why) */
static Msg *call(Bus *bus, Buf *msg, uint32_t serial, const char **why)
{
	bool was = rt_gil_drop();
	Msg *answer = NULL;
	*why = "the bus closed the connection";
	if (send_raw(bus, msg->d, msg->len)) {
		for (;;) {
			struct pollfd p = { bus->fd, POLLIN, 0 };
			int wait = bus->wait_ms > 0 ? bus->wait_ms : CALL_WAIT_MS;
			int got = poll(&p, 1, wait);
			if (got < 0 && errno == EINTR)
				continue;
			if (got == 0) {
				static char said[64];
				snprintf(said, sizeof said, "no answer came (it waited %d seconds)", wait / 1000);
				*why = said;
				break;
			}
			Msg *m = receive(bus);
			if (!m)
				break;
			if ((m->type == M_RETURN || m->type == M_ERROR) && m->reply_to == serial) {
				answer = m;
				break;
			}
			if (m->type == M_SIGNAL)
				queue_signal(bus, m);
			else if (m->type == M_CALL)
				incoming_call(bus, m);
			else
				msg_free(m);
		}
	}
	rt_gil_take(was);
	if (bus->nqueue)
		wake_loop(bus);
	return answer;
}

/* (cby_dbus_try_session's: 0 instead of a panic) */
static _Thread_local bool connect_quietly;
#define GIVE_UP(...) do { if (connect_quietly) return 0; rt_panic(NULL, 0, 0, __VA_ARGS__); } while (0)

static int64_t bus_connect(const char *address, const char *which)
{
	if (!address || !*address)
		GIVE_UP("there's no %s bus to connect to (nothing says where it is: is D-Bus running?)",
			 which);
	char why[256] = "nothing at that address";
	int fd = connect_to(address, why, sizeof why);
	if (fd < 0)
		GIVE_UP("couldn't connect to the %s bus (%s): %s", which, address, why);
	/* signing in: the bus knows who we are from the socket; we say our uid */
	char uid[32], hex[80], line[512];
	snprintf(uid, sizeof uid, "%u", (unsigned)getuid());
	for (size_t i = 0; uid[i]; i++)
		snprintf(hex + 2 * i, 3, "%02x", (unsigned char)uid[i]);
	char auth[128];
	int n = snprintf(auth, sizeof auth, "AUTH EXTERNAL %s\r\n", hex);
	if (!write_all(fd, "", 1) || !write_all(fd, auth, (size_t)n) || !read_line(fd, line, sizeof line) ||
	    strncmp(line, "OK ", 3) != 0) {
		close(fd);
		GIVE_UP("the %s bus wouldn't let this program in%s%s", which, line[0] ? ": " : "",
			 line[0] ? line : "");
	}
	/* open files passed over it (h), if it can */
	bool fds_ok = write_all(fd, "NEGOTIATE_UNIX_FD\r\n", 19) && read_line(fd, line, sizeof line) &&
		      strncmp(line, "AGREE_UNIX_FD", 13) == 0;
	if (!write_all(fd, "BEGIN\r\n", 7)) {
		close(fd);
		GIVE_UP("the %s bus closed the connection", which);
	}
	Bus *bus = rt_alloc_zeroed(sizeof *bus);
	bus->fd = fd;
	bus->fds_ok = fds_ok;
	bus->watch = bus->wake_watch = -1;
	bus->wake[0] = bus->wake[1] = -1;
	bus->error = dup_s("");
	Buf out = { 0 };
	uint32_t serial = ++bus->serial;
	build(&out, M_CALL, serial, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello",
	      NULL, NULL, 0, NULL);
	const char *why2;
	Msg *m = call(bus, &out, serial, &why2);
	free(out.d);
	if (!m || m->type != M_RETURN) {
		msg_free(m);
		close(fd);
		free(bus->error);
		free(bus);
		GIVE_UP("the %s bus didn't say hello back", which);
	}
	In r = { m->raw + m->body_at, m->body_len, 0, m->swap, false, NULL, 0 };
	uint32_t nn = 0;
	const char *name = in_text(&r, false, &nn);
	bus->name = dup_s(r.bad ? "" : name);
	msg_free(m);
	return (int64_t)(intptr_t)bus;
}

int64_t cby_dbus_session(void)
{
	const char *a = getenv("DBUS_SESSION_BUS_ADDRESS");
	char fallback[300];
	if ((!a || !*a) && getenv("XDG_RUNTIME_DIR")) {
		snprintf(fallback, sizeof fallback, "unix:path=%s/bus", getenv("XDG_RUNTIME_DIR"));
		if (access(fallback + 10, F_OK) == 0)
			a = fallback;
	}
	if ((!a || !*a) && getenv("DBUS_LAUNCHD_SESSION_BUS_SOCKET")) {   /* macOS, D-Bus from Homebrew */
		snprintf(fallback, sizeof fallback, "unix:path=%s", getenv("DBUS_LAUNCHD_SESSION_BUS_SOCKET"));
		a = fallback;
	}
	return bus_connect(a, "session");
}

/* the session bus, or 0 if there's none (for libraries that do without:
 * desktop.notify) */
int64_t cby_dbus_try_session(void)
{
	connect_quietly = true;
	int64_t bus = cby_dbus_session();
	connect_quietly = false;
	return bus;
}

int64_t cby_dbus_system(void)
{
	const char *a = getenv("DBUS_SYSTEM_BUS_ADDRESS");
	if (!a || !*a)
		a = "unix:path=/var/run/dbus/system_bus_socket";
	return bus_connect(a, "system");
}

void cby_dbus_close(int64_t id)
{
	Bus *bus = bus_of(id);
	if (!bus)
		return;
	if (bus->watch >= 0)
		cby_events_unwatch(bus->watch);
	if (bus->wake_watch >= 0) {
		cby_events_unwatch(bus->wake_watch);
		close(bus->wake[0]);
		close(bus->wake[1]);
	}
	close(bus->fd);
	for (int i = 0; i < bus->nqueue; i++)
		msg_free(bus->queue[i]);
	free(bus->queue);
	for (int i = 0; i < bus->nsubs; i++) {
		free(bus->subs[i].iface);
		free(bus->subs[i].member);
	}
	free(bus->subs);
	for (int i = 0; i < bus->nserved; i++) {
		free(bus->served[i]);
		free(bus->described[i]);
	}
	free(bus->served);
	free(bus->described);
	for (int i = 0; i < bus->ninbox; i++)
		close(bus->inbox[i]);
	free(bus->inbox);
	for (int i = 0; i < bus->nprops; i++) {
		free(bus->props[i].path);
		free(bus->props[i].iface);
		free(bus->props[i].name);
		free(bus->props[i].sig);
		free(bus->props[i].body);
	}
	free(bus->props);
	free(bus->tree_prefix);
	if (bus->signal_args)
		cby_json_release(bus->signal_args);
	msg_free(bus->signal);
	free(bus->name);
	free(bus->error);
	free(bus);
}

/* ------------------------------------------------------------ calls */

static void set_error(Bus *bus, const char *s)
{
	free(bus->error);
	bus->error = dup_s(s);
}

/* the answer's values (a json list), or 0 with bus->error said */
static int64_t send_call(Bus *bus, Out *o)
{
	if (o->type != M_CALL)
		rt_panic(NULL, 0, 0, "'call' sends a method call, made with dbus.method_call (%s)",
			 o->type == M_SIGNAL ? "a signal is sent with 'send'" : "an answer is given with 'reply'");
	ensure_closed(o);
	Buf out = { 0 };
	uint32_t serial = ++bus->serial;
	out_fds = o->fds;
	nout_fds = o->nfds;
	build(&out, M_CALL, serial, o->dest, o->path, o->iface, o->member, o->sig, &o->body, 0, NULL);
	const char *why;
	Msg *m = call(bus, &out, serial, &why);
	free(out.d);
	if (!m) {
		set_error(bus, why);
		return 0;
	}
	if (m->type == M_ERROR) {
		/* "org.freedesktop.DBus.Error.ServiceUnknown: The name ... was not provided" */
		char text[1024];
		In r = { m->raw + m->body_at, m->body_len, 0, m->swap, false, NULL, 0 };
		uint32_t n = 0;
		const char *said = m->sig[0] == 's' ? in_text(&r, false, &n) : "";
		snprintf(text, sizeof text, "%s%s%.*s", m->error, n ? ": " : "", (int)n, r.bad ? "" : said);
		set_error(bus, text);
		msg_free(m);
		return 0;
	}
	int64_t list = read_body(m->raw + m->body_at, m->body_len, m->swap, m->sig, m->fds, m->nfds);
	msg_free(m);
	if (!list) {
		set_error(bus, "the answer couldn't be read (it didn't match its own signature)");
		return 0;
	}
	set_error(bus, "");
	return list;
}

int64_t cby_dbus_call(int64_t id, int64_t msg)
{
	Bus *bus = bus_of(id);
	Out *o = out_of(msg);
	int64_t list = send_call(bus, o);
	if (!list)
		rt_panic(NULL, 0, 0, "the D-Bus call %s.%s (to %s) didn't work: %s", o->iface, o->member,
			 o->dest[0] ? o->dest : "no one", bus->error);
	return list;
}

int64_t cby_dbus_try_call(int64_t id, int64_t msg)
{
	int64_t list = id ? send_call(bus_of(id), out_of(msg)) : 0;   /* (0: dbus.try_session found none) */
	return list ? list : cby_json_new(J_LIST);
}

/* ------------------------------------------------------------ signals */

static void dispatch(Bus *bus);
static void readable(void *data);
static void woken(void *data);

/* the event loop watches the bus (and a pipe, to be woken when something
 * came while a call waited) */
static void watch_bus(Bus *bus)
{
	if (bus->watch < 0)
		bus->watch = cby_events_watch(bus->fd, readable, bus);
	if (bus->wake_watch < 0 && pipe(bus->wake) == 0) {
		fcntl(bus->wake[0], F_SETFL, fcntl(bus->wake[0], F_GETFL) | O_NONBLOCK);
		fcntl(bus->wake[1], F_SETFL, fcntl(bus->wake[1], F_GETFL) | O_NONBLOCK);
		bus->wake_watch = cby_events_watch(bus->wake[0], woken, bus);
	}
	if (bus->nqueue)
		wake_loop(bus);
}

static void readable(void *data)
{
	Bus *bus = data;
	for (;;) {
		struct pollfd p = { bus->fd, POLLIN, 0 };
		if (poll(&p, 1, 0) <= 0)
			break;
		if (p.revents & (POLLHUP | POLLERR) && !(p.revents & POLLIN)) {
			cby_events_unwatch(bus->watch);
			bus->watch = -1;
			break;
		}
		Msg *m = receive(bus);
		if (!m) {   /* the bus went away: nothing more will come */
			cby_events_unwatch(bus->watch);
			bus->watch = -1;
			break;
		}
		if (m->type == M_SIGNAL)
			queue_signal(bus, m);
		else if (m->type == M_CALL)
			incoming_call(bus, m);
		else
			msg_free(m);
	}
	dispatch(bus);
}

static void woken(void *data)
{
	Bus *bus = data;
	char b[64];
	while (read(bus->wake[0], b, sizeof b) > 0) {
	}
	dispatch(bus);
}

static void wake_loop(Bus *bus)
{
	if (bus->wake[1] >= 0 && write(bus->wake[1], "s", 1) < 0) {
		/* (full: it's waking already) */
	}
}

static bool matches(const Sub *s, const Msg *m)
{
	return (!s->iface[0] || strcmp(s->iface, m->iface) == 0) && (!s->member[0] || strcmp(s->member, m->member) == 0);
}

/* a call, to the program's handler; unanswered, it's answered with
 * nothing (a method that gives nothing back) */
static void handle_call(Bus *bus, Msg *m, int64_t args)
{
	Msg *was = bus->current;
	int64_t was_args = bus->current_args;
	bool was_answered = bus->answered;
	bus->current = m;
	bus->current_args = args;
	bus->answered = false;
	rt_invoke_hook(bus->call_obj, bus->call_fn);
	if (!bus->answered)
		answer_with(bus, m, NULL, NULL);
	cby_json_release(args);
	msg_free(m);
	bus->current = was;
	bus->current_args = was_args;
	bus->answered = was_answered;
}

/* each signal kept, to the handlers that want it, in order (and calls,
 * to the call handler) */
static void dispatch(Bus *bus)
{
	if (bus->dispatching)
		return;   /* (a handler that makes a call: what comes meanwhile waits for this loop) */
	bus->dispatching = true;
	while (bus->nqueue) {
		Msg *m = bus->queue[0];
		memmove(bus->queue, bus->queue + 1, sizeof *bus->queue * (size_t)(bus->nqueue - 1));
		bus->nqueue--;
		int64_t args = read_body(m->raw + m->body_at, m->body_len, m->swap, m->sig, m->fds, m->nfds);
		if (!args) {
			if (m->type == M_CALL)
				answer_error(bus, m, "org.freedesktop.DBus.Error.InvalidArgs", "its values didn't match its signature");
			msg_free(m);
			continue;
		}
		if (m->type == M_CALL) {
			handle_call(bus, m, args);
			continue;
		}
		msg_free(bus->signal);
		if (bus->signal_args)
			cby_json_release(bus->signal_args);
		bus->signal = m;
		bus->signal_args = args;
		bool wanted = false;
		for (int i = 0; i < bus->nsubs && !wanted; i++)
			wanted = matches(&bus->subs[i], m);
		if (wanted && bus->fn)
			rt_invoke_hook(bus->obj, bus->fn);
	}
	bus->dispatching = false;
}

/* who's told: the bus object's say_signal */
void cby_dbus_on_signal(int64_t id, void *obj, void *fn)
{
	Bus *bus = bus_of(id);
	bus->obj = obj;
	bus->fn = fn;
	if (bus->nqueue)
		wake_loop(bus);
}

void cby_dbus_listen(int64_t id, const char *iface, const char *member)
{
	Bus *bus = bus_of(id);
	/* asking the bus for them: a match rule */
	char rule[600];
	int n = snprintf(rule, sizeof rule, "type='signal'");
	if (iface[0])
		n += snprintf(rule + n, sizeof rule - (size_t)n, ",interface='%s'", iface);
	if (member[0])
		snprintf(rule + n, sizeof rule - (size_t)n, ",member='%s'", member);
	int64_t msg = cby_dbus_message_new("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
					   "AddMatch");
	cby_dbus_add_str(msg, rule);
	int64_t answer = send_call(bus, out_of(msg));
	cby_dbus_message_free(msg);
	if (!answer)
		rt_panic(NULL, 0, 0, "the bus wouldn't send the signals %s%s%s: %s", iface, iface[0] && member[0] ? "." : "",
			 member, bus->error);
	cby_json_release(answer);
	bus->subs = rt_realloc(bus->subs, sizeof *bus->subs * (size_t)(bus->nsubs + 1));
	bus->subs[bus->nsubs++] = (Sub){ dup_s(iface), dup_s(member) };
	watch_bus(bus);
}

/* ------------------------------------------------------------ objects of our own */

/* calls to the object at path go to the call handler */
void cby_dbus_serve(int64_t id, const char *path)
{
	if (path[0] != '/')
		rt_panic(NULL, 0, 0, "a D-Bus object's path starts with '/' (\"/org/example/Thing\"), not \"%s\"", path);
	Bus *bus = bus_of(id);
	if (serves(bus, path))
		return;
	bus->served = rt_realloc(bus->served, sizeof *bus->served * (size_t)(bus->nserved + 1));
	bus->described = rt_realloc(bus->described, sizeof *bus->described * (size_t)(bus->nserved + 1));
	bus->described[bus->nserved] = dup_s("");
	bus->served[bus->nserved++] = dup_s(path);
	watch_bus(bus);
}

/* what the object at path has, for tools that ask (Introspect): its
 * <interface> elements, in D-Bus's XML */
void cby_dbus_describe(int64_t id, const char *path, const char *xml)
{
	Bus *bus = bus_of(id);
	cby_dbus_serve(id, path);
	for (int i = 0; i < bus->nserved; i++)
		if (strcmp(bus->served[i], path) == 0) {
			free(bus->described[i]);
			bus->described[i] = dup_s(xml);
		}
}

void cby_dbus_on_call(int64_t id, void *obj, void *fn)
{
	Bus *bus = bus_of(id);
	bus->call_obj = obj;
	bus->call_fn = fn;
	watch_bus(bus);
}

/* a well-known name ("org.example.Thing") for this connection: 1 if it's
 * ours now, 0 if someone else has it */
int cby_dbus_own_name(int64_t id, const char *name)
{
	Bus *bus = bus_of(id);
	int64_t msg = cby_dbus_message_new("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
					   "RequestName");
	cby_dbus_add_str(msg, name);
	cby_dbus_add_uint(msg, 4);   /* DO_NOT_QUEUE: ours now, or not at all */
	int64_t answer = send_call(bus, out_of(msg));
	cby_dbus_message_free(msg);
	if (!answer)
		rt_panic(NULL, 0, 0, "the bus wouldn't give this program the name \"%s\": %s", name, bus->error);
	extern int64_t cby_json_at(int64_t h, int64_t i);
	extern int64_t cby_json_long(int64_t h);
	int64_t first = cby_json_at(answer, 0);
	int64_t got = cby_json_long(first);
	cby_json_release(first);
	cby_json_release(answer);
	return got == 1 || got == 4;   /* the owner now, or already */
}

/* the call being handled: panics if there's none */
static Msg *being_handled(Bus *bus, const char *what)
{
	if (!bus->current)
		rt_panic(NULL, 0, 0, "'%s' is for answering a call, in the on_call handler", what);
	if (bus->answered)
		rt_panic(NULL, 0, 0, "this call has been answered already: a call gets one answer");
	return bus->current;
}

void cby_dbus_reply(int64_t id, int64_t msg)
{
	Bus *bus = bus_of(id);
	Out *o = out_of(msg);
	Msg *m = being_handled(bus, "reply");
	if (o->type != M_RETURN)
		rt_panic(NULL, 0, 0, "'reply' sends an answer, made with dbus.answer, not a call or a signal");
	ensure_closed(o);
	out_fds = o->fds;
	nout_fds = o->nfds;
	answer_with(bus, m, o->sig, &o->body);
	nout_fds = 0;
	bus->answered = true;
}

void cby_dbus_fail(int64_t id, const char *name, const char *text)
{
	Bus *bus = bus_of(id);
	Msg *m = being_handled(bus, "fail");
	if (!strchr(name, '.'))
		rt_panic(NULL, 0, 0, "a D-Bus error's name has dots in it (\"org.example.Error.NotFound\"), not \"%s\"", name);
	answer_error(bus, m, name, text);
	bus->answered = true;
}

/* a signal, sent at once (no answer comes) */
void cby_dbus_send(int64_t id, int64_t msg)
{
	Bus *bus = bus_of(id);
	Out *o = out_of(msg);
	if (o->type != M_SIGNAL)
		rt_panic(NULL, 0, 0, "'send' sends a signal, made with dbus.signal_message (a call is sent with 'call')");
	ensure_closed(o);
	Buf out = { 0 };
	out_fds = o->fds;
	nout_fds = o->nfds;
	build(&out, M_SIGNAL, ++bus->serial, NULL, o->path, o->iface, o->member, o->sig, &o->body, 0, NULL);
	bool sent = send_raw(bus, out.d, out.len);
	free(out.d);
	if (!sent)
		rt_panic(NULL, 0, 0, "the signal %s.%s couldn't be sent: the bus closed the connection", o->iface, o->member);
}

/* the call being handled: its values (a json list, held for the caller) */
int64_t cby_dbus_call_args(int64_t id)
{
	Bus *bus = bus_of(id);
	extern void cby_json_hold(int64_t h);
	if (!bus->current_args)
		return cby_json_new(J_LIST);
	cby_json_hold(bus->current_args);
	return bus->current_args;
}

/* the signal being handled: its values (a json list, held for the caller) */
int64_t cby_dbus_signal_args(int64_t id)
{
	Bus *bus = bus_of(id);
	if (!bus->signal_args)
		return cby_json_new(J_LIST);
	extern void cby_json_hold(int64_t h);
	cby_json_hold(bus->signal_args);
	return bus->signal_args;
}

/* ------------------------------------------------------------ text back to Cabby */

static _Thread_local char *text_out;

static void text_ends(void) { free(text_out); text_out = NULL; }

/* 0 the unique name, 1 the last error, 2 the signal's sender, 3 its path,
 * 4 its interface, 5 its name (member), 6 a message's signature so far;
 * 7 to 10 the call being handled's sender, path, interface and method */
int64_t cby_dbus_text(int64_t id, int64_t what)
{
	Bus *bus = what == 6 ? NULL : bus_of(id);
	const Msg *s = bus ? bus->signal : NULL;
	const char *t = "";
	if (!bus && what != 6)
		t = what == 1 ? "no session bus" : "";
	else switch (what) {
	case 0: t = bus->name; break;
	case 1: t = bus->error; break;
	case 2: t = s ? s->sender : ""; break;
	case 3: t = s ? s->path : ""; break;
	case 4: t = s ? s->iface : ""; break;
	case 5: t = s ? s->member : ""; break;
	case 6: t = out_of(id)->sig; break;
	case 7: t = bus->current ? bus->current->sender : ""; break;
	case 8: t = bus->current ? bus->current->path : ""; break;
	case 9: t = bus->current ? bus->current->iface : ""; break;
	case 10: t = bus->current ? bus->current->member : ""; break;
	}
	static _Thread_local bool hooked;
	if (!hooked) {
		hooked = true;
		rt_on_thread_end(text_ends);
	}
	free(text_out);
	text_out = dup_s(t);
	return (int64_t)strlen(t);
}

void cby_dbus_take_text(char *dst)
{
	if (text_out)
		memcpy(dst, text_out, strlen(text_out));
}

/* ------------------------------------------------------------ for C: a tree of objects
 *
 * The gui library's accessibility: a bus at an address (the accessibility
 * bus's), or 0 if it can't be reached; and every object under a path,
 * answered by a C function, which replies with cby_dbus_reply (or
 * cby_dbus_fail). */

int64_t cby_dbus_try_address(const char *address)
{
	connect_quietly = true;
	int64_t bus = bus_connect(address, "accessibility");
	connect_quietly = false;
	return bus;
}

void cby_dbus_serve_tree(int64_t id, const char *prefix,
			 void (*fn)(void *ctx, int64_t bus, const char *path, const char *iface, const char *member, int64_t args),
			 void *ctx)
{
	Bus *bus = bus_of(id);
	free(bus->tree_prefix);
	bus->tree_prefix = dup_s(prefix);
	bus->tree_fn = fn;
	bus->tree_ctx = ctx;
	watch_bus(bus);
}

/* how long this bus's calls wait for their answer, in milliseconds (0: 25
 * seconds, as libdbus): Bluetooth's pairing waits for a person */
void cby_dbus_set_wait(int64_t id, int64_t ms)
{
	bus_of(id)->wait_ms = ms > 0 ? (int)ms : 0;
}
