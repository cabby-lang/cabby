/* diag.h — compiler diagnostics shared by every stage.
 * Prints "file:line:col: error: message", then the offending source line
 * with a caret under the column (tabs are preserved so the caret lines up).
 *
 * Every source file the compiler reads is registered with diag_add_file();
 * reports go to the "current" file unless a file id is given explicitly. */
#ifndef DIAG_H
#define DIAG_H

#include <stdbool.h>

typedef enum { DIAG_ERROR, DIAG_WARNING, DIAG_NOTE } DiagLevel;

int         diag_add_file(const char *filename, const char *src);   /* returns its id */
void        diag_set_file(int id);
int         diag_current_file(void);
const char *diag_file_name(int id);
const char *diag_file_source(int id);
/* add + set, for single-file tools */
void        diag_set_source(const char *filename, const char *src);

void diag_report(DiagLevel level, int line, int col, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));
void diag_report_in(DiagLevel level, int file, int line, int col, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));
/* Prints a source line of `file` with a caret under `col` (no message). */
void diag_show_line_in(int file, int line, int col);
void diag_show_line(int line, int col);
int  diag_error_count(void);
int  diag_warning_count(void);
void diag_reset(void);

/* ---- hooks for the interactive session (repl.c) ----
 * A mapper rewrites a location for display: the name shown instead of the
 * file's, the line and column, and how many leading characters of the
 * source line to leave out (the column is already adjusted for them). */
typedef struct DiagPos {
	const char *name;
	int line, col;
	int strip;
} DiagPos;
typedef bool (*DiagMapper)(int file, int line, int col, DiagPos *out);
/* A filter decides whether a report is printed; it is still counted. A
 * note is printed only if the report before it was. */
typedef bool (*DiagFilter)(DiagLevel level, int file, int line);
void    diag_set_mapper(DiagMapper m);
void    diag_set_filter(DiagFilter f);
void    diag_set_quiet(bool quiet);      /* count reports, print nothing */
typedef struct { bool quiet; int errors, warnings; } DiagMute;
DiagMute diag_mute(void);               /* report nothing, count nothing ... */
int      diag_unmute(DiagMute m);       /* ... until this; gives the errors there would have been */
DiagPos diag_map(int file, int line, int col);
void    diag_clear_files(void);          /* forget every registered file */

#define diag_error(line, col, ...) diag_report(DIAG_ERROR, (line), (col), __VA_ARGS__)

#endif
