/* postgres.h — PostgreSQL, for the databases library (included by
 * databases.c, which it shares its results, values and statements with).
 *
 * A PostgreSQL database is a server's, reached through libpq, the
 * client library every PostgreSQL comes with; it's loaded the first time
 * a program connects to one. SQL goes as it is, but for its ?s, which
 * become $1, $2 ... (those inside quotes, or a comment, are left alone);
 * values go separately, as text (bytes as bytes), and the server makes
 * them the types its columns need. What comes back is text, as SQLite's
 * rows are; for records, numbers are made numbers and true and false
 * true and false, by the columns' types. */

typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;

enum { PG_CONNECTION_OK = 0, PG_COMMAND_OK = 1, PG_TUPLES_OK = 2 };

/* the types of columns whose values are made more than text in records */
enum { OID_BOOL = 16, OID_BYTEA = 17, OID_INT8 = 20, OID_INT2 = 21, OID_INT4 = 23, OID_OID = 26, OID_FLOAT4 = 700,
       OID_FLOAT8 = 701, OID_NUMERIC = 1700 };

static struct {
	int tried, ok;
	PGconn *(*connectdb)(const char *);
	int (*status)(const PGconn *);
	char *(*error_message)(const PGconn *);
	void (*finish)(PGconn *);
	PGresult *(*exec)(PGconn *, const char *);
	PGresult *(*exec_params)(PGconn *, const char *, int, const unsigned *, const char *const *, const int *, const int *, int);
	PGresult *(*prepare)(PGconn *, const char *, const char *, int, const unsigned *);
	PGresult *(*exec_prepared)(PGconn *, const char *, int, const char *const *, const int *, const int *, int);
	int (*result_status)(const PGresult *);
	char *(*result_error)(const PGresult *);
	int (*ntuples)(const PGresult *);
	int (*nfields)(const PGresult *);
	char *(*fname)(const PGresult *, int);
	unsigned (*ftype)(const PGresult *, int);
	char *(*getvalue)(const PGresult *, int, int);
	int (*getlength)(const PGresult *, int, int);
	int (*getisnull)(const PGresult *, int, int);
	char *(*cmd_tuples)(PGresult *);
	void (*clear)(PGresult *);
	char *(*db)(const PGconn *);
	void *(*set_notice_processor)(PGconn *, void (*)(void *, const char *), void *);
} pq;

static void pg_need(void)
{
	if (!pq.tried) {
		pq.tried = 1;
		static const char *const names[] = { "libpq.so.5", "libpq.so",
						     /* (a Mac: Homebrew's, on an Apple processor or Intel, or Postgres.app's) */
						     "/opt/homebrew/opt/libpq/lib/libpq.5.dylib", "/opt/homebrew/lib/libpq.5.dylib",
						     "/usr/local/opt/libpq/lib/libpq.5.dylib", "/usr/local/lib/libpq.5.dylib",
						     "/Applications/Postgres.app/Contents/Versions/latest/lib/libpq.5.dylib", NULL };
		void *h = NULL;
		const char *own = getenv("CABBY_LIBPQ");
		if (own && *own)
			h = dlopen(own, RTLD_NOW | RTLD_LOCAL);
		for (int i = 0; names[i] && !h; i++)
			h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (h) {
#define LOAD(field, name) do { void *p_ = dlsym(h, name); memcpy(&pq.field, &p_, sizeof p_); } while (0)
			LOAD(connectdb, "PQconnectdb");
			LOAD(status, "PQstatus");
			LOAD(error_message, "PQerrorMessage");
			LOAD(finish, "PQfinish");
			LOAD(exec, "PQexec");
			LOAD(exec_params, "PQexecParams");
			LOAD(prepare, "PQprepare");
			LOAD(exec_prepared, "PQexecPrepared");
			LOAD(result_status, "PQresultStatus");
			LOAD(result_error, "PQresultErrorMessage");
			LOAD(ntuples, "PQntuples");
			LOAD(nfields, "PQnfields");
			LOAD(fname, "PQfname");
			LOAD(ftype, "PQftype");
			LOAD(getvalue, "PQgetvalue");
			LOAD(getlength, "PQgetlength");
			LOAD(getisnull, "PQgetisnull");
			LOAD(cmd_tuples, "PQcmdTuples");
			LOAD(clear, "PQclear");
			LOAD(db, "PQdb");
			LOAD(set_notice_processor, "PQsetNoticeProcessor");
#undef LOAD
			pq.ok = pq.connectdb && pq.status && pq.error_message && pq.finish && pq.exec && pq.exec_params && pq.prepare &&
				pq.exec_prepared && pq.result_status && pq.result_error && pq.ntuples && pq.nfields && pq.fname &&
				pq.ftype && pq.getvalue && pq.getlength && pq.getisnull && pq.cmd_tuples && pq.clear && pq.db &&
				pq.set_notice_processor;
		}
	}
	if (!pq.ok)
		rt_panic(NULL, 0, 0, "%s",
#if defined(__APPLE__)
			 "PostgreSQL needs its client library, libpq: brew install libpq"
#else
			 "PostgreSQL needs its client library, libpq (the 'postgresql-libs' package on Arch, 'libpq5' on Debian)"
#endif
		);
}

static void pg_quietly(void *data, const char *message) { (void)data, (void)message; }

/* the end of a message (libpq's have a line end, or two, after them) */
static char *pg_trimmed(const char *s)
{
	char *t = strdup(s ? s : "");
	size_t n = strlen(t);
	while (n && (t[n - 1] == '\n' || t[n - 1] == ' '))
		t[--n] = 0;
	return t;
}

/* a server's database, connected to: "postgresql://tom:secret@localhost/shop",
 * or "host=localhost dbname=shop user=tom" (libpq's own two ways) */
int64_t cby_databases_connect(const char *where)
{
	pg_need();
	bool was = rt_gil_drop();
	PGconn *c = pq.connectdb(where);
	rt_gil_take(was);
	if (!c || pq.status(c) != PG_CONNECTION_OK) {
		char *why = pg_trimmed(c ? pq.error_message(c) : "it couldn't be reached");
		if (c)
			pq.finish(c);
		/* (never the password: only what's after it, if it's a URL) */
		const char *at = strrchr(where, '@');
		char shown[300];
		snprintf(shown, sizeof shown, "%s", strstr(where, "://") && at ? at + 1 : where);
		if (!strstr(where, "://") && strstr(shown, "password"))
			snprintf(shown, sizeof shown, "%s", "(that server)");
		for (char *w = why; *w; w++)   /* (its lines, as one) */
			if (*w == '\n' || *w == '\t')
				*w = ' ';
		char say[700];
		snprintf(say, sizeof say, "%s", why);
		free(why);
		rt_panic(NULL, 0, 0, "can't connect to the PostgreSQL database %s: %s", shown, say);
	}
	pq.set_notice_processor(c, pg_quietly, NULL);   /* ("NOTICE: relation already exists, skipping") */
	Db *d = calloc(1, sizeof *d);
	d->pg = c;
	char name[300];
	snprintf(name, sizeof name, "%s", pq.db(c) ? pq.db(c) : "");
	d->path = strdup(name);
	return keep_db(d);
}

static void pg_close(void *conn)
{
	if (pq.finish)
		pq.finish(conn);
}

/* the SQL with its ?s made $1, $2 ...; how many in *count. A ? inside
 * '...', "...", a $tag$...$tag$ or a comment stays a ?. */
static char *pg_dollars(const char *sql, int *count)
{
	size_t n = strlen(sql);
	char *out = malloc(n * 4 + 16), *o = out;
	int k = 0;
	for (size_t i = 0; i < n;) {
		char c = sql[i];
		if (c == '\'' || c == '"') {
			size_t j = i + 1;
			while (j < n && !(sql[j] == c && !(j + 1 < n && sql[j + 1] == c)))
				j += sql[j] == c ? 2 : 1;
			j = j < n ? j + 1 : n;
			memcpy(o, sql + i, j - i);
			o += j - i;
			i = j;
		} else if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
			size_t j = i;
			while (j < n && sql[j] != '\n')
				j++;
			memcpy(o, sql + i, j - i);
			o += j - i;
			i = j;
		} else if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
			const char *e = strstr(sql + i + 2, "*/");
			size_t j = e ? (size_t)(e - sql) + 2 : n;
			memcpy(o, sql + i, j - i);
			o += j - i;
			i = j;
		} else if (c == '$' && (i == 0 || !(isalnum((unsigned char)sql[i - 1]) || sql[i - 1] == '_'))) {
			/* $tag$ ... $tag$ (or $$ ... $$): a function's body, as it is */
			size_t t = i + 1;
			while (t < n && (isalnum((unsigned char)sql[t]) || sql[t] == '_'))
				t++;
			if (t < n && sql[t] == '$' && !(t > i + 1 && isdigit((unsigned char)sql[i + 1]))) {
				size_t taglen = t - i + 1;
				const char *e = sql + t + 1;
				while ((e = strstr(e, "$")) && strncmp(e, sql + i, taglen) != 0)
					e++;
				size_t j = e ? (size_t)(e - sql) + taglen : n;
				memcpy(o, sql + i, j - i);
				o += j - i;
				i = j;
			} else {
				*o++ = c;
				i++;
			}
		} else if (c == '?') {
			o += sprintf(o, "$%d", ++k);
			i++;
		} else {
			*o++ = c;
			i++;
		}
	}
	*o = 0;
	*count = k;
	return out;
}

/* the values, as libpq takes them: text, or bytes (sent as they are) */
typedef struct {
	const char **values;
	int *lengths, *formats;
	char **made;   /* (numbers, made text) */
} PgValues;

static PgValues pg_values(const Binds *l)
{
	PgValues v;
	int n = l->n ? l->n : 1;
	v.values = calloc((size_t)n, sizeof *v.values);
	v.lengths = calloc((size_t)n, sizeof *v.lengths);
	v.formats = calloc((size_t)n, sizeof *v.formats);
	v.made = calloc((size_t)n, sizeof *v.made);
	for (int i = 0; i < l->n; i++) {
		const Bind *b = &l->b[i];
		char num[64];
		switch (b->kind) {
		case B_TEXT: v.values[i] = b->data; break;
		case B_BYTES:
			v.values[i] = b->data;
			v.lengths[i] = (int)b->len;
			v.formats[i] = 1;
			break;
		case B_NUMBER:
			snprintf(num, sizeof num, "%.17g", b->number);
			v.values[i] = v.made[i] = strdup(num);
			break;
		case B_WHOLE:
			snprintf(num, sizeof num, "%lld", (long long)b->whole);
			v.values[i] = v.made[i] = strdup(num);
			break;
		default: v.values[i] = NULL;
		}
	}
	return v;
}

static void pg_values_free(PgValues *v, int n)
{
	for (int i = 0; i < n; i++)
		free(v->made[i]);
	free(v->made);
	free(v->values);
	free(v->lengths);
	free(v->formats);
}

/* a bytea's text ("\x48656c6c6f") as its bytes */
static int64_t pg_bytes(const char *t, int len)
{
	if (len >= 2 && t[0] == '\\' && t[1] == 'x') {
		size_t n = (size_t)(len - 2) / 2;
		unsigned char *b = malloc(n + 1);
		for (size_t i = 0; i < n; i++) {
			unsigned v = 0;
			sscanf(t + 2 + 2 * i, "%2x", &v);
			b[i] = (unsigned char)v;
		}
		int64_t h = rt_bytes_make(b, (int64_t)n);
		free(b);
		return h;
	}
	return rt_bytes_make(t, len);
}

static int64_t pg_json_value(PGresult *r, int row, int c)
{
	if (pq.getisnull(r, row, c))
		return cby_json_new(JSON_NULL);
	const char *t = pq.getvalue(r, row, c);
	switch (pq.ftype(r, c)) {
	case OID_INT2: case OID_INT4: case OID_INT8: case OID_OID: case OID_FLOAT4: case OID_FLOAT8: case OID_NUMERIC:
		return cby_json_from_double(strtod(t, NULL));
	case OID_BOOL:
		return cby_json_from_bool(t[0] == 't');
	case OID_BYTEA:
		return cby_json_from_str(t[0] == '\\' && t[1] == 'x' ? t + 2 : t);   /* (in hexadecimal, as SQLite's are) */
	default:
		return cby_json_from_str(t);
	}
}

/* a result: kept as `keep` says, as step_rows keeps SQLite's (or a panic
 * if it went wrong); how many rows */
static int64_t pg_result(Db *d, PGresult *r, const char *sql, int keep)
{
	int st = r ? pq.result_status(r) : -1;
	if (st != PG_COMMAND_OK && st != PG_TUPLES_OK) {
		char *why = pg_trimmed(r ? pq.result_error(r) : pq.error_message(d->pg));
		if (r)
			pq.clear(r);
		char *nl = strchr(why, '\n');   /* (its first line: "LINE 1: ..." and a ^ follow) */
		if (nl)
			*nl = 0;
		char say[600];
		snprintf(say, sizeof say, "%s", strncmp(why, "ERROR:  ", 8) == 0 ? why + 8 : why);
		free(why);
		sql_failed(d, sql, say);
	}
	const char *changed = pq.cmd_tuples(r);
	d->pg_changes = changed && *changed ? strtoll(changed, NULL, 10) : 0;
	int rows = pq.ntuples(r), cols = pq.nfields(r);
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
			const char *name = pq.fname(r, c);
			result_n(name ? name : "", name ? strlen(name) : 0);
		}
		for (int row = 0; row < rows; row++) {
			if (keep == KEEP_RECORDS) {
				int64_t one = cby_json_new(JSON_LOOKUP);
				for (int c = 0; c < cols; c++) {
					int64_t v = pg_json_value(r, row, c);
					cby_json_set(one, res[c], v);
					cby_json_release(v);
				}
				cby_json_add(records, one);
				cby_json_release(one);
			} else if (keep == KEEP_BYTES) {
				if (row == 0)
					first_bytes = pq.getisnull(r, 0, 0) ? rt_bytes_make(NULL, 0)
									    : pg_bytes(pq.getvalue(r, 0, 0), pq.getlength(r, 0, 0));
			} else {
				for (int c = 0; c < cols; c++)
					result_n(pq.getvalue(r, row, c), (size_t)pq.getlength(r, row, c));
			}
		}
	}
	pq.clear(r);
	return cols ? rows : 0;
}

static int64_t pg_run(Db *d, const char *sql, int keep)
{
	results_clear();
	int wants = 0;
	char *dollars = pg_dollars(sql, &wants);
	if (wants != given.n) {
		char why[120];
		if (!wants)
			snprintf(why, sizeof why, "%s", "values were given, but it has no ?s to put them in");
		else
			snprintf(why, sizeof why, "it has %d ?s, and %d values were given", wants, given.n);
		free(dollars);
		sql_failed(d, sql, why);
	}
	PGresult *r;
	bool was = rt_gil_drop();
	if (!wants) {
		r = pq.exec(d->pg, dollars);   /* (several statements, with ; between them, all run) */
	} else {
		PgValues v = pg_values(&given);
		r = pq.exec_params(d->pg, dollars, given.n, NULL, v.values, v.lengths, v.formats, 0);
		pg_values_free(&v, given.n);
	}
	rt_gil_take(was);
	free(dollars);
	binds_clear();
	return pg_result(d, r, sql, keep);
}

static Statement *pg_prepare(Db *d, int64_t id, const char *sql)
{
	int wants = 0;
	char *dollars = pg_dollars(sql, &wants);
	char name[48];
	snprintf(name, sizeof name, "cabby_%d", ++d->pg_statements);
	bool was = rt_gil_drop();
	PGresult *r = pq.prepare(d->pg, name, dollars, wants, NULL);
	rt_gil_take(was);
	free(dollars);
	pg_result(d, r, sql, 0);
	Statement *s = calloc(1, sizeof *s);
	s->db = id;
	s->pg_name = strdup(name);
	s->pg_sql = strdup(sql);
	s->pg_wants = wants;
	return s;
}

static int64_t pg_statement_run(Statement *s, Db *d, int keep)
{
	results_clear();
	if (s->pg_wants != s->values.n) {
		char why[120];
		snprintf(why, sizeof why, "it has %d ?s, and %d values were given", s->pg_wants, s->values.n);
		binds_clear_list(&s->values);
		sql_failed(d, s->pg_sql, why);
	}
	PgValues v = pg_values(&s->values);
	bool was = rt_gil_drop();
	PGresult *r = pq.exec_prepared(d->pg, s->pg_name, s->values.n, v.values, v.lengths, v.formats, 0);
	rt_gil_take(was);
	pg_values_free(&v, s->values.n);
	binds_clear_list(&s->values);
	return pg_result(d, r, s->pg_sql, keep);
}

/* the number the last row put in was given, by a serial (or identity)
 * column: PostgreSQL's lastval() */
static int64_t pg_last_id(Db *d)
{
	int64_t changes = d->pg_changes;
	bool was = rt_gil_drop();
	PGresult *r = pq.exec(d->pg, "select lastval()");
	rt_gil_take(was);
	int st = r ? pq.result_status(r) : -1;
	int64_t id = 0;
	bool ok = st == PG_TUPLES_OK && pq.ntuples(r) == 1;
	if (ok)
		id = strtoll(pq.getvalue(r, 0, 0), NULL, 10);
	if (r)
		pq.clear(r);
	d->pg_changes = changes;
	if (!ok)
		sql_failed(d, "select lastval()",
			   "no row has been given a number yet (a serial column's), in this connection: "
			   "\"insert ... returning id\" gives it too");
	return id;
}
