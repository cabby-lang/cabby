/* parser.h — Cabby parser and file loader.
 *
 * parse_program() reads the main file and, recursively, every file named
 * by `use`, appending all declarations to one Program (dependencies
 * first). Each file is pre-scanned for the type names it declares (object,
 * struct, union, enum, typedef), because C-style syntax needs them: `(T)x`
 * is a cast only if T is a type, and `window w` is a declaration only if
 * `window` is a type.
 *
 * Statements and declarations are parsed by recursive descent, binary
 * expressions by precedence climbing (C's table). Braces delimit blocks;
 * the parser also checks that each line inside a block is indented by
 * exactly one more tab than the line that opened it, and that `}` lines up
 * with that line. Syntax errors are reported once, then the parser skips to
 * the end of the statement (respecting braces) and carries on.
 */
#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stdio.h>

#include "arena.h"
#include "ast.h"

/* `lib_dirs` (NULL-terminated, may be NULL) are searched by `use name`.
 * Always returns a Program; check diag_error_count() before using it. */
Program *parse_program(Arena *arena, const char *path, const char *const *lib_dirs);

/* A library header that has been renamed or merged into another: what to
 * write instead (" (it is called 'strings' now: use strings)"), or NULL. */
const char *retired_header_hint(const char *name, int len);
/* The same for text already in memory (the interactive session): `name`
 * is used in messages and `use local` resolves against the current
 * directory. `src` must be malloc'd; the Program owns it (files[0].src). */
Program *parse_program_source(Arena *arena, const char *name, char *src, const char *const *lib_dirs);

/* AST printer (ast_print.c): used by tests and `cabby --dump-ast`. */
void ast_print_program(FILE *out, const Program *prog);
void ast_print_expr(FILE *out, const Expr *e);
void ast_print_type(FILE *out, const TypeExpr *t);

#endif
