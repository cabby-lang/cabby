/* tls.h — encrypted connections (TLS), for the networks library (included
 * by connections.c): HTTPS and wss:// for a web server, encrypted
 * connections between programs, and certificates made for trying them.
 *
 * It's OpenSSL's libssl, loaded when it's first needed, as the secrets
 * library loads libcrypto: libssl.so.3 on Linux (every system has it,
 * since curl and so much else need it), Homebrew's OpenSSL (3 or later) on a Mac
 * (whose own /usr/lib/libssl stops any program that loads it). Nothing of
 * the encrypting is Cabby's own.
 *
 * A connection with TLS reads and writes through it (fill and
 * cby_conn_send, in connections.c): its socket doesn't wait, so the event
 * loop is never held up by a slow or half-finished one, and whatever TLS
 * has decrypted is read out whole each time. A server's connection shakes
 * hands as what comes allows; on_connect is told once it has. One this end
 * makes shakes hands as it's made, checking the server's certificate as a
 * browser does: by the system's trusted certificates, and any the program
 * trusts as well (networks.trust_certificate). */

#include <dlfcn.h>

#define TLS_ERROR_SSL 1
#define TLS_ERROR_WANT_READ 2
#define TLS_ERROR_WANT_WRITE 3
#define TLS_ERROR_SYSCALL 5
#define TLS_ERROR_ZERO_RETURN 6

static struct {
	int tried, ok, can_make;
	void *ssl, *crypto;
	const void *(*server_method)(void);
	const void *(*client_method)(void);
	void *(*ctx_new)(const void *method);
	void (*ctx_free)(void *ctx);
	int (*use_chain)(void *ctx, const char *file);
	int (*use_key)(void *ctx, const char *file, int type);
	int (*check_key)(const void *ctx);
	int (*default_paths)(void *ctx);
	int (*load_locations)(void *ctx, const char *file, const char *dir);
	void (*set_verify)(void *ctx, int mode, void *callback);
	long (*ctx_ctrl)(void *ctx, int cmd, long larg, void *parg);
	void *(*new_)(void *ctx);
	void (*free_)(void *ssl);
	int (*set_fd)(void *ssl, int fd);
	int (*accept_)(void *ssl);
	int (*connect_)(void *ssl);
	int (*read_)(void *ssl, void *buf, int n);
	int (*write_)(void *ssl, const void *buf, int n);
	int (*shutdown_)(void *ssl);
	int (*get_error)(const void *ssl, int ret);
	long (*ctrl)(void *ssl, int cmd, long larg, void *parg);
	int (*set1_host)(void *ssl, const char *host);
	void *(*get0_param)(void *ssl);
	long (*verify_result)(const void *ssl);
	int (*param_ip)(void *param, const char *ip);
	const char *(*verify_string)(long n);
	unsigned long (*err_get)(void);
	void (*err_string)(unsigned long e, char *buf, size_t len);
	void (*err_clear)(void);
	/* making a certificate */
	void *(*q_keygen)(void *libctx, const char *propq, const char *type, ...);
	void *(*x509_new)(void);
	void (*x509_free)(void *x);
	int (*set_version)(void *x, long v);
	void *(*serial)(void *x);
	int (*asn1_int_set)(void *a, long v);
	void *(*not_before)(void *x);
	void *(*not_after)(void *x);
	void *(*gmtime_adj)(void *t, long secs);
	int (*set_pubkey)(void *x, void *key);
	void *(*subject)(void *x);
	int (*name_add)(void *name, const char *field, int type, const unsigned char *bytes, int len, int loc, int set);
	int (*set_issuer)(void *x, void *name);
	void *(*ext_conf)(void *conf, void *ctx, int nid, const char *value);
	int (*add_ext)(void *x, void *ext, int loc);
	void (*ext_free)(void *ext);
	const void *(*sha256)(void);
	int (*sign)(void *x, void *key, const void *md);
	int (*write_x509)(FILE *f, void *x);
	int (*write_key)(FILE *f, void *key, const void *enc, unsigned char *kstr, int klen, void *cb, void *u);
	void (*key_free)(void *key);
} T;

static void *tls_client;       /* the SSL_CTX for connections this end makes */
static char **tls_trusted;     /* certificates the program trusts as well */
static int ntls_trusted;

static const char *tls_missing;   /* (the first function the OpenSSL found didn't have) */

static void *tls_sym(void *h, const char *name)
{
	void *p = dlsym(h, name);
	if (!p) {
		T.ok = 0;
		if (!tls_missing)
			tls_missing = name;
	}
	return p;
}

#define TLS_SYM(field, lib, name) memcpy(&T.field, &(void *){ tls_sym(T.lib, name) }, sizeof T.field)
/* (one only making certificates needs: TLS works without it) */
#define TLS_OPT(field, lib, name) memcpy(&T.field, &(void *){ dlsym(T.lib, name) }, sizeof T.field)

/* loads OpenSSL, once: true if it's there */
static int tls_load(void)
{
	if (T.tried)
		return T.ok;
	T.tried = 1;
#ifdef __APPLE__
	/* (Homebrew's: 3 or 4, or whichever "openssl" is now, on an Apple
	 * processor or Intel; libssl and libcrypto from the same one) */
	static const char *const ssls[] = { "/opt/homebrew/opt/openssl@3/lib/libssl.3.dylib",
					    "/opt/homebrew/opt/openssl@4/lib/libssl.4.dylib",
					    "/opt/homebrew/opt/openssl/lib/libssl.dylib",
					    "/usr/local/opt/openssl@3/lib/libssl.3.dylib",
					    "/usr/local/opt/openssl@4/lib/libssl.4.dylib",
					    "/usr/local/opt/openssl/lib/libssl.dylib", NULL };
	static const char *const cryptos[] = { "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib",
					       "/opt/homebrew/opt/openssl@4/lib/libcrypto.4.dylib",
					       "/opt/homebrew/opt/openssl/lib/libcrypto.dylib",
					       "/usr/local/opt/openssl@3/lib/libcrypto.3.dylib",
					       "/usr/local/opt/openssl@4/lib/libcrypto.4.dylib",
					       "/usr/local/opt/openssl/lib/libcrypto.dylib", NULL };
#else
	static const char *const ssls[] = { "libssl.so.3", "libssl.so.4", "libssl.so", NULL };
	static const char *const cryptos[] = { "libcrypto.so.3", "libcrypto.so.4", "libcrypto.so", NULL };
#endif
	/* (libcrypto first: libssl is built on it; the libssl that goes with it) */
	int which = -1;
	for (int i = 0; cryptos[i] && !T.crypto; i++)
		if ((T.crypto = dlopen(cryptos[i], RTLD_NOW | RTLD_LOCAL)))
			which = i;
	if (which >= 0)
		T.ssl = dlopen(ssls[which], RTLD_NOW | RTLD_LOCAL);
	for (int i = 0; ssls[i] && !T.ssl; i++)
		T.ssl = dlopen(ssls[i], RTLD_NOW | RTLD_LOCAL);
	if (!T.ssl || !T.crypto)
		return 0;
	T.ok = 1;
	TLS_SYM(server_method, ssl, "TLS_server_method");
	TLS_SYM(client_method, ssl, "TLS_client_method");
	TLS_SYM(ctx_new, ssl, "SSL_CTX_new");
	TLS_SYM(ctx_free, ssl, "SSL_CTX_free");
	TLS_SYM(use_chain, ssl, "SSL_CTX_use_certificate_chain_file");
	TLS_SYM(use_key, ssl, "SSL_CTX_use_PrivateKey_file");
	TLS_SYM(check_key, ssl, "SSL_CTX_check_private_key");
	TLS_SYM(default_paths, ssl, "SSL_CTX_set_default_verify_paths");
	TLS_SYM(load_locations, ssl, "SSL_CTX_load_verify_locations");
	TLS_SYM(set_verify, ssl, "SSL_CTX_set_verify");
	TLS_SYM(ctx_ctrl, ssl, "SSL_CTX_ctrl");
	TLS_SYM(new_, ssl, "SSL_new");
	TLS_SYM(free_, ssl, "SSL_free");
	TLS_SYM(set_fd, ssl, "SSL_set_fd");
	TLS_SYM(accept_, ssl, "SSL_accept");
	TLS_SYM(connect_, ssl, "SSL_connect");
	TLS_SYM(read_, ssl, "SSL_read");
	TLS_SYM(write_, ssl, "SSL_write");
	TLS_SYM(shutdown_, ssl, "SSL_shutdown");
	TLS_SYM(get_error, ssl, "SSL_get_error");
	TLS_SYM(ctrl, ssl, "SSL_ctrl");
	TLS_SYM(set1_host, ssl, "SSL_set1_host");
	TLS_SYM(get0_param, ssl, "SSL_get0_param");
	TLS_SYM(verify_result, ssl, "SSL_get_verify_result");
	TLS_SYM(param_ip, crypto, "X509_VERIFY_PARAM_set1_ip_asc");
	TLS_SYM(verify_string, crypto, "X509_verify_cert_error_string");
	TLS_SYM(err_get, crypto, "ERR_get_error");
	TLS_SYM(err_string, crypto, "ERR_error_string_n");
	TLS_SYM(err_clear, crypto, "ERR_clear_error");
	TLS_OPT(q_keygen, crypto, "EVP_PKEY_Q_keygen");
	TLS_OPT(x509_new, crypto, "X509_new");
	TLS_OPT(x509_free, crypto, "X509_free");
	TLS_OPT(set_version, crypto, "X509_set_version");
	TLS_OPT(serial, crypto, "X509_get_serialNumber");
	TLS_OPT(asn1_int_set, crypto, "ASN1_INTEGER_set");
	TLS_OPT(not_before, crypto, "X509_getm_notBefore");
	TLS_OPT(not_after, crypto, "X509_getm_notAfter");
	TLS_OPT(gmtime_adj, crypto, "X509_gmtime_adj");
	TLS_OPT(set_pubkey, crypto, "X509_set_pubkey");
	TLS_OPT(subject, crypto, "X509_get_subject_name");
	TLS_OPT(name_add, crypto, "X509_NAME_add_entry_by_txt");
	TLS_OPT(set_issuer, crypto, "X509_set_issuer_name");
	TLS_OPT(ext_conf, crypto, "X509V3_EXT_conf_nid");
	if (!T.ext_conf)
		TLS_OPT(ext_conf, crypto, "X509V3_EXT_nconf_nid");   /* (the newer name for it) */
	TLS_OPT(add_ext, crypto, "X509_add_ext");
	TLS_OPT(ext_free, crypto, "X509_EXTENSION_free");
	TLS_OPT(sha256, crypto, "EVP_sha256");
	TLS_OPT(sign, crypto, "X509_sign");
	TLS_OPT(write_x509, crypto, "PEM_write_X509");
	TLS_OPT(write_key, crypto, "PEM_write_PrivateKey");
	TLS_OPT(key_free, crypto, "EVP_PKEY_free");
	T.can_make = T.q_keygen && T.x509_new && T.x509_free && T.set_version && T.serial && T.asn1_int_set && T.not_before &&
		     T.not_after && T.gmtime_adj && T.set_pubkey && T.subject && T.name_add && T.set_issuer && T.ext_conf &&
		     T.add_ext && T.ext_free && T.sha256 && T.sign && T.write_x509 && T.write_key && T.key_free;
	return T.ok;
}

/* (for networks.c: loaded before libcurl is) */
void cby_tls_preload(void) { tls_load(); }

static void tls_need(void)
{
	if (!tls_load() && tls_missing)
		rt_panic(NULL, 0, 0, "the OpenSSL found has no %s, which encrypted connections need: it's too old, or too new, for this Cabby", tls_missing);
	if (!T.ok)
		rt_panic(NULL, 0, 0, "%s",
#ifdef __APPLE__
			 "encrypted connections need OpenSSL 3 or later: brew install openssl"
#else
			 "encrypted connections need OpenSSL 3 or later's libssl (the 'openssl' package on Arch, 'libssl3' on Debian)"
#endif
		);
}

/* why OpenSSL said no, as words (and its errors cleared) */
static void tls_why(char *out, size_t n)
{
	unsigned long e = T.err_get(), last = e;
	while (e) {
		last = e;
		e = T.err_get();
	}
	if (last)
		T.err_string(last, out, n);
	else
		snprintf(out, n, "%s", "the connection closed");
}

/* OpenSSL writes to the socket itself, so a connection whose other end
 * has gone would stop the program with SIGPIPE (a server sends its
 * session tickets after the handshake, by which time a client may have
 * closed). A Mac's sockets are told not to (SO_NOSIGPIPE, in new_conn);
 * on Linux, SIGPIPE is held back while OpenSSL works, and one that came
 * is let go of, unseen. */
typedef struct {
	sigset_t old;
	int had;
} Quiet;

static void tls_quiet_begin(Quiet *q)
{
#ifndef __APPLE__
	sigset_t pipe_;
	sigemptyset(&pipe_);
	sigaddset(&pipe_, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &pipe_, &q->old);
	sigset_t pending;
	sigpending(&pending);
	q->had = sigismember(&pending, SIGPIPE);   /* (one already waiting isn't OpenSSL's) */
#else
	(void)q;
#endif
}

static void tls_quiet_end(Quiet *q)
{
#ifndef __APPLE__
	if (!q->had) {
		sigset_t pipe_;
		sigemptyset(&pipe_);
		sigaddset(&pipe_, SIGPIPE);
		struct timespec none = { 0, 0 };
		while (sigtimedwait(&pipe_, NULL, &none) == SIGPIPE) {
		}
	}
	pthread_sigmask(SIG_SETMASK, &q->old, NULL);
#else
	(void)q;
#endif
}

static void tls_nonblocking(int fd)
{
	int fl = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* waits until the socket can be read (or written): false after a minute */
static bool tls_wait(int fd, int write)
{
	struct pollfd p = { fd, (short)(write ? POLLOUT : POLLIN), 0 };
	int r;
	do
		r = poll(&p, 1, 60000);
	while (r < 0 && errno == EINTR);
	return r > 0;
}

/* ------------------------------------------------------------ reading and writing */

/* what TLS has for the connection, onto its buffer (as fill does):
 * 1 something came, 0 nothing yet (it's still open), -1 closed. With
 * wait, it waits for something (never 0). */
static int tls_fill(Conn *c, bool wait)
{
	int got = 0;
	for (;;) {
		if (c->in_cap - c->in_len < 4096) {
			c->in_cap = c->in_cap ? c->in_cap * 2 : 8192;
			c->in = realloc(c->in, c->in_cap);
		}
		T.err_clear();   /* (SSL_get_error goes by what's on the queue: nothing older) */
		Quiet q;
		tls_quiet_begin(&q);
		int n = T.read_(c->tls, c->in + c->in_len, (int)(c->in_cap - c->in_len - 1));
		tls_quiet_end(&q);
		if (n > 0) {
			c->in_len += (size_t)n;
			got = 1;
			continue;   /* (all of what's been decrypted: the event loop won't say so again) */
		}
		int e = T.get_error(c->tls, n);
		if (e == TLS_ERROR_WANT_READ || e == TLS_ERROR_WANT_WRITE) {
			if (got || !wait)
				return got;
			bool was = rt_gil_drop();
			bool ready = tls_wait(c->fd, e == TLS_ERROR_WANT_WRITE);
			rt_gil_take(was);
			if (!ready)
				return -1;
			continue;
		}
		T.err_clear();
		return got ? 1 : -1;   /* (closed: what came before it first) */
	}
}

/* all of it, through TLS: false if the other end has gone */
static bool tls_send(Conn *c, const char *data, int64_t len)
{
	int64_t done = 0;
	while (done < len) {
		int chunk = len - done > (1 << 30) ? (1 << 30) : (int)(len - done);
		T.err_clear();   /* (SSL_get_error goes by what's on the queue: nothing older) */
		Quiet q;
		tls_quiet_begin(&q);
		int w = T.write_(c->tls, data + done, chunk);
		tls_quiet_end(&q);
		if (w > 0) {
			done += w;
			continue;
		}
		int e = T.get_error(c->tls, w);
		if ((e == TLS_ERROR_WANT_WRITE || e == TLS_ERROR_WANT_READ) && tls_wait(c->fd, e == TLS_ERROR_WANT_WRITE))
			continue;
		T.err_clear();
		return false;
	}
	return true;
}

/* the connection's TLS gone: saying so to the other end, if it can */
static void tls_end(Conn *c)
{
	if (!c->tls)
		return;
	if (c->fd >= 0 && !c->tls_handshaking) {
		Quiet q;
		tls_quiet_begin(&q);
		T.shutdown_(c->tls);
		tls_quiet_end(&q);
	}
	T.free_(c->tls);
	T.err_clear();
	c->tls = NULL;
}

/* ------------------------------------------------------------ a server's */

/* a server's certificate (and the chain to it, as Let's Encrypt's
 * fullchain.pem has) and its key: HTTPS (or encrypted connections) from
 * then on */
void cby_server_set_certificate(int64_t sid, const char *cert, const char *key)
{
	tls_need();
	Server *s = server(sid);
	void *ctx = T.ctx_new(T.server_method());
	T.ctx_ctrl(ctx, 123, 0x0303, NULL);   /* (SSL_CTX_set_min_proto_version: TLS 1.2 and later) */
	T.ctx_ctrl(ctx, 33, 0x2, NULL);       /* (SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER) */
	char why[256];
	const char *files[2] = { cert, key };
	for (int i = 0; i < 2; i++) {
		FILE *f = fopen(files[i], "r");
		if (!f) {
			T.ctx_free(ctx);
			rt_panic(NULL, 0, 0, "can't use the %s '%s': it isn't there (or can't be read)", i ? "key" : "certificate", files[i]);
		}
		fclose(f);
	}
	if (T.use_chain(ctx, cert) != 1) {
		tls_why(why, sizeof why);
		T.ctx_free(ctx);
		rt_panic(NULL, 0, 0, "can't use the certificate '%s': %s", cert, why);
	}
	if (T.use_key(ctx, key, 1) != 1) {
		tls_why(why, sizeof why);
		T.ctx_free(ctx);
		if (strstr(why, "mismatch"))
			rt_panic(NULL, 0, 0, "the key '%s' isn't the certificate's ('%s')", key, cert);
		rt_panic(NULL, 0, 0, "can't use the key '%s': %s", key, why);
	}
	if (T.check_key(ctx) != 1) {
		T.err_clear();
		T.ctx_free(ctx);
		rt_panic(NULL, 0, 0, "the key '%s' isn't the certificate's", key);
	}
	if (s->tls)
		T.ctx_free(s->tls);
	s->tls = ctx;
}

int cby_server_encrypted(int64_t sid) { return server(sid)->tls != NULL; }

/* a connection just accepted, to a server with a certificate: its
 * handshake begins (as what comes allows) */
static void tls_accept_start(Server *s, Conn *c)
{
	c->tls = T.new_(s->tls);
	T.set_fd(c->tls, c->fd);
	tls_nonblocking(c->fd);
	c->tls_handshaking = 1;
}

/* more of the handshake: 1 done, 0 not yet, -1 it failed */
static int tls_accept_more(Conn *c)
{
	T.err_clear();   /* (SSL_get_error goes by what's on the queue: nothing older) */
	Quiet q;
	tls_quiet_begin(&q);
	int r = T.accept_(c->tls);
	tls_quiet_end(&q);
	if (r == 1) {
		c->tls_handshaking = 0;
		return 1;
	}
	int e = T.get_error(c->tls, r);
	if (e == TLS_ERROR_WANT_READ || e == TLS_ERROR_WANT_WRITE)
		return 0;
	T.err_clear();
	return -1;
}

/* ------------------------------------------------------------ connecting */

/* a certificate the program trusts, as well as the system's: one it made
 * (networks.make_certificate), or its own organisation's */
void cby_conn_trust_certificate(const char *path)
{
	tls_need();
	FILE *f = fopen(path, "r");
	if (!f)
		rt_panic(NULL, 0, 0, "can't read the certificate '%s'", path);
	fclose(f);
	tls_trusted = realloc(tls_trusted, sizeof *tls_trusted * (size_t)(ntls_trusted + 1));
	tls_trusted[ntls_trusted++] = strdup(path);
	if (tls_client && T.load_locations(tls_client, path, NULL) != 1) {
		T.err_clear();
		rt_panic(NULL, 0, 0, "'%s' isn't a certificate (a .pem file)", path);
	}
}

static void *tls_client_ctx(void)
{
	if (tls_client)
		return tls_client;
	tls_client = T.ctx_new(T.client_method());
	T.ctx_ctrl(tls_client, 123, 0x0303, NULL);
	T.ctx_ctrl(tls_client, 33, 0x2, NULL);
	T.default_paths(tls_client);
	for (int i = 0; i < ntls_trusted; i++)
		if (T.load_locations(tls_client, tls_trusted[i], NULL) != 1) {
			T.err_clear();
			rt_panic(NULL, 0, 0, "'%s' isn't a certificate (a .pem file)", tls_trusted[i]);
		}
	T.set_verify(tls_client, 1, NULL);   /* (SSL_VERIFY_PEER: the server's certificate must check out) */
	return tls_client;
}

static int is_address(const char *host)
{
	unsigned char buf[16];
	return inet_pton(AF_INET, host, buf) == 1 || inet_pton(AF_INET6, host, buf) == 1;
}

/* a connection just made, encrypted: the handshake, the server's
 * certificate checked for the host. False (and why) if it isn't. */
static bool tls_connect(int64_t id, const char *host)
{
	tls_need();
	Conn *c = conn(id);
	void *ctx = tls_client_ctx();
	c->tls = T.new_(ctx);
	T.set_fd(c->tls, c->fd);
	if (is_address(host)) {
		T.param_ip(T.get0_param(c->tls), host);
	} else {
		T.ctrl(c->tls, 55, 0, (void *)host);   /* (SSL_set_tlsext_host_name: which site, for a server with several) */
		T.set1_host(c->tls, host);
	}
	T.err_clear();
	bool was = rt_gil_drop();
	Quiet q;
	tls_quiet_begin(&q);
	int r = T.connect_(c->tls);
	tls_quiet_end(&q);
	rt_gil_take(was);
	if (r != 1) {
		char why[256];
		long v = T.verify_result(c->tls);
		if (v != 0)
			snprintf(why, sizeof why, "its certificate isn't trusted (%s)", T.verify_string(v));
		else
			tls_why(why, sizeof why);
		T.err_clear();
		T.free_(c->tls);
		c->tls = NULL;
		char at[200], said[200];
		snprintf(at, sizeof at, "%.199s", host);
		snprintf(said, sizeof said, "%.199s", why);
		say_error("can't connect to %s securely: %s", at, said);
		return false;
	}
	tls_nonblocking(c->fd);
	return true;
}

/* a connection to a program listening at host and port, encrypted: its
 * number, or 0 (cby_conn_error says why) */
int64_t cby_conn_open_encrypted(const char *host, int64_t port)
{
	int64_t id = cby_conn_open(host, port);
	if (!id)
		return 0;
	if (!tls_connect(id, host)) {
		cby_conn_free(id);
		return 0;
	}
	return id;
}

int cby_conn_encrypted(int64_t id) { return conn(id)->tls != NULL; }

/* ------------------------------------------------------------ certificates */

/* A certificate of its own (self-signed) for a name, with its key: for
 * trying HTTPS on this computer or a home network. It's for the name, and
 * for localhost, 127.0.0.1 and ::1; it lasts 825 days. Browsers warn of it
 * until it's trusted (in their settings, or the system's); a Cabby
 * program trusts it with networks.trust_certificate. */
void cby_conn_make_certificate(const char *name, const char *cert, const char *key)
{
	tls_need();
	if (!T.can_make)
		rt_panic(NULL, 0, 0, "%s", "this OpenSSL can't make certificates as Cabby asks it to: make one with the openssl command instead (openssl req -x509 ...)");
	if (!*name || strpbrk(name, ", \t\r\n"))
		rt_panic(NULL, 0, 0, "'%s' isn't a computer's name or address", name);
	void *pkey = T.q_keygen(NULL, NULL, "EC", "P-256");
	void *x = pkey ? T.x509_new() : NULL;
	if (!x) {
		T.err_clear();
		rt_panic(NULL, 0, 0, "%s", "a key couldn't be made");
	}
	T.set_version(x, 2);
	unsigned char rnd[4];
	ws_random(rnd, sizeof rnd);
	T.asn1_int_set(T.serial(x), 1 + (long)(((unsigned long)rnd[0] << 16 | rnd[1] << 8 | rnd[2]) & 0x7fffff));
	T.gmtime_adj(T.not_before(x), 0);
	T.gmtime_adj(T.not_after(x), 825L * 24 * 3600);
	T.set_pubkey(x, pkey);
	void *subject = T.subject(x);
	T.name_add(subject, "CN", 0x1001, (const unsigned char *)name, -1, -1, 0);   /* (MBSTRING_ASC) */
	T.name_add(subject, "O", 0x1001, (const unsigned char *)"Cabby (made for trying HTTPS)", -1, -1, 0);
	T.set_issuer(x, subject);
	char alt[600];
	int ip = is_address(name);
	if (strcmp(name, "localhost") == 0 || strcmp(name, "127.0.0.1") == 0 || strcmp(name, "::1") == 0)
		snprintf(alt, sizeof alt, "DNS:localhost,IP:127.0.0.1,IP:::1");
	else
		snprintf(alt, sizeof alt, "%s:%s,DNS:localhost,IP:127.0.0.1,IP:::1", ip ? "IP" : "DNS", name);
	struct { int nid; const char *value; } exts[] = {
		{ 85, alt },                       /* subjectAltName: the names it's for */
		{ 87, "critical,CA:FALSE" },       /* basicConstraints */
		{ 83, "critical,digitalSignature" },   /* keyUsage */
		{ 126, "serverAuth" },             /* extendedKeyUsage */
	};
	for (size_t i = 0; i < sizeof exts / sizeof *exts; i++) {
		void *e = T.ext_conf(NULL, NULL, exts[i].nid, exts[i].value);
		if (e) {
			T.add_ext(x, e, -1);
			T.ext_free(e);
		}
	}
	if (T.sign(x, pkey, T.sha256()) <= 0) {
		T.err_clear();
		T.x509_free(x);
		T.key_free(pkey);
		rt_panic(NULL, 0, 0, "%s", "the certificate couldn't be signed");
	}
	FILE *f = fopen(cert, "w");
	if (!f)
		rt_panic(NULL, 0, 0, "can't write the certificate '%s'", cert);
	T.write_x509(f, x);
	fclose(f);
	/* (the key: for this person's eyes only) */
	int fd = open(key, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	FILE *k = fd >= 0 ? fdopen(fd, "w") : NULL;
	if (!k)
		rt_panic(NULL, 0, 0, "can't write the key '%s'", key);
	fchmod(fd, 0600);
	T.write_key(k, pkey, NULL, NULL, 0, NULL, NULL);
	fclose(k);
	T.x509_free(x);
	T.key_free(pkey);
}

static void tls_server_free(Server *s)
{
	if (s->tls && T.ok)
		T.ctx_free(s->tls);
	s->tls = NULL;
}

static void tls_forget(void)
{
	if (tls_client && T.ok)
		T.ctx_free(tls_client);
	tls_client = NULL;
	for (int i = 0; i < ntls_trusted; i++)
		free(tls_trusted[i]);
	free(tls_trusted);
	tls_trusted = NULL;
	ntls_trusted = 0;
}
