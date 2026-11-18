/* interpreter.h — tree-walking interpreter for --interpret.
 *
 * Runs a program that has passed sema_check(). Values live in cells
 * (frame slots, object fields, array elements, statement temporaries);
 * a reference is a pointer to a cell. Ownership follows sema's
 * annotations exactly:
 *   - a move (Expr.use == USE_MOVE) empties the source cell, which is the
 *     runtime "drop flag";
 *   - at every block exit (normal, return, break, continue) the block's
 *     still-initialized locals are dropped in reverse declaration order,
 *     then the function's parameters when it returns;
 *   - temporaries created while evaluating a statement are dropped, in
 *     reverse order, when that statement finishes (for `if`/`while`
 *     conditions: as soon as the condition has been evaluated);
 *   - dropping an object runs every destructor (~name) in its class layout
 *     from most derived to base-most, then drops its fields in layout order,
 *     then frees it.
 *
 * Extern functions are resolved with dlsym() in the cabby process and
 * called through libffi, so anything in libc/libm is callable. `long &x`
 * (etc.) arguments are passed as pointers to a C scratch copy that is
 * written back after the call, so out-parameters (e.g. scanf) work.
 */
#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "sema.h"

/* Runs main() with the program's own arguments (argv[0] is the source
 * path). Returns the process exit status: main's int result (or 0).
 * Panics exit the process directly with status 101. */
int interpret(Sema *sema, int argc, char **argv);

/* ---- the interactive session (repl.c) ----
 * The session is the body of a `main` that grows one input at a time.
 * After each input the whole session is checked again; the new Sema is
 * adopted (variables keep their values: main's frame and the globals are
 * carried over, and objects are re-pointed at the new class records) and
 * only the new top-level statements of main are run. A panic or Ctrl-C
 * abandons the input and returns to the prompt. */
#include <signal.h>

typedef struct InterpSession InterpSession;
typedef enum { IRUN_OK, IRUN_PANIC, IRUN_INTERRUPTED, IRUN_RETURNED } IRun;

extern volatile sig_atomic_t interp_interrupted;   /* set by a SIGINT handler */

InterpSession *isession_new(void);
/* `key` names a global stably across checks (by where it was declared).
 * Fails only if main has too many variables for the session. */
bool isession_adopt(InterpSession *IS, Sema *S, const char *(*key)(Symbol *g, void *ctx), void *ctx);
/* Runs main's top-level statements from index `first`. On IRUN_RETURNED
 * *status is main's exit status. After a panic or interruption the caller
 * must adopt a Sema for the session without that input. */
IRun isession_run(InterpSession *IS, int first, int *status);
/* Drops every variable (running destructors) and frees the session. */
void isession_end(InterpSession *IS);

#endif
