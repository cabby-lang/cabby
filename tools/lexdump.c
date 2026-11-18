/* lexdump — print the token stream of a source file (development aid). */
#include <stdio.h>
#include <stdlib.h>

#include "../arena.h"
#include "../diag.h"
#include "../lexer.h"
#include "../util.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return 2;
	}
	char *src = read_file(argv[1], NULL);
	if (!src)
		return 1;

	Arena arena;
	arena_init(&arena);
	diag_set_source(argv[1], src);

	Lexer L;
	lexer_init(&L, &arena, argv[1], src);
	int depth = 0;
	for (;;) {
		Token t = lexer_next(&L);
		(void)depth;
		printf("%4d:%-3d %c%d %-16s", t.line, t.col, t.bol ? '^' : ' ', t.indent, token_kind_name(t.kind));
		switch (t.kind) {
		case TOK_IDENT:  printf(" %.*s", t.length, t.start); break;
		case TOK_INT:    printf(" %llu%s%s", (unsigned long long)t.v.u, t.lit_unsigned ? " u" : "", t.lit_long ? (t.lit_long == 2 ? " ll" : " l") : ""); break;
		case TOK_FLOAT:  printf(" %g", t.v.f); break;
		case TOK_CHAR:   printf(" byte %u", t.v.ch); break;
		case TOK_USE:    printf(" %s%s", t.use_local ? "local " : "", t.v.str.data); break;
		case TOK_STRING: printf(" \"%s\" (%zu bytes)", t.v.str.data, t.v.str.len); break;
		default: break;
		}
		putchar('\n');
		if (t.kind == TOK_EOF)
			break;
	}

	int errs = diag_error_count();
	arena_free(&arena);
	free(src);
	return errs ? 1 : 0;
}
