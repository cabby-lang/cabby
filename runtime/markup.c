/* markup.c — the C side of the html and xml libraries (lib/markup.hby,
 * lib/html.hby, lib/xml.hby). Part of Cabby's runtime, so the interpreter
 * and compiled programs both have it.
 *
 * A page or document is a tree of nodes: elements (a tag, attributes and
 * what's inside), text, and comments. Cabby's `element` object holds one
 * node (as a number); nodes are counted, so an element found inside a page
 * shares its node with the page, and each node is freed when nothing holds
 * it any more.
 *
 * html.read forgives what browsers forgive (tags left open, <br> with no
 * end, capitals in names); xml.read is strict and says where XML is wrong.
 * Elements are found with CSS selectors: a, .price, #prices, li.price,
 * ul li, ul > li, a[href], a[href="x"], several with commas. */
#include "rt.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <setjmp.h>

enum { M_DOC, M_ELEM, M_TEXT, M_COMMENT, M_MISSING };

typedef struct Node Node;
struct Node {
	int kind;
	bool html;             /* written and matched by HTML's rules (else XML's) */
	int64_t refs;
	char *tag;             /* M_ELEM: the name (lower case in HTML) */
	char *text;            /* M_TEXT / M_COMMENT: the text; M_MISSING: what wasn't found */
	char **names, **values;/* attributes, in order */
	int nattrs, capattrs;
	Node **kids;
	int64_t nkids, capkids;
	Node *parent;          /* not counted: a parent holds its children */
};

/* ---- nodes */

static Node *node_new(int kind, bool html)
{
	Node *n = rt_alloc_zeroed(sizeof *n);
	n->kind = kind;
	n->html = html;
	n->refs = 1;
	return n;
}

static char *dup_n(const char *s, size_t len)
{
	char *p = rt_alloc(len + 1);
	memcpy(p, s, len);
	p[len] = '\0';
	return p;
}

static char *dup_s(const char *s) { return dup_n(s, strlen(s)); }

static void node_release(Node *n)
{
	if (!n || --n->refs > 0)
		return;
	for (int64_t i = 0; i < n->nkids; i++) {
		if (n->kids[i]->parent == n)
			n->kids[i]->parent = NULL;
		node_release(n->kids[i]);
	}
	for (int i = 0; i < n->nattrs; i++) {
		free(n->names[i]);
		free(n->values[i]);
	}
	free(n->names);
	free(n->values);
	free(n->kids);
	free(n->tag);
	free(n->text);
	free(n);
}

static void add_kid(Node *p, Node *k)   /* p takes over the caller's count on k */
{
	if (p->nkids == p->capkids) {
		p->capkids = p->capkids ? p->capkids * 2 : 4;
		p->kids = rt_realloc(p->kids, sizeof *p->kids * (size_t)p->capkids);
	}
	p->kids[p->nkids++] = k;
	k->parent = p;
}

static const char *attr_get(const Node *n, const char *name)
{
	for (int i = 0; i < n->nattrs; i++)
		if (n->html ? strcasecmp(n->names[i], name) == 0 : strcmp(n->names[i], name) == 0)
			return n->values[i];
	return NULL;
}

static void attr_set(Node *n, const char *name, size_t nl, const char *value, size_t vl)
{
	for (int i = 0; i < n->nattrs; i++)
		if (strlen(n->names[i]) == nl && memcmp(n->names[i], name, nl) == 0) {
			free(n->values[i]);
			n->values[i] = dup_n(value, vl);
			return;
		}
	if (n->nattrs == n->capattrs) {
		n->capattrs = n->capattrs ? n->capattrs * 2 : 4;
		n->names = rt_realloc(n->names, sizeof *n->names * (size_t)n->capattrs);
		n->values = rt_realloc(n->values, sizeof *n->values * (size_t)n->capattrs);
	}
	n->names[n->nattrs] = dup_n(name, nl);
	n->values[n->nattrs++] = dup_n(value, vl);
}

/* ---- text being built (for parsing, and for handing to Cabby) */

typedef struct { char *s; size_t n, cap; } Buf;

static void buf_put(Buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 256;
		while (cap < b->n + n + 1)
			cap *= 2;
		b->s = rt_realloc(b->s, cap);
		b->cap = cap;
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = '\0';
}

static void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }

static void buf_utf8(Buf *b, unsigned long c)
{
	char u[4];
	size_t n;
	if (c < 0x80) { u[0] = (char)c; n = 1; }
	else if (c < 0x800) { u[0] = (char)(0xC0 | (c >> 6)); u[1] = (char)(0x80 | (c & 0x3F)); n = 2; }
	else if (c < 0x10000) { u[0] = (char)(0xE0 | (c >> 12)); u[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		u[2] = (char)(0x80 | (c & 0x3F)); n = 3; }
	else { u[0] = (char)(0xF0 | (c >> 18)); u[1] = (char)(0x80 | ((c >> 12) & 0x3F));
		u[2] = (char)(0x80 | ((c >> 6) & 0x3F)); u[3] = (char)(0x80 | (c & 0x3F)); n = 4; }
	buf_put(b, u, n);
}

/* what Cabby takes next (cby_markup_take) */
static Buf out;   /* (not per thread: kept until the program ends, as the next text reuses it) */

/* ---- reading: shared by HTML and XML */

typedef struct {
	const char *src, *p;
	bool html;
	Buf text;              /* text waiting to become a node */
} Reader;

static void where_of(const Reader *r, const char *at, int *line, int *col)
{
	*line = 1;
	*col = 1;
	for (const char *q = r->src; q < at; q++) {
		if (*q == '\n') {
			(*line)++;
			*col = 1;
		} else if (((unsigned char)*q & 0xC0) != 0x80) {
			(*col)++;
		}
	}
}

/* xml.can_read: a mistake jumps back here instead of panicking, and what
 * was half read is given back */
static _Thread_local jmp_buf *quiet;
static _Thread_local struct { struct Node *doc; void *stack, *opened; char *text, *tag; } reading;

static _Noreturn void xml_error(const Reader *r, const char *at, const char *fmt, const char *arg)
{
	if (quiet) {
		reading.text = r->text.s;
		longjmp(*quiet, 1);
	}
	int line, col;
	where_of(r, at, &line, &col);
	char msg[300];
	snprintf(msg, sizeof msg, fmt, arg ? arg : "");
	rt_panic(NULL, 0, 0, "this isn't XML: line %d, column %d: %s", line, col, msg);
}

static const struct { const char *name; unsigned long code; } entities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
	{ "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "hellip", 0x2026 }, { "mdash", 0x2014 },
	{ "ndash", 0x2013 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
	{ "bull", 0x2022 }, { "middot", 0xB7 }, { "deg", 0xB0 }, { "euro", 0x20AC }, { "pound", 0xA3 },
	{ "yen", 0xA5 }, { "cent", 0xA2 }, { "times", 0xD7 }, { "divide", 0xF7 }, { "plusmn", 0xB1 },
	{ "laquo", 0xAB }, { "raquo", 0xBB }, { "sect", 0xA7 }, { "para", 0xB6 }, { "frac12", 0xBD },
	{ "frac14", 0xBC }, { "frac34", 0xBE }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "eacute", 0xE9 },
	{ "egrave", 0xE8 }, { "aacute", 0xE1 }, { "agrave", 0xE0 }, { "ouml", 0xF6 }, { "uuml", 0xFC },
	{ "auml", 0xE4 }, { "szlig", 0xDF }, { "ccedil", 0xE7 }, { "ntilde", 0xF1 }, { "larr", 0x2190 },
	{ "rarr", 0x2192 }, { "uarr", 0x2191 }, { "darr", 0x2193 }, { "hearts", 0x2665 }, { "check", 0x2713 },
};

/* text up to `end` (or a '<'), with &entities; turned into characters */
static void read_text(Reader *r, Buf *b, const char *end)
{
	while (*r->p && (end ? r->p < end : *r->p != '<')) {
		if (*r->p != '&') {
			buf_put(b, r->p++, 1);
			continue;
		}
		const char *semi = r->p + 1;
		while (*semi && *semi != ';' && semi - r->p < 12 && (isalnum((unsigned char)*semi) || *semi == '#'))
			semi++;
		bool done = false;
		if (*semi == ';') {
			size_t nl = (size_t)(semi - r->p - 1);
			const char *name = r->p + 1;
			if (nl > 1 && name[0] == '#') {
				unsigned long c = name[1] == 'x' || name[1] == 'X' ? strtoul(name + 2, NULL, 16)
										 : strtoul(name + 1, NULL, 10);
				if (c > 0 && c < 0x110000) {
					buf_utf8(b, c);
					done = true;
				}
			} else {
				for (size_t i = 0; i < sizeof entities / sizeof *entities; i++)
					if (strlen(entities[i].name) == nl && memcmp(entities[i].name, name, nl) == 0) {
						buf_utf8(b, entities[i].code);
						done = true;
						break;
					}
			}
			if (done)
				r->p = semi + 1;
			else if (!r->html)
				xml_error(r, r->p, "unknown entity '%s' (XML knows &amp; &lt; &gt; &quot; &apos; and &#numbers;)",
					  dup_n(name, nl));
		} else if (!r->html) {
			xml_error(r, r->p, "a '&' must start an entity such as &amp;%s", NULL);
		}
		if (!done)
			buf_put(b, r->p++, 1);   /* HTML: a lone & is just a & */
	}
}

static bool name_char(char c) { return isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':' || c == '.'; }

static void skip_space(Reader *r)
{
	while (isspace((unsigned char)*r->p))
		r->p++;
}

static void flush_text(Reader *r, Node *into)
{
	if (!r->text.n)
		return;
	Node *t = node_new(M_TEXT, r->html);
	t->text = dup_n(r->text.s, r->text.n);
	add_kid(into, t);
	r->text.n = 0;
}

/* <name attr="v" attr2 ...> or ... />: fills n's attributes; true for "/>" */
static bool read_attributes(Reader *r, Node *n)
{
	for (;;) {
		skip_space(r);
		if (*r->p == '>') {
			r->p++;
			return false;
		}
		if (r->p[0] == '/' && r->p[1] == '>') {
			r->p += 2;
			return true;
		}
		if (!*r->p) {
			if (r->html)
				return false;
			xml_error(r, r->p, "the text ends inside a tag%s", NULL);
		}
		const char *ns = r->p;
		while (*r->p && !isspace((unsigned char)*r->p) && *r->p != '=' && *r->p != '>' &&
		       !(r->p[0] == '/' && r->p[1] == '>'))
			r->p++;
		size_t nl = (size_t)(r->p - ns);
		if (!nl) {
			if (!r->html)
				xml_error(r, r->p, "expected an attribute's name%s", NULL);
			r->p++;
			continue;
		}
		char *name = dup_n(ns, nl);
		if (r->html)
			for (char *c = name; *c; c++)
				*c = (char)tolower((unsigned char)*c);
		skip_space(r);
		Buf v = { 0 };
		buf_put(&v, "", 0);
		if (*r->p == '=') {
			r->p++;
			skip_space(r);
			char q = *r->p;
			if (q == '"' || q == '\'') {
				const char *end = strchr(r->p + 1, q);
				if (!end) {
					if (!r->html)
						xml_error(r, r->p, "this attribute's quote is never closed%s", NULL);
					end = r->p + strlen(r->p);
				}
				r->p++;
				read_text(r, &v, end);
				r->p = *end ? end + 1 : end;
			} else {
				if (!r->html)
					xml_error(r, r->p, "an attribute's value goes in quotes: %s=\"...\"", name);
				const char *s = r->p;
				while (*r->p && !isspace((unsigned char)*r->p) && *r->p != '>')
					r->p++;
				Reader sub = *r;
				sub.p = s;
				read_text(&sub, &v, r->p);
			}
		} else if (!r->html) {
			xml_error(r, r->p, "attribute '%s' needs a value: name=\"value\"", name);
		}
		attr_set(n, name, strlen(name), v.s, v.n);
		free(name);
		free(v.s);
	}
}

/* ---- HTML's rules */

static bool in_list(const char *s, const char *const *list)
{
	for (; *list; list++)
		if (strcmp(s, *list) == 0)
			return true;
	return false;
}

static const char *const void_tags[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link",
					 "meta", "param", "source", "track", "wbr", NULL };
static const char *const raw_tags[] = { "script", "style", NULL };          /* nothing inside is markup */
static const char *const rcdata_tags[] = { "title", "textarea", NULL };   /* only &entities; */
static const char *const closes_p[] = { "address", "article", "aside", "blockquote", "details", "div", "dl",
					"fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4",
					"h5", "h6", "header", "hr", "main", "menu", "nav", "ol", "p", "pre", "section",
					"table", "ul", NULL };
static const char *const block_tags[] = { "address", "article", "aside", "blockquote", "br", "dd", "details",
					  "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form",
					  "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li", "main", "nav",
					  "ol", "p", "pre", "section", "table", "td", "th", "tr", "ul", "title",
					  "option", "body", "head", "html", NULL };

/* opening `tag`: an element still open that it ends (a <p> by a <div>, an
 * <li> by the next <li>), as browsers do */
static void html_implied_close(Node ***stack, int *depth, const char *tag)
{
	Node **s = *stack;
	if (in_list(tag, closes_p) && *depth > 1 && strcmp(s[*depth - 1]->tag, "p") == 0)
		(*depth)--;
	struct { const char *tag; const char *const *ends; const char *const *stops; } rules[] = {
		{ "li", (const char *const[]){ "li", NULL }, (const char *const[]){ "ul", "ol", "menu", NULL } },
		{ "dt", (const char *const[]){ "dt", "dd", NULL }, (const char *const[]){ "dl", NULL } },
		{ "dd", (const char *const[]){ "dt", "dd", NULL }, (const char *const[]){ "dl", NULL } },
		{ "tr", (const char *const[]){ "tr", "td", "th", NULL }, (const char *const[]){ "table", "thead", "tbody", "tfoot", NULL } },
		{ "td", (const char *const[]){ "td", "th", NULL }, (const char *const[]){ "tr", "table", NULL } },
		{ "th", (const char *const[]){ "td", "th", NULL }, (const char *const[]){ "tr", "table", NULL } },
		{ "thead", (const char *const[]){ "thead", "tbody", "tfoot", "tr", "td", "th", NULL }, (const char *const[]){ "table", NULL } },
		{ "tbody", (const char *const[]){ "thead", "tbody", "tfoot", "tr", "td", "th", NULL }, (const char *const[]){ "table", NULL } },
		{ "tfoot", (const char *const[]){ "thead", "tbody", "tfoot", "tr", "td", "th", NULL }, (const char *const[]){ "table", NULL } },
		{ "option", (const char *const[]){ "option", NULL }, (const char *const[]){ "select", "datalist", NULL } },
	};
	for (size_t i = 0; i < sizeof rules / sizeof *rules; i++) {
		if (strcmp(tag, rules[i].tag) != 0)
			continue;
		/* the outermost one it ends, before reaching where it belongs (a <tr>
		 * ends the <td> and the <tr> before it, not a <tr> in another table) */
		int to = -1;
		for (int k = *depth - 1; k >= 1; k--) {
			if (in_list(s[k]->tag, rules[i].stops))
				break;
			if (in_list(s[k]->tag, rules[i].ends))
				to = k;
		}
		if (to > 0)
			*depth = to;
	}
}

/* ---- the reader */

static Node *parse(const char *text, bool html)
{
	Reader r = { text, text, html, { 0 } };
	Node *doc = node_new(M_DOC, html);
	int depth = 1, cap = 16;
	Node **stack = rt_alloc(sizeof *stack * (size_t)cap);
	const char **opened = rt_alloc(sizeof *opened * (size_t)cap);   /* XML: where each was opened */
	stack[0] = doc;
	opened[0] = text;
	reading.doc = doc;
	reading.stack = stack;
	reading.opened = opened;
	int roots = 0;
	while (*r.p) {
		Node *cur = stack[depth - 1];
		if (*r.p != '<') {
			const char *s = r.p;
			read_text(&r, &r.text, NULL);
			if (!html && depth == 1) {
				bool blank = true;
				for (size_t i = 0; i < r.text.n; i++)
					blank = blank && isspace((unsigned char)r.text.s[i]);
				if (!blank)
					xml_error(&r, s, "text outside the top element%s", NULL);
				r.text.n = 0;
			}
			continue;
		}
		/* <!-- comment --> */
		if (strncmp(r.p, "<!--", 4) == 0) {
			flush_text(&r, cur);
			const char *end = strstr(r.p + 4, "-->");
			if (!end && !html)
				xml_error(&r, r.p, "this comment is never closed with -->%s", NULL);
			Node *c = node_new(M_COMMENT, html);
			c->text = dup_n(r.p + 4, end ? (size_t)(end - r.p - 4) : strlen(r.p + 4));
			add_kid(cur, c);
			r.p = end ? end + 3 : r.p + strlen(r.p);
			continue;
		}
		/* <![CDATA[ text ]]> */
		if (strncmp(r.p, "<![CDATA[", 9) == 0) {
			const char *end = strstr(r.p + 9, "]]>");
			if (!end && !html)
				xml_error(&r, r.p, "this CDATA section is never closed with ]]>%s", NULL);
			buf_put(&r.text, r.p + 9, end ? (size_t)(end - r.p - 9) : strlen(r.p + 9));
			r.p = end ? end + 3 : r.p + strlen(r.p);
			continue;
		}
		/* <!DOCTYPE ...>, <?xml ...?>: noted, not kept */
		if (r.p[1] == '!' || r.p[1] == '?') {
			const char *end = r.p[1] == '?' ? strstr(r.p, "?>") : strchr(r.p, '>');
			r.p = end ? end + (r.p[1] == '?' ? 2 : 1) : r.p + strlen(r.p);
			continue;
		}
		/* </end> */
		if (r.p[1] == '/') {
			const char *s = r.p + 2, *e = s;
			while (name_char(*e))
				e++;
			char *tag = dup_n(s, (size_t)(e - s));
			reading.tag = tag;
			if (html)
				for (char *c = tag; *c; c++)
					*c = (char)tolower((unsigned char)*c);
			const char *gt = strchr(e, '>');
			flush_text(&r, cur);
			if (!html) {
				if (depth == 1)
					xml_error(&r, r.p, "</%s> closes nothing: no element is open", tag);
				if (strcmp(cur->tag, tag) != 0) {
					int line, col;
					where_of(&r, opened[depth - 1], &line, &col);
					char msg[200];
					snprintf(msg, sizeof msg, "expected </%s> (opened at line %d), found </%%s>", cur->tag, line);
					xml_error(&r, r.p, msg, tag);
				}
				depth--;
			} else {
				for (int k = depth - 1; k >= 1; k--)
					if (strcmp(stack[k]->tag, tag) == 0) {
						depth = k;   /* closes it, and anything left open inside it */
						break;
					}
				/* an end with nothing to end is ignored, as browsers do */
			}
			free(tag);
			reading.tag = NULL;
			r.p = gt ? gt + 1 : r.p + strlen(r.p);
			continue;
		}
		/* <start ...> */
		if (!name_char(r.p[1]) || isdigit((unsigned char)r.p[1])) {
			if (!html)
				xml_error(&r, r.p, "a '<' must start a tag (write &lt; for the character)%s", NULL);
			buf_put(&r.text, r.p++, 1);
			continue;
		}
		flush_text(&r, cur);
		const char *s = r.p + 1, *e = s;
		while (name_char(*e))
			e++;
		Node *el = node_new(M_ELEM, html);
		el->tag = dup_n(s, (size_t)(e - s));
		if (html)
			for (char *c = el->tag; *c; c++)
				*c = (char)tolower((unsigned char)*c);
		const char *at = r.p;
		r.p = e;
		bool self_closed = read_attributes(&r, el);
		if (html) {
			html_implied_close(&stack, &depth, el->tag);
			cur = stack[depth - 1];
		} else if (depth == 1 && roots++) {
			xml_error(&r, at, "a second top element, <%s>: XML has one, holding everything", el->tag);
		}
		add_kid(cur, el);
		if (self_closed || (html && in_list(el->tag, void_tags)))
			continue;
		if (html && (in_list(el->tag, raw_tags) || in_list(el->tag, rcdata_tags))) {
			/* everything up to </script> is its text */
			char close[64];
			snprintf(close, sizeof close, "</%s", el->tag);
			const char *end = r.p;
			while (*end && strncasecmp(end, close, strlen(close)) != 0)
				end++;
			Buf t = { 0 };
			if (in_list(el->tag, raw_tags))
				buf_put(&t, r.p, (size_t)(end - r.p));
			else
				read_text(&r, &t, end);
			if (t.n) {
				Node *tn = node_new(M_TEXT, html);
				tn->text = dup_n(t.s, t.n);
				add_kid(el, tn);
			}
			free(t.s);
			const char *gt = *end ? strchr(end, '>') : NULL;
			r.p = gt ? gt + 1 : end;
			continue;
		}
		if (depth == cap) {
			cap *= 2;
			stack = rt_realloc(stack, sizeof *stack * (size_t)cap);
			opened = rt_realloc(opened, sizeof *opened * (size_t)cap);
			reading.stack = stack;
			reading.opened = opened;
		}
		opened[depth] = at;
		stack[depth++] = el;
	}
	flush_text(&r, stack[depth - 1]);
	if (!html) {
		if (depth > 1) {
			int line, col;
			where_of(&r, opened[depth - 1], &line, &col);
			char msg[200];
			snprintf(msg, sizeof msg, "the text ends with <%s> (opened at line %d) still open", stack[depth - 1]->tag, line);
			xml_error(&r, r.p, "%s", msg);
		}
		if (!roots)
			xml_error(&r, r.p, "there's no element: XML needs one, holding everything%s", NULL);
	}
	free(stack);
	free(opened);
	free(r.text.s);
	if (!html) {
		/* an XML document is its top element */
		Node *top = NULL;
		for (int64_t i = 0; i < doc->nkids; i++)
			if (doc->kids[i]->kind == M_ELEM)
				top = doc->kids[i];
		top->refs++;
		node_release(doc);
		top->parent = NULL;
		return top;
	}
	return doc;
}

/* ---- handles */

#define N(h) ((Node *)(intptr_t)(h))

static _Noreturn void missing_panic(const Node *n) { rt_panic(NULL, 0, 0, "%s", n->text); }

static Node *want(int64_t h)
{
	Node *n = N(h);
	if (n->kind == M_MISSING)
		missing_panic(n);
	return n;
}

int64_t cby_markup_read(const char *text, int64_t html) { return (int64_t)(intptr_t)parse(text, html != 0); }

int64_t cby_markup_new(const char *tag, int64_t html)
{
	Node *n = node_new(M_ELEM, html != 0);
	n->tag = dup_s(tag);
	if (html)
		for (char *c = n->tag; *c; c++)
			*c = (char)tolower((unsigned char)*c);
	return (int64_t)(intptr_t)n;
}

void cby_markup_release(int64_t h) { node_release(N(h)); }
int cby_markup_is_missing(int64_t h) { return N(h)->kind == M_MISSING; }

/* a short description, for messages: <li class="price"> */
static void describe(const Node *n, char *b, size_t size)
{
	if (n->kind == M_DOC) {
		snprintf(b, size, "the page");
		return;
	}
	const char *cls = attr_get(n, "class"), *id = attr_get(n, "id");
	if (id)
		snprintf(b, size, "<%s id=\"%s\">", n->tag, id);
	else if (cls)
		snprintf(b, size, "<%s class=\"%s\">", n->tag, cls);
	else
		snprintf(b, size, "<%s>", n->tag);
}

static int64_t missing(const char *fmt, const char *a, const char *b)
{
	Node *m = node_new(M_MISSING, false);
	char msg[400];
	snprintf(msg, sizeof msg, fmt, a, b);
	m->text = dup_s(msg);
	return (int64_t)(intptr_t)m;
}

/* ---- CSS selectors */

/* A selector is chains of parts (a list of them, between commas): each
 * part is one element's tag, id, classes, attributes and pseudo-classes
 * (li.price:first-child), joined to the part before it by how the two
 * elements stand: inside it (a space), directly inside (>), straight
 * after it (+), or after it at all (~). */

typedef struct Chain Chain;

enum {
	P_FIRST_CHILD, P_LAST_CHILD, P_ONLY_CHILD, P_NTH_CHILD, P_NTH_LAST_CHILD,
	P_FIRST_OF_TYPE, P_LAST_OF_TYPE, P_ONLY_OF_TYPE, P_NTH_OF_TYPE, P_NTH_LAST_OF_TYPE,
	P_EMPTY, P_ROOT, P_NOT, P_IS, P_HAS, P_CHECKED, P_DISABLED, P_ENABLED,
};

typedef struct {
	int kind;
	int64_t a, b;          /* :nth-child(an+b) */
	Chain *sub;            /* :not( ), :is( ), :has( ): a selector of its own */
	int nsub;
} Pseudo;

typedef struct {
	char *tag;             /* NULL or "*": any */
	char *id;
	char **classes;
	int nclasses;
	char **anames, **avalues;   /* [name] (value NULL) or [name=value] */
	char *aops;            /* how each is compared: = ^ $ * ~ | (0: just there) */
	int nattrs;
	Pseudo *pseudos;
	int npseudos;
	char comb;             /* how it joins the one before it: ' ' inside, '>' directly inside,
				  '+' straight after, '~' after; 0 first */
} Part;

struct Chain {
	Part *parts;
	int nparts;
};

static void free_chains(Chain *c, int n)
{
	for (int i = 0; i < n; i++) {
		for (int k = 0; k < c[i].nparts; k++) {
			Part *p = &c[i].parts[k];
			free(p->tag);
			free(p->id);
			for (int j = 0; j < p->nclasses; j++)
				free(p->classes[j]);
			for (int j = 0; j < p->nattrs; j++) {
				free(p->anames[j]);
				free(p->avalues[j]);
			}
			for (int j = 0; j < p->npseudos; j++)
				if (p->pseudos[j].sub)
					free_chains(p->pseudos[j].sub, p->pseudos[j].nsub);
			free(p->classes);
			free(p->anames);
			free(p->avalues);
			free(p->aops);
			free(p->pseudos);
		}
		free(c[i].parts);
	}
	free(c);
}

static _Noreturn void selector_error(const char *sel, const char *at, const char *what)
{
	rt_panic(NULL, 0, 0, "can't understand the selector \"%s\" at \"%s\": %s", sel, at, what);
}

static bool sel_char(char c) { return isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':' || (unsigned char)c >= 0x80; }

static const struct { const char *name; int kind; bool takes; } pseudo_classes[] = {
	{ "first-child", P_FIRST_CHILD, false }, { "last-child", P_LAST_CHILD, false },
	{ "only-child", P_ONLY_CHILD, false }, { "nth-child", P_NTH_CHILD, true },
	{ "nth-last-child", P_NTH_LAST_CHILD, true }, { "first-of-type", P_FIRST_OF_TYPE, false },
	{ "last-of-type", P_LAST_OF_TYPE, false }, { "only-of-type", P_ONLY_OF_TYPE, false },
	{ "nth-of-type", P_NTH_OF_TYPE, true }, { "nth-last-of-type", P_NTH_LAST_OF_TYPE, true },
	{ "empty", P_EMPTY, false }, { "root", P_ROOT, false }, { "not", P_NOT, true }, { "is", P_IS, true },
	{ "where", P_IS, true }, { "has", P_HAS, true }, { "checked", P_CHECKED, false },
	{ "disabled", P_DISABLED, false }, { "enabled", P_ENABLED, false },
};

/* the pseudo-class whose name starts at s (after its ':'), or -1 */
static int pseudo_at(const char *s, size_t *len)
{
	size_t n = 0;
	while (isalnum((unsigned char)s[n]) || s[n] == '-')
		n++;
	for (size_t i = 0; i < sizeof pseudo_classes / sizeof pseudo_classes[0]; i++)
		if (strlen(pseudo_classes[i].name) == n && strncmp(pseudo_classes[i].name, s, n) == 0) {
			*len = n;
			return (int)i;
		}
	return -1;
}

/* where a name (a tag, class or id) ends: at the first character that
 * can't be in one, or a ':' that starts a pseudo-class (a ':' can be in
 * an XML name: dc:creator) */
static const char *name_end(const char *p)
{
	size_t len;
	while (sel_char(*p) && !(*p == ':' && pseudo_at(p + 1, &len) >= 0))
		p++;
	return p;
}

/* an+b, odd, even, 3, -n+3 */
static void read_nth(const char *sel, const char *arg, size_t n, int64_t *a, int64_t *b)
{
	char t[64];
	size_t k = 0;
	for (size_t i = 0; i < n && k < sizeof t - 1; i++)
		if (!isspace((unsigned char)arg[i]))
			t[k++] = (char)tolower((unsigned char)arg[i]);
	t[k] = 0;
	if (strcmp(t, "odd") == 0) { *a = 2; *b = 1; return; }
	if (strcmp(t, "even") == 0) { *a = 2; *b = 0; return; }
	char *e;
	char *np = strchr(t, 'n');
	if (!np) {
		*a = 0;
		*b = strtoll(t, &e, 10);
		if (e == t || *e)
			selector_error(sel, arg, "expected a number, odd, even or a form like 2n+1 in ( )");
		return;
	}
	if (np == t) *a = 1;
	else if (np == t + 1 && t[0] == '-') *a = -1;
	else if (np == t + 1 && t[0] == '+') *a = 1;
	else {
		*a = strtoll(t, &e, 10);
		if (e != np)
			selector_error(sel, arg, "expected a form like 2n+1 in ( )");
	}
	*b = 0;
	if (np[1]) {
		*b = strtoll(np + 1, &e, 10);
		if (e == np + 1 || *e || (np[1] != '+' && np[1] != '-'))
			selector_error(sel, arg, "expected a form like 2n+1 in ( )");
	}
}

static Chain *parse_selector(const char *sel, int *nchains);
static _Thread_local bool parsing_html;   /* (a ':' in a tag name is only XML's: dc:creator) */
static Chain *parse_list(const char *sel, const char **pp, int *nchains, char stop);

/* :name or :name( ), from just after the ':' */
static const char *parse_pseudo(const char *sel, const char *p, Part *part)
{
	size_t len;
	int i = pseudo_at(p, &len);
	if (i < 0) {
		char why[120];
		size_t n = strcspn(p, " ()[].#>+~,:");
		snprintf(why, sizeof why, "':%.*s' isn't a pseudo-class Cabby knows (html.md lists them)", (int)(n > 40 ? 40 : n), p);
		selector_error(sel, p - 1, why);
	}
	Pseudo ps = { pseudo_classes[i].kind, 0, 0, NULL, 0 };
	p += len;
	if (pseudo_classes[i].takes) {
		if (*p != '(')
			selector_error(sel, p, "this pseudo-class needs something in ( ) after it");
		p++;
		if (ps.kind == P_NOT || ps.kind == P_IS || ps.kind == P_HAS) {
			ps.sub = parse_list(sel, &p, &ps.nsub, ')');
		} else {
			const char *arg = p;
			while (*p && *p != ')')
				p++;
			read_nth(sel, arg, (size_t)(p - arg), &ps.a, &ps.b);
		}
		if (*p != ')')
			selector_error(sel, p, "expected ')'");
		p++;
	} else if (*p == '(') {
		selector_error(sel, p, "this pseudo-class takes nothing in ( )");
	}
	part->pseudos = rt_realloc(part->pseudos, sizeof *part->pseudos * (size_t)(part->npseudos + 1));
	part->pseudos[part->npseudos++] = ps;
	return p;
}

/* selectors separated by commas, up to `stop` (or the end, for 0); for
 * :has( ), each may start with > + or ~, how it stands to the element */
static Chain *parse_list(const char *sel, const char **pp, int *nchains, char stop)
{
	Chain *chains = NULL;
	int n = 0;
	const char *p = *pp;
	for (;;) {
		Chain c = { 0 };
		char comb = 0;
		while (isspace((unsigned char)*p))
			p++;
		if (stop && (*p == '>' || *p == '+' || *p == '~')) {
			comb = *p++;   /* (:has(> img): directly inside) */
			while (isspace((unsigned char)*p))
				p++;
		}
		for (;;) {
			Part part = { 0 };
			part.comb = comb;
			const char *start = p;
			if (*p == '*') {
				p++;
			} else if (sel_char(*p) && *p != ':') {
				const char *s = p;
				p = name_end(p);
				part.tag = dup_n(s, (size_t)(p - s));
				const char *colon = strchr(part.tag, ':');
				static const char *const shown[] = { "hover", "focus", "active", "visited", "link", "target",
								     "focus-within", "focus-visible", "before", "after",
								     "first-line", "first-letter", "placeholder", NULL };
				if (colon && parsing_html && !in_list(colon + 1 + (colon[1] == ':'), shown)) {
					char why[120];
					snprintf(why, sizeof why, "':%.40s' isn't a pseudo-class Cabby knows (html.md lists them)", colon + 1);
					selector_error(sel, s + (colon - part.tag), why);
				}
				if (colon && in_list(colon + 1 + (colon[1] == ':'), shown))
					selector_error(sel, s + (colon - part.tag), "this pseudo-class is about a page being shown "
						       "and used in a browser (where the mouse is, what's been visited), which "
						       "a page read by Cabby never is");
			}
			for (;;) {
				if (*p == '.' || *p == '#') {
					char kind = *p++;
					const char *s = p;
					p = name_end(p);
					if (p == s)
						selector_error(sel, s - 1, kind == '.' ? "a '.' needs a class name after it" : "a '#' needs an id after it");
					if (kind == '#') {
						free(part.id);
						part.id = dup_n(s, (size_t)(p - s));
					} else {
						part.classes = rt_realloc(part.classes, sizeof *part.classes * (size_t)(part.nclasses + 1));
						part.classes[part.nclasses++] = dup_n(s, (size_t)(p - s));
					}
				} else if (*p == ':') {
					p = parse_pseudo(sel, p + 1, &part);
				} else if (*p == '[') {
					p++;
					while (isspace((unsigned char)*p))
						p++;
					const char *s = p;
					while (sel_char(*p))
						p++;
					if (p == s)
						selector_error(sel, s, "'[' needs an attribute's name after it");
					char *name = dup_n(s, (size_t)(p - s)), *value = NULL;
					char op = 0;
					while (isspace((unsigned char)*p))
						p++;
					if (strchr("^$*~|", *p) && *p && p[1] == '=') {
						op = *p;
						p++;
					}
					if (*p == '=') {
						if (!op)
							op = '=';
						p++;
						while (isspace((unsigned char)*p))
							p++;
						char q = *p;
						if (q == '"' || q == '\'') {
							const char *e = strchr(p + 1, q);
							if (!e)
								selector_error(sel, p, "this quote is never closed");
							value = dup_n(p + 1, (size_t)(e - p - 1));
							p = e + 1;
						} else {
							const char *v = p;
							while (*p && *p != ']' && !isspace((unsigned char)*p))
								p++;
							value = dup_n(v, (size_t)(p - v));
						}
						while (isspace((unsigned char)*p))
							p++;
					} else if (op) {
						selector_error(sel, p, "expected '='");
					}
					if (*p != ']')
						selector_error(sel, p, "expected ']'");
					p++;
					part.anames = rt_realloc(part.anames, sizeof *part.anames * (size_t)(part.nattrs + 1));
					part.avalues = rt_realloc(part.avalues, sizeof *part.avalues * (size_t)(part.nattrs + 1));
					part.aops = rt_realloc(part.aops, (size_t)(part.nattrs + 1));
					part.anames[part.nattrs] = name;
					part.aops[part.nattrs] = op;
					part.avalues[part.nattrs++] = value;
				} else {
					break;
				}
			}
			if (p == start)
				selector_error(sel, p, *p ? "expected a tag, .class, #id, [attribute] or :pseudo-class" : "it ends too soon");
			c.parts = rt_realloc(c.parts, sizeof *c.parts * (size_t)(c.nparts + 1));
			c.parts[c.nparts++] = part;
			const char *before = p;
			while (isspace((unsigned char)*p))
				p++;
			if (*p == '>' || *p == '+' || *p == '~') {
				comb = *p++;
				while (isspace((unsigned char)*p))
					p++;
			} else if (*p == ',' || !*p || (stop && *p == stop)) {
				break;
			} else if (p > before) {
				comb = ' ';
			} else {
				selector_error(sel, p, "expected a space, '>', '+', '~' or ','");
			}
		}
		chains = rt_realloc(chains, sizeof *chains * (size_t)(n + 1));
		chains[n++] = c;
		if (*p == ',') {
			p++;
			continue;
		}
		break;
	}
	*pp = p;
	*nchains = n;
	return chains;
}

static Chain *parse_selector(const char *sel, int *nchains)
{
	const char *p = sel;
	Chain *c = parse_list(sel, &p, nchains, 0);
	if (*p)
		selector_error(sel, p, *p == ')' ? "a ')' without its '('" : "expected a space, '>', '+', '~' or ','");
	return c;
}

static bool has_class(const Node *n, const char *cls)
{
	const char *list = attr_get(n, "class");
	size_t cl = strlen(cls);
	for (const char *p = list; p && *p;) {
		while (isspace((unsigned char)*p))
			p++;
		const char *s = p;
		while (*p && !isspace((unsigned char)*p))
			p++;
		if ((size_t)(p - s) == cl && memcmp(s, cls, cl) == 0)
			return true;
	}
	return false;
}

/* [name op value]: = the same, ^= starts with, $= ends with, *= has it
 * in it, ~= one of its words, |= it, or it then a '-' (en, en-GB) */
static bool attr_test(const char *v, char op, const char *want)
{
	size_t vl = strlen(v), wl = strlen(want);
	switch (op) {
	case 0: return true;
	case '=': return strcmp(v, want) == 0;
	case '^': return wl && strncmp(v, want, wl) == 0;
	case '$': return wl && vl >= wl && strcmp(v + vl - wl, want) == 0;
	case '*': return wl && strstr(v, want) != NULL;
	case '|': return strncmp(v, want, wl) == 0 && (v[wl] == 0 || v[wl] == '-');
	default:   /* ~ */
		for (const char *p = v; *p;) {
			while (isspace((unsigned char)*p))
				p++;
			const char *s = p;
			while (*p && !isspace((unsigned char)*p))
				p++;
			if (wl && (size_t)(p - s) == wl && memcmp(s, want, wl) == 0)
				return true;
		}
		return false;
	}
}

static bool same_tag(const Node *a, const Node *b)
{
	return a->html ? strcasecmp(a->tag, b->tag) == 0 : strcmp(a->tag, b->tag) == 0;
}

/* n's place among its parent's elements (or those of its own tag), from
 * 1, counting from the start or the end; and how many there are */
static int64_t place_among(const Node *n, bool of_type, bool from_end, int64_t *count)
{
	const Node *p = n->parent;
	*count = 0;
	if (!p)
		return 1;
	int64_t at = 0;
	for (int64_t i = 0; i < p->nkids; i++) {
		const Node *k = p->kids[i];
		if (k->kind != M_ELEM || (of_type && !same_tag(k, n)))
			continue;
		++*count;
		if (k == n)
			at = *count;
	}
	return from_end ? *count - at + 1 : at;
}

/* place is a + b, + 2b, ...: an+b, for some n of 0 or more */
static bool nth(int64_t a, int64_t b, int64_t place)
{
	if (a == 0)
		return place == b;
	int64_t d = place - b;
	return d / a >= 0 && d % a == 0;
}

/* the element just before n among its parent's (or NULL) */
static const Node *element_before(const Node *n)
{
	const Node *p = n->parent, *prev = NULL;
	if (!p)
		return NULL;
	for (int64_t i = 0; i < p->nkids; i++) {
		if (p->kids[i] == n)
			return prev;
		if (p->kids[i]->kind == M_ELEM)
			prev = p->kids[i];
	}
	return NULL;
}

/* does `n` stand to `a` as `how` says: inside it (' '), directly inside
 * (>), straight after it (+), after it at all (~)? */
static bool stands(const Node *n, const Node *a, char how)
{
	switch (how) {
	case '>':
		return n->parent == a;
	case '+':
		return element_before(n) == a;
	case '~':
		for (const Node *b = element_before(n); b; b = element_before(b))
			if (b == a)
				return true;
		return false;
	default:
		for (const Node *p = n->parent; p; p = p->parent)
			if (p == a)
				return true;
		return false;
	}
}

static bool chain_matches(const Chain *c, int k, const Node *n, const Node *top, const Node *anchor);

static bool any_chain(const Chain *cs, int nc, const Node *n, const Node *anchor)
{
	for (int i = 0; i < nc; i++)
		if (chain_matches(&cs[i], cs[i].nparts - 1, n, NULL, anchor))
			return true;
	return false;
}

/* :has( ): an element standing to n as its selector says (inside it,
 * by default) matches it */
static bool has_matching(const Chain *cs, int nc, const Node *n, const Node *under)
{
	for (int64_t i = 0; i < under->nkids; i++) {
		const Node *k = under->kids[i];
		if (k->kind != M_ELEM)
			continue;
		if (k != n && any_chain(cs, nc, k, n))
			return true;
		if (has_matching(cs, nc, n, k))
			return true;
	}
	return false;
}

static bool pseudo_matches(const Pseudo *ps, const Node *n)
{
	int64_t count;
	switch (ps->kind) {
	case P_FIRST_CHILD: return place_among(n, false, false, &count) == 1;
	case P_LAST_CHILD: return place_among(n, false, true, &count) == 1;
	case P_ONLY_CHILD: place_among(n, false, false, &count); return count == 1;
	case P_NTH_CHILD: return nth(ps->a, ps->b, place_among(n, false, false, &count));
	case P_NTH_LAST_CHILD: return nth(ps->a, ps->b, place_among(n, false, true, &count));
	case P_FIRST_OF_TYPE: return place_among(n, true, false, &count) == 1;
	case P_LAST_OF_TYPE: return place_among(n, true, true, &count) == 1;
	case P_ONLY_OF_TYPE: place_among(n, true, false, &count); return count == 1;
	case P_NTH_OF_TYPE: return nth(ps->a, ps->b, place_among(n, true, false, &count));
	case P_NTH_LAST_OF_TYPE: return nth(ps->a, ps->b, place_among(n, true, true, &count));
	case P_EMPTY:
		for (int64_t i = 0; i < n->nkids; i++)
			if (n->kids[i]->kind == M_ELEM || (n->kids[i]->kind == M_TEXT && n->kids[i]->text && n->kids[i]->text[0]))
				return false;
		return true;
	case P_ROOT: return !n->parent || n->parent->kind != M_ELEM;
	case P_NOT: return !any_chain(ps->sub, ps->nsub, n, NULL);
	case P_IS: return any_chain(ps->sub, ps->nsub, n, NULL);
	case P_HAS: {
		/* (> + ~ say where to look: under n, or among those after it) */
		const Node *under = n;
		for (int i = 0; i < ps->nsub; i++)
			if (ps->sub[i].parts[0].comb == '+' || ps->sub[i].parts[0].comb == '~')
				under = n->parent ? n->parent : n;
		return has_matching(ps->sub, ps->nsub, n, under);
	}
	case P_CHECKED: return attr_get(n, "checked") || attr_get(n, "selected");
	case P_DISABLED: return attr_get(n, "disabled") != NULL;
	default: {   /* :enabled: a form's part, without disabled */
		static const char *const parts[] = { "input", "button", "select", "textarea", "option", "optgroup", "fieldset", NULL };
		bool form = false;
		for (int i = 0; parts[i]; i++)
			form |= strcasecmp(n->tag, parts[i]) == 0;
		return form && !attr_get(n, "disabled");
	}
	}
}

static bool part_matches(const Part *pt, const Node *n)
{
	if (n->kind != M_ELEM)
		return false;
	if (pt->tag && strcmp(pt->tag, "*") != 0 && (n->html ? strcasecmp(pt->tag, n->tag) : strcmp(pt->tag, n->tag)) != 0)
		return false;
	if (pt->id) {
		const char *id = attr_get(n, "id");
		if (!id || strcmp(id, pt->id) != 0)
			return false;
	}
	for (int i = 0; i < pt->nclasses; i++)
		if (!has_class(n, pt->classes[i]))
			return false;
	for (int i = 0; i < pt->nattrs; i++) {
		const char *v = attr_get(n, pt->anames[i]);
		if (!v || !attr_test(v, pt->aops[i], pt->avalues[i] ? pt->avalues[i] : ""))
			return false;
	}
	for (int i = 0; i < pt->npseudos; i++)
		if (!pseudo_matches(&pt->pseudos[i], n))
			return false;
	return true;
}

/* does n match chain parts[0..k], with parts[k] being n itself? The
 * elements it goes up through stay inside `top` (when there is one); with
 * an anchor (in :has( )), the first part's element must stand to it as
 * that part says. */
static bool chain_matches(const Chain *c, int k, const Node *n, const Node *top, const Node *anchor)
{
	if (!part_matches(&c->parts[k], n))
		return false;
	char how = c->parts[k].comb;
	if (k == 0)
		return !anchor || stands(n, anchor, how);
	switch (how) {
	case '>': {
		const Node *p = n->parent;
		return p && p != top && chain_matches(c, k - 1, p, top, anchor);
	}
	case '+': {
		const Node *b = element_before(n);
		return b && chain_matches(c, k - 1, b, top, anchor);
	}
	case '~':
		for (const Node *b = element_before(n); b; b = element_before(b))
			if (chain_matches(c, k - 1, b, top, anchor))
				return true;
		return false;
	default:
		for (const Node *p = n->parent; p && p != top; p = p->parent)
			if (chain_matches(c, k - 1, p, top, anchor))
				return true;
		return false;
	}
}

static bool matches(const Chain *cs, int nc, const Node *n, const Node *top)
{
	for (int i = 0; i < nc; i++)
		if (chain_matches(&cs[i], cs[i].nparts - 1, n, top, NULL))
			return true;
	return false;
}

/* every element inside `top` that matches, in the order they appear */
static _Thread_local Node **found;
static _Thread_local int64_t nfound, capfound;

static void collect(const Chain *cs, int nc, Node *n, Node *top, bool first_only)
{
	for (int64_t i = 0; i < n->nkids; i++) {
		if (first_only && nfound)
			return;
		Node *k = n->kids[i];
		if (k->kind != M_ELEM)
			continue;
		if (matches(cs, nc, k, top)) {
			if (nfound == capfound) {
				capfound = capfound ? capfound * 2 : 16;
				found = rt_realloc(found, sizeof *found * (size_t)capfound);
			}
			found[nfound++] = k;
		}
		collect(cs, nc, k, top, first_only);
	}
}

static void search(int64_t h, const char *sel, bool first_only)
{
	Node *top = want(h);
	int nc;
	parsing_html = top->html;
	Chain *cs = parse_selector(sel, &nc);
	nfound = 0;
	collect(cs, nc, top, top, first_only);
	free_chains(cs, nc);
}

int64_t cby_markup_find(int64_t h, const char *sel)
{
	search(h, sel, true);
	if (!nfound) {
		char d[200];
		describe(N(h), d, sizeof d);
		return missing("nothing in %s matches \"%s\"", d, sel);
	}
	found[0]->refs++;
	return (int64_t)(intptr_t)found[0];
}

int64_t cby_markup_find_all(int64_t h, const char *sel)
{
	search(h, sel, false);
	return nfound;
}

/* the i-th found by the last find_all */
int64_t cby_markup_found(int64_t i)
{
	found[i]->refs++;
	return (int64_t)(intptr_t)found[i];
}

/* ---- inside and around */

int64_t cby_markup_child_count(int64_t h)
{
	Node *n = want(h);
	int64_t c = 0;
	for (int64_t i = 0; i < n->nkids; i++)
		c += n->kids[i]->kind == M_ELEM;
	return c;
}

int64_t cby_markup_child(int64_t h, int64_t i)
{
	Node *n = want(h);
	for (int64_t k = 0; k < n->nkids; k++)
		if (n->kids[k]->kind == M_ELEM && i-- == 0) {
			n->kids[k]->refs++;
			return (int64_t)(intptr_t)n->kids[k];
		}
	char d[200];
	describe(n, d, sizeof d);
	return missing("%s has no element at [%s]", d, "that place");
}

int64_t cby_markup_parent(int64_t h)
{
	Node *n = want(h);
	if (!n->parent) {
		char d[200];
		describe(n, d, sizeof d);
		return missing("%s is at the top: it has no parent%s", d, "");
	}
	n->parent->refs++;
	return (int64_t)(intptr_t)n->parent;
}

/* ---- attributes */

int cby_markup_has_attribute(int64_t h, const char *name) { return attr_get(want(h), name) != NULL; }

/* the attribute's length (its text waits for cby_markup_take), or -1 if it
 * isn't there and `must` is 0 (a panic if it is 1) */
int64_t cby_markup_attribute(int64_t h, const char *name, int64_t must)
{
	Node *n = want(h);
	const char *v = n->kind == M_ELEM ? attr_get(n, name) : NULL;
	if (!v) {
		if (!must)
			return -1;
		char d[200];
		describe(n, d, sizeof d);
		rt_panic(NULL, 0, 0, "%s has no attribute \"%s\"", d, name);
	}
	out.n = 0;
	buf_put(&out, v, strlen(v));
	return (int64_t)out.n;
}

void cby_markup_set_attribute(int64_t h, const char *name, const char *value)
{
	Node *n = want(h);
	if (n->kind != M_ELEM)
		rt_panic(NULL, 0, 0, "only an element has attributes");
	attr_set(n, name, strlen(name), value, strlen(value));
}

void cby_markup_remove_attribute(int64_t h, const char *name)
{
	Node *n = want(h);
	for (int i = 0; i < n->nattrs; i++)
		if (strcmp(n->names[i], name) == 0) {
			free(n->names[i]);
			free(n->values[i]);
			memmove(n->names + i, n->names + i + 1, sizeof *n->names * (size_t)(n->nattrs - i - 1));
			memmove(n->values + i, n->values + i + 1, sizeof *n->values * (size_t)(n->nattrs - i - 1));
			n->nattrs--;
			return;
		}
}

int64_t cby_markup_attribute_count(int64_t h) { return want(h)->nattrs; }

int64_t cby_markup_attribute_name(int64_t h, int64_t i)
{
	Node *n = want(h);
	out.n = 0;
	buf_put(&out, "", 0);
	if (i >= 0 && i < n->nattrs)
		buf_puts(&out, n->names[i]);
	return (int64_t)out.n;
}

/* ---- building */

static bool inside(const Node *n, const Node *maybe_parent)
{
	for (const Node *p = n; p; p = p->parent)
		if (p == maybe_parent)
			return true;
	return false;
}

void cby_markup_add(int64_t h, int64_t child)
{
	Node *n = want(h), *c = want(child);
	if (c->kind == M_DOC)
		rt_panic(NULL, 0, 0, "a whole page can't go inside an element; add the elements found in it");
	if (inside(n, c))
		rt_panic(NULL, 0, 0, "an element can't go inside itself");
	if (c->parent) {   /* moving: out of where it was */
		Node *p = c->parent;
		for (int64_t i = 0; i < p->nkids; i++)
			if (p->kids[i] == c) {
				memmove(p->kids + i, p->kids + i + 1, sizeof *p->kids * (size_t)(p->nkids - i - 1));
				p->nkids--;
				c->refs--;   /* its old parent's count, handed to the new one */
				break;
			}
	}
	c->refs++;
	add_kid(n, c);
}

void cby_markup_add_text(int64_t h, const char *text)
{
	Node *n = want(h);
	Node *t = node_new(M_TEXT, n->html);
	t->text = dup_s(text);
	add_kid(n, t);
}

void cby_markup_clear(int64_t h)
{
	Node *n = want(h);
	for (int64_t i = 0; i < n->nkids; i++) {
		n->kids[i]->parent = NULL;
		node_release(n->kids[i]);
	}
	n->nkids = 0;
}

void cby_markup_remove(int64_t h)
{
	Node *n = want(h), *p = n->parent;
	if (!p)
		return;
	for (int64_t i = 0; i < p->nkids; i++)
		if (p->kids[i] == n) {
			memmove(p->kids + i, p->kids + i + 1, sizeof *p->kids * (size_t)(p->nkids - i - 1));
			p->nkids--;
			n->parent = NULL;
			node_release(n);
			return;
		}
}

static Node *copy_node(const Node *n)
{
	Node *c = node_new(n->kind, n->html);
	c->tag = n->tag ? dup_s(n->tag) : NULL;
	c->text = n->text ? dup_s(n->text) : NULL;
	for (int i = 0; i < n->nattrs; i++)
		attr_set(c, n->names[i], strlen(n->names[i]), n->values[i], strlen(n->values[i]));
	for (int64_t i = 0; i < n->nkids; i++)
		add_kid(c, copy_node(n->kids[i]));
	return c;
}

int64_t cby_markup_copy(int64_t h) { return (int64_t)(intptr_t)copy_node(want(h)); }

/* ---- text out */

/* all the text inside, as a person would read it: spaces and line ends run
 * together into one space; a block (a paragraph, a list item) is apart from
 * the next */
/* the text inside: with a space around each block (a paragraph, a list
 * item) when spaced, as a person reads it; exactly as it is when not */
static void gather_text(const Node *n, Buf *b, bool spaced)
{
	if (n->kind == M_TEXT) {
		buf_puts(b, n->text);
		return;
	}
	if (n->kind == M_COMMENT || (n->html && n->tag && in_list(n->tag, raw_tags)))
		return;
	bool block = spaced && n->html && n->tag && in_list(n->tag, block_tags);
	if (block)
		buf_put(b, " ", 1);
	for (int64_t i = 0; i < n->nkids; i++)
		gather_text(n->kids[i], b, spaced);
	if (block)
		buf_put(b, " ", 1);
}

static void tidy_into_out(const char *s, size_t n)
{
	out.n = 0;
	buf_put(&out, "", 0);
	bool space = false;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		bool sp = isspace(c) || (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0);   /* &nbsp; too */
		if (sp) {
			if (c == 0xC2)
				i++;
			space = out.n > 0;
			continue;
		}
		if (space) {
			buf_put(&out, " ", 1);
			space = false;
		}
		buf_put(&out, s + i, 1);
	}
}

static void put_escaped(Buf *b, const char *s, bool attr)
{
	for (; *s; s++) {
		switch (*s) {
		case '&': buf_puts(b, "&amp;"); break;
		case '<': buf_puts(b, "&lt;"); break;
		case '>': buf_puts(b, "&gt;"); break;
		case '"': if (attr) { buf_puts(b, "&quot;"); break; } /* fall through */
		default: buf_put(b, s, 1);
		}
	}
}

static bool blank(const char *s)
{
	for (; s && *s; s++)
		if (!isspace((unsigned char)*s))
			return false;
	return true;
}

static void indent(Buf *b, int depth)
{
	buf_put(b, "\n", 1);
	for (int i = 0; i < depth; i++)
		buf_put(b, "  ", 2);
}

static void write_node(const Node *n, Buf *b, bool pretty, int depth)
{
	switch (n->kind) {
	case M_TEXT:
		if (n->parent && n->parent->html && n->parent->tag && in_list(n->parent->tag, raw_tags))
			buf_puts(b, n->text);   /* a script stays as it is */
		else
			put_escaped(b, n->text, false);
		return;
	case M_COMMENT:
		buf_puts(b, "<!--");
		buf_puts(b, n->text);
		buf_puts(b, "-->");
		return;
	case M_DOC:
		for (int64_t i = 0; i < n->nkids; i++) {
			if (pretty && blank(n->kids[i]->kind == M_TEXT ? n->kids[i]->text : "x"))
				continue;
			if (pretty && b->n)
				buf_put(b, "\n", 1);
			write_node(n->kids[i], b, pretty, depth);
		}
		return;
	default:
		break;
	}
	buf_put(b, "<", 1);
	buf_puts(b, n->tag);
	for (int i = 0; i < n->nattrs; i++) {
		buf_put(b, " ", 1);
		buf_puts(b, n->names[i]);
		buf_puts(b, "=\"");
		put_escaped(b, n->values[i], true);
		buf_put(b, "\"", 1);
	}
	if (n->html && in_list(n->tag, void_tags)) {
		buf_put(b, ">", 1);
		return;
	}
	if (!n->html && n->nkids == 0) {
		buf_puts(b, "/>");
		return;
	}
	buf_put(b, ">", 1);
	/* pretty: children on lines of their own, unless there's text among them */
	bool text_inside = false;
	for (int64_t i = 0; i < n->nkids; i++)
		if (n->kids[i]->kind == M_TEXT && !blank(n->kids[i]->text))
			text_inside = true;
	bool lines = pretty && !text_inside && n->nkids > 0;
	for (int64_t i = 0; i < n->nkids; i++) {
		if (lines && n->kids[i]->kind == M_TEXT)
			continue;   /* the spacing between elements: the indenting replaces it */
		if (lines)
			indent(b, depth + 1);
		write_node(n->kids[i], b, pretty && lines, depth + 1);
	}
	if (lines)
		indent(b, depth);
	buf_puts(b, "</");
	buf_puts(b, n->tag);
	buf_put(b, ">", 1);
}

/* what: 0 the tag, 1 the text inside (tidied), 2 markup, 3 indented markup,
 * 4 the text inside as it is */
int64_t cby_markup_text(int64_t h, int64_t what)
{
	Node *n = want(h);
	if (what == 0) {
		out.n = 0;
		buf_put(&out, "", 0);
		if (n->tag)
			buf_puts(&out, n->tag);
	} else if (what == 1 || what == 4) {
		Buf b = { 0 };
		buf_put(&b, "", 0);
		gather_text(n, &b, what == 1);
		if (what == 1) {
			tidy_into_out(b.s, b.n);
		} else {
			out.n = 0;
			buf_put(&out, b.s, b.n);
		}
		free(b.s);
	} else {
		out.n = 0;
		buf_put(&out, "", 0);
		write_node(n, &out, what == 3, 0);
	}
	return (int64_t)out.n;
}

/* copies the text just made into dst (room for it, from a str of that length) */
void cby_markup_take(char *dst)
{
	if (out.n)
		memcpy(dst, out.s, out.n);
}

int cby_markup_can_read(const char *text)
{
	/* XML only (HTML always reads) */
	jmp_buf jb;
	quiet = &jb;
	memset(&reading, 0, sizeof reading);
	int ok = 0;
	if (setjmp(jb) == 0) {
		node_release(parse(text, false));
		ok = 1;
	} else {
		node_release(reading.doc);
		free(reading.stack);
		free(reading.opened);
		free(reading.text);
		free(reading.tag);
	}
	quiet = NULL;
	return ok;
}

/* a worker's thread, ending: what it kept for itself */
static void markup_thread_end(void) { free(found); found = NULL; nfound = capfound = 0; }
__attribute__((constructor)) static void markup_threads(void) { rt_on_thread_end(markup_thread_end); }
/* and the main thread's, at the program's end */
__attribute__((destructor)) static void markup_main_end(void) { markup_thread_end(); }
