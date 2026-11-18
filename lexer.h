/* lexer.h — Cabby tokens and lexer.
 *
 * Layout
 * ------
 *  - Each logical line ends with TOK_NEWLINE; there are no semicolons
 *    outside `for` headers. Blank and comment-only lines produce nothing.
 *  - Inside ( ) and [ ] newlines are suppressed, so argument lists wrap.
 *  - Braces are ordinary tokens. Block indentation is enforced by the
 *    parser, using the per-token layout fields below: every token records
 *    whether it is the first on its line (`bol`), how many leading tabs that
 *    line has (`indent`), and where the first space in the leading
 *    whitespace is, if any (`space_col`).
 *  - `ns.name` is one TOK_IDENT when `ns` is a known namespace (see
 *    Lexer.is_namespace), e.g. `gui.window`.
 *  - `use [local] name` at the start of a line is one TOK_USE token; its
 *    path is the name with ".hby" added. (`use` anywhere else is the
 *    reserved word TOK_KW_KWUSE, which the parser rejects.)
 *  - At end of file a final NEWLINE is synthesized if missing, then TOK_EOF.
 *
 * Comments are line comments (//) and block comments (slash-star to
 * star-slash, not nested).
 */
#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"

/* T = structural/literal token, K = keyword, O = operator/punctuation.
 * The second argument is the display name (and exact spelling for K/O). */
#define TOKEN_KINDS(T, K, O)                 \
	T(EOF,        "end of file")         \
	T(ERROR,      "invalid token")       \
	T(NEWLINE,    "newline")             \
	T(IDENT,      "identifier")          \
	T(INT,        "integer literal")     \
	T(FLOAT,      "floating-point literal") \
	T(STRING,     "string literal")      \
	T(CHAR,       "char literal")        \
	T(USE,        "use declaration")     \
	                                     \
	K(AUTO,       "auto")                \
	K(BREAK,      "break")               \
	K(CASE,       "case")                \
	K(CHAR,       "char")                \
	K(CONST,      "const")               \
	K(CONTINUE,   "continue")            \
	K(DEFAULT,    "default")             \
	K(DO,         "do")                  \
	K(DOUBLE,     "double")              \
	K(ELSE,       "else")                \
	K(ENUM,       "enum")                \
	K(EXTERN,     "extern")              \
	K(FLOAT,      "float")               \
	K(FOR,        "for")                 \
	K(GOTO,       "goto")                \
	K(IF,         "if")                  \
	K(INT,        "int")                 \
	K(LONG,       "long")                \
	K(REGISTER,   "register")            \
	K(RETURN,     "return")              \
	K(SHORT,      "short")               \
	K(SIGNED,     "signed")              \
	K(SIZEOF,     "sizeof")              \
	K(STATIC,     "static")              \
	K(STRUCT,     "struct")              \
	K(SWITCH,     "switch")              \
	K(TYPEDEF,    "typedef")             \
	K(UNION,      "union")               \
	K(UNSIGNED,   "unsigned")            \
	K(VOID,       "void")                \
	K(VOLATILE,   "volatile")            \
	K(WHILE,      "while")               \
	K(STR,        "str")                 \
	K(OBJECT,     "object")              \
	K(BOOL,       "bool")                \
	K(TRUE,       "true")                \
	K(FALSE,      "false")               \
	K(SELF,       "self")                \
	K(KWUSE,      "use")                 \
	K(IN,         "in")                  \
	K(TO,         "to")                  \
	K(DOWN,       "down")                \
	K(BY,         "by")                  \
	K(LIST,       "list")                \
	K(LOOKUP,     "lookup")              \
	K(AKA,        "aka")                 \
	K(IS,         "is")                  \
	K(NOT,        "not")                 \
	K(AND,        "and")                 \
	K(OR,         "or")                  \
	K(UNTIL,      "until")               \
	K(INNER,      "inner")               \
	                                     \
	O(PLUS,       "+")                   \
	O(MINUS,      "-")                   \
	O(STAR,       "*")                   \
	O(SLASH,      "/")                   \
	O(PERCENT,    "%")                   \
	O(AMP,        "&")                   \
	O(PIPE,       "|")                   \
	O(CARET,      "^")                   \
	O(TILDE,      "~")                   \
	O(SHL,        "<<")                  \
	O(SHR,        ">>")                  \
	O(AND_AND,    "&&")                  \
	O(OR_OR,      "||")                  \
	O(BANG,       "!")                   \
	O(EQ,         "=")                   \
	O(EQ_EQ,      "==")                  \
	O(BANG_EQ,    "!=")                  \
	O(LT,         "<")                   \
	O(LE,         "<=")                  \
	O(GT,         ">")                   \
	O(GE,         ">=")                  \
	O(PLUS_EQ,    "+=")                  \
	O(MINUS_EQ,   "-=")                  \
	O(STAR_EQ,    "*=")                  \
	O(SLASH_EQ,   "/=")                  \
	O(PERCENT_EQ, "%=")                  \
	O(AMP_EQ,     "&=")                  \
	O(PIPE_EQ,    "|=")                  \
	O(CARET_EQ,   "^=")                  \
	O(SHL_EQ,     "<<=")                 \
	O(SHR_EQ,     ">>=")                 \
	O(PLUS_PLUS,  "++")                  \
	O(MINUS_MINUS,"--")                  \
	O(DOT,        ".")                   \
	O(ELLIPSIS,   "...")                 \
	O(COMMA,      ",")                   \
	O(COLON,      ":")                   \
	O(SEMI,       ";")                   \
	O(LPAREN,     "(")                   \
	O(RPAREN,     ")")                   \
	O(LBRACKET,   "[")                   \
	O(RBRACKET,   "]")                   \
	O(LBRACE,     "{")                   \
	O(RBRACE,     "}")

typedef enum {
#define TK_T(name, str) TOK_##name,
#define TK_K(name, str) TOK_KW_##name,
#define TK_O(name, str) TOK_##name,
	TOKEN_KINDS(TK_T, TK_K, TK_O)
#undef TK_T
#undef TK_K
#undef TK_O
	TOK__COUNT
} TokenKind;

typedef struct Token {
	TokenKind kind;
	const char *start;   /* points into the source buffer */
	int length;
	int line, col;       /* 1-based; a tab counts as one column */

	/* layout */
	bool bol;            /* first token on its line */
	int indent;          /* leading tabs on this token's line */
	int space_col;       /* column of the first space in the leading whitespace, or 0 */

	/* literal details */
	bool lit_unsigned;   /* INT: u/U suffix */
	uint8_t lit_long;    /* INT: number of l/L in the suffix (0-2) */
	bool lit_float32;    /* FLOAT: f/F suffix */
	bool lit_based;      /* INT: written in hex/binary/octal (affects C's typing rule) */
	bool use_local;      /* USE: `use local ...` */

	union {
		uint64_t u;      /* INT: magnitude */
		double f;        /* FLOAT */
		uint8_t ch;      /* CHAR: one byte */
		struct {         /* STRING / USE path: decoded, NUL-terminated, arena-owned */
			const char *data;
			size_t len;
		} str;
	} v;
} Token;

typedef struct Lexer {
	const char *filename;
	const char *src;
	const char *cur;
	const char *line_start;
	int line;

	bool at_line_start;  /* next token is the first on a physical line */
	int line_indent;     /* layout of the current line */
	int line_space_col;
	int nesting;         /* depth of open ( and [ — newlines suppressed when > 0 */
	TokenKind last_kind;
	bool quiet;          /* suppress diagnostics (used for look-ahead and pre-scans) */
	bool open_comment;   /* the text ended inside a block comment */
	/* Namespaces: `gui.window` is one identifier when `gui` is a namespace
	 * (a library declared names like `object gui.window`). */
	bool (*is_namespace)(void *ctx, const char *s, int len);
	void *ns_ctx;

	Arena *arena;
} Lexer;

void        lexer_init(Lexer *L, Arena *arena, const char *filename, const char *src);
Token       lexer_next(Lexer *L);
const char *token_kind_name(TokenKind kind);
bool        token_is_keyword(TokenKind kind);

#endif
