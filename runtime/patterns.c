/* patterns.c — the C side of the patterns library (lib/patterns.hby):
 * regular expressions. Part of Cabby's runtime, so the interpreter and
 * compiled programs both have it.
 *
 * Cabby matches patterns itself (the system's regular expressions differ
 * between Linux and a Mac, and have no lazy repeats or looking ahead). A
 * pattern is read into a tree of steps, and matched by trying each way in
 * turn, going back to try the next when one doesn't work out, as Perl's
 * and Python's do: the first alternative that works is the one taken, a
 * repeat takes as much as it can (or, lazily, as little), and \b, \1
 * and (?= ) work as they do there. Text is matched as UTF-8, a letter at
 * a time, not a byte at a time: "." is one letter, and [[:alpha:]] takes
 * in Greek (the classes are the C library's iswalpha and the rest, in a
 * UTF-8 locale). */
#ifndef __APPLE__
#define _GNU_SOURCE   /* (pthread_getattr_np: how big this thread's stack is) */
#endif
#include "rt.h"
#include <pthread.h>
#include <locale.h>
#include <stdint.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

/* ------------------------------------------------------------ UTF-8 */

/* a locale for UTF-8 text, used just while matching (the program's own
 * is left as it is) */
static locale_t utf8(void)
{
	static int tried;
	static locale_t loc = (locale_t)0;
	if (!tried) {
		tried = 1;
		static const char *const names[] = { "C.UTF-8", "C.utf8", "en_US.UTF-8", "en_GB.UTF-8", "UTF-8", NULL };
		for (int i = 0; names[i] && !loc; i++)
			loc = newlocale(LC_CTYPE_MASK | LC_COLLATE_MASK, names[i], (locale_t)0);
	}
	return loc;
}

static locale_t enter(void)
{
	locale_t want = utf8();
	return want ? uselocale(want) : (locale_t)0;
}

static void leave(locale_t old)
{
	if (old)
		uselocale(old);
}

/* how many bytes the letter starting at s takes */
static int letter_length(const char *s)
{
	unsigned char c = (unsigned char)*s;
	int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
	for (int i = 1; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80)
			return i;
	return n;
}

/* ------------------------------------------------------------ text */

typedef struct {
	char *s;
	size_t n, cap;
} Text;

static void put_n(Text *t, const char *s, size_t n)
{
	if (t->n + n + 1 > t->cap) {
		t->cap = (t->n + n + 1) * 2 + 32;
		t->s = realloc(t->s, t->cap);
	}
	memcpy(t->s + t->n, s, n);
	t->n += n;
	t->s[t->n] = '\0';
}

static void put(Text *t, const char *s) { put_n(t, s, strlen(s)); }

/* ------------------------------------------------------------ reading a pattern */

/* A pattern is read into a tree of nodes. Each node is one step (a
 * letter, a class of letters, the start of the text, a group, a choice
 * of alternatives, a repeat), and the steps of a sequence are joined by
 * `next`. */
enum {
	N_CHAR,        /* one letter */
	N_ANY,         /* . */
	N_CLASS,       /* [ ], \d, \w, \s and their opposites */
	N_START,       /* ^ */
	N_END,         /* $ */
	N_EDGE,        /* \b: the edge of a word */
	N_NOT_EDGE,    /* \B */
	N_GROUP,       /* ( ), (?: ) */
	N_ALT,         /* a|b */
	N_REPEAT,      /* * + ? { }, and their lazy forms */
	N_BACKREF,     /* \1: what part 1 matched, again */
	N_AHEAD,       /* (?= ): looking ahead */
	N_NOT_AHEAD,   /* (?! ) */
	N_BEHIND,      /* (?<= ): looking behind */
	N_NOT_BEHIND,  /* (?<! ) */
	N_EMPTY,       /* nothing (an empty alternative) */
};

/* the classes in [[:alpha:]] and the rest, as bits */
enum {
	C_ALPHA = 1 << 0, C_DIGIT = 1 << 1, C_ALNUM = 1 << 2, C_UPPER = 1 << 3, C_LOWER = 1 << 4,
	C_SPACE = 1 << 5, C_PUNCT = 1 << 6, C_XDIGIT = 1 << 7, C_BLANK = 1 << 8, C_CNTRL = 1 << 9,
	C_PRINT = 1 << 10, C_GRAPH = 1 << 11, C_WORD = 1 << 12,
};

typedef struct {
	uint32_t lo, hi;
} Range;

typedef struct Node Node;
struct Node {
	int type;
	Node *next;
	uint32_t ch;              /* N_CHAR */
	Range *ranges;            /* N_CLASS: its letters and ranges, */
	int nranges;
	unsigned named;           /* its named classes, */
	bool negate;              /* and whether it's [^ ] */
	int index;                /* N_GROUP: its part's number (0: none); N_BACKREF: the part */
	Node *child;              /* N_GROUP, N_REPEAT, N_AHEAD: what's inside */
	Node **alts;              /* N_ALT: the alternatives */
	int nalts;
	int min, max;             /* N_REPEAT: how many (max -1: no limit) */
	bool lazy;                /* N_REPEAT: as few as it can */
};

typedef struct {
	Node *root;
	Node **nodes;             /* every node, to free */
	int nnodes, capnodes;
	size_t groups;
	char **names;             /* each part's name, if (?<name> ) gave it one (by number: [0] unused) */
	bool icase;
	bool anchored;            /* it starts with ^: tried at the start alone */
	char *source;             /* as it was written (for messages) */
} Pattern;

typedef struct {
	long rm_so, rm_eo;        /* where a part starts and ends (-1: it didn't match) */
} Span;

typedef struct {
	const char *pattern;      /* (for messages) */
	const char *p;            /* where reading is */
	Pattern *pt;
	int most_backref;
} Reader;

static _Noreturn void bad(const char *pattern, const char *why)
{
	rt_panic(NULL, 0, 0, "the pattern '%s' %s", pattern, why);
}

static Node *node(Reader *r, int type)
{
	Pattern *pt = r->pt;
	if (pt->nnodes == pt->capnodes) {
		pt->capnodes = pt->capnodes ? pt->capnodes * 2 : 16;
		pt->nodes = realloc(pt->nodes, sizeof *pt->nodes * (size_t)pt->capnodes);
	}
	Node *n = calloc(1, sizeof *n);
	n->type = type;
	pt->nodes[pt->nnodes++] = n;
	return n;
}

/* the letter (as a number) at s, and how many bytes it takes */
static uint32_t decode(const char *s, int *len)
{
	unsigned char c = (unsigned char)s[0];
	int n = letter_length(s);
	*len = n;
	if (c < 0x80 || n == 1)
		return c;
	uint32_t cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
	for (int i = 1; i < n; i++)
		cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
	return cp;
}

static void add_range(Node *n, uint32_t lo, uint32_t hi)
{
	n->ranges = realloc(n->ranges, sizeof *n->ranges * (size_t)(n->nranges + 1));
	n->ranges[n->nranges].lo = lo;
	n->ranges[n->nranges].hi = hi;
	n->nranges++;
}

static Node *shorthand(Reader *r, char e)
{
	Node *n = node(r, N_CLASS);
	n->named = e == 'd' || e == 'D' ? C_DIGIT : e == 'w' || e == 'W' ? C_WORD : C_SPACE;
	n->negate = e == 'D' || e == 'W' || e == 'S';
	return n;
}

static unsigned class_named(const char *name, size_t len)
{
	static const struct { const char *name; unsigned bit; } classes[] = {
		{ "alpha", C_ALPHA }, { "digit", C_DIGIT }, { "alnum", C_ALNUM }, { "upper", C_UPPER },
		{ "lower", C_LOWER }, { "space", C_SPACE }, { "punct", C_PUNCT }, { "xdigit", C_XDIGIT },
		{ "blank", C_BLANK }, { "cntrl", C_CNTRL }, { "print", C_PRINT }, { "graph", C_GRAPH },
	};
	for (size_t i = 0; i < sizeof classes / sizeof classes[0]; i++)
		if (strlen(classes[i].name) == len && strncmp(classes[i].name, name, len) == 0)
			return classes[i].bit;
	return 0;
}

/* a letter inside [ ], escaped or not; *e is set to the letter after a \ */
static uint32_t class_letter(Reader *r, char *escaped)
{
	*escaped = 0;
	int len;
	if (*r->p == '\\') {
		char e = r->p[1];
		if (!e)
			bad(r->pattern, "ends with a \\ that has nothing after it");
		r->p += 2;
		*escaped = e;
		return e == 't' ? '\t' : e == 'n' ? '\n' : e == 'r' ? '\r' : (uint32_t)(unsigned char)e;
	}
	uint32_t cp = decode(r->p, &len);
	r->p += len;
	return cp;
}

/* [ ], from just after the [ */
static Node *bracket(Reader *r)
{
	Node *n = node(r, N_CLASS);
	if (*r->p == '^') {
		n->negate = true;
		r->p++;
	}
	bool first = true;
	while (*r->p && (*r->p != ']' || first)) {
		first = false;
		if (r->p[0] == '[' && r->p[1] == ':') {
			const char *end = strstr(r->p + 2, ":]");
			if (!end)
				bad(r->pattern, "has a [: without its :]");
			unsigned bit = class_named(r->p + 2, (size_t)(end - (r->p + 2)));
			if (!bit) {
				char why[120];
				snprintf(why, sizeof why, "has [:%.*s:], a class patterns doesn't know (patterns.md lists them)",
					 (int)(end - (r->p + 2)) > 40 ? 40 : (int)(end - (r->p + 2)), r->p + 2);
				bad(r->pattern, why);
			}
			n->named |= bit;
			r->p = end + 2;
			continue;
		}
		char e;
		uint32_t lo = class_letter(r, &e);
		if (e == 'd' || e == 'w' || e == 's') {
			n->named |= e == 'd' ? C_DIGIT : e == 'w' ? C_WORD : C_SPACE;
			continue;
		}
		if (e == 'D' || e == 'W' || e == 'S')
			bad(r->pattern, "has \\D, \\W or \\S inside [ ]: write [^ ] instead, like [^0-9]");
		if (e && ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z') || (e >= '0' && e <= '9')) &&
		    e != 't' && e != 'n' && e != 'r') {
			char why[96];
			snprintf(why, sizeof why, "has \\%c inside [ ], which patterns doesn't know (patterns.md lists what it does)", e);
			bad(r->pattern, why);
		}
		uint32_t hi = lo;
		if (r->p[0] == '-' && r->p[1] && r->p[1] != ']') {
			r->p++;
			char e2;
			hi = class_letter(r, &e2);
			if (hi < lo)
				bad(r->pattern, "has a range in [ ] whose end comes before its start (like z-a)");
		}
		add_range(n, lo, hi);
	}
	if (*r->p != ']')
		bad(r->pattern, "has a [ without its ]");
	r->p++;
	return n;
}

static Node *alternatives(Reader *r);

/* {2}, {2,4}, {2,}: true (and the numbers) if p is at one */
static bool counted(const char *p, int *min, int *max, const char **after)
{
	if (*p != '{' || !(p[1] >= '0' && p[1] <= '9'))
		return false;
	char *end;
	long lo = strtol(p + 1, &end, 10), hi = lo;
	if (*end == ',') {
		if (end[1] == '}') {
			hi = -1;
			end++;
		} else if (end[1] >= '0' && end[1] <= '9') {
			hi = strtol(end + 1, &end, 10);
		} else {
			return false;
		}
	}
	if (*end != '}')
		return false;
	*min = (int)(lo > 100000 ? 100000 : lo);
	*max = hi < 0 ? -1 : (int)(hi > 100000 ? 100000 : hi);
	*after = end + 1;
	return true;
}

/* the number of the part called name (0: none) */
static int part_named(const Pattern *pt, const char *name, size_t len)
{
	if (pt->names)
		for (size_t g = 1; g <= pt->groups; g++)
			if (pt->names[g] && strlen(pt->names[g]) == len && !strncmp(pt->names[g], name, len))
				return (int)g;
	return 0;
}

static Node *atom(Reader *r)
{
	const char *p = r->p;
	switch (*p) {
	case '(': {
		Node *n;
		if (p[1] == '?') {
			if (p[2] == ':')
				n = node(r, N_GROUP);
			else if (p[2] == '=')
				n = node(r, N_AHEAD);
			else if (p[2] == '!')
				n = node(r, N_NOT_AHEAD);
			else if (p[2] == '<' && p[3] == '=')
				n = node(r, N_BEHIND);
			else if (p[2] == '<' && p[3] == '!')
				n = node(r, N_NOT_BEHIND);
			else if (p[2] == '<') {
				const char *name = p + 3, *e = name;
				while ((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') || (*e >= '0' && *e <= '9') || *e == '_')
					e++;
				if (e == name || *e != '>')
					bad(r->pattern, "has (?< without a name and > after it: (?<year>\\d{4}) names a part");
				if (part_named(r->pt, name, (size_t)(e - name)))
					bad(r->pattern, "names two parts the same");
				n = node(r, N_GROUP);
				n->index = (int)++r->pt->groups;
				if (!r->pt->names)
					r->pt->names = calloc(r->pt->groups + 1, sizeof *r->pt->names);
				else
					r->pt->names = realloc(r->pt->names, sizeof *r->pt->names * (r->pt->groups + 1));
				r->pt->names[n->index] = strndup(name, (size_t)(e - name));
				r->p = e + 1;
				goto inside;
			} else
				bad(r->pattern, "has (? followed by something patterns doesn't know: (?: (?= (?! (?<= (?<! and (?<name> are what it has");
			r->p += n->type == N_BEHIND || n->type == N_NOT_BEHIND ? 4 : 3;
		} else {
			n = node(r, N_GROUP);
			n->index = (int)++r->pt->groups;
			if (r->pt->names) {
				r->pt->names = realloc(r->pt->names, sizeof *r->pt->names * (r->pt->groups + 1));
				r->pt->names[n->index] = NULL;
			}
			r->p++;
		}
	inside:
		n->child = alternatives(r);
		if (*r->p != ')')
			bad(r->pattern, "has a ( without its )");
		r->p++;
		return n;
	}
	case '[':
		r->p++;
		return bracket(r);
	case '.':
		r->p++;
		return node(r, N_ANY);
	case '^':
		r->p++;
		return node(r, N_START);
	case '$':
		r->p++;
		return node(r, N_END);
	case '*': case '+': case '?':
		bad(r->pattern, "has a repeat (*, + or ?) with nothing before it to repeat");
	case '{': {
		int a, b;
		const char *after;
		if (counted(p, &a, &b, &after))
			bad(r->pattern, "has a repeat ({ }) with nothing before it to repeat");
		break;   /* (a { that isn't a repeat is just a {) */
	}
	case '\\': {
		char e = p[1];
		if (!e)
			bad(r->pattern, "ends with a \\ that has nothing after it");
		r->p += 2;
		switch (e) {
		case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
			return shorthand(r, e);
		case 'k': {
			const char *name = r->p + 1, *close = name;
			if (*r->p == '<')
				while (*close && *close != '>')
					close++;
			int g = *r->p == '<' && *close == '>' ? part_named(r->pt, name, (size_t)(close - name)) : 0;
			if (!g)
				bad(r->pattern, "has \\k without <name> of a part named before it");
			r->p = close + 1;
			Node *n = node(r, N_BACKREF);
			n->index = g;
			return n;
		}
		case 'b':
			return node(r, N_EDGE);
		case 'B':
			return node(r, N_NOT_EDGE);
		case 't': case 'n': case 'r': {
			Node *n = node(r, N_CHAR);
			n->ch = e == 't' ? '\t' : e == 'n' ? '\n' : '\r';
			return n;
		}
		default:
			if (e >= '1' && e <= '9') {
				Node *n = node(r, N_BACKREF);
				n->index = e - '0';
				if (n->index > r->most_backref)
					r->most_backref = n->index;
				return n;
			}
			if ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z') || e == '0') {
				char why[96];
				snprintf(why, sizeof why, "has \\%c, which patterns doesn't know (patterns.md lists what it does)", e);
				bad(r->pattern, why);
			}
			Node *n = node(r, N_CHAR);   /* \. \( \\ ...: that character itself */
			n->ch = (unsigned char)e;
			return n;
		}
	}
	}
	int len;
	Node *n = node(r, N_CHAR);
	n->ch = decode(p, &len);
	r->p += len;
	return n;
}

/* one alternative: steps one after another, up to a | or a ) */
static Node *sequence(Reader *r)
{
	Node *head = NULL, *tail = NULL;
	while (*r->p && *r->p != '|' && *r->p != ')') {
		Node *a = atom(r);
		for (;;) {
			int min, max;
			const char *after = NULL;
			char q = *r->p;
			if (q == '*') { min = 0; max = -1; after = r->p + 1; }
			else if (q == '+') { min = 1; max = -1; after = r->p + 1; }
			else if (q == '?') { min = 0; max = 1; after = r->p + 1; }
			else if (!counted(r->p, &min, &max, &after)) break;
			if (max >= 0 && max < min)
				bad(r->pattern, "has a repeat whose most is less than its least (like {4,2})");
			if (a->type == N_START || a->type == N_END || a->type == N_EDGE || a->type == N_NOT_EDGE)
				bad(r->pattern, "has a repeat after ^, $ or \\b, which take up no room, so there's nothing to repeat");
			if (a->type == N_REPEAT && !a->lazy && a->child && r->p[-1] != ')')
				bad(r->pattern, "has two repeats in a row (like a**): put the first in ( ) to repeat it again");
			r->p = after;
			Node *rep = node(r, N_REPEAT);
			rep->child = a;
			rep->min = min;
			rep->max = max;
			if (*r->p == '?') {   /* +? *? ?? {2,4}?: as few as it can */
				rep->lazy = true;
				r->p++;
			}
			a = rep;
			if (*r->p == '*' || *r->p == '+' || (*r->p == '?' && !rep->lazy) || counted(r->p, &min, &max, &after))
				bad(r->pattern, "has two repeats in a row (like a**): put the first in ( ) to repeat it again");
		}
		if (tail)
			tail->next = a;
		else
			head = a;
		tail = a;
	}
	return head ? head : node(r, N_EMPTY);
}

/* alternatives, a|b|c, up to a ) or the end */
static Node *alternatives(Reader *r)
{
	Node *first = sequence(r);
	if (*r->p != '|')
		return first;
	Node *n = node(r, N_ALT);
	n->alts = malloc(sizeof *n->alts);
	n->alts[0] = first;
	n->nalts = 1;
	while (*r->p == '|') {
		r->p++;
		n->alts = realloc(n->alts, sizeof *n->alts * (size_t)(n->nalts + 1));
		n->alts[n->nalts++] = sequence(r);
	}
	return n;
}

static void pattern_free(Pattern *pt)
{
	for (int i = 0; i < pt->nnodes; i++) {
		free(pt->nodes[i]->ranges);
		free(pt->nodes[i]->alts);
		free(pt->nodes[i]);
	}
	free(pt->nodes);
	free(pt->source);
	if (pt->names)
		for (size_t g = 1; g <= pt->groups; g++)
			free(pt->names[g]);
	free(pt->names);
	free(pt);
}

static Pattern *read_pattern(const char *pattern, bool icase)
{
	Pattern *pt = calloc(1, sizeof *pt);
	pt->icase = icase;
	pt->source = strdup(pattern);
	Reader r = { pattern, pattern, pt, 0 };
	pt->root = alternatives(&r);
	if (*r.p == ')')
		bad(pattern, "has a ) without its (");
	if (r.most_backref > (int)pt->groups) {
		char why[120];
		snprintf(why, sizeof why, "has \\%d, but only %d ( ) part%s", r.most_backref, (int)pt->groups,
			 pt->groups == 1 ? "" : "s");
		bad(pattern, why);
	}
	pt->anchored = pt->root->type == N_START;
	return pt;
}

/* ------------------------------------------------------------ matching */

/* What's left to do after a step, when the step itself isn't the last of
 * its sequence: carry on with a sequence, end a part (remembering where it
 * matched), go round a repeat again, or accept (inside a look ahead). */
enum { K_SEQ, K_GROUP, K_REPEAT, K_ACCEPT, K_AT };

typedef struct Cont {
	int kind;
	Node *node;
	int count;
	size_t start;
	struct Cont *next;
} Cont;

typedef struct {
	const char *text;
	size_t len;
	Span *parts;
	bool icase;
	bool whole;               /* it must reach the text's end */
	size_t end;               /* where the match ended */
	long steps, most_steps;
	const char *stack_top;    /* where the stack was when matching began */
	const char *pattern;
} State;


/* How deep matching may go: half of this thread's stack. Each turn of a
 * repeated ( ) goes a little deeper (a few hundred bytes), so (.)* over a
 * long text can't go on for ever. */
static size_t stack_room(void)
{
	static _Thread_local size_t room;
	if (!room) {
		size_t size = 0;
#ifdef __APPLE__
		size = pthread_get_stacksize_np(pthread_self());
#else
		pthread_attr_t a;
		if (pthread_getattr_np(pthread_self(), &a) == 0) {
			pthread_attr_getstacksize(&a, &size);
			pthread_attr_destroy(&a);
		}
#endif
		room = size ? size / 2 : (size_t)2 << 20;
		if (room < ((size_t)256 << 10))
			room = (size_t)256 << 10;
	}
	return room;
}

static bool is_word_letter(uint32_t cp)
{
	return cp == '_' || iswalnum((wint_t)cp);
}

static bool in_named(unsigned named, uint32_t cp)
{
	wint_t c = (wint_t)cp;
	return ((named & C_ALPHA) && iswalpha(c)) || ((named & C_DIGIT) && iswdigit(c)) ||
	       ((named & C_ALNUM) && iswalnum(c)) || ((named & C_UPPER) && iswupper(c)) ||
	       ((named & C_LOWER) && iswlower(c)) || ((named & C_SPACE) && iswspace(c)) ||
	       ((named & C_PUNCT) && iswpunct(c)) || ((named & C_XDIGIT) && iswxdigit(c)) ||
	       ((named & C_BLANK) && iswblank(c)) || ((named & C_CNTRL) && iswcntrl(c)) ||
	       ((named & C_PRINT) && iswprint(c)) || ((named & C_GRAPH) && iswgraph(c)) ||
	       ((named & C_WORD) && is_word_letter(cp));
}

static bool in_class_exactly(const Node *n, uint32_t cp)
{
	for (int i = 0; i < n->nranges; i++)
		if (cp >= n->ranges[i].lo && cp <= n->ranges[i].hi)
			return true;
	return n->named && in_named(n->named, cp);
}

/* whether a letter is what a one-letter step wants */
static bool one_letter(const State *s, const Node *n, uint32_t cp)
{
	switch (n->type) {
	case N_CHAR:
		return cp == n->ch || (s->icase && towlower((wint_t)cp) == towlower((wint_t)n->ch));
	case N_ANY:
		return true;
	default: {
		bool in = in_class_exactly(n, cp);
		if (!in && s->icase) {
			uint32_t lo = (uint32_t)towlower((wint_t)cp), up = (uint32_t)towupper((wint_t)cp);
			in = (lo != cp && in_class_exactly(n, lo)) || (up != cp && in_class_exactly(n, up));
		}
		return in != n->negate;
	}
	}
}

static bool single_letter(const Node *n)
{
	return n->type == N_CHAR || n->type == N_ANY || n->type == N_CLASS;
}

/* the letter just before pos (pos > 0) */
static uint32_t letter_before(const State *s, size_t pos)
{
	size_t b = pos - 1;
	while (b > 0 && ((unsigned char)s->text[b] & 0xC0) == 0x80 && pos - b < 4)
		b--;
	int len;
	return decode(s->text + b, &len);
}

static bool at_edge(const State *s, size_t pos)
{
	bool before = pos > 0 && is_word_letter(letter_before(s, pos));
	int len;
	bool after = pos < s->len && is_word_letter(decode(s->text + pos, &len));
	return before != after;
}

static bool go(State *s, Node *n, size_t pos, Cont *k);
static bool resume(State *s, Cont *k, size_t pos);

/* a sequence of one-letter steps alone (ab, \d\d, .): each turn of a
 * repeat of it can only go one way */
static bool letters_only(const Node *seq)
{
	for (; seq; seq = seq->next)
		if (!single_letter(seq))
			return false;
	return true;
}

/* one turn of such a sequence from at: where it ends, or (size_t)-1 */
static size_t one_turn(const State *s, const Node *seq, size_t at)
{
	for (; seq; seq = seq->next) {
		if (at >= s->len)
			return (size_t)-1;
		int len;
		if (!one_letter(s, seq, decode(s->text + at, &len)))
			return (size_t)-1;
		at += (size_t)len;
	}
	return at;
}

static bool repeat(State *s, Node *r, int count, size_t pos, Cont *after)
{
	Node *c = r->child;
	const Node *seq = single_letter(c) ? c : c->type == N_GROUP && letters_only(c->child) && c->child->type != N_EMPTY ? c->child : NULL;
	if (seq && count == 0) {
		/* letters, repeated (\w+, .*, [^,]*, (ab)+): where each count of
		 * turns ends, worked out going forward, then tried from the most
		 * (or, lazily, the fewest). A part, (ab), is what its last turn
		 * matched. */
		const Node *one = single_letter(c) ? c : NULL;
		size_t room[8], *ends = room, cap = 8, n = 0;
		ends[n++] = pos;
		size_t at = pos;
		while ((r->max < 0 || (int)n - 1 < r->max) && at < s->len) {
			if (one) {
				int len;
				if (!one_letter(s, one, decode(s->text + at, &len)))
					break;
				at += (size_t)len;
			} else {
				size_t next = one_turn(s, seq, at);
				if (next == (size_t)-1)
					break;
				at = next;
			}
			if (n == cap) {
				size_t *more = malloc(sizeof *more * cap * 2);
				memcpy(more, ends, sizeof *ends * cap);
				if (ends != room)
					free(ends);
				ends = more;
				cap *= 2;
			}
			ends[n++] = at;
		}
		bool found = false;
		int most = (int)n - 1;
		Span *part = !one && c->index ? &s->parts[c->index] : NULL;
		Span kept = part ? *part : (Span){ -1, -1 };
		if (most >= r->min) {
			for (int k = 0; k <= most - r->min && !found; k++) {
				int i = r->lazy ? r->min + k : most - k;
				if (part && i > 0) {
					part->rm_so = (long)ends[i - 1];
					part->rm_eo = (long)ends[i];
				} else if (part) {
					*part = kept;
				}
				found = resume(s, after, ends[i]);
			}
		}
		if (part && !found)
			*part = kept;
		if (ends != room)
			free(ends);
		return found;
	}
	bool more = r->max < 0 || count < r->max;
	bool enough = count >= r->min;
	Cont again = { K_REPEAT, r, count, pos, after };
	if (r->lazy) {
		if (enough && resume(s, after, pos))
			return true;
		return more && go(s, r->child, pos, &again);
	}
	if (more && go(s, r->child, pos, &again))
		return true;
	return enough && resume(s, after, pos);
}

static bool resume(State *s, Cont *k, size_t pos)
{
	if (!k) {
		if (s->whole && pos != s->len)
			return false;
		s->end = pos;
		return true;
	}
	switch (k->kind) {
	case K_SEQ:
		return go(s, k->node, pos, k->next);
	case K_GROUP: {
		Span *part = &s->parts[k->node->index];
		Span old = *part;
		part->rm_so = (long)k->start;
		part->rm_eo = (long)pos;
		if (resume(s, k->next, pos))
			return true;
		*part = old;
		return false;
	}
	case K_REPEAT:
		if (pos == k->start && k->count + 1 > k->node->min)
			return false;   /* (a turn that took up nothing: going round again would never end) */
		return repeat(s, k->node, k->count + 1, pos, k->next);
	case K_AT:   /* (a look behind: it must end just where it started looking) */
		return pos == k->start;
	default:
		return true;
	}
}

/* matches n, and what comes after it in its sequence, and then k */
static bool go(State *s, Node *n, size_t pos, Cont *k)
{
	char here;
	if ((size_t)(s->stack_top - &here) > stack_room()) {
		rt_panic(NULL, 0, 0, "the pattern '%s' needed to go back and forth too many times in a row on this text "
			 "(a repeated ( ) over a very long text): try a simpler pattern", s->pattern);
	}
	bool found = false;
	for (;;) {
		if (++s->steps > s->most_steps)
			rt_panic(NULL, 0, 0, "the pattern '%s' tried too many ways of matching this text: a repeat inside a repeat, "
				 "like (a+)+, can have far more ways to try than the text has letters. Try a simpler pattern (a+)",
				 s->pattern);
		if (!n) {
			found = resume(s, k, pos);
			break;
		}
		if (single_letter(n)) {
			if (pos >= s->len)
				break;
			int len;
			if (!one_letter(s, n, decode(s->text + pos, &len)))
				break;
			pos += (size_t)len;
			n = n->next;
			continue;
		}
		if (n->type == N_START || n->type == N_END || n->type == N_EDGE || n->type == N_NOT_EDGE || n->type == N_EMPTY) {
			bool ok = n->type == N_START ? pos == 0 : n->type == N_END ? pos == s->len :
				  n->type == N_EDGE ? at_edge(s, pos) : n->type == N_NOT_EDGE ? !at_edge(s, pos) : true;
			if (!ok)
				break;
			n = n->next;
			continue;
		}
		if (n->type == N_BACKREF) {
			Span part = s->parts[n->index];
			if (part.rm_so < 0)
				break;   /* (its part didn't match: there's nothing to match again) */
			size_t want = (size_t)(part.rm_eo - part.rm_so);
			if (!s->icase) {
				if (pos + want > s->len || memcmp(s->text + pos, s->text + part.rm_so, want) != 0)
					break;
				pos += want;
			} else {
				size_t a = (size_t)part.rm_so, b = pos;
				bool same = true;
				while (a < (size_t)part.rm_eo) {
					if (b >= s->len) { same = false; break; }
					int la, lb;
					uint32_t ca = decode(s->text + a, &la), cb = decode(s->text + b, &lb);
					if (towlower((wint_t)ca) != towlower((wint_t)cb)) { same = false; break; }
					a += (size_t)la;
					b += (size_t)lb;
				}
				if (!same)
					break;
				pos = b;
			}
			n = n->next;
			continue;
		}
		if (n->type == N_AHEAD || n->type == N_NOT_AHEAD) {
			Cont accept = { K_ACCEPT, NULL, 0, 0, NULL };
			bool whole = s->whole;
			s->whole = false;
			bool there = go(s, n->child, pos, &accept);
			s->whole = whole;
			if (there != (n->type == N_AHEAD))
				break;
			n = n->next;
			continue;
		}
		if (n->type == N_BEHIND || n->type == N_NOT_BEHIND) {
			/* what's inside must match some text that ends here: tried from
			 * the nearest start back */
			Cont at = { K_AT, NULL, 0, pos, NULL };
			bool whole = s->whole;
			s->whole = false;
			bool there = false;
			for (size_t from = pos + 1; from-- > 0;) {
				if (from < s->len && ((unsigned char)s->text[from] & 0xC0) == 0x80)
					continue;   /* (the middle of a letter) */
				if (go(s, n->child, from, &at)) {
					there = true;
					break;
				}
			}
			s->whole = whole;
			if (there != (n->type == N_BEHIND))
				break;
			n = n->next;
			continue;
		}
		/* a group, alternatives or a repeat: what comes after it is a
		 * continuation of its own */
		Cont rest = { K_SEQ, n->next, 0, 0, k };
		Cont *after = n->next ? &rest : k;
		if (n->type == N_GROUP) {
			if (!n->index) {
				found = go(s, n->child, pos, after);
			} else {
				Cont end = { K_GROUP, n, 0, pos, after };
				found = go(s, n->child, pos, &end);
			}
		} else if (n->type == N_ALT) {
			for (int i = 0; i < n->nalts && !found; i++)
				found = go(s, n->alts[i], pos, after);
		} else {
			found = repeat(s, n, 0, pos, after);
		}
		break;
	}
	return found;
}

/* the first match at or after `from`; m has room for the parts too. 1 if
 * there's one. With whole, only one that's all of the text. */
static int search_as(Pattern *pt, const char *text, size_t len, size_t from, Span *m, bool whole, const char *source)
{
	char top;
	/* (a pattern that goes back and forth too much, like (a+)+b on a long
	 * line of a's, is stopped rather than left to run for years) */
	State s = { text, len, m, pt->icase, whole, 0, 0, 20000000L + 50L * (long)len, &top, source };
	locale_t old = enter();
	int found = 0;
	for (size_t start = from; start <= len;) {
		for (size_t g = 0; g <= pt->groups; g++)
			m[g].rm_so = m[g].rm_eo = -1;
		if (pt->root->type == N_CHAR && !pt->icase && pt->root->ch < 0x80) {
			/* (it starts with a letter: straight to the next one) */
			const char *at = memchr(text + start, (int)pt->root->ch, len - start);
			if (!at)
				break;
			start = (size_t)(at - text);
		}
		if (go(&s, pt->root, start, NULL)) {
			m[0].rm_so = (long)start;
			m[0].rm_eo = (long)s.end;
			found = 1;
			break;
		}
		if (whole || pt->anchored || start >= len)
			break;
		start += (size_t)letter_length(text + start);
	}
	leave(old);
	return found;
}

/* ------------------------------------------------------------ patterns */

static Pattern **pats;
static int npats;

int64_t cby_patterns_make(const char *pattern, int ignore_case)
{
	Pattern *pt = read_pattern(pattern, ignore_case != 0);
	int i = 0;
	while (i < npats && pats[i])
		i++;
	if (i == npats) {
		pats = realloc(pats, sizeof *pats * (size_t)(npats + 1));
		npats++;
	}
	pats[i] = pt;
	return i + 1;
}

static Pattern *pattern(int64_t id)
{
	if (id < 1 || id > npats || !pats[id - 1])
		rt_panic(NULL, 0, 0, "that pattern has been dropped");
	return pats[id - 1];
}

void cby_patterns_free(int64_t id)
{
	if (id >= 1 && id <= npats && pats[id - 1]) {
		pattern_free(pats[id - 1]);
		pats[id - 1] = NULL;
	}
}

static int search(Pattern *pt, const char *text, size_t len, size_t from, Span *m)
{
	return search_as(pt, text, len, from, m, false, pt->source);
}

/* ---- results: the texts an operation found, taken by Cabby one at a time */

static _Thread_local char **res;
static _Thread_local size_t *reslen;
static _Thread_local int nres, capres;

static void results_clear(void)
{
	for (int i = 0; i < nres; i++)
		free(res[i]);
	nres = 0;
}

static void result_n(const char *s, size_t n)
{
	if (nres == capres) {
		capres = capres ? capres * 2 : 16;
		res = realloc(res, sizeof *res * (size_t)capres);
		reslen = realloc(reslen, sizeof *reslen * (size_t)capres);
	}
	res[nres] = malloc(n + 1);
	memcpy(res[nres], s, n);
	res[nres][n] = '\0';
	reslen[nres++] = n;
}

int64_t cby_patterns_result_length(int64_t i) { return i >= 0 && i < nres ? (int64_t)reslen[i] : 0; }

void cby_patterns_take(int64_t i, char *dst)
{
	if (i >= 0 && i < nres && reslen[i])
		memcpy(dst, res[i], reslen[i]);
}

/* ---- matching */

int cby_patterns_matches(int64_t id, const char *text)
{
	Pattern *pt = pattern(id);
	Span *m = malloc(sizeof *m * (pt->groups + 1));
	int whole = search_as(pt, text, strlen(text), 0, m, true, pt->source);
	free(m);
	return whole;
}

/* where the first match starts, in bytes (-1: none) */
int64_t cby_patterns_position(int64_t id, const char *text)
{
	Pattern *pt = pattern(id);
	Span *m = malloc(sizeof *m * (pt->groups + 1));
	int64_t at = search(pt, text, strlen(text), 0, m) ? (int64_t)m[0].rm_so : -1;
	free(m);
	return at;
}

/* each match, one after another (just the first, unless all): how many.
 * After an empty match, the next is looked for a letter on. */
int64_t cby_patterns_find(int64_t id, const char *text, int all)
{
	Pattern *pt = pattern(id);
	results_clear();
	Span *m = malloc(sizeof *m * (pt->groups + 1));
	size_t len = strlen(text), from = 0;
	while (from <= len && search(pt, text, len, from, m)) {
		result_n(text + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
		if (!all)
			break;
		from = (size_t)m[0].rm_eo;
		if (m[0].rm_eo == m[0].rm_so) {
			if (from >= len)
				break;
			from += (size_t)letter_length(text + from);
		}
	}
	free(m);
	return nres;
}

/* the parts of the first match, in its ( )s: how many (0 if no match;
 * a part that matched nothing is "") */
int64_t cby_patterns_parts(int64_t id, const char *text)
{
	Pattern *pt = pattern(id);
	results_clear();
	Span *m = malloc(sizeof *m * (pt->groups + 1));
	if (search(pt, text, strlen(text), 0, m))
		for (size_t g = 1; g <= pt->groups; g++) {
			if (m[g].rm_so >= 0)
				result_n(text + m[g].rm_so, (size_t)(m[g].rm_eo - m[g].rm_so));
			else
				result_n("", 0);
		}
	free(m);
	return nres;
}

/* the number of the part called name: 0 if there's none */
int64_t cby_patterns_part_number(int64_t id, const char *name, int needed)
{
	Pattern *pt = pattern(id);
	int g = part_named(pt, name, strlen(name));
	if (!g && needed)
		rt_panic(NULL, 0, 0, "the pattern '%s' has no part named %s", pt->source, name);
	return g;
}

/* the parts' names, by number ("" for a part without one): how many */
int64_t cby_patterns_names(int64_t id)
{
	Pattern *pt = pattern(id);
	results_clear();
	for (size_t g = 1; g <= pt->groups; g++) {
		const char *name = pt->names && pt->names[g] ? pt->names[g] : "";
		result_n(name, strlen(name));
	}
	return nres;
}

/* the text with the first match (or every one) replaced by With, in which
 * \1 to \9 are the match's parts, \0 the whole match, and \\ a \ */
int64_t cby_patterns_replace(int64_t id, const char *text, const char *with, int all)
{
	Pattern *pt = pattern(id);
	results_clear();
	Span *m = malloc(sizeof *m * (pt->groups + 1));
	Text out = { 0 };
	put(&out, "");
	size_t len = strlen(text), from = 0, copied = 0;
	while (from <= len && search(pt, text, len, from, m)) {
		put_n(&out, text + copied, (size_t)m[0].rm_so - copied);
		for (const char *w = with; *w; w++) {
			if (*w == '\\' && w[1] >= '0' && w[1] <= '9') {
				size_t g = (size_t)(w[1] - '0');
				if (g <= pt->groups && m[g].rm_so >= 0)
					put_n(&out, text + m[g].rm_so, (size_t)(m[g].rm_eo - m[g].rm_so));
				w++;
			} else if (*w == '\\' && w[1] == 'k' && w[2] == '<' && strchr(w + 3, '>')) {
				const char *e = strchr(w + 3, '>');
				int g = part_named(pt, w + 3, (size_t)(e - w - 3));
				if (!g) {
					char why[160];
					snprintf(why, sizeof why, "has no part named %.*s, for the \\k<%.*s> in what replaces it",
						 (int)(e - w - 3), w + 3, (int)(e - w - 3), w + 3);
					bad(pt->source, why);
				}
				if (m[g].rm_so >= 0)
					put_n(&out, text + m[g].rm_so, (size_t)(m[g].rm_eo - m[g].rm_so));
				w = e;
			} else if (*w == '\\' && w[1] == '\\') {
				put_n(&out, "\\", 1);
				w++;
			} else {
				put_n(&out, w, 1);
			}
		}
		copied = (size_t)m[0].rm_eo;
		from = copied;
		if (m[0].rm_eo == m[0].rm_so) {
			if (from >= len)
				break;
			int l = letter_length(text + from);
			put_n(&out, text + from, (size_t)l);   /* (the letter an empty match was before) */
			from += (size_t)l;
			copied = from;
		}
		if (!all)
			break;
	}
	if (copied < len)
		put_n(&out, text + copied, len - copied);
	result_n(out.s, out.n);
	free(out.s);
	free(m);
	return 1;
}

/* the text in pieces, cut at each match (the matches themselves go) */
int64_t cby_patterns_split(int64_t id, const char *text)
{
	Pattern *pt = pattern(id);
	results_clear();
	Span *m = malloc(sizeof *m * (pt->groups + 1));
	size_t len = strlen(text), from = 0, start = 0;
	while (from < len && search(pt, text, len, from, m)) {
		if (m[0].rm_eo == m[0].rm_so) {   /* (an empty match cuts nothing) */
			from = (size_t)m[0].rm_eo + (size_t)letter_length(text + m[0].rm_eo);
			continue;
		}
		result_n(text + start, (size_t)m[0].rm_so - start);
		start = from = (size_t)m[0].rm_eo;
	}
	result_n(text + start, len - start);
	free(m);
	return nres;
}

/* everything kept, given back at the end (for valgrind) */
__attribute__((destructor)) static void patterns_end(void)
{
	for (int i = 0; i < npats; i++)
		cby_patterns_free(i + 1);
	free(pats);
	results_clear();
	free(res);
	free(reslen);
	if (utf8())
		freelocale(utf8());
}

/* a worker's thread, ending: what it kept for itself */
static void patterns_thread_end(void)
{
	results_clear();
	free(res);
	free(reslen);
	res = NULL;
	reslen = NULL;
	capres = 0;
}
__attribute__((constructor)) static void patterns_threads(void) { rt_on_thread_end(patterns_thread_end); }
