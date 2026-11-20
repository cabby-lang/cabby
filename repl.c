/* repl.c — the interactive session. See repl.h. */
#include "version.h"
#include "license.h"
#include "repl.h"

#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#if defined(__APPLE__)
#define purge_input(f) fpurge(f)
#else
#include <stdio_ext.h>
#define purge_input(f) __fpurge(f)
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "arena.h"
#include "diag.h"
#include "interpreter.h"
#include "lexer.h"
#include "lineedit.h"
#include "parser.h"
#include "runtime/rt.h"
#include "highlight.h"
#include "sema.h"

#define SESSION_NAME "<session>"
#define STACK_SIZE ((size_t)1 << 30)

typedef enum {
	CH_DECL,      /* use, types, typedef, extern, prototypes */
	CH_FUNC,      /* a function definition */
	CH_STMT,      /* statements: part of main */
	CH_FAILED,    /* statements that panicked or were interrupted: kept only as a comment */
} ChunkKind;

typedef struct Chunk {
	int id;                  /* unique, never reused */
	ChunkKind kind;
	char *text;              /* lines joined by '\n', no final newline */
	char *fname;             /* CH_FUNC: the function's name */
	int prefix;              /* characters added in front of what was typed (echo) */
	bool is_use;             /* a use line */
	const char *why;         /* CH_FAILED: "panicked" or "was interrupted" */
} Chunk;

typedef struct LineInfo {
	int chunk;               /* index into R.chunks, or -1 for the lines Cabby adds */
	int line;                /* line within the input */
	int strip;               /* characters in front of the input's own text */
} LineInfo;

/* One parse + check of the whole session. */
typedef struct Gen {
	Arena arena;
	Program *prog;
	Sema sema;
	LineInfo *lines;
	int nlines;
} Gen;

static struct {
	Chunk *chunks;
	int nchunks, cap;
	int next_id;
	int current;             /* input being checked or run, or -1 */
	Gen *cur;                /* the check the interpreter is running */
	Gen *table;              /* the check whose files diag knows (newest) */
	InterpSession *IS;
	const char *const *lib_dirs;
	int main_stmts;          /* top-level statements of main in `cur` */
	bool tty;
	LineEditor *le;
	char *pushback;          /* a line read ahead that starts the next input */
	int status;
	bool ended;              /* `return` ran */
} R;

/* The session text is always the first file registered after
 * diag_clear_files(). */
#define SESSION_FILE 0

/* ============================================================== text helpers */

static char *xstrdup(const char *s) { return strdup(s ? s : ""); }

static char *concat(const char *a, const char *b, const char *c, const char *d)
{
	size_t n = strlen(a) + strlen(b) + strlen(c) + strlen(d);
	char *s = malloc(n + 1);
	snprintf(s, n + 1, "%s%s%s%s", a, b, c, d);
	return s;
}

static bool is_blank(const char *s)
{
	for (; *s; s++)
		if (*s != ' ' && *s != '\t' && *s != '\r')
			return false;
	return true;
}

static const char *skip_ws(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static int leading_tabs(const char *s)
{
	int n = 0;
	while (s[n] == '\t')
		n++;
	return n;
}

/* ============================================================== session text */

typedef struct Out {
	FILE *f;
	LineInfo *lines;
	int nlines, cap;
} Out;

static void out_line(Out *o, const char *prefix, const char *text, size_t len, int chunk, int line, int strip)
{
	fprintf(o->f, "%s%.*s\n", prefix, (int)len, text);
	if (o->nlines == o->cap) {
		o->cap = o->cap ? o->cap * 2 : 64;
		o->lines = realloc(o->lines, sizeof *o->lines * (size_t)o->cap);
	}
	o->lines[o->nlines++] = (LineInfo){ chunk, line, strip };
}

/* Each line of a chunk, with `prefix` in front. */
static void out_chunk(Out *o, int ci, const char *prefix)
{
	const Chunk *c = &R.chunks[ci];
	const char *p = c->text;
	int line = 1;
	int pl = (int)strlen(prefix);
	for (;;) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		out_line(o, prefix, p, len, ci, line, pl + (line == 1 ? c->prefix : 0));
		if (!e)
			break;
		p = e + 1;
		line++;
	}
}

/* The session as a program. For saving, failed inputs appear as comments
 * and declarations are separated by blank lines. */
static char *build_source(bool for_save, LineInfo **lines, int *nlines)
{
	Out o = { 0 };
	char *buf = NULL;
	size_t size = 0;
	o.f = open_memstream(&buf, &size);

	if (for_save) {
		char when[64];
		time_t now = time(NULL);
		strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
		fprintf(o.f, "// Cabby session, saved %s\n\n", when);
	}
	for (int i = 0; i < R.nchunks; i++)
		if (R.chunks[i].kind == CH_DECL || R.chunks[i].kind == CH_FUNC) {
			out_chunk(&o, i, "");
			if (for_save)
				fputc('\n', o.f);
		}
	out_line(&o, "", "int main {", 10, -1, 0, 0);
	for (int i = 0; i < R.nchunks; i++) {
		if (R.chunks[i].kind == CH_STMT)
			out_chunk(&o, i, "\t");
		else if (R.chunks[i].kind == CH_FAILED && for_save) {
			fprintf(o.f, "\t// not kept (it %s):\n", R.chunks[i].why);
			out_chunk(&o, i, "\t// ");
		}
	}
	out_line(&o, "", "}", 1, -1, 0, 0);
	fclose(o.f);
	if (lines) {
		*lines = o.lines;
		*nlines = o.nlines;
	} else {
		free(o.lines);
	}
	return buf;
}

/* ============================================================== diagnostics */

static bool map_loc(int file, int line, int col, DiagPos *out)
{
	Gen *g = R.table;
	if (!g || file != SESSION_FILE || line < 1 || line > g->nlines)
		return false;
	LineInfo li = g->lines[line - 1];
	if (li.chunk < 0) {
		out->name = SESSION_NAME;
		return true;
	}
	static char names[2][160];
	static int turn;
	const Chunk *c = &R.chunks[li.chunk];
	if (li.chunk == R.current) {
		out->name = "<input>";
	} else if (c->kind == CH_FUNC) {
		char *n = names[turn ^= 1];
		snprintf(n, sizeof names[0], "<function %s>", c->fname);
		out->name = n;
	} else {
		out->name = "<earlier input>";
	}
	out->line = li.line;
	out->col = col - li.strip < 1 ? 1 : col - li.strip;
	out->strip = li.strip;
	return true;
}

/* the tabs the session put in front of a line of its text (a statement's,
 * inside main), for the parser's indentation messages */
static int tabs_added(int file, int line)
{
	DiagPos p;
	if (!map_loc(file, line, 1, &p) || p.strip <= 0)
		return 0;
	return p.strip;
}

/* Errors are always shown; warnings only for the input being entered, so
 * earlier inputs' warnings are not repeated every time. */
static bool filter(DiagLevel level, int file, int line)
{
	if (level == DIAG_ERROR)
		return true;
	Gen *g = R.table;
	if (file == SESSION_FILE && g)
		return line >= 1 && line <= g->nlines && g->lines[line - 1].chunk == R.current;
	return R.current >= 0 && R.chunks[R.current].is_use;   /* a header's own warnings */
}

/* ============================================================== checking */

static void gen_free(Gen *g)
{
	if (!g)
		return;
	if (g->prog)
		for (int i = 0; i < g->prog->files.len; i++)
			free(g->prog->files.items[i].src);
	arena_free(&g->arena);
	free(g->lines);
	free(g);
}

/* Parses and checks the whole session. Returns NULL on errors; *parsed
 * (optional) says whether parsing succeeded. */
static Gen *check(bool quiet, bool *parsed)
{
	if (R.table && R.table != R.cur)
		gen_free(R.table);
	diag_reset();
	diag_clear_files();
	diag_set_quiet(quiet);

	Gen *g = calloc(1, sizeof *g);
	arena_init(&g->arena);
	char *src = build_source(false, &g->lines, &g->nlines);
	R.table = g;
	g->prog = parse_program_source(&g->arena, SESSION_NAME, src, R.lib_dirs);
	bool ok = diag_error_count() == 0;
	if (parsed)
		*parsed = ok;
	if (ok)
		ok = sema_check(&g->arena, g->prog, &g->sema) && diag_error_count() == 0;
	diag_set_quiet(false);
	return ok ? g : NULL;
}

/* Globals are matched between checks by where they were declared. */
static const char *global_key(Symbol *s, void *ctx)
{
	Gen *g = ctx;
	static char buf[600];
	if (s->loc.file == SESSION_FILE && s->loc.line >= 1 && s->loc.line <= g->nlines) {
		LineInfo li = g->lines[s->loc.line - 1];
		int id = li.chunk >= 0 ? R.chunks[li.chunk].id : -1;
		snprintf(buf, sizeof buf, "#%d:%d:%d:%s", id, li.line, s->loc.col, s->name);
	} else {
		snprintf(buf, sizeof buf, "%s:%d:%d:%s", diag_file_name(s->loc.file), s->loc.line, s->loc.col, s->name);
	}
	return buf;
}

static bool adopt(Gen *g)
{
	if (!isession_adopt(R.IS, &g->sema, global_key, g)) {
		fprintf(stderr, "error: the session has too many variables\n");
		return false;
	}
	if (R.cur && R.cur != g)
		gen_free(R.cur);
	R.cur = g;
	R.main_stmts = g->sema.main_fn->body->stmts.len;
	return true;
}

/* After an input is dropped: check the session without it and carry on
 * from that. */
static void resync(void)
{
	int saved = R.current;
	R.current = -1;
	Gen *g = check(true, NULL);
	if (!g || !adopt(g)) {
		fprintf(stderr, "cabby: internal error: the session no longer checks\n");
		exit(1);
	}
	R.current = saved;
}

/* ============================================================== classifying input */

static Chunk *push_chunk(ChunkKind kind, char *text, int prefix)
{
	if (R.nchunks == R.cap) {
		R.cap = R.cap ? R.cap * 2 : 32;
		R.chunks = realloc(R.chunks, sizeof *R.chunks * (size_t)R.cap);
	}
	Chunk *c = &R.chunks[R.nchunks++];
	memset(c, 0, sizeof *c);
	c->id = R.next_id++;
	c->kind = kind;
	c->text = text;
	c->prefix = prefix;
	return c;
}

static void pop_chunk(void)
{
	Chunk *c = &R.chunks[--R.nchunks];
	free(c->text);
	free(c->fname);
}

static bool is_known_type(const char *s, int len)
{
	if (!R.cur)
		return false;
	DeclVec *d = &R.cur->prog->decls;
	for (int i = 0; i < d->len; i++) {
		const char *name = NULL;
		switch (d->items[i]->kind) {
		case DECL_OBJECT:  name = d->items[i]->as.object->name; break;
		case DECL_ENUM:    name = d->items[i]->as.enum_->name; break;
		case DECL_TYPEDEF: name = d->items[i]->as.typedef_.name; break;
		default: break;
		}
		if (name && (int)strlen(name) == len && memcmp(name, s, (size_t)len) == 0)
			return true;
	}
	return false;
}

static bool is_extern_function(const char *s, int len)
{
	if (!R.cur)
		return false;
	DeclVec *d = &R.cur->prog->decls;
	for (int i = 0; i < d->len; i++)
		if (d->items[i]->kind == DECL_FN && d->items[i]->as.fn->is_extern) {
			const char *name = d->items[i]->as.fn->name;
			if ((int)strlen(name) == len && memcmp(name, s, (size_t)len) == 0)
				return true;
		}
	return false;
}

static bool is_type_keyword(TokenKind k)
{
	switch (k) {
	case TOK_KW_CHAR: case TOK_KW_SHORT: case TOK_KW_INT: case TOK_KW_LONG:
	case TOK_KW_FLOAT: case TOK_KW_DOUBLE: case TOK_KW_VOID: case TOK_KW_SIGNED:
	case TOK_KW_UNSIGNED: case TOK_KW_BOOL: case TOK_KW_STR: case TOK_KW_CONST:
	case TOK_KW_VOLATILE: case TOK_KW_STATIC: case TOK_KW_REGISTER: case TOK_KW_AUTO:
		return true;
	default:
		return false;
	}
}

static bool starts_type(Token t)
{
	return is_type_keyword(t.kind) || (t.kind == TOK_IDENT && is_known_type(t.start, t.length));
}

typedef struct Toks {
	Token *t;
	int n;
	int nesting;             /* unclosed ( [ { at the end */
	bool open_comment;       /* a block comment not yet closed */
	Arena arena;
} Toks;

static void lex_all(Toks *T, const char *text)
{
	memset(T, 0, sizeof *T);
	arena_init(&T->arena);
	Lexer L;
	lexer_init(&L, &T->arena, "<input>", text);
	L.quiet = true;
	int cap = 0, depth = 0;
	for (;;) {
		Token t = lexer_next(&L);
		if (t.kind == TOK_EOF)
			break;
		if (t.kind == TOK_LPAREN || t.kind == TOK_LBRACKET || t.kind == TOK_LBRACE)
			depth++;
		if ((t.kind == TOK_RPAREN || t.kind == TOK_RBRACKET || t.kind == TOK_RBRACE) && depth > 0)
			depth--;
		if (T->n == cap) {
			cap = cap ? cap * 2 : 64;
			T->t = realloc(T->t, sizeof *T->t * (size_t)cap);
		}
		T->t[T->n++] = t;
	}
	T->nesting = depth;
	T->open_comment = L.open_comment;
}

static void toks_free(Toks *T)
{
	free(T->t);
	arena_free(&T->arena);
}

/* Token index after the leading NEWLINEs. */
static int first_tok(const Toks *T)
{
	int i = 0;
	while (i < T->n && T->t[i].kind == TOK_NEWLINE)
		i++;
	return i;
}

typedef enum { FN_NO, FN_DEF, FN_HEADER } FnShape;

/* `T name {`, `T name T2 p, ... {` and Allman headers. *name gets the
 * function's name. A header without its `{` yet is FN_HEADER. */
static FnShape function_shape(const Toks *T, int i, char **name)
{
	bool saw_type = false, only_void = true;
	bool named_type = false;
	while (i < T->n) {
		Token t = T->t[i];
		if (is_type_keyword(t.kind)) {
			saw_type = true;
			if (t.kind != TOK_KW_VOID)
				only_void = false;
			i++;
		} else if (t.kind == TOK_IDENT && !named_type && is_known_type(t.start, t.length)) {
			saw_type = named_type = true;
			only_void = false;
			i++;
		} else {
			break;
		}
	}
	if (!saw_type || i >= T->n || T->t[i].kind != TOK_IDENT)
		return FN_NO;
	Token nm = T->t[i++];
	if (i >= T->n)
		return FN_NO;
	Token u = T->t[i];
	bool def = false, header = false;
	if (u.kind == TOK_LBRACE)
		def = true;
	else if (starts_type(u))
		header = true;   /* parameters follow */
	else if (u.kind == TOK_NEWLINE && only_void)
		header = true;   /* `void greet` can only be a function */
	else if (u.kind == TOK_NEWLINE && i + 1 < T->n && T->t[i + 1].kind == TOK_LBRACE)
		def = true;      /* Allman */
	if (!def && !header)
		return FN_NO;
	if (header)
		for (int j = i; j < T->n; j++)
			if (T->t[j].kind == TOK_LBRACE)
				def = true;
	if (name)
		*name = strndup(nm.start, (size_t)nm.length);
	return def ? FN_DEF : FN_HEADER;
}

static ChunkKind classify(const char *text, char **fname, bool *is_use)
{
	Toks T;
	lex_all(&T, text);
	int i = first_tok(&T);
	ChunkKind k = CH_STMT;
	*fname = NULL;
	*is_use = false;
	for (int j = i; j < T.n && T.t[j].kind != TOK_NEWLINE; j++) {
		if (T.t[j].kind == TOK_KW_AKA)
			k = CH_DECL;   /* double aka celsius */
		/* long kind meters (and ..., steps T) */
		if (j > i && T.t[j].kind == TOK_IDENT && T.t[j].length == 4 && memcmp(T.t[j].start, "kind", 4) == 0 &&
		    token_is_keyword(T.t[j - 1].kind) && j + 1 < T.n && T.t[j + 1].kind == TOK_IDENT &&
		    (j + 2 >= T.n || T.t[j + 2].kind == TOK_NEWLINE || T.t[j + 2].kind == TOK_EOF ||
		     T.t[j + 2].kind == TOK_COMMA))
			k = CH_DECL;
	}
	if (i < T.n && k == CH_STMT) {
		switch (T.t[i].kind) {
		case TOK_USE:
			*is_use = true;
			k = CH_DECL;
			break;
		case TOK_KW_OBJECT: case TOK_KW_STRUCT: case TOK_KW_UNION:
		case TOK_KW_ENUM: case TOK_KW_TYPEDEF: case TOK_KW_EXTERN:
			k = CH_DECL;
			break;
		default: {
			FnShape s = function_shape(&T, i, fname);
			if (s == FN_DEF)
				k = CH_FUNC;
			else if (s == FN_HEADER)
				k = CH_DECL;   /* a prototype */
			break;
		}
		}
	}
	toks_free(&T);
	return k;
}

typedef enum { IN_COMPLETE, IN_MORE, IN_HEADER, IN_IF } Completeness;

/* Whether an if statement's last branch is a plain `else` (not `else if`):
 * then nothing more can follow it. */
static bool ends_with_plain_else(const Toks *T, int first)
{
	int depth = 0;
	bool plain = false;
	for (int i = first; i < T->n; i++) {
		TokenKind k = T->t[i].kind;
		if (k == TOK_LBRACE)
			depth++;
		else if (k == TOK_RBRACE)
			depth--;
		else if (k == TOK_KW_ELSE && depth == 0)
			plain = i + 1 >= T->n || T->t[i + 1].kind != TOK_KW_IF;
	}
	return plain;
}

/* Is the input finished? IN_MORE: inside brackets; IN_HEADER: the last line
 * is a header still waiting for its `{` (a blank line ends it anyway);
 * IN_IF: an if statement that an `else` could still continue. */
static Completeness completeness(const char *text)
{
	Toks T;
	lex_all(&T, text);
	Completeness r = IN_COMPLETE;
	int first = first_tok(&T);
	if (T.open_comment) {
		r = IN_MORE;
	} else if (first >= T.n) {
		r = IN_COMPLETE;
	} else if (T.nesting > 0) {
		r = IN_MORE;
	} else {
		/* tokens of the last line */
		int end = T.n;
		while (end > 0 && T.t[end - 1].kind == TOK_NEWLINE)
			end--;
		int start = end - 1;
		while (start > 0 && T.t[start - 1].kind != TOK_NEWLINE)
			start--;
		int s = start;
		bool closes = s < end && T.t[s].kind == TOK_RBRACE;
		if (closes)
			s++;
		bool opens = T.t[end - 1].kind == TOK_LBRACE;
		TokenKind k = s < end ? T.t[s].kind : TOK_NEWLINE;
		bool header = false;
		if (!opens) {
			if (k == TOK_KW_IF || k == TOK_KW_ELSE || k == TOK_KW_FOR || k == TOK_KW_SWITCH || k == TOK_KW_DO ||
			    (k == TOK_KW_UNTIL && !closes))
				header = true;
			else if (k == TOK_KW_WHILE && !closes)
				header = true;
			else if ((k == TOK_KW_OBJECT || k == TOK_KW_STRUCT || k == TOK_KW_UNION || k == TOK_KW_ENUM) &&
				 start == first)
				header = true;
			else if (start == first && function_shape(&T, first, NULL) == FN_HEADER)
				header = true;
		}
		if (header)
			r = IN_HEADER;
		else if (T.t[first].kind == TOK_KW_IF && closes && end - start == 1 && !ends_with_plain_else(&T, first))
			r = IN_IF;
	}
	toks_free(&T);
	return r;
}

/* Statements that might be an expression to echo: `x + 1` becomes
 * `println x + 1`; a bare call `area 3, 4` becomes `println area(3, 4)`. */
static int echo_forms(const char *text, char **out)
{
	if (strchr(text, '\n'))
		return 0;
	Toks T;
	lex_all(&T, text);
	int i = first_tok(&T);
	int n = 0;
	if (i >= T.n || T.nesting)
		goto done;
	Token t = T.t[i];
	if (token_is_keyword(t.kind) && t.kind != TOK_KW_TRUE && t.kind != TOK_KW_FALSE &&
	    t.kind != TOK_KW_SELF && t.kind != TOK_KW_SIZEOF)
		goto done;
	if (t.kind == TOK_IDENT && ((t.length == 5 && memcmp(t.start, "print", 5) == 0) ||
				    (t.length == 7 && memcmp(t.start, "println", 7) == 0)))
		goto done;
	if (starts_type(t) && !(T.t[i + 1].kind == TOK_DOT && T.t[i + 1].start == t.start + t.length))
		goto done;   /* a declaration (but `json.read(...)` is a call: a type's library function) */
	if (t.kind == TOK_IDENT && is_extern_function(t.start, t.length) && T.t[i + 1].kind != TOK_LPAREN)
		goto done;   /* `printf "..."` is called for what it does; its count would be noise */
	int last = T.n - 1;
	while (last > i && T.t[last].kind == TOK_NEWLINE)
		last--;
	if (T.t[last].kind == TOK_COLON)
		goto done;   /* a label */

	out[n++] = concat("println ", skip_ws(text), "", "");

	/* a bare call: name(.name)* followed by its arguments. Not for C
	 * functions: `printf "..."` is called for what it does, and echoing
	 * its count would be noise. */
	if (t.kind == TOK_IDENT && !is_extern_function(t.start, t.length)) {
		int j = i + 1;
		while (j + 1 < T.n && T.t[j].kind == TOK_DOT && T.t[j + 1].kind == TOK_IDENT)
			j += 2;
		Token after = T.t[j];
		const char *callee_end = T.t[j - 1].start + T.t[j - 1].length;
		char *callee = strndup(t.start, (size_t)(callee_end - t.start));
		if (after.kind == TOK_NEWLINE) {
			out[n++] = concat("println ", callee, "()", "");
		} else {
			switch (after.kind) {
			case TOK_IDENT: case TOK_INT: case TOK_FLOAT: case TOK_STRING: case TOK_CHAR:
			case TOK_KW_TRUE: case TOK_KW_FALSE: case TOK_KW_SELF: case TOK_KW_SIZEOF:
			case TOK_MINUS: case TOK_BANG: case TOK_TILDE: case TOK_LBRACE: case TOK_KW_NOT: {
				char *args = xstrdup(after.start);
				size_t al = strlen(args);
				while (al && (args[al - 1] == ' ' || args[al - 1] == '\t' || args[al - 1] == '\r'))
					args[--al] = '\0';
				char *call = concat(callee, "(", args, ")");
				out[n++] = concat("println ", call, "", "");
				free(call);
				free(args);
				break;
			}
			default:
				break;
			}
		}
		free(callee);
	}
done:
	toks_free(&T);
	return n;
}

/* ============================================================== completion */

typedef struct { char **items; int n, cap; const char *prefix; int plen; } Cands;

static void cand_add(Cands *c, const char *name)
{
	if (!name || !*name || strchr(name, ' ') || strncmp(name, c->prefix, (size_t)c->plen) != 0)
		return;
	for (int i = 0; i < c->n; i++)
		if (strcmp(c->items[i], name) == 0)
			return;
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 32;
		c->items = realloc(c->items, sizeof *c->items * (size_t)c->cap);
	}
	c->items[c->n++] = xstrdup(name);
}

static void cand_add_all(Cands *c, const char *const *names)
{
	for (; *names; names++)
		cand_add(c, *names);
}

static const char *const str_member_names[] = {
	"length", "copy", "find", "contains", "starts_with", "ends_with", "replace", "trim", "upper", "lower",
	"repeat", "pad_left", "pad_right", "centered", "split", "is_int", "to_int", "is_long", "to_long", "is_number", "to_double", NULL,
};
static const char *const list_member_names[] = {
	"length", "add", "insert", "remove_at", "take_last", "clear", "contains", "find", "copy", "sort", "reverse", NULL,
};
static const char *const lookup_member_names[] = {
	"length", "contains", "remove", "clear", "copy", "keys", "values", NULL,
};
static const char *const files_names[] = {
	"read", "lines", "write", "append", "exists", "size", "copy", "rename", "delete", "open", "open_to_write",
	"open_to_add", NULL,
};
static const char *const directories_names[] = {
	"list", "all_files", "exists", "make", "remove", "rename", "current", "change_to", "home", "save_folder", "open", NULL,
};
static const char *const shell_names[] = { "run", "status", "get", "has", "set", "unset", "run_with", "run_program", "run_program_with", "program_status", NULL };
static const char *const addresses_names[] = { "of", "of_text", "of_items", "at", "none", "allocate", "free", "reallocate", NULL };
static const char *const events_names[] = { "run", "stop", NULL };
static const char *const cabby_names[] = { "exit", "clear", "preserve", "restore", "remove", "sessions", "version", "license", "help", NULL };

/* The session's variables: main's (the session is main's body), then globals */
static Symbol *find_variable(const char *name, size_t len)
{
	Sema *S = R.table ? &R.table->sema : NULL;
	if (!S)
		return NULL;
	if (S->main_fn)
		for (int i = S->main_fn->frame.len - 1; i >= 0; i--) {
			Symbol *v = S->main_fn->frame.items[i];
			if (v->name && strlen(v->name) == len && strncmp(v->name, name, len) == 0)
				return v;
		}
	for (int i = 0; i < S->globals.len; i++) {
		Symbol *v = S->globals.items[i];
		if (v->name && strlen(v->name) == len && strncmp(v->name, name, len) == 0)
			return v;
	}
	return NULL;
}

/* The type of a chain of names: Pet, Room.Pupils, Shop.Owner.Name */
static Type *chain_type(const char *base, size_t len)
{
	const char *dot = memchr(base, '.', len);
	size_t first = dot ? (size_t)(dot - base) : len;
	Symbol *v = find_variable(base, first);
	if (!v)
		return NULL;
	Type *t = v->type;
	while (dot) {
		const char *seg = dot + 1;
		const char *next = memchr(seg, '.', len - (size_t)(seg - base));
		size_t sl = next ? (size_t)(next - seg) : len - (size_t)(seg - base);
		while (t && t->kind == TY_REF)
			t = t->inner;
		if (!t || t->kind != TY_OBJECT)
			return NULL;
		Type *ft = NULL;
		for (int i = 0; i < t->obj->field_count; i++)
			if (strlen(t->obj->fields[i].name) == sl && strncmp(t->obj->fields[i].name, seg, sl) == 0)
				ft = t->obj->fields[i].type;
		if (!ft)
			return NULL;
		t = ft;
		dot = next;
	}
	while (t && t->kind == TY_REF)
		t = t->inner;
	return t;
}

/* functions whose names start "<prefix>.": a library's (time.now), or a
 * kind's or enum's methods (date.year) */
static void functions_named(Cands *c, const char *prefix, size_t pl)
{
	if (!R.table || !R.table->prog)
		return;
	for (int i = 0; i < R.table->prog->decls.len; i++) {
		Decl *d = R.table->prog->decls.items[i];
		if (d->kind != DECL_FN || d->is_static)
			continue;
		const char *nm = d->as.fn->name;
		if (strlen(nm) > pl && strncmp(nm, prefix, pl) == 0 && nm[pl] == '.' && !strchr(nm + pl + 1, '.'))
			cand_add(c, nm + pl + 1);
	}
}

static void members_of_type(Cands *c, Type *t)
{
	if (t->methods)
		functions_named(c, t->methods, strlen(t->methods));
	switch (t->kind) {
	case TY_STR:
		cand_add_all(c, str_member_names);
		break;
	case TY_LIST:
		cand_add_all(c, list_member_names);
		if (t->inner->kind == TY_STR)
			cand_add(c, "join");
		break;
	case TY_LOOKUP:
		cand_add_all(c, lookup_member_names);
		break;
	case TY_INT: case TY_FLOAT: case TY_BOOL:
		cand_add(c, "text");
		break;
	case TY_ARRAY: case TY_SLICE:
		cand_add(c, "length");
		break;
	case TY_FILE:
		cand_add(c, "read_line");
		cand_add(c, "at_end");
		break;
	case TY_ADDRESS:
		cand_add(c, "value");
		cand_add(c, "number");
		cand_add(c, "is_none");
		break;
	case TY_HANDLER:
		cand_add(c, "call");
		cand_add(c, "is_set");
		cand_add(c, "clear");
		break;
	case TY_OBJECT: {
		ObjectInfo *o = t->obj;
		for (int i = 0; i < o->field_count; i++)
			if (!o->fields[i].is_inner)   /* the session is outside every object */
				cand_add(c, o->fields[i].name);
		if (o->kind == OBJ_OBJECT)
			for (int k = 0; k < o->mro_len; k++) {
				ObjectDecl *d = o->mro[k]->decl;
				for (int m = 0; d && m < d->methods.len; m++)
					if (!d->methods.items[m]->is_destructor && !d->methods.items[m]->is_maker &&
					    !d->methods.items[m]->is_inner) {
						/* str_ is written J.str: a method named after a type */
						const char *nm = d->methods.items[m]->name;
						size_t nl = strlen(nm);
						if (nl > 1 && nm[nl - 1] == '_') {
							char *bare = strndup(nm, nl - 1);
							cand_add(c, bare);
							free(bare);
						} else {
							cand_add(c, nm);
						}
					}
			}
		break;
	}
	default:
		break;
	}
}

static bool header_used(const char *name)
{
	if (!R.table)
		return false;
	char want[64];
	snprintf(want, sizeof want, "/%s.hby", name);
	size_t wl = strlen(want);
	for (int i = 0; i < R.table->prog->files.len; i++) {
		const char *p = R.table->prog->files.items[i].path;
		size_t pl = p ? strlen(p) : 0;
		if ((pl >= wl && strcmp(p + pl - wl, want) == 0) || (p && strcmp(p, want + 1) == 0))
			return true;
	}
	return false;
}

/* Headers (`name.hby`) in a directory, as names without `.hby`; with
 * `dirs`, its subdirectories too, as `name/`. */
static void add_headers(Cands *c, const char *dir, bool dirs)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		const char *n = e->d_name;
		size_t len = strlen(n);
		if (n[0] == '.')
			continue;
		if (len > 4 && strcmp(n + len - 4, ".hby") == 0) {
			if (!dirs && retired_header_hint(n, (int)len - 4))
				continue;   /* an old library, still installed */
			char *base = strndup(n, len - 4);
			cand_add(c, base);
			free(base);
		} else if (dirs) {
			char *full = concat(dir, "/", n, "");
			struct stat st;
			if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
				char *sub = concat(n, "/", "", "");
				cand_add(c, sub);
				free(sub);
			}
			free(full);
		}
	}
	closedir(d);
}

/* A library not used yet (`bluetooth.` before `use bluetooth`): what its
 * header has, read from its text, as a used one's would be: functions
 * written "<type> name.what" and objects "object name.what" at the start
 * of a line. Calling one still asks for the `use`, which says so. */
static void unused_library(Cands *c, const char *base, size_t bl)
{
	if (bl == 0 || bl > 40 || memchr(base, '.', bl))
		return;
	char name[48];
	snprintf(name, sizeof name, "%.*s", (int)bl, base);
	for (size_t i = 0; i < bl; i++)
		if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
			return;
	if (header_used(name))
		return;
	for (const char *const *d = R.lib_dirs; d && *d; d++) {
		char *path = concat(*d, "/", name, ".hby");
		FILE *f = fopen(path, "r");
		free(path);
		if (!f)
			continue;
		/* each line's words that start "<name>.", up to its { or (: the
		 * last is the function's own name, unless it's a parameter's
		 * type, so a type of the library's own (bluetooth.device) is
		 * passed over when another follows it */
		char line[1024];
		while (fgets(line, sizeof line, f)) {
			if (line[0] == '\t' || line[0] == ' ' || line[0] == '/' || line[0] == '\n')
				continue;
			bool object = strncmp(line, "object ", 7) == 0;
			char *found[8];
			size_t lens[8];
			int n = 0;
			for (char *p = line; *p && *p != '{' && *p != '(' && n < 8; p++)
				if ((p == line || p[-1] == ' ') && strncmp(p, name, bl) == 0 && p[bl] == '.') {
					char *w = p + bl + 1, *e = w;
					while (isalnum((unsigned char)*e) || *e == '_')
						e++;
					if (e > w && (*e == ' ' || *e == '\n' || *e == '{')) {
						found[n] = w;
						lens[n++] = (size_t)(e - w);
					}
				}
			/* object X; or "<type> X ..." where X is the first after the
			 * return type: the second if the return type is the
			 * library's own */
			int pick = -1;
			if (object || n == 1)
				pick = 0;
			else if (n >= 2) {
				/* the return type ends where the first word does; the name comes next */
				char *after = found[0] + lens[0];
				while (*after == ' ')
					after++;
				pick = after == found[1] - bl - 1 ? 1 : 0;
			}
			if (pick >= 0) {
				char *word = strndup(found[pick], lens[pick]);
				cand_add(c, word);
				free(word);
			}
		}
		fclose(f);
		break;
	}
}

/* After `use` or `use local`: the headers that can be used. */
static int complete_use(const char *line, int pos, int *start, char ***items, bool *handled)
{
	const char *b = skip_ws(line);
	*handled = false;
	if (strncmp(b, "use", 3) != 0 || (b[3] != ' ' && b[3] != '\t'))
		return 0;
	const char *name = skip_ws(b + 3);
	bool local = strncmp(name, "local", 5) == 0 && (name[5] == ' ' || name[5] == '\t');
	if (local)
		name = skip_ws(name + 5);
	if (line + pos < name)
		return 0;   /* the cursor is before the name */
	*handled = true;
	const char *slash = NULL;
	for (const char *p = name; p < line + pos; p++)
		if (*p == '/')
			slash = p;
	const char *word = slash ? slash + 1 : name;
	*start = (int)(word - line);
	char *partial = strndup(word, (size_t)(line + pos - word));
	Cands c = { 0 };
	c.prefix = partial;
	c.plen = (int)strlen(partial);
	if (local) {
		/* next to the session: the directory it was started in */
		char *dir = slash ? strndup(name, (size_t)(slash - name)) : xstrdup(".");
		add_headers(&c, dir, true);
		free(dir);
	} else {
		if (!slash && strncmp("local", partial, strlen(partial)) == 0)
			cand_add(&c, "local");
		for (const char *const *d = R.lib_dirs; d && *d; d++)
			add_headers(&c, *d, false);
	}
	free(partial);
	for (int i = 1; i < c.n; i++)
		for (int j = i; j > 0 && strcmp(c.items[j - 1], c.items[j]) > 0; j--) {
			char *t = c.items[j];
			c.items[j] = c.items[j - 1];
			c.items[j - 1] = t;
		}
	*items = c.items;
	return c.n;
}

static int complete_word(const char *line, int pos, int *start, char ***items)
{
	bool is_use;
	int nu = complete_use(line, pos, start, items, &is_use);
	if (is_use)
		return nu;
	if (pos == 0 || line[pos - 1] == ' ' || line[pos - 1] == '/')
		return 0;   /* (only a use line completes after a space or a slash) */
	int w = pos;
	while (w > 0 && (isalnum((unsigned char)line[w - 1]) || line[w - 1] == '_' || line[w - 1] == '.'))
		w--;
	if (w < pos && isdigit((unsigned char)line[w]))
		return 0;   /* a number: 2.5 */
	const char *dot = NULL;
	for (int i = w; i < pos; i++)
		if (line[i] == '.')
			dot = line + i;
	Cands c = { 0 };
	Sema *S = R.table ? &R.table->sema : NULL;
	if (dot) {
		*start = (int)(dot - line) + 1;
		c.prefix = dot + 1;
		c.plen = pos - *start;
		const char *base = line + w;
		size_t bl = (size_t)(dot - base);
		char *partial = strndup(c.prefix, (size_t)c.plen);
		c.prefix = partial;
		if (bl == 5 && strncmp(base, "files", 5) == 0) {
			cand_add_all(&c, files_names);
		} else if (bl == 11 && strncmp(base, "directories", 11) == 0) {
			cand_add_all(&c, directories_names);
		} else if (bl == 5 && strncmp(base, "shell", 5) == 0) {
			cand_add_all(&c, shell_names);
		} else if (bl == 9 && strncmp(base, "addresses", 9) == 0) {
			cand_add_all(&c, addresses_names);
		} else if (bl == 6 && strncmp(base, "events", 6) == 0) {
			cand_add_all(&c, events_names);
		} else if (bl == 5 && strncmp(base, "cabby", 5) == 0) {
			cand_add_all(&c, cabby_names);
		} else if (bl > 0 && S) {
			/* a library's types (gui.window), or a variable's members */
			for (int i = 0; i < S->nobjects; i++) {
				const char *nm = S->objects[i]->name;
				if (strlen(nm) > bl && strncmp(nm, base, bl) == 0 && nm[bl] == '.')
					cand_add(&c, nm + bl + 1);
			}
			if (!chain_type(base, bl))
				functions_named(&c, base, bl);
			Type *t = chain_type(base, bl);
			if (t)
				members_of_type(&c, t);
			if (!t)
				unused_library(&c, base, bl);
		} else if (bl > 0) {
			unused_library(&c, base, bl);
		}
		free(partial);
	} else {
		*start = w;
		char *partial = strndup(line + w, (size_t)(pos - w));
		c.prefix = partial;
		c.plen = pos - w;
		static const char *const words[] = {
			"if", "else", "while", "do", "for", "in", "to", "down", "by", "switch", "case", "default", "break",
			"continue", "return", "goto", "sizeof", "const", "static", "list", "lookup", "object", "struct",
			"union", "enum", "aka", "callback", "until", "inner", "use", "true", "false", "self", "int", "long", "short", "char", "float",
			"double", "bool", "str", "void", "unsigned", "signed", "print", "println", "input", "read_line",
			"end_of_input", "read_char", "read_key", "stdout", "stderr", "exit", "abs", "close_to", "random", "random_seed", "cabby", NULL,
		};
		cand_add_all(&c, words);
		static const char *const libs[] = { "files", "directories", "shell", "processes", "events", "addresses", "time", "dates", "json", "bytes", "networks", "html", "xml", "gui", "gtk", "tui", "game", "tgame", "world", "sound", "bluetooth", "emails", "secrets", "volume", "brightness", "printing", "speech", "system", "videos", "os", "linux", "screens", "mouse", "keyboard", "csv", NULL };
		for (int i = 0; libs[i]; i++)
			if (header_used(libs[i]))
				cand_add(&c, libs[i]);
		if (S) {
			if (S->main_fn)
				for (int i = 0; i < S->main_fn->frame.len; i++)
					cand_add(&c, S->main_fn->frame.items[i]->name);
			for (int i = 0; i < S->globals.len; i++)
				cand_add(&c, S->globals.items[i]->name);
			for (int i = 0; i < S->nobjects; i++)
				if (!strchr(S->objects[i]->name, '.'))
					cand_add(&c, S->objects[i]->name);
			for (int i = 0; i < R.table->prog->decls.len; i++) {
				Decl *d = R.table->prog->decls.items[i];
				if (d->kind == DECL_FN && strncmp(d->as.fn->name, "cby_", 4) != 0 && strcmp(d->as.fn->name, "main") != 0)
					cand_add(&c, d->as.fn->name);
			}
		}
		free(partial);
	}
	/* sorted, so the list reads in order */
	for (int i = 1; i < c.n; i++)
		for (int j = i; j > 0 && strcmp(c.items[j - 1], c.items[j]) > 0; j--) {
			char *t = c.items[j];
			c.items[j] = c.items[j - 1];
			c.items[j - 1] = t;
		}
	*items = c.items;
	return c.n;
}

/* ============================================================== running */

static void on_sigint(int sig)
{
	(void)sig;
	interp_interrupted = 1;
}

/* After an input has run: show everything it printed (C's printf keeps
 * text without a newline in its buffer), and, at a keyboard, forget what
 * it was typed but didn't read (getchar takes one character; the rest of
 * that line, and its Enter, belong to that input, not to the next one). */
/* The session's colors: each line on its own, except that a line typed
 * inside a block comment the input opened starts colored as a comment. */
static bool line_in_comment;

static void color_line(const char *line, int len, unsigned char *cls)
{
	bool in_comment = line_in_comment;
	hl_classify(line, len, cls, &in_comment);
}

static void finish_io(void)
{
	fflush(stdout);
	rt_flush_files();   /* so another program (cat) sees what's been written */
	if (R.tty) {
		purge_input(stdin);
		/* Ctrl-D during one input's read_line ends only that input's reading:
		 * the keyboard is still there for the next one */
		clearerr(stdin);
		rt_input_ended = false;
	}
}

/* Runs the statements the newest input added to main. */
static void run_input(int first)
{
	struct sigaction sa, old;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_sigint;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, &old);
	interp_interrupted = 0;

	int status = 0;
	IRun r = isession_run(R.IS, first, &status);
	finish_io();

	sigaction(SIGINT, &old, NULL);
	interp_interrupted = 0;
	switch (r) {
	case IRUN_OK:
		break;
	case IRUN_RETURNED:
		R.status = status;
		R.ended = true;
		break;
	case IRUN_PANIC:
	case IRUN_INTERRUPTED: {
		if (r == IRUN_INTERRUPTED)
			fprintf(stderr, R.tty ? "\ninterrupted\n" : "interrupted\n");
		Chunk *c = &R.chunks[R.current];
		c->kind = CH_FAILED;
		c->why = r == IRUN_PANIC ? "panicked" : "was interrupted";
		resync();
		break;
	}
	}
}

/* Checks the session with the new chunk at index `ci`; if `quiet`
 * succeeds, checks again aloud (for its warnings). */
static Gen *try_chunk(int ci, bool quiet, bool *parsed)
{
	R.current = ci;
	Gen *g = check(quiet, parsed);
	if (g && quiet)
		g = check(false, NULL);
	return g;
}

static int find_function(const char *name)
{
	for (int i = 0; i < R.nchunks; i++)
		if (R.chunks[i].kind == CH_FUNC && strcmp(R.chunks[i].fname, name) == 0)
			return i;
	return -1;
}

/* Checks one complete input and, if it is accepted, runs it. Takes
 * ownership of `text`. */
static void process(char *text)
{
	bool is_use;
	char *fname;
	ChunkKind kind = classify(text, &fname, &is_use);
	int first = R.main_stmts;
	Gen *g = NULL;
	int ci;

	if (kind == CH_FUNC && strcmp(fname, "main") == 0) {
		fprintf(stderr, "error: the session is already the body of main; type its statements directly\n");
		free(fname);
		free(text);
		return;
	}

	if (kind == CH_STMT) {
		free(fname);
		/* echo forms first, quietly; then the input as typed */
		char *forms[2] = { NULL, NULL };
		int nf = echo_forms(text, forms);
		bool form0_parsed = false;
		for (int i = 0; i < nf && !g; i++) {
			push_chunk(CH_STMT, forms[i], (int)strlen("println "));
			forms[i] = NULL;
			bool parsed = false;
			g = try_chunk(R.nchunks - 1, true, &parsed);
			if (i == 0)
				form0_parsed = parsed;
			if (!g)
				pop_chunk();
		}
		for (int i = 0; i < nf; i++)
			free(forms[i]);
		if (g) {
			free(text);
		} else {
			push_chunk(CH_STMT, text, 0);
			ci = R.nchunks - 1;
			bool parsed = true;
			g = try_chunk(ci, nf > 0, &parsed);
			if (!g) {
				if (nf > 0 && form0_parsed) {
					/* it reads as an expression (`x + y` with y undeclared, a
					 * moved variable on its own): the echo form says why */
					char *again = concat("println ", skip_ws(R.chunks[ci].text), "", "");
					pop_chunk();
					push_chunk(CH_STMT, again, (int)strlen("println "));
					try_chunk(R.nchunks - 1, false, NULL);
				} else if (nf > 0) {
					try_chunk(ci, false, NULL);   /* now with its messages */
				}
				pop_chunk();
				R.current = -1;
				return;
			}
		}
		ci = R.nchunks - 1;
	} else if (kind == CH_FUNC && (ci = find_function(fname)) >= 0) {
		/* a new definition replaces the old one, in place */
		Chunk saved = R.chunks[ci];
		R.chunks[ci].id = R.next_id++;
		R.chunks[ci].text = text;
		R.chunks[ci].fname = fname;
		g = try_chunk(ci, false, NULL);
		if (!g) {
			fprintf(stderr, "note: the earlier definition of '%s' is kept\n", fname);
			free(text);
			free(fname);
			R.chunks[ci] = saved;
			R.current = -1;
			return;
		}
		free(saved.text);
		free(saved.fname);
	} else {
		Chunk *c = push_chunk(kind, text, 0);
		c->fname = fname;
		c->is_use = is_use;
		ci = R.nchunks - 1;
		g = try_chunk(ci, false, NULL);
		if (!g) {
			pop_chunk();
			R.current = -1;
			return;
		}
	}

	if (!adopt(g)) {
		pop_chunk();
		resync();
		R.current = -1;
		return;
	}
	if (R.chunks[ci].kind == CH_STMT)
		run_input(first);
	R.current = -1;
}

/* ============================================================== input */

typedef enum { RD_LINE, RD_EOF, RD_INTERRUPT } ReadStatus;

static ReadStatus read_line(const char *prompt, const char *prefill, char **out)
{
	*out = NULL;
	if (R.tty && rt_line_open)
		putchar('\n');
	rt_line_open = false;
	fflush(stdout);
	if (R.tty) {
		LeStatus st = le_read(R.le, prompt, prefill, true, out);
		if (st == LE_OK) {
			le_history_add(R.le, *out);
			return RD_LINE;
		}
		return st == LE_INTERRUPT ? RD_INTERRUPT : RD_EOF;
	}
	char *line = NULL;
	size_t cap = 0;
	ssize_t n = getline(&line, &cap, stdin);
	if (n < 0) {
		free(line);
		return RD_EOF;
	}
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = '\0';
	*out = line;
	return RD_LINE;
}

/* Indentation for the next line of an input: as the line before, one more
 * after a `{` or a case label. */
static char *auto_indent(const char *text)
{
	const char *last = text, *p = text;
	for (; *p; p++)
		if (*p == '\n' && !is_blank(p + 1))
			last = p + 1;
	int n = leading_tabs(last);
	const char *e = last + strlen(last);
	const char *nl = strchr(last, '\n');
	if (nl)
		e = nl;
	/* a comment at the end doesn't count: `if x > 0 {    // why` opens */
	int len = (int)(e - last);
	if (len > 0) {
		unsigned char *cls = malloc((size_t)len);
		bool in_comment = hl_comment_open_at(text, last);
		hl_classify(last, len, cls, &in_comment);
		for (int i = 0; i < len; i++)
			if (cls[i] == HL_COMMENT && !(i > 0 && cls[i - 1] == HL_COMMENT)) {
				e = last + i;
				break;
			}
		free(cls);
	}
	while (e > last && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		e--;
	const char *b = skip_ws(last);
	if (e > last && e[-1] == '{')
		n++;
	else if (e > last && e[-1] == ':' && (strncmp(b, "case", 4) == 0 || strncmp(b, "default", 7) == 0))
		n++;
	char *s = malloc((size_t)n + 1);
	memset(s, '\t', (size_t)n);
	s[n] = '\0';
	return s;
}

/* Inside a block comment that began a line, the next line starts as Vim
 * starts it: the comment's tabs, then " * ". NULL when the comment began
 * after code on its line (`x = 4`, then a comment), or no comment is open. */
static char *comment_leader(const char *text)
{
	if (!hl_comment_open_at(text, text + strlen(text)))
		return NULL;
	const char *last = text, *p = text;
	for (; *p; p++)
		if (*p == '\n' && !is_blank(p + 1))
			last = p + 1;
	int n = leading_tabs(last);
	const char *b = last + n;
	while (*b == ' ')
		b++;
	if (!(b[0] == '/' && b[1] == '*') && b[0] != '*')
		return NULL;
	char *s = malloc((size_t)n + 4);
	memset(s, '\t', (size_t)n);
	memcpy(s + n, " * ", 4);
	return s;
}

static void append_line(char **text, const char *line)
{
	char *t = *text ? concat(*text, "\n", line, "") : xstrdup(line);
	free(*text);
	*text = t;
}

/* the line, without spaces around it, is this word? */
static bool line_is(const char *line, const char *word)
{
	const char *b = skip_ws(line);
	size_t n = strlen(b), wl = strlen(word);
	while (n && (b[n - 1] == ' ' || b[n - 1] == '\t' || b[n - 1] == '\r'))
		n--;
	return n == wl && memcmp(b, word, wl) == 0;
}

/* Does the session have something of its own called this (a function, a
 * global, or one from a header it uses, such as standard's exit)? */
static bool session_has(const char *name)
{
	if (find_variable(name, strlen(name)))
		return true;
	if (!R.table || !R.table->prog)
		return false;
	for (int i = 0; i < R.table->prog->decls.len; i++) {
		Decl *d = R.table->prog->decls.items[i];
		if (d->kind == DECL_FN && strcmp(d->as.fn->name, name) == 0)
			return true;
		if (d->kind == DECL_VAR)
			for (int j = 0; j < d->as.vars.len; j++)
				if (strcmp(d->as.vars.items[j]->name, name) == 0)
					return true;
	}
	return false;
}

/* `exit` ends the session, unless the session has an exit of its own;
 * `cabby.exit` always does. */
static bool is_exit(const char *line)
{
	return line_is(line, "cabby.exit") || (line_is(line, "exit") && !session_has("exit"));
}

static void process(char *text);

/* ---- the session's own commands: cabby.clear, cabby.preserve, ... */

/* where cabby.preserve keeps a session: ~/.cabby_sessions/NAME.cby */
static char *session_file(const char *name)
{
	const char *home = getenv("HOME");
	if (!home || !*home)
		home = ".";
	char *dir = concat(home, "/.cabby_sessions", "", "");
	mkdir(dir, 0755);
	char *path = concat(dir, "/", name, ".cby");
	free(dir);
	return path;
}

/* "cabby.preserve work" -> "work"; "" if no name; NULL if the line isn't `cmd` */
static char *command_arg(const char *line, const char *cmd)
{
	const char *b = skip_ws(line);
	size_t cl = strlen(cmd);
	if (strncmp(b, cmd, cl) != 0 || (b[cl] && b[cl] != ' ' && b[cl] != '\t' && b[cl] != '\r'))
		return NULL;
	const char *a = skip_ws(b + cl);
	size_t n = strlen(a);
	while (n && (a[n - 1] == ' ' || a[n - 1] == '\t' || a[n - 1] == '\r' || a[n - 1] == '"'))
		n--;
	if (*a == '"') {
		a++;
		if (n)
			n--;
	}
	return strndup(a, n);
}

#define PRESERVE_MARK "// ---- cabby.preserve: one input"

static void preserve(const char *name)
{
	char *path = session_file(*name ? name : "session");
	FILE *f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "cannot write '%s'\n", path);
		free(path);
		return;
	}
	int n = 0;
	for (int i = 0; i < R.nchunks; i++) {
		if (R.chunks[i].kind == CH_FAILED)
			continue;
		fprintf(f, "%s\n%s\n", PRESERVE_MARK, R.chunks[i].text + R.chunks[i].prefix);
		n++;
	}
	fclose(f);
	printf("Session preserved (%d input%s) in %s; cabby.restore%s%s brings it back\n", n, n == 1 ? "" : "s", path,
	       *name ? " " : "", name);
	free(path);
}

static void restore(const char *name)
{
	char *path = session_file(*name ? name : "session");
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "no preserved session called '%s' (cabby.preserve%s%s makes one)\n", *name ? name : "session",
			*name ? " " : "", name);
		free(path);
		return;
	}
	/* each input runs again, as if typed, with what it prints hidden */
	char *line = NULL, *text = NULL;
	size_t cap = 0;
	ssize_t got;
	int n = 0;
	fflush(stdout);
	int saved = dup(1), null = open("/dev/null", O_WRONLY);
	if (null >= 0)
		dup2(null, 1);
	while ((got = getline(&line, &cap, f)) >= 0) {
		if (got && line[got - 1] == '\n')
			line[--got] = '\0';
		if (strcmp(line, PRESERVE_MARK) == 0) {
			if (text) {
				process(text);
				n++;
			}
			text = NULL;
			continue;
		}
		append_line(&text, line);
	}
	if (text) {
		process(text);
		n++;
	}
	fflush(stdout);
	if (saved >= 0) {
		dup2(saved, 1);
		close(saved);
	}
	if (null >= 0)
		close(null);
	free(line);
	fclose(f);
	printf("Session restored (%d input%s) from %s\n", n, n == 1 ? "" : "s", path);
	free(path);
}

static void remove_session(const char *name)
{
	char *path = session_file(*name ? name : "session");
	if (remove(path) == 0)
		printf("Removed the preserved session '%s'\n", *name ? name : "session");
	else
		fprintf(stderr, "no preserved session called '%s' (cabby.sessions lists them)\n", *name ? name : "session");
	free(path);
}

static void list_sessions(void)
{
	char *probe = session_file("x");
	char *slash = strrchr(probe, '/');
	*slash = '\0';
	DIR *d = opendir(probe);
	int n = 0;
	struct dirent *e;
	while (d && (e = readdir(d))) {
		size_t l = strlen(e->d_name);
		if (l > 4 && strcmp(e->d_name + l - 4, ".cby") == 0) {
			printf("%.*s\n", (int)(l - 4), e->d_name);
			n++;
		}
	}
	if (d)
		closedir(d);
	if (!n)
		printf("No preserved sessions yet (cabby.preserve keeps one)\n");
	free(probe);
}

/* the session's own commands; true if the line was one */
static bool session_command(const char *line)
{
	char *arg;
	if (line_is(line, "cabby.clear")) {
		fputs("\x1b[H\x1b[2J\x1b[3J", stdout);   /* home, clear the screen and what scrolled off */
		fflush(stdout);
		return true;
	}
	if (line_is(line, "cabby.help")) {
		printf("cabby.exit              end the session (so does 'exit', unless the session has an exit of its own)\n"
		       "cabby.clear             clear the screen\n"
		       "cabby.preserve [name]   keep the session, to bring back another time\n"
		       "cabby.restore [name]    bring a preserved session back (its inputs run again, quietly)\n"
		       "cabby.remove [name]     remove a preserved session\n"
		       "cabby.sessions          list the preserved sessions\n"
		       "cabby.license           Cabby's license (MIT)\n"
		       "cabby.version           which Cabby this is\n"
		       "cabby.help              this list\n");
		return true;
	}
	if (line_is(line, "cabby.license")) {
		fputs(CABBY_LICENSE_TEXT, stdout);
		return true;
	}
	if (line_is(line, "cabby.version")) {
		printf("Cabby %s (%s)\n%s\n", CABBY_VERSION, CABBY_VERSION_DATE, CABBY_COPYRIGHT);
		return true;
	}
	if ((arg = command_arg(line, "cabby.preserve"))) {
		preserve(arg);
		free(arg);
		return true;
	}
	if (line_is(line, "cabby.sessions")) {
		list_sessions();
		return true;
	}
	if ((arg = command_arg(line, "cabby.remove"))) {
		remove_session(arg);
		free(arg);
		return true;
	}
	if ((arg = command_arg(line, "cabby.restore"))) {
		restore(arg);
		free(arg);
		return true;
	}
	return false;
}

/* Reads one complete input. Returns NULL at `exit` or end of input. */
static char *read_input(void)
{
	for (;;) {
		char *line;
		if (R.pushback) {
			line = R.pushback;
			R.pushback = NULL;
		} else {
			ReadStatus st = read_line(">>> ", "", &line);
			if (st == RD_EOF)
				return NULL;
			if (st == RD_INTERRUPT)
				continue;
		}
		if (is_exit(line)) {
			free(line);
			return NULL;
		}
		if (session_command(line)) {
			free(line);
			continue;
		}
		if (is_blank(line)) {
			free(line);
			continue;
		}
		char *text = NULL;
		append_line(&text, line);
		free(line);

		for (;;) {
			Completeness c = completeness(text);
			if (c == IN_COMPLETE)
				return text;
			char *indent = c == IN_IF ? xstrdup("") : comment_leader(text);
			if (!indent)
				indent = auto_indent(text);
			if (c == IN_HEADER) {
				/* `{` goes at the header's own indentation */
				free(indent);
				const char *last = strrchr(text, '\n');
				last = last ? last + 1 : text;
				int n = leading_tabs(last);
				indent = malloc((size_t)n + 1);
				memset(indent, '\t', (size_t)n);
				indent[n] = '\0';
			}
			line_in_comment = hl_comment_open_at(text, text + strlen(text));
			ReadStatus st = read_line("... ", indent, &line);
			line_in_comment = false;
			free(indent);
			if (st == RD_EOF) {
				free(text);
				return NULL;
			}
			if (st == RD_INTERRUPT) {
				free(text);
				text = NULL;
				break;
			}
			if (c == IN_IF) {
				const char *b = skip_ws(line);
				if (is_blank(line)) {
					free(line);
					return text;
				}
				if (strncmp(b, "else", 4) == 0 && !isalnum((unsigned char)b[4]) && b[4] != '_') {
					append_line(&text, line);
					free(line);
					continue;
				}
				R.pushback = line;   /* the next input has started */
				return text;
			}
			if (c == IN_HEADER && is_blank(line)) {
				free(line);
				return text;   /* e.g. a prototype */
			}
			append_line(&text, line);
			free(line);
		}
	}
}

/* ============================================================== saving */

static bool yes(const char *s)
{
	s = skip_ws(s);
	return (s[0] == 'y' || s[0] == 'Y') && (s[1] == '\0' || strcasecmp(s, "yes") == 0 || is_blank(s + 1));
}

static void offer_save(void)
{
	if (R.nchunks == 0)
		return;   /* nothing was entered */

	char *answer;
	if (read_line("Save this session? [y/N] ", "", &answer) != RD_LINE)
		return;
	bool save = yes(answer);
	free(answer);
	if (!save)
		return;
	for (;;) {
		char *name;
		if (read_line("File name: ", "", &name) != RD_LINE)
			return;
		char *trimmed = xstrdup(skip_ws(name));
		free(name);
		size_t n = strlen(trimmed);
		while (n && (trimmed[n - 1] == ' ' || trimmed[n - 1] == '\t' || trimmed[n - 1] == '\r'))
			trimmed[--n] = '\0';
		if (n == 0) {
			free(trimmed);
			printf("Not saved.\n");
			return;
		}
		const char *base = strrchr(trimmed, '/');
		base = base ? base + 1 : trimmed;
		char *path = strchr(base, '.') ? xstrdup(trimmed) : concat(trimmed, ".cby", "", "");
		free(trimmed);

		struct stat st;
		if (stat(path, &st) == 0) {
			char *q = concat(path, " already exists. Overwrite it? [y/N] ", "", "");
			char *ans;
			ReadStatus rs = read_line(q, "", &ans);
			free(q);
			if (rs != RD_LINE) {
				free(path);
				return;
			}
			bool ok = yes(ans);
			free(ans);
			if (!ok) {
				free(path);
				continue;
			}
		}
		FILE *f = fopen(path, "w");
		if (!f) {
			fprintf(stderr, "cannot write '%s': ", path);
			perror(NULL);
			free(path);
			continue;
		}
		char *src = build_source(true, NULL, NULL);
		fputs(src, f);
		free(src);
		if (fclose(f) != 0) {
			fprintf(stderr, "cannot write '%s'\n", path);
			free(path);
			continue;
		}
		printf("Session saved to %s\n", path);
		free(path);
		return;
	}
}

/* ============================================================== main loop */

static void *session_thread(void *arg)
{
	(void)arg;
	R.IS = isession_new();
	diag_set_mapper(map_loc);
	parser_tabs_added = tabs_added;
	diag_set_filter(filter);
	R.current = -1;
	resync();   /* the empty session: an empty main */

	if (R.tty)
		printf("Cabby %s interactive session. Type statements and declarations; \"exit\" to finish, \"cabby.help\" for more.\n"
		       "%s; \"cabby.license\" for details.\n",
		       CABBY_VERSION, CABBY_WARRANTY);

	while (!R.ended) {
		char *text = read_input();
		if (!text)
			break;
		process(text);
	}

	/* drop every variable (destructors run) against the running check */
	if (R.table != R.cur)
		resync();
	isession_end(R.IS);
	offer_save();
	return NULL;
}

int repl_main(const char *const *lib_dirs)
{
	memset(&R, 0, sizeof R);
	R.lib_dirs = lib_dirs;
	R.tty = isatty(0) && isatty(1);
	if (R.tty) {
		const char *home = getenv("HOME");
		char *hist = home && *home ? concat(home, "/.cabby_history", "", "") : NULL;
		R.le = le_new(hist);
		free(hist);
		if (hl_enabled(1))
			le_set_colors(R.le, color_line, hl_start);
		le_set_completer(R.le, complete_word);
	}

	/* the session runs on a thread with a big stack -- except on macOS,
	 * where only the first thread may open windows */
#if defined(__APPLE__)
	session_thread(NULL);
#else
	pthread_attr_t attr;
	pthread_t th;
	bool threaded = pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, STACK_SIZE) == 0 &&
			pthread_create(&th, &attr, session_thread, NULL) == 0;
	if (threaded)
		pthread_join(th, NULL);
	else
		session_thread(NULL);
	pthread_attr_destroy(&attr);
#endif

	le_free(R.le);
	for (int i = 0; i < R.nchunks; i++) {
		free(R.chunks[i].text);
		free(R.chunks[i].fname);
	}
	free(R.chunks);
	if (R.table != R.cur)
		gen_free(R.table);
	gen_free(R.cur);
	diag_set_mapper(NULL);
	parser_tabs_added = NULL;
	diag_set_filter(NULL);
	return R.status;
}
