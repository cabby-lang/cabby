#include "diag.h"
#include "highlight.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILES 256

static struct {
	const char *name;
	const char *src;
} g_files[MAX_FILES];
static int g_nfiles = 0;
static int g_cur = -1;
static int g_errors = 0;
static int g_warnings = 0;
static DiagMapper g_mapper = NULL;
static DiagFilter g_filter = NULL;
static bool g_quiet = false;
static bool g_last_shown = true;

void diag_set_mapper(DiagMapper m) { g_mapper = m; }
void diag_set_filter(DiagFilter f) { g_filter = f; }
void diag_set_quiet(bool quiet) { g_quiet = quiet; }

/* Peeking: check something without reporting or counting anything. */
DiagMute diag_mute(void)
{
	DiagMute m = { g_quiet, g_errors, g_warnings };
	g_quiet = true;
	return m;
}

int diag_unmute(DiagMute m)
{
	int errors = g_errors - m.errors;
	g_quiet = m.quiet;
	g_errors = m.errors;
	g_warnings = m.warnings;
	return errors;
}
void diag_clear_files(void) { g_nfiles = 0; g_cur = -1; }

DiagPos diag_map(int file, int line, int col)
{
	DiagPos p = { NULL, line, col, 0 };
	if (!(g_mapper && line > 0 && g_mapper(file, line, col, &p)))
		p = (DiagPos){ NULL, line, col, 0 };
	if (!p.name)
		p.name = diag_file_name(file);
	return p;
}

int diag_add_file(const char *filename, const char *src)
{
	if (g_nfiles == MAX_FILES) {
		fprintf(stderr, "fatal: too many source files (limit %d)\n", MAX_FILES);
		exit(1);
	}
	g_files[g_nfiles].name = filename ? filename : "<input>";
	g_files[g_nfiles].src = src;
	return g_nfiles++;
}

void diag_set_file(int id) { g_cur = id; }
int diag_current_file(void) { return g_cur; }

const char *diag_file_name(int id)
{
	return (id >= 0 && id < g_nfiles) ? g_files[id].name : "<input>";
}

const char *diag_file_source(int id)
{
	return (id >= 0 && id < g_nfiles) ? g_files[id].src : NULL;
}

void diag_set_source(const char *filename, const char *src)
{
	diag_set_file(diag_add_file(filename, src));
}

int diag_error_count(void) { return g_errors; }
int diag_warning_count(void) { return g_warnings; }
void diag_reset(void) { g_errors = g_warnings = 0; }

static void vreport(DiagLevel level, int file, int line, int col, const char *fmt, va_list ap)
{
	static const char *names[] = { "error", "warning", "note" };
	static const char *colors[] = { "\x1b[1;31m", "\x1b[1;35m", "\x1b[1;36m" };
	int color = isatty(2);

	if (level == DIAG_ERROR)
		g_errors++;
	else if (level == DIAG_WARNING)
		g_warnings++;

	bool show = !g_quiet;
	if (show && g_filter)
		show = level == DIAG_NOTE ? g_last_shown : g_filter(level, file, line);
	if (level != DIAG_NOTE)
		g_last_shown = show;
	if (!show)
		return;

	DiagPos p = diag_map(file, line, col);
	if (line > 0)
		fprintf(stderr, "%s%s:%d:%d: %s%s:%s ", color ? "\x1b[1m" : "", p.name, p.line, p.col,
			color ? colors[level] : "", names[level], color ? "\x1b[0m" : "");
	else
		fprintf(stderr, "%s%s: %s%s:%s ", color ? "\x1b[1m" : "", p.name,
			color ? colors[level] : "", names[level], color ? "\x1b[0m" : "");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	diag_show_line_in(file, line, col);
}

void diag_report(DiagLevel level, int line, int col, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vreport(level, g_cur, line, col, fmt, ap);
	va_end(ap);
}

void diag_report_in(DiagLevel level, int file, int line, int col, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vreport(level, file, line, col, fmt, ap);
	va_end(ap);
}

void diag_show_line_in(int file, int line, int col)
{
	const char *src = diag_file_source(file);
	int color = isatty(2);
	if (!src || line < 1)
		return;

	const char *p = src;
	for (int l = 1; l < line && *p; p++)
		if (*p == '\n')
			l++;
	const char *e = p;
	while (*e && *e != '\n')
		e++;
	if (e > p && e[-1] == '\r')
		e--;
	DiagPos m = diag_map(file, line, col);
	const char *line_start = p;
	for (int i = 0; i < m.strip && p < e; i++)
		p++;
	col = m.col;

	if (color && hl_enabled(2)) {
		/* the code in the same colors as the session and the Vim plugin */
		int n = (int)(e - p);
		unsigned char *cls = malloc((size_t)(n ? n : 1));
		bool in_comment = hl_comment_open_at(src, line_start);
		hl_classify(p, n, cls, &in_comment);
		fputs("    ", stderr);
		int shown = -1;
		for (int i = 0; i < n; i++) {
			if (cls[i] != shown && p[i] != ' ' && p[i] != '\t') {
				fputs(hl_start(cls[i]), stderr);
				shown = cls[i];
			}
			fputc(p[i], stderr);
		}
		fputs(HL_RESET "\n    ", stderr);
		free(cls);
	} else {
		fprintf(stderr, "    %.*s\n    ", (int)(e - p), p);
	}
	for (int i = 0; i < col - 1 && p + i < e; i++)
		fputc(p[i] == '\t' ? '\t' : ' ', stderr);
	fprintf(stderr, "%s^%s\n", color ? "\x1b[1;32m" : "", color ? "\x1b[0m" : "");
}

void diag_show_line(int line, int col) { diag_show_line_in(g_cur, line, col); }
