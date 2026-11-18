/* codegen.h — LLVM IR generation for `cabby --compile`.
 *
 * The program (already checked by sema) is lowered to one textual LLVM IR
 * module, which clang then compiles and links with the Cabby runtime
 * (libcabbyrt.a) and libm.
 *
 * Lowering
 *  - Scalars (bool, integers, floats, borrows, object handles) live in SSA
 *    registers; bool is i1 in registers and i8 in memory.
 *  - str = { ptr data, i64 len } with bit 62 of len marking a static
 *    literal; slice = { ptr data, i64 len }; structs are LLVM structs with C
 *    layout; unions are byte blobs; arrays are [N x T]. These aggregates live
 *    in memory and are moved/copied with memcpy.
 *  - An object is a pointer to a heap block { ptr classinfo, B0, B1, ... }
 *    with one sub-struct per class in its layout, so a class's fields have
 *    the same offsets inside every subclass. classinfo = { vtable, block
 *    offsets, size }: virtual calls index the vtable, and field access
 *    through a borrow of a base class adds the dynamic block offset.
 *  - Every local that needs dropping has a drop flag. Every exit from a
 *    scope (falling off, return, break, continue, goto) emits the drops for
 *    the scopes it leaves. Statement temporaries are dropped at the end of
 *    their statement. Each object class has a drop function that runs the
 *    destructors from most derived to base-most, drops the fields, and
 *    frees the block.
 *  - Failure checks (division, shifts, bounds) are inline compares whose
 *    cold path calls the same runtime check the interpreter uses, so panic
 *    messages are identical.
 */
#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "sema.h"

typedef struct CompileOptions {
	const char *output;            /* executable (or .ll with emit_llvm) */
	bool emit_llvm;                /* write the IR instead of building */
	int opt_level;                 /* passed to clang as -O<n> */
	const char *const *lib_dirs;   /* where libcabbyrt.a lives */
} CompileOptions;

/* Writes the LLVM IR module for the program. */
void codegen_emit(Sema *S, FILE *out);

/* Generates IR and builds an executable. Returns 0 on success. */
int compile_program(Sema *S, const char *src_path, const CompileOptions *opt);

#endif
