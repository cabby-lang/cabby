/* databases.c — the C side of the databases library (lib/databases.hby):
 * SQLite databases, each a single file, with no server. Part of Cabby's
 * runtime, so the interpreter and compiled programs both have it.
 *
 * SQLite itself comes with every Mac and nearly every Linux (it's what
 * Firefox, and much of the desktop, keep their data in): it's loaded the
 * first time a program opens a database, so neither building Cabby nor a
 * program that never uses one needs it, and only the parts of its
 * interface used here are declared. Values go in as text, bound to the
 * ?s of a statement (never pasted into it, so a name with a ' in it can't
 * break the statement, or change what it does); SQLite turns them into
 * numbers for columns of numbers.
 *
 * PostgreSQL, a server, is reached the same way, through its own libpq
 * (loaded when a program first connects to one), and asked the same
 * things: databases.connect gives the same databases.database, whose ?s
 * are made PostgreSQL's $1, $2 ... before the SQL is sent. */
#include "rt.h"
#include <ctype.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

enum { SQ_OK = 0, SQ_ROW = 100, SQ_DONE = 101, SQ_INTEGER = 1, SQ_FLOAT = 2, SQ_TEXT = 3, SQ_BLOB = 4, SQ_NULL = 5 };

/* json.c's and bytes.c's, for records and binary values */
extern void cby_bytes_release(int64_t b);
extern int64_t cby_json_new(int64_t kind);
extern int64_t cby_json_from_str(const char *s);
extern int64_t cby_json_from_double(double d);
extern int64_t cby_json_from_bool(int b);
extern void cby_json_set(int64_t h, const char *key, int64_t value);
extern void cby_json_add(int64_t h, int64_t value);
extern void cby_json_release(int64_t h);
enum { JSON_NULL = 0, JSON_LIST = 4, JSON_LOOKUP = 5 };
#define SQ_TRANSIENT ((void (*)(void *))(intptr_t)-1)
enum { OPEN_READWRITE = 0x02, OPEN_CREATE = 0x04 };

static struct {
	int tried, ok;
	int (*open_v2)(const char *, sqlite3 **, int, const char *);
	int (*close_v2)(sqlite3 *);
	const char *(*errmsg)(sqlite3 *);
	int (*prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
	int (*bind_text)(sqlite3_stmt *, int, const char *, int, void (*)(void *));
	int (*bind_blob)(sqlite3_stmt *, int, const void *, int, void (*)(void *));
	int (*bind_double)(sqlite3_stmt *, int, double);
	int (*bind_int64)(sqlite3_stmt *, int, int64_t);
	int (*bind_null)(sqlite3_stmt *, int);
	int (*reset)(sqlite3_stmt *);
	int (*clear_bindings)(sqlite3_stmt *);
	const void *(*column_blob)(sqlite3_stmt *, int);
	double (*column_double)(sqlite3_stmt *, int);
	int64_t (*column_int64)(sqlite3_stmt *, int);
	const char *(*sql)(sqlite3_stmt *);
	int (*bind_parameter_count)(sqlite3_stmt *);
	int (*step)(sqlite3_stmt *);
	int (*column_count)(sqlite3_stmt *);
	const char *(*column_name)(sqlite3_stmt *, int);
	const unsigned char *(*column_text)(sqlite3_stmt *, int);
	int (*column_bytes)(sqlite3_stmt *, int);
	int (*column_type)(sqlite3_stmt *, int);
	int (*finalize)(sqlite3_stmt *);
	int (*changes)(sqlite3 *);
	int64_t (*last_insert_rowid)(sqlite3 *);
	int (*busy_timeout)(sqlite3 *, int);
} sq;

static void need_sqlite(void)
{
	if (!sq.tried) {
		sq.tried = 1;
		static const char *const names[] = { "libsqlite3.so.0", "libsqlite3.so", "libsqlite3.dylib",
						      "/usr/lib/libsqlite3.dylib", "libsqlite3.0.dylib", NULL };
		void *h = NULL;
		for (int i = 0; names[i] && !h; i++)
			h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (h) {
#define LOAD(field, name) do { void *p_ = dlsym(h, name); memcpy(&sq.field, &p_, sizeof p_); } while (0)
			LOAD(open_v2, "sqlite3_open_v2");
			LOAD(close_v2, "sqlite3_close_v2");
			LOAD(errmsg, "sqlite3_errmsg");
			LOAD(prepare_v2, "sqlite3_prepare_v2");
			LOAD(bind_text, "sqlite3_bind_text");
			LOAD(bind_blob, "sqlite3_bind_blob");
			LOAD(bind_double, "sqlite3_bind_double");
			LOAD(bind_int64, "sqlite3_bind_int64");
			LOAD(bind_null, "sqlite3_bind_null");
			LOAD(reset, "sqlite3_reset");
			LOAD(clear_bindings, "sqlite3_clear_bindings");
			LOAD(column_blob, "sqlite3_column_blob");
			LOAD(column_double, "sqlite3_column_double");
			LOAD(column_int64, "sqlite3_column_int64");
			LOAD(sql, "sqlite3_sql");
			LOAD(bind_parameter_count, "sqlite3_bind_parameter_count");
			LOAD(step, "sqlite3_step");
			LOAD(column_count, "sqlite3_column_count");
			LOAD(column_name, "sqlite3_column_name");
			LOAD(column_text, "sqlite3_column_text");
			LOAD(column_bytes, "sqlite3_column_bytes");
			LOAD(column_type, "sqlite3_column_type");
			LOAD(finalize, "sqlite3_finalize");
			LOAD(changes, "sqlite3_changes");
			LOAD(last_insert_rowid, "sqlite3_last_insert_rowid");
			LOAD(busy_timeout, "sqlite3_busy_timeout");
#undef LOAD
			sq.ok = sq.open_v2 && sq.close_v2 && sq.errmsg && sq.prepare_v2 && sq.bind_text &&
				sq.bind_parameter_count && sq.step && sq.column_count && sq.column_name && sq.column_text &&
				sq.column_bytes && sq.column_type && sq.finalize && sq.changes && sq.last_insert_rowid &&
				sq.busy_timeout && sq.bind_blob && sq.bind_double && sq.bind_int64 && sq.bind_null && sq.reset &&
				sq.clear_bindings && sq.column_blob && sq.column_double && sq.column_int64 && sq.sql;
		}
	}
	if (!sq.ok)
		rt_panic(NULL, 0, 0, "the databases library needs SQLite, which most systems have: install the "
				     "'sqlite' package (libsqlite3-0 on Debian) and try again");
}

/* ------------------------------------------------------------ databases open */

typedef struct {
	sqlite3 *db;
	char *path;
	void *pg;              /* a PostgreSQL connection (PGconn), instead of db */
	int64_t pg_changes;    /* how many rows its last statement changed */
	int pg_statements;     /* how many statements it's made (for their names) */
} Db;

static Db *database(int64_t id);

static Db **dbs;
static int ndbs;

static int64_t keep_db(Db *d);

int64_t cby_databases_open(const char *path)
{
	need_sqlite();
	sqlite3 *db = NULL;
	if (sq.open_v2(path, &db, OPEN_READWRITE | OPEN_CREATE, NULL) != SQ_OK) {
		char why[300];
		snprintf(why, sizeof why, "%s", db ? sq.errmsg(db) : "it couldn't be opened");
		if (db)
			sq.close_v2(db);
		rt_panic(NULL, 0, 0, "can't open the database '%s': %s", path, why);
	}
	sq.busy_timeout(db, 5000);   /* (another program writing to it: wait a moment, not fail) */
	Db *d = calloc(1, sizeof *d);
	d->db = db;
	d->path = strdup(path);
	return keep_db(d);
}

static int64_t keep_db(Db *d)
{
	int i = 0;
	while (i < ndbs && dbs[i])
		i++;
	if (i == ndbs) {
		dbs = realloc(dbs, sizeof *dbs * (size_t)(ndbs + 1));
		ndbs++;
	}
	dbs[i] = d;
	return i + 1;
}

static Db *database(int64_t id)
{
	if (id < 1 || id > ndbs || !dbs[id - 1])
		rt_panic(NULL, 0, 0, "that database has been closed");
	return dbs[id - 1];
}

static void pg_close(void *conn);

void cby_databases_close(int64_t id)
{
	if (id >= 1 && id <= ndbs && dbs[id - 1]) {
		if (dbs[id - 1]->pg)
			pg_close(dbs[id - 1]->pg);
		else
			sq.close_v2(dbs[id - 1]->db);
		free(dbs[id - 1]->path);
		free(dbs[id - 1]);
		dbs[id - 1] = NULL;
	}
}

/* ------------------------------------------------------------ values for the ?s */

/* the values for the next statement's ?s, in order: text, bytes, a
 * number, a whole number, or nothing (NULL) */
enum { B_TEXT, B_BYTES, B_NUMBER, B_WHOLE, B_NOTHING };

typedef struct {
	int kind;
	char *data;
	int64_t len;
	double number;
	int64_t whole;
} Bind;

typedef struct {
	Bind *b;
	int n, cap;
} Binds;

/* the values for run_with's next statement (each statement made once has
 * its own) */
static _Thread_local Binds given;

static Bind *new_bind(Binds *l, int kind)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 8;
		l->b = realloc(l->b, sizeof *l->b * (size_t)l->cap);
	}
	Bind *b = &l->b[l->n++];
	memset(b, 0, sizeof *b);
	b->kind = kind;
	return b;
}

static void add_text(Binds *l, const char *v)
{
	Bind *b = new_bind(l, B_TEXT);
	b->data = strdup(v);
	b->len = (int64_t)strlen(v);
}

static void add_bytes(Binds *l, int64_t h)
{
	int64_t len = 0;
	const uint8_t *d = rt_bytes_data(h, &len);
	Bind *b = new_bind(l, B_BYTES);
	b->data = malloc((size_t)len + 1);
	if (len)
		memcpy(b->data, d, (size_t)len);
	b->len = len;
}

void cby_databases_value(const char *v) { add_text(&given, v); }

static void binds_clear_list(Binds *l)
{
	for (int i = 0; i < l->n; i++)
		free(l->b[i].data);
	l->n = 0;
}

static void binds_clear(void) { binds_clear_list(&given); }

static void bind_all(sqlite3_stmt *st, const Binds *l)
{
	for (int i = 0; i < l->n; i++) {
		const Bind *b = &l->b[i];
		switch (b->kind) {
		case B_TEXT: sq.bind_text(st, i + 1, b->data, (int)b->len, SQ_TRANSIENT); break;
		case B_BYTES: sq.bind_blob(st, i + 1, b->data, (int)b->len, SQ_TRANSIENT); break;
		case B_NUMBER: sq.bind_double(st, i + 1, b->number); break;
		case B_WHOLE: sq.bind_int64(st, i + 1, b->whole); break;
		default: sq.bind_null(st, i + 1);
		}
	}
}

/* ------------------------------------------------------------ results */

static _Thread_local char **res;
static _Thread_local size_t *reslen;
static _Thread_local int nres, capres;
static _Thread_local int ncolumns;

static void results_clear(void)
{
	for (int i = 0; i < nres; i++)
		free(res[i]);
	nres = 0;
	ncolumns = 0;
}

static void result_n(const void *s, size_t n)
{
	if (nres == capres) {
		capres = capres ? capres * 2 : 64;
		res = realloc(res, sizeof *res * (size_t)capres);
		reslen = realloc(reslen, sizeof *reslen * (size_t)capres);
	}
	res[nres] = malloc(n + 1);
	memcpy(res[nres], s, n);
	res[nres][n] = '\0';
	reslen[nres++] = n;
}

int64_t cby_databases_result_length(int64_t i) { return i >= 0 && i < nres ? (int64_t)reslen[i] : 0; }

void cby_databases_take(int64_t i, char *dst)
{
	if (i >= 0 && i < nres && reslen[i])
		memcpy(dst, res[i], reslen[i]);
}

int64_t cby_databases_columns(void) { return ncolumns; }

/* what keep asks for, of the rows of the last statement that gives rows */
enum { KEEP_NOTHING, KEEP_TEXT, KEEP_RECORDS, KEEP_BYTES };

static _Thread_local int64_t records;   /* KEEP_RECORDS: a json list (0: none) */
static _Thread_local int64_t first_bytes;   /* KEEP_BYTES: the first row's first column (0: none) */

/* the json list the last run made (taken: the caller owns it) */
int64_t cby_databases_records(void)
{
	int64_t r = records ? records : cby_json_new(JSON_LIST);
	records = 0;
	return r;
}

int64_t cby_databases_bytes(void)
{
	int64_t b = first_bytes ? first_bytes : rt_bytes_make(NULL, 0);
	first_bytes = 0;
	return b;
}

/* a value of a row, as JSON: a number as a number, text as text, nothing
 * as null, and binary values (BLOBs) as their bytes in hexadecimal */
static int64_t json_value(sqlite3_stmt *st, int c)
{
	switch (sq.column_type(st, c)) {
	case SQ_INTEGER: return cby_json_from_double((double)sq.column_int64(st, c));
	case SQ_FLOAT: return cby_json_from_double(sq.column_double(st, c));
	case SQ_NULL: return cby_json_new(JSON_NULL);
	case SQ_BLOB: {
		int n = sq.column_bytes(st, c);
		const unsigned char *d = sq.column_blob(st, c);
		char *hex = malloc((size_t)n * 2 + 1);
		for (int i = 0; i < n; i++)
			sprintf(hex + 2 * i, "%02x", d[i]);
		hex[2 * n] = 0;
		int64_t v = cby_json_from_str(hex);
		free(hex);
		return v;
	}
	default: {
		const unsigned char *t = sq.column_text(st, c);
		return cby_json_from_str(t ? (const char *)t : "");
	}
	}
}

/* ------------------------------------------------------------ running SQL */

static _Noreturn void sql_failed(Db *d, const char *sql, const char *why)
{
	char say[600];
	snprintf(say, sizeof say, "%s", why);
	binds_clear();
	rt_panic(NULL, 0, 0, "the database '%s' can't do \"%s\": %s", d->path, sql, say);
}

/* steps through one statement, keeping its rows as `keep` says: how many
 * rows (it's finalized by the caller) */
static int64_t step_rows(Db *d, sqlite3_stmt *st, const char *sql, int keep)
{
	int64_t rows = 0;
	int cols = sq.column_count(st);
	if (cols && keep) {
		results_clear();
		if (records)
			cby_json_release(records);
		records = keep == KEEP_RECORDS ? cby_json_new(JSON_LIST) : 0;
		if (first_bytes)
			cby_bytes_release(first_bytes);
		first_bytes = 0;
		ncolumns = cols;
		for (int c = 0; c < cols; c++) {
			const char *name = sq.column_name(st, c);
			result_n(name ? name : "", name ? strlen(name) : 0);
		}
	}
	int r;
	while ((r = sq.step(st)) == SQ_ROW) {
		if (!keep || !cols)
			continue;
		if (keep == KEEP_RECORDS) {
			int64_t row = cby_json_new(JSON_LOOKUP);
			for (int c = 0; c < cols; c++) {
				int64_t v = json_value(st, c);
				cby_json_set(row, res[c], v);
				cby_json_release(v);
			}
			cby_json_add(records, row);
			cby_json_release(row);
		} else if (keep == KEEP_BYTES) {
			if (rows == 0) {
				const void *blob = sq.column_type(st, 0) == SQ_NULL ? NULL : sq.column_blob(st, 0);
				first_bytes = rt_bytes_make(blob, blob ? sq.column_bytes(st, 0) : 0);
			}
		} else {
			for (int c = 0; c < cols; c++) {
				const unsigned char *t = sq.column_type(st, c) == SQ_NULL ? NULL : sq.column_text(st, c);
				result_n(t ? (const char *)t : "", t ? (size_t)sq.column_bytes(st, c) : 0);
			}
		}
		rows++;
	}
	if (r != SQ_DONE) {
		char why[400];
		snprintf(why, sizeof why, "%s", sq.errmsg(d->db));
		sq.finalize(st);
		sql_failed(d, sql, why);
	}
	return rows;
}

/* the statements in sql, one after another, the values given to the ?s of
 * the first that has any; the rows of the last that gives rows kept as
 * `keep` says (text: the columns' names, then each row's values). How
 * many rows. */
static int64_t pg_run(Db *d, const char *sql, int keep);

int64_t cby_databases_run(int64_t id, const char *sql, int keep)
{
	Db *d = database(id);
	if (d->pg)
		return pg_run(d, sql, keep);
	results_clear();
	int64_t rows = 0;
	const char *rest = sql;
	int bound = 0;
	while (rest && *rest) {
		sqlite3_stmt *st = NULL;
		const char *tail = NULL;
		if (sq.prepare_v2(d->db, rest, -1, &st, &tail) != SQ_OK)
			sql_failed(d, sql, sq.errmsg(d->db));
		rest = tail;
		if (!st)
			continue;   /* (only spaces, or a comment) */
		int wants = sq.bind_parameter_count(st);
		if (wants && !bound) {
			if (wants != given.n) {
				char why[120];
				snprintf(why, sizeof why, "it has %d ?s, and %d values were given", wants, given.n);
				sq.finalize(st);
				sql_failed(d, sql, why);
			}
			bind_all(st, &given);
			bound = 1;
		}
		int64_t got = step_rows(d, st, sql, keep);
		if (sq.column_count(st) && keep)
			rows = got;
		sq.finalize(st);
	}
	if (given.n && !bound)
		sql_failed(d, sql, "values were given, but it has no ?s to put them in");
	binds_clear();
	return rows;
}

/* ------------------------------------------------------------ statements made once */

typedef struct {
	sqlite3_stmt *st;
	int64_t db;
	Binds values;   /* for its ?s, the next time it's run */
	char *pg_name;  /* PostgreSQL's: its name there, its SQL, and how many ?s */
	char *pg_sql;
	int pg_wants;
} Statement;

static Statement *pg_prepare(Db *d, int64_t id, const char *sql);
static int64_t pg_statement_run(Statement *s, Db *d, int keep);

static Statement **stmts;
static int nstmts;

/* one statement, ready to run as many times as it's asked (with new
 * values for its ?s each time): quicker than running its SQL each time */
static int64_t keep_statement(Statement *s);

int64_t cby_databases_prepare(int64_t id, const char *sql)
{
	Db *d = database(id);
	if (d->pg)
		return keep_statement(pg_prepare(d, id, sql));
	sqlite3_stmt *st = NULL;
	const char *tail = NULL;
	if (sq.prepare_v2(d->db, sql, -1, &st, &tail) != SQ_OK)
		sql_failed(d, sql, sq.errmsg(d->db));
	if (!st)
		sql_failed(d, sql, "there's no statement in it");
	while (tail && (*tail == ' ' || *tail == '\t' || *tail == '\n' || *tail == '\r' || *tail == ';'))
		tail++;
	if (tail && *tail) {
		sq.finalize(st);
		sql_failed(d, sql, "a statement made once can only be one statement (this has more, after a ;)");
	}
	Statement *s = calloc(1, sizeof *s);
	s->st = st;
	s->db = id;
	return keep_statement(s);
}

static int64_t keep_statement(Statement *s)
{
	int i = 0;
	while (i < nstmts && stmts[i])
		i++;
	if (i == nstmts) {
		stmts = realloc(stmts, sizeof *stmts * (size_t)(nstmts + 1));
		nstmts++;
	}
	stmts[i] = s;
	return i + 1;
}

static Statement *statement(int64_t id)
{
	if (id < 1 || id > nstmts || !stmts[id - 1])
		rt_panic(NULL, 0, 0, "that statement has been dropped");
	return stmts[id - 1];
}

/* a value for its next ? */
void cby_databases_statement_text(int64_t id, const char *v) { add_text(&statement(id)->values, v); }
void cby_databases_statement_bytes(int64_t id, int64_t h) { add_bytes(&statement(id)->values, h); }
void cby_databases_statement_number(int64_t id, double v) { new_bind(&statement(id)->values, B_NUMBER)->number = v; }
void cby_databases_statement_whole(int64_t id, int64_t v) { new_bind(&statement(id)->values, B_WHOLE)->whole = v; }
void cby_databases_statement_nothing(int64_t id) { new_bind(&statement(id)->values, B_NOTHING); }

/* runs it, with the values given since (none, if it has no ?s) */
int64_t cby_databases_statement_run(int64_t id, int keep)
{
	Statement *s = statement(id);
	Db *d = database(s->db);
	if (d->pg)
		return pg_statement_run(s, d, keep);
	const char *sql = sq.sql(s->st);
	results_clear();
	int wants = sq.bind_parameter_count(s->st);
	if (wants != s->values.n) {
		char why[120];
		snprintf(why, sizeof why, "it has %d ?s, and %d values were given", wants, s->values.n);
		binds_clear_list(&s->values);
		sq.reset(s->st);
		sql_failed(d, sql ? sql : "", why);
	}
	sq.clear_bindings(s->st);
	bind_all(s->st, &s->values);
	binds_clear_list(&s->values);
	int64_t rows = step_rows(d, s->st, sql ? sql : "", keep);
	sq.reset(s->st);
	return rows;
}

void cby_databases_statement_free(int64_t id)
{
	if (id >= 1 && id <= nstmts && stmts[id - 1]) {
		if (sq.finalize && stmts[id - 1]->st)
			sq.finalize(stmts[id - 1]->st);
		free(stmts[id - 1]->pg_name);
		free(stmts[id - 1]->pg_sql);
		binds_clear_list(&stmts[id - 1]->values);
		free(stmts[id - 1]->values.b);
		free(stmts[id - 1]);
		stmts[id - 1] = NULL;
	}
}

static int64_t pg_last_id(Db *d);

int64_t cby_databases_changes(int64_t id)
{
	Db *d = database(id);
	return d->pg ? d->pg_changes : sq.changes(d->db);
}

int64_t cby_databases_last_id(int64_t id)
{
	Db *d = database(id);
	return d->pg ? pg_last_id(d) : sq.last_insert_rowid(d->db);
}

#include "postgres.h"

__attribute__((destructor)) static void databases_end(void)
{
	for (int i = 0; i < nstmts; i++)
		cby_databases_statement_free(i + 1);
	free(stmts);
	if (records)
		cby_json_release(records);
	if (first_bytes)
		cby_bytes_release(first_bytes);
	for (int i = 0; i < ndbs; i++)
		cby_databases_close(i + 1);
	free(dbs);
	results_clear();
	free(res);
	free(reslen);
	binds_clear();
	free(given.b);
}

/* a worker's thread, ending: what it kept for itself */
static void databases_thread_end(void)
{
	results_clear();
	free(res);
	free(reslen);
	res = NULL;
	reslen = NULL;
	capres = 0;
	binds_clear();
	free(given.b);
	given.b = NULL;
	given.cap = 0;
	if (records)
		cby_json_release(records);
	records = 0;
	if (first_bytes)
		cby_bytes_release(first_bytes);
	first_bytes = 0;
}
__attribute__((constructor)) static void databases_threads(void) { rt_on_thread_end(databases_thread_end); }
