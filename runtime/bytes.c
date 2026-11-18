/* bytes.c — the C side of the bytes library (lib/bytes.hby). Part of
 * Cabby's runtime, so the interpreter and compiled programs both have it.
 *
 * A bytes value is a run of bytes, each 0 to 255, that grows as bytes are
 * added. Cabby's `bytes` object holds one (as a number) and frees it in
 * its destructor; a part (`.part`) is a copy, so every bytes value is its
 * own.
 *
 * Numbers inside bytes (2, 4 or 8 bytes in a row) are read and written in
 * either byte order: smallest byte first ("little-endian", what PCs use and
 * most file formats too) or biggest byte first ("big-endian": PNG, and
 * what networks send). */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	uint8_t *data;
	int64_t len, cap;
} Bytes;

static Bytes *B(int64_t h) { return (Bytes *)(intptr_t)h; }

static void room(Bytes *b, int64_t more)
{
	if (b->len + more <= b->cap)
		return;
	int64_t cap = b->cap ? b->cap * 2 : 64;
	while (cap < b->len + more)
		cap *= 2;
	b->data = rt_realloc(b->data, (size_t)cap);
	b->cap = cap;
}

int64_t rt_bytes_make(const void *data, int64_t len)
{
	Bytes *b = rt_alloc_zeroed(sizeof *b);
	if (len > 0) {
		room(b, len);
		memcpy(b->data, data, (size_t)len);
		b->len = len;
	}
	return (int64_t)(intptr_t)b;
}

const uint8_t *rt_bytes_data(int64_t h, int64_t *len)
{
	*len = B(h)->len;
	return B(h)->data;
}

int64_t cby_bytes_new(void) { return rt_bytes_make(NULL, 0); }

void cby_bytes_release(int64_t h)
{
	Bytes *b = B(h);
	if (!b)
		return;
	free(b->data);
	free(b);
}

int64_t cby_bytes_length(int64_t h) { return B(h)->len; }

static void check_index(const Bytes *b, int64_t i, const char *doing)
{
	if (i >= 0 && i < b->len)
		return;
	if (b->len == 0)
		rt_panic(NULL, 0, 0, "there's no byte [%lld] %s: there are no bytes (add some with .add)", (long long)i, doing);
	rt_panic(NULL, 0, 0, "there's no byte [%lld] %s: there are %lld (from [0] to [%lld])", (long long)i, doing,
		 (long long)b->len, (long long)b->len - 1);
}

static void check_byte(int64_t v)
{
	if (v < 0 || v > 255)
		rt_panic(NULL, 0, 0, "a byte is a number from 0 to 255, not %lld", (long long)v);
}

int64_t cby_bytes_get(int64_t h, int64_t i)
{
	check_index(B(h), i, "to read");
	return B(h)->data[i];
}

void cby_bytes_set(int64_t h, int64_t i, int64_t v)
{
	check_index(B(h), i, "to change");
	check_byte(v);
	B(h)->data[i] = (uint8_t)v;
}

void cby_bytes_add(int64_t h, int64_t v)
{
	check_byte(v);
	Bytes *b = B(h);
	room(b, 1);
	b->data[b->len++] = (uint8_t)v;
}

void cby_bytes_add_bytes(int64_t h, int64_t other)
{
	Bytes *b = B(h), *o = B(other);
	int64_t n = o->len;   /* (o may be b: adding bytes to themselves) */
	room(b, n);
	memmove(b->data + b->len, B(other)->data, (size_t)n);
	b->len += n;
}

/* the panic for a stretch of bytes that isn't all there */
static void check_stretch(const Bytes *b, int64_t start, int64_t count, const char *what)
{
	if (count < 0)
		rt_panic(NULL, 0, 0, "a part can't be %lld bytes long", (long long)count);
	if (start < 0 || start > b->len || count > b->len - start) {
		if (!what)
			rt_panic(NULL, 0, 0, "the part (%lld bytes from byte [%lld]) goes past the end: there are %lld bytes",
				 (long long)count, (long long)start, (long long)b->len);
		rt_panic(NULL, 0, 0, "%s at byte [%lld] goes past the end: there are %lld bytes", what, (long long)start,
			 (long long)b->len);
	}
}

int64_t cby_bytes_part(int64_t h, int64_t start, int64_t count)
{
	check_stretch(B(h), start, count, NULL);
	return rt_bytes_make(B(h)->data + start, count);
}

int64_t cby_bytes_copy(int64_t h) { return rt_bytes_make(B(h)->data, B(h)->len); }

int64_t cby_bytes_from_text(const char *s, int64_t len) { return rt_bytes_make(s, len); }

/* "89 50 4e 47": two hexadecimal digits a byte; spaces and line ends
 * between bytes are passed over */
int64_t cby_bytes_from_hex(const char *s)
{
	Bytes *b = B(rt_bytes_make(NULL, 0));
	int high = -1;
	for (const char *p = s; *p; p++) {
		int c = (unsigned char)*p, v;
		if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
			if (high >= 0) {
				free(b->data);
				free(b);
				rt_panic(NULL, 0, 0, "this isn't hexadecimal bytes: a byte is two digits, and '%c' has one "
					 "(write 0%c)", p[-1], p[-1]);
			}
			continue;
		}
		if (c >= '0' && c <= '9')
			v = c - '0';
		else if (c >= 'a' && c <= 'f')
			v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			v = c - 'A' + 10;
		else {
			free(b->data);
			free(b);
			rt_panic(NULL, 0, 0, "this isn't hexadecimal bytes: '%c' isn't a digit (0 to 9, and a to f)", c);
		}
		if (high < 0) {
			high = v;
		} else {
			room(b, 1);
			b->data[b->len++] = (uint8_t)(high * 16 + v);
			high = -1;
		}
	}
	if (high >= 0) {
		free(b->data);
		free(b);
		rt_panic(NULL, 0, 0, "this isn't hexadecimal bytes: the last byte has only one digit");
	}
	return (int64_t)(intptr_t)b;
}

int cby_bytes_same(int64_t a, int64_t b)
{
	return B(a)->len == B(b)->len && (B(a)->len == 0 || memcmp(B(a)->data, B(b)->data, (size_t)B(a)->len) == 0);
}

int cby_bytes_starts_with(int64_t a, int64_t b)
{
	return B(b)->len <= B(a)->len && (B(b)->len == 0 || memcmp(B(a)->data, B(b)->data, (size_t)B(b)->len) == 0);
}

/* the first place from `from` where `b`'s bytes are, or -1 */
int64_t cby_bytes_find(int64_t a, int64_t b, int64_t from)
{
	const Bytes *x = B(a), *y = B(b);
	if (from < 0)
		from = 0;
	for (int64_t i = from; i + y->len <= x->len; i++)
		if (y->len == 0 || memcmp(x->data + i, y->data, (size_t)y->len) == 0)
			return i;
	return -1;
}

/* ---- numbers inside: 2, 4 or 8 bytes, in either order */

static const char *size_word(int64_t size)
{
	return size == 2 ? "a short (2 bytes)" : size == 4 ? "an int (4 bytes)" : "a long (8 bytes)";
}

static uint64_t get_raw(const Bytes *b, int64_t at, int64_t size, int big)
{
	uint64_t v = 0;
	for (int64_t i = 0; i < size; i++) {
		uint64_t byte = b->data[at + (big ? i : size - 1 - i)];
		v = v << 8 | byte;
	}
	return v;
}

static void put_raw(uint8_t *p, int64_t size, int big, uint64_t v)
{
	for (int64_t i = 0; i < size; i++) {
		uint8_t byte = (uint8_t)(v >> (8 * i));
		p[big ? size - 1 - i : i] = byte;
	}
}

/* signed: the number as C's short, int or long would hold it; unsigned:
 * the same bytes counted from 0 up (65535, not -1) */
int64_t cby_bytes_number_at(int64_t h, int64_t at, int64_t size, int big, int is_unsigned)
{
	check_stretch(B(h), at, size, size_word(size));
	uint64_t v = get_raw(B(h), at, size, big);
	if (is_unsigned || size == 8)
		return (int64_t)v;
	if (size == 2)
		return (int16_t)(uint16_t)v;
	return (int32_t)(uint32_t)v;
}

double cby_bytes_double_at(int64_t h, int64_t at, int big)
{
	check_stretch(B(h), at, 8, "a double (8 bytes)");
	uint64_t v = get_raw(B(h), at, 8, big);
	double d;
	memcpy(&d, &v, sizeof d);
	return d;
}

/* where the number goes: `at`, or the end when at is -1 (adding) */
static uint8_t *place(Bytes *b, int64_t at, int64_t size, const char *what)
{
	if (at == -1) {
		room(b, size);
		b->len += size;
		return b->data + b->len - size;
	}
	check_stretch(b, at, size, what);
	return b->data + at;
}

/* A number that doesn't fit is a panic. Both a signed and an unsigned
 * range are fine (a short holds -32768 to 65535), since formats store
 * either. */
void cby_bytes_put_number(int64_t h, int64_t at, int64_t size, int big, int64_t v)
{
	if (size < 8) {
		int64_t lo = -((int64_t)1 << (size * 8 - 1)), hi = ((int64_t)1 << (size * 8)) - 1;
		if (v < lo || v > hi)
			rt_panic(NULL, 0, 0, "%lld doesn't fit in %s: it holds %lld to %lld", (long long)v, size_word(size),
				 (long long)lo, (long long)hi);
	}
	put_raw(place(B(h), at, size, size_word(size)), size, big, (uint64_t)v);
}

void cby_bytes_put_double(int64_t h, int64_t at, int big, double d)
{
	uint64_t v;
	memcpy(&v, &d, sizeof v);
	put_raw(place(B(h), at, 8, "a double (8 bytes)"), 8, big, v);
}

/* ---- as text */

static _Thread_local char *out;
static _Thread_local size_t out_len, out_cap;

static void out_room(size_t more)
{
	if (out_len + more <= out_cap)
		return;
	size_t cap = out_cap ? out_cap * 2 : 256;
	while (cap < out_len + more)
		cap *= 2;
	out = rt_realloc(out, cap);
	out_cap = cap;
}

static void out_put(const char *s, size_t n)
{
	out_room(n);
	memcpy(out + out_len, s, n);
	out_len += n;
}

/* what: 0 hex on one line ("89 50 4e 47"), 1 the bytes as they are (a str),
 * 2 a hex dump: 16 bytes a line, with where each line starts and the bytes
 * that are letters */
int64_t cby_bytes_text(int64_t h, int64_t what)
{
	const Bytes *b = B(h);
	out_len = 0;
	if (what == 1) {
		out_put((const char *)b->data, (size_t)b->len);
	} else if (what == 0) {
		char hex[4];
		for (int64_t i = 0; i < b->len; i++) {
			snprintf(hex, sizeof hex, i ? " %02x" : "%02x", b->data[i]);
			out_put(hex, strlen(hex));
		}
	} else {
		char line[128];
		for (int64_t at = 0; at < b->len; at += 16) {
			int n = snprintf(line, sizeof line, "%08llx  ", (unsigned long long)at);
			for (int64_t i = 0; i < 16; i++) {
				if (at + i < b->len)
					n += snprintf(line + n, sizeof line - (size_t)n, "%02x ", b->data[at + i]);
				else
					n += snprintf(line + n, sizeof line - (size_t)n, "   ");
				if (i == 7)
					line[n++] = ' ';
			}
			line[n++] = ' ';
			line[n++] = '|';
			for (int64_t i = 0; i < 16 && at + i < b->len; i++) {
				uint8_t c = b->data[at + i];
				line[n++] = c >= 32 && c < 127 ? (char)c : '.';
			}
			line[n++] = '|';
			line[n++] = '\n';
			out_put(line, (size_t)n);
		}
		if (out_len)
			out_len--;   /* (no line end after the last line: println adds one) */
	}
	return (int64_t)out_len;
}

void cby_bytes_take_text(char *dst)
{
	if (out_len)
		memcpy(dst, out, out_len);
	free(out);
	out = NULL;
	out_len = out_cap = 0;
}

/* a worker's thread, ending: what it kept for itself */
static void bytes_thread_end(void) { free(out); out = NULL; out_len = out_cap = 0; }
__attribute__((constructor)) static void bytes_threads(void) { rt_on_thread_end(bytes_thread_end); }
/* and the main thread's, at the program's end */
__attribute__((destructor)) static void bytes_main_end(void) { bytes_thread_end(); }
