/* astdump — parse a Cabby program and print its AST (development aid).
 * Exit status: 0 if it parsed cleanly, 1 on syntax errors.
 * `use name` is searched for in $CABBY_LIB, then ./lib. */
#include <stdio.h>
#include <stdlib.h>

#include "../arena.h"
#include "../diag.h"
#include "../parser.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <file.cby>\n", argv[0]);
		return 2;
	}
	const char *dirs[3] = { getenv("CABBY_LIB"), "lib", NULL };
	const char *const *libs = dirs[0] ? dirs : dirs + 1;

	Arena arena;
	arena_init(&arena);
	Program *prog = parse_program(&arena, argv[1], libs);
	int errs = diag_error_count();
	if (errs == 0)
		ast_print_program(stdout, prog);
	else
		fprintf(stderr, "%d error%s\n", errs, errs == 1 ? "" : "s");
	for (int i = 0; i < prog->files.len; i++)
		free(prog->files.items[i].src);
	arena_free(&arena);
	return errs ? 1 : 0;
}
