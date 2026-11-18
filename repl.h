/* repl.h — the interactive session (`cabby` with no file).
 *
 * Each input is either a declaration (use, object, struct, union, enum,
 * typedef, extern, or a function definition) or statements. The session is
 * kept as a program: the declarations in the order they were entered, then
 * `int main {` with every statement entered so far. After each input the
 * whole program is parsed and checked again, so types and ownership are
 * checked across everything typed (using a moved string three inputs later
 * is still an error), and then only the new statements run. Variables keep
 * their values between inputs.
 *
 *   - An input that fails to check is not kept.
 *   - A single expression is echoed: `x + 1` runs as `println x + 1`, and
 *     a bare call whose result can be printed shows it.
 *   - A function definition with the name of an earlier one replaces it.
 *   - A panic or Ctrl-C abandons the input and returns to the prompt; the
 *     input is not kept.
 *   - `exit` (or Ctrl-D) ends the session: variables are dropped, then the
 *     user is offered to save the session as a .cby file.
 *   - `return n` ends the session too, with exit status n.
 */
#ifndef REPL_H
#define REPL_H

/* Runs the session; returns the process exit status. */
int repl_main(const char *const *lib_dirs);

#endif
