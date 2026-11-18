#include "lexer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"

/* ---------------------------------------------------------------- tables */

static const char *const kind_names[] = {
#define TK_X(name, str) str,
	TOKEN_KINDS(TK_X, TK_X, TK_X)
#undef TK_X
};

static const struct {
	const char *spelling;
	TokenKind kind;
} keywords[] = {
#define TK_T(name, str)
#define TK_K(name, str) { str, TOK_KW_##name },
#define TK_O(name, str)
	TOKEN_KINDS(TK_T, TK_K, TK_O)
#undef TK_T
#undef TK_K
#undef TK_O
};

const char *token_kind_name(TokenKind kind)
{
	return (kind >= 0 && kind < TOK__COUNT) ? kind_names[kind] : "?";
}

bool token_is_keyword(TokenKind kind)
{
	return kind >= TOK_KW_AUTO && kind <= TOK_KW_LOOKUP;   /* the last keyword in lexer.h */
}

static TokenKind keyword_or_ident(const char *s, int len)
{
	for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
		if ((int)strlen(keywords[i].spelling) == len && memcmp(keywords[i].spelling, s, (size_t)len) == 0)
			return keywords[i].kind;
	return TOK_IDENT;
}

/* --------------------------------------------------------------- helpers */

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_ident_start(char c) { return is_alpha(c) || c == '_'; }
static bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

static int hex_val(char c)
{
	if (is_digit(c)) return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int col_of(const Lexer *L, const char *p) { return (int)(p - L->line_start) + 1; }

static void lex_error(Lexer *L, const char *at, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void lex_error(Lexer *L, const char *at, const char *fmt, ...)
{
	if (L->quiet)
		return;
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	diag_error(L->line, col_of(L, at), "%s", buf);
}

static Token make(const Lexer *L, TokenKind kind, const char *start, const char *end)
{
	Token t;
	memset(&t, 0, sizeof t);
	t.kind = kind;
	t.start = start;
	t.length = (int)(end - start);
	t.line = L->line;
	t.col = col_of(L, start);
	t.indent = L->line_indent;
	t.space_col = L->line_space_col;
	return t;
}

/* --------------------------------------------------------------- lexer */

void lexer_init(Lexer *L, Arena *arena, const char *filename, const char *src)
{
	memset(L, 0, sizeof *L);
	L->filename = filename;
	L->src = L->cur = L->line_start = src;
	/* #!/usr/bin/env cabby on the first line, so ./prog.cby runs it: skipped */
	if (src && src[0] == '#' && src[1] == '!')
		while (*L->cur && *L->cur != '\n')
			L->cur++;
	L->line = 1;
	L->at_line_start = true;
	L->last_kind = TOK_NEWLINE; /* suppresses a leading/empty-file NEWLINE */
	L->arena = arena;
	if ((unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
		L->cur = L->line_start = src + 3;   /* UTF-8 BOM */
}

static void newline_at(Lexer *L, const char *nl)
{
	L->line++;
	L->line_start = nl + 1;
}

/* Skips spaces, tabs, and comments. Newlines are skipped too when
 * `cross_lines` (inside brackets); otherwise the caller sees the '\n'. */
static void skip_trivia(Lexer *L, bool cross_lines)
{
	for (;;) {
		char c = *L->cur;
		if (c == ' ' || c == '\t' || c == '\r') {
			L->cur++;
		} else if (c == '/' && L->cur[1] == '/') {
			while (*L->cur && *L->cur != '\n')
				L->cur++;
		} else if (c == '/' && L->cur[1] == '*') {
			const char *start = L->cur;
			int start_line = L->line;
			const char *start_ls = L->line_start;
			L->cur += 2;
			while (*L->cur && !(L->cur[0] == '*' && L->cur[1] == '/')) {
				if (*L->cur == '\n')
					newline_at(L, L->cur);
				L->cur++;
			}
			if (!*L->cur) {
				L->open_comment = true;
				int save_line = L->line;
				const char *save_ls = L->line_start;
				L->line = start_line;
				L->line_start = start_ls;
				lex_error(L, start, "unterminated /* comment");
				L->line = save_line;
				L->line_start = save_ls;
				return;
			}
			L->cur += 2;
		} else if (c == '\n' && cross_lines) {
			newline_at(L, L->cur);
			L->cur++;
		} else {
			return;
		}
	}
}

/* At the start of a physical line: measure its indentation, skipping
 * blank and comment-only lines entirely. */
static void begin_line(Lexer *L)
{
	for (;;) {
		const char *p = L->cur;
		int tabs = 0;
		while (*p == '\t') {
			tabs++;
			p++;
		}
		const char *q = p;
		const char *space = NULL;
		while (*q == ' ' || *q == '\t' || *q == '\r') {
			if (*q == ' ' && !space)
				space = q;
			q++;
		}
		if (*q == '\n') {                        /* blank line */
			newline_at(L, q);
			L->cur = q + 1;
			continue;
		}
		if (q[0] == '/' && q[1] == '/') {        /* comment-only line */
			while (*q && *q != '\n')
				q++;
			if (*q == '\n') {
				newline_at(L, q);
				L->cur = q + 1;
				continue;
			}
			L->cur = q;
			return;
		}
		/* Block comments are skipped by the token loop; a line holding
		 * only a comment produces no tokens and is re-measured there. */
		L->line_indent = tabs;
		L->line_space_col = space ? col_of(L, space) : 0;
		L->cur = q;
		return;
	}
}

static long read_escape(Lexer *L, const char **pp)
{
	const char *p = *pp;
	char c = *p++;
	long v;
	switch (c) {
	case 'n':  v = '\n'; break;
	case 't':  v = '\t'; break;
	case 'r':  v = '\r'; break;
	case '0':  v = '\0'; break;
	case 'a':  v = '\a'; break;
	case 'b':  v = '\b'; break;
	case 'f':  v = '\f'; break;
	case 'v':  v = '\v'; break;
	case 'e':  v = 0x1B; break;
	case '\\': v = '\\'; break;
	case '\'': v = '\''; break;
	case '"':  v = '"';  break;
	case 'x': {
		int hi = hex_val(p[0]);
		int lo = hi >= 0 ? hex_val(p[1]) : -1;
		if (lo < 0) {
			lex_error(L, p - 2, "\\x escape needs exactly two hex digits");
			*pp = p;
			return -1;
		}
		v = hi * 16 + lo;
		p += 2;
		break;
	}
	default:
		lex_error(L, p - 2, "unknown escape sequence '\\%c'", c ? c : '0');
		*pp = c ? p : p - 1;
		return -1;
	}
	*pp = p;
	return v;
}

static Token lex_number(Lexer *L, const char *start)
{
	const char *p = start;
	int base = 10;
	bool is_float = false, overflow = false, bad_digit = false, any_digit = false;
	uint64_t val = 0;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
	else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
	else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) { base = 8; p += 2; }

	const char *digits = p;
	if (base == 10) {
		while (is_digit(*p) || *p == '_')
			p++;
		if (*p == '.' && is_digit(p[1])) {
			is_float = true;
			p++;
			while (is_digit(*p) || *p == '_')
				p++;
		} else if (*p == '.' && !is_ident_start(p[1]) && p[1] != '.') {
			is_float = true;   /* `1.` */
			p++;
		}
		if (*p == 'e' || *p == 'E') {
			const char *q = p + 1;
			if (*q == '+' || *q == '-')
				q++;
			if (is_digit(*q)) {
				is_float = true;
				p = q;
				while (is_digit(*p))
					p++;
			}
		}
	} else {
		while (hex_val(*p) >= 0 || *p == '_')
			p++;
	}

	Token t;
	if (is_float) {
		char buf[128];
		size_t n = 0;
		for (const char *q = start; q < p && n < sizeof buf - 1; q++)
			if (*q != '_')
				buf[n++] = *q;
		buf[n] = '\0';
		bool f32 = false;
		if (*p == 'f' || *p == 'F') {
			f32 = true;
			p++;
		}
		if (is_ident_char(*p)) {
			const char *bad = p;
			while (is_ident_char(*p))
				p++;
			L->cur = p;
			lex_error(L, bad, "invalid suffix on floating-point literal");
			return make(L, TOK_ERROR, start, p);
		}
		L->cur = p;
		t = make(L, TOK_FLOAT, start, p);
		t.v.f = strtod(buf, NULL);
		t.lit_float32 = f32;
		return t;
	}

	for (const char *q = digits; q < p; q++) {
		if (*q == '_')
			continue;
		int d = hex_val(*q);
		if (d < 0 || d >= base) {
			bad_digit = true;
			continue;
		}
		any_digit = true;
		if (val > (UINT64_MAX - (uint64_t)d) / (uint64_t)base)
			overflow = true;
		else
			val = val * (uint64_t)base + (uint64_t)d;
	}

	/* integer suffix: u, l, ll in either order */
	bool uns = false;
	int longs = 0;
	const char *suf = p;
	for (int k = 0; k < 3; k++) {
		if ((*p == 'u' || *p == 'U') && !uns) {
			uns = true;
			p++;
		} else if ((*p == 'l' || *p == 'L') && longs == 0) {
			longs = 1;
			p++;
			if (*p == p[-1]) {
				longs = 2;
				p++;
			}
		}
	}
	if (is_ident_char(*p)) {
		const char *bad = (p > suf) ? suf : p;
		while (is_ident_char(*p))
			p++;
		L->cur = p;
		lex_error(L, bad, "invalid character '%c' in numeric literal", *bad);
		return make(L, TOK_ERROR, start, p);
	}
	L->cur = p;

	if (bad_digit || !any_digit) {
		lex_error(L, start, "malformed base-%d integer literal", base);
		return make(L, TOK_ERROR, start, p);
	}
	if (base == 10 && start[0] == '0' && is_digit(start[1])) {
		lex_error(L, start, "integer literals cannot start with 0; for octal write 0o%.*s",
			  (int)(suf - start - 1), start + 1);
		return make(L, TOK_ERROR, start, p);
	}
	if (overflow) {
		lex_error(L, start, "integer literal does not fit in 64 bits");
		return make(L, TOK_ERROR, start, p);
	}
	t = make(L, TOK_INT, start, p);
	t.v.u = val;
	t.lit_unsigned = uns;
	t.lit_long = (uint8_t)longs;
	t.lit_based = base != 10;
	return t;
}

static Token lex_string(Lexer *L, const char *start)
{
	const char *p = start + 1;
	const char *end = p;
	while (*end && *end != '"' && *end != '\n') {
		if (*end == '\\' && end[1] && end[1] != '\n')
			end++;
		end++;
	}
	if (*end != '"') {
		lex_error(L, start, "unterminated string literal");
		L->cur = end;
		return make(L, TOK_ERROR, start, end);
	}

	char *buf = arena_alloc(L->arena, (size_t)(end - p) + 1);
	size_t n = 0;
	bool ok = true;
	while (p < end) {
		if (*p == '\\') {
			p++;
			long v = read_escape(L, &p);
			if (v < 0)
				ok = false;
			else
				buf[n++] = (char)v;
		} else {
			buf[n++] = *p++;
		}
	}
	buf[n] = '\0';
	L->cur = end + 1;

	Token t = make(L, ok ? TOK_STRING : TOK_ERROR, start, L->cur);
	t.v.str.data = buf;
	t.v.str.len = n;
	return t;
}

static Token lex_char(Lexer *L, const char *start)
{
	const char *p = start + 1;
	long v;
	if (*p == '\'') {
		lex_error(L, start, "empty char literal");
		L->cur = p + 1;
		return make(L, TOK_ERROR, start, L->cur);
	}
	if (*p == '\n' || *p == '\0') {
		lex_error(L, start, "unterminated char literal");
		L->cur = p;
		return make(L, TOK_ERROR, start, p);
	}
	if (*p == '\\') {
		p++;
		v = read_escape(L, &p);
	} else {
		v = (unsigned char)*p++;
	}
	if (*p != '\'') {
		bool multibyte = (unsigned char)start[1] >= 0x80;
		while (*p && *p != '\'' && *p != '\n')
			p++;
		if (*p == '\'')
			p++;
		L->cur = p;
		if (multibyte)
			lex_error(L, start, "%.*s is more than one byte; a char holds one byte, so use a string (\"...\")",
				  (int)(p - start), start);
		else
			lex_error(L, start, "char literal must contain exactly one character");
		return make(L, TOK_ERROR, start, p);
	}
	L->cur = p + 1;
	Token t = make(L, v < 0 ? TOK_ERROR : TOK_CHAR, start, L->cur);
	t.v.ch = v < 0 ? 0 : (uint8_t)v;
	return t;
}

/* `use [local] name` at the start of a line: `start` points at "use". */
static Token lex_use(Lexer *L, const char *start)
{
	const char *p = start + 3;
	while (*p == ' ' || *p == '\t')
		p++;
	bool local = false;
	if (strncmp(p, "local", 5) == 0 && (p[5] == ' ' || p[5] == '\t')) {
		local = true;
		p += 5;
		while (*p == ' ' || *p == '\t')
			p++;
	}
	const char *path = p;
	while (*p && *p != '\n' && *p != ' ' && *p != '\t' && *p != '\r' && !(p[0] == '/' && p[1] == '/'))
		p++;
	const char *pe = p;
	/* the rest of the line may only be a comment */
	const char *q = p;
	while (*q == ' ' || *q == '\t' || *q == '\r')
		q++;
	bool junk = *q && *q != '\n' && !(q[0] == '/' && (q[1] == '/' || q[1] == '*'));
	while (*p && *p != '\n' && !(p[0] == '/' && p[1] == '*'))
		p++;
	if (*p == '/')
		p = q;   /* a block comment: let the normal lexer skip it */
	L->cur = p;
	if (pe <= path) {
		lex_error(L, start, "'use' needs a header name, e.g. 'use standard' or 'use local util'");
		return make(L, TOK_ERROR, start, p);
	}
	size_t n = (size_t)(pe - path);
	if (n > 4 && memcmp(pe - 4, ".hby", 4) == 0) {
		lex_error(L, path, "leave out '.hby': write 'use %s%.*s'", local ? "local " : "", (int)(n - 4), path);
		return make(L, TOK_ERROR, start, p);
	}
	if (junk) {
		lex_error(L, q, "a 'use' line names one header; put each on its own line");
		return make(L, TOK_ERROR, start, p);
	}
	Token t = make(L, TOK_USE, start, pe);
	char *full = arena_alloc(L->arena, n + 5);
	memcpy(full, path, n);
	memcpy(full + n, ".hby", 5);
	t.v.str.data = full;
	t.v.str.len = n + 4;
	t.use_local = local;
	return t;
}

static Token lex_raw(Lexer *L)
{
	bool bol = false;
	if (L->at_line_start && L->nesting == 0) {
		begin_line(L);
		L->at_line_start = false;
		bol = true;
	}

	for (;;) {
		skip_trivia(L, L->nesting > 0);
		if (*L->cur == '\n') {
			const char *nl = L->cur;
			Token t = make(L, TOK_NEWLINE, nl, nl + 1);
			newline_at(L, nl);
			L->cur++;
			if (L->nesting > 0)
				continue;
			L->at_line_start = true;
			if (L->last_kind == TOK_NEWLINE) {   /* line had only a trailing comment */
				begin_line(L);
				L->at_line_start = false;
				bol = true;
				continue;
			}
			return t;
		}
		break;
	}

	const char *s = L->cur;
	char c = *s;
	Token t;

	if (c == '\0') {
		if (L->last_kind != TOK_NEWLINE)
			return make(L, TOK_NEWLINE, s, s);
		t = make(L, TOK_EOF, s, s);
		t.bol = true;
		t.indent = 0;
		t.space_col = 0;
		return t;
	}

	if (is_ident_start(c)) {
		const char *p = s;
		while (is_ident_char(*p))
			p++;
		/* a namespaced name: gui.window, as one identifier */
		/* (not after a dot: in X.month.text, month is a field or method name) */
		while (L->is_namespace && p[0] == '.' && is_ident_start(p[1]) && !(s > L->src && s[-1] == '.') &&
		       L->is_namespace(L->ns_ctx, s, (int)(p - s))) {
			p++;
			while (is_ident_char(*p))
				p++;
		}
		L->cur = p;
		TokenKind k = memchr(s, '.', (size_t)(p - s)) ? TOK_IDENT : keyword_or_ident(s, (int)(p - s));
		if (p - s == 6 && memcmp(s, "cement", 6) == 0)
			lex_error(L, s, "'cement' is reserved for build-time settings (coming soon); choose another name");
		if ((p - s == 8 && memcmp(s, "extended", 8) == 0) || (p - s == 9 && memcmp(s, "quadruple", 9) == 0))
			lex_error(L, s, "'%.*s' is reserved for a type to come (%s); choose another name", (int)(p - s), s,
				  p - s == 8 ? "a whole number larger than long" : "a floating-point number larger than double");
		if (k == TOK_KW_KWUSE && bol)
			t = lex_use(L, s);
		else
			t = make(L, k, s, p);
	} else if (is_digit(c) || (c == '.' && is_digit(s[1]))) {
		t = lex_number(L, s);
	} else if (c == '"') {
		t = lex_string(L, s);
	} else if (c == '\'') {
		t = lex_char(L, s);
	} else {
		const char *p = s + 1;
		TokenKind k;
#define NEXT_IS(ch) (*p == (ch) ? (p++, 1) : 0)
		switch (c) {
		case '(': k = TOK_LPAREN; L->nesting++; break;
		case '[': k = TOK_LBRACKET; L->nesting++; break;
		case ')': k = TOK_RPAREN; if (L->nesting > 0) L->nesting--; break;
		case ']': k = TOK_RBRACKET; if (L->nesting > 0) L->nesting--; break;
		case '{': k = TOK_LBRACE; break;
		case '}': k = TOK_RBRACE; break;
		case ',': k = TOK_COMMA; break;
		case ':': k = TOK_COLON; break;
		case ';': k = TOK_SEMI; break;
		case '~': k = TOK_TILDE; break;
		case '.':
			if (p[0] == '.' && p[1] == '.') {
				p += 2;
				k = TOK_ELLIPSIS;
			} else {
				k = TOK_DOT;
			}
			break;
		case '+':
			if (NEXT_IS('+')) k = TOK_PLUS_PLUS;
			else k = NEXT_IS('=') ? TOK_PLUS_EQ : TOK_PLUS;
			break;
		case '-':
			if (NEXT_IS('-')) k = TOK_MINUS_MINUS;
			else if (*p == '>') {
				p++;
				L->cur = p;
				lex_error(L, s, "there is no '->' in Cabby; use '.' (borrows are followed automatically)");
				return make(L, TOK_ERROR, s, p);
			}
			else k = NEXT_IS('=') ? TOK_MINUS_EQ : TOK_MINUS;
			break;
		case '*': k = NEXT_IS('=') ? TOK_STAR_EQ : TOK_STAR; break;
		case '/': k = NEXT_IS('=') ? TOK_SLASH_EQ : TOK_SLASH; break;
		case '%': k = NEXT_IS('=') ? TOK_PERCENT_EQ : TOK_PERCENT; break;
		case '^': k = NEXT_IS('=') ? TOK_CARET_EQ : TOK_CARET; break;
		case '&':
			if (NEXT_IS('&')) k = TOK_AND_AND;
			else k = NEXT_IS('=') ? TOK_AMP_EQ : TOK_AMP;
			break;
		case '|':
			if (NEXT_IS('|')) k = TOK_OR_OR;
			else k = NEXT_IS('=') ? TOK_PIPE_EQ : TOK_PIPE;
			break;
		case '!': k = NEXT_IS('=') ? TOK_BANG_EQ : TOK_BANG; break;
		case '=': k = NEXT_IS('=') ? TOK_EQ_EQ : TOK_EQ; break;
		case '<':
			if (NEXT_IS('<')) k = NEXT_IS('=') ? TOK_SHL_EQ : TOK_SHL;
			else k = NEXT_IS('=') ? TOK_LE : TOK_LT;
			break;
		case '>':
			if (NEXT_IS('>')) k = NEXT_IS('=') ? TOK_SHR_EQ : TOK_SHR;
			else k = NEXT_IS('=') ? TOK_GE : TOK_GT;
			break;
		default: {
			const unsigned char *q = (const unsigned char *)s;
			int n = 1;
			if (q[0] >= 0xC0) {
				while ((q[n] & 0xC0) == 0x80 && n < 4)
					n++;
			}
			L->cur = s + n;
			if (c == '#' && strncmp(s, "#use", 4) == 0) {
				const char *e = s;
				while (*e && *e != '\n')
					e++;
				L->cur = e;   /* the whole line: one error is enough */
				lex_error(L, s, "write 'use name' without the '#' (and without '.hby')");
			}
			else if (c == '#')
				lex_error(L, s, "Cabby has no '#' directives; comments are // and /* */");
			else if (c == '?')
				lex_error(L, s, "there is no '?:' operator in Cabby; use if/else");
			else if (c == '`' || c == '@' || c == '$' || c == '\\')
				lex_error(L, s, "unexpected character '%c'", c);
			else if ((unsigned char)c >= 0x80)
				lex_error(L, s, "unexpected character '%.*s'", n, s);
			else
				lex_error(L, s, "unexpected character (byte 0x%02X)", (unsigned char)c);
			return make(L, TOK_ERROR, s, L->cur);
		}
		}
#undef NEXT_IS
		L->cur = p;
		t = make(L, k, s, p);
	}
	t.bol = bol;
	return t;
}

Token lexer_next(Lexer *L)
{
	Token t = lex_raw(L);
	L->last_kind = t.kind;
	return t;
}
