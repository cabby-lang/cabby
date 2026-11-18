/* json.c — the C side of the json library (lib/json.hby). Part of Cabby's
 * runtime, so the interpreter and compiled programs both have it.
 *
 * A JSON value is a tree of nodes. Cabby's `json` object holds one node
 * (as a number); nodes are counted, so a value taken out of another
 * (J["days"]) shares its node with the whole, and each node is freed when
 * nothing holds it any more. Each node knows where it is in its parent,
 * so a panic can say which value it means ("Weather.days[0].high").
 *
 * A name that isn't there, or an item past the end, gives a "missing"
 * node: using it is a panic that says what was missing, and the _or
 * methods give their default instead. */
#include "rt.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { J_NULL, J_BOOL, J_NUMBER, J_STR, J_LIST, J_LOOKUP, J_MISSING };

typedef struct Node Node;
struct Node {
	int kind;
	int64_t refs;
	bool b;
	double num;
	char *text;            /* J_STR: the text; J_MISSING: the name or index that wasn't there */
	size_t len;
	Node **items;          /* J_LIST and J_LOOKUP: the values, in order */
	char **keys;           /* J_LOOKUP: their names */
	int64_t n, cap;
	Node *parent;          /* where it is, for messages (not held) */
	char *where;           /* its name or [index] in the parent */
};

static const char *kind_word(int k)
{
	static const char *const w[] = { "null", "true or false", "a number", "text", "a list", "a lookup", "missing" };
	return w[k];
}

static Node *node_new(int kind)
{
	Node *n = rt_alloc_zeroed(sizeof *n);
	n->kind = kind;
	n->refs = 1;
	return n;
}

static void node_release(Node *n)
{
	if (!n || --n->refs > 0)
		return;
	for (int64_t i = 0; i < n->n; i++) {
		if (n->items[i]->parent == n)
			n->items[i]->parent = NULL;
		node_release(n->items[i]);
		if (n->keys)
			free(n->keys[i]);
	}
	free(n->items);
	free(n->keys);
	free(n->text);
	free(n->where);
	if (n->kind == J_MISSING && n->parent)
		node_release(n->parent);   /* a missing node holds its parent, for its message */
	free(n);
}

static char *dup_n(const char *s, size_t len)
{
	char *p = rt_alloc(len + 1);
	memcpy(p, s, len);
	p[len] = '\0';
	return p;
}

/* "days[0].high": where a node is, from the top */
static void path_of(const Node *n, char *buf, size_t size)
{
	const Node *chain[64];
	int depth = 0;
	for (const Node *p = n; p && p->where && depth < 64; p = p->parent)
		chain[depth++] = p;
	buf[0] = '\0';
	size_t used = 0;
	for (int i = depth - 1; i >= 0 && used + 1 < size; i--) {
		const char *w = chain[i]->where;
		int k = snprintf(buf + used, size - used, "%s%s", (w[0] == '[' || used == 0) ? "" : ".", w);
		if (k < 0)
			break;
		used += (size_t)k;
	}
	if (!buf[0]) {
		snprintf(buf, size, "the JSON");
	} else if (used + 3 < size) {
		/* 'days[0].high', quoted so it reads as a name */
		memmove(buf + 1, buf, used + 1);
		buf[0] = '\'';
		buf[used + 1] = '\'';
		buf[used + 2] = '\0';
	}
}

static void set_place(Node *child, Node *parent, const char *where)
{
	child->parent = parent;
	free(child->where);
	child->where = dup_n(where, strlen(where));
}

static void add_item(Node *list, Node *child, const char *key)
{
	if (list->n == list->cap) {
		list->cap = list->cap ? list->cap * 2 : 4;
		list->items = rt_realloc(list->items, sizeof *list->items * (size_t)list->cap);
		if (list->kind == J_LOOKUP)
			list->keys = rt_realloc(list->keys, sizeof *list->keys * (size_t)list->cap);
	}
	char where[64];
	if (key)
		list->keys[list->n] = dup_n(key, strlen(key));
	else
		snprintf(where, sizeof where, "[%lld]", (long long)list->n);
	set_place(child, list, key ? key : where);
	list->items[list->n++] = child;
}

/* ---- reading JSON text */

typedef struct {
	const char *s, *start;
	int line;
	const char *line_start;
	char why[160];
} Reader;

static bool fail(Reader *r, const char *what)
{
	if (!r->why[0])
		snprintf(r->why, sizeof r->why, "line %d, column %d: %s", r->line, (int)(r->s - r->line_start) + 1, what);
	return false;
}

static void space(Reader *r)
{
	while (*r->s == ' ' || *r->s == '\t' || *r->s == '\n' || *r->s == '\r') {
		if (*r->s == '\n') {
			r->line++;
			r->line_start = r->s + 1;
		}
		r->s++;
	}
}

static void put_utf8(char **buf, size_t *len, size_t *cap, uint32_t c);

static void grow_put(char **buf, size_t *len, size_t *cap, char c)
{
	if (*len + 1 >= *cap) {
		*cap = *cap ? *cap * 2 : 32;
		*buf = rt_realloc(*buf, *cap);
	}
	(*buf)[(*len)++] = c;
}

static void put_utf8(char **buf, size_t *len, size_t *cap, uint32_t c)
{
	if (c < 0x80) {
		grow_put(buf, len, cap, (char)c);
	} else if (c < 0x800) {
		grow_put(buf, len, cap, (char)(0xC0 | (c >> 6)));
		grow_put(buf, len, cap, (char)(0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		grow_put(buf, len, cap, (char)(0xE0 | (c >> 12)));
		grow_put(buf, len, cap, (char)(0x80 | ((c >> 6) & 0x3F)));
		grow_put(buf, len, cap, (char)(0x80 | (c & 0x3F)));
	} else {
		grow_put(buf, len, cap, (char)(0xF0 | (c >> 18)));
		grow_put(buf, len, cap, (char)(0x80 | ((c >> 12) & 0x3F)));
		grow_put(buf, len, cap, (char)(0x80 | ((c >> 6) & 0x3F)));
		grow_put(buf, len, cap, (char)(0x80 | (c & 0x3F)));
	}
}

static int hex4(const char *p)
{
	int v = 0;
	for (int i = 0; i < 4; i++) {
		int c = p[i], d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
		if (d < 0)
			return -1;
		v = v * 16 + d;
	}
	return v;
}

static bool read_text(Reader *r, char **out, size_t *out_len)
{
	r->s++;   /* the opening quote */
	char *buf = NULL;
	size_t len = 0, cap = 0;
	for (;;) {
		unsigned char c = (unsigned char)*r->s;
		if (c == '"')
			break;
		if (c == '\0' || c == '\n') {
			free(buf);
			return fail(r, "this text has no closing '\"'");
		}
		if (c < 0x20) {
			free(buf);
			return fail(r, "a control character inside text (write it as \\n, \\t...)");
		}
		if (c == '\\') {
			char e = r->s[1];
			const char *map = "\"\"\\\\//b\bf\fn\nr\rt\t";
			const char *m = e ? strchr(map, e) : NULL;
			if (m && (m - map) % 2 == 0) {
				grow_put(&buf, &len, &cap, m[1]);
				r->s += 2;
				continue;
			}
			if (e == 'u') {
				int v = hex4(r->s + 2);
				if (v < 0) {
					free(buf);
					return fail(r, "\\u needs four hex digits");
				}
				r->s += 6;
				uint32_t cp = (uint32_t)v;
				if (v >= 0xD800 && v <= 0xDBFF && r->s[0] == '\\' && r->s[1] == 'u') {
					int lo = hex4(r->s + 2);
					if (lo >= 0xDC00 && lo <= 0xDFFF) {
						cp = 0x10000 + (((uint32_t)v - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
						r->s += 6;
					}
				}
				put_utf8(&buf, &len, &cap, cp);
				continue;
			}
			free(buf);
			return fail(r, "an unknown escape after '\\'");
		}
		grow_put(&buf, &len, &cap, (char)c);
		r->s++;
	}
	r->s++;   /* the closing quote */
	grow_put(&buf, &len, &cap, '\0');
	*out = buf;
	*out_len = len - 1;
	return true;
}

static Node *read_value(Reader *r, int depth);

static Node *read_value(Reader *r, int depth)
{
	space(r);
	if (depth > 1000) {
		fail(r, "nested more than 1000 deep");
		return NULL;
	}
	char c = *r->s;
	if (c == '{' || c == '[') {
		bool lookup = c == '{';
		Node *n = node_new(lookup ? J_LOOKUP : J_LIST);
		r->s++;
		space(r);
		if (*r->s == (lookup ? '}' : ']')) {
			r->s++;
			return n;
		}
		for (;;) {
			char *key = NULL;
			size_t klen;
			if (lookup) {
				space(r);
				if (*r->s != '"') {
					fail(r, "expected a name in quotes");
					node_release(n);
					return NULL;
				}
				if (!read_text(r, &key, &klen)) {
					node_release(n);
					return NULL;
				}
				space(r);
				if (*r->s != ':') {
					free(key);
					fail(r, "expected ':' after the name");
					node_release(n);
					return NULL;
				}
				r->s++;
			}
			Node *v = read_value(r, depth + 1);
			if (!v) {
				free(key);
				node_release(n);
				return NULL;
			}
			add_item(n, v, key);
			free(key);
			space(r);
			if (*r->s == ',') {
				r->s++;
				continue;
			}
			if (*r->s == (lookup ? '}' : ']')) {
				r->s++;
				return n;
			}
			fail(r, lookup ? "expected ',' or '}'" : "expected ',' or ']'");
			node_release(n);
			return NULL;
		}
	}
	if (c == '"') {
		Node *n = node_new(J_STR);
		if (!read_text(r, &n->text, &n->len)) {
			node_release(n);
			return NULL;
		}
		return n;
	}
	if (c == '-' || isdigit((unsigned char)c)) {
		const char *p = r->s;
		if (*p == '-')
			p++;
		if (!isdigit((unsigned char)*p)) {
			fail(r, "expected a digit");
			return NULL;
		}
		if (*p == '0' && isdigit((unsigned char)p[1])) {
			fail(r, "a number can't start with 0");
			return NULL;
		}
		while (isdigit((unsigned char)*p))
			p++;
		if (*p == '.') {
			p++;
			if (!isdigit((unsigned char)*p)) {
				r->s = p;
				fail(r, "expected a digit after '.'");
				return NULL;
			}
			while (isdigit((unsigned char)*p))
				p++;
		}
		if (*p == 'e' || *p == 'E') {
			p++;
			if (*p == '+' || *p == '-')
				p++;
			if (!isdigit((unsigned char)*p)) {
				r->s = p;
				fail(r, "expected a digit in the exponent");
				return NULL;
			}
			while (isdigit((unsigned char)*p))
				p++;
		}
		Node *n = node_new(J_NUMBER);
		n->num = strtod(r->s, NULL);
		r->s = p;
		return n;
	}
	if (strncmp(r->s, "true", 4) == 0 || strncmp(r->s, "false", 5) == 0) {
		Node *n = node_new(J_BOOL);
		n->b = c == 't';
		r->s += n->b ? 4 : 5;
		return n;
	}
	if (strncmp(r->s, "null", 4) == 0) {
		r->s += 4;
		return node_new(J_NULL);
	}
	fail(r, c ? "expected a value: text in quotes, a number, true, false, null, [ or {" : "the text ended early");
	return NULL;
}

static Node *read_all(const char *text, char *why, size_t why_size)
{
	Reader r = { text, text, 1, text, "" };
	Node *n = read_value(&r, 0);
	if (n) {
		space(&r);
		if (*r.s) {
			fail(&r, "more after the end of the value");
			node_release(n);
			n = NULL;
		}
	}
	if (!n)
		snprintf(why, why_size, "%s", r.why);
	return n;
}

int64_t cby_json_read(const char *text)
{
	char why[200];
	Node *n = read_all(text, why, sizeof why);
	if (!n)
		rt_panic(NULL, 0, 0, "this isn't JSON: %s", why);
	return (int64_t)(intptr_t)n;
}

int cby_json_can_read(const char *text)
{
	char why[200];
	Node *n = read_all(text, why, sizeof why);
	node_release(n);
	return n != NULL;
}

/* ---- making values */

int64_t cby_json_new(int64_t kind) { return (int64_t)(intptr_t)node_new((int)kind); }

int64_t cby_json_from_str(const char *s)
{
	Node *n = node_new(J_STR);
	n->len = strlen(s);
	n->text = dup_n(s, n->len);
	return (int64_t)(intptr_t)n;
}

int64_t cby_json_from_double(double d)
{
	Node *n = node_new(J_NUMBER);
	n->num = d;
	return (int64_t)(intptr_t)n;
}

int64_t cby_json_from_bool(int b)
{
	Node *n = node_new(J_BOOL);
	n->b = b != 0;
	return (int64_t)(intptr_t)n;
}

void cby_json_hold(int64_t h) { if (h) ((Node *)(intptr_t)h)->refs++; }
void cby_json_release(int64_t h) { node_release((Node *)(intptr_t)h); }

static Node *N(int64_t h) { return (Node *)(intptr_t)h; }

static _Noreturn void missing_panic(const Node *n)
{
	char p[300];
	if (n->parent) {
		path_of(n->parent, p, sizeof p);
		if (n->text[0] == '[')
			rt_panic(NULL, 0, 0, "%s has %lld item%s, so there's no item %s", p, (long long)n->parent->n,
				 n->parent->n == 1 ? "" : "s", n->text);
		rt_panic(NULL, 0, 0, "%s has no \"%s\"", p, n->text);
	}
	rt_panic(NULL, 0, 0, "there's no \"%s\"", n->text);
}

/* the node, which must be of this kind (or a panic that says what it is) */
static Node *want(int64_t h, int kind, const char *what)
{
	Node *n = N(h);
	if (n->kind == J_MISSING)
		missing_panic(n);
	if (n->kind != kind && !(kind == J_LOOKUP && what == NULL)) {
		char p[300];
		path_of(n, p, sizeof p);
		if (n->kind == J_STR)
			rt_panic(NULL, 0, 0, "%s is text (\"%.40s%s\"), not %s", p, n->text, n->len > 40 ? "..." : "",
				 what ? what : kind_word(kind));
		if (n->kind == J_NUMBER)
			rt_panic(NULL, 0, 0, "%s is a number (%g), not %s", p, n->num, what ? what : kind_word(kind));
		rt_panic(NULL, 0, 0, "%s is %s, not %s", p, kind_word(n->kind), what ? what : kind_word(kind));
	}
	return n;
}

/* ---- looking inside */

static int64_t missing(Node *parent, const char *what)
{
	Node *m = node_new(J_MISSING);
	m->text = dup_n(what, strlen(what));
	m->parent = parent;
	parent->refs++;
	return (int64_t)(intptr_t)m;
}

int64_t cby_json_get(int64_t h, const char *key)
{
	Node *n = want(h, J_LOOKUP, "a lookup (names and values, written { })");
	for (int64_t i = 0; i < n->n; i++)
		if (strcmp(n->keys[i], key) == 0) {
			n->items[i]->refs++;
			return (int64_t)(intptr_t)n->items[i];
		}
	return missing(n, key);
}

int64_t cby_json_at(int64_t h, int64_t i)
{
	Node *n = want(h, J_LIST, "a list (written [ ])");
	if (i < 0 || i >= n->n) {
		char w[32];
		snprintf(w, sizeof w, "[%lld]", (long long)i);
		return missing(n, w);
	}
	n->items[i]->refs++;
	return (int64_t)(intptr_t)n->items[i];
}

int cby_json_has(int64_t h, const char *key)
{
	Node *n = N(h);
	if (n->kind != J_LOOKUP)
		return 0;
	for (int64_t i = 0; i < n->n; i++)
		if (strcmp(n->keys[i], key) == 0)
			return 1;
	return 0;
}

/* 0 null, 1 bool, 2 number, 3 text, 4 list, 5 lookup, 6 missing */
int64_t cby_json_kind(int64_t h) { return N(h)->kind; }

int64_t cby_json_length(int64_t h)
{
	Node *n = N(h);
	if (n->kind == J_MISSING)
		missing_panic(n);
	if (n->kind == J_STR)
		return (int64_t)n->len;
	if (n->kind != J_LIST && n->kind != J_LOOKUP)
		want(h, J_LIST, "a list or a lookup");
	return n->n;
}

double cby_json_double(int64_t h) { return want(h, J_NUMBER, NULL)->num; }

int64_t cby_json_long(int64_t h)
{
	Node *n = want(h, J_NUMBER, NULL);
	if (n->num != floor(n->num) || fabs(n->num) > 9.2e18) {
		char p[300];
		path_of(n, p, sizeof p);
		rt_panic(NULL, 0, 0, "%s is %g, not a whole number (use .double)", p, n->num);
	}
	return (int64_t)n->num;
}

int cby_json_bool(int64_t h) { return want(h, J_BOOL, NULL)->b; }

/* ---- for structs (json.from and .fill: the functions Cabby writes for them) */

/* a lookup, to fill a struct from */
void cby_json_must_be_lookup(int64_t h, const char *struct_name)
{
	char what[200];
	snprintf(what, sizeof what, "a lookup to fill a '%s' from (names and values, written { })", struct_name);
	want(h, J_LOOKUP, what);
}

/* the lookup has this name, and it isn't null */
int cby_json_given(int64_t h, const char *key)
{
	Node *n = want(h, J_LOOKUP, "a lookup (names and values, written { })");
	for (int64_t i = 0; i < n->n; i++)
		if (strcmp(n->keys[i], key) == 0)
			return n->items[i]->kind != J_NULL;
	return 0;
}

/* which of a field's spellings the lookup has (not null): 1, 2 or 3; 0 if none */
int64_t cby_json_given_name(int64_t h, const char *a, const char *b, const char *c)
{
	return cby_json_given(h, a) ? 1 : cby_json_given(h, b) ? 2 : cby_json_given(h, c) ? 3 : 0;
}

/* a whole number from lo to hi, for a field that holds only those */
int64_t cby_json_whole_in(int64_t h, int64_t lo, int64_t hi)
{
	int64_t v = cby_json_long(h);
	if (v < lo || v > hi) {
		char p[300];
		path_of(N(h), p, sizeof p);
		rt_panic(NULL, 0, 0, "%s is %lld, which doesn't fit: it must be from %lld to %lld", p, (long long)v,
			 (long long)lo, (long long)hi);
	}
	return v;
}

/* a char field: text of one character */
int64_t cby_json_char(int64_t h)
{
	Node *n = want(h, J_STR, "text (one character)");
	if (n->len != 1) {
		char p[300];
		path_of(n, p, sizeof p);
		rt_panic(NULL, 0, 0, "%s is \"%.40s\", not one character", p, n->text);
	}
	return (unsigned char)n->text[0];
}

/* an enum's name that isn't one of its names */
void cby_json_not_one_of(int64_t h, const char *what)
{
	Node *n = N(h);
	char p[300];
	path_of(n, p, sizeof p);
	rt_panic(NULL, 0, 0, "%s is \"%.40s\", not %s", p, n->text ? n->text : "", what);
}

/* for the _or methods: is it there, and of this kind (a whole number for 7)? */
int cby_json_is(int64_t h, int64_t kind)
{
	Node *n = N(h);
	if (kind == 7)
		return n->kind == J_NUMBER && n->num == floor(n->num) && fabs(n->num) <= 9.2e18;
	return n->kind == kind;
}

/* ---- text out: made here, then copied into a Cabby string the right size */

static _Thread_local char *out;   /* (each thread's own, kept for reuse) */
static _Thread_local size_t out_len, out_cap;

static void put(const char *s, size_t n)
{
	if (out_len + n + 1 > out_cap) {
		out_cap = (out_len + n + 1) * 2;
		out = rt_realloc(out, out_cap);
	}
	memcpy(out + out_len, s, n);
	out_len += n;
	out[out_len] = '\0';
}

static void put_s(const char *s) { put(s, strlen(s)); }

static void put_quoted(const char *s, size_t n)
{
	put("\"", 1);
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		char b[8];
		switch (c) {
		case '"': put_s("\\\""); break;
		case '\\': put_s("\\\\"); break;
		case '\n': put_s("\\n"); break;
		case '\r': put_s("\\r"); break;
		case '\t': put_s("\\t"); break;
		case '\b': put_s("\\b"); break;
		case '\f': put_s("\\f"); break;
		default:
			if (c < 0x20) {
				snprintf(b, sizeof b, "\\u%04x", c);
				put_s(b);
			} else {
				put((const char *)&s[i], 1);
			}
		}
	}
	put("\"", 1);
}

static void put_number(double d)
{
	char b[40];
	if (d != d || d == INFINITY || d == -INFINITY)
		snprintf(b, sizeof b, "null");   /* JSON has no NaN or infinity */
	else if (d == floor(d) && fabs(d) < 1e15)
		snprintf(b, sizeof b, "%.0f", d);
	else {
		/* the shortest that reads back as the same number: 10.7, not 10.699999999999999 */
		for (int digits = 15; digits <= 17; digits++) {
			snprintf(b, sizeof b, "%.*g", digits, d);
			if (strtod(b, NULL) == d)
				break;
		}
	}
	put_s(b);
}

static void put_indent(int depth)
{
	put("\n", 1);
	for (int i = 0; i < depth; i++)
		put("  ", 2);
}

static void write_node(const Node *n, bool pretty, int depth)
{
	switch (n->kind) {
	case J_NULL: case J_MISSING: put_s("null"); break;
	case J_BOOL: put_s(n->b ? "true" : "false"); break;
	case J_NUMBER: put_number(n->num); break;
	case J_STR: put_quoted(n->text, n->len); break;
	case J_LIST:
	case J_LOOKUP: {
		bool lookup = n->kind == J_LOOKUP;
		put(lookup ? "{" : "[", 1);
		for (int64_t i = 0; i < n->n; i++) {
			if (i)
				put(",", 1);
			if (pretty)
				put_indent(depth + 1);
			if (lookup) {
				put_quoted(n->keys[i], strlen(n->keys[i]));
				put(pretty ? ": " : ":", pretty ? 2 : 1);
			}
			write_node(n->items[i], pretty, depth + 1);
		}
		if (pretty && n->n)
			put_indent(depth);
		put(lookup ? "}" : "]", 1);
		break;
	}
	}
}

/* what: 0 compact JSON, 1 indented JSON, 2 the text of a text value, 3 the
 * name of the i'th entry of a lookup; gives the length */
int64_t cby_json_text(int64_t h, int64_t what, int64_t i)
{
	out_len = 0;
	put("", 0);
	Node *n = N(h);
	if (what == 2) {
		want(h, J_STR, NULL);
		put(n->text, n->len);
	} else if (what == 3) {
		want(h, J_LOOKUP, "a lookup (names and values, written { })");
		if (i >= 0 && i < n->n)
			put_s(n->keys[i]);
	} else {
		if (n->kind == J_MISSING)
			missing_panic(n);
		write_node(n, what == 1, 0);
	}
	return (int64_t)out_len;
}

/* copies the text just made into dst (room for it, from a str of that length) */
void cby_json_take_text(char *dst)
{
	if (out_len)
		memcpy(dst, out, out_len);
}

/* ---- changing values */

static int64_t copy_node(const Node *n);

void cby_json_set(int64_t h, const char *key, int64_t value)
{
	Node *n = want(h, J_LOOKUP, "a lookup (names and values, written { })");
	Node *v = N(value);
	if (v->kind == J_MISSING)
		missing_panic(v);
	if (v->parent)   /* already part of another value: this one gets a copy */
		v = N(copy_node(v));
	else
		v->refs++;
	for (int64_t i = 0; i < n->n; i++)
		if (strcmp(n->keys[i], key) == 0) {
			Node *old = n->items[i];
			if (old->parent == n)
				old->parent = NULL;
			node_release(old);
			set_place(v, n, key);
			n->items[i] = v;
			return;
		}
	add_item(n, v, key);
}

void cby_json_set_at(int64_t h, int64_t i, int64_t value)
{
	Node *n = want(h, J_LIST, "a list (written [ ])");
	Node *v = N(value);
	if (v->kind == J_MISSING)
		missing_panic(v);
	if (i < 0 || i >= n->n) {
		char p[300];
		path_of(n, p, sizeof p);
		rt_panic(NULL, 0, 0, "%s has %lld item%s, so there's no item [%lld] to change (add one with .add)", p,
			 (long long)n->n, n->n == 1 ? "" : "s", (long long)i);
	}
	if (v->parent)
		v = N(copy_node(v));
	else
		v->refs++;
	Node *old = n->items[i];
	if (old->parent == n)
		old->parent = NULL;
	node_release(old);
	char w[32];
	snprintf(w, sizeof w, "[%lld]", (long long)i);
	set_place(v, n, w);
	n->items[i] = v;
}

void cby_json_add(int64_t h, int64_t value)
{
	Node *n = want(h, J_LIST, "a list (written [ ])");
	Node *v = N(value);
	if (v->kind == J_MISSING)
		missing_panic(v);
	if (v->parent)
		v = N(copy_node(v));
	else
		v->refs++;
	add_item(n, v, NULL);
}

void cby_json_remove(int64_t h, const char *key)
{
	Node *n = want(h, J_LOOKUP, "a lookup (names and values, written { })");
	for (int64_t i = 0; i < n->n; i++)
		if (strcmp(n->keys[i], key) == 0) {
			if (n->items[i]->parent == n)
				n->items[i]->parent = NULL;
			node_release(n->items[i]);
			free(n->keys[i]);
			memmove(&n->items[i], &n->items[i + 1], sizeof *n->items * (size_t)(n->n - i - 1));
			memmove(&n->keys[i], &n->keys[i + 1], sizeof *n->keys * (size_t)(n->n - i - 1));
			n->n--;
			return;
		}
}

static int64_t copy_node(const Node *n)
{
	Node *c = node_new(n->kind == J_MISSING ? J_NULL : n->kind);
	c->b = n->b;
	c->num = n->num;
	if (n->kind == J_STR) {
		c->text = dup_n(n->text, n->len);
		c->len = n->len;
	}
	for (int64_t i = 0; i < n->n; i++) {
		Node *k = N(copy_node(n->items[i]));
		add_item(c, k, n->kind == J_LOOKUP ? n->keys[i] : NULL);
	}
	return (int64_t)(intptr_t)c;
}

int64_t cby_json_copy(int64_t h)
{
	if (N(h)->kind == J_MISSING)
		missing_panic(N(h));
	return copy_node(N(h));
}

/* a node's place, for a top-level value read from text: "Weather" */
void cby_json_name(int64_t h, const char *name)
{
	Node *n = N(h);
	if (!n->parent && name[0])
		set_place(n, NULL, name);
}

/* a worker's thread, ending: what it kept for itself */
static void json_thread_end(void) { free(out); out = NULL; out_len = out_cap = 0; }
__attribute__((constructor)) static void json_threads(void) { rt_on_thread_end(json_thread_end); }
/* and the main thread's, at the program's end */
__attribute__((destructor)) static void json_main_end(void) { json_thread_end(); }

/* (C's: a text value's text, as it's kept; "" for anything else) */
const char *cby_json_str_of(int64_t h)
{
	Node *n = N(h);
	return n && n->kind == J_STR && n->text ? n->text : "";
}
