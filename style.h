/* style.h — `cabby --style`: names that don't follow STYLE.md. */
#ifndef STYLE_H
#define STYLE_H

#include "ast.h"

/* Reports each name in the program's own files that breaks the naming
 * rules, as a warning with the suggested spelling. Returns how many. */
int style_check(const Program *prog);

/* Reports each call in the program's own files written with parentheses
 * it doesn't need (STYLE.md, Calls). Run it before the checker, which
 * rewrites calls. Returns how many. */
int style_calls(const Program *prog);

#endif
