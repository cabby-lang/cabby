/* csv.c — reading CSV (use csv): text into rows of fields, as json lists
 * (json.c's), which the header turns into a list list str.
 *
 * As RFC 4180 has it, and as spreadsheets write it: fields separated by a
 * comma (or what's given: ';', a tab), a field in double quotes may hold
 * the separator, line ends and "" for a quote; rows end at \n, \r\n or \r.
 * A byte order mark at the start (Excel's UTF-8) is left out, and so are
 * empty lines. A quoted field with more after its closing quote keeps it,
 * as spreadsheets do, rather than being an error.
 */
#include "rt.h"
#include <stdlib.h>
#include <string.h>

/* json.c's */
extern int64_t cby_json_new(int64_t kind);
extern int64_t cby_json_from_str(const char *s);
extern void cby_json_add(int64_t h, int64_t value);
extern void cby_json_release(int64_t h);
enum { J_LIST = 4 };

typedef struct {
	char *d;
	size_t n, cap;
} Field;

static void put(Field *f, char c)
{
	if (f->n + 2 > f->cap) {
		f->cap = f->cap ? f->cap * 2 : 64;
		f->d = realloc(f->d, f->cap);
	}
	f->d[f->n++] = c;
}

static void end_field(Field *f, int64_t row)
{
	put(f, 0);
	int64_t v = cby_json_from_str(f->d);
	cby_json_add(row, v);
	cby_json_release(v);
	f->n = 0;
}

/* the rows: a json list of lists of strings */
int64_t cby_csv_parse(const char *text, int64_t sep)
{
	const char *p = text;
	if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
		p += 3;
	char s = (char)sep;
	int64_t rows = cby_json_new(J_LIST);
	int64_t row = 0;
	Field f = { 0 };
	bool in_row = false;
	while (*p) {
		if (!in_row) {
			/* (an empty line is no row) */
			if (*p == '\n' || *p == '\r') {
				p++;
				continue;
			}
			row = cby_json_new(J_LIST);
			in_row = true;
		}
		if (*p == '"') {
			p++;
			for (;;) {
				if (!*p)
					break;
				if (*p == '"') {
					if (p[1] == '"') {
						put(&f, '"');
						p += 2;
						continue;
					}
					p++;
					break;
				}
				put(&f, *p++);
			}
			/* (anything after the closing quote, up to the separator) */
			while (*p && *p != s && *p != '\n' && *p != '\r')
				put(&f, *p++);
		} else {
			while (*p && *p != s && *p != '\n' && *p != '\r')
				put(&f, *p++);
		}
		end_field(&f, row);
		if (*p == s) {
			p++;
			if (!*p || *p == '\n' || *p == '\r')
				end_field(&f, row);   /* a separator at the end: an empty last field */
			else
				continue;
		}
		if (*p == '\r' && p[1] == '\n')
			p += 2;
		else if (*p)
			p++;
		cby_json_add(rows, row);
		cby_json_release(row);
		in_row = false;
	}
	if (in_row) {
		cby_json_add(rows, row);
		cby_json_release(row);
	}
	free(f.d);
	return rows;
}
