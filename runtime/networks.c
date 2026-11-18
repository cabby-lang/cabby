/* networks.c — the C side of the networks library (lib/networks.hby). Part
 * of Cabby's runtime, so the interpreter and compiled programs both have it.
 *
 * Downloads go through libcurl, which every Linux system has (it's what the
 * curl command uses): HTTPS, redirects, proxies and the rest. It is loaded
 * when a program first downloads something, so neither building Cabby nor
 * a program that never downloads needs it, and only the few parts of its
 * interface used here are declared. */
#include "rt.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

typedef void CURL;
typedef int CURLcode;

/* libcurl's own numbers for the options used (stable since libcurl 7) */
enum {
	OPT_WRITEDATA = 10001, OPT_URL = 10002, OPT_ERRORBUFFER = 10010, OPT_USERAGENT = 10018,
	OPT_WRITEFUNCTION = 20011, OPT_FAILONERROR = 45, OPT_FOLLOWLOCATION = 52, OPT_NOBODY = 44,
	OPT_CONNECTTIMEOUT = 78, OPT_MAXREDIRS = 68,
	OPT_POSTFIELDS = 10015, OPT_POSTFIELDSIZE_LARGE = 30120, OPT_CUSTOMREQUEST = 10036, OPT_HTTPHEADER = 10023,
	OPT_HEADERFUNCTION = 20079, OPT_HEADERDATA = 10029, OPT_XFERINFOFUNCTION = 20219, OPT_XFERINFODATA = 10057,
	OPT_NOPROGRESS = 43, OPT_POST = 47, OPT_TIMEOUT = 13, OPT_CAINFO = 10065,
};
struct curl_slist;
#define INFO_RESPONSE_CODE 0x200002
#define INFO_CAINFO 0x10003D   /* (CURLINFO_CAINFO: the certificates it trusts, to begin with) */

static struct {
	bool tried, ok;
	int (*global_init)(long);
	CURL *(*init)(void);
	CURLcode (*setopt)(CURL *, int, ...);
	CURLcode (*perform)(CURL *);
	CURLcode (*getinfo)(CURL *, int, ...);
	void (*cleanup)(CURL *);
	const char *(*strerror)(CURLcode);
	struct curl_slist *(*slist_append)(struct curl_slist *, const char *);
	void (*slist_free_all)(struct curl_slist *);
} curl;

void cby_tls_preload(void);   /* (connections.c's) */

static void load_curl(void)
{
	if (curl.tried) {
		if (!curl.ok)
			rt_panic(NULL, 0, 0, "the networks library needs libcurl, which comes with the 'curl' package; "
				 "install it and try again");
		return;
	}
	curl.tried = true;
#ifndef __APPLE__
	/* (OpenSSL first, for encrypted connections, if it's there: libcurl
	 * is built on it, and so finds it loaded; the other way round, the
	 * system's loader keeps a list it loses track of) */
	cby_tls_preload();
#endif
	static const char *const names[] = { "libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4", "libcurl.4.dylib",
					      "libcurl.dylib", NULL };
	void *h = NULL;
	for (int i = 0; names[i] && !h; i++)
		h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (h) {
		/* (through memcpy: C has no direct cast from dlsym's pointer to a function's) */
#define LOAD(field, name) do { void *p_ = dlsym(h, name); memcpy(&curl.field, &p_, sizeof p_); } while (0)
		LOAD(global_init, "curl_global_init");
		LOAD(init, "curl_easy_init");
		LOAD(setopt, "curl_easy_setopt");
		LOAD(perform, "curl_easy_perform");
		LOAD(getinfo, "curl_easy_getinfo");
		LOAD(cleanup, "curl_easy_cleanup");
		LOAD(strerror, "curl_easy_strerror");
		LOAD(slist_append, "curl_slist_append");
		LOAD(slist_free_all, "curl_slist_free_all");
#undef LOAD
		curl.ok = curl.global_init && curl.init && curl.setopt && curl.perform && curl.getinfo && curl.cleanup &&
			  curl.strerror && curl.slist_append && curl.slist_free_all;
	}
	if (curl.ok)
		curl.global_init(3);   /* CURL_GLOBAL_ALL */
	load_curl();
}

/* ---- certificates the program trusts, as well as the system's
 * (networks.trust_certificate): a file of them all, for libcurl, which can
 * only be given one */

static char *trusted_file;     /* (in the system's temporary folder; NULL: none) */
static char *trusted_extra;    /* the program's own, one after another */

static void trusted_gone(void)
{
	if (trusted_file)
		unlink(trusted_file);
	free(trusted_file);
	free(trusted_extra);
	trusted_file = trusted_extra = NULL;
}

static char *whole_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	char *all = NULL;
	size_t n = 0, cap = 0, r;
	char buf[8192];
	while ((r = fread(buf, 1, sizeof buf, f)) > 0) {
		if (n + r + 1 > cap) {
			cap = (n + r + 1) * 2;
			all = realloc(all, cap);
		}
		memcpy(all + n, buf, r);
		n += r;
	}
	fclose(f);
	if (!all)
		all = calloc(1, 1);
	all[n] = 0;
	*len = n;
	return all;
}

void cby_networks_trust(const char *path)
{
	size_t n = 0;
	char *cert = whole_file(path, &n);
	if (!cert)
		rt_panic(NULL, 0, 0, "can't read the certificate '%s'", path);
	size_t had = trusted_extra ? strlen(trusted_extra) : 0;
	trusted_extra = realloc(trusted_extra, had + n + 2);
	memcpy(trusted_extra + had, cert, n);
	trusted_extra[had + n] = '\n';
	trusted_extra[had + n + 1] = 0;
	free(cert);
	/* the system's (where libcurl looks to begin with), then the program's */
	load_curl();
	const char *usual = NULL;
	CURL *c = curl.init();
	if (c) {
		char *info = NULL;
		if (curl.getinfo(c, INFO_CAINFO, &info) == 0 && info && *info)
			usual = info;
		static const char *const known[] = { "/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem",
						     "/etc/pki/tls/certs/ca-bundle.crt", "/opt/homebrew/etc/ca-certificates/cert.pem",
						     "/usr/local/etc/ca-certificates/cert.pem", NULL };
		for (int i = 0; !usual && known[i]; i++)
			if (access(known[i], R_OK) == 0)
				usual = known[i];
	}
	size_t ulen = 0;
	char *system = usual ? whole_file(usual, &ulen) : NULL;
	if (c)
		curl.cleanup(c);
	if (!trusted_file) {
		const char *tmp = getenv("TMPDIR");
		char name[4096];
		snprintf(name, sizeof name, "%s/cabby-trusted-XXXXXX", tmp && *tmp ? tmp : "/tmp");
		int fd = mkstemp(name);
		if (fd < 0)
			rt_panic(NULL, 0, 0, "%s", "can't make a file of trusted certificates in the temporary folder");
		close(fd);
		trusted_file = strdup(name);
		atexit(trusted_gone);
	}
	FILE *f = fopen(trusted_file, "wb");
	if (!f)
		rt_panic(NULL, 0, 0, "%s", "can't write the file of trusted certificates");
	if (system) {
		fwrite(system, 1, ulen, f);
		fputc('\n', f);
	}
	fputs(trusted_extra, f);
	fclose(f);
	free(system);
}

static void trust_for(CURL *c)
{
	if (trusted_file)
		curl.setopt(c, OPT_CAINFO, trusted_file);
}

/* what was downloaded, until Cabby takes it (cby_networks_take) */
static _Thread_local char *got;
static _Thread_local size_t got_len, got_cap;

static size_t to_memory(char *data, size_t size, size_t n, void *unused)
{
	(void)unused;
	size_t more = size * n;
	if (got_len + more + 1 > got_cap) {
		size_t cap = got_cap ? got_cap * 2 : 16384;
		while (cap < got_len + more + 1)
			cap *= 2;
		char *g = realloc(got, cap);
		if (!g)
			return 0;   /* libcurl stops: out of memory */
		got = g;
		got_cap = cap;
	}
	memcpy(got + got_len, data, more);
	got_len += more;
	return more;
}

static size_t to_file(char *data, size_t size, size_t n, void *f) { return fwrite(data, size, n, (FILE *)f); }

static const char *status_words(long code)
{
	switch (code) {
	case 400: return "Bad Request";
	case 401: return "Unauthorized: it needs a login";
	case 403: return "Forbidden: the site refused";
	case 404: return "Not Found: nothing is at that address";
	case 405: return "Method Not Allowed";
	case 408: return "Request Timeout";
	case 410: return "Gone";
	case 429: return "Too Many Requests: try again later";
	case 500: return "Internal Server Error: the site has a problem";
	case 502: return "Bad Gateway";
	case 503: return "Service Unavailable: try again later";
	case 504: return "Gateway Timeout";
	default: return code >= 500 ? "a problem at the site" : "refused";
	}
}

/* Fetches a URL. file: write there instead of to memory. head: only ask
 * whether it's there. Gives the HTTP status (200: fine), or 0 if it never
 * got an answer, with the reason in why. */
static long fetch(const char *url, FILE *file, bool head, char *why, size_t why_size)
{
	load_curl();
	CURL *c = curl.init();
	if (!c) {
		snprintf(why, why_size, "libcurl couldn't start");
		return 0;
	}
	char err[256] = "";
	curl.setopt(c, OPT_URL, url);
	curl.setopt(c, OPT_FOLLOWLOCATION, 1L);
	curl.setopt(c, OPT_MAXREDIRS, 20L);
	curl.setopt(c, OPT_CONNECTTIMEOUT, 30L);
	curl.setopt(c, OPT_USERAGENT, "Cabby");
	curl.setopt(c, OPT_ERRORBUFFER, err);
	trust_for(c);
	if (head)
		curl.setopt(c, OPT_NOBODY, 1L);
	if (file) {
		curl.setopt(c, OPT_WRITEFUNCTION, to_file);
		curl.setopt(c, OPT_WRITEDATA, (void *)file);
	} else {
		got_len = 0;
		curl.setopt(c, OPT_WRITEFUNCTION, to_memory);
		curl.setopt(c, OPT_WRITEDATA, (void *)NULL);
	}
	CURLcode r = curl.perform(c);
	long code = 0;
	curl.getinfo(c, INFO_RESPONSE_CODE, &code);
	if (r != 0) {
		snprintf(why, why_size, "%s", err[0] ? err : curl.strerror(r));
		code = 0;
	} else if (code == 0) {
		code = 200;   /* file:// and the like have no HTTP status: it worked */
	}
	curl.cleanup(c);
	return code;
}

static void check(const char *url, long code, const char *why)
{
	if (code == 0)
		rt_panic(NULL, 0, 0, "can't download '%s': %s", url, why);
	if (code >= 400)
		rt_panic(NULL, 0, 0, "can't download '%s': %ld %s", url, code, status_words(code));
}

/* networks.download: the length; the bytes wait for cby_networks_take */
int64_t cby_networks_download(const char *url)
{
	char why[300];
	long code = fetch(url, NULL, false, why, sizeof why);
	check(url, code, why);
	return (int64_t)got_len;
}

/* networks.download_bytes: straight into a bytes value */
int64_t cby_networks_download_bytes(const char *url)
{
	char why[300];
	long code = fetch(url, NULL, false, why, sizeof why);
	check(url, code, why);
	int64_t b = rt_bytes_make(got, (int64_t)got_len);
	free(got);
	got = NULL;
	got_len = got_cap = 0;
	return b;
}

void cby_networks_take(char *dst)
{
	if (got_len)
		memcpy(dst, got, got_len);
	free(got);
	got = NULL;
	got_len = got_cap = 0;
}

/* networks.download_to: straight into a file */
void cby_networks_download_to(const char *url, const char *path)
{
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.download", path);
	FILE *f = fopen(tmp, "wb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't write '%s' to download into", path);
	char why[300];
	long code = fetch(url, f, false, why, sizeof why);
	fclose(f);
	if (code == 0 || code >= 400)
		remove(tmp);   /* nothing half-downloaded is left behind */
	check(url, code, why);
	if (rename(tmp, path) != 0) {
		remove(tmp);
		rt_panic(NULL, 0, 0, "can't save the download as '%s'", path);
	}
}

/* networks.status: the HTTP status the address answers with, 0 for none */
int64_t cby_networks_status(const char *url)
{
	char why[300];
	return fetch(url, NULL, true, why, sizeof why);
}

/* a web address that can't be read: a panic at the line that read it */
void cby_networks_bad_url(const char *why)
{
	rt_panic(NULL, 0, 0, "%s", why);
}

/* ------------------------------------------------------------ requests of every kind
 *
 * A request (networks.request): a method (GET, POST, PUT, DELETE...),
 * headers, and something to send; then sent, and its answer kept: the
 * status, its headers and what came back. Unlike download, an answer of
 * 404 or 500 isn't a panic: the program looks at the status. Only no
 * answer at all (no connection, a name that isn't there) is status 0,
 * with why in error. A request can say how a big transfer is going, to a
 * handler, as it goes. */

typedef struct {
	char *url, *method, *error;
	struct curl_slist *headers;
	char *body;
	size_t body_len;
	bool has_body;
	long status;
	char *answer;            /* what came back */
	size_t answer_len, answer_cap;
	char *answer_headers;    /* "Name: value" lines */
	size_t ah_len, ah_cap;
	void *progress_obj, *progress_fn;
	int64_t received, total, sent, to_send;
	double last_said;        /* (when progress was last said, in seconds) */
	int timeout;
} Request;

static Request **reqs;
static int64_t nreqs;

static Request *req(int64_t id)
{
	if (id < 1 || id > nreqs || !reqs[id - 1])
		rt_panic(NULL, 0, 0, "that request has been dropped");
	return reqs[id - 1];
}

int64_t cby_net_request_new(const char *url)
{
	Request *r = calloc(1, sizeof *r);
	r->url = strdup(url);
	r->method = strdup("GET");
	r->error = strdup("");
	int64_t i = 0;
	while (i < nreqs && reqs[i])
		i++;
	if (i == nreqs) {
		reqs = realloc(reqs, sizeof *reqs * (size_t)(nreqs + 1));
		nreqs++;
	}
	reqs[i] = r;
	return i + 1;
}

void cby_net_request_free(int64_t id)
{
	if (id < 1 || id > nreqs || !reqs[id - 1])
		return;
	Request *r = reqs[id - 1];
	free(r->url);
	free(r->method);
	free(r->error);
	free(r->body);
	free(r->answer);
	free(r->answer_headers);
	if (r->headers && curl.slist_free_all)
		curl.slist_free_all(r->headers);
	free(r);
	reqs[id - 1] = NULL;
}

void cby_net_request_method(int64_t id, const char *method)
{
	Request *r = req(id);
	free(r->method);
	r->method = strdup(method);
	for (char *c = r->method; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c = (char)(*c - 32);
}

void cby_net_request_header(int64_t id, const char *name, const char *value)
{
	load_curl();
	Request *r = req(id);
	size_t n = strlen(name) + strlen(value) + 3;
	char *line = malloc(n);
	snprintf(line, n, "%s: %s", name, value);
	r->headers = curl.slist_append(r->headers, line);
	free(line);
}

/* what's sent: text (any bytes), and its kind, as a header */
void cby_net_request_body(int64_t id, const char *data, int64_t len)
{
	Request *r = req(id);
	free(r->body);
	r->body = malloc((size_t)len + 1);
	memcpy(r->body, data, (size_t)len);
	r->body[len] = 0;
	r->body_len = (size_t)len;
	r->has_body = true;
}

void cby_net_request_body_bytes(int64_t id, int64_t bytes)
{
	int64_t len = 0;
	const uint8_t *d = rt_bytes_data(bytes, &len);
	cby_net_request_body(id, (const char *)d, len);
}

void cby_net_request_timeout(int64_t id, int64_t seconds) { req(id)->timeout = (int)seconds; }

void cby_net_request_on_progress(int64_t id, void *obj, void *fn)
{
	Request *r = req(id);
	r->progress_obj = obj;
	r->progress_fn = fn;
}

static size_t to_answer(char *data, size_t size, size_t n, void *p)
{
	Request *r = p;
	size_t more = size * n;
	if (r->answer_len + more + 1 > r->answer_cap) {
		size_t cap = r->answer_cap ? r->answer_cap * 2 : 16384;
		while (cap < r->answer_len + more + 1)
			cap *= 2;
		char *a = realloc(r->answer, cap);
		if (!a)
			return 0;
		r->answer = a;
		r->answer_cap = cap;
	}
	memcpy(r->answer + r->answer_len, data, more);
	r->answer_len += more;
	r->answer[r->answer_len] = 0;
	return more;
}

static size_t to_headers(char *data, size_t size, size_t n, void *p)
{
	Request *r = p;
	size_t more = size * n;
	/* a new answer (after a redirect, or "100 Continue"): its own headers */
	if (more >= 5 && memcmp(data, "HTTP/", 5) == 0)
		r->ah_len = 0;
	if (r->ah_len + more + 1 > r->ah_cap) {
		r->ah_cap = (r->ah_len + more + 1) * 2;
		r->answer_headers = realloc(r->answer_headers, r->ah_cap);
	}
	memcpy(r->answer_headers + r->ah_len, data, more);
	r->ah_len += more;
	r->answer_headers[r->ah_len] = 0;
	return more;
}

static double seconds_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + t.tv_nsec / 1e9;
}

/* libcurl, as the transfer goes: told to the program's handler, ten times
 * a second at most (and at the end) */
static int on_transfer(void *p, int64_t dl_total, int64_t dl_now, int64_t ul_total, int64_t ul_now)
{
	Request *r = p;
	bool changed = dl_now != r->received || ul_now != r->sent || dl_total != r->total || ul_total != r->to_send;
	r->received = dl_now;
	r->total = dl_total;
	r->sent = ul_now;
	r->to_send = ul_total;
	double now = seconds_now();
	if (changed && r->progress_fn && now - r->last_said >= 0.1) {
		r->last_said = now;
		rt_invoke_hook(r->progress_obj, r->progress_fn);
	}
	return 0;
}

/* sends it; to a file (path) or into memory: its status (0: no answer) */
static long request_send(Request *r, FILE *file)
{
	load_curl();
	CURL *c = curl.init();
	if (!c) {
		free(r->error);
		r->error = strdup("libcurl couldn't start");
		return r->status = 0;
	}
	char err[256] = "";
	r->answer_len = r->ah_len = 0;
	r->received = r->total = r->sent = r->to_send = 0;
	r->last_said = 0;
	curl.setopt(c, OPT_URL, r->url);
	curl.setopt(c, OPT_FOLLOWLOCATION, 1L);
	curl.setopt(c, OPT_MAXREDIRS, 20L);
	curl.setopt(c, OPT_CONNECTTIMEOUT, 30L);
	if (r->timeout > 0)
		curl.setopt(c, OPT_TIMEOUT, (long)r->timeout);
	curl.setopt(c, OPT_USERAGENT, "Cabby");
	curl.setopt(c, OPT_ERRORBUFFER, err);
	trust_for(c);
	if (r->has_body) {
		curl.setopt(c, OPT_POST, 1L);
		curl.setopt(c, OPT_POSTFIELDSIZE_LARGE, (int64_t)r->body_len);
		curl.setopt(c, OPT_POSTFIELDS, r->body);
	}
	if (strcmp(r->method, "HEAD") == 0)
		curl.setopt(c, OPT_NOBODY, 1L);
	else if (strcmp(r->method, "GET") != 0 && !(strcmp(r->method, "POST") == 0 && r->has_body))
		curl.setopt(c, OPT_CUSTOMREQUEST, r->method);
	if (r->headers)
		curl.setopt(c, OPT_HTTPHEADER, r->headers);
	curl.setopt(c, OPT_HEADERFUNCTION, to_headers);
	curl.setopt(c, OPT_HEADERDATA, (void *)r);
	if (file) {
		curl.setopt(c, OPT_WRITEFUNCTION, to_file);
		curl.setopt(c, OPT_WRITEDATA, (void *)file);
	} else {
		curl.setopt(c, OPT_WRITEFUNCTION, to_answer);
		curl.setopt(c, OPT_WRITEDATA, (void *)r);
	}
	if (r->progress_fn) {
		curl.setopt(c, OPT_XFERINFOFUNCTION, on_transfer);
		curl.setopt(c, OPT_XFERINFODATA, (void *)r);
		curl.setopt(c, OPT_NOPROGRESS, 0L);
	}
	CURLcode res = curl.perform(c);
	long code = 0;
	curl.getinfo(c, INFO_RESPONSE_CODE, &code);
	free(r->error);
	if (res != 0) {
		r->error = strdup(err[0] ? err : curl.strerror(res));
		code = 0;
	} else {
		r->error = strdup("");
		if (code == 0)
			code = 200;   /* (file:// and the like have no HTTP status: it worked) */
	}
	curl.cleanup(c);
	if (r->progress_fn && code) {
		/* (the end, said once more, so a bar reaches the end) */
		if (r->total <= 0)
			r->total = r->received;
		rt_invoke_hook(r->progress_obj, r->progress_fn);
	}
	return r->status = code;
}

int64_t cby_net_request_send(int64_t id) { return request_send(req(id), NULL); }

/* the answer saved to a file, as it comes (nothing is left of it if there
 * was no answer, or an error's: 400 and over) */
int64_t cby_net_request_save_to(int64_t id, const char *path)
{
	Request *r = req(id);
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.download", path);
	FILE *f = fopen(tmp, "wb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't write '%s' to download into", path);
	long code = request_send(r, f);
	fclose(f);
	if (code == 0 || code >= 400) {
		remove(tmp);
		return code;
	}
	if (rename(tmp, path) != 0) {
		remove(tmp);
		rt_panic(NULL, 0, 0, "can't save the download as '%s'", path);
	}
	return code;
}

/* 0 received, 1 to receive (0: not known), 2 sent, 3 to send */
int64_t cby_net_request_progress(int64_t id, int64_t what)
{
	Request *r = req(id);
	return what == 0 ? r->received : what == 1 ? r->total : what == 2 ? r->sent : r->to_send;
}

/* text back to Cabby: its length now, then copied by cby_net_take */
static _Thread_local const char *giving;
static _Thread_local size_t giving_len;

static int64_t give(const char *p, size_t n)
{
	giving = p;
	giving_len = n;
	return (int64_t)n;
}

void cby_net_take(char *dst)
{
	if (giving_len)
		memcpy(dst, giving, giving_len);
}

/* 0 what came back, 1 why there was no answer, 2 the answer's headers */
int64_t cby_net_request_text(int64_t id, int64_t what)
{
	Request *r = req(id);
	if (what == 0)
		return give(r->answer ? r->answer : "", r->answer_len);
	if (what == 1)
		return give(r->error, strlen(r->error));
	return give(r->answer_headers ? r->answer_headers : "", r->ah_len);
}

int64_t cby_net_request_answer_bytes(int64_t id)
{
	Request *r = req(id);
	return rt_bytes_make(r->answer ? r->answer : "", (int64_t)r->answer_len);
}

/* one of the answer's headers, by its name (any capitals): "" if it has
 * none by that name */
static _Thread_local char *header_found;

int64_t cby_net_request_answer_header(int64_t id, const char *name)
{
	Request *r = req(id);
	free(header_found);
	header_found = NULL;
	size_t n = strlen(name);
	for (const char *line = r->answer_headers; line && *line;) {
		const char *end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (len > n && strncasecmp(line, name, n) == 0 && line[n] == ':') {
			const char *v = line + n + 1;
			while (*v == ' ' || *v == '\t')
				v++;
			size_t vl = (size_t)(line + len - v);
			while (vl && (v[vl - 1] == '\r' || v[vl - 1] == ' '))
				vl--;
			header_found = strndup(v, vl);
		}
		line = end ? end + 1 : line + len;
	}
	return header_found ? give(header_found, strlen(header_found)) : give("", 0);
}

__attribute__((destructor)) static void requests_end(void)
{
	for (int64_t i = 0; i < nreqs; i++)
		cby_net_request_free(i + 1);
	free(reqs);
	free(header_found);
}

/* for the shortcuts (networks.post...), which panic as download does: no
 * answer, or an error's (400 and over) */
void cby_net_request_check(int64_t id, const char *what)
{
	Request *r = req(id);
	if (r->status == 0)
		rt_panic(NULL, 0, 0, "can't %s '%s': %s", what, r->url, r->error);
	if (r->status >= 400) {
		/* (with the start of what it said, which often says why) */
		char said[200] = "";
		if (r->answer_len) {
			size_t n = r->answer_len < 150 ? r->answer_len : 150;
			snprintf(said, sizeof said, " (it said: %.*s%s)", (int)n, r->answer, r->answer_len > 150 ? "..." : "");
			for (char *c = said; *c; c++)
				if (*c == '\n' || *c == '\r')
					*c = ' ';
		}
		rt_panic(NULL, 0, 0, "can't %s '%s': %ld %s%s", what, r->url, r->status, status_words(r->status), said);
	}
}

int64_t cby_net_request_send_status(int64_t id) { return req(id)->status; }
