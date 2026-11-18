/* web.h — a web server, for the networks library's networks.web_server
 * (included by connections.c). It's a server as networks.server is, its
 * connections watched by the event loop, but what comes on them is HTTP:
 * a browser's requests (a method, a path, headers, perhaps a body), each
 * told to the program's on_request handler, which answers it with a page,
 * a file, JSON or a status. Keep-alive: a browser's connection carries one
 * request after another. Requests are answered one at a time, in the
 * order they come, as everything in the event loop is.
 *
 * HTTP/1.1, as RFC 9112 says, the part a browser and a program fetching
 * pages use: Content-Length bodies (chunked ones are turned down, as
 * browsers don't send them). HTTPS is the same, its connections through
 * TLS (tls.h), once the server has a certificate. */

#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>

/* whether word is in s, whatever the letters' case */
static int has_word(const char *s, const char *word)
{
	size_t n = strlen(word);
	for (; *s; s++)
		if (strncasecmp(s, word, n) == 0)
			return 1;
	return 0;
}

#define WEB_MOST_HEAD (64 * 1024)          /* the request line and headers, at most */
#define WEB_MOST_BODY (64 * 1024 * 1024)   /* a body (an upload), at most */

typedef struct Request {
	int64_t conn;
	char method[16];
	char *target;          /* as sent: "/search?q=cabby%20lang" */
	char *path;            /* decoded: "/search" */
	char *query;           /* as sent: "q=cabby%20lang" */
	char **names, **values;   /* headers, names in small letters */
	int nheaders;
	char *body;
	size_t body_len;
	int keep_alive, head_only;
	int answered;
	char *extra;           /* headers the program added, for the answer */
	char *got;             /* (text being handed back) */
} Request;

static void request_free(Request *r)
{
	if (!r)
		return;
	free(r->target);
	free(r->path);
	free(r->query);
	for (int i = 0; i < r->nheaders; i++) {
		free(r->names[i]);
		free(r->values[i]);
	}
	free(r->names);
	free(r->values);
	free(r->body);
	free(r->extra);
	free(r->got);
	free(r);
}

static void web_forget(Server *s)
{
	request_free(s->req);
	s->req = NULL;
}

/* %41 to A, and (in a query or form) + to a space */
static char *url_decode(const char *s, size_t n, int plus)
{
	char *out = malloc(n + 1), *o = out;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '%' && i + 2 < n && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
			char hex[3] = { s[i + 1], s[i + 2], 0 };
			*o++ = (char)strtol(hex, NULL, 16);
			i += 2;
		} else {
			*o++ = plus && s[i] == '+' ? ' ' : s[i];
		}
	}
	*o = 0;
	return out;
}

static const char *status_words(int code)
{
	switch (code) {
	case 200: return "OK";
	case 201: return "Created";
	case 204: return "No Content";
	case 301: return "Moved Permanently";
	case 302: return "Found";
	case 303: return "See Other";
	case 304: return "Not Modified";
	case 307: return "Temporary Redirect";
	case 308: return "Permanent Redirect";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 409: return "Conflict";
	case 411: return "Length Required";
	case 413: return "Content Too Large";
	case 414: return "URI Too Long";
	case 418: return "I'm a teapot";
	case 422: return "Unprocessable Content";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	}
	return code < 300 ? "OK" : code < 400 ? "Redirect" : code < 500 ? "Client Error" : "Server Error";
}

/* a file's type, from its name's ending */
static const char *type_of(const char *path)
{
	static const char *const types[][2] = {
		{ ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" },
		{ ".css", "text/css; charset=utf-8" }, { ".js", "text/javascript; charset=utf-8" },
		{ ".mjs", "text/javascript; charset=utf-8" }, { ".json", "application/json" },
		{ ".txt", "text/plain; charset=utf-8" }, { ".md", "text/markdown; charset=utf-8" },
		{ ".cby", "text/plain; charset=utf-8" }, { ".hby", "text/plain; charset=utf-8" },
		{ ".csv", "text/csv; charset=utf-8" }, { ".xml", "application/xml" },
		{ ".svg", "image/svg+xml" }, { ".png", "image/png" }, { ".jpg", "image/jpeg" },
		{ ".jpeg", "image/jpeg" }, { ".gif", "image/gif" }, { ".webp", "image/webp" },
		{ ".ico", "image/x-icon" }, { ".avif", "image/avif" }, { ".bmp", "image/bmp" },
		{ ".woff", "font/woff" }, { ".woff2", "font/woff2" }, { ".ttf", "font/ttf" }, { ".otf", "font/otf" },
		{ ".mp3", "audio/mpeg" }, { ".ogg", "audio/ogg" }, { ".wav", "audio/wav" }, { ".flac", "audio/flac" },
		{ ".mp4", "video/mp4" }, { ".webm", "video/webm" }, { ".pdf", "application/pdf" },
		{ ".zip", "application/zip" }, { ".wasm", "application/wasm" }, { ".glb", "model/gltf-binary" },
		{ ".gltf", "model/gltf+json" },
	};
	const char *dot = strrchr(path, '.');
	const char *slash = strrchr(path, '/');
	if (dot && (!slash || dot > slash))
		for (size_t i = 0; i < sizeof types / sizeof *types; i++)
			if (strcasecmp(dot, types[i][0]) == 0)
				return types[i][1];
	return "application/octet-stream";
}

static Request *web_request(int64_t sid)
{
	Server *s = server(sid);
	if (!s->req)
		rt_panic(NULL, 0, 0, "%s", "there's no request being answered (only in on_request's handler)");
	return s->req;
}

/* the answer: its status, its type, and what it is (len bytes) */
static void web_answer(Request *r, int status, const char *type, const char *data, size_t len)
{
	if (r->answered)
		rt_panic(NULL, 0, 0, "%s", "this request has been answered already (one answer to each)");
	r->answered = 1;
	if (r->conn < 1 || r->conn > nconns || !conns[r->conn - 1] || conns[r->conn - 1]->fd < 0)
		return;   /* (the browser has gone) */
	char head[1024];
	int n = snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n%s%s%s%sConnection: %s\r\n\r\n",
			 status, status_words(status), len, type && *type ? "Content-Type: " : "", type ? type : "",
			 type && *type ? "\r\n" : "", "", r->keep_alive ? "keep-alive" : "close");
	/* the program's own headers go before the blank line */
	size_t extra = r->extra ? strlen(r->extra) : 0;
	char *whole = malloc((size_t)n + extra + (r->head_only ? 0 : len));
	size_t at = (size_t)n - 2;   /* (before the last \r\n) */
	memcpy(whole, head, at);
	if (extra)
		memcpy(whole + at, r->extra, extra);
	memcpy(whole + at + extra, "\r\n", 2);
	size_t total = at + extra + 2;
	if (!r->head_only && len) {
		memcpy(whole + total, data, len);
		total += len;
	}
	cby_conn_send(r->conn, whole, (int64_t)total);
	free(whole);
}

static void web_simple(Request *r, int status, const char *words)
{
	char page[512];
	int n = snprintf(page, sizeof page,
			 "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>%d %s</title></head>"
			 "<body><h1>%d %s</h1><p>%s</p></body></html>\n",
			 status, status_words(status), status, status_words(status), words);
	web_answer(r, status, "text/html; charset=utf-8", page, (size_t)n);
}

/* a whole request in the buffer, taken out of it: 1; 0 not yet; -1 bad
 * (answered, and the connection to be closed) */
static int web_parse(int64_t id, Request **out)
{
	Conn *c = conns[id - 1];
	*out = NULL;
	char *end = NULL;
	for (size_t i = 0; i + 3 < c->in_len; i++)
		if (c->in[i] == '\r' && c->in[i + 1] == '\n' && c->in[i + 2] == '\r' && c->in[i + 3] == '\n') {
			end = c->in + i;
			break;
		}
	/* (browsers' keep-alive connections may start with a stray line end) */
	while (c->in_len >= 2 && c->in[0] == '\r' && c->in[1] == '\n' && (!end || end != c->in)) {
		memmove(c->in, c->in + 2, c->in_len - 2);
		c->in_len -= 2;
		if (end)
			end -= 2;
	}
	Request tmp = { .conn = id, .keep_alive = 0 };
	if (!end) {
		if (c->in_len > WEB_MOST_HEAD) {
			web_simple(&tmp, 431, "The request's headers were too long.");
			return -1;
		}
		return 0;
	}
	size_t head_len = (size_t)(end - c->in) + 4;
	Request *r = calloc(1, sizeof *r);
	r->conn = id;
	/* the request line: METHOD TARGET HTTP/1.x */
	char *line_end = memchr(c->in, '\r', head_len);
	char *sp1 = memchr(c->in, ' ', (size_t)(line_end - c->in));
	char *sp2 = sp1 ? memchr(sp1 + 1, ' ', (size_t)(line_end - sp1 - 1)) : NULL;
	if (!sp1 || !sp2 || sp1 - c->in >= (long)sizeof r->method || sp2 == sp1 + 1) {
		request_free(r);
		web_simple(&tmp, 400, "That wasn't a request a web server understands.");
		return -1;
	}
	memcpy(r->method, c->in, (size_t)(sp1 - c->in));
	r->target = strndup(sp1 + 1, (size_t)(sp2 - sp1 - 1));
	int http10 = (size_t)(line_end - sp2 - 1) >= 8 && strncmp(sp2 + 1, "HTTP/1.0", 8) == 0;
	r->keep_alive = !http10;
	r->head_only = strcmp(r->method, "HEAD") == 0;
	char *q = strchr(r->target, '?');
	char *hash = strchr(r->target, '#');
	size_t plen = q ? (size_t)(q - r->target) : hash ? (size_t)(hash - r->target) : strlen(r->target);
	r->path = url_decode(r->target, plen, 0);
	r->query = q ? strndup(q + 1, hash && hash > q ? (size_t)(hash - q - 1) : strlen(q + 1)) : strdup("");
	/* headers */
	size_t content_length = 0;
	int chunked = 0;
	for (char *p = line_end + 2; p < end;) {
		char *e = memchr(p, '\r', (size_t)(end - p) + 2);
		if (!e)
			break;
		char *colon = memchr(p, ':', (size_t)(e - p));
		if (colon) {
			char *name = strndup(p, (size_t)(colon - p));
			for (char *k = name; *k; k++)
				*k = (char)tolower((unsigned char)*k);
			char *v = colon + 1;
			while (v < e && (*v == ' ' || *v == '\t'))
				v++;
			char *ve = e;
			while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t'))
				ve--;
			char *value = strndup(v, (size_t)(ve - v));
			r->names = realloc(r->names, sizeof *r->names * (size_t)(r->nheaders + 1));
			r->values = realloc(r->values, sizeof *r->values * (size_t)(r->nheaders + 1));
			r->names[r->nheaders] = name;
			r->values[r->nheaders++] = value;
			if (strcmp(name, "content-length") == 0)
				content_length = (size_t)strtoull(value, NULL, 10);
			else if (strcmp(name, "transfer-encoding") == 0 && has_word(value, "chunked"))
				chunked = 1;
			else if (strcmp(name, "connection") == 0)
				r->keep_alive = has_word(value, "close") ? 0 : has_word(value, "keep-alive") ? 1 : r->keep_alive;
		}
		p = e + 2;
	}
	if (chunked) {
		r->keep_alive = 0;
		web_simple(r, 411, "A body in chunks isn't taken here: send its length (Content-Length).");
		request_free(r);
		return -1;
	}
	if (content_length > WEB_MOST_BODY) {
		r->keep_alive = 0;
		web_simple(r, 413, "That was too big to take.");
		request_free(r);
		return -1;
	}
	if (c->in_len < head_len + content_length) {
		request_free(r);   /* (the body's still coming: read again when it's here) */
		return 0;
	}
	r->body = malloc(content_length + 1);
	memcpy(r->body, c->in + head_len, content_length);
	r->body[content_length] = 0;
	r->body_len = content_length;
	size_t used = head_len + content_length;
	memmove(c->in, c->in + used, c->in_len - used);
	c->in_len -= used;
	*out = r;
	return 1;
}

/* what's come on a web server's connection: each whole request to the
 * handler, and its answer sent */
static void web_readable(int64_t id, bool open)
{
	for (;;) {
		if (id > nconns || !conns[id - 1] || conns[id - 1]->fd < 0)
			return;
		Conn *c = conns[id - 1];
		int64_t sid = c->server;
		Request *r;
		int got = web_parse(id, &r);
		if (got < 0) {
			conn_closed(id);
			return;
		}
		if (got == 0)
			break;
		Server *s = servers[sid - 1];
		request_free(s->req);
		s->req = r;
		s->from = id;
		if (s->on_request.fn)
			rt_invoke_hook(s->on_request.obj, s->on_request.fn);
		if (sid > nservers || !servers[sid - 1])
			return;   /* (the server was closed in the handler) */
		s = servers[sid - 1];
		if (s->req == r && id <= nconns && conns[id - 1] && conns[id - 1]->ws) {
			/* (it's become a WebSocket: what's come after is its frames) */
			request_free(r);
			s->req = NULL;
			ws_readable(id, open);
			return;
		}
		if (s->req == r) {
			if (!r->answered)
				web_simple(r, 404, "There's nothing here.");
			int keep = r->keep_alive;
			request_free(r);
			s->req = NULL;
			if (!keep) {
				conn_closed(id);
				return;
			}
		}
	}
	if (!open)
		conn_closed(id);
}

/* ---- what networks.hby calls */

int64_t cby_web_listen(int64_t port, int here_only)
{
	int64_t id = cby_server_listen(port, here_only);
	if (id)
		servers[id - 1]->web = 1;
	return id;
}

void cby_web_on_request(int64_t sid, void *obj, void *fn) { server(sid)->on_request = (Hook){ obj, fn }; }

static int64_t web_give(Request *r, const char *s)
{
	free(r->got);
	r->got = strdup(s ? s : "");
	return give(r->got, strlen(r->got));
}

static const char *header_of(Request *r, const char *name)
{
	for (int i = 0; i < r->nheaders; i++)
		if (strcasecmp(r->names[i], name) == 0)
			return r->values[i];
	return NULL;
}

/* name=value&...: the value for name, decoded (NULL: not there) */
static char *param_of(const char *s, size_t n, const char *name)
{
	size_t nl = strlen(name);
	const char *p = s, *end = s + n;
	while (p < end) {
		const char *amp = memchr(p, '&', (size_t)(end - p));
		const char *e = amp ? amp : end;
		const char *eq = memchr(p, '=', (size_t)(e - p));
		const char *ke = eq ? eq : e;
		char *key = url_decode(p, (size_t)(ke - p), 1);
		int same = strlen(key) == nl && memcmp(key, name, nl) == 0;
		free(key);
		if (same)
			return eq ? url_decode(eq + 1, (size_t)(e - eq - 1), 1) : strdup("");
		p = e + 1;
	}
	return NULL;
}

/* 0 method, 1 path, 2 query, 3 body, 4 a header, 5 a query value, 6 a form
 * value (the body's, as a form sends it), 7 a cookie, 8 the target as sent */
int64_t cby_web_text(int64_t sid, int64_t what, const char *name)
{
	Request *r = web_request(sid);
	switch (what) {
	case 0: return web_give(r, r->method);
	case 1: return web_give(r, r->path);
	case 2: return web_give(r, r->query);
	case 3: free(r->got); r->got = malloc(r->body_len + 1); memcpy(r->got, r->body, r->body_len + 1);
		return give(r->got, r->body_len);
	case 4: return web_give(r, header_of(r, name));
	case 5: case 6: {
		char *v = what == 5 ? param_of(r->query, strlen(r->query), name) : param_of(r->body, r->body_len, name);
		int64_t n = web_give(r, v);
		free(v);
		return n;
	}
	case 7: {
		const char *all = header_of(r, "cookie");
		char *v = NULL;
		if (all) {
			/* "a=1; b=2": as a query, with "; " between */
			char *copy = strdup(all), *o = copy;
			for (const char *p = all; *p; p++)
				if (!(*p == ' ' && o > copy && o[-1] == '&'))
					*o++ = *p == ';' ? '&' : *p;
			*o = 0;
			v = param_of(copy, strlen(copy), name);
			free(copy);
		}
		int64_t n = web_give(r, v);
		free(v);
		return n;
	}
	case 8: return web_give(r, r->target);
	}
	return web_give(r, "");
}

/* whether a query value, a form value, a header or a cookie is there */
int cby_web_has(int64_t sid, int64_t what, const char *name)
{
	Request *r = web_request(sid);
	if (what == 4)
		return header_of(r, name) != NULL;
	char *v = NULL;
	if (what == 5)
		v = param_of(r->query, strlen(r->query), name);
	else if (what == 6)
		v = param_of(r->body, r->body_len, name);
	else if (what == 7) {
		cby_web_text(sid, 7, name);
		return r->got && *r->got;
	}
	int there = v != NULL;
	free(v);
	return there;
}

/* a header of the answer's own: "Cache-Control", "max-age=3600" */
void cby_web_add_header(int64_t sid, const char *name, const char *value)
{
	Request *r = web_request(sid);
	if (strpbrk(name, "\r\n:") || strpbrk(value, "\r\n"))
		rt_panic(NULL, 0, 0, "%s", "a header's name and value can't have a line end in them (or a colon in its name)");
	size_t had = r->extra ? strlen(r->extra) : 0;
	r->extra = realloc(r->extra, had + strlen(name) + strlen(value) + 5);
	sprintf(r->extra + had, "%s: %s\r\n", name, value);
}

void cby_web_send(int64_t sid, int64_t status, const char *type, const char *data, int64_t len)
{
	if (status < 100 || status > 599)
		rt_panic(NULL, 0, 0, "%s", "a status is a number from 100 to 599 (200: all's well, 404: not found)");
	web_answer(web_request(sid), (int)status, type, data, (size_t)len);
}

void cby_web_send_status(int64_t sid, int64_t status, const char *words)
{
	if (status < 100 || status > 599)
		rt_panic(NULL, 0, 0, "%s", "a status is a number from 100 to 599 (200: all's well, 404: not found)");
	web_simple(web_request(sid), (int)status, words);
}

void cby_web_redirect(int64_t sid, const char *to, int64_t status)
{
	cby_web_add_header(sid, "Location", to);
	web_simple(web_request(sid), (int)status, "It's moved.");
}

/* a file, with its type from its name: 1, or 0 if it isn't there (the
 * request isn't answered then) */
int cby_web_send_file(int64_t sid, const char *path)
{
	Request *r = web_request(sid);
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return 0;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	char *data = malloc((size_t)st.st_size + 1);
	size_t got = fread(data, 1, (size_t)st.st_size, f);
	fclose(f);
	web_answer(r, 200, type_of(path), data, got);
	free(data);
	return 1;
}

/* the file in a folder that the request's path names (index.html for a
 * folder); 1, or 0 if there's none (not answered) */
int cby_web_send_from_folder(int64_t sid, const char *folder, const char *prefix)
{
	Request *r = web_request(sid);
	const char *p = r->path;
	size_t pl = strlen(prefix);
	if (pl && strncmp(p, prefix, pl) == 0)
		p += pl;
	else if (pl)
		return 0;
	/* no way out of the folder: no "..", no NUL, nothing hidden */
	if (strstr(p, "..") || strstr(p, "/.") || p[0] == '.' || strchr(p, '\\'))
		return 0;
	size_t n = strlen(folder) + strlen(p) + 16;
	char *file = malloc(n);
	snprintf(file, n, "%s%s%s", folder, *p == '/' || !*p ? "" : "/", p);
	struct stat st;
	if (stat(file, &st) == 0 && S_ISDIR(st.st_mode)) {
		size_t fl = strlen(file);
		if (fl && file[fl - 1] != '/') {
			/* a folder without its slash: there, with it, so its links work */
			char *to = malloc(strlen(r->path) + 2);
			sprintf(to, "%s/", r->path);
			free(file);
			cby_web_redirect(sid, to, 301);
			free(to);
			return 1;
		}
		snprintf(file + fl, n - fl, "index.html");
	}
	int ok = cby_web_send_file(sid, file);
	free(file);
	return ok;
}

int cby_web_answered(int64_t sid) { return web_request(sid)->answered; }
