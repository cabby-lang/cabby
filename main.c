/* main.c — cabby: the Cabby compiler and interpreter driver. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <unistd.h>

#include "version.h"
#include "license.h"
#include "arena.h"
#include "codegen.h"
#include "diag.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "repl.h"
#include "sema.h"
#include "style.h"
#include "util.h"

typedef enum { MODE_NONE, MODE_TOKENS, MODE_AST, MODE_CHECK, MODE_STYLE, MODE_INTERPRET, MODE_COMPILE } Mode;

static void usage(FILE *f)
{
	fprintf(f,
		"usage: cabby                            start an interactive session\n"
		"       cabby <file.cby> [arguments...]  run a program (arguments go to main)\n"
		"       cabby <mode> <file.cby>\n"
		"\n"
		"modes:\n"
		"  -c, --compile <file>  build a native executable via LLVM\n"
		"                          -o <output>   name the executable (default: the file name without .cby)\n"
		"                          -O0 .. -O3    optimization level (default -O2)\n"
		"                          --emit-llvm   write the LLVM IR instead\n"
		"  --check <file>        type- and ownership-check only\n"
		"  --style <file>        check, then list names that don't follow STYLE.md\n"
		"  --dump-ast <file>     print the parsed syntax tree\n"
		"  --dump-tokens <file>  print the token stream\n"
		"  --help                show this message\n"
		"  --version             say which Cabby this is\n"
		"  --license             show Cabby's license (MIT)\n"
		"\n"
		"use <name> looks in $CABBY_LIB, then the lib directory next to cabby.\n"
		"--compile needs clang (or $CABBY_CC) and finds libcabbyrt.a the same way.\n");
}

static int dump_tokens(Arena *a, const char *path)
{
	char *src = read_file(path, NULL);
	if (!src)
		return 1;
	Lexer L;
	diag_set_source(path, src);
	lexer_init(&L, a, path, src);
	for (;;) {
		Token t = lexer_next(&L);
		printf("%d:%d\t%s%s", t.line, t.col, t.bol ? "^" : "", token_kind_name(t.kind));
		if (t.bol)
			printf("[%d]", t.indent);
		if (t.kind == TOK_IDENT || t.kind == TOK_INT || t.kind == TOK_FLOAT ||
		    t.kind == TOK_STRING || t.kind == TOK_CHAR || t.kind == TOK_USE)
			printf("\t%.*s", t.length, t.start);
		putchar('\n');
		if (t.kind == TOK_EOF)
			break;
	}
	free(src);
	return diag_error_count() ? 1 : 0;
}

static bool is_dir(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Library search path: $CABBY_LIB, <exe dir>/lib, <exe dir>/../lib/cabby,
 * then the usual install prefixes. */
static const char **library_dirs(Arena *a)
{
	const char **dirs = arena_alloc(a, sizeof(char *) * 10);
	int n = 0;
	const char *env = getenv("CABBY_LIB");
	if (env && *env)
		dirs[n++] = env;
	char exe[PATH_MAX];
	ssize_t len = -1;
#ifdef __APPLE__
	/* (a Mac has no /proc: the system says where the program is, and
	 * Homebrew's bin/cabby is a link to its Cellar) */
	char here[PATH_MAX];
	uint32_t size = sizeof here;
	if (_NSGetExecutablePath(here, &size) == 0 && realpath(here, exe))
		len = (ssize_t)strlen(exe);
#else
	len = readlink("/proc/self/exe", exe, sizeof exe - 1);
#endif
	if (len > 0) {
		exe[len] = '\0';
		char *slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			char buf[PATH_MAX + 32];
			snprintf(buf, sizeof buf, "%s/lib", exe);
			if (is_dir(buf))
				dirs[n++] = arena_strndup(a, buf, strlen(buf));
			snprintf(buf, sizeof buf, "%s/../lib/cabby", exe);
			if (is_dir(buf))
				dirs[n++] = arena_strndup(a, buf, strlen(buf));
		}
	}
#ifdef CABBY_PREFIX
	/* where `make install` put it (PREFIX), if it's moved since */
	if (is_dir(CABBY_PREFIX "/lib/cabby"))
		dirs[n++] = CABBY_PREFIX "/lib/cabby";
#endif
	if (is_dir("/usr/local/lib/cabby"))
		dirs[n++] = "/usr/local/lib/cabby";
	if (is_dir("/usr/lib/cabby"))
		dirs[n++] = "/usr/lib/cabby";
	dirs[n] = NULL;
	return dirs;
}

int main(int argc, char **argv)
{
	Mode mode = MODE_NONE;
	const char *path = NULL;
	const char *output = NULL;
	bool emit_llvm = false;
	int opt_level = 2;
	int prog_argc = 0;
	char **prog_argv = NULL;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		Mode m = MODE_NONE;
		if (mode == MODE_COMPILE && strcmp(a, "-o") == 0 && i + 1 < argc) {
			output = argv[++i];
			continue;
		}
		if (mode == MODE_COMPILE && strcmp(a, "--emit-llvm") == 0) {
			emit_llvm = true;
			continue;
		}
		if (mode == MODE_COMPILE && a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3' && !a[3]) {
			opt_level = a[2] - '0';
			continue;
		}
		if (path) {
			/* everything after the source file belongs to the program */
			prog_argc = argc - i;
			prog_argv = argv + i;
			break;
		}
		if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage(stdout);
			return 0;
		} else if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
			printf("Cabby %s (%s)\n%s\n%s; \"cabby --license\" for details.\n",
			       CABBY_VERSION, CABBY_VERSION_DATE, CABBY_COPYRIGHT, CABBY_WARRANTY);
			return 0;
		} else if (strcmp(a, "--license") == 0) {
			fputs(CABBY_LICENSE_TEXT, stdout);
			return 0;
		} else if (strcmp(a, "--interpret") == 0) m = MODE_INTERPRET;   /* the default; still accepted */
		else if (strcmp(a, "--compile") == 0 || strcmp(a, "-c") == 0) m = MODE_COMPILE;
		else if (strcmp(a, "--check") == 0)       m = MODE_CHECK;
		else if (strcmp(a, "--style") == 0)       m = MODE_STYLE;
		else if (strcmp(a, "--dump-ast") == 0)    m = MODE_AST;
		else if (strcmp(a, "--dump-tokens") == 0) m = MODE_TOKENS;
		else if (strcmp(a, "-o") == 0 && i + 1 < argc) {
			output = argv[++i];
			continue;
		} else if (a[0] == '-' && a[1]) {
			fprintf(stderr, "cabby: unknown option '%s'\n", a);
			usage(stderr);
			return 2;
		} else {
			path = a;
			continue;
		}
		if (mode != MODE_NONE) {
			fprintf(stderr, "cabby: choose exactly one mode\n");
			return 2;
		}
		mode = m;
	}
	if (!path) {
		if (mode != MODE_NONE || output) {
			usage(stderr);
			return 2;
		}
		Arena la;
		arena_init(&la);
		int rc = repl_main(library_dirs(&la));
		arena_free(&la);
		return rc;
	}
	if (mode == MODE_NONE)
		mode = MODE_INTERPRET;

	Arena arena;
	arena_init(&arena);
	int rc = 0;
	Program *prog = NULL;

	if (mode == MODE_TOKENS) {
		rc = dump_tokens(&arena, path);
		goto done;
	}

	prog = parse_program(&arena, path, library_dirs(&arena));
	if (diag_error_count()) {
		rc = 1;
		goto done;
	}
	if (mode == MODE_AST) {
		ast_print_program(stdout, prog);
		goto done;
	}

	/* (before the checker, which rewrites calls) */
	int bad_calls = mode == MODE_STYLE ? style_calls(prog) : 0;

	Sema sema;
	if (!sema_check(&arena, prog, &sema)) {
		rc = 1;
		goto done;
	}

	switch (mode) {
	case MODE_CHECK:
		break;
	case MODE_STYLE: {
		int n = style_check(prog);
		if (n && bad_calls)
			fprintf(stderr, "%d name%s and %d call%s to change\n", n, n == 1 ? "" : "s", bad_calls,
				bad_calls == 1 ? "" : "s");
		else if (n)
			fprintf(stderr, "%d name%s to change\n", n, n == 1 ? "" : "s");
		else if (bad_calls)
			fprintf(stderr, "%d call%s to change\n", bad_calls, bad_calls == 1 ? "" : "s");
		rc = n || bad_calls ? 1 : 0;
		break;
	}
	case MODE_INTERPRET: {
		/* the program sees its source path as argv[0], then its own arguments */
		char **pv = malloc(sizeof(char *) * (size_t)(prog_argc + 2));
		pv[0] = (char *)path;
		for (int i = 0; i < prog_argc; i++)
			pv[i + 1] = prog_argv[i];
		pv[prog_argc + 1] = NULL;
		rc = interpret(&sema, prog_argc + 1, pv);
		free(pv);
		break;
	}
	case MODE_COMPILE: {
		CompileOptions co = { output, emit_llvm, opt_level, library_dirs(&arena) };
		rc = compile_program(&sema, path, &co);
		break;
	}
	default:
		break;
	}

done:
	if (diag_error_count() > 0)
		fprintf(stderr, "%d error%s\n", diag_error_count(), diag_error_count() == 1 ? "" : "s");
	if (prog)
		for (int i = 0; i < prog->files.len; i++)
			free(prog->files.items[i].src);
	arena_free(&arena);
	return rc;
}
