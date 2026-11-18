/* highlight.h — colors for Cabby source in a terminal.
 *
 * The same groups as the Vim plugin (editor/vim/syntax/cabby.vim), drawn in
 * the colors Vim gives those groups by default, using the terminal's own
 * 16-color palette. Used by the interactive session's line editor, and for
 * the line of code shown under an error.
 *
 * Color is off when the output isn't a terminal, when NO_COLOR is set, or
 * when TERM is "dumb". CABBY_COLORS changes colors, like LS_COLORS:
 *   CABBY_COLORS="keyword=33:type=32:constant=35:special=95:function=1;36:use=94:comment=34:error=37;41"
 */
#ifndef CABBY_HIGHLIGHT_H
#define CABBY_HIGHLIGHT_H

#include <stdbool.h>

enum {
	HL_NONE,
	HL_KEYWORD,    /* if, while, return, case, sizeof ... (Vim: Statement) */
	HL_TYPE,       /* int, str, list, const, object, gui.window ... (Type) */
	HL_CONSTANT,   /* strings, characters, numbers, true, false (Constant) */
	HL_SPECIAL,    /* escapes and % codes in strings, self, .length ... (Special) */
	HL_FUNCTION,   /* println, read_line, stderr ... (Function) */
	HL_USE,        /* use, use local (PreProc) */
	HL_COMMENT,    /* comments (Comment) */
	HL_ERROR,      /* cement, a stray ';' (Error) */
	HL__COUNT
};

/* Fills cls[0..n) with an HL_ class for each byte of the line s[0..n).
 * *in_comment carries a block comment from one line to the next (start at
 * false); it is updated for the line after. */
void hl_classify(const char *s, int n, unsigned char *cls, bool *in_comment);

/* Whether a block comment is still open at byte `upto` of a whole source text. */
bool hl_comment_open_at(const char *src, const char *upto);

/* Whether to color output on this file descriptor. */
bool hl_enabled(int fd);

/* The escape sequence that starts class c ("" for HL_NONE: reset), and the reset. */
const char *hl_start(int c);
#define HL_RESET "\x1b[0m"

#endif
