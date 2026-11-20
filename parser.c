#include "parser.h"

#include <limits.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "diag.h"
#include "lexer.h"
#include "util.h"

/* ================================================================ loader */

typedef struct Loader {
	Arena *A;
	Program *prog;
	const char *const *lib_dirs;
	VEC(const char *) loaded;    /* canonical paths already parsed (or in progress) */
	VEC(const char *) types;     /* every type name declared so far */
	VEC(const char *) namespaces;/* prefixes of dotted names: "gui" for gui.window */
	VEC(const char *) callables; /* functions and methods that take arguments */
	VEC(const char *) fields;    /* field names of objects, structs and unions */
	int depth;
} Loader;

typedef struct Parser {
	Loader *ld;
	Lexer lex;
	Arena *arena;
	int file;                    /* diag file id */
	const char *path;
	Token cur;
	Token prev;
	bool panic;
	bool no_init;                /* inside an if/while/for/switch header: `T{` is not an initializer */
	bool in_switch_case;         /* a case label line is legal */
	int layout_errors;           /* per-block indentation error budget */
	int stmt_indent;             /* the statement being read's line's indent (for an else block) */
} Parser;

static bool is_type_name(Parser *P, const char *s, int len)
{
	for (int i = 0; i < P->ld->types.len; i++) {
		const char *t = P->ld->types.items[i];
		if ((int)strlen(t) == len && memcmp(t, s, (size_t)len) == 0)
			return true;
	}
	return false;
}

static bool loader_is_namespace(void *ctx, const char *s, int len)
{
	Loader *ld = ctx;
	for (int i = 0; i < ld->namespaces.len; i++)
		if ((int)strlen(ld->namespaces.items[i]) == len && memcmp(ld->namespaces.items[i], s, (size_t)len) == 0)
			return true;
	return false;
}

static void add_type_name(Loader *ld, const char *s, int len)
{
	for (int i = 0; i < ld->types.len; i++)
		if ((int)strlen(ld->types.items[i]) == len && memcmp(ld->types.items[i], s, (size_t)len) == 0)
			return;
	vec_push(ld->A, ld->types, arena_strndup(ld->A, s, (size_t)len));
	/* every prefix of a dotted name is a namespace: gui.window -> gui */
	for (int i = 0; i < len; i++)
		if (s[i] == '.' && !loader_is_namespace(ld, s, i))
			vec_push(ld->A, ld->namespaces, arena_strndup(ld->A, s, (size_t)i));
}

static void add_namespace(Loader *ld, const char *s, int len)
{
	if (!loader_is_namespace(ld, s, len))
		vec_push(ld->A, ld->namespaces, arena_strndup(ld->A, s, (size_t)len));
}

/* Collects the type names a file declares, without reporting anything.
 * A declared name may be dotted (`object gui.window`): that makes its
 * prefix a namespace. */
static void prescan_types(Loader *ld, const char *path, const char *src)
{
	Lexer L;
	lexer_init(&L, ld->A, path, src);
	L.quiet = true;
	TokenKind prev = TOK_NEWLINE;
	Token prev_tok = { 0 }, prev2_tok = { 0 };
	bool in_typedef = false;
	bool in_callback = false;   /* after `callback`, before its name */
	Token last_ident = { 0 };
	for (;;) {
		Token t = lexer_next(&L);
		if (t.kind == TOK_EOF)
			break;
		/* `callback int comparer ...`: the first name after the type it
		 * gives (a keyword, or a type already known) is the new type's */
		if (t.kind == TOK_KW_CALLBACK) {
			in_callback = true;
		} else if (in_callback && t.kind == TOK_NEWLINE) {
			in_callback = false;
		} else if (in_callback && t.kind == TOK_IDENT) {
			bool known = false;
			for (int i = 0; i < ld->types.len && !known; i++)
				known = (int)strlen(ld->types.items[i]) == t.length &&
					memcmp(ld->types.items[i], t.start, (size_t)t.length) == 0;
			if (!known && !(t.length == 7 && memcmp(t.start, "address", 7) == 0)) {
				add_type_name(ld, t.start, t.length);
				in_callback = false;
			}
		}
		if (t.kind == TOK_IDENT && (prev == TOK_KW_OBJECT || prev == TOK_KW_STRUCT ||
					    prev == TOK_KW_UNION || prev == TOK_KW_ENUM)) {
			/* gui . window before `gui` is known as a namespace */
			const char *end = t.start + t.length;
			for (;;) {
				Lexer copy = L;
				Token d = lexer_next(&copy);
				if (d.kind != TOK_DOT || d.start != end)
					break;
				Token n = lexer_next(&copy);
				if ((n.kind != TOK_IDENT && !token_is_keyword(n.kind)) || n.start != end + 1)
					break;
				L = copy;
				end = n.start + n.length;
			}
			add_type_name(ld, t.start, (int)(end - t.start));
			/* an enum's name is also where its methods' names start (weekday.text) */
			if (prev == TOK_KW_ENUM)
				add_namespace(ld, t.start, (int)(end - t.start));
		}
		/* `long kind duration`: a type, and where its methods' names start */
		if (t.kind == TOK_IDENT && prev_tok.kind == TOK_IDENT && prev_tok.length == 4 &&
		    memcmp(prev_tok.start, "kind", 4) == 0 && token_is_keyword(prev2_tok.kind)) {
			add_type_name(ld, t.start, t.length);
			add_namespace(ld, t.start, t.length);
		}
		/* `double aka celsius`: the name after aka is a type */
		if (t.kind == TOK_KW_AKA) {
			in_typedef = true;
			last_ident.kind = TOK_EOF;
		} else if (in_typedef && t.kind == TOK_IDENT && last_ident.kind != TOK_IDENT) {
			last_ident = t;
		} else if (in_typedef && t.kind == TOK_NEWLINE) {
			if (last_ident.kind == TOK_IDENT)
				add_type_name(ld, last_ident.start, last_ident.length);
			in_typedef = false;
		}
		prev = t.kind;
		prev2_tok = prev_tok;
		prev_tok = t;
	}
}

static void parse_file(Loader *ld, const char *path, SrcLoc from);

/* At `kind` in `long kind duration` (or `..., steps T`)? A function called
 * kind (`void kind char c {`) has something else after the name. */
static bool kind_decl_here(struct Parser *P);
static void brace_on_next_line(struct Parser *P);
static bool maker_here(struct Parser *P, ObjectDecl *obj);

/* gui, game, gtk and videos need a library installed to be built; when it wasn't,
 * their header is there but their native code isn't. The message says what
 * to install. NULL: nothing missing. */
static const char *missing_library_hint(const char *header, const char *name, int n)
{
	static const struct { const char *name, *message; } needs[] = {
		{ "gui", "the gui library isn't installed: it needs SDL 3 (sdl3 on Arch, libsdl3-dev on Debian, "
			 "'brew install sdl3' on a Mac), then Cabby built and installed again. Or 'use gtk' for the GTK one" },
		{ "game", "the game library isn't installed: it needs SDL 3 (sdl3 on Arch, libsdl3-dev on Debian, "
			  "'brew install sdl3' on a Mac), then Cabby built and installed again" },
		{ "world", "the world library isn't installed: it needs SDL 3 (sdl3 on Arch, libsdl3-dev on Debian, "
			   "'brew install sdl3' on a Mac), then Cabby built and installed again" },
		{ "tgame", "the tgame library isn't installed: it needs SDL 3 (sdl3 on Arch, libsdl3-dev on Debian, "
			   "'brew install sdl3' on a Mac), which draws the game off screen for the terminal, then Cabby "
			   "built and installed again" },
		{ "videos", "the videos library isn't installed: it needs FFmpeg's libraries (ffmpeg on Arch, "
			    "libavformat-dev, libavcodec-dev, libswscale-dev and libswresample-dev on Debian, "
			    "'brew install ffmpeg' on a Mac), then Cabby built and installed again" },
		{ "gtk", "the gtk library isn't installed: it needs GTK 4 (gtk4 on Arch, libgtk-4-dev on Debian, "
			 "'brew install gtk4' on a Mac), then Cabby built and installed again. Or 'use gui', drawn with SDL 3" },
	};
	for (size_t i = 0; i < sizeof needs / sizeof *needs; i++) {
		if ((int)strlen(needs[i].name) != n || memcmp(name, needs[i].name, (size_t)n) != 0)
			continue;
		const char *slash = strrchr(header, '/');
		char lib[4096];
		snprintf(lib, sizeof lib, "%.*s/libcabby%.*s.so", slash ? (int)(slash - header) : 1, slash ? header : ".", n, name);
		struct stat st;
		if (stat(lib, &st) != 0)
			return needs[i].message;
	}
	return NULL;
}

const char *retired_header_hint(const char *name, int len)
{
	static const struct { const char *old, *hint; } renamed[] = {
		{ "string", " (it is called 'strings' now: use strings)" },
		{ "stdio", " (printf is in 'standard' now: use standard; for the rest, Cabby has "
			   "println, print, read_line and input)" },
		{ "stdlib", " (it is part of 'standard' now: use standard; for atoi and atof, "
			    "Cabby has .to_int and .to_double)" },
	};
	for (size_t r = 0; r < sizeof renamed / sizeof *renamed; r++)
		if ((size_t)len == strlen(renamed[r].old) && memcmp(name, renamed[r].old, (size_t)len) == 0)
			return renamed[r].hint;
	return NULL;
}

/* ================================================================ basics */

/* `~window` (or `~gui.window`) for `object gui.window` */
static bool destructor_name_ok(Token t, const char *obj)
{
	const char *last = strrchr(obj, '.');
	size_t n = strlen(obj);
	if ((size_t)t.length == n && memcmp(t.start, obj, n) == 0)
		return true;
	return last && (size_t)t.length == strlen(last + 1) && memcmp(t.start, last + 1, strlen(last + 1)) == 0;
}

static SrcLoc loc_of(Parser *P, Token t) { return (SrcLoc){ P->file, t.line, t.col }; }

static void advance(Parser *P)
{
	P->prev = P->cur;
	P->cur = lexer_next(&P->lex);
}

static Token peek(Parser *P)
{
	Lexer copy = P->lex;
	copy.quiet = true;
	return lexer_next(&copy);
}

/* the token after the next one */
static Token peek2(Parser *P)
{
	Lexer copy = P->lex;
	copy.quiet = true;
	lexer_next(&copy);
	return lexer_next(&copy);
}

static bool starts_type(Parser *P, Token t);

/* At a `(`: does a cast `(T)x` start here? Not for `(T.make 3)`, where
 * a type's name is followed by a dot: that calls one of its static methods. */
static bool cast_here(Parser *P)
{
	Lexer copy = P->lex;
	copy.quiet = true;
	Token t = lexer_next(&copy);
	if (!starts_type(P, t))
		return false;
	return t.kind != TOK_IDENT || lexer_next(&copy).kind != TOK_DOT;
}

static bool check(const Parser *P, TokenKind k) { return P->cur.kind == k; }

static bool match(Parser *P, TokenKind k)
{
	if (!check(P, k))
		return false;
	advance(P);
	return true;
}

static const char *tok_text(Parser *P, Token t)
{
	return arena_strndup(P->arena, t.start, (size_t)t.length);
}

static const char *describe(Token t)
{
	static char buf[96];
	switch (t.kind) {
	case TOK_NEWLINE: return "end of line";
	case TOK_EOF:     return "end of file";
	case TOK_IDENT:
		snprintf(buf, sizeof buf, "identifier '%.*s'", t.length > 40 ? 40 : t.length, t.start);
		return buf;
	case TOK_INT: case TOK_FLOAT: case TOK_STRING: case TOK_CHAR:
		snprintf(buf, sizeof buf, "%s %.*s", token_kind_name(t.kind), t.length > 40 ? 40 : t.length, t.start);
		return buf;
	default:
		if (token_is_keyword(t.kind))
			snprintf(buf, sizeof buf, "the keyword '%s'", token_kind_name(t.kind));
		else
			snprintf(buf, sizeof buf, "'%s'", token_kind_name(t.kind));
		return buf;
	}
}

static void error_at(Parser *P, Token t, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void error_at(Parser *P, Token t, const char *fmt, ...)
{
	if (P->panic)
		return;
	P->panic = true;
	if (t.kind == TOK_ERROR)  /* the lexer already explained this one */
		return;
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	diag_set_file(P->file);
	diag_error(t.line, t.col, "%s", msg);
}

/* `is`, `and`, `or` and `not` were Cabby's words for ==, &&, || and ! until
 * 0.36.0: each is an error saying what to write, and is read as before, so
 * the rest of the line is checked too (and one slip doesn't bring more) */
static void old_word(Parser *P, Token t, const char *word, const char *instead, const char *example)
{
	diag_set_file(P->file);
	diag_error(t.line, t.col, "Cabby writes '%s', not '%s': %s", instead, word, example);
}

static void warn_at(Parser *P, SrcLoc l, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void warn_at(Parser *P, SrcLoc l, const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	diag_report_in(DIAG_WARNING, P->file, l.line, l.col, "%s", msg);
}

static bool expect(Parser *P, TokenKind k, const char *what)
{
	if (match(P, k))
		return true;
	error_at(P, P->cur, "expected %s, found %s", what, describe(P->cur));
	return false;
}

static bool expect_newline(Parser *P, const char *after)
{
	if (match(P, TOK_NEWLINE))
		return true;
	if (check(P, TOK_SEMI))
		error_at(P, P->cur, "unexpected ';' after %s; statements end at the end of the line", after);
	else
		error_at(P, P->cur, "expected end of line after %s, found %s", after, describe(P->cur));
	return false;
}

/* Layout check for a token that must start a line at `indent` tabs. */
int (*parser_tabs_added)(int file, int line);

static void check_layout(Parser *P, Token t, int indent, const char *what)
{
	if (t.kind == TOK_EOF || P->layout_errors > 0)
		return;
	if (t.space_col) {
		P->layout_errors++;
		diag_report_in(DIAG_ERROR, P->file, t.line, t.space_col,
			       "indentation must use tabs only (found a space)");
		static bool hinted = false;
		if (!hinted) {
			hinted = true;
			diag_report_in(DIAG_NOTE, P->file, 0, 0,
				       "Cabby indents each block with one tab character; set your editor to "
				       "insert tabs, not spaces, for .cby files");
		}
		return;
	}
	if (t.indent != indent) {
		P->layout_errors++;
		/* (in the session, the tabs it added itself aren't counted) */
		int added = parser_tabs_added ? parser_tabs_added(P->file, t.line) : 0;
		int want = indent - added, got = t.indent - added;
		if (want < 0 || got < 0)
			want = indent, got = t.indent;
		diag_report_in(DIAG_ERROR, P->file, t.line, t.col,
			       "%s should be indented by %d tab%s, not %d", what, want, want == 1 ? "" : "s", got);
	}
}

/* Skip to the start of the next statement in the current block: the end
 * of this line (or of a braced construct it opens), stopping before a `}`
 * that closes the enclosing block. */
static void synchronize(Parser *P)
{
	int depth = 0;
	P->panic = false;
	P->layout_errors++;   /* the rest of this block's layout is suspect now */
	for (;;) {
		switch (P->cur.kind) {
		case TOK_EOF:
			return;
		case TOK_LBRACE:
			depth++;
			advance(P);
			continue;
		case TOK_RBRACE:
			if (depth == 0)
				return;
			depth--;
			advance(P);
			continue;
		case TOK_NEWLINE:
			advance(P);
			if (depth == 0)
				return;
			continue;
		default:
			advance(P);
			continue;
		}
	}
}

/* ================================================================ types */

static bool is_builtin_type_kw(TokenKind k)
{
	switch (k) {
	case TOK_KW_CHAR: case TOK_KW_SHORT: case TOK_KW_INT: case TOK_KW_LONG:
	case TOK_KW_SIGNED: case TOK_KW_UNSIGNED: case TOK_KW_FLOAT: case TOK_KW_DOUBLE:
	case TOK_KW_BOOL: case TOK_KW_VOID: case TOK_KW_STR:
		return true;
	default:
		return false;
	}
}

static bool is_qualifier_kw(TokenKind k)
{
	switch (k) {
	case TOK_KW_CONST: case TOK_KW_STATIC: case TOK_KW_REGISTER: case TOK_KW_VOLATILE:
	case TOK_KW_AUTO: case TOK_KW_EXTERN:
		return true;
	default:
		return false;
	}
}

/* Could this token begin a type (in a cast, parameter or declaration)? */
static bool starts_type(Parser *P, Token t)
{
	if (is_builtin_type_kw(t.kind) || t.kind == TOK_KW_CONST || t.kind == TOK_KW_VOLATILE || t.kind == TOK_KW_LIST ||
	    t.kind == TOK_KW_LOOKUP || t.kind == TOK_KW_STRUCT || t.kind == TOK_KW_UNION || t.kind == TOK_KW_ENUM)
		return true;
	return t.kind == TOK_IDENT && is_type_name(P, t.start, t.length);
}

static void unknown_type_error(Parser *P, Token t)
{
	static const struct { const char *name, *hint; } hints[] = {
		{ "string", "str" }, { "String", "str" }, { "boolean", "bool" }, { "Bool", "bool" },
		{ "_Bool", "bool" }, { "uint8_t", "unsigned char" }, { "int8_t", "signed char" },
		{ "uint16_t", "unsigned short" }, { "int16_t", "short" }, { "uint32_t", "unsigned int" },
		{ "int32_t", "int" }, { "uint64_t", "unsigned long" }, { "int64_t", "long" },
		{ "size_t", "unsigned long" },
	};
	for (size_t i = 0; i < sizeof hints / sizeof *hints; i++)
		if ((int)strlen(hints[i].name) == t.length && memcmp(hints[i].name, t.start, (size_t)t.length) == 0) {
			error_at(P, t, "unknown type '%.*s'; did you mean '%s'?", t.length, t.start, hints[i].hint);
			return;
		}
	if (t.length == 7 && memcmp(t.start, "address", 7) == 0)
		error_at(P, t, "unknown type 'address' (add 'use addresses' at the top of the file)");
	else if ((t.length == 7 && memcmp(t.start, "process", 7) == 0) || (t.length == 4 && memcmp(t.start, "file", 4) == 0))
		error_at(P, t, "unknown type '%.*s' (add 'use %s' at the top of the file)", t.length, t.start,
			 t.length == 4 ? "files" : "processes");
	else
		error_at(P, t, "unknown type '%.*s'", t.length, t.start);
}

typedef struct DeclSpec {
	bool is_const, is_static, is_register, is_volatile, is_auto, is_extern;
	TypeExpr *base;       /* NULL only for `auto` */
	Token first;
} DeclSpec;

/* Parses a built-in type keyword sequence (int, unsigned long, ...). */
static TypeExpr *parse_builtin_type(Parser *P)
{
	Token first = P->cur;
	int n_signed = 0, n_unsigned = 0, n_short = 0, n_long = 0, n_int = 0, n_char = 0;
	TokenKind other = TOK_EOF;
	Token other_tok = first;
	for (;;) {
		TokenKind k = P->cur.kind;
		if (k == TOK_KW_SIGNED) n_signed++;
		else if (k == TOK_KW_UNSIGNED) n_unsigned++;
		else if (k == TOK_KW_SHORT) n_short++;
		else if (k == TOK_KW_LONG) n_long++;
		else if (k == TOK_KW_INT) n_int++;
		else if (k == TOK_KW_CHAR) n_char++;
		else if (k == TOK_KW_FLOAT || k == TOK_KW_DOUBLE || k == TOK_KW_BOOL || k == TOK_KW_VOID || k == TOK_KW_STR) {
			if (other != TOK_EOF)
				break;
			other = k;
			other_tok = P->cur;
		} else if (k == TOK_KW_CONST || k == TOK_KW_VOLATILE) {
			break;   /* handled by the caller (may follow the type) */
		} else {
			break;
		}
		advance(P);
	}

	const char *name = NULL;
	bool bad = false;
	int sign = n_signed + n_unsigned;
	if (n_signed > 1 || n_unsigned > 1 || (n_signed && n_unsigned) || n_short > 1 || n_long > 2 ||
	    n_int > 1 || n_char > 1 || (n_short && n_long))
		bad = true;

	if (other != TOK_EOF) {
		if (sign || n_short || n_int || n_char || (n_long && other != TOK_KW_DOUBLE) || n_long > 1) {
			bad = true;
		} else if (other == TOK_KW_DOUBLE && n_long) {
			error_at(P, first, "'long double' is not supported; use 'double'");
			name = "double";
		} else {
			name = other == TOK_KW_FLOAT ? "float" : other == TOK_KW_DOUBLE ? "double" :
			       other == TOK_KW_BOOL ? "bool" : other == TOK_KW_VOID ? "void" : "str";
		}
	} else if (n_char) {
		if (n_short || n_long || n_int)
			bad = true;
		name = n_unsigned ? "unsigned char" : n_signed ? "signed char" : "char";
	} else if (n_short) {
		name = n_unsigned ? "unsigned short" : "short";
	} else if (n_long) {
		name = n_unsigned ? "unsigned long" : "long";
	} else {
		name = n_unsigned ? "unsigned int" : "int";
	}
	(void)other_tok;
	if (bad) {
		error_at(P, first, "invalid combination of type keywords");
		name = "int";
	}
	return ast_type_named(P->arena, name, true, loc_of(P, first));
}

/* A lookup's key type, after `lookup`: a type name, or one type word,
 * optionally after signed/unsigned (`unsigned long`). Unlike other types it
 * never takes a second word such as the `int` of `long int`, so that
 * `lookup long int Counts` reads as keys of long, values of int. */
static TypeExpr *parse_lookup_key(Parser *P)
{
	Token first = P->cur;
	if (check(P, TOK_IDENT) && is_type_name(P, P->cur.start, P->cur.length)) {
		advance(P);
		return ast_type_named(P->arena, tok_text(P, first), false, loc_of(P, first));
	}
	const char *sign = NULL;
	if (check(P, TOK_KW_SIGNED) || check(P, TOK_KW_UNSIGNED)) {
		sign = check(P, TOK_KW_UNSIGNED) ? "unsigned " : "";
		advance(P);
	}
	const char *word = NULL;
	switch (P->cur.kind) {
	case TOK_KW_CHAR:  word = "char"; break;
	case TOK_KW_SHORT: word = "short"; break;
	case TOK_KW_LONG:  word = "long"; break;
	case TOK_KW_INT:   word = "int"; break;
	case TOK_KW_STR: case TOK_KW_BOOL: case TOK_KW_FLOAT: case TOK_KW_DOUBLE:
		if (!sign) {
			word = P->cur.kind == TOK_KW_STR ? "str" : P->cur.kind == TOK_KW_BOOL ? "bool" :
			       P->cur.kind == TOK_KW_FLOAT ? "float" : "double";
		}
		break;
	default:
		break;
	}
	if (!word && !sign) {
		error_at(P, P->cur, "expected the type of the keys after 'lookup' (e.g. 'lookup str int Ages'), found %s",
			 describe(P->cur));
		return NULL;
	}
	if (word) {
		advance(P);
		if (strcmp(word, "long") == 0 && check(P, TOK_KW_LONG))
			advance(P);   /* long long is long */
	} else {
		word = "int";     /* signed or unsigned alone */
	}
	char name[32];
	if (sign && strcmp(word, "char") == 0)
		snprintf(name, sizeof name, "%s", *sign ? "unsigned char" : "signed char");
	else
		snprintf(name, sizeof name, "%s%s", sign ? sign : "", word);
	return ast_type_named(P->arena, arena_strndup(P->arena, name, strlen(name)), true, loc_of(P, first));
}

/* the types a method may be named after: J.str, J.long, J.double, J.bool */
static bool is_value_type_kw(TokenKind k)
{
	return k == TOK_KW_STR || k == TOK_KW_LONG || k == TOK_KW_DOUBLE || k == TOK_KW_BOOL || k == TOK_KW_INT ||
	       k == TOK_KW_CHAR || k == TOK_KW_FLOAT;
}

static bool kind_decl_here(Parser *P)
{
	if (P->cur.kind != TOK_IDENT || P->cur.length != 4 || memcmp(P->cur.start, "kind", 4) != 0)
		return false;
	Lexer copy = P->lex;
	copy.quiet = true;
	Token n1 = lexer_next(&copy);
	Token n2 = lexer_next(&copy);
	return n1.kind == TOK_IDENT && (n2.kind == TOK_NEWLINE || n2.kind == TOK_EOF || n2.kind == TOK_COMMA);
}

static bool parse_decl_spec(Parser *P, DeclSpec *ds, bool allow_storage)
{
	memset(ds, 0, sizeof *ds);
	ds->first = P->cur;
	for (;;) {
		Token t = P->cur;
		switch (t.kind) {
		case TOK_KW_CONST:    ds->is_const = true; break;
		case TOK_KW_VOLATILE: ds->is_volatile = true; break;
		case TOK_KW_STATIC:
		case TOK_KW_REGISTER:
		case TOK_KW_EXTERN:
			if (!allow_storage) {
				error_at(P, t, "'%s' can't be used here", token_kind_name(t.kind));
				return false;
			}
			if (t.kind == TOK_KW_STATIC) ds->is_static = true;
			else if (t.kind == TOK_KW_REGISTER) ds->is_register = true;
			else ds->is_extern = true;
			break;
		case TOK_KW_AUTO:     ds->is_auto = true; break;
		default:
			goto base;
		}
		advance(P);
	}
base:
	if (check(P, TOK_IDENT) && P->cur.length == 7 && memcmp(P->cur.start, "address", 7) == 0 &&
	    !is_type_name(P, "address", 7) && is_builtin_type_kw(peek(P).kind)) {
		error_at(P, P->cur, "unknown type 'address' (add 'use addresses' at the top of the file)");
		return false;
	}
	if (check(P, TOK_IDENT) && P->cur.length == 7 && memcmp(P->cur.start, "address", 7) == 0 &&
	    is_type_name(P, "address", 7)) {
		/* address T (use addresses); `address` alone points at nothing in particular */
		Token at = P->cur;
		advance(P);
		TypeExpr *te = ast_type_expr(P->arena, TYPEX_ADDRESS, loc_of(P, at));
		if (starts_type(P, P->cur)) {
			DeclSpec inner;
			if (!parse_decl_spec(P, &inner, false))
				return false;
			te->inner = inner.base;
		}
		ds->base = te;
	} else if (check(P, TOK_KW_LIST)) {
		/* list T: the element type follows, and may be a list itself */
		Token lt = P->cur;
		advance(P);
		DeclSpec inner;
		if (check(P, TOK_KW_CONST) || check(P, TOK_KW_STATIC)) {
			error_at(P, P->cur, "a list's element type comes straight after 'list', e.g. 'list int'");
			return false;
		}
		if (!starts_type(P, P->cur)) {
			error_at(P, P->cur, "expected the type of the list's elements after 'list' (e.g. 'list int Scores'), found %s",
				 describe(P->cur));
			return false;
		}
		if (!parse_decl_spec(P, &inner, false))
			return false;
		TypeExpr *te = ast_type_expr(P->arena, TYPEX_LIST, loc_of(P, lt));
		te->inner = inner.base;
		ds->base = te;
	} else if (check(P, TOK_KW_LOOKUP)) {
		/* lookup K V: the key type (one word, or signed/unsigned and one
		 * word), then the value type, which may be anything */
		Token lt = P->cur;
		advance(P);
		TypeExpr *key = parse_lookup_key(P);
		if (!key)
			return false;
		if (!starts_type(P, P->cur)) {
			error_at(P, P->cur, "expected the type of the values after the keys' type (e.g. 'lookup str int Ages'), "
				 "found %s", describe(P->cur));
			return false;
		}
		DeclSpec inner;
		if (check(P, TOK_KW_CONST) || check(P, TOK_KW_STATIC)) {
			error_at(P, P->cur, "a lookup's value type comes straight after its key type, e.g. 'lookup str int'");
			return false;
		}
		if (!parse_decl_spec(P, &inner, false))
			return false;
		TypeExpr *te = ast_type_expr(P->arena, TYPEX_LOOKUP, loc_of(P, lt));
		te->key = key;
		te->inner = inner.base;
		ds->base = te;
	} else if (is_builtin_type_kw(P->cur.kind)) {
		ds->base = parse_builtin_type(P);
	} else if (check(P, TOK_KW_STRUCT) || check(P, TOK_KW_UNION) || check(P, TOK_KW_ENUM)) {
		advance(P);   /* `struct point p` — the keyword is optional noise */
		if (!check(P, TOK_IDENT)) {
			error_at(P, P->cur, "expected a type name after '%s'", token_kind_name(P->prev.kind));
			return false;
		}
		ds->base = ast_type_named(P->arena, tok_text(P, P->cur), false, loc_of(P, P->cur));
		advance(P);
	} else if (check(P, TOK_IDENT) && is_type_name(P, P->cur.start, P->cur.length)) {
		ds->base = ast_type_named(P->arena, tok_text(P, P->cur), false, loc_of(P, P->cur));
		advance(P);
	} else if (ds->is_auto) {
		return true;
	} else {
		if (check(P, TOK_IDENT))
			unknown_type_error(P, P->cur);
		else
			error_at(P, P->cur, "expected a type, found %s", describe(P->cur));
		return false;
	}
	if (ds->is_auto) {
		error_at(P, ds->first, "Cabby has no 'auto': write just the type");
		return false;
	}
	/* `int const x` */
	while (check(P, TOK_KW_CONST) || check(P, TOK_KW_VOLATILE)) {
		if (check(P, TOK_KW_CONST)) ds->is_const = true;
		else ds->is_volatile = true;
		advance(P);
	}
	return true;
}

static Expr *parse_expr(Parser *P);
static Expr *parse_initializer_list(Parser *P, TypeExpr *type, Token open);

typedef enum { DCTX_LOCAL, DCTX_GLOBAL, DCTX_FIELD, DCTX_PARAM, DCTX_TYPEDEF } DeclCtx;

/* declarator = [ "&" ] name { "[" [ N ] "]" } [ "=" init ] */
static VarDecl *parse_declarator(Parser *P, const DeclSpec *ds, DeclCtx ctx)
{
	Token amp = P->cur;
	bool is_ref = match(P, TOK_AMP);
	if (!is_ref && check(P, TOK_AND_AND)) {
		error_at(P, P->cur, "a reference to a reference is not allowed");
		return NULL;
	}
	if (check(P, TOK_KW_AKA)) {
		error_at(P, P->cur, "'aka' gives a type a second name, and belongs at the top level of a file "
			 "(e.g. 'double aka celsius')");
		return NULL;
	}
	if (!check(P, TOK_IDENT)) {
		if (check(P, TOK_STAR))
			error_at(P, P->cur, "Cabby has no pointers; to borrow, write '&' before the name (e.g. 'window &w')");
		else if (P->cur.kind >= TOK_KW_AUTO && P->cur.kind <= TOK_KW_SELF)
			error_at(P, P->cur, "'%s' is a keyword and can't be used as a name", token_kind_name(P->cur.kind));
		else
			error_at(P, P->cur, "expected a name, found %s", describe(P->cur));
		return NULL;
	}
	/* A library's parameter may share a name with a program's type (its
	 * `extern ... long node` and a program's `struct node`): the library
	 * was written without knowing the program's names. */
	size_t pl = strlen(P->path);
	bool lib_param = ctx == DCTX_PARAM && pl > 4 && strcmp(P->path + pl - 4, ".hby") == 0;
	if (ctx != DCTX_TYPEDEF && !lib_param && is_type_name(P, P->cur.start, P->cur.length)) {
		error_at(P, P->cur, "'%.*s' is a type name", P->cur.length, P->cur.start);
		return NULL;
	}
	VarDecl *v = ast_var(P->arena, tok_text(P, P->cur), loc_of(P, P->cur));
	advance(P);
	v->is_static = ds->is_static;
	v->is_register = ds->is_register;
	v->is_volatile = ds->is_volatile;
	v->is_auto = ds->is_auto;

	/* array dimensions, outermost first */
	Expr *dims[16];
	bool unsized[16];
	int nd = 0;
	Token first_dim = P->cur;
	while (check(P, TOK_LBRACKET)) {
		if (nd == 16) {
			error_at(P, P->cur, "too many array dimensions");
			return NULL;
		}
		advance(P);
		unsized[nd] = check(P, TOK_RBRACKET);
		dims[nd] = unsized[nd] ? NULL : parse_expr(P);
		nd++;
		if (!expect(P, TOK_RBRACKET, "']'"))
			return NULL;
	}

	TypeExpr *t = ds->base;
	if (nd && !t) {
		error_at(P, first_dim, "'auto' can't be used with array dimensions");
		return NULL;
	}
	for (int i = nd - 1; i >= 0; i--) {
		if (unsized[i] && i > 0) {
			error_at(P, first_dim, "only the first array dimension may be left empty");
			return NULL;
		}
		TypeExpr *a = ast_type_expr(P->arena, TYPEX_ARRAY, loc_of(P, first_dim));
		a->inner = t;
		a->len = dims[i];
		t = a;
	}

	if (ctx == DCTX_PARAM && nd) {
		/* array parameters are always borrowed (as in C) */
		if (is_ref) {
			error_at(P, amp, "array parameters are already borrowed; drop the '&'");
			return NULL;
		}
		TypeExpr *r;
		if (unsized[0]) {
			r = ast_type_expr(P->arena, TYPEX_SLICE, t->loc);
			r->inner = t->inner;
		} else {
			r = ast_type_expr(P->arena, TYPEX_REF, t->loc);
			r->inner = t;
		}
		r->is_const = ds->is_const;
		t = r;
	} else if (nd && unsized[0] && ctx != DCTX_LOCAL && ctx != DCTX_GLOBAL) {
		error_at(P, first_dim, "an array without a size is only allowed for parameters, "
			 "or when an initializer gives the length");
		return NULL;
	} else if (is_ref) {
		if (!t) {
			error_at(P, amp, "'auto' can't be a reference; write the type");
			return NULL;
		}
		TypeExpr *r = ast_type_expr(P->arena, TYPEX_REF, loc_of(P, amp));
		r->inner = t;
		r->is_const = ds->is_const;
		t = r;
	} else {
		v->is_const = ds->is_const;
	}
	v->type = t;

	if (check(P, TOK_EQ)) {
		Token eq = P->cur;
		if (ctx == DCTX_FIELD) {
			error_at(P, eq, "fields can't have initializers; every field starts at zero");
			return NULL;
		}
		if (ctx == DCTX_PARAM) {
			error_at(P, eq, "parameters can't have default values");
			return NULL;
		}
		if (ctx == DCTX_TYPEDEF) {
			error_at(P, eq, "'aka' gives a type a second name; it can't have a value");
			return NULL;
		}
		advance(P);
		if (check(P, TOK_LBRACE)) {
			Token open = P->cur;
			advance(P);
			v->init = parse_initializer_list(P, NULL, open);
		} else {
			v->init = parse_expr(P);
		}
	} else if (nd && unsized[0] && ctx != DCTX_PARAM) {
		error_at(P, P->cur, "an array without a size needs an initializer to give its length");
		return NULL;
	}
	if (ds->is_auto && !v->init && ctx != DCTX_PARAM) {
		error_at(P, P->cur, "'auto' needs an initial value to infer the type from");
		return NULL;
	}
	if (ds->is_const && !is_ref && !v->init && (ctx == DCTX_LOCAL || ctx == DCTX_GLOBAL) && !ds->is_extern) {
		error_at(P, P->cur, "a const variable needs an initial value");
		return NULL;
	}
	return v;
}

/* ---------------------------------------------------- callable names
 * `abs -7` passes -7 to `abs`, but `n -7` subtracts. To tell them apart
 * while parsing, every file is first scanned for the functions and
 * methods that take arguments (subtracting from one of those can never
 * make sense), and for field names (so `P.Size - 1` stays a subtraction). */

static bool name_listed(const char *const *items, int n, const char *s, int len)
{
	for (int i = 0; i < n; i++)
		if ((int)strlen(items[i]) == len && memcmp(items[i], s, (size_t)len) == 0)
			return true;
	return false;
}

#define LIST_ADD(LD, LIST, S, N) do { \
	if (!name_listed((const char *const *)(LD)->LIST.items, (LD)->LIST.len, (S), (N))) \
		vec_push((LD)->A, (LD)->LIST, arena_strndup((LD)->A, (S), (size_t)(N))); \
} while (0)

/* One line at the level of declarations (top level, or directly inside an
 * object, struct or union), starting at `t`. */
static void prescan_decl_line(Loader *ld, Token t, Lexer *L, bool in_type)
{
	while (is_qualifier_kw(t.kind))
		t = lexer_next(L);
	while (t.kind == TOK_KW_LIST || t.kind == TOK_KW_LOOKUP) {
		if (t.kind == TOK_KW_LIST) {
			t = lexer_next(L);
			continue;
		}
		/* lookup K V: skip the key type, as parse_lookup_key reads it */
		t = lexer_next(L);
		if (t.kind == TOK_KW_SIGNED || t.kind == TOK_KW_UNSIGNED) {
			t = lexer_next(L);
			if (t.kind == TOK_KW_CHAR || t.kind == TOK_KW_SHORT || t.kind == TOK_KW_INT)
				t = lexer_next(L);
			else if (t.kind == TOK_KW_LONG && (t = lexer_next(L)).kind == TOK_KW_LONG)
				t = lexer_next(L);
		} else if (t.kind == TOK_KW_LONG) {
			if ((t = lexer_next(L)).kind == TOK_KW_LONG)
				t = lexer_next(L);
		} else if (t.kind == TOK_IDENT || is_builtin_type_kw(t.kind)) {
			t = lexer_next(L);
		} else {
			return;
		}
	}
	if (is_builtin_type_kw(t.kind)) {
		while (is_builtin_type_kw(t.kind))
			t = lexer_next(L);
	} else if (t.kind == TOK_KW_STRUCT || t.kind == TOK_KW_UNION || t.kind == TOK_KW_ENUM) {
		if (lexer_next(L).kind != TOK_IDENT)
			return;
		t = lexer_next(L);
	} else if (t.kind == TOK_IDENT) {
		t = lexer_next(L);
		while (t.kind == TOK_DOT) {
			if (lexer_next(L).kind != TOK_IDENT)
				return;
			t = lexer_next(L);
		}
	} else {
		return;
	}
	while (t.kind == TOK_AMP || t.kind == TOK_STAR)
		t = lexer_next(L);
	if (t.kind != TOK_IDENT)
		return;
	Token name = t;
	t = lexer_next(L);
	switch (t.kind) {
	case TOK_NEWLINE: case TOK_EOF: case TOK_EQ: case TOK_LBRACKET: case TOK_COMMA:
		if (in_type)
			LIST_ADD(ld, fields, name.start, name.length);
		return;
	case TOK_LBRACE:
		return;
	case TOK_KW_CONST: case TOK_KW_VOID: {
		Token n = lexer_next(L);
		if (n.kind == TOK_LBRACE || n.kind == TOK_NEWLINE || n.kind == TOK_EOF || n.kind == TOK_KW_CONST)
			return;
		break;
	}
	default:
		if (!is_builtin_type_kw(t.kind) && !is_qualifier_kw(t.kind) && t.kind != TOK_IDENT &&
		    t.kind != TOK_KW_STRUCT && t.kind != TOK_KW_UNION && t.kind != TOK_KW_ENUM &&
		    t.kind != TOK_ELLIPSIS && t.kind != TOK_KW_LIST && t.kind != TOK_KW_LOOKUP)
			return;
	}
	LIST_ADD(ld, callables, name.start, name.length);
}

static void prescan_callables(Loader *ld, const char *path, const char *src)
{
	enum { BODY, TYPE_BODY };
	char kinds[256];
	int depth = 0;
	bool line_opens_type = false;
	Lexer L;
	lexer_init(&L, ld->A, path, src);
	L.quiet = true;
	for (;;) {
		Token t = lexer_next(&L);
		if (t.kind == TOK_EOF)
			break;
		if (t.bol) {
			line_opens_type = t.kind == TOK_KW_OBJECT || t.kind == TOK_KW_STRUCT || t.kind == TOK_KW_UNION;
			bool in_type = depth > 0 && depth <= 256 && kinds[depth - 1] == TYPE_BODY;
			if (depth == 0 || in_type) {
				Lexer copy = L;
				prescan_decl_line(ld, t, &copy, in_type);
			}
		}
		if (t.kind == TOK_LBRACE) {
			if (depth < 256)
				kinds[depth] = line_opens_type ? TYPE_BODY : BODY;
			depth++;
			line_opens_type = false;
		} else if (t.kind == TOK_RBRACE && depth > 0) {
			depth--;
		}
	}
}

/* Is `name -x` a call? Only for a function or method that takes arguments
 * (and, after a dot, not for something that is also a field). */
static bool minus_starts_call(Parser *P, const Expr *e)
{
	const char *name = e->kind == EXPR_IDENT ? e->as.ident.name : e->as.field.name;
	int len = (int)strlen(name);
	Loader *ld = P->ld;
	static const char *const own[] = { "abs", "exit", "random_seed" };   /* (Cabby's own, which take a number) */
	if (e->kind == EXPR_IDENT && name_listed(own, 3, name, len))
		return true;
	if (!name_listed((const char *const *)ld->callables.items, ld->callables.len, name, len))
		return false;
	if (e->kind == EXPR_FIELD) {
		static const char *const members[] = { "length", "text", "copy" };
		if (name_listed(members, 3, name, len) ||
		    name_listed((const char *const *)ld->fields.items, ld->fields.len, name, len))
			return false;
	}
	return true;
}

/* ================================================================ expressions */

static Expr *parse_unary(Parser *P);

static Expr *parse_initializer_list(Parser *P, TypeExpr *type, Token open)
{
	Expr *e = ast_expr(P->arena, EXPR_INIT, loc_of(P, open));
	e->as.init.type = type;
	bool saved = P->no_init;
	P->no_init = false;
	for (;;) {
		while (match(P, TOK_NEWLINE))
			;
		if (check(P, TOK_RBRACE) || check(P, TOK_EOF))
			break;
		InitEntry ent = { 0 };
		ent.loc = loc_of(P, P->cur);
		if (check(P, TOK_DOT)) {
			advance(P);
			if (!check(P, TOK_IDENT)) {
				error_at(P, P->cur, "expected a field name after '.', found %s", describe(P->cur));
				break;
			}
			ent.field = tok_text(P, P->cur);
			advance(P);
			if (!expect(P, TOK_EQ, "'=' after the field name"))
				break;
		}
		if (check(P, TOK_LBRACE)) {
			Token o = P->cur;
			advance(P);
			ent.value = parse_initializer_list(P, NULL, o);
		} else {
			ent.value = parse_expr(P);
		}
		if (!ent.field && !P->panic && match(P, TOK_COLON)) {
			/* a lookup's entry: key: value */
			ent.key = ent.value;
			if (check(P, TOK_LBRACE)) {
				Token o = P->cur;
				advance(P);
				ent.value = parse_initializer_list(P, NULL, o);
			} else {
				ent.value = parse_expr(P);
			}
		}
		vec_push(P->arena, e->as.init.entries, ent);
		if (P->panic)
			break;
		while (match(P, TOK_NEWLINE))
			;
		if (!match(P, TOK_COMMA))
			break;
	}
	while (match(P, TOK_NEWLINE))
		;
	P->no_init = saved;
	expect(P, TOK_RBRACE, "',' or '}' in the initializer");
	return e;
}

static Expr *parse_primary(Parser *P)
{
	Token t = P->cur;
	Expr *e;

	switch (t.kind) {
	case TOK_INT:
		advance(P);
		e = ast_expr(P->arena, EXPR_INT, loc_of(P, t));
		e->as.int_lit.value = t.v.u;
		e->as.int_lit.is_unsigned = t.lit_unsigned;
		e->as.int_lit.longs = t.lit_long;
		e->as.int_lit.based = t.lit_based;
		return e;
	case TOK_FLOAT:
		advance(P);
		e = ast_expr(P->arena, EXPR_FLOAT, loc_of(P, t));
		e->as.float_lit.value = t.v.f;
		e->as.float_lit.is_float32 = t.lit_float32;
		return e;
	case TOK_STRING: {
		advance(P);
		e = ast_expr(P->arena, EXPR_STRING, loc_of(P, t));
		const char *data = t.v.str.data;
		size_t len = t.v.str.len;
		while (check(P, TOK_STRING)) {   /* "a" "b" concatenation, as in C */
			char *n = arena_alloc(P->arena, len + P->cur.v.str.len + 1);
			memcpy(n, data, len);
			memcpy(n + len, P->cur.v.str.data, P->cur.v.str.len);
			len += P->cur.v.str.len;
			n[len] = '\0';
			data = n;
			advance(P);
		}
		e->as.str.data = data;
		e->as.str.len = len;
		return e;
	}
	case TOK_CHAR:
		advance(P);
		e = ast_expr(P->arena, EXPR_CHAR, loc_of(P, t));
		e->as.char_val = t.v.ch;
		return e;
	case TOK_KW_TRUE:
	case TOK_KW_FALSE:
		advance(P);
		e = ast_expr(P->arena, EXPR_BOOL, loc_of(P, t));
		e->as.bool_val = t.kind == TOK_KW_TRUE;
		return e;
	case TOK_KW_SELF:
		advance(P);
		return ast_expr(P->arena, EXPR_SELF, loc_of(P, t));
	case TOK_IDENT:
		advance(P);
		if (is_type_name(P, t.start, t.length)) {
			if (check(P, TOK_LBRACE) && !P->no_init) {
				Token open = P->cur;
				advance(P);
				return parse_initializer_list(P, ast_type_named(P->arena, tok_text(P, t), false, loc_of(P, t)), open);
			}
			if (!check(P, TOK_DOT)) {
				error_at(P, t, "'%.*s' is a type, not a value", t.length, t.start);
				return ast_expr(P->arena, EXPR_INT, loc_of(P, t));
			}
		}
		e = ast_expr(P->arena, EXPR_IDENT, loc_of(P, t));
		e->as.ident.name = tok_text(P, t);
		return e;
	case TOK_LPAREN: {
		advance(P);
		bool was = P->no_init;   /* inside parentheses, { } is a list again: for X in ({1, 2}) */
		P->no_init = false;
		e = parse_expr(P);
		P->no_init = was;
		e->parenthesized = true;
		expect(P, TOK_RPAREN, "')'");
		return e;
	}
	case TOK_LBRACE:
		if (!P->no_init) {
			advance(P);
			return parse_initializer_list(P, NULL, t);
		}
		error_at(P, t, "an initializer can't appear directly in a condition; put it in parentheses");
		return ast_expr(P->arena, EXPR_INT, loc_of(P, t));
	case TOK_ERROR:
		advance(P);
		P->panic = true;
		return ast_expr(P->arena, EXPR_INT, loc_of(P, t));
	default:
		if (is_builtin_type_kw(t.kind))
			error_at(P, t, "'%s' is a type, not a value", token_kind_name(t.kind));
		else if (t.kind == TOK_AMP)
			error_at(P, t, "there is no address-of '&' in Cabby; borrows happen automatically when "
				 "a function takes a reference");
		else if (t.kind == TOK_STAR)
			error_at(P, t, "there is no '*' dereference in Cabby; references are used like the value itself");
		else
			error_at(P, t, "expected an expression, found %s", describe(t));
		return ast_expr(P->arena, EXPR_INT, loc_of(P, t));
	}
}

/* `random 1 to 6`: a range, as random's two arguments */
static bool is_random(const Expr *call)
{
	const Expr *f = call->as.call.callee;
	return f->kind == EXPR_IDENT && strcmp(f->as.ident.name, "random") == 0;
}

static void push_arg(Parser *P, Expr *call)
{
	vec_push(P->arena, call->as.call.args, parse_expr(P));
	if (!P->panic && call->as.call.args.len == 1 && is_random(call) && match(P, TOK_KW_TO))
		vec_push(P->arena, call->as.call.args, parse_expr(P));
}

static void parse_call_args(Parser *P, Expr *call)
{
	while (!check(P, TOK_RPAREN) && !check(P, TOK_EOF)) {
		push_arg(P, call);
		if (P->panic || !match(P, TOK_COMMA))
			break;
	}
	expect(P, TOK_RPAREN, "',' or ')'");
}

static bool assign_op(TokenKind k, bool *compound, BinaryOp *op);

/* At the start of a statement, is the `(` at P->cur the call's own
 * parentheses? Only if they hold the whole rest of the call: after the
 * matching `)` the line ends, or the chain goes on (`.x`, `[i]`, `(...)`),
 * or the statement is an assignment or ++/--. Otherwise, as in
 * `print (a + b) * 2`, the bracket starts the first bare argument. */
static bool call_parentheses_here(Parser *P)
{
	Lexer copy = P->lex;
	copy.quiet = true;
	int depth = 1;
	Token n;
	do {
		n = lexer_next(&copy);
		if (n.kind == TOK_EOF)
			return true;
		if (n.kind == TOK_LPAREN)
			depth++;
		else if (n.kind == TOK_RPAREN)
			depth--;
	} while (depth > 0);
	/* The parentheses hold the call's arguments only if they hold all of
	 * them: nothing but the end of the statement may follow. Otherwise they
	 * start the first argument, as in `print (a + b) * 2` or
	 * `print (n + 1).text + " left"`. (An assignment after them is kept
	 * as the call's, for a clear error message.) */
	n = lexer_next(&copy);
	bool compound;
	BinaryOp op;
	if (n.kind == TOK_NEWLINE || n.kind == TOK_EOF || n.kind == TOK_RBRACE || n.kind == TOK_SEMI ||
	    n.kind == TOK_PLUS_PLUS || n.kind == TOK_MINUS_MINUS || assign_op(n.kind, &compound, &op))
		return true;
	/* `json.read(Text).fill P`, `P.find("td").clear`: with the `(` written
	 * against the name, a chain after it that ends the statement, or calls
	 * a method with bare arguments, means they're the call's own parentheses.
	 * (`print (n + 1).text` with a space, and `print(n).text, 5` or
	 * `print(n).text + "!"`, carrying on as an argument, are bare.) */
	bool against = P->prev.start + P->prev.length == P->cur.start;
	if (against && n.kind == TOK_DOT) {
		Lexer chain = copy;
		Token m = n;
		while (m.kind == TOK_DOT) {
			Token name = lexer_next(&chain);
			if (name.kind != TOK_IDENT && !is_value_type_kw(name.kind))
				break;
			m = lexer_next(&chain);
		}
		switch (m.kind) {
		case TOK_NEWLINE: case TOK_EOF: case TOK_RBRACE: case TOK_SEMI:
		case TOK_IDENT: case TOK_INT: case TOK_FLOAT: case TOK_STRING: case TOK_CHAR:
		case TOK_KW_TRUE: case TOK_KW_FALSE:
			return true;
		default:
			break;
		}
	}
	/* `w.get(0).x = 1` is still the call's parentheses: step over `.name` and
	 * `[i]`, and see if an assignment follows. */
	while (n.kind == TOK_DOT || n.kind == TOK_LBRACKET) {
		if (n.kind == TOK_DOT) {
			lexer_next(&copy);
		} else {
			int d = 1;
			while (d > 0) {
				n = lexer_next(&copy);
				if (n.kind == TOK_EOF || n.kind == TOK_NEWLINE)
					return false;
				d += n.kind == TOK_LBRACKET ? 1 : n.kind == TOK_RBRACKET ? -1 : 0;
			}
		}
		n = lexer_next(&copy);
	}
	return assign_op(n.kind, &compound, &op) || n.kind == TOK_PLUS_PLUS || n.kind == TOK_MINUS_MINUS;
}

/* Postfix chain. *last_was_call reports whether it ended with (...).
 * With `bare_ok` (the start of a statement), a `(` that is not the call's
 * own parentheses ends the chain: it starts a bare argument, as in
 * `print (int)x` or `print (a + b) * 2`. */
static Expr *parse_postfix_chain(Parser *P, Expr *e, bool *last_was_call, bool bare_ok)
{
	bool was_call = false;
	for (;;) {
		Token t = P->cur;
		if (bare_ok && check(P, TOK_LPAREN) &&
		    (cast_here(P) || ((e->kind == EXPR_IDENT || e->kind == EXPR_FIELD) && !call_parentheses_here(P))))
			break;
		if (match(P, TOK_LPAREN)) {
			Expr *call = ast_expr(P->arena, EXPR_CALL, e->loc);
			call->as.call.callee = e;
			parse_call_args(P, call);
			e = call;
			was_call = true;
		} else if (match(P, TOK_LBRACKET)) {
			Expr *idx = ast_expr(P->arena, EXPR_INDEX, loc_of(P, t));
			idx->as.index.base = e;
			/* s[i], or a slice: s[a to b], s[a to], s[to b] */
			if (match(P, TOK_KW_TO)) {
				idx->as.index.is_slice = true;
				if (check(P, TOK_RBRACKET))
					error_at(P, P->cur, "a slice needs a start or an end: 's[2 to]', 's[to 4]'");
				else
					idx->as.index.to = parse_expr(P);
			} else {
				idx->as.index.index = parse_expr(P);
				if (match(P, TOK_KW_TO)) {
					idx->as.index.is_slice = true;
					if (!check(P, TOK_RBRACKET))
						idx->as.index.to = parse_expr(P);
				}
			}
			expect(P, TOK_RBRACKET, "']'");
			e = idx;
			was_call = false;
		} else if (match(P, TOK_DOT)) {
			Expr *f = ast_expr(P->arena, EXPR_FIELD, loc_of(P, t));
			f->as.field.object = e;
			if (check(P, TOK_IDENT)) {
				f->as.field.name = tok_text(P, P->cur);
				advance(P);
			} else if (is_value_type_kw(P->cur.kind)) {
				/* J.str, J.long: a method named after the type it gives, declared
				 * with a '_' after its name (`long long_ const { ... }`) since
				 * the type's name is a keyword */
				const char *kw = tok_text(P, P->cur);
				char *n = arena_alloc(P->arena, strlen(kw) + 2);
				sprintf(n, "%s_", kw);
				f->as.field.name = n;
				advance(P);
			} else {
				f->as.field.name = "<error>";
				error_at(P, P->cur, "expected a field or method name after '.', found %s", describe(P->cur));
			}
			e = f;
			was_call = false;
		} else {
			break;
		}
		if (P->panic)
			break;
	}
	if (last_was_call)
		*last_was_call = was_call;
	return e;
}

static Expr *parse_postfix_tail(Parser *P, Expr *e, bool *last_was_call)
{
	return parse_postfix_chain(P, e, last_was_call, false);
}

/* Tokens that start the first argument of a bare call inside an
 * expression. Not `(` (that is an ordinary call), nor `-` or `+` (those
 * are subtraction and addition: `n - 1`), nor `{`. */
static bool starts_expr_arg(TokenKind k)
{
	switch (k) {
	case TOK_IDENT: case TOK_INT: case TOK_FLOAT: case TOK_STRING: case TOK_CHAR:
	case TOK_KW_TRUE: case TOK_KW_FALSE: case TOK_KW_SELF: case TOK_KW_SIZEOF:
	case TOK_BANG: case TOK_TILDE: case TOK_KW_NOT:
		return true;
	default:
		return false;
	}
}

static Expr *parse_not_operand(Parser *P);

static Expr *parse_unary(Parser *P)
{
	Token t = P->cur;
	UnaryOp op;
	switch (t.kind) {
	case TOK_MINUS: op = UN_NEG; break;
	case TOK_BANG:  op = UN_NOT; break;
	case TOK_TILDE: op = UN_BITNOT; break;
	case TOK_PLUS:
		advance(P);
		return parse_unary(P);
	case TOK_KW_NOT: {
		/* (as it was read: the whole comparison after it, `not Count is 0`
		 * being !(Count == 0), with `and` and `or` ending it) */
		old_word(P, t, "not", "!", "'!Done', or '!(A == B)' around a comparison");
		advance(P);
		Expr *e = ast_expr(P->arena, EXPR_UNARY, loc_of(P, t));
		e->as.unary.op = UN_NOT;
		e->as.unary.operand = parse_not_operand(P);
		return e;
	}
	case TOK_PLUS_PLUS:
	case TOK_MINUS_MINUS:
		error_at(P, t, "'%s' can only be used as a statement on its own (e.g. 'i++')", token_kind_name(t.kind));
		return ast_expr(P->arena, EXPR_INT, loc_of(P, t));
	case TOK_KW_SIZEOF: {
		advance(P);
		Expr *e = ast_expr(P->arena, EXPR_SIZEOF, loc_of(P, t));
		if (check(P, TOK_LPAREN) && cast_here(P)) {
			advance(P);
			DeclSpec ds;
			if (parse_decl_spec(P, &ds, false)) {
				TypeExpr *ty = ds.base;
				while (check(P, TOK_LBRACKET)) {   /* sizeof(int[4]) */
					Token lb = P->cur;
					advance(P);
					TypeExpr *a = ast_type_expr(P->arena, TYPEX_ARRAY, loc_of(P, lb));
					a->len = parse_expr(P);
					expect(P, TOK_RBRACKET, "']'");
					/* dims nest outermost-first */
					if (ty->kind == TYPEX_ARRAY) {
						TypeExpr **slot = &ty;
						while ((*slot)->kind == TYPEX_ARRAY)
							slot = &(*slot)->inner;
						a->inner = *slot;
						*slot = a;
					} else {
						a->inner = ty;
						ty = a;
					}
				}
				e->as.size_of.type = ty;
			}
			expect(P, TOK_RPAREN, "')'");
		} else {
			e->as.size_of.expr = parse_unary(P);
		}
		return e;
	}
	case TOK_LPAREN: {
		if (cast_here(P)) {
			/* C cast: (T)expr */
			advance(P);
			DeclSpec ds;
			Expr *e = ast_expr(P->arena, EXPR_CAST, loc_of(P, t));
			if (parse_decl_spec(P, &ds, false)) {
				if (check(P, TOK_AMP))
					error_at(P, P->cur, "can't cast to a reference");
				e->as.cast.target = ds.base;
			}
			expect(P, TOK_RPAREN, "')' to close the cast");
			e->as.cast.value = parse_unary(P);
			return e;
		}
		break;
	}
	default:
		break;
	}
	if (t.kind == TOK_MINUS || t.kind == TOK_BANG || t.kind == TOK_TILDE) {
		advance(P);
		Expr *e = ast_expr(P->arena, EXPR_UNARY, loc_of(P, t));
		e->as.unary.op = op;
		e->as.unary.operand = parse_unary(P);
		return e;
	}
	bool was_call = false;
	Expr *e = parse_postfix_tail(P, parse_primary(P), &was_call);
	bool minus = !was_call && !e->parenthesized && (e->kind == EXPR_IDENT || e->kind == EXPR_FIELD) &&
		     check(P, TOK_MINUS) && minus_starts_call(P, e);
	/* `Names.add {"a", "b"}`: outside an if/while/for/switch header, a '{'
	 * after a name starts its argument (there, it opens the block) */
	bool brace = !P->no_init && check(P, TOK_LBRACE);
	if (!was_call && (e->kind == EXPR_IDENT || e->kind == EXPR_FIELD) && (starts_expr_arg(P->cur.kind) || minus || brace)) {
		/* a bare call inside an expression: `int n = strlen s`, `(add a, b) * 2`.
		 * Its arguments run to the end of the line or the closing bracket. */
		Expr *call = ast_expr(P->arena, EXPR_CALL, e->loc);
		call->as.call.callee = e;
		call->as.call.bare = true;
		call->as.call.minus = minus;
		do {
			push_arg(P, call);
		} while (!P->panic && match(P, TOK_COMMA));
		return call;
	}
	if (check(P, TOK_PLUS_PLUS) || check(P, TOK_MINUS_MINUS))
		error_at(P, P->cur, "'%s' can only be used as a statement on its own (e.g. 'i++')",
			 token_kind_name(P->cur.kind));
	return e;
}

/* C precedence, loosest first. */
enum {
	PREC_NONE = 0,
	PREC_LOG_OR,
	PREC_LOG_AND,
	PREC_BIT_OR,
	PREC_BIT_XOR,
	PREC_BIT_AND,
	PREC_EQUALITY,
	PREC_RELATIONAL,
	PREC_SHIFT,
	PREC_ADD,
	PREC_MUL,
};

static int binop_info(TokenKind k, BinaryOp *op)
{
	switch (k) {
	case TOK_OR_OR:   *op = BIN_LOG_OR;  return PREC_LOG_OR;
	case TOK_KW_OR:   *op = BIN_LOG_OR;  return PREC_LOG_OR;
	case TOK_AND_AND: *op = BIN_LOG_AND; return PREC_LOG_AND;
	case TOK_KW_AND:  *op = BIN_LOG_AND; return PREC_LOG_AND;
	case TOK_KW_IS:   *op = BIN_EQ;      return PREC_EQUALITY;   /* `is not`: see parse_binary */
	case TOK_PIPE:    *op = BIN_BIT_OR;  return PREC_BIT_OR;
	case TOK_CARET:   *op = BIN_BIT_XOR; return PREC_BIT_XOR;
	case TOK_AMP:     *op = BIN_BIT_AND; return PREC_BIT_AND;
	case TOK_EQ_EQ:   *op = BIN_EQ;      return PREC_EQUALITY;
	case TOK_BANG_EQ: *op = BIN_NE;      return PREC_EQUALITY;
	case TOK_LT:      *op = BIN_LT;      return PREC_RELATIONAL;
	case TOK_LE:      *op = BIN_LE;      return PREC_RELATIONAL;
	case TOK_GT:      *op = BIN_GT;      return PREC_RELATIONAL;
	case TOK_GE:      *op = BIN_GE;      return PREC_RELATIONAL;
	case TOK_SHL:     *op = BIN_SHL;     return PREC_SHIFT;
	case TOK_SHR:     *op = BIN_SHR;     return PREC_SHIFT;
	case TOK_PLUS:    *op = BIN_ADD;     return PREC_ADD;
	case TOK_MINUS:   *op = BIN_SUB;     return PREC_ADD;
	case TOK_STAR:    *op = BIN_MUL;     return PREC_MUL;
	case TOK_SLASH:   *op = BIN_DIV;     return PREC_MUL;
	case TOK_PERCENT: *op = BIN_MOD;     return PREC_MUL;
	default:          return PREC_NONE;
	}
}

static bool is_bare_comparison(const Expr *e)
{
	return e->kind == EXPR_BINARY && !e->parenthesized && ast_binop_is_comparison(e->as.binary.op);
}

/* Precedence climbing; `lhs` may be pre-parsed (statement-level parsing). */
static Expr *parse_binary(Parser *P, int min_prec, Expr *lhs)
{
	if (!lhs)
		lhs = parse_unary(P);
	for (;;) {
		if (P->panic)
			return lhs;
		BinaryOp op;
		int prec = binop_info(P->cur.kind, &op);
		if (prec == PREC_NONE || prec < min_prec)
			return lhs;
		Token t = P->cur;
		advance(P);
		if (t.kind == TOK_KW_IS && match(P, TOK_KW_NOT)) {
			op = BIN_NE;   /* `is not` */
			old_word(P, t, "is not", "!=", "'Name != \"Ada\"'");
		} else if (t.kind == TOK_KW_IS) {
			old_word(P, t, "is", "==", "'Name == \"Ada\"'");
		} else if (t.kind == TOK_KW_AND) {
			old_word(P, t, "and", "&&", "'Age >= 18 && Member'");
		} else if (t.kind == TOK_KW_OR) {
			old_word(P, t, "or", "||", "'Day == 6 || Day == 7'");
		}
		while (match(P, TOK_NEWLINE))   /* a line may end with a binary operator */
			;
		Expr *rhs = parse_binary(P, prec + 1, NULL);
		Expr *e = ast_expr(P->arena, EXPR_BINARY, loc_of(P, t));
		e->as.binary.op = op;
		e->as.binary.lhs = lhs;
		e->as.binary.rhs = rhs;
		if (ast_binop_is_bitwise(op) && op != BIN_SHL && op != BIN_SHR &&
		    (is_bare_comparison(lhs) || is_bare_comparison(rhs)))
			warn_at(P, e->loc, "'%s' binds more loosely than comparisons (as in C); "
				"add parentheses, e.g. '(x %s 1) == 0'", ast_binop_str(op), ast_binop_str(op));
		lhs = e;
	}
}

static Expr *parse_expr(Parser *P) { return parse_binary(P, PREC_LOG_OR, NULL); }

/* what `not` applies to: everything tighter than `and` and `or` */
static Expr *parse_not_operand(Parser *P) { return parse_binary(P, PREC_LOG_AND + 1, NULL); }

/* `until C` is `while !C`: the condition, turned round */
static Expr *negate(Parser *P, Expr *c)
{
	if (!c)
		return c;
	Expr *e = ast_expr(P->arena, EXPR_UNARY, c->loc);
	e->as.unary.op = UN_NOT;
	e->as.unary.operand = c;
	return e;
}

static Expr *parse_cond(Parser *P, const char *what)
{
	bool saved = P->no_init;
	P->no_init = true;
	Expr *e = parse_expr(P);
	P->no_init = saved;
	if (!P->panic && check(P, TOK_EQ))
		error_at(P, P->cur, "'=' is assignment; use '==' to compare in %s", what);
	return e;
}

/* ================================================================ statements */

static Stmt *parse_stmt(Parser *P, int indent);
static Block *parse_block(Parser *P, int indent, const char *what);

static bool assign_op(TokenKind k, bool *compound, BinaryOp *op)
{
	*compound = true;
	switch (k) {
	case TOK_EQ:         *compound = false; *op = BIN_ADD; return true;
	case TOK_PLUS_EQ:    *op = BIN_ADD;     return true;
	case TOK_MINUS_EQ:   *op = BIN_SUB;     return true;
	case TOK_STAR_EQ:    *op = BIN_MUL;     return true;
	case TOK_SLASH_EQ:   *op = BIN_DIV;     return true;
	case TOK_PERCENT_EQ: *op = BIN_MOD;     return true;
	case TOK_AMP_EQ:     *op = BIN_BIT_AND; return true;
	case TOK_PIPE_EQ:    *op = BIN_BIT_OR;  return true;
	case TOK_CARET_EQ:   *op = BIN_BIT_XOR; return true;
	case TOK_SHL_EQ:     *op = BIN_SHL;     return true;
	case TOK_SHR_EQ:     *op = BIN_SHR;     return true;
	default:             return false;
	}
}

static bool is_place(const Expr *e)
{
	return e->kind == EXPR_IDENT || e->kind == EXPR_SELF || e->kind == EXPR_FIELD || e->kind == EXPR_INDEX;
}

/* Tokens that can start a bare-call argument at the start of a statement
 * (unary & and * do not exist). A `(` gets here only when it is not the
 * call's own parentheses (see call_parentheses_here). */
static bool starts_bare_arg(Parser *P, Token t)
{
	if (t.kind == TOK_LPAREN)
		return true;
	switch (t.kind) {
	case TOK_INT: case TOK_FLOAT: case TOK_STRING: case TOK_CHAR: case TOK_IDENT:
	case TOK_KW_TRUE: case TOK_KW_FALSE: case TOK_KW_SELF: case TOK_KW_SIZEOF:
	case TOK_MINUS: case TOK_BANG: case TOK_TILDE: case TOK_LBRACE: case TOK_PLUS: case TOK_KW_NOT:
		return true;
	default:
		(void)P;
		return false;
	}
}

static bool at_stmt_end(Parser *P)
{
	return check(P, TOK_NEWLINE) || check(P, TOK_EOF) || check(P, TOK_SEMI) || check(P, TOK_RBRACE) ||
	       check(P, TOK_LBRACE) || check(P, TOK_KW_ELSE);
}

/* `else V`, `else { ... }` or `else Why { ... }` after a declaration or a
 * statement (P->cur is the else): what's done if it panics. */
static void parse_recover(Parser *P, Stmt *s)
{
	Token kw = P->cur;
	advance(P);
	Recover *r = arena_alloc(P->arena, sizeof *r);
	r->loc = loc_of(P, kw);
	s->recover = r;
	int indent = P->stmt_indent;
	bool named = check(P, TOK_IDENT) && peek(P).kind == TOK_LBRACE && !is_type_name(P, P->cur.start, P->cur.length);
	if (named) {
		r->why = tok_text(P, P->cur);
		advance(P);
	}
	if (named || check(P, TOK_LBRACE)) {
		Token open = P->cur;
		r->block = parse_block(P, indent, "'else'");
		if (r->why) {
			/* str Why = (the panic's message), first in the block */
			Stmt *d = ast_stmt(P->arena, STMT_DECL, loc_of(P, open));
			TypeExpr *te = arena_alloc(P->arena, sizeof *te);
			te->kind = TYPEX_NAMED;
			te->name = "str";
			te->builtin = true;
			te->loc = r->loc;
			VarDecl *v = ast_var(P->arena, r->why, r->loc);
			v->type = te;
			Expr *call = ast_expr(P->arena, EXPR_CALL, r->loc);
			call->as.call.callee = ast_expr(P->arena, EXPR_IDENT, r->loc);
			call->as.call.callee->as.ident.name = " why";
			v->init = call;
			vec_push(P->arena, d->as.decl, v);
			vec_push(P->arena, r->block->stmts, d);
			for (int i = r->block->stmts.len - 1; i > 0; i--)
				r->block->stmts.items[i] = r->block->stmts.items[i - 1];
			r->block->stmts.items[0] = d;
		}
		return;
	}
	if (check(P, TOK_NEWLINE) || check(P, TOK_EOF)) {
		error_at(P, P->cur, "expected what to do if it fails after 'else': a value, or a block ('else { ... }', "
			 "or 'else Why { ... }' for the reason)");
		return;
	}
	Expr *v;
	if (check(P, TOK_LBRACE)) {
		Token open = P->cur;
		advance(P);
		v = parse_initializer_list(P, NULL, open);
	} else {
		v = parse_expr(P);
	}
	if (s->kind == STMT_DECL) {
		r->value = v;
	} else if (s->kind == STMT_EXPR && s->as.expr->kind == EXPR_ASSIGN) {
		/* Target = V (or += V ...), as a block of its own */
		Expr *a = ast_expr(P->arena, EXPR_ASSIGN, v->loc);
		a->as.assign.compound = s->as.expr->as.assign.compound;
		a->as.assign.op = s->as.expr->as.assign.op;
		a->as.assign.target = ast_clone_expr(P->arena, s->as.expr->as.assign.target);
		a->as.assign.value = v;
		Stmt *st = ast_stmt(P->arena, STMT_EXPR, v->loc);
		st->as.expr = a;
		r->block = ast_block(P->arena, v->loc);
		vec_push(P->arena, r->block->stmts, st);
	} else {
		error_at(P, kw, "'else' with a value needs something to give it to: 'X = ... else V' or "
			 "'T X = ... else V'; after a statement alone, write 'else { ... }'");
	}
}

static Expr *make_incdec(Parser *P, Expr *target, Token op_tok)
{
	Expr *a = ast_expr(P->arena, EXPR_ASSIGN, loc_of(P, op_tok));
	a->as.assign.compound = true;
	a->as.assign.op = op_tok.kind == TOK_PLUS_PLUS ? BIN_ADD : BIN_SUB;
	a->as.assign.target = target;
	Expr *one = ast_expr(P->arena, EXPR_INT, loc_of(P, op_tok));
	one->as.int_lit.value = 1;
	a->as.assign.value = one;
	if (!is_place(target))
		error_at(P, op_tok, "'%s' needs a variable, field or element", token_kind_name(op_tok.kind));
	return a;
}

/* Assignment, ++/--, call or bare call. `allow_bare` is false in for headers. */
static Expr *parse_simple(Parser *P, bool allow_bare)
{
	Token t = P->cur;
	if (check(P, TOK_PLUS_PLUS) || check(P, TOK_MINUS_MINUS)) {
		advance(P);
		Expr *target = parse_postfix_tail(P, parse_primary(P), NULL);
		return make_incdec(P, target, t);
	}

	bool ends_with_call = false;
	Expr *e;
	if (check(P, TOK_IDENT) || check(P, TOK_KW_SELF))
		e = parse_postfix_chain(P, parse_primary(P), &ends_with_call, allow_bare);
	else
		return parse_expr(P);
	if (P->panic)
		return e;

	bool compound;
	BinaryOp op;
	if (assign_op(P->cur.kind, &compound, &op)) {
		Token at = P->cur;
		advance(P);
		if (!is_place(e))
			error_at(P, at, "the left side of '%s' is not something that can be assigned", token_kind_name(at.kind));
		Expr *a = ast_expr(P->arena, EXPR_ASSIGN, loc_of(P, at));
		a->as.assign.compound = compound;
		a->as.assign.op = op;
		a->as.assign.target = e;
		if (check(P, TOK_LBRACE)) {
			Token open = P->cur;
			advance(P);
			a->as.assign.value = parse_initializer_list(P, NULL, open);
		} else {
			a->as.assign.value = parse_expr(P);
		}
		return a;
	}
	if (check(P, TOK_PLUS_PLUS) || check(P, TOK_MINUS_MINUS)) {
		Token op_tok = P->cur;
		advance(P);
		return make_incdec(P, e, op_tok);
	}
	if (allow_bare && !ends_with_call && (e->kind == EXPR_IDENT || e->kind == EXPR_FIELD)) {
		if (e->kind == EXPR_IDENT && check(P, TOK_IDENT)) {
			Token n = peek(P);
			if (n.kind == TOK_EQ || n.kind == TOK_SEMI) {
				/* `strng name = ...` reads like a declaration with a misspelled type */
				Token tt = { 0 };
				tt.kind = TOK_IDENT;
				tt.start = e->as.ident.name;
				tt.length = (int)strlen(e->as.ident.name);
				tt.line = e->loc.line;
				tt.col = e->loc.col;
				unknown_type_error(P, tt);
				return e;
			}
		}
		if (starts_bare_arg(P, P->cur)) {   /* including '{': `Names.add {"a", "b"}` */
			Expr *call = ast_expr(P->arena, EXPR_CALL, e->loc);
			call->as.call.callee = e;
			call->as.call.bare = true;
			do {
				push_arg(P, call);
			} while (!P->panic && match(P, TOK_COMMA));
			return call;
		}
		if (at_stmt_end(P)) {
			Expr *call = ast_expr(P->arena, EXPR_CALL, e->loc);
			call->as.call.callee = e;
			call->as.call.bare = true;
			return call;
		}
	}
	return parse_binary(P, PREC_LOG_OR, e);
}

static bool starts_decl(Parser *P)
{
	Token t = P->cur;
	if (is_builtin_type_kw(t.kind) || is_qualifier_kw(t.kind) || t.kind == TOK_KW_LIST || t.kind == TOK_KW_LOOKUP)
		return true;
	if (t.kind == TOK_KW_STRUCT || t.kind == TOK_KW_UNION || t.kind == TOK_KW_ENUM)
		return true;
	if (t.kind == TOK_IDENT && is_type_name(P, t.start, t.length)) {
		Token n = peek(P);
		if (t.length == 7 && memcmp(t.start, "address", 7) == 0 && (is_builtin_type_kw(n.kind) ||
		    n.kind == TOK_KW_STRUCT || n.kind == TOK_KW_LIST))
			return true;   /* address int A */
		return n.kind == TOK_IDENT || n.kind == TOK_AMP || n.kind == TOK_KW_CONST;
	}
	if (t.kind == TOK_IDENT && t.length == 7 && memcmp(t.start, "address", 7) == 0 && is_builtin_type_kw(peek(P).kind))
		return true;   /* without `use addresses`: the declaration says what's missing */
	return false;
}

static Stmt *parse_decl_stmt(Parser *P, bool in_for)
{
	Stmt *s = ast_stmt(P->arena, STMT_DECL, loc_of(P, P->cur));
	DeclSpec ds;
	if (!parse_decl_spec(P, &ds, true))
		return s;
	if (ds.is_extern) {
		error_at(P, ds.first, "'extern' declarations belong at the top level of a file");
		return s;
	}
	if (ds.base && ds.base->builtin && strcmp(ds.base->name, "void") == 0) {
		error_at(P, ds.first, "a variable can't have type 'void'");
		return s;
	}
	do {
		VarDecl *v = parse_declarator(P, &ds, DCTX_LOCAL);
		if (!v)
			return s;
		vec_push(P->arena, s->as.decl, v);
	} while (match(P, TOK_COMMA));
	if (!in_for && check(P, TOK_KW_ELSE) && !P->panic) {
		if (s->as.decl.len != 1 || !s->as.decl.items[0]->init)
			error_at(P, P->cur, "'else' goes after one variable's declaration with a value: 'T X = ... else ...'");
		else
			parse_recover(P, s);
	}
	if (!in_for && !P->panic)
		expect_newline(P, "declaration");
	return s;
}

static void block_end_layout(Parser *P, int indent)
{
	if (P->cur.bol)
		check_layout(P, P->cur, indent, "the closing '}'");
}

/* After a `}`, allow the construct to continue on the next line at the
 * same indentation with `kw` (else / while). */
static bool continues_with(Parser *P, TokenKind kw, int indent)
{
	if (check(P, kw))
		return true;
	if (check(P, TOK_NEWLINE)) {
		Lexer copy = P->lex;
		copy.quiet = true;
		Token n = lexer_next(&copy);
		if (n.kind == kw && n.bol && n.indent == indent) {
			advance(P);
			return true;
		}
	}
	return false;
}

static Stmt *parse_if(Parser *P, Token kw, int indent)
{
	Stmt *s = ast_stmt(P->arena, STMT_IF, loc_of(P, kw));
	s->as.if_.cond = parse_cond(P, "an 'if' condition");
	s->as.if_.then_block = parse_block(P, indent, "the 'if' condition");
	if (P->panic)
		return s;
	if (check(P, TOK_IDENT) && P->cur.length == 4 && memcmp(P->cur.start, "elif", 4) == 0) {
		error_at(P, P->cur, "write 'else if', not 'elif'");
		return s;
	}
	if (continues_with(P, TOK_KW_ELSE, indent)) {
		Token e = P->cur;
		advance(P);
		if (check(P, TOK_KW_IF)) {
			Token i = P->cur;
			advance(P);
			s->as.if_.else_branch = parse_if(P, i, indent);
			return s;
		}
		Stmt *b = ast_stmt(P->arena, STMT_BLOCK, loc_of(P, e));
		b->as.block = parse_block(P, indent, "'else'");
		s->as.if_.else_branch = b;
	}
	return s;
}

/* `for [T] name in ...`: is there an `in` on this header line? */
static bool for_is_range(Parser *P)
{
	if (check(P, TOK_KW_IN))
		return true;
	Lexer copy = P->lex;
	copy.quiet = true;
	for (int i = 0; i < 8; i++) {
		Token n = lexer_next(&copy);
		if (n.kind == TOK_KW_IN)
			return true;
		if (n.kind == TOK_SEMI || n.kind == TOK_LBRACE || n.kind == TOK_NEWLINE || n.kind == TOK_EOF ||
		    n.kind == TOK_EQ)
			return false;
	}
	return false;
}

static Stmt *parse_for_range(Parser *P, Stmt *s, int indent)
{
	ForRange *r = arena_alloc(P->arena, sizeof *r);
	s->as.for_.range = r;
	TypeExpr *type = NULL;
	if (is_builtin_type_kw(P->cur.kind) || is_qualifier_kw(P->cur.kind) || check(P, TOK_KW_LIST) ||
	    check(P, TOK_KW_LOOKUP) ||
	    (check(P, TOK_IDENT) && is_type_name(P, P->cur.start, P->cur.length) &&
	     peek(P).kind != TOK_COMMA && peek(P).kind != TOK_KW_IN)) {
		DeclSpec ds;
		if (!parse_decl_spec(P, &ds, false))
			return s;
		if (ds.is_const || ds.is_volatile || ds.is_auto || !ds.base) {
			error_at(P, ds.first, "a 'for ... in' counter takes just a type (or none): 'for long i in n'");
			return s;
		}
		type = ds.base;
	}
	if (!check(P, TOK_IDENT)) {
		error_at(P, P->cur, "expected the counter's name after 'for', found %s", describe(P->cur));
		return s;
	}
	VarDecl *v = ast_var(P->arena, tok_text(P, P->cur), loc_of(P, P->cur));
	v->type = type;
	r->var = v;
	advance(P);
	if (match(P, TOK_COMMA)) {
		/* for Key, Value in Lookup */
		if (!check(P, TOK_IDENT)) {
			error_at(P, P->cur, "expected a second name after ',' (e.g. 'for Name, Age in Ages'), found %s",
				 describe(P->cur));
			return s;
		}
		r->var2 = ast_var(P->arena, tok_text(P, P->cur), loc_of(P, P->cur));
		advance(P);
	}
	if (!expect(P, TOK_KW_IN, r->var2 ? "'in' after the two names" : "'in' after the counter's name"))
		return s;
	bool saved = P->no_init;
	P->no_init = true;
	Expr *first = parse_expr(P);
	if (!P->panic && match(P, TOK_KW_TO)) {
		r->start = first;
		r->inclusive = true;
		r->end = parse_expr(P);
	} else if (!P->panic && match(P, TOK_KW_DOWN)) {
		r->start = first;
		r->down = true;
		r->inclusive = true;
		if (expect(P, TOK_KW_TO, "'to' after 'down' (count down with 'a down to b')"))
			r->end = parse_expr(P);
	} else {
		r->end = first;
	}
	if (!P->panic && match(P, TOK_KW_BY))
		r->step = parse_expr(P);
	P->no_init = saved;
	if (P->panic)
		return s;
	if (!check(P, TOK_LBRACE) && !check(P, TOK_NEWLINE)) {
		error_at(P, P->cur, "expected '{' after the range, found %s", describe(P->cur));
		return s;
	}
	s->as.for_.body = parse_block(P, indent, "the 'for' header");
	return s;
}

static Stmt *parse_for(Parser *P, Token kw, int indent)
{
	Stmt *s = ast_stmt(P->arena, STMT_FOR, loc_of(P, kw));
	if (for_is_range(P))
		return parse_for_range(P, s, indent);
	bool parens = false;
	/* optional C-style parentheses around the whole header */
	if (check(P, TOK_LPAREN)) {
		Lexer copy = P->lex;
		copy.quiet = true;
		int depth = 1;
		bool has_semi = false;
		for (;;) {
			Token n = lexer_next(&copy);
			if (n.kind == TOK_EOF || n.kind == TOK_NEWLINE)
				break;
			if (n.kind == TOK_LPAREN) depth++;
			if (n.kind == TOK_RPAREN && --depth == 0)
				break;
			if (n.kind == TOK_SEMI && depth == 1)
				has_semi = true;
		}
		if (has_semi) {
			parens = true;
			advance(P);
		}
	}
	bool saved = P->no_init;
	P->no_init = true;
	if (!check(P, TOK_SEMI)) {
		if (starts_decl(P)) {
			s->as.for_.init = parse_decl_stmt(P, true);
		} else {
			Stmt *i = ast_stmt(P->arena, STMT_EXPR, loc_of(P, P->cur));
			i->as.expr = parse_simple(P, false);
			s->as.for_.init = i;
		}
	}
	if (!P->panic && !check(P, TOK_SEMI)) {
		if (check(P, TOK_LBRACE) || check(P, TOK_NEWLINE))
			error_at(P, P->cur, "a 'for' header has three parts: 'for init; condition; update {' "
				 "(use 'while' for a plain condition)");
		else
			expect(P, TOK_SEMI, "';' after the 'for' initializer");
	} else {
		match(P, TOK_SEMI);
	}
	if (!P->panic && !check(P, TOK_SEMI))
		s->as.for_.cond = parse_cond(P, "a 'for' condition");
	if (!P->panic)
		expect(P, TOK_SEMI, "';' after the 'for' condition");
	if (!P->panic && !check(P, TOK_LBRACE) && !check(P, TOK_RPAREN)) {
		Stmt *u = ast_stmt(P->arena, STMT_EXPR, loc_of(P, P->cur));
		u->as.expr = parse_simple(P, false);
		s->as.for_.update = u;
	}
	P->no_init = saved;
	if (parens && !P->panic)
		expect(P, TOK_RPAREN, "')' to close the 'for' header");
	if (P->panic)
		return s;
	s->as.for_.body = parse_block(P, indent, "the 'for' header");
	return s;
}

static Stmt *parse_switch(Parser *P, Token kw, int indent)
{
	Stmt *s = ast_stmt(P->arena, STMT_SWITCH, loc_of(P, kw));
	s->as.switch_.value = parse_cond(P, "a 'switch' value");
	if (P->panic)
		return s;
	if (!expect(P, TOK_LBRACE, "'{' to open the switch"))
		return s;
	if (!expect_newline(P, "'{'"))
		return s;
	int saved_layout = P->layout_errors;
	P->layout_errors = 0;
	SwitchCase *cur = NULL;
	while (!check(P, TOK_RBRACE) && !check(P, TOK_EOF)) {
		Token t = P->cur;
		if (t.kind == TOK_KW_CASE || t.kind == TOK_KW_DEFAULT) {
			check_layout(P, t, indent + 1, "a case label");
			advance(P);
			SwitchCase c = { 0 };
			c.loc = loc_of(P, t);
			c.body = ast_block(P->arena, loc_of(P, t));
			if (t.kind == TOK_KW_DEFAULT) {
				if (s->as.switch_.default_index >= 0)
					error_at(P, t, "a switch can only have one 'default'");
				c.is_default = true;
				s->as.switch_.default_index = s->as.switch_.cases.len;
			} else {
				vec_push(P->arena, c.values, parse_expr(P));
			}
			if (!P->panic && !expect(P, TOK_COLON, "':' after the case label")) {
				synchronize(P);
				continue;
			}
			if (!P->panic)
				expect_newline(P, "the case label");
			if (P->panic)
				synchronize(P);
			vec_push(P->arena, s->as.switch_.cases, c);
			cur = &s->as.switch_.cases.items[s->as.switch_.cases.len - 1];
			continue;
		}
		if (!cur) {
			error_at(P, t, "statements in a switch must come after a 'case' or 'default' label");
			synchronize(P);
			continue;
		}
		check_layout(P, t, indent + 2, "this line");
		Stmt *st = parse_stmt(P, t.bol ? t.indent : indent + 2);
		if (st)
			vec_push(P->arena, cur->body->stmts, st);
		if (P->panic)
			synchronize(P);
	}
	block_end_layout(P, indent);
	expect(P, TOK_RBRACE, "'}' to close the switch");
	P->layout_errors = saved_layout;
	return s;
}

static Stmt *parse_stmt(Parser *P, int indent)
{
	Token t = P->cur;
	Stmt *s;
	P->stmt_indent = indent;

	switch (t.kind) {
	case TOK_LBRACE: {
		s = ast_stmt(P->arena, STMT_BLOCK, loc_of(P, t));
		s->as.block = parse_block(P, indent, "a nested block");
		if (!P->panic)
			expect_newline(P, "'}'");
		return s;
	}
	case TOK_KW_IF:
		advance(P);
		s = parse_if(P, t, indent);
		if (!P->panic)
			expect_newline(P, "'}'");
		return s;
	case TOK_KW_WHILE:
		advance(P);
		s = ast_stmt(P->arena, STMT_WHILE, loc_of(P, t));
		s->as.while_.cond = parse_cond(P, "a 'while' condition");
		s->as.while_.body = parse_block(P, indent, "the 'while' condition");
		if (!P->panic)
			expect_newline(P, "'}'");
		return s;
	case TOK_KW_UNTIL: {
		/* `until Done { }`: while !Done */
		advance(P);
		s = ast_stmt(P->arena, STMT_WHILE, loc_of(P, t));
		Expr *c = parse_cond(P, "an 'until' condition");
		s->as.while_.cond = negate(P, c);
		s->as.while_.body = parse_block(P, indent, "the 'until' condition");
		if (!P->panic)
			expect_newline(P, "'}'");
		return s;
	}
	case TOK_KW_DO: {
		advance(P);
		s = ast_stmt(P->arena, STMT_DO, loc_of(P, t));
		s->as.while_.body = parse_block(P, indent, "'do'");
		if (P->panic)
			return s;
		bool until = continues_with(P, TOK_KW_UNTIL, indent);
		if (!until && !continues_with(P, TOK_KW_WHILE, indent)) {
			error_at(P, P->cur, "expected 'while' or 'until' after the 'do' block (as '} while condition')");
			return s;
		}
		advance(P);
		s->as.while_.cond = parse_cond(P, until ? "a 'do ... until' condition" : "a 'do ... while' condition");
		if (until)
			s->as.while_.cond = negate(P, s->as.while_.cond);
		if (!P->panic)
			expect_newline(P, until ? "the 'do ... until' condition" : "the 'do ... while' condition");
		return s;
	}
	case TOK_KW_FOR:
		advance(P);
		s = parse_for(P, t, indent);
		if (!P->panic)
			expect_newline(P, "'}'");
		return s;
	case TOK_KW_SWITCH:
		advance(P);
		s = parse_switch(P, t, indent);
		if (!P->panic)
			expect_newline(P, "'}'");
		return s;
	case TOK_KW_RETURN:
		advance(P);
		s = ast_stmt(P->arena, STMT_RETURN, loc_of(P, t));
		if (!check(P, TOK_NEWLINE) && !check(P, TOK_SEMI)) {
			if (check(P, TOK_LBRACE)) {
				Token open = P->cur;
				advance(P);
				s->as.ret.value = parse_initializer_list(P, NULL, open);
			} else {
				s->as.ret.value = parse_expr(P);
			}
		}
		expect_newline(P, "'return'");
		return s;
	case TOK_KW_BREAK:
	case TOK_KW_CONTINUE:
		advance(P);
		s = ast_stmt(P->arena, t.kind == TOK_KW_BREAK ? STMT_BREAK : STMT_CONTINUE, loc_of(P, t));
		expect_newline(P, describe(t));
		return s;
	case TOK_KW_GOTO:
		advance(P);
		s = ast_stmt(P->arena, STMT_GOTO, loc_of(P, t));
		if (!check(P, TOK_IDENT)) {
			error_at(P, P->cur, "expected a label name after 'goto', found %s", describe(P->cur));
			return s;
		}
		s->as.goto_.label = tok_text(P, P->cur);
		advance(P);
		expect_newline(P, "the 'goto' label");
		return s;
	case TOK_KW_CASE:
	case TOK_KW_DEFAULT:
		error_at(P, t, "'%s' is only allowed directly inside a switch", token_kind_name(t.kind));
		return NULL;
	case TOK_KW_ELSE:
		error_at(P, t, "'else' without a matching 'if'");
		return NULL;
	case TOK_KW_OBJECT:
		error_at(P, t, "'%s' declarations are only allowed at the top level", token_kind_name(t.kind));
		return NULL;
	case TOK_KW_TYPEDEF:
		error_at(P, t, "Cabby has no 'typedef': a second name for a type is written 'double aka celsius', "
			 "at the top level of a file");
		return NULL;
	case TOK_USE:
		error_at(P, t, "'use' belongs at the top level of a file");
		advance(P);
		return NULL;
	case TOK_IDENT: {
		Token n = peek(P);
		if (n.kind == TOK_COLON) {
			advance(P);
			advance(P);
			s = ast_stmt(P->arena, STMT_LABEL, loc_of(P, t));
			s->as.label.name = tok_text(P, t);
			expect_newline(P, "the label (put the next statement on its own line)");
			return s;
		}
		break;
	}
	default:
		break;
	}

	if ((t.kind == TOK_KW_STRUCT || t.kind == TOK_KW_UNION || t.kind == TOK_KW_ENUM) &&
	    peek(P).kind == TOK_IDENT) {
		Lexer copy = P->lex;
		copy.quiet = true;
		lexer_next(&copy);
		if (lexer_next(&copy).kind == TOK_LBRACE) {
			error_at(P, t, "'%s' declarations are only allowed at the top level", token_kind_name(t.kind));
			return NULL;
		}
	}
	if (starts_decl(P))
		return parse_decl_stmt(P, false);

	s = ast_stmt(P->arena, STMT_EXPR, loc_of(P, t));
	s->as.expr = parse_simple(P, true);
	if (!P->panic && check(P, TOK_KW_ELSE))
		parse_recover(P, s);
	if (!P->panic) {
		if (check(P, TOK_LBRACE) && s->as.expr->kind == EXPR_CALL && s->as.expr->as.call.bare)
			error_at(P, P->cur, "unexpected '{'; is a keyword (if, while, for ...) missing?");
		else
			expect_newline(P, "the statement");
	}
	return s;
}

/* block = "{" NEWLINE { stmt } "}" — `indent` is the opening line's. The
 * NEWLINE after the closing brace is left for the caller. */
static Block *parse_block(Parser *P, int indent, const char *what)
{
	Block *b = ast_block(P->arena, loc_of(P, P->cur));
	if (P->panic)
		return b;

	if (check(P, TOK_NEWLINE)) {
		/* `{` alone on the next line (Allman style) */
		Lexer copy = P->lex;
		copy.quiet = true;
		Token n = lexer_next(&copy);
		if (n.kind == TOK_LBRACE && n.bol) {
			advance(P);
			check_layout(P, P->cur, indent, "the opening '{'");
		}
	}
	if (check(P, TOK_COLON)) {
		error_at(P, P->cur, "unexpected ':' after %s; open the block with '{'", what);
		return b;
	}
	if (check(P, TOK_EQ)) {
		error_at(P, P->cur, "'=' is assignment; use '==' to compare");
		return b;
	}
	if (!check(P, TOK_LBRACE)) {
		error_at(P, P->cur, "expected '{' after %s, found %s", what, describe(P->cur));
		return b;
	}
	b->loc = loc_of(P, P->cur);
	advance(P);
	if (match(P, TOK_RBRACE))   /* `{ }` on one line */
		return b;
	if (!expect_newline(P, "'{' (the block's statements go on the following lines)"))
		return b;

	int saved_layout = P->layout_errors;
	P->layout_errors = 0;
	while (!check(P, TOK_RBRACE) && !check(P, TOK_EOF)) {
		check_layout(P, P->cur, indent + 1, "this line");
		/* nested blocks are checked against this line as written, so one
		 * misplaced line does not make everything inside it an error too */
		Stmt *s = parse_stmt(P, P->cur.bol ? P->cur.indent : indent + 1);
		if (s)
			vec_push(P->arena, b->stmts, s);
		if (P->panic)
			synchronize(P);
	}
	P->layout_errors = 0;
	block_end_layout(P, indent);
	P->layout_errors = saved_layout;
	if (check(P, TOK_EOF))
		error_at(P, P->cur, "missing '}' to close the block opened at line %d", b->loc.line);
	else
		advance(P);
	return b;
}

/* ================================================================ declarations */

/* Parameters up to `{`, NEWLINE or `const` (methods). */
static void parse_params(Parser *P, FnDecl *fn, bool is_extern)
{
	if (check(P, TOK_KW_VOID)) {
		Token n = peek(P);
		if (n.kind == TOK_LBRACE || n.kind == TOK_NEWLINE || n.kind == TOK_KW_CONST || n.kind == TOK_EOF) {
			advance(P);
			return;
		}
	}
	if (check(P, TOK_LPAREN)) {
		error_at(P, P->cur, "parameters are written without parentheses: 'int add int a, int b {'");
		return;
	}
	while (!check(P, TOK_LBRACE) && !check(P, TOK_NEWLINE) && !check(P, TOK_EOF) &&
	       !(check(P, TOK_KW_CONST) && fn->owner && (peek(P).kind == TOK_LBRACE || peek(P).kind == TOK_NEWLINE))) {
		if (check(P, TOK_ELLIPSIS)) {
			if (!is_extern)
				error_at(P, P->cur, "variadic '...' is only allowed on extern functions");
			advance(P);
			fn->is_variadic = true;
			if (!check(P, TOK_NEWLINE))
				error_at(P, P->cur, "'...' must be the last parameter");
			return;
		}
		DeclSpec ds;
		if (!starts_type(P, P->cur)) {
			if (check(P, TOK_IDENT) && (peek(P).kind == TOK_IDENT ||
						    (P->cur.length == 7 && memcmp(P->cur.start, "address", 7) == 0)))
				unknown_type_error(P, P->cur);
			else if (check(P, TOK_IDENT))
				error_at(P, P->cur, "expected a parameter type before '%.*s' (e.g. 'int %.*s')",
					 P->cur.length, P->cur.start, P->cur.length, P->cur.start);
			else
				error_at(P, P->cur, "expected a parameter or '{', found %s", describe(P->cur));
			return;
		}
		if (!parse_decl_spec(P, &ds, false))
			return;
		VarDecl *v = parse_declarator(P, &ds, DCTX_PARAM);
		if (!v)
			return;
		if (is_extern && v->type && v->type->kind == TYPEX_NAMED && v->type->builtin &&
		    strcmp(v->type->name, "str") == 0)
			v->is_extern_str = true;
		vec_push(P->arena, fn->params, v);
		if (!match(P, TOK_COMMA))
			break;
	}
}

/* After `type name`: parameters, optional const (methods), body or end of line. */
static FnDecl *parse_fn_rest_as(Parser *P, TypeExpr *ret, const char *name, SrcLoc at, ObjectDecl *owner,
				bool is_extern, bool is_static, int indent);

static FnDecl *parse_fn_rest(Parser *P, TypeExpr *ret, Token name, ObjectDecl *owner, bool is_extern,
			     bool is_static, int indent)
{
	return parse_fn_rest_as(P, ret, tok_text(P, name), loc_of(P, name), owner, is_extern, is_static, indent);
}

static FnDecl *parse_fn_rest_as(Parser *P, TypeExpr *ret, const char *name, SrcLoc at, ObjectDecl *owner,
				bool is_extern, bool is_static, int indent)
{
	FnDecl *fn = ast_fn(P->arena, name, at);
	fn->ret = ret;
	fn->owner = owner;
	fn->is_extern = is_extern;
	fn->is_static = is_static;
	if (owner)
		fn->self_kind = is_static ? SELF_NONE : SELF_MUT_REF;
	parse_params(P, fn, is_extern);
	if (P->panic)
		return fn;
	if (owner && check(P, TOK_KW_CONST)) {
		Token c = P->cur;
		advance(P);
		if (is_static)
			error_at(P, c, "a static method has no object to be const about");
		fn->self_kind = SELF_REF;
	}
	if (is_extern) {
		if (check(P, TOK_LBRACE))
			error_at(P, P->cur, "extern functions are defined in C and can't have a body here");
		else
			expect_newline(P, "the extern declaration");
		fn->is_prototype = true;
		return fn;
	}
	if (check(P, TOK_NEWLINE) && !owner) {
		Lexer copy = P->lex;
		copy.quiet = true;
		Token n = lexer_next(&copy);
		if (!(n.kind == TOK_LBRACE && n.bol)) {
			advance(P);   /* prototype */
			fn->is_prototype = true;
			return fn;
		}
	}
	fn->body = parse_block(P, indent, "the function header");
	if (!P->panic)
		expect_newline(P, "'}'");
	return fn;
}

static ObjectDecl *parse_object(Parser *P, ObjectKind kind, Token kw)
{
	int indent = kw.bol ? kw.indent : 0;
	if (!check(P, TOK_IDENT)) {
		error_at(P, P->cur, "expected a name after '%s', found %s", token_kind_name(kw.kind), describe(P->cur));
		return NULL;
	}
	ObjectDecl *obj = ast_object(P->arena, kind, tok_text(P, P->cur), loc_of(P, P->cur));
	advance(P);

	if (check(P, TOK_COLON)) {
		if (kind != OBJ_OBJECT) {
			error_at(P, P->cur, "only objects can inherit; a %s is plain data", ast_object_kind_name(kind));
			return obj;
		}
		advance(P);
		do {
			if (!check(P, TOK_IDENT)) {
				error_at(P, P->cur, "expected a parent object name, found %s", describe(P->cur));
				return obj;
			}
			ParentRef pr = { tok_text(P, P->cur), loc_of(P, P->cur), NULL };
			for (int i = 0; i < obj->parents.len; i++)
				if (strcmp(obj->parents.items[i].name, pr.name) == 0)
					error_at(P, P->cur, "'%s' is listed as a parent more than once", pr.name);
			vec_push(P->arena, obj->parents, pr);
			advance(P);
			if (check(P, TOK_DOT) && (strcmp(pr.name, "gui") == 0 || strcmp(pr.name, "tui") == 0 ||
						      strcmp(pr.name, "game") == 0 || strcmp(pr.name, "gtk") == 0 ||
						      strcmp(pr.name, "tgame") == 0 || strcmp(pr.name, "world") == 0)) {
				error_at(P, P->cur, "'%s.' names come from the %s library: add 'use %s' at the top of the file",
					 pr.name, pr.name, pr.name);
				return obj;
			}
		} while (match(P, TOK_COMMA));
	}
	brace_on_next_line(P);
	if (!expect(P, TOK_LBRACE, "'{' to open the body"))
		return obj;
	if (match(P, TOK_RBRACE)) {
		expect_newline(P, "'}'");
		return obj;
	}
	if (!expect_newline(P, "'{'"))
		return obj;

	P->layout_errors = 0;
	while (!check(P, TOK_RBRACE) && !check(P, TOK_EOF)) {
		Token t = P->cur;
		check_layout(P, t, indent + 1, "this member");
		/* `inner`: for the object's own methods only (and its children's) */
		bool inner = false;
		if (t.kind == TOK_KW_INNER) {
			inner = true;
			advance(P);
			if (kind != OBJ_OBJECT)
				error_at(P, t, "only objects have inner parts; everything in a %s is plain data, open to all",
					 ast_object_kind_name(kind));
			t = P->cur;
			if (t.kind == TOK_TILDE || maker_here(P, obj)) {
				error_at(P, t, "'inner' is for fields and methods; this part runs by itself");
				inner = false;
			}
		}
		int fields_before = obj->fields.len, methods_before = obj->methods.len;
		if (t.kind == TOK_IDENT && destructor_name_ok(t, obj->name) && !maker_here(P, obj) &&
		    (is_builtin_type_kw(peek(P).kind) || (peek(P).kind == TOK_IDENT &&
							      is_type_name(P, peek(P).start, peek(P).length) &&
							      /* (`json item const str &Name`: a method called
							       * item, returning a json, though there's a type
							       * called item too; `rocket item Thing {` would
							       * be a parameter) */
							      peek2(P).kind == TOK_IDENT &&
							      !is_type_name(P, peek2(P).start, peek2(P).length)))) {
			/* `rocket int Seconds {`: nothing could give it the values */
			error_at(P, t, "what runs when '%s' is made takes no parameters: write '%.*s {'", obj->name,
				 t.length, t.start);
			synchronize(P);
			continue;
		}
		if (maker_here(P, obj)) {
			/* `rocket { }`: runs when a rocket is made */
			if (kind != OBJ_OBJECT) {
				error_at(P, t, "only objects can run something when made; a %s is plain data",
					 ast_object_kind_name(kind));
			}
			advance(P);
			FnDecl *d = ast_fn(P->arena, arena_strndup(P->arena, t.start, (size_t)t.length), loc_of(P, t));
			d->owner = obj;
			d->self_kind = SELF_MUT_REF;
			d->is_maker = true;
			d->ret = ast_type_named(P->arena, "void", true, loc_of(P, t));
			d->body = parse_block(P, t.bol ? t.indent : indent + 1, "what runs when it is made");
			if (!P->panic)
				expect_newline(P, "'}'");
			if (obj->make_fn)
				error_at(P, t, "'%s' already says what runs when one is made", obj->name);
			obj->make_fn = d;
			if (kind == OBJ_OBJECT)
				vec_push(P->arena, obj->methods, d);
		} else if (t.kind == TOK_TILDE) {
			advance(P);
			if (kind != OBJ_OBJECT) {
				error_at(P, t, "only objects have destructors; a %s is plain data", ast_object_kind_name(kind));
			} else if (!check(P, TOK_IDENT) || !destructor_name_ok(P->cur, obj->name)) {
				const char *last = strrchr(obj->name, '.');
				error_at(P, P->cur, "the destructor must be named '~%s'", last ? last + 1 : obj->name);
			} else {
				Token n = P->cur;
				advance(P);
				FnDecl *d = ast_fn(P->arena, arena_strndup(P->arena, t.start, (size_t)(n.start + n.length - t.start)),
						   loc_of(P, t));
				d->owner = obj;
				d->self_kind = SELF_MUT_REF;
				d->is_destructor = true;
				d->ret = ast_type_named(P->arena, "void", true, loc_of(P, t));
				if (!check(P, TOK_LBRACE) && !check(P, TOK_NEWLINE))
					error_at(P, P->cur, "a destructor takes no parameters");
				else {
					d->body = parse_block(P, t.bol ? t.indent : indent + 1, "the destructor");
					if (!P->panic)
						expect_newline(P, "'}'");
				}
				if (obj->drop_fn)
					error_at(P, t, "'%s' already has a destructor", obj->name);
				obj->drop_fn = d;
				vec_push(P->arena, obj->methods, d);
			}
		} else if (t.kind == TOK_USE || t.kind == TOK_KW_OBJECT || t.kind == TOK_KW_TYPEDEF ||
			   t.kind == TOK_KW_EXTERN) {
			error_at(P, t, "%s is not allowed inside a %s", describe(t), ast_object_kind_name(kind));
		} else if ((t.kind == TOK_IDENT && !is_type_name(P, t.start, t.length) && peek(P).kind != TOK_IDENT) ||
			   t.kind == TOK_KW_IF || t.kind == TOK_KW_WHILE || t.kind == TOK_KW_FOR ||
			   t.kind == TOK_KW_RETURN || t.kind == TOK_KW_SWITCH) {
			/* `every 1000, tick` straight inside an object: a statement */
			if (kind == OBJ_OBJECT)
				error_at(P, t, "an object holds only fields and methods; a line that does "
					 "something, like this one, goes inside a method (for a window, usually 'build')");
			else
				error_at(P, t, "a %s holds only fields; a line that does something, like this "
					 "one, goes inside a function", ast_object_kind_name(kind));
		} else {
			DeclSpec ds;
			bool ok = parse_decl_spec(P, &ds, true);
			if (ok && (ds.is_register || ds.is_extern || ds.is_auto)) {
				error_at(P, ds.first, "'%s' can't be used on a member",
					 ds.is_register ? "register" : ds.is_extern ? "extern" : "auto");
				ok = false;
			}
			if (ok && check(P, TOK_KW_AKA)) {
				error_at(P, P->cur, "'aka' gives a type a second name, and belongs at the top level of "
					 "a file (e.g. 'double aka celsius')");
				ok = false;
			}
			if (ok) {
				bool amp = check(P, TOK_AMP);
				Token name = amp ? peek(P) : P->cur;
				Token after;
				bool brace_next = false;   /* `void tick` with `{` alone on the next line */
				{
					Lexer copy = P->lex;
					copy.quiet = true;
					if (amp)
						lexer_next(&copy);
					after = lexer_next(&copy);
					if (after.kind == TOK_NEWLINE) {
						Token n = lexer_next(&copy);
						brace_next = n.kind == TOK_LBRACE && n.bol;
					}
				}
				bool is_field = !ds.is_static && !brace_next &&
						(after.kind == TOK_NEWLINE || after.kind == TOK_LBRACKET ||
						 after.kind == TOK_EQ || after.kind == TOK_COMMA || after.kind == TOK_EOF);
				if (is_field) {
					if (amp) {
						error_at(P, P->cur, "fields can't be references; a borrow may not be stored");
					} else {
						do {
							VarDecl *v = parse_declarator(P, &ds, DCTX_FIELD);
							if (!v)
								break;
							if (ds.base->builtin && strcmp(ds.base->name, "void") == 0)
								error_at(P, ds.first, "a field can't have type 'void'");
							vec_push(P->arena, obj->fields, v);
						} while (match(P, TOK_COMMA));
						if (!P->panic)
							expect_newline(P, "the field");
					}
				} else if (kind != OBJ_OBJECT) {
					error_at(P, name, "a %s has no methods; use an object for that",
						 ast_object_kind_name(kind));
				} else if (ds.is_const) {
					error_at(P, ds.first, "to make a method read-only, put 'const' after its parameters: "
						 "'int area const {'");
				} else if (amp) {
					error_at(P, P->cur, "methods can't return references");
				} else if (name.kind != TOK_IDENT) {
					error_at(P, name, "expected a field or method name, found %s", describe(name));
				} else {
					advance(P);
					FnDecl *m = parse_fn_rest(P, ds.base, name, obj, false, ds.is_static,
								  t.bol ? t.indent : indent + 1);
					vec_push(P->arena, obj->methods, m);
				}
			}
		}
		if (inner) {
			for (int i = fields_before; i < obj->fields.len; i++)
				obj->fields.items[i]->is_inner = true;
			for (int i = methods_before; i < obj->methods.len; i++)
				obj->methods.items[i]->is_inner = true;
		}
		if (P->panic)
			synchronize(P);
	}
	P->layout_errors = 0;
	block_end_layout(P, indent);
	if (!expect(P, TOK_RBRACE, "'}' to close the body"))
		return obj;
	expect_newline(P, "'}'");
	return obj;
}

/* `rocket {` (or `rocket` with `{` alone on the next line) inside object
 * rocket: what runs when one is made */
static bool maker_here(Parser *P, ObjectDecl *obj)
{
	if (!check(P, TOK_IDENT) || !destructor_name_ok(P->cur, obj->name))
		return false;
	Lexer copy = P->lex;
	copy.quiet = true;
	Token n = lexer_next(&copy);
	if (n.kind == TOK_NEWLINE) {
		n = lexer_next(&copy);
		return n.kind == TOK_LBRACE && n.bol;
	}
	return n.kind == TOK_LBRACE;
}

/* `{` alone on the next line (Allman style), after an object, struct,
 * union or enum's name: steps onto it. */
static void brace_on_next_line(Parser *P)
{
	if (!check(P, TOK_NEWLINE))
		return;
	Lexer copy = P->lex;
	copy.quiet = true;
	Token n = lexer_next(&copy);
	if (n.kind == TOK_LBRACE && n.bol) {
		advance(P);
		check_layout(P, P->cur, 0, "the opening '{'");
	}
}

/* After `struct name` (or `enum name`): is a body next, on this line or
 * alone on the next? */
static bool body_follows(Parser *P)
{
	Lexer copy = P->lex;
	copy.quiet = true;
	lexer_next(&copy);                 /* the name */
	Token n = lexer_next(&copy);
	if (n.kind == TOK_NEWLINE) {
		n = lexer_next(&copy);
		return n.kind == TOK_LBRACE && n.bol;
	}
	return n.kind == TOK_LBRACE || n.kind == TOK_COLON;
}

static EnumDecl *parse_enum(Parser *P)
{
	if (!check(P, TOK_IDENT)) {
		error_at(P, P->cur, "expected a name after 'enum', found %s", describe(P->cur));
		return NULL;
	}
	EnumDecl *en = arena_alloc(P->arena, sizeof *en);
	en->name = tok_text(P, P->cur);
	en->loc = loc_of(P, P->cur);
	advance(P);
	brace_on_next_line(P);
	if (!expect(P, TOK_LBRACE, "'{' to open the enum"))
		return en;
	for (;;) {
		while (match(P, TOK_NEWLINE))
			;
		if (check(P, TOK_RBRACE) || check(P, TOK_EOF))
			break;
		if (P->cur.bol)
			check_layout(P, P->cur, 1, "this enum constant");
		if (!check(P, TOK_IDENT)) {
			error_at(P, P->cur, "expected an enum constant name, found %s", describe(P->cur));
			return en;
		}
		EnumItem it = { tok_text(P, P->cur), loc_of(P, P->cur), NULL, 0 };
		advance(P);
		if (match(P, TOK_EQ))
			it.value = parse_expr(P);
		vec_push(P->arena, en->items, it);
		if (P->panic)
			return en;
		if (!match(P, TOK_COMMA) && !check(P, TOK_NEWLINE) && !check(P, TOK_RBRACE)) {
			error_at(P, P->cur, "expected ',' or a new line between enum constants, found %s", describe(P->cur));
			return en;
		}
	}
	block_end_layout(P, 0);
	if (expect(P, TOK_RBRACE, "'}' to close the enum"))
		expect_newline(P, "'}'");
	return en;
}

static void push_decl(Parser *P, Decl *d) { vec_push(P->arena, P->ld->prog->decls, d); }

static void parse_top(Parser *P)
{
	Token t = P->cur;
	SrcLoc l = loc_of(P, t);
	P->layout_errors = 0;
	check_layout(P, t, 0, "a top-level declaration");

	switch (t.kind) {
	case TOK_USE: {
		advance(P);
		char *path;
		/* A file of one's own gives its name as a prefix (`use local
		 * shapes/geo` gives geo.area), as a library's name does */
		if (t.use_local) {
			const char *s = t.v.str.data;
			int n = (int)t.v.str.len;
			if (n > 4 && memcmp(s + n - 4, ".hby", 4) == 0)
				n -= 4;
			int from = n;
			while (from > 0 && s[from - 1] != '/')
				from--;
			bool word = from < n && (isalpha((unsigned char)s[from]) || s[from] == '_');
			for (int k = from; word && k < n; k++)
				word = isalnum((unsigned char)s[k]) || s[k] == '_';
			if (word)
				add_namespace(P->ld, s + from, n - from);
		}
		if (t.use_local) {
			const char *slash = strrchr(P->path, '/');
			size_t dl = slash ? (size_t)(slash - P->path + 1) : 0;
			path = arena_alloc(P->arena, dl + t.v.str.len + 1);
			memcpy(path, P->path, dl);
			memcpy(path + dl, t.v.str.data, t.v.str.len + 1);
			struct stat st;
			if (stat(path, &st) != 0) {
				/* no header: a library of plain functions can be its .cby alone */
				size_t pl = strlen(path);
				char *impl = arena_strndup(P->arena, path, pl);
				memcpy(impl + pl - 4, ".cby", 4);
				if (stat(impl, &st) == 0) {
					parse_file(P->ld, impl, l);
					diag_set_file(P->file);
					expect_newline(P, "use");
					return;
				}
				error_at(P, t, "can't find '%.*s' (looked for %s, and %s beside it)", (int)t.v.str.len - 4,
					 t.v.str.data, path, impl);
				expect_newline(P, "use");
				return;
			}
		} else {
			path = NULL;
			const char *retired = retired_header_hint(t.v.str.data, (int)t.v.str.len - 4);
			if (retired) {
				/* an old copy may still be installed: it's never used */
				error_at(P, t, "can't find header '%.*s' in the Cabby library directory%s", (int)t.v.str.len - 4,
					 t.v.str.data, retired);
				expect_newline(P, "use");
				return;
			}
			for (const char *const *d = P->ld->lib_dirs; d && *d; d++) {
				size_t dl = strlen(*d);
				char *cand = arena_alloc(P->arena, dl + t.v.str.len + 2);
				memcpy(cand, *d, dl);
				cand[dl] = '/';
				memcpy(cand + dl + 1, t.v.str.data, t.v.str.len + 1);
				struct stat st;
				if (stat(cand, &st) == 0) {
					path = cand;
					break;
				}
			}
			if (!path) {
				const char *hint = P->ld->lib_dirs && *P->ld->lib_dirs ? "" : " (none configured; set CABBY_LIB)";
				error_at(P, t, "can't find header '%.*s' in the Cabby library directory%s", (int)t.v.str.len - 4,
					 t.v.str.data, hint);
				expect_newline(P, "use");
				return;
			}
		}
		/* A library that needs something installed to be built (SDL 3 for gui
		 * and game, GTK 4 for gtk) has its header anyway: without its native
		 * code, say what to install, here at the `use` */
		if (!t.use_local) {
			const char *needs = missing_library_hint(path, t.v.str.data, (int)t.v.str.len - 4);
			if (needs) {
				error_at(P, t, "%s", needs);
				expect_newline(P, "use");
				return;
			}
		}
		/* A library's name is its namespace: use files gives files.read */
		if (!t.use_local) {
			int nl = (int)t.v.str.len - 4;
			if (!loader_is_namespace(P->ld, t.v.str.data, nl))
				vec_push(P->ld->A, P->ld->namespaces, arena_strndup(P->ld->A, t.v.str.data, (size_t)nl));
			if (nl == 5 && memcmp(t.v.str.data, "files", 5) == 0) {
				add_type_name(P->ld, "file", 4);   /* the files library's type */
				add_namespace(P->ld, "file", 4);   /* and where its methods' names start (file.read_bytes) */
			}
			if (nl == 9 && memcmp(t.v.str.data, "addresses", 9) == 0)
				add_type_name(P->ld, "address", 7);   /* address T */
		}
		/* Read the header before the next line is lexed, so that line can
		 * already use the header's namespaces (gui.window). */
		parse_file(P->ld, path, l);
		/* its implementation file, if one sits next to it */
		size_t pl = strlen(path);
		if (pl > 4 && strcmp(path + pl - 4, ".hby") == 0) {
			char *impl = arena_strndup(P->arena, path, pl);
			memcpy(impl + pl - 4, ".cby", 4);
			struct stat st;
			if (stat(impl, &st) == 0)
				parse_file(P->ld, impl, l);
		}
		if (!t.use_local) {
			/* a library header may come with native code: libcabby<name>.so/.a */
			const char *slash = strrchr(path, '/');
			NativeLib nl = { arena_strndup(P->arena, t.v.str.data, t.v.str.len - 4),
					 slash ? arena_strndup(P->arena, path, (size_t)(slash - path)) : "." };
			bool seen = false;
			for (int i = 0; i < P->ld->prog->natives.len; i++)
				if (strcmp(P->ld->prog->natives.items[i].name, nl.name) == 0)
					seen = true;
			if (!seen)
				vec_push(P->arena, P->ld->prog->natives, nl);
		}
		diag_set_file(P->file);
		expect_newline(P, "use");
		return;
	}
	case TOK_KW_OBJECT:
	case TOK_KW_STRUCT:
	case TOK_KW_UNION: {
		Token n = peek(P);
		if (t.kind == TOK_KW_OBJECT || (n.kind == TOK_IDENT && body_follows(P))) {
			advance(P);
			ObjectKind k = t.kind == TOK_KW_OBJECT ? OBJ_OBJECT : t.kind == TOK_KW_STRUCT ? OBJ_STRUCT : OBJ_UNION;
			ObjectDecl *o = parse_object(P, k, t);
			if (o) {
				Decl *d = ast_decl(P->arena, DECL_OBJECT, l);
				d->as.object = o;
				push_decl(P, d);
			}
			return;
		}
		break;   /* `struct point p` — a global variable */
	}
	case TOK_KW_ENUM: {
		if (peek(P).kind == TOK_IDENT && body_follows(P)) {
			advance(P);
			EnumDecl *en = parse_enum(P);
			if (en) {
				Decl *d = ast_decl(P->arena, DECL_ENUM, l);
				d->as.enum_ = en;
				push_decl(P, d);
			}
			return;
		}
		break;
	}
	case TOK_KW_CALLBACK: {
		/* `callback int comparer address void, address void`: a C function
		 * pointer's type, by the shape of the functions it can point to */
		advance(P);
		DeclSpec ds;
		if (!parse_decl_spec(P, &ds, false))
			return;
		FnDecl *sig;
		bool address_named = ds.base && ds.base->kind == TYPEX_ADDRESS && ds.base->inner &&
				     ds.base->inner->kind == TYPEX_NAMED && !ds.base->inner->builtin &&
				     (starts_type(P, P->cur) || check(P, TOK_NEWLINE) || check(P, TOK_KW_VOID));
		if (check(P, TOK_IDENT) && !address_named) {
			Token name = P->cur;
			advance(P);
			sig = parse_fn_rest(P, ds.base, name, NULL, true, false, 0);
		} else if (address_named) {
			/* `callback address starter address Given`: an address it gives, and
			 * its name (already a type's, as the file's types are found first) */
			TypeExpr *plain = ast_type_expr(P->arena, TYPEX_ADDRESS, ds.base->loc);
			sig = parse_fn_rest_as(P, plain, ds.base->inner->name, ds.base->inner->loc, NULL, true, false, 0);
		} else {
			error_at(P, P->cur, "a callback is named after what it gives: 'callback int comparer address A, address B'");
			return;
		}
		Decl *d = ast_decl(P->arena, DECL_TYPEDEF, l);
		d->as.typedef_.name = sig->name;
		d->as.typedef_.callback = sig;
		push_decl(P, d);
		return;
	}
	case TOK_KW_TYPEDEF:
		error_at(P, t, "Cabby has no 'typedef': write the type, then 'aka', then the new name "
			 "(e.g. 'double aka celsius')");
		return;
	case TOK_KW_IF: case TOK_KW_WHILE: case TOK_KW_FOR: case TOK_KW_DO: case TOK_KW_SWITCH:
	case TOK_KW_RETURN: case TOK_KW_UNTIL:
		error_at(P, t, "statements must be inside a function");
		return;
	case TOK_LBRACE:
		error_at(P, t, "unexpected '{' at top level");
		return;
	case TOK_RBRACE:
		error_at(P, t, "unmatched '}'");
		advance(P);
		return;
	default:
		break;
	}

	/* `strict long kind duration`: a kind that never takes plain numbers */
	bool strict = false;
	if (t.kind == TOK_IDENT && t.length == 6 && memcmp(t.start, "strict", 6) == 0 && starts_type(P, peek(P))) {
		strict = true;
		advance(P);
		t = P->cur;
	}
	if (!starts_type(P, t) && !is_qualifier_kw(t.kind)) {
		if (t.kind == TOK_IDENT) {
			Token n = peek(P);
			if (n.kind == TOK_IDENT)
				unknown_type_error(P, t);
			else
				error_at(P, t, "expected a declaration; statements must be inside a function");
		} else {
			error_at(P, t, "expected a declaration, found %s", describe(t));
		}
		return;
	}

	DeclSpec ds;
	if (!parse_decl_spec(P, &ds, true))
		return;
	if (kind_decl_here(P)) {
		/* `long kind duration`: a kind of value of its own, kept as a long;
		 * `long kind moment, steps duration`: one that steps by another */
		Token kw = P->cur;
		if (ds.is_const || ds.is_volatile || ds.is_static || ds.is_extern || ds.is_register || ds.is_auto ||
		    !ds.base) {
			error_at(P, ds.first, "'kind' makes a kind of number: write the number it is kept as, then "
				 "'kind', then its name (e.g. 'long kind duration')");
			return;
		}
		advance(P);
		Token name = P->cur;
		advance(P);
		Decl *d = ast_decl(P->arena, DECL_TYPEDEF, l);
		d->as.typedef_.name = arena_strndup(P->arena, name.start, (size_t)name.length);
		d->as.typedef_.type = ds.base;
		d->as.typedef_.is_kind = true;
		d->as.typedef_.is_strict = strict;
		if (check(P, TOK_COMMA)) {
			advance(P);
			if (!check(P, TOK_IDENT) || P->cur.length != 5 || memcmp(P->cur.start, "steps", 5) != 0) {
				error_at(P, P->cur, "after the ',' write 'steps' and the kind it steps by (e.g. 'long kind "
					 "moment, steps duration')");
				return;
			}
			advance(P);
			DeclSpec sd;
			if (!parse_decl_spec(P, &sd, false) || !sd.base)
				return;
			d->as.typedef_.steps = sd.base;
		}
		(void)kw;
		push_decl(P, d);
		expect_newline(P, "the kind's name");
		return;
	}
	if (strict) {
		error_at(P, ds.first, "'strict' belongs in front of a kind: 'strict long kind duration'");
		return;
	}
	if (check(P, TOK_KW_AKA)) {
		/* `double aka celsius`: a second name for a type */
		Token aka = P->cur;
		if (ds.is_const || ds.is_volatile || ds.is_static || ds.is_extern || ds.is_register || ds.is_auto) {
			error_at(P, ds.first, "'aka' names a type; '%.*s' belongs on the variables that use it",
				 ds.first.length, ds.first.start);
			return;
		}
		advance(P);
		if (!check(P, TOK_IDENT)) {
			error_at(P, P->cur, "expected the new name after 'aka', found %s", describe(P->cur));
			return;
		}
		VarDecl *v = parse_declarator(P, &ds, DCTX_TYPEDEF);
		if (!v)
			return;
		if (v->type->kind == TYPEX_REF) {
			error_at(P, aka, "'aka' can't name a reference type");
			return;
		}
		Decl *d = ast_decl(P->arena, DECL_TYPEDEF, l);
		d->as.typedef_.name = v->name;
		d->as.typedef_.type = v->type;
		push_decl(P, d);
		expect_newline(P, "the new name");
		return;
	}
	if (ds.is_register) {
		error_at(P, ds.first, "'register' only applies to local variables");
		return;
	}
	if (ds.is_auto && !check(P, TOK_IDENT)) {
		error_at(P, P->cur, "expected a name after 'auto'");
		return;
	}
	Token name = P->cur;
	if (!check(P, TOK_IDENT)) {
		if (check(P, TOK_AMP)) {
			/* `int &name ...`: a reference global, or a function returning one */
			Lexer copy = P->lex;
			copy.quiet = true;
			Token n1 = lexer_next(&copy);
			Token n2 = lexer_next(&copy);
			bool var = n1.kind != TOK_IDENT || n2.kind == TOK_EQ || n2.kind == TOK_COMMA ||
				   n2.kind == TOK_LBRACKET || n2.kind == TOK_NEWLINE || n2.kind == TOK_EOF;
			if (var)
				error_at(P, P->cur, "global variables can't be references");
			else
				error_at(P, P->cur, "a function can't return a reference: the borrow could outlive "
					 "what it points to; return a value instead");
		}
		else if (check(P, TOK_LPAREN) || check(P, TOK_STAR))
			error_at(P, P->cur, "expected a name; Cabby functions are written 'int name int a, int b {'");
		else
			error_at(P, P->cur, "expected a name, found %s", describe(P->cur));
		return;
	}
	Token after = peek(P);
	bool is_var = after.kind == TOK_EQ || after.kind == TOK_COMMA || after.kind == TOK_LBRACKET ||
		      after.kind == TOK_NEWLINE || after.kind == TOK_EOF;
	if (after.kind == TOK_NEWLINE) {
		Lexer copy = P->lex;
		copy.quiet = true;
		lexer_next(&copy);   /* the newline */
		if (lexer_next(&copy).kind == TOK_LBRACE)
			is_var = false;   /* `{` on the next line: a function body */
	}
	if (is_var && !ds.is_extern) {
		Decl *d = ast_decl(P->arena, DECL_VAR, l);
		d->is_static = ds.is_static;
		ds.is_static = false;   /* top-level static = file-local, not a storage class */
		do {
			VarDecl *v = parse_declarator(P, &ds, DCTX_GLOBAL);
			if (!v)
				return;
			vec_push(P->arena, d->as.vars, v);
		} while (match(P, TOK_COMMA));
		push_decl(P, d);
		expect_newline(P, "the declaration");
		return;
	}
	if (ds.is_auto) {
		error_at(P, ds.first, "a function needs its return type written out");
		return;
	}
	if (ds.is_const) {
		error_at(P, ds.first, "a return type can't be const");
		return;
	}
	advance(P);
	FnDecl *fn = parse_fn_rest(P, ds.base, name, NULL, ds.is_extern, ds.is_static, t.bol ? t.indent : 0);
	Decl *d = ast_decl(P->arena, DECL_FN, l);
	d->is_static = ds.is_static;
	d->as.fn = fn;
	push_decl(P, d);
}

/* Skip to the next top-level line after an error. */
static void synchronize_top(Parser *P)
{
	P->panic = false;
	int depth = 0;
	for (;;) {
		if (check(P, TOK_EOF))
			return;
		if (check(P, TOK_LBRACE))
			depth++;
		else if (check(P, TOK_RBRACE) && depth > 0)
			depth--;
		advance(P);
		if (P->prev.kind == TOK_NEWLINE && depth == 0 && P->cur.bol && P->cur.indent == 0 &&
		    !check(P, TOK_RBRACE))
			return;
		if (P->prev.kind == TOK_RBRACE && depth == 0 && check(P, TOK_NEWLINE)) {
			advance(P);
			return;
		}
	}
}

static const char *canonical_path(Arena *A, const char *path)
{
	char buf[PATH_MAX];
	if (realpath(path, buf))
		return arena_strndup(A, buf, strlen(buf));
	return path;
}

static void parse_source(Loader *ld, const char *path, char *src);

static void parse_file(Loader *ld, const char *path, SrcLoc from)
{
	const char *canon = canonical_path(ld->A, path);
	for (int i = 0; i < ld->loaded.len; i++)
		if (strcmp(ld->loaded.items[i], canon) == 0)
			return;   /* already loaded (or being loaded: a use cycle is harmless) */
	vec_push(ld->A, ld->loaded, canon);
	if (ld->depth > 64) {
		diag_report_in(DIAG_ERROR, from.file, from.line, from.col, "'use' nesting is too deep");
		return;
	}

	char *src = read_file(path, NULL);
	if (!src) {
		if (from.line)
			diag_report_in(DIAG_NOTE, from.file, from.line, from.col, "while reading this 'use'");
		diag_report_in(DIAG_ERROR, from.file, 0, 0, "can't read '%s'", path);
		return;
	}
	parse_source(ld, path, src);
}

/* Parses one file's text; `src` (malloc'd) becomes owned by the Program. */
static void parse_source(Loader *ld, const char *path, char *src)
{
	int id = diag_add_file(path, src);
	SourceFile sf = { arena_strndup(ld->A, path, strlen(path)), id, src };
	vec_push(ld->A, ld->prog->files, sf);

	prescan_types(ld, path, src);
	prescan_callables(ld, path, src);

	Parser P;
	memset(&P, 0, sizeof P);
	P.ld = ld;
	P.arena = ld->A;
	P.file = id;
	P.path = sf.path;
	diag_set_file(id);
	lexer_init(&P.lex, ld->A, path, src);
	P.lex.is_namespace = loader_is_namespace;
	P.lex.ns_ctx = ld;
	advance(&P);

	ld->depth++;
	while (!check(&P, TOK_EOF)) {
		if (match(&P, TOK_NEWLINE))
			continue;
		Token before = P.cur;
		parse_top(&P);
		diag_set_file(id);
		if (P.panic)
			synchronize_top(&P);
		if (P.cur.start == before.start && P.cur.kind == before.kind && !check(&P, TOK_EOF))
			advance(&P);
	}
	ld->depth--;
}

/* ===================================================== structs as JSON
 *
 * With `use json`, every struct gets two functions, written here as Cabby
 * source and parsed like any other file:
 *
 *     json json.from_struct_player const player &Value      (json.from P)
 *     void json.fill_struct_player const json &Json, player &Value   (J.fill P)
 *
 * Fields go to and from JSON by their names: text, numbers, bools, chars
 * (as one-letter text), enums (by their names), other structs, and lists,
 * lookups with str keys, and arrays of those. A struct with a field of any
 * other kind gets no functions; json_skipped says why, for sema's message.
 * Filling leaves a field that the JSON doesn't have (or has as null) as it
 * was, so a struct's own values are its defaults. */

typedef enum { JK_NONE, JK_STR, JK_BOOL, JK_CHAR, JK_INT, JK_FLOAT, JK_ENUM, JK_STRUCT, JK_JSON } JsonKind;

typedef struct {
	JsonKind k;
	const char *type;      /* the type as written (for declarations and casts) */
	const char *target;    /* JK_STRUCT and JK_ENUM: the struct's or enum's own name */
	const char *lo, *hi;   /* JK_INT: the range a value must be in ("" for long) */
} JsonType;

typedef struct {
	char *s;
	size_t len, cap;
} Text;

typedef VEC(const char *) NameVec;

static const char *arena_sprintf(Arena *A, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	return arena_strndup(A, buf, strlen(buf));
}

static void tput(Text *t, const char *fmt, ...)
{
	va_list ap;
	for (;;) {
		va_start(ap, fmt);
		int n = vsnprintf(t->s ? t->s + t->len : NULL, t->s ? t->cap - t->len : 0, fmt, ap);
		va_end(ap);
		if (t->s && t->len + (size_t)n < t->cap) {
			t->len += (size_t)n;
			return;
		}
		t->cap = (t->cap + (size_t)n + 1) * 2;
		t->s = realloc(t->s, t->cap);
		if (!t->s) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
	}
}

static Decl *json_find_decl(Program *p, const char *name)
{
	for (int i = 0; i < p->decls.len; i++) {
		Decl *d = p->decls.items[i];
		const char *n = d->kind == DECL_OBJECT ? d->as.object->name : d->kind == DECL_ENUM ? d->as.enum_->name
				: d->kind == DECL_TYPEDEF ? d->as.typedef_.name : NULL;
		if (n && strcmp(n, name) == 0)
			return d;
	}
	return NULL;
}

/* a type name as part of a function's name: gui.point -> gui_point */
static const char *json_suffix(Arena *A, const char *name)
{
	char *s = arena_strndup(A, name, strlen(name));
	for (char *p = s; *p; p++)
		if (*p == '.')
			*p = '_';
	return s;
}

/* A field's name in JSON. Cabby writes fields with a capital at the start
 * of each word (DateOfBirth); JSON mostly uses small letters with `_`
 * between words (date_of_birth), so that's what's written. Reading also
 * takes the other usual spellings: dateOfBirth, and DateOfBirth itself. */
static const char *json_snake(Arena *A, const char *name)
{
	char buf[256];
	size_t n = 0;
	for (size_t i = 0; name[i] && n + 3 < sizeof buf; i++) {
		char c = name[i];
		bool up = c >= 'A' && c <= 'Z';
		if (up && i > 0 && name[i - 1] != '_') {
			char p = name[i - 1], nx = name[i + 1];
			bool p_low = (p >= 'a' && p <= 'z') || (p >= '0' && p <= '9');
			bool p_up = p >= 'A' && p <= 'Z';
			if (p_low || (p_up && nx >= 'a' && nx <= 'z'))   /* aB, or the B in ABc (HTMLPage) */
				buf[n++] = '_';
		}
		buf[n++] = up ? (char)(c - 'A' + 'a') : c;
	}
	buf[n] = '\0';
	return arena_strndup(A, buf, n);
}

static const char *json_camel(Arena *A, const char *snake)
{
	char buf[256];
	size_t n = 0;
	for (size_t i = 0; snake[i] && n + 2 < sizeof buf; i++) {
		if (snake[i] == '_' && snake[i + 1] && i > 0) {
			char c = snake[++i];
			buf[n++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
		} else {
			buf[n++] = snake[i];
		}
	}
	buf[n] = '\0';
	return arena_strndup(A, buf, n);
}

/* What a field's (or an item's) type is, for JSON; JK_NONE with *why set
 * if it can't be one. */
static JsonType json_type_of(Program *p, const TypeExpr *te, const char *written, const char **why)
{
	JsonType j = { JK_NONE, written, NULL, "", "" };
	if (!te || te->kind != TYPEX_NAMED) {
		*why = "a list, lookup or array inside another";
		return j;
	}
	const char *n = te->name;
	if (te->builtin) {
		static const struct { const char *name; JsonKind k; const char *lo, *hi; } b[] = {
			{ "str", JK_STR, "", "" }, { "bool", JK_BOOL, "", "" }, { "char", JK_CHAR, "", "" },
			{ "signed char", JK_INT, "-128", "127" }, { "unsigned char", JK_INT, "0", "255" },
			{ "short", JK_INT, "-32768", "32767" }, { "unsigned short", JK_INT, "0", "65535" },
			{ "int", JK_INT, "-2147483648", "2147483647" }, { "unsigned int", JK_INT, "0", "4294967295" },
			{ "long", JK_INT, "", "" }, { "unsigned long", JK_INT, "0", "9223372036854775807" },
			{ "float", JK_FLOAT, "", "" }, { "double", JK_FLOAT, "", "" },
		};
		for (size_t i = 0; i < sizeof b / sizeof *b; i++)
			if (strcmp(b[i].name, n) == 0) {
				j.k = b[i].k;
				j.lo = b[i].lo;
				j.hi = b[i].hi;
				return j;
			}
		*why = n;
		return j;
	}
	if (strcmp(n, "json") == 0) {
		j.k = JK_JSON;   /* any JSON at all, kept as it is */
		return j;
	}
	Decl *d = json_find_decl(p, n);
	if (!d) {
		*why = n;
		return j;
	}
	if (d->kind == DECL_ENUM) {
		if (d->as.enum_->items.len == 0) {
			*why = n;
			return j;
		}
		j.k = JK_ENUM;
		j.target = n;
		return j;
	}
	if (d->kind == DECL_TYPEDEF) {
		/* an aka or a kind: what it is underneath, written by its own name */
		JsonType u = json_type_of(p, d->as.typedef_.type, written, why);
		if (u.k == JK_NONE)
			return u;
		if (d->as.typedef_.is_kind && u.k != JK_INT && u.k != JK_FLOAT) {
			*why = n;
			return j;
		}
		return u;
	}
	if (d->kind == DECL_OBJECT && d->as.object->kind == OBJ_STRUCT) {
		j.k = JK_STRUCT;
		j.target = n;
		return j;
	}
	*why = n;   /* an object or a union */
	return j;
}

/* the json value of `src` (a field or item of type j), as an expression */
static void json_write_value(Text *t, Arena *A, JsonType j, const char *src)
{
	switch (j.k) {
	case JK_STR:    tput(t, "%s.copy", src); break;
	case JK_BOOL:   tput(t, "%s", src); break;
	case JK_CHAR:   tput(t, "%s.text", src); break;
	case JK_INT:    tput(t, "(long)%s", src); break;
	case JK_FLOAT:  tput(t, "(double)%s", src); break;
	case JK_ENUM:   tput(t, "json.enum_name_%s(%s)", json_suffix(A, j.target), src); break;
	case JK_STRUCT: tput(t, "json.from_struct_%s(%s)", json_suffix(A, j.target), src); break;
	case JK_JSON:   tput(t, "%s.copy", src); break;
	default: break;
	}
}

/* statements setting `dst` (of type j) from the json `src` */
static void json_read_value(Text *t, Arena *A, JsonType j, const char *src, const char *dst, const char *ind)
{
	switch (j.k) {
	case JK_STR:    tput(t, "%s%s = %s.str\n", ind, dst, src); break;
	case JK_BOOL:   tput(t, "%s%s = %s.bool\n", ind, dst, src); break;
	case JK_CHAR:   tput(t, "%s%s = json.char_of(%s)\n", ind, dst, src); break;
	case JK_FLOAT:  tput(t, "%s%s = (%s)%s.double\n", ind, dst, j.type, src); break;
	case JK_INT:
		if (*j.lo)
			tput(t, "%s%s = (%s)json.whole_in(%s, %s, %s)\n", ind, dst, j.type, src, j.lo, j.hi);
		else
			tput(t, "%s%s = (%s)%s.long\n", ind, dst, j.type, src);
		break;
	case JK_ENUM:   tput(t, "%s%s = json.enum_from_%s(%s)\n", ind, dst, json_suffix(A, j.target), src); break;
	case JK_STRUCT: tput(t, "%sjson.fill_struct_%s %s, %s\n", ind, json_suffix(A, j.target), src, dst); break;
	case JK_JSON:   tput(t, "%s%s = %s.copy\n", ind, dst, src); break;
	default: break;
	}
}

/* the type as written: "list point", "int" */
static const char *json_written(Arena *A, const TypeExpr *te)
{
	if (!te)
		return "?";
	if (te->kind == TYPEX_NAMED)
		return te->name;
	return "?";
}

/* Writes both functions for one struct, or says why it can't */
static const char *json_struct(Program *p, Arena *A, ObjectDecl *od, Text *t, NameVec *enums)
{
	const char *why = NULL;
	/* first, check every field */
	for (int i = 0; i < od->fields.len; i++) {
		TypeExpr *te = od->fields.items[i]->type;
		const TypeExpr *item = te;
		if (te && (te->kind == TYPEX_LIST || te->kind == TYPEX_ARRAY))
			item = te->inner;
		else if (te && te->kind == TYPEX_LOOKUP) {
			if (!te->key || te->key->kind != TYPEX_NAMED || strcmp(te->key->name, "str") != 0)
				return arena_sprintf(A, "its field '%s' is a lookup whose keys aren't str (JSON's names are text)",
						     od->fields.items[i]->name);
			item = te->inner;
		}
		if (!te || (te->kind != TYPEX_NAMED && te->kind != TYPEX_LIST && te->kind != TYPEX_ARRAY &&
			    te->kind != TYPEX_LOOKUP))
			return arena_sprintf(A, "its field '%s' isn't data JSON can hold", od->fields.items[i]->name);
		JsonType j = json_type_of(p, item, json_written(A, item), &why);
		if (j.k == JK_NONE)
			return arena_sprintf(A, "its field '%s' is %s'%s', which JSON can't hold", od->fields.items[i]->name,
					     item == te ? "" : "made of ", why);
	}
	const char *sfx = json_suffix(A, od->name);

	/* ---- to JSON */
	tput(t, "json json.from_struct_%s const %s &Value {\n\tjson Json = json.lookup\n", sfx, od->name);
	for (int i = 0; i < od->fields.len; i++) {
		VarDecl *f = od->fields.items[i];
		TypeExpr *te = f->type;
		const TypeExpr *item = te->kind == TYPEX_NAMED ? te : te->inner;
		JsonType j = json_type_of(p, item, json_written(A, item), &why);
		if (j.k == JK_ENUM) {
			bool seen = false;
			for (int k = 0; k < enums->len; k++)
				seen |= strcmp(enums->items[k], j.target) == 0;
			if (!seen)
				vec_push(A, *enums, j.target);
		}
		char src[256];
		if (te->kind == TYPEX_NAMED) {
			snprintf(src, sizeof src, "Value.%s", f->name);
			tput(t, "\tJson[\"%s\"] = ", json_snake(A, f->name));
			json_write_value(t, A, j, src);
			tput(t, "\n");
		} else if (te->kind == TYPEX_LIST) {
			tput(t, "\tjson Part%d = json.list\n\tfor Each in Value.%s {\n\t\tPart%d.add ", i, f->name, i);
			json_write_value(t, A, j, "Each");
			tput(t, "\n\t}\n\tJson[\"%s\"] = Part%d\n", json_snake(A, f->name), i);
		} else if (te->kind == TYPEX_ARRAY) {
			tput(t, "\tjson Part%d = json.list\n\tfor i in Value.%s.length {\n\t\tPart%d.add ", i, f->name, i);
			snprintf(src, sizeof src, "Value.%s[i]", f->name);
			json_write_value(t, A, j, src);
			tput(t, "\n\t}\n\tJson[\"%s\"] = Part%d\n", json_snake(A, f->name), i);
		} else {
			tput(t, "\tjson Part%d = json.lookup\n\tfor Key, Each in Value.%s {\n\t\tPart%d[Key] = ", i, f->name, i);
			json_write_value(t, A, j, "Each");
			tput(t, "\n\t}\n\tJson[\"%s\"] = Part%d\n", json_snake(A, f->name), i);
		}
	}
	tput(t, "\treturn Json\n}\n\n");

	/* ---- from JSON */
	tput(t, "void json.fill_struct_%s const json &Json, %s &Value {\n\tjson.must_be_lookup Json, \"%s\"\n", sfx,
	     od->name, od->name);
	for (int i = 0; i < od->fields.len; i++) {
		VarDecl *f = od->fields.items[i];
		TypeExpr *te = f->type;
		const TypeExpr *item = te->kind == TYPEX_NAMED ? te : te->inner;
		JsonType j = json_type_of(p, item, json_written(A, item), &why);
		char dst[256], src[256];
		const char *snake = json_snake(A, f->name);
		tput(t, "\tstr Name%d = json.given_name(Json, \"%s\", \"%s\", \"%s\")\n\tif Name%d.length > 0 {\n", i, snake,
		     json_camel(A, snake), f->name, i);
		snprintf(src, sizeof src, "Json[Name%d]", i);
		if (te->kind == TYPEX_NAMED) {
			snprintf(dst, sizeof dst, "Value.%s", f->name);
			json_read_value(t, A, j, src, dst, "\t\t");
		} else if (te->kind == TYPEX_LIST) {
			tput(t, "\t\tlist %s Made\n\t\tfor Each in %s {\n\t\t\t%s Item\n", j.type, src, j.type);
			json_read_value(t, A, j, "Each", "Item", "\t\t\t");
			tput(t, "\t\t\tMade.add Item\n\t\t}\n\t\tValue.%s = Made\n", f->name);
		} else if (te->kind == TYPEX_ARRAY) {
			tput(t, "\t\tjson Part = %s\n\t\tfor i in Value.%s.length {\n\t\t\tif i < Part.length {\n", src, f->name);
			snprintf(dst, sizeof dst, "Value.%s[i]", f->name);
			json_read_value(t, A, j, "Part[i]", dst, "\t\t\t\t");
			tput(t, "\t\t\t}\n\t\t}\n");
		} else {
			tput(t, "\t\tlookup str %s Made\n\t\tjson Part = %s\n\t\tfor Key in Part.keys {\n\t\t\t%s Item\n",
			     j.type, src, j.type);
			json_read_value(t, A, j, "Part[Key]", "Item", "\t\t\t");
			tput(t, "\t\t\tMade[Key] = Item\n\t\t}\n\t\tValue.%s = Made\n", f->name);
		}
		tput(t, "\t}\n");
	}
	tput(t, "}\n\n");
	return NULL;
}

static void json_enum(Program *p, Arena *A, const char *name, Text *t)
{
	EnumDecl *e = json_find_decl(p, name)->as.enum_;
	const char *sfx = json_suffix(A, name);
	tput(t, "str json.enum_name_%s %s Value {\n", sfx, name);
	for (int i = 0; i < e->items.len; i++)
		tput(t, "\tif Value == %s {\n\t\treturn \"%s\"\n\t}\n", e->items.items[i].name, e->items.items[i].name);
	tput(t, "\tlong Number = (long)Value\n\treturn Number.text\n}\n\n");
	tput(t, "%s json.enum_from_%s const json &Json {\n\tstr Given = Json.str\n", name, sfx);
	Text names = { 0 };
	for (int i = 0; i < e->items.len; i++) {
		tput(t, "\tif Given == \"%s\" {\n\t\treturn %s\n\t}\n", e->items.items[i].name, e->items.items[i].name);
		tput(&names, "%s%s", i == 0 ? "" : i == e->items.len - 1 ? " or " : ", ", e->items.items[i].name);
	}
	tput(t, "\tjson.not_one_of Json, \"a %s (%s)\"\n\treturn %s\n}\n\n", name, names.s, e->items.items[0].name);
	free(names.s);
}

static void add_json_for_structs(Loader *ld)
{
	Program *p = ld->prog;
	Decl *j = json_find_decl(p, "json");
	if (!j || j->kind != DECL_OBJECT || j->as.object->kind != OBJ_OBJECT)
		return;   /* no `use json` */
	Text t = { 0 };
	tput(&t, "// json.from and .fill for each struct: made by Cabby\n\n");
	NameVec enums = { 0 };
	int n = p->decls.len;
	for (int i = 0; i < n; i++) {
		Decl *d = p->decls.items[i];
		if (d->kind != DECL_OBJECT || d->as.object->kind != OBJ_STRUCT)
			continue;
		const char *why = json_struct(p, ld->A, d->as.object, &t, &enums);
		if (why) {
			vec_push(ld->A, p->json_skipped, d->as.object->name);
			vec_push(ld->A, p->json_skipped, why);
		}
	}
	for (int i = 0; i < enums.len; i++)
		json_enum(p, ld->A, enums.items[i], &t);
	/* named .hby, so a panic inside is reported where the program called it */
	parse_source(ld, "(json for structs).hby", t.s);
}

Program *parse_program(Arena *arena, const char *path, const char *const *lib_dirs)
{
	Loader ld;
	memset(&ld, 0, sizeof ld);
	ld.A = arena;
	ld.lib_dirs = lib_dirs;
	ld.prog = arena_alloc(arena, sizeof *ld.prog);
	add_type_name(&ld, "handler", 7);   /* built in: a window's method (parameters only) */
	ld.prog->main_file = path;
	parse_file(&ld, path, (SrcLoc){ -1, 0, 0 });
	add_json_for_structs(&ld);
	return ld.prog;
}

Program *parse_program_source(Arena *arena, const char *name, char *src, const char *const *lib_dirs)
{
	Loader ld;
	memset(&ld, 0, sizeof ld);
	ld.A = arena;
	ld.lib_dirs = lib_dirs;
	ld.prog = arena_alloc(arena, sizeof *ld.prog);
	add_type_name(&ld, "handler", 7);   /* built in: a window's method (parameters only) */
	ld.prog->main_file = name;
	vec_push(ld.A, ld.loaded, name);
	parse_source(&ld, name, src);
	add_json_for_structs(&ld);
	return ld.prog;
}
