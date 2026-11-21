/* wasm/stubs.c — what the WebAssembly build of cabby has in place of what a
 * browser can't do: compiling (clang isn't there), the interactive session
 * (it needs a terminal), and a library's C functions that aren't in it. */
#include <stdio.h>
#include <stdlib.h>
#include "../codegen.h"
#include "../repl.h"
#include "../runtime/rt.h"

int compile_program(Sema *S, const char *src_path, const CompileOptions *opt)
{
	(void)S; (void)src_path; (void)opt;
	fprintf(stderr, "cabby: compiling (-c) needs clang, which isn't here (in a browser); the program can be run "
		"as it is\n");
	return 1;
}

int repl_main(const char *const *lib_dirs)
{
	(void)lib_dirs;
	fprintf(stderr, "cabby: the interactive session needs a terminal; here (in a browser), give it a program\n");
	return 1;
}

_Noreturn void cby_wasm_missing(const char *name)
{
	rt_panic(NULL, 0, 0, "'%s' isn't here (in a browser): its library needs the computer itself", name);
}

/* No libraries to load here (SQLite, OpenSSL, curl and the like): those that
 * would use one say they couldn't find it, as on a computer without it. */
#include <dlfcn.h>
void *dlopen(const char *file, int mode) { (void)file; (void)mode; return NULL; }
void *dlsym(void *restrict h, const char *restrict name) { (void)h; (void)name; return NULL; }
char *dlerror(void) { return "there are no libraries to load here (in a browser)"; }
int dlclose(void *h) { (void)h; return 0; }

/* nor other programs */
int system(const char *command) { (void)command; return -1; }
