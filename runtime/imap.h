/* imap.h — reading a mailbox, for the emails library's emails.mailbox
 * (included by connections.c, after tls.h): IMAP4rev1, as RFC 3501 says,
 * over a connection of the networks library's own, encrypted (TLS from
 * the start, on 993, or STARTTLS, on 143) unless a test says not.
 *
 * Each command is sent with a tag ("c12 UID SEARCH UNSEEN"), and what
 * comes back is read until the line with that tag: untagged lines
 * ("* SEARCH 4 7 9") kept for the command to read, with any literal in
 * them ({1234}, then that many bytes: a message) read whole. The messages
 * themselves are read by emails.c (cby_email_read). */

int64_t cby_email_read(const char *raw, int64_t len);
void cby_email_got_set(int64_t id, int64_t uid, int64_t size, int seen, int flagged);

typedef struct {
	char **lines;          /* the untagged answers, literals in them */
	size_t *lens;
	int n;
	char *said;            /* the tagged line's text: "OK", "NO [AUTHENTICATIONFAILED] ..." */
	int next_tag;
	char *selected;        /* the folder selected */
	int64_t *found;        /* SEARCH's numbers */
	int nfound;
	char **folders;
	int nfolders;
} Imap;

static Imap **imaps;
static int64_t nimaps;   /* (by connection number) */

static Imap *imap_of(int64_t id)
{
	if (id < 1 || id > nimaps || !imaps[id - 1] || !conn(id) || conn(id)->fd < 0)
		rt_panic(NULL, 0, 0, "%s", "the mailbox isn't open (its connection has closed)");
	return imaps[id - 1];
}

static void imap_clear(Imap *m)
{
	for (int i = 0; i < m->n; i++)
		free(m->lines[i]);
	free(m->lines);
	free(m->lens);
	m->lines = NULL;
	m->lens = NULL;
	m->n = 0;
}

/* one answer from the server, a line, and any literal in it read whole
 * (and the line after it, on: an answer goes on after its literal): NULL
 * if the connection closes */
static char *imap_answer(Conn *c, size_t *len)
{
	Buf b = { 0 };
	for (;;) {
		/* a line */
		char *nl = NULL;
		while (!(nl = c->in_len ? memchr(c->in, '\n', c->in_len) : NULL)) {
			bool was = rt_gil_drop();
			bool more = c->fd >= 0 && fill(c, true);
			rt_gil_take(was);
			if (!more) {
				free(b.d);
				return NULL;
			}
		}
		size_t ll = (size_t)(nl - c->in) + 1;
		put_n(&b, c->in, ll);
		memmove(c->in, c->in + ll, c->in_len - ll);
		c->in_len -= ll;
		/* "... {123}\r\n": that many bytes come next */
		size_t bl = b.len;
		while (bl && (b.d[bl - 1] == '\n' || b.d[bl - 1] == '\r'))
			bl--;
		if (!bl || b.d[bl - 1] != '}')
			break;
		size_t open = bl - 1;
		while (open > 0 && b.d[open - 1] != '{')
			open--;
		if (!open)
			break;
		char *end;
		unsigned long long want = strtoull(b.d + open, &end, 10);
		if (end != b.d + bl - 1)
			break;
		while (c->in_len < want) {
			bool was = rt_gil_drop();
			bool more = c->fd >= 0 && fill(c, true);
			rt_gil_take(was);
			if (!more) {
				free(b.d);
				return NULL;
			}
		}
		put_n(&b, c->in, (size_t)want);
		memmove(c->in, c->in + want, c->in_len - want);
		c->in_len -= want;
	}
	*len = b.len;
	return b.d;
}

/* a command: true if the server said OK. Its untagged answers are kept,
 * and why it didn't, in the error. A command ending in a literal ({123})
 * has it sent when the server says to go on ("+"). */
static bool imap_run_with(int64_t id, const char *command, const char *literal, size_t literal_len)
{
	Imap *m = imap_of(id);
	Conn *c = conn(id);
	imap_clear(m);
	char tag[16];
	snprintf(tag, sizeof tag, "c%d", ++m->next_tag);
	size_t n = strlen(command) + 32;
	char *line = malloc(n);
	snprintf(line, n, "%s %s\r\n", tag, command);
	bool sent = cby_conn_send(id, line, (int64_t)strlen(line));
	free(line);
	if (!sent) {
		say_error("%s%s", "the mailbox's connection has closed", "");
		return false;
	}
	size_t tl = strlen(tag);
	for (;;) {
		size_t len;
		char *a = imap_answer(c, &len);
		if (!a) {
			say_error("%s%s", "the mailbox's connection closed", "");
			return false;
		}
		if (strncmp(a, tag, tl) == 0 && a[tl] == ' ') {
			char *t = a + tl + 1;
			size_t e = strcspn(t, "\r\n");
			t[e] = 0;
			free(m->said);
			m->said = strdup(t);
			bool ok = strncasecmp(t, "OK", 2) == 0;
			if (!ok) {
				/* "NO [AUTHENTICATIONFAILED] Invalid credentials": what it said */
				const char *why = t + strcspn(t, " ");
				while (*why == ' ')
					why++;
				say_error("the mailbox said: %s%s", *why ? why : t, "");
			}
			free(a);
			return ok;
		}
		if (a[0] == '+') {
			free(a);   /* (a continuation: what's waiting to be sent, if anything) */
			if (literal) {
				bool sent_all = cby_conn_send(id, literal, (int64_t)literal_len) && cby_conn_send(id, "\r\n", 2);
				literal = NULL;
				if (!sent_all) {
					say_error("%s%s", "the mailbox's connection has closed", "");
					return false;
				}
			}
			continue;
		}
		m->lines = realloc(m->lines, sizeof *m->lines * (size_t)(m->n + 1));
		m->lens = realloc(m->lens, sizeof *m->lens * (size_t)(m->n + 1));
		m->lines[m->n] = a;
		m->lens[m->n++] = len;
	}
}

static bool imap_run(int64_t id, const char *command) { return imap_run_with(id, command, NULL, 0); }

/* text in a command, quoted as IMAP quotes it ("a \"b\"") */
static char *imap_quote(const char *s)
{
	Buf b = { 0 };
	put(&b, "\"");
	for (const char *p = s; *p; p++) {
		if (*p == '\r' || *p == '\n')
			continue;
		if (*p == '"' || *p == '\\')
			put(&b, "\\");
		put_n(&b, p, 1);
	}
	put(&b, "\"");
	return b.d;
}

static void imap_forget(int64_t id)
{
	if (id < 1 || id > nimaps || !imaps[id - 1])
		return;
	Imap *m = imaps[id - 1];
	imap_clear(m);
	free(m->said);
	free(m->selected);
	free(m->found);
	for (int i = 0; i < m->nfolders; i++)
		free(m->folders[i]);
	free(m->folders);
	free(m);
	imaps[id - 1] = NULL;
}

/* a mailbox opened, and logged in to: its number (its connection's), or
 * 0 (cby_conn_error says why). security: 0 TLS from the start, 1
 * STARTTLS, 2 none (a test's own server) */
int64_t cby_imap_open(const char *host, int64_t port, int64_t security, const char *user, const char *password)
{
	int64_t id = cby_conn_open(host, port);
	if (!id)
		return 0;
	/* (a server that says nothing for a minute: given up on, not waited
	 * for always, as one expecting TLS when this end isn't would be) */
	struct timeval minute = { 60, 0 };
	setsockopt(conn(id)->fd, SOL_SOCKET, SO_RCVTIMEO, &minute, sizeof minute);
	setsockopt(conn(id)->fd, SOL_SOCKET, SO_SNDTIMEO, &minute, sizeof minute);
	if (security == 0 && !tls_connect(id, host)) {
		cby_conn_free(id);
		return 0;
	}
	Imap *m = calloc(1, sizeof *m);
	while (nimaps < id) {
		imaps = realloc(imaps, sizeof *imaps * (size_t)(nimaps + 1));
		imaps[nimaps++] = NULL;
	}
	free(imaps[id - 1]);
	imaps[id - 1] = m;
	size_t len;
	char *hello = imap_answer(conn(id), &len);
	if (!hello || strncmp(hello, "* OK", 4) != 0) {
		say_error("%s%s", "that isn't an email server (IMAP) answering", "");
		free(hello);
		imap_forget(id);
		cby_conn_free(id);
		return 0;
	}
	free(hello);
	if (security == 1) {
		if (!imap_run(id, "STARTTLS") || !tls_connect(id, host)) {
			imap_forget(id);
		cby_conn_free(id);
			return 0;
		}
	}
	char *u = imap_quote(user), *p = imap_quote(password);
	size_t n = strlen(u) + strlen(p) + 16;
	char *login = malloc(n);
	snprintf(login, n, "LOGIN %s %s", u, p);
	bool ok = imap_run(id, login);
	free(login);
	free(u);
	free(p);
	if (!ok) {
		imap_forget(id);
		cby_conn_free(id);
		return 0;
	}
	return id;
}

void cby_imap_close(int64_t id)
{
	if (id < 1 || id > nimaps || !imaps[id - 1])
		return;
	if (conn(id)->fd >= 0)
		imap_run(id, "LOGOUT");
	imap_forget(id);
	cby_conn_free(id);
}

/* a word of an answer, from p: an atom, a "quoted" one, or a literal
 * (whose bytes follow its {n} and line end); where it ends */
static char *imap_word(const char *p, const char **after)
{
	while (*p == ' ')
		p++;
	if (*p == '"') {
		Buf b = { 0 };
		for (p++; *p && *p != '"'; p++) {
			if (*p == '\\' && p[1])
				p++;
			put_n(&b, p, 1);
		}
		*after = *p ? p + 1 : p;
		return b.d ? b.d : strdup("");
	}
	if (*p == '{') {
		char *e;
		unsigned long n = strtoul(p + 1, &e, 10);
		const char *start = strchr(e, '\n');
		start = start ? start + 1 : e;
		*after = start + n;
		return strndup(start, n);
	}
	size_t n = strcspn(p, " \r\n)");
	*after = p + n;
	return strndup(p, n);
}

/* the folders: their count */
int64_t cby_imap_folders(int64_t id)
{
	Imap *m = imap_of(id);
	for (int i = 0; i < m->nfolders; i++)
		free(m->folders[i]);
	m->nfolders = 0;
	if (!imap_run(id, "LIST \"\" \"*\""))
		return -1;
	for (int i = 0; i < m->n; i++) {
		/* * LIST (\HasNoChildren) "/" "INBOX" */
		const char *l = m->lines[i];
		if (strncasecmp(l, "* LIST ", 7) != 0)
			continue;
		const char *close_ = strchr(l, ')');
		if (!close_)
			continue;
		if (strcasestr_portable(l, "\\Noselect", close_))
			continue;   /* (a folder that only holds folders) */
		const char *after;
		char *delim = imap_word(close_ + 1, &after);
		free(delim);
		char *name = imap_word(after, &after);
		m->folders = realloc(m->folders, sizeof *m->folders * (size_t)(m->nfolders + 1));
		m->folders[m->nfolders++] = name;
	}
	return m->nfolders;
}

int64_t cby_imap_folder(int64_t id, int64_t i)
{
	Imap *m = imap_of(id);
	return i >= 0 && i < m->nfolders ? give(m->folders[i], strlen(m->folders[i])) : give("", 0);
}

/* a folder chosen, to read: true if it's there */
int cby_imap_select(int64_t id, const char *folder)
{
	Imap *m = imap_of(id);
	char *q = imap_quote(folder);
	size_t n = strlen(q) + 16;
	char *cmd = malloc(n);
	snprintf(cmd, n, "SELECT %s", q);
	bool ok = imap_run(id, cmd);
	free(cmd);
	free(q);
	if (ok) {
		free(m->selected);
		m->selected = strdup(folder);
	}
	return ok;
}

/* a folder's count: 0 of its messages, 1 of those unread (-1: it isn't there) */
int64_t cby_imap_count(int64_t id, const char *folder, int64_t what)
{
	Imap *m = imap_of(id);
	char *q = imap_quote(folder);
	size_t n = strlen(q) + 40;
	char *cmd = malloc(n);
	snprintf(cmd, n, "STATUS %s (MESSAGES UNSEEN)", q);
	bool ok = imap_run(id, cmd);
	free(cmd);
	free(q);
	if (!ok)
		return -1;
	for (int i = 0; i < m->n; i++) {
		const char *key = what == 0 ? "MESSAGES " : "UNSEEN ";
		const char *at = strstr(m->lines[i], key);
		if (strncasecmp(m->lines[i], "* STATUS", 8) == 0 && at)
			return strtoll(at + strlen(key), NULL, 10);
	}
	return 0;
}

/* the messages the criteria find (IMAP's: "UNSEEN", "FROM \"ana\"",
 * "ALL"), by number (UID), oldest first: their count */
int64_t cby_imap_search(int64_t id, const char *criteria)
{
	Imap *m = imap_of(id);
	m->nfound = 0;
	size_t n = strlen(criteria) + 32;
	char *cmd = malloc(n);
	snprintf(cmd, n, "UID SEARCH CHARSET UTF-8 %s", criteria);
	bool ok = imap_run(id, cmd);
	if (!ok) {
		/* (a server without CHARSET: as it is) */
		snprintf(cmd, n, "UID SEARCH %s", criteria);
		ok = imap_run(id, cmd);
	}
	free(cmd);
	if (!ok)
		return -1;
	for (int i = 0; i < m->n; i++) {
		if (strncasecmp(m->lines[i], "* SEARCH", 8) != 0)
			continue;
		for (const char *p = m->lines[i] + 8; *p;) {
			char *e;
			long long v = strtoll(p, &e, 10);
			if (e == p) {
				p++;
				continue;
			}
			m->found = realloc(m->found, sizeof *m->found * (size_t)(m->nfound + 1));
			m->found[m->nfound++] = v;
			p = e;
		}
	}
	return m->nfound;
}

int64_t cby_imap_found(int64_t id, int64_t i)
{
	Imap *m = imap_of(id);
	return i >= 0 && i < m->nfound ? m->found[i] : 0;
}

/* a message, fetched whole and read (emails.c's): its number there, or 0
 * if there's none by that number. Fetched with PEEK: it isn't marked read
 * by being read. */
int64_t cby_imap_fetch(int64_t id, int64_t uid)
{
	Imap *m = imap_of(id);
	char cmd[96];
	snprintf(cmd, sizeof cmd, "UID FETCH %lld (UID FLAGS RFC822.SIZE BODY.PEEK[])", (long long)uid);
	if (!imap_run(id, cmd))
		return 0;
	for (int i = 0; i < m->n; i++) {
		/* * 3 FETCH (UID 4 FLAGS (\Seen) RFC822.SIZE 1234 BODY[] {1234}\r\n...) */
		const char *l = m->lines[i];
		const char *body = strstr(l, "BODY[]");
		if (!strstr(l, " FETCH (") || !body)
			continue;
		const char *u = strstr(l, "UID ");
		const char *flags = strstr(l, "FLAGS (");
		const char *sz = strstr(l, "RFC822.SIZE ");
		int seen = 0, flagged = 0;
		if (flags) {
			const char *fe = strchr(flags, ')');
			seen = strcasestr_portable(flags, "\\Seen", fe) != NULL;
			flagged = strcasestr_portable(flags, "\\Flagged", fe) != NULL;
		}
		const char *after;
		const char *open = strchr(body, '{');
		if (!open)
			continue;
		char *raw = imap_word(open, &after);
		size_t rl = 0;
		unsigned long n = strtoul(open + 1, NULL, 10);
		rl = n;
		int64_t got = cby_email_read(raw, (int64_t)rl);
		free(raw);
		cby_email_got_set(got, u ? strtoll(u + 4, NULL, 10) : uid, sz ? strtoll(sz + 12, NULL, 10) : (int64_t)rl, seen, flagged);
		return got;
	}
	return 0;
}

/* a message's flags changed: "+FLAGS (\Seen)", "-FLAGS (\Seen)" */
int cby_imap_store(int64_t id, int64_t uid, const char *change)
{
	char cmd[160];
	snprintf(cmd, sizeof cmd, "UID STORE %lld %s", (long long)uid, change);
	return imap_run(id, cmd);
}

/* moved to another folder (MOVE, or else copied there, and deleted
 * here): true if it was */
int cby_imap_move(int64_t id, int64_t uid, const char *folder)
{
	char *q = imap_quote(folder);
	size_t n = strlen(q) + 48;
	char *cmd = malloc(n);
	snprintf(cmd, n, "UID MOVE %lld %s", (long long)uid, q);
	bool ok = imap_run(id, cmd);
	if (!ok) {
		snprintf(cmd, n, "UID COPY %lld %s", (long long)uid, q);
		ok = imap_run(id, cmd) && cby_imap_store(id, uid, "+FLAGS (\\Deleted)") && imap_run(id, "EXPUNGE");
	}
	free(cmd);
	free(q);
	return ok;
}

/* a message (as it's sent: its headers and body) put into a folder, read
 * already or not: true if it was. Line ends are made \r\n, as IMAP needs. */
int cby_imap_append(int64_t id, const char *folder, const char *raw, int seen)
{
	Buf b = { 0 };
	for (const char *p = raw; *p; p++) {
		if (*p == '\n' && (p == raw || p[-1] != '\r'))
			put_n(&b, "\r", 1);
		put_n(&b, p, 1);
	}
	char *q = imap_quote(folder);
	size_t n = strlen(q) + 64;
	char *cmd = malloc(n);
	snprintf(cmd, n, "APPEND %s (%s) {%zu}", q, seen ? "\\Seen" : "", b.len);
	bool ok = imap_run_with(id, cmd, b.d ? b.d : "", b.len);
	free(cmd);
	free(q);
	free(b.d);
	return ok;
}

/* deleted, for good: true if it was */
int cby_imap_delete(int64_t id, int64_t uid)
{
	return cby_imap_store(id, uid, "+FLAGS (\\Deleted)") && imap_run(id, "EXPUNGE");
}

/* (the program's end: what's left of each) */
static void imaps_end(void)
{
	for (int64_t i = 0; i < nimaps; i++)
		imap_forget(i + 1);
	free(imaps);
	imaps = NULL;
	nimaps = 0;
}
