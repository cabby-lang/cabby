/* emails.c — the emails library's C side: a message made (headers, its
 * text, an HTML version, attachments: MIME, as RFC 5322 and 2045 say), and
 * sent by SMTP, through libcurl (loaded when it's first needed, as the
 * networks library loads it), the same on Linux and macOS.
 *
 * Text goes as it is when it's plain ASCII in short lines, and otherwise
 * quoted-printable (readable in the raw message); attachments go in
 * base64. A subject or name that isn't ASCII is written as RFC 2047 says
 * (=?UTF-8?B?...?=). */

#include "rt.h"
#include <ctype.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef void CURL;
struct curl_slist;

enum {
	E_URL = 10002, E_ERRORBUFFER = 10010, E_READDATA = 10009, E_READFUNCTION = 20012, E_UPLOAD = 46,
	E_USERNAME = 10173, E_PASSWORD = 10174, E_MAIL_FROM = 10186, E_MAIL_RCPT = 10187, E_USE_SSL = 119,
	E_CONNECTTIMEOUT = 78, E_TIMEOUT = 13, E_NOSIGNAL = 99
};

static struct {
	bool tried, ok;
	int (*global_init)(long);
	CURL *(*init)(void);
	int (*setopt)(CURL *, int, ...);
	int (*perform)(CURL *);
	void (*cleanup)(CURL *);
	const char *(*strerror)(int);
	struct curl_slist *(*slist_append)(struct curl_slist *, const char *);
	void (*slist_free_all)(struct curl_slist *);
} ec;

static bool load(void)
{
	if (ec.tried)
		return ec.ok;
	ec.tried = true;
	static const char *const names[] = { "libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4", "libcurl.4.dylib",
					      "libcurl.dylib", NULL };
	void *h = NULL;
	for (int i = 0; names[i] && !h; i++)
		h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!h)
		return false;
#define LOAD(field, name) do { void *p_ = dlsym(h, name); memcpy(&ec.field, &p_, sizeof p_); } while (0)
	LOAD(global_init, "curl_global_init");
	LOAD(init, "curl_easy_init");
	LOAD(setopt, "curl_easy_setopt");
	LOAD(perform, "curl_easy_perform");
	LOAD(cleanup, "curl_easy_cleanup");
	LOAD(strerror, "curl_easy_strerror");
	LOAD(slist_append, "curl_slist_append");
	LOAD(slist_free_all, "curl_slist_free_all");
#undef LOAD
	ec.ok = ec.global_init && ec.init && ec.setopt && ec.perform && ec.cleanup && ec.strerror && ec.slist_append &&
		ec.slist_free_all;
	if (ec.ok)
		ec.global_init(3);
	return ec.ok;
}

/* ------------------------------------------------------------ text, growing */

typedef struct {
	char *d;
	size_t len, cap;
} Buf;

static void put_n(Buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->d = realloc(b->d, b->cap);
	}
	memcpy(b->d + b->len, s, n);
	b->len += n;
	b->d[b->len] = 0;
}

static void put(Buf *b, const char *s) { put_n(b, s, strlen(s)); }

static void putf(Buf *b, const char *fmt, ...)
{
	char tmp[2048];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	put_n(b, tmp, n < (int)sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

/* ------------------------------------------------------------ messages */

typedef struct {
	char *from, *subject, *text, *html, *reply_to;
	char **to, **cc, **bcc, **files;
	int nto, ncc, nbcc, nfiles;
	char *rendered;
} Email;

static Email **emails;
static int nemails;
static char error[600];
static char *handed;

static Email *email(int64_t id)
{
	if (id < 1 || id > nemails || !emails[id - 1])
		rt_panic(NULL, 0, 0, "%s", "that message has gone");
	return emails[id - 1];
}

int64_t cby_email_new(void)
{
	emails = realloc(emails, sizeof *emails * (size_t)(nemails + 1));
	emails[nemails] = calloc(1, sizeof **emails);
	return ++nemails;
}

void cby_email_free(int64_t id)
{
	if (id < 1 || id > nemails || !emails[id - 1])
		return;
	Email *e = emails[id - 1];
	free(e->from);
	free(e->subject);
	free(e->text);
	free(e->html);
	free(e->reply_to);
	char **lists[] = { e->to, e->cc, e->bcc, e->files };
	int counts[] = { e->nto, e->ncc, e->nbcc, e->nfiles };
	for (int k = 0; k < 4; k++) {
		for (int i = 0; i < counts[k]; i++)
			free(lists[k][i]);
		free(lists[k]);
	}
	free(e->rendered);
	free(e);
	emails[id - 1] = NULL;
}

static void no_line_ends(const char *what, const char *s)
{
	if (strpbrk(s, "\r\n"))
		rt_panic(NULL, 0, 0, "a message's %s can't have a line end in it", what);
}

/* 0 from, 1 subject, 2 text, 3 html, 4 reply-to */
void cby_email_set(int64_t id, int64_t what, const char *value)
{
	Email *e = email(id);
	char **field = what == 0 ? &e->from : what == 1 ? &e->subject : what == 2 ? &e->text : what == 3 ? &e->html : &e->reply_to;
	if (what == 0 || what == 1 || what == 4)
		no_line_ends(what == 1 ? "subject" : "address", value);
	free(*field);
	*field = strdup(value);
}

/* 0 to, 1 cc, 2 bcc, 3 an attachment (a file's path) */
void cby_email_add(int64_t id, int64_t what, const char *value)
{
	Email *e = email(id);
	if (what == 3) {
		struct stat st;
		if (stat(value, &st) != 0 || !S_ISREG(st.st_mode))
			rt_panic(NULL, 0, 0, "can't attach '%s': it isn't there (or isn't a file)", value);
	} else {
		no_line_ends("address", value);
		if (!strchr(value, '@'))
			rt_panic(NULL, 0, 0, "'%s' isn't an email address (it has no @)", value);
	}
	char ***list = what == 0 ? &e->to : what == 1 ? &e->cc : what == 2 ? &e->bcc : &e->files;
	int *n = what == 0 ? &e->nto : what == 1 ? &e->ncc : what == 2 ? &e->nbcc : &e->nfiles;
	*list = realloc(*list, sizeof **list * (size_t)(*n + 1));
	(*list)[(*n)++] = strdup(value);
}

/* ---- encodings */

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* base64, in lines of 76 (wrap: 0 for none, as in a header's word) */
static void put_base64(Buf *b, const unsigned char *s, size_t n, int wrap)
{
	int col = 0;
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)s[i] << 16 | (i + 1 < n ? (unsigned)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
		char q[4] = { b64[v >> 18 & 63], b64[v >> 12 & 63], i + 1 < n ? b64[v >> 6 & 63] : '=', i + 2 < n ? b64[v & 63] : '=' };
		put_n(b, q, 4);
		col += 4;
		if (wrap && col >= 76) {
			put(b, "\r\n");
			col = 0;
		}
	}
	if (wrap && col)
		put(b, "\r\n");
}

static bool plain_ascii(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		if (*p >= 127 || (*p < 32 && *p != '\t' && *p != '\n' && *p != '\r'))
			return false;
	return true;
}

/* a header's words: as they are if ASCII, else RFC 2047's =?UTF-8?B?...?= */
static void put_words(Buf *b, const char *s)
{
	if (plain_ascii(s)) {
		put(b, s);
		return;
	}
	/* (in pieces of at most 45 bytes, whole characters, so each word is
	 * short enough: 60 encoded) */
	size_t n = strlen(s), i = 0;
	while (i < n) {
		size_t take = n - i < 45 ? n - i : 45;
		while (take < n - i && take > 0 && ((unsigned char)s[i + take] & 0xC0) == 0x80)
			take--;
		if (i)
			put(b, "\r\n ");
		put(b, "=?UTF-8?B?");
		put_base64(b, (const unsigned char *)s + i, take, 0);
		put(b, "?=");
		i += take;
	}
}

/* an address as it's written ("Ana <ana@example.com>" or the address
 * alone), with a name that isn't ASCII encoded */
static void put_address(Buf *b, const char *a)
{
	const char *lt = strchr(a, '<');
	if (!lt) {
		put(b, a);
		return;
	}
	size_t nl = (size_t)(lt - a);
	while (nl && a[nl - 1] == ' ')
		nl--;
	char *name = strndup(a, nl);
	if (plain_ascii(name)) {
		put(b, a);
	} else {
		put_words(b, name);
		put(b, " ");
		put(b, lt);
	}
	free(name);
}

/* the address alone: "ana@example.com" from "Ana <ana@example.com>" */
static char *bare(const char *a)
{
	const char *lt = strchr(a, '<'), *gt = lt ? strchr(lt, '>') : NULL;
	if (lt && gt)
		return strndup(lt + 1, (size_t)(gt - lt - 1));
	while (*a == ' ')
		a++;
	char *s = strdup(a);
	for (size_t n = strlen(s); n && s[n - 1] == ' '; n--)
		s[n - 1] = 0;
	return s;
}

/* text, as it is (ASCII in short lines) or quoted-printable; its
 * Content-Transfer-Encoding first, in the headers already written */
static void put_text_part(Buf *b, const char *type, const char *text)
{
	bool simple = plain_ascii(text);
	size_t line = 0;
	for (const char *p = text; *p && simple; p++) {
		line = *p == '\n' ? 0 : line + 1;
		if (line > 76)
			simple = false;
	}
	putf(b, "Content-Type: %s; charset=utf-8\r\n", type);
	if (simple) {
		put(b, "Content-Transfer-Encoding: 7bit\r\n\r\n");
		for (const char *p = text; *p; p++) {
			if (*p == '\n' && (p == text || p[-1] != '\r'))
				put(b, "\r\n");
			else if (*p != '\r' || p[1] != '\n')
				put_n(b, p, 1);
			else
				put(b, "\r\n"), p++;
		}
		put(b, "\r\n");
		return;
	}
	put(b, "Content-Transfer-Encoding: quoted-printable\r\n\r\n");
	int col = 0;
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		if (*p == '\r' && p[1] == '\n')
			continue;
		if (*p == '\n') {
			put(b, "\r\n");
			col = 0;
			continue;
		}
		char enc[4];
		int is_plain = (*p >= 33 && *p <= 126 && *p != '=') || ((*p == ' ' || *p == '\t') && p[1] != '\n' && p[1] != '\r' && p[1]);
		if (is_plain)
			enc[0] = (char)*p, enc[1] = 0;
		else
			snprintf(enc, sizeof enc, "=%02X", *p);
		int w = (int)strlen(enc);
		if (col + w > 75) {
			put(b, "=\r\n");   /* (a soft line end: the line goes on) */
			col = 0;
		}
		put(b, enc);
		col += w;
	}
	put(b, "\r\n");
}

static const char *file_type(const char *path)
{
	static const char *const types[][2] = {
		{ ".pdf", "application/pdf" }, { ".txt", "text/plain" }, { ".md", "text/markdown" }, { ".csv", "text/csv" },
		{ ".html", "text/html" }, { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" },
		{ ".gif", "image/gif" }, { ".webp", "image/webp" }, { ".svg", "image/svg+xml" }, { ".mp3", "audio/mpeg" },
		{ ".wav", "audio/wav" }, { ".flac", "audio/flac" }, { ".ogg", "audio/ogg" }, { ".mid", "audio/midi" },
		{ ".mp4", "video/mp4" }, { ".zip", "application/zip" }, { ".json", "application/json" },
		{ ".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
		{ ".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
		{ ".odt", "application/vnd.oasis.opendocument.text" }, { ".cby", "text/plain" },
	};
	const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/');
	if (dot && (!slash || dot > slash))
		for (size_t i = 0; i < sizeof types / sizeof *types; i++)
			if (strcasecmp(dot, types[i][0]) == 0)
				return types[i][1];
	return "application/octet-stream";
}

/* a file's name, in a header: as it is, or RFC 2231's filename*=UTF-8''... */
static void put_file_name(Buf *b, const char *key, const char *name)
{
	if (plain_ascii(name) && !strpbrk(name, "\"\\")) {
		putf(b, "%s=\"%s\"", key, name);
		return;
	}
	putf(b, "%s*=UTF-8''", key);
	for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
		if (isalnum(*p) || strchr("-._~", *p))
			put_n(b, (const char *)p, 1);
		else
			putf(b, "%%%02X", *p);
	}
}

static void put_attachment(Buf *b, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't attach '%s': it can't be read", path);
	Buf data = { 0 };
	char chunk[65536];
	size_t got;
	while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
		put_n(&data, chunk, got);
	fclose(f);
	const char *slash = strrchr(path, '/');
	const char *name = slash ? slash + 1 : path;
	putf(b, "Content-Type: %s; ", file_type(path));
	put_file_name(b, "name", name);
	put(b, "\r\nContent-Disposition: attachment; ");
	put_file_name(b, "filename", name);
	put(b, "\r\nContent-Transfer-Encoding: base64\r\n\r\n");
	put_base64(b, (const unsigned char *)(data.d ? data.d : ""), data.len, 1);
	free(data.d);
}

static void random_hex(char *out, int n)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char r[32];
	FILE *u = fopen("/dev/urandom", "rb");
	size_t got = u ? fread(r, 1, sizeof r, u) : 0;
	if (u)
		fclose(u);
	for (size_t i = got; i < sizeof r; i++)
		r[i] = (unsigned char)(rand() ^ (int)(time(NULL) >> (i % 8)));
	for (int i = 0; i < n; i++)
		out[i] = hex[r[i % 32] >> (i & 1 ? 0 : 4) & 15];
	out[n] = 0;
}

/* the whole message, as it's sent */
static const char *render(Email *e)
{
	if (!e->from || !*e->from)
		rt_panic(NULL, 0, 0, "%s", "a message needs who it's from (set_from)");
	if (!e->nto && !e->ncc && !e->nbcc)
		rt_panic(NULL, 0, 0, "%s", "a message needs someone to go to (add_to)");
	Buf b = { 0 };
	const char *fixed = getenv("CABBY_EMAIL_DATE");   /* (the tests': the same every time) */
	char date[64];
	if (fixed && *fixed) {
		snprintf(date, sizeof date, "%s", fixed);
	} else {
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
		static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		long off = tm.tm_gmtoff / 60;
		snprintf(date, sizeof date, "%s, %d %s %d %02d:%02d:%02d %c%02ld%02ld", days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
			 tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec, off < 0 ? '-' : '+', labs(off) / 60, labs(off) % 60);
	}
	char id[40];
	if (fixed && *fixed)
		snprintf(id, sizeof id, "test");
	else
		random_hex(id, 32);
	char *from = bare(e->from);
	const char *at = strchr(from, '@');
	putf(&b, "Date: %s\r\nFrom: ", date);
	put_address(&b, e->from);
	struct { const char *name; char **list; int n; } heads[] = { { "To", e->to, e->nto }, { "Cc", e->cc, e->ncc } };
	for (int k = 0; k < 2; k++) {
		if (!heads[k].n)
			continue;
		putf(&b, "\r\n%s: ", heads[k].name);
		for (int i = 0; i < heads[k].n; i++) {
			if (i)
				put(&b, ",\r\n ");
			put_address(&b, heads[k].list[i]);
		}
	}
	if (e->reply_to && *e->reply_to) {
		put(&b, "\r\nReply-To: ");
		put_address(&b, e->reply_to);
	}
	put(&b, "\r\nSubject: ");
	put_words(&b, e->subject ? e->subject : "");
	putf(&b, "\r\nMessage-ID: <%s@%s>\r\nMIME-Version: 1.0\r\n", id, at ? at + 1 : "cabby");
	free(from);
	const char *text = e->text ? e->text : "";
	char mixed[48], alt[48];
	if (fixed && *fixed) {
		snprintf(mixed, sizeof mixed, "cabby-mixed");
		snprintf(alt, sizeof alt, "cabby-alternative");
	} else {
		char r[25];
		random_hex(r, 24);
		snprintf(mixed, sizeof mixed, "mixed-%s", r);
		snprintf(alt, sizeof alt, "alt-%s", r);
	}
	if (e->nfiles)
		putf(&b, "Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n--%s\r\n", mixed, mixed);
	if (e->html && *e->html) {
		putf(&b, "Content-Type: multipart/alternative; boundary=\"%s\"\r\n\r\n--%s\r\n", alt, alt);
		put_text_part(&b, "text/plain", text);
		putf(&b, "--%s\r\n", alt);
		put_text_part(&b, "text/html", e->html);
		putf(&b, "--%s--\r\n", alt);
	} else {
		put_text_part(&b, "text/plain", text);
	}
	if (e->nfiles) {
		for (int i = 0; i < e->nfiles; i++) {
			putf(&b, "--%s\r\n", mixed);
			put_attachment(&b, e->files[i]);
		}
		putf(&b, "--%s--\r\n", mixed);
	}
	free(e->rendered);
	e->rendered = b.d;
	return e->rendered;
}

static int64_t hand(const char *s)
{
	free(handed);
	handed = strdup(s ? s : "");
	return (int64_t)strlen(handed);
}

char cby_email_char(int64_t i) { return handed && i >= 0 && i < (int64_t)strlen(handed) ? handed[i] : 0; }

int64_t cby_email_text(int64_t id) { return hand(render(email(id))); }

int64_t cby_email_error(void) { return hand(error); }

/* ---- sending */

typedef struct {
	const char *d;
	size_t len, at;
} Reading;

static size_t read_out(char *dst, size_t size, size_t n, void *data)
{
	Reading *r = data;
	size_t room = size * n, left = r->len - r->at;
	size_t take = left < room ? left : room;
	memcpy(dst, r->d + r->at, take);
	r->at += take;
	return take;
}

/* Sent, by the server at host and port, logged in as user (if there's
 * one); security: 0 TLS from the start (port 465), 1 STARTTLS (587), 2 none
 * (a test's own server). 1 if it went; else 0, and error says why. */
int cby_email_send(int64_t id, const char *host, int64_t port, const char *user, const char *password, int64_t security)
{
	Email *e = email(id);
	const char *msg = render(e);
	error[0] = 0;
	if (!load()) {
		snprintf(error, sizeof error, "sending email needs libcurl, which comes with the 'curl' package");
		return 0;
	}
	CURL *c = ec.init();
	if (!c) {
		snprintf(error, sizeof error, "libcurl couldn't start");
		return 0;
	}
	char url[600];
	snprintf(url, sizeof url, "%s://%s:%lld", security == 0 ? "smtps" : "smtp", host, (long long)port);
	char err[256] = "";
	ec.setopt(c, E_URL, url);
	ec.setopt(c, E_ERRORBUFFER, err);
	ec.setopt(c, E_NOSIGNAL, 1L);
	ec.setopt(c, E_CONNECTTIMEOUT, 30L);
	ec.setopt(c, E_TIMEOUT, 300L);
	if (security == 1)
		ec.setopt(c, E_USE_SSL, 3L);   /* CURLUSESSL_ALL: STARTTLS, or not at all */
	if (user && *user) {
		ec.setopt(c, E_USERNAME, user);
		ec.setopt(c, E_PASSWORD, password ? password : "");
	}
	char *from = bare(e->from);
	char envelope[620];
	snprintf(envelope, sizeof envelope, "<%s>", from);
	ec.setopt(c, E_MAIL_FROM, envelope);
	struct curl_slist *to = NULL;
	char **lists[] = { e->to, e->cc, e->bcc };
	int counts[] = { e->nto, e->ncc, e->nbcc };
	for (int k = 0; k < 3; k++)
		for (int i = 0; i < counts[k]; i++) {
			char *a = bare(lists[k][i]);
			char r[620];
			snprintf(r, sizeof r, "<%s>", a);
			to = ec.slist_append(to, r);
			free(a);
		}
	ec.setopt(c, E_MAIL_RCPT, to);
	Reading rd = { msg, strlen(msg), 0 };
	ec.setopt(c, E_READFUNCTION, read_out);
	ec.setopt(c, E_READDATA, &rd);
	ec.setopt(c, E_UPLOAD, 1L);
	bool was = rt_gil_drop();
	int r = ec.perform(c);
	rt_gil_take(was);
	if (r != 0)
		snprintf(error, sizeof error, "%s", *err ? err : ec.strerror(r));
	ec.slist_free_all(to);
	ec.cleanup(c);
	free(from);
	return r == 0;
}

/* the message, saved as a file (.eml: a mail program opens it) */
void cby_email_save(int64_t id, const char *path)
{
	const char *msg = render(email(id));
	FILE *f = fopen(path, "wb");
	if (!f || fwrite(msg, 1, strlen(msg), f) != strlen(msg))
		rt_panic(NULL, 0, 0, "can't save the message as '%s'", path);
	fclose(f);
}

/* ============================================================ reading messages
 *
 * A message as it came (from IMAP: emails.mailbox), read: its headers
 * (their encoded words, =?UTF-8?B?...?=, decoded), its text and HTML (from
 * base64 or quoted-printable, and from their character set into UTF-8),
 * and its attachments, however the parts are nested (multipart/mixed,
 * alternative, related). */

typedef struct {
	char *name, *type;
	char *data;
	size_t len;
} Attachment;

typedef struct {
	char **names, **values;   /* the headers, decoded */
	int nheaders;
	char *text, *html;
	Attachment *files;
	int nfiles;
	char *raw;
	size_t raw_len;
	int64_t uid, size;
	int seen, flagged;
} Got;

static Got **gots;
static int64_t ngots;

static Got *got_of(int64_t id)
{
	if (id < 1 || id > ngots || !gots[id - 1])
		rt_panic(NULL, 0, 0, "%s", "that message has been dropped");
	return gots[id - 1];
}

/* ---- character sets, into UTF-8 */

static void put_code(Buf *b, uint32_t c)
{
	char u[4];
	int n;
	if (c < 0x80) { u[0] = (char)c; n = 1; }
	else if (c < 0x800) { u[0] = (char)(0xc0 | c >> 6); u[1] = (char)(0x80 | (c & 0x3f)); n = 2; }
	else if (c < 0x10000) { u[0] = (char)(0xe0 | c >> 12); u[1] = (char)(0x80 | (c >> 6 & 0x3f)); u[2] = (char)(0x80 | (c & 0x3f)); n = 3; }
	else { u[0] = (char)(0xf0 | c >> 18); u[1] = (char)(0x80 | (c >> 12 & 0x3f)); u[2] = (char)(0x80 | (c >> 6 & 0x3f)); u[3] = (char)(0x80 | (c & 0x3f)); n = 4; }
	put_n(b, u, (size_t)n);
}

/* windows-1252's 0x80 to 0x9f (Latin-1 has controls there) */
static const uint16_t cp1252[32] = { 0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160,
				     0x2039, 0x152, 0x8d, 0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
				     0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178 };

typedef void *(*iconv_open_f)(const char *, const char *);
typedef size_t (*iconv_f)(void *, char **, size_t *, char **, size_t *);
typedef int (*iconv_close_f)(void *);

/* by the system's iconv, for the rest (Shift JIS, KOI8-R, GB2312 ...):
 * NULL if it can't */
static char *by_iconv(const char *charset, const char *s, size_t n)
{
	static iconv_open_f op;
	static iconv_f conv;
	static iconv_close_f cl;
	static int tried;
	if (!tried) {
		tried = 1;
		void *h = dlopen(NULL, RTLD_NOW);
		void *p = h ? dlsym(h, "iconv_open") : NULL;
#ifdef __APPLE__
		if (!p) {
			h = dlopen("/usr/lib/libiconv.2.dylib", RTLD_NOW | RTLD_LOCAL);
			p = h ? dlsym(h, "iconv_open") : NULL;
		}
#endif
		if (p) {
			memcpy(&op, &p, sizeof p);
			p = dlsym(h, "iconv");
			memcpy(&conv, &p, sizeof p);
			p = dlsym(h, "iconv_close");
			memcpy(&cl, &p, sizeof p);
		}
	}
	if (!op || !conv || !cl)
		return NULL;
	void *cd = op("UTF-8", charset);
	if (cd == (void *)-1)
		return NULL;
	size_t cap = n * 4 + 16, left = cap - 1;
	char *out = malloc(cap), *o = out, *in = (char *)s;
	size_t in_left = n;
	while (in_left) {
		if (conv(cd, &in, &in_left, &o, &left) == (size_t)-1) {
			/* (a byte that isn't of it: skipped, and on) */
			in++;
			in_left--;
		}
	}
	cl(cd);
	*o = 0;
	return out;
}

/* text in a character set, as UTF-8 */
static char *to_utf8(const char *charset, const char *s, size_t n)
{
	char cs[64];
	snprintf(cs, sizeof cs, "%s", charset ? charset : "");
	for (char *c = cs; *c; c++)
		*c = (char)tolower((unsigned char)*c);
	int latin1 = strcmp(cs, "iso-8859-1") == 0 || strcmp(cs, "latin1") == 0 || strcmp(cs, "iso8859-1") == 0;
	int win = strcmp(cs, "windows-1252") == 0 || strcmp(cs, "cp1252") == 0;
	if (!*cs || strcmp(cs, "utf-8") == 0 || strcmp(cs, "utf8") == 0 || strcmp(cs, "us-ascii") == 0 || strcmp(cs, "ascii") == 0) {
		char *out = malloc(n + 1);
		memcpy(out, s, n);
		out[n] = 0;
		return out;
	}
	if (latin1 || win || strcmp(cs, "iso-8859-15") == 0) {
		Buf b = { 0 };
		for (size_t i = 0; i < n; i++) {
			unsigned char c = (unsigned char)s[i];
			uint32_t u = c;
			if (win && c >= 0x80 && c < 0xa0)
				u = cp1252[c - 0x80];
			else if (strcmp(cs, "iso-8859-15") == 0)
				u = c == 0xa4 ? 0x20ac : c == 0xa6 ? 0x160 : c == 0xa8 ? 0x161 : c == 0xb4 ? 0x17d : c == 0xb8 ? 0x17e :
				    c == 0xbc ? 0x152 : c == 0xbd ? 0x153 : c == 0xbe ? 0x178 : c;
			put_code(&b, u);
		}
		put_n(&b, "", 0);
		return b.d ? b.d : strdup("");
	}
	char *got = by_iconv(charset, s, n);
	if (got)
		return got;
	char *out = malloc(n + 1);
	memcpy(out, s, n);
	out[n] = 0;
	return out;
}

/* ---- base64 and quoted-printable */

static int b64_value(int c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

static char *from_base64(const char *s, size_t n, size_t *len)
{
	char *out = malloc(n * 3 / 4 + 4), *o = out;
	unsigned bits = 0;
	int have = 0;
	for (size_t i = 0; i < n; i++) {
		int v = b64_value((unsigned char)s[i]);
		if (v < 0)
			continue;   /* (line ends, and the = at the end) */
		bits = bits << 6 | (unsigned)v;
		have += 6;
		if (have >= 8) {
			have -= 8;
			*o++ = (char)(bits >> have & 0xff);
		}
	}
	*o = 0;
	*len = (size_t)(o - out);
	return out;
}

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

/* quoted-printable; in a header (words), _ is a space */
static char *from_qp(const char *s, size_t n, size_t *len, int words)
{
	char *out = malloc(n + 1), *o = out;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '=' && i + 1 < n && (s[i + 1] == '\r' || s[i + 1] == '\n')) {
			/* a soft line end: the line goes on */
			i++;
			if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
				i++;
		} else if (s[i] == '=' && i + 2 < n && hexv((unsigned char)s[i + 1]) >= 0 && hexv((unsigned char)s[i + 2]) >= 0) {
			*o++ = (char)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2]));
			i += 2;
		} else {
			*o++ = words && s[i] == '_' ? ' ' : s[i];
		}
	}
	*o = 0;
	*len = (size_t)(o - out);
	return out;
}

/* a header's encoded words decoded: "=?UTF-8?B?...?= rest", as UTF-8 */
static char *decode_words(const char *s)
{
	Buf b = { 0 };
	const char *p = s;
	int last_word = 0;
	while (*p) {
		const char *start = strstr(p, "=?");
		const char *q1 = start ? strchr(start + 2, '?') : NULL;
		const char *q2 = q1 && q1[1] && q1[2] == '?' ? q1 + 2 : NULL;
		const char *end = q2 ? strstr(q2 + 1, "?=") : NULL;
		if (!end) {
			put(&b, p);
			break;
		}
		/* (spaces between two encoded words aren't part of the text) */
		int only_space = 1;
		for (const char *c = p; c < start; c++)
			only_space &= *c == ' ' || *c == '\t' || *c == '\r' || *c == '\n';
		if (!(last_word && only_space))
			put_n(&b, p, (size_t)(start - p));
		char charset[64];
		snprintf(charset, sizeof charset, "%.*s", (int)(q1 - start - 2), start + 2);
		char *star = strchr(charset, '*');   /* (RFC 2231's language: "UTF-8*en") */
		if (star)
			*star = 0;
		size_t len;
		char *raw = toupper((unsigned char)q1[1]) == 'B' ? from_base64(q2 + 1, (size_t)(end - q2 - 1), &len)
							      : from_qp(q2 + 1, (size_t)(end - q2 - 1), &len, 1);
		char *u = to_utf8(charset, raw, len);
		put(&b, u);
		free(u);
		free(raw);
		p = end + 2;
		last_word = 1;
	}
	put_n(&b, "", 0);
	return b.d ? b.d : strdup("");
}

/* ---- headers, and parts */

typedef struct {
	char **names, **values;
	int n;
} Headers;

static void headers_free(Headers *h)
{
	for (int i = 0; i < h->n; i++) {
		free(h->names[i]);
		free(h->values[i]);
	}
	free(h->names);
	free(h->values);
}

/* the headers, unfolded (a line starting with a space goes on the one
 * before): where the body starts */
static const char *read_headers(const char *s, const char *end, Headers *h)
{
	*h = (Headers){ 0 };
	const char *p = s;
	while (p < end) {
		const char *e = memchr(p, '\n', (size_t)(end - p));
		if (!e)
			e = end;
		size_t ll = (size_t)(e - p);
		if (ll && p[ll - 1] == '\r')
			ll--;
		if (ll == 0)
			return e < end ? e + 1 : end;   /* the blank line: the body's next */
		if ((*p == ' ' || *p == '\t') && h->n) {
			char *v = h->values[h->n - 1];
			size_t vl = strlen(v);
			const char *t = p;
			while (t < p + ll && (*t == ' ' || *t == '\t'))
				t++;
			v = realloc(v, vl + (size_t)(p + ll - t) + 2);
			v[vl] = ' ';
			memcpy(v + vl + 1, t, (size_t)(p + ll - t));
			v[vl + 1 + (p + ll - t)] = 0;
			h->values[h->n - 1] = v;
		} else {
			const char *colon = memchr(p, ':', ll);
			if (colon) {
				const char *v = colon + 1;
				while (v < p + ll && (*v == ' ' || *v == '\t'))
					v++;
				h->names = realloc(h->names, sizeof *h->names * (size_t)(h->n + 1));
				h->values = realloc(h->values, sizeof *h->values * (size_t)(h->n + 1));
				h->names[h->n] = strndup(p, (size_t)(colon - p));
				h->values[h->n++] = strndup(v, (size_t)(p + ll - v));
			}
		}
		p = e + 1;
	}
	return end;
}

static const char *header(const Headers *h, const char *name)
{
	for (int i = 0; i < h->n; i++)
		if (strcasecmp(h->names[i], name) == 0)
			return h->values[i];
	return NULL;
}

/* a parameter of a header's value: boundary="xyz"; charset=utf-8;
 * name*=UTF-8''... (RFC 2231's, as many send names now) */
static char *param(const char *value, const char *name)
{
	if (!value)
		return NULL;
	size_t nl = strlen(name);
	for (const char *p = strchr(value, ';'); p; p = strchr(p + 1, ';')) {
		const char *k = p + 1;
		while (*k == ' ' || *k == '\t')
			k++;
		if (strncasecmp(k, name, nl) != 0)
			continue;
		const char *after = k + nl;
		int extended = *after == '*';
		if (extended)
			after++;
		if (*after == '*') {   /* (name*0*=: in pieces, the first only) */
			while (*after && *after != '=')
				after++;
		}
		if (*after != '=')
			continue;
		const char *v = after + 1;
		char *out;
		if (*v == '"') {
			const char *e = strchr(v + 1, '"');
			out = strndup(v + 1, e ? (size_t)(e - v - 1) : strlen(v + 1));
		} else {
			size_t n = strcspn(v, "; \t");
			out = strndup(v, n);
		}
		if (extended) {
			/* charset'language'%xx... */
			char *q1 = strchr(out, '\''), *q2 = q1 ? strchr(q1 + 1, '\'') : NULL;
			if (q2) {
				char cs[64];
				snprintf(cs, sizeof cs, "%.*s", (int)(q1 - out), out);
				Buf b = { 0 };
				for (const char *c = q2 + 1; *c; c++) {
					if (*c == '%' && hexv((unsigned char)c[1]) >= 0 && hexv((unsigned char)c[2]) >= 0) {
						char ch = (char)(hexv((unsigned char)c[1]) * 16 + hexv((unsigned char)c[2]));
						put_n(&b, &ch, 1);
						c += 2;
					} else {
						put_n(&b, c, 1);
					}
				}
				put_n(&b, "", 0);
				char *u = to_utf8(cs, b.d ? b.d : "", b.d ? strlen(b.d) : 0);
				free(b.d);
				free(out);
				out = u;
			}
		} else {
			char *u = decode_words(out);
			free(out);
			out = u;
		}
		return out;
	}
	return NULL;
}

/* a part's body, as its Content-Transfer-Encoding says */
static char *decoded_body(const Headers *h, const char *s, size_t n, size_t *len)
{
	const char *enc = header(h, "Content-Transfer-Encoding");
	if (enc && strncasecmp(enc, "base64", 6) == 0)
		return from_base64(s, n, len);
	if (enc && strncasecmp(enc, "quoted-printable", 16) == 0)
		return from_qp(s, n, len, 0);
	char *out = malloc(n + 1);
	memcpy(out, s, n);
	out[n] = 0;
	*len = n;
	return out;
}

/* line ends as a program has them: \n, not mail's \r\n */
static void plain_line_ends(char *s)
{
	char *o = s;
	for (char *p = s; *p; p++)
		if (!(p[0] == '\r' && p[1] == '\n'))
			*o++ = *p;
	*o = 0;
	/* (and not the one that ends every message's last line) */
	if (o > s && o[-1] == '\n')
		o[-1] = 0;
}

/* a part, and the parts inside it */
static void read_part(Got *g, const char *s, const char *end, int depth)
{
	Headers h;
	const char *body = read_headers(s, end, &h);
	const char *ctype = header(&h, "Content-Type");
	char type[128] = "text/plain";
	if (ctype) {
		size_t n = strcspn(ctype, "; \t");
		snprintf(type, sizeof type, "%.*s", (int)n, ctype);
		for (char *c = type; *c; c++)
			*c = (char)tolower((unsigned char)*c);
	}
	const char *disp = header(&h, "Content-Disposition");
	char *filename = param(disp, "filename");   /* ("attachment; filename=x") */
	if (!filename)
		filename = param(ctype, "name");
	int attachment = (disp && strncasecmp(disp, "attachment", 10) == 0) || (filename && strncmp(type, "multipart/", 10) != 0);
	if (strncmp(type, "multipart/", 10) == 0 && depth < 20) {
		char *boundary = param(ctype, "boundary");
		if (boundary) {
			size_t bl = strlen(boundary);
			const char *p = body, *part = NULL;
			while (p < end) {
				const char *e = memchr(p, '\n', (size_t)(end - p));
				if (!e)
					e = end;
				if ((size_t)(e - p) >= bl + 2 && p[0] == '-' && p[1] == '-' && strncmp(p + 2, boundary, bl) == 0) {
					if (part)
						read_part(g, part, p > part && p[-1] == '\n' ? p - 1 - (p - 1 > part && p[-2] == '\r') : p, depth + 1);
					if (p + 2 + bl + 1 < end && p[2 + bl] == '-' && p[3 + bl] == '-')
						break;   /* the last boundary */
					part = e < end ? e + 1 : end;
				}
				p = e + 1;
			}
			free(boundary);
		}
	} else if (!attachment && strcmp(type, "text/plain") == 0 && !g->text) {
		size_t len;
		char *raw = decoded_body(&h, body, (size_t)(end - body), &len);
		char *cs = param(ctype, "charset");
		g->text = to_utf8(cs, raw, len);
		plain_line_ends(g->text);
		free(cs);
		free(raw);
	} else if (!attachment && strcmp(type, "text/html") == 0 && !g->html) {
		size_t len;
		char *raw = decoded_body(&h, body, (size_t)(end - body), &len);
		char *cs = param(ctype, "charset");
		g->html = to_utf8(cs, raw, len);
		plain_line_ends(g->html);
		free(cs);
		free(raw);
	} else if (attachment || strncmp(type, "text/", 5) != 0) {
		size_t len;
		char *data = decoded_body(&h, body, (size_t)(end - body), &len);
		g->files = realloc(g->files, sizeof *g->files * (size_t)(g->nfiles + 1));
		char unnamed[64];
		snprintf(unnamed, sizeof unnamed, "attachment-%d", g->nfiles + 1);
		g->files[g->nfiles++] = (Attachment){ strdup(filename ? filename : unnamed), strdup(type), data, len };
	}
	free(filename);
	headers_free(&h);
}

/* a message, read: its number */
int64_t cby_email_read(const char *raw, int64_t len)
{
	Got *g = calloc(1, sizeof *g);
	g->raw = malloc((size_t)len + 1);
	memcpy(g->raw, raw, (size_t)len);
	g->raw[len] = 0;
	g->raw_len = (size_t)len;
	Headers h;
	read_headers(g->raw, g->raw + len, &h);
	g->names = h.names;
	g->values = h.values;
	g->nheaders = h.n;
	for (int i = 0; i < g->nheaders; i++) {
		char *d = decode_words(g->values[i]);
		free(g->values[i]);
		g->values[i] = d;
	}
	read_part(g, g->raw, g->raw + len, 0);
	int64_t i = 0;
	while (i < ngots && gots[i])
		i++;
	if (i == ngots) {
		gots = realloc(gots, sizeof *gots * (size_t)(ngots + 1));
		ngots++;
	}
	gots[i] = g;
	return i + 1;
}

/* a message read from a file (.eml) */
int64_t cby_email_read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't read the message '%s': it isn't there", path);
	Buf b = { 0 };
	char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		put_n(&b, buf, n);
	fclose(f);
	int64_t id = cby_email_read(b.d ? b.d : "", (int64_t)b.len);
	free(b.d);
	return id;
}

void cby_email_got_free(int64_t id)
{
	if (id < 1 || id > ngots || !gots[id - 1])
		return;
	Got *g = gots[id - 1];
	for (int i = 0; i < g->nheaders; i++) {
		free(g->names[i]);
		free(g->values[i]);
	}
	free(g->names);
	free(g->values);
	free(g->text);
	free(g->html);
	for (int i = 0; i < g->nfiles; i++) {
		free(g->files[i].name);
		free(g->files[i].type);
		free(g->files[i].data);
	}
	free(g->files);
	free(g->raw);
	free(g);
	gots[id - 1] = NULL;
}

/* the address alone, from "Ana Rossi <ana@example.com>" */
static char *address_of(const char *s)
{
	const char *lt = strrchr(s, '<'), *gt = lt ? strchr(lt, '>') : NULL;
	if (lt && gt)
		return strndup(lt + 1, (size_t)(gt - lt - 1));
	char *t = strdup(s);
	char *a = t;
	while (*a == ' ')
		a++;
	char *out = strdup(a);
	free(t);
	return out;
}

/* the name alone ("" if there's none) */
static char *name_of(const char *s)
{
	const char *lt = strrchr(s, '<');
	if (!lt)
		return strdup("");
	char *n = strndup(s, (size_t)(lt - s));
	size_t l = strlen(n);
	while (l && (n[l - 1] == ' ' || n[l - 1] == '\t'))
		n[--l] = 0;
	char *a = n;
	while (*a == ' ')
		a++;
	if (*a == '"' && l && n[l - 1] == '"') {
		n[l - 1] = 0;
		a++;
	}
	char *out = strdup(a);
	free(n);
	return out;
}

/* 0 a header (name), 1 its text, 2 its HTML, 3 from's address, 4 from's
 * name, 5 an attachment's name (index in name), 6 an attachment's type,
 * 7 the whole message as it came */
int64_t cby_email_got_text(int64_t id, int64_t what, const char *name)
{
	Got *g = got_of(id);
	switch (what) {
	case 0: {
		for (int i = 0; i < g->nheaders; i++)
			if (strcasecmp(g->names[i], name) == 0)
				return hand(g->values[i]);
		return hand("");
	}
	case 1: return hand(g->text ? g->text : "");
	case 2: return hand(g->html ? g->html : "");
	case 3: case 4: {
		const char *from = "";
		for (int i = 0; i < g->nheaders; i++)
			if (strcasecmp(g->names[i], "From") == 0)
				from = g->values[i];
		char *t = what == 3 ? address_of(from) : name_of(from);
		int64_t n = hand(t);
		free(t);
		return n;
	}
	case 5: case 6: {
		long k = strtol(name, NULL, 10);
		if (k < 0 || k >= g->nfiles)
			rt_panic(NULL, 0, 0, "there's no attachment %ld (there are %d)", k, g->nfiles);
		return hand(what == 5 ? g->files[k].name : g->files[k].type);
	}
	case 7: return hand(g->raw);
	}
	return hand("");
}

/* 0 attachments, 1 an attachment's size (k), 2 its number in the mailbox
 * (uid), 3 its size in all, 4 read, 5 flagged */
int64_t cby_email_got_number(int64_t id, int64_t what, int64_t k)
{
	Got *g = got_of(id);
	switch (what) {
	case 0: return g->nfiles;
	case 1: return k >= 0 && k < g->nfiles ? (int64_t)g->files[k].len : 0;
	case 2: return g->uid;
	case 3: return g->size ? g->size : (int64_t)g->raw_len;
	case 4: return g->seen;
	case 5: return g->flagged;
	}
	return 0;
}

void cby_email_got_set(int64_t id, int64_t uid, int64_t size, int seen, int flagged)
{
	Got *g = got_of(id);
	g->uid = uid;
	g->size = size;
	g->seen = seen;
	g->flagged = flagged;
}

/* an attachment saved as a file: false if it couldn't be written */
int cby_email_save_attachment(int64_t id, int64_t k, const char *path)
{
	Got *g = got_of(id);
	if (k < 0 || k >= g->nfiles)
		rt_panic(NULL, 0, 0, "there's no attachment %lld (there are %d)", (long long)k, g->nfiles);
	FILE *f = fopen(path, "wb");
	if (!f)
		return 0;
	size_t w = fwrite(g->files[k].data, 1, g->files[k].len, f);
	return fclose(f) == 0 && w == g->files[k].len;
}

void cby_email_take(char *dst)
{
	if (handed)
		memcpy(dst, handed, strlen(handed));
}
