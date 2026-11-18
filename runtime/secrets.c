/* secrets.c — the secrets library's C side: random numbers no one can
 * guess, passwords kept as hashes, text and files locked with a password,
 * and signatures. Part of Cabby's runtime.
 *
 * Nothing here is cryptography of Cabby's own: random bytes come from the
 * system (getentropy: the kernel's own generator), and the rest from a
 * well-tested library, loaded when it's first needed: OpenSSL's libcrypto
 * on Linux (every system has it), and on a Mac its own CommonCrypto (part
 * of the system).
 *
 *   hash_password  PBKDF2 with HMAC-SHA256, 600,000 rounds and a random
 *                  16-byte salt, as OWASP advises; kept as
 *                  "$pbkdf2-sha256$600000$<salt>$<hash>" (base64), which
 *                  says how it was made, so a later Cabby can check it
 *   lock           AES-256-GCM (encrypted, and any change noticed), its key
 *                  from the password by PBKDF2-HMAC-SHA256 (200,000
 *                  rounds, a random salt), a random 12-byte nonce: "CBY1",
 *                  the salt, the nonce, what's locked, then GCM's tag
 *   signature      HMAC-SHA256, in hexadecimal */

#include "rt.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif

#define HASH_ROUNDS 600000
#define LOCK_ROUNDS 200000

/* ------------------------------------------------------------ the library */

static struct {
	int tried, ok, apple;
	/* OpenSSL */
	int (*pbkdf2)(const char *, int, const unsigned char *, int, int, const void *, int, unsigned char *);
	const void *(*sha256)(void);
	unsigned char *(*hmac)(const void *, const void *, int, const unsigned char *, size_t, unsigned char *, unsigned int *);
	void *(*ctx_new)(void);
	void (*ctx_free)(void *);
	const void *(*aes_gcm)(void);
	int (*enc_init)(void *, const void *, void *, const unsigned char *, const unsigned char *);
	int (*enc_update)(void *, unsigned char *, int *, const unsigned char *, int);
	int (*enc_final)(void *, unsigned char *, int *);
	int (*dec_init)(void *, const void *, void *, const unsigned char *, const unsigned char *);
	int (*dec_update)(void *, unsigned char *, int *, const unsigned char *, int);
	int (*dec_final)(void *, unsigned char *, int *);
	int (*ctrl)(void *, int, int, void *);
	/* CommonCrypto */
	int (*cc_pbkdf)(unsigned, const char *, size_t, const uint8_t *, size_t, unsigned, unsigned, uint8_t *, size_t);
	void (*cc_hmac)(unsigned, const void *, size_t, const void *, size_t, void *);
	int (*cc_gcm_enc)(unsigned, const void *, size_t, const void *, size_t, const void *, size_t, const void *, size_t, void *,
			  void *, size_t);
	int (*cc_gcm_dec)(unsigned, const void *, size_t, const void *, size_t, const void *, size_t, const void *, size_t, void *,
			  const void *, size_t);
} lib;

#define FN(field, h, name) do { void *p_ = dlsym(h, name); memcpy(&lib.field, &p_, sizeof p_); } while (0)

/* OpenSSL's libcrypto, NULL if there's none. On a Mac it's Homebrew's
 * (3 or 4, or whichever "openssl" is now, on an Apple processor or
 * Intel), never the system's own /usr/lib/libcrypto, which stops any
 * program that loads it. */
static void *crypto(void)
{
	static int tried;
	static void *h;
	if (!tried) {
		tried = 1;
#if defined(__APPLE__)
		static const char *const names[] = { "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib",
						     "/opt/homebrew/opt/openssl@4/lib/libcrypto.4.dylib",
						     "/opt/homebrew/opt/openssl/lib/libcrypto.dylib",
						     "/usr/local/opt/openssl@3/lib/libcrypto.3.dylib",
						     "/usr/local/opt/openssl@4/lib/libcrypto.4.dylib",
						     "/usr/local/opt/openssl/lib/libcrypto.dylib", NULL };
#else
		static const char *const names[] = { "libcrypto.so.3", "libcrypto.so.4", "libcrypto.so.1.1", "libcrypto.so", NULL };
#endif
		const char *own = getenv("CABBY_LIBCRYPTO");   /* (for the tests: a newer one) */
		if (own && *own)
			h = dlopen(own, RTLD_NOW | RTLD_LOCAL);
		for (int i = 0; names[i] && !h; i++)
			h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	}
	return h;
}

static void need(void)
{
	if (!lib.tried) {
		lib.tried = 1;
#if defined(__APPLE__)
		lib.apple = 1;
		void *cc = RTLD_DEFAULT;   /* (CommonCrypto is part of the system's own library) */
		FN(cc_pbkdf, cc, "CCKeyDerivationPBKDF");
		FN(cc_hmac, cc, "CCHmac");
		FN(cc_gcm_enc, cc, "CCCryptorGCMOneshotEncrypt");
		FN(cc_gcm_dec, cc, "CCCryptorGCMOneshotDecrypt");
		lib.ok = lib.cc_pbkdf && lib.cc_hmac && lib.cc_gcm_enc && lib.cc_gcm_dec;
		if (lib.ok)
			return;
		/* (a macOS without them: Homebrew's OpenSSL, if it's there) */
		lib.apple = 0;
#endif
		void *h = crypto();
		if (h) {
			FN(pbkdf2, h, "PKCS5_PBKDF2_HMAC");
			FN(sha256, h, "EVP_sha256");
			FN(hmac, h, "HMAC");
			FN(ctx_new, h, "EVP_CIPHER_CTX_new");
			FN(ctx_free, h, "EVP_CIPHER_CTX_free");
			FN(aes_gcm, h, "EVP_aes_256_gcm");
			FN(enc_init, h, "EVP_EncryptInit_ex");
			FN(enc_update, h, "EVP_EncryptUpdate");
			FN(enc_final, h, "EVP_EncryptFinal_ex");
			FN(dec_init, h, "EVP_DecryptInit_ex");
			FN(dec_update, h, "EVP_DecryptUpdate");
			FN(dec_final, h, "EVP_DecryptFinal_ex");
			FN(ctrl, h, "EVP_CIPHER_CTX_ctrl");
			lib.ok = lib.pbkdf2 && lib.sha256 && lib.hmac && lib.ctx_new && lib.ctx_free && lib.aes_gcm && lib.enc_init &&
				 lib.enc_update && lib.enc_final && lib.dec_init && lib.dec_update && lib.dec_final && lib.ctrl;
		}
	}
	if (!lib.ok)
		rt_panic(NULL, 0, 0, "%s",
#if defined(__APPLE__)
			 "this Mac's CommonCrypto doesn't have what the secrets library needs: 'brew install openssl' gives it another way"
#else
			 "the secrets library needs OpenSSL's libcrypto (the 'openssl' package on Arch, 'libssl3' on Debian)"
#endif
		);
}

/* ---- the four things used: random bytes, PBKDF2, HMAC, AES-256-GCM */

static void random_bytes(void *out, size_t n)
{
	unsigned char *p = out;
	while (n > 0) {
		size_t take = n > 256 ? 256 : n;   /* (getentropy's most at once) */
		if (getentropy(p, take) != 0)
			rt_panic(NULL, 0, 0, "%s", "the system's random numbers couldn't be read");
		p += take;
		n -= take;
	}
}

static void pbkdf2(const char *pw, size_t pwlen, const unsigned char *salt, size_t saltlen, int rounds, unsigned char *out,
		   size_t outlen)
{
	need();
	bool was = rt_gil_drop();   /* (it takes a moment: other threads go on) */
	int ok;
	if (lib.apple)
		ok = lib.cc_pbkdf(2, pw, pwlen, salt, saltlen, 3 /* SHA-256 */, (unsigned)rounds, out, outlen) == 0;
	else
		ok = lib.pbkdf2(pw, (int)pwlen, salt, (int)saltlen, rounds, lib.sha256(), (int)outlen, out) == 1;
	rt_gil_take(was);
	if (!ok)
		rt_panic(NULL, 0, 0, "%s", "a key couldn't be made from the password");
}

static void hmac256(const void *key, size_t keylen, const void *data, size_t len, unsigned char out[32])
{
	need();
	if (lib.apple) {
		lib.cc_hmac(2 /* SHA-256 */, key, keylen, data, len, out);
	} else {
		unsigned int n = 32;
		if (!lib.hmac(lib.sha256(), key, (int)keylen, data, len, out, &n))
			rt_panic(NULL, 0, 0, "%s", "the signature couldn't be made");
	}
}

enum { GCM_SET_IVLEN = 0x9, GCM_GET_TAG = 0x10, GCM_SET_TAG = 0x11 };

/* AES-256-GCM: out gets len bytes, tag 16 */
static void gcm_encrypt(const unsigned char key[32], const unsigned char *iv, size_t ivlen, const unsigned char *in, size_t len,
			unsigned char *out, unsigned char tag[16])
{
	need();
	if (lib.apple) {
		if (lib.cc_gcm_enc(0 /* AES */, key, 32, iv, ivlen, NULL, 0, in, len, out, tag, 16) != 0)
			rt_panic(NULL, 0, 0, "%s", "it couldn't be locked");
		return;
	}
	void *c = lib.ctx_new();
	int n = 0, ok = c && lib.enc_init(c, lib.aes_gcm(), NULL, NULL, NULL) == 1 &&
			 lib.ctrl(c, GCM_SET_IVLEN, (int)ivlen, NULL) == 1 && lib.enc_init(c, NULL, NULL, key, iv) == 1;
	size_t done = 0;
	while (ok && done < len) {
		int take = len - done > (1 << 30) ? (1 << 30) : (int)(len - done);
		ok = lib.enc_update(c, out + done, &n, in + done, take) == 1;
		done += (size_t)n;
	}
	ok = ok && lib.enc_final(c, out + done, &n) == 1 && lib.ctrl(c, GCM_GET_TAG, 16, tag) == 1;
	if (c)
		lib.ctx_free(c);
	if (!ok)
		rt_panic(NULL, 0, 0, "%s", "it couldn't be locked");
}

/* 1 if it was what was locked (the tag agrees), 0 if not */
static int gcm_decrypt(const unsigned char key[32], const unsigned char *iv, size_t ivlen, const unsigned char *in, size_t len,
		       const unsigned char tag[16], unsigned char *out)
{
	need();
	if (lib.apple)
		return lib.cc_gcm_dec(0, key, 32, iv, ivlen, NULL, 0, in, len, out, tag, 16) == 0;
	void *c = lib.ctx_new();
	int n = 0, ok = c && lib.dec_init(c, lib.aes_gcm(), NULL, NULL, NULL) == 1 &&
			 lib.ctrl(c, GCM_SET_IVLEN, (int)ivlen, NULL) == 1 && lib.dec_init(c, NULL, NULL, key, iv) == 1;
	size_t done = 0;
	while (ok && done < len) {
		int take = len - done > (1 << 30) ? (1 << 30) : (int)(len - done);
		ok = lib.dec_update(c, out + done, &n, in + done, take) == 1;
		done += (size_t)n;
	}
	ok = ok && lib.ctrl(c, GCM_SET_TAG, 16, (void *)tag) == 1 && lib.dec_final(c, out + done, &n) == 1;
	if (c)
		lib.ctx_free(c);
	return ok;
}

/* ------------------------------------------------------------ text back to Cabby */

static _Thread_local char *handed;
static _Thread_local size_t handed_len;

static int64_t hand_n(const char *s, size_t n)
{
	free(handed);
	handed = malloc(n + 1);
	memcpy(handed, s, n);
	handed[n] = 0;
	handed_len = n;
	return (int64_t)n;
}

static int64_t hand(const char *s) { return hand_n(s, strlen(s)); }

char cby_secrets_char(int64_t i) { return handed && i >= 0 && (size_t)i < handed_len ? handed[i] : 0; }

/* ---- base64 (standard, and URL-safe) */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64(const unsigned char *s, size_t n)
{
	char *out = malloc(4 * ((n + 2) / 3) + 1), *o = out;
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)s[i] << 16 | (i + 1 < n ? (unsigned)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
		*o++ = B64[v >> 18 & 63];
		*o++ = B64[v >> 12 & 63];
		*o++ = i + 1 < n ? B64[v >> 6 & 63] : '=';
		*o++ = i + 2 < n ? B64[v & 63] : '=';
	}
	*o = 0;
	return out;
}

/* NULL if it isn't base64 */
static unsigned char *unbase64(const char *s, size_t *n)
{
	size_t len = strlen(s);
	unsigned char *out = malloc(len / 4 * 3 + 3);
	size_t o = 0;
	unsigned v = 0;
	int bits = 0;
	for (size_t i = 0; i < len; i++) {
		char c = s[i];
		if (c == '=' || c == '\n' || c == '\r' || c == ' ')
			continue;
		const char *at = strchr(B64, c);
		if (!at || !c) {
			free(out);
			return NULL;
		}
		v = v << 6 | (unsigned)(at - B64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[o++] = (unsigned char)(v >> bits & 255);
		}
	}
	*n = o;
	return out;
}

/* ------------------------------------------------------------ random */

int64_t cby_secrets_random_bytes(int64_t n)
{
	if (n < 0)
		rt_panic(NULL, 0, 0, "%s", "a number of bytes can't be below 0");
	unsigned char *b = malloc((size_t)n + 1);
	random_bytes(b, (size_t)n);
	int64_t h = rt_bytes_make(b, n);
	free(b);
	return h;
}

/* from to to, both included, every number as likely as the next */
int64_t cby_secrets_random_number(int64_t from, int64_t to)
{
	if (to < from) {
		int64_t t = from;
		from = to;
		to = t;
	}
	uint64_t span = (uint64_t)to - (uint64_t)from + 1, r;
	if (span == 0) {   /* (the whole range of a long) */
		random_bytes(&r, sizeof r);
		return (int64_t)r;
	}
	uint64_t limit = UINT64_MAX - UINT64_MAX % span;   /* (none of the numbers more likely: those above are thrown back) */
	do
		random_bytes(&r, sizeof r);
	while (r >= limit);
	return from + (int64_t)(r % span);
}

static void random_from(char *out, int64_t n, const char *alphabet)
{
	size_t k = strlen(alphabet);
	for (int64_t i = 0; i < n; i++)
		out[i] = alphabet[cby_secrets_random_number(0, (int64_t)k - 1)];
	out[n] = 0;
}

/* letters, digits, - and _: safe in a web address or a file's name */
int64_t cby_secrets_random_text(int64_t n)
{
	if (n < 1 || n > 1 << 20)
		rt_panic(NULL, 0, 0, "%s", "random text is from 1 character long");
	char *s = malloc((size_t)n + 1);
	random_from(s, n, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
	int64_t r = hand(s);
	free(s);
	return r;
}

/* a password for a person: letters and digits (and symbols), at least one
 * of each, none that look alike (0 O, 1 l I) */
int64_t cby_secrets_random_password(int64_t n, int symbols)
{
	if (n < 8 || n > 1024)
		rt_panic(NULL, 0, 0, "%s", "a password is from 8 to 1024 characters long (16 is good)");
	static const char lower[] = "abcdefghijkmnopqrstuvwxyz", upper[] = "ABCDEFGHJKLMNPQRSTUVWXYZ", digits[] = "23456789",
			  marks[] = "!#$%&*+-=?@^_";
	char all[128];
	snprintf(all, sizeof all, "%s%s%s%s", lower, upper, digits, symbols ? marks : "");
	char *s = malloc((size_t)n + 1);
	for (;;) {
		random_from(s, n, all);
		int has[4] = { 0, 0, 0, 0 };
		for (int64_t i = 0; i < n; i++) {
			has[0] |= strchr(lower, s[i]) != NULL;
			has[1] |= strchr(upper, s[i]) != NULL;
			has[2] |= strchr(digits, s[i]) != NULL;
			has[3] |= strchr(marks, s[i]) != NULL;
		}
		if (has[0] && has[1] && has[2] && (has[3] || !symbols))
			break;
	}
	int64_t r = hand(s);
	free(s);
	return r;
}

/* ------------------------------------------------------------ passwords */

int64_t cby_secrets_hash_password(const char *pw)
{
	unsigned char salt[16], hash[32];
	random_bytes(salt, sizeof salt);
	pbkdf2(pw, strlen(pw), salt, sizeof salt, HASH_ROUNDS, hash, sizeof hash);
	char *s = base64(salt, sizeof salt), *h = base64(hash, sizeof hash);
	char out[200];
	snprintf(out, sizeof out, "$pbkdf2-sha256$%d$%s$%s", HASH_ROUNDS, s, h);
	free(s);
	free(h);
	return hand(out);
}

/* equal, taking the same time however much is the same (so the time it
 * takes gives nothing away) */
static int same(const unsigned char *a, size_t an, const unsigned char *b, size_t bn)
{
	unsigned diff = an != bn;
	size_t n = an < bn ? an : bn;
	for (size_t i = 0; i < n; i++)
		diff |= (unsigned)(a[i] ^ b[i]);
	return diff == 0;
}

int cby_secrets_same(const char *a, const char *b) { return same((const unsigned char *)a, strlen(a), (const unsigned char *)b, strlen(b)); }

/* 1 if the password is the one hashed; 0 if not, or the hash isn't one
 * hash_password made */
static int check_argon2(const char *pw, const char *kept);

int cby_secrets_check_password(const char *pw, const char *kept)
{
	if (!strncmp(kept, "$argon2id$", 10))
		return check_argon2(pw, kept);
	const char *head = "$pbkdf2-sha256$";
	if (strncmp(kept, head, strlen(head)) != 0)
		return 0;
	char *copy = strdup(kept + strlen(head));
	char *d1 = strchr(copy, '$'), *d2 = d1 ? strchr(d1 + 1, '$') : NULL;
	if (!d1 || !d2) {
		free(copy);
		return 0;
	}
	*d1 = *d2 = 0;
	long rounds = strtol(copy, NULL, 10);
	size_t sn = 0, hn = 0;
	unsigned char *salt = unbase64(d1 + 1, &sn), *hash = unbase64(d2 + 1, &hn);
	int ok = 0;
	if (rounds >= 1000 && rounds <= 100000000 && salt && hash && hn >= 16 && hn <= 64) {
		unsigned char got[64];
		pbkdf2(pw, strlen(pw), salt, sn, (int)rounds, got, hn);
		ok = same(got, hn, hash, hn);
	}
	free(salt);
	free(hash);
	free(copy);
	return ok;
}

/* ------------------------------------------------------------ signatures */

int64_t cby_secrets_signature(const char *text, int64_t len, const char *key)
{
	unsigned char mac[32];
	hmac256(key, strlen(key), text, (size_t)len, mac);
	char hex[65];
	for (int i = 0; i < 32; i++)
		snprintf(hex + 2 * i, 3, "%02x", mac[i]);
	return hand(hex);
}

/* ------------------------------------------------------------ locking */

#define MAGIC "CBY1"
#define HEAD (4 + 16 + 12)

/* what's locked: magic, salt, nonce, it, tag (a new bytes value) */
static unsigned char *lock(const unsigned char *in, size_t len, const char *pw, size_t *outlen)
{
	unsigned char *out = malloc(HEAD + len + 16), key[32];
	memcpy(out, MAGIC, 4);
	random_bytes(out + 4, 16 + 12);
	pbkdf2(pw, strlen(pw), out + 4, 16, LOCK_ROUNDS, key, sizeof key);
	gcm_encrypt(key, out + 20, 12, in, len, out + HEAD, out + HEAD + len);
	memset(key, 0, sizeof key);
	*outlen = HEAD + len + 16;
	return out;
}

/* NULL if it can't be unlocked with that password (or it's been changed,
 * or it isn't something locked) */
static unsigned char *unlock(const unsigned char *in, size_t len, const char *pw, size_t *outlen)
{
	if (len < HEAD + 16 || memcmp(in, MAGIC, 4) != 0)
		return NULL;
	unsigned char key[32];
	pbkdf2(pw, strlen(pw), in + 4, 16, LOCK_ROUNDS, key, sizeof key);
	size_t n = len - HEAD - 16;
	unsigned char *out = malloc(n + 1);
	int ok = gcm_decrypt(key, in + 20, 12, in + HEAD, n, in + len - 16, out);
	memset(key, 0, sizeof key);
	if (!ok) {
		free(out);
		return NULL;
	}
	out[n] = 0;
	*outlen = n;
	return out;
}

/* text, locked: base64 text, to keep or send */
int64_t cby_secrets_lock_text(const char *text, int64_t len, const char *pw)
{
	size_t n;
	unsigned char *l = lock((const unsigned char *)text, (size_t)len, pw, &n);
	char *b = base64(l, n);
	free(l);
	int64_t r = hand(b);
	free(b);
	return r;
}

/* the text again: its length, or -1 if it couldn't be unlocked */
int64_t cby_secrets_unlock_text(const char *locked, const char *pw)
{
	size_t n = 0, got = 0;
	unsigned char *raw = unbase64(locked, &n);
	if (!raw)
		return -1;
	unsigned char *out = unlock(raw, n, pw, &got);
	free(raw);
	if (!out)
		return -1;
	int64_t r = hand_n((const char *)out, got);
	free(out);
	return r;
}

int64_t cby_secrets_lock_bytes(int64_t h, const char *pw)
{
	int64_t len;
	const uint8_t *d = rt_bytes_data(h, &len);
	size_t n;
	unsigned char *l = lock(d, (size_t)len, pw, &n);
	int64_t r = rt_bytes_make(l, (int64_t)n);
	free(l);
	return r;
}

/* 0 if it couldn't be unlocked */
int64_t cby_secrets_unlock_bytes(int64_t h, const char *pw)
{
	int64_t len;
	const uint8_t *d = rt_bytes_data(h, &len);
	size_t n;
	unsigned char *out = unlock(d, (size_t)len, pw, &n);
	if (!out)
		return 0;
	int64_t r = rt_bytes_make(out, (int64_t)n);
	free(out);
	return r;
}

static unsigned char *read_all(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't read '%s': it isn't there, or can't be read", path);
	size_t cap = 65536, len = 0;
	unsigned char *d = malloc(cap);
	size_t got;
	while ((got = fread(d + len, 1, cap - len, f)) > 0) {
		len += got;
		if (len == cap)
			d = realloc(d, cap *= 2);
	}
	fclose(f);
	*n = len;
	return d;
}

static void write_all(const char *path, const unsigned char *d, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f || fwrite(d, 1, n, f) != n || fclose(f) != 0)
		rt_panic(NULL, 0, 0, "can't write '%s'", path);
}

void cby_secrets_lock_file(const char *from, const char *to, const char *pw)
{
	size_t n, ln;
	unsigned char *d = read_all(from, &n);
	unsigned char *l = lock(d, n, pw, &ln);
	free(d);
	write_all(to, l, ln);
	free(l);
}

/* 1, or 0 if it couldn't be unlocked (nothing's written then) */
int cby_secrets_unlock_file(const char *from, const char *to, const char *pw)
{
	size_t n, un;
	unsigned char *d = read_all(from, &n);
	unsigned char *u = unlock(d, n, pw, &un);
	free(d);
	if (!u)
		return 0;
	write_all(to, u, un);
	free(u);
	return 1;
}

/* ------------------------------------------------------------ for the tests: the
 * library's own answers to standard examples (hexadecimal in and out) */

static unsigned char *from_hex(const char *s, size_t *n)
{
	*n = strlen(s) / 2;
	unsigned char *b = malloc(*n + 1);
	for (size_t i = 0; i < *n; i++) {
		unsigned v;
		sscanf(s + 2 * i, "%2x", &v);
		b[i] = (unsigned char)v;
	}
	return b;
}

static int64_t hand_hex(const unsigned char *b, size_t n)
{
	char *s = malloc(2 * n + 1);
	for (size_t i = 0; i < n; i++)
		snprintf(s + 2 * i, 3, "%02x", b[i]);
	int64_t r = hand_n(s, 2 * n);
	free(s);
	return r;
}

int64_t cby_secrets_test_pbkdf2(const char *pw, const char *salt, int64_t rounds, int64_t len)
{
	unsigned char out[128];
	pbkdf2(pw, strlen(pw), (const unsigned char *)salt, strlen(salt), (int)rounds, out, (size_t)len);
	return hand_hex(out, (size_t)len);
}

/* AES-256-GCM of the plain text: what it becomes, then the tag */
int64_t cby_secrets_test_gcm(const char *key_hex, const char *iv_hex, const char *plain_hex)
{
	size_t kn, ivn, pn;
	unsigned char *key = from_hex(key_hex, &kn), *iv = from_hex(iv_hex, &ivn), *p = from_hex(plain_hex, &pn);
	unsigned char *out = malloc(pn + 16);
	gcm_encrypt(key, iv, ivn, p, pn, out, out + pn);
	int64_t r = hand_hex(out, pn + 16);
	unsigned char *back = malloc(pn + 1);
	if (!gcm_decrypt(key, iv, ivn, out, pn, out + pn, back) || memcmp(back, p, pn) != 0)
		r = hand("it didn't come back");
	free(key), free(iv), free(p), free(out), free(back);
	return r;
}

void cby_secrets_cant_unlock(void)
{
	rt_panic(NULL, 0, 0, "%s", "it can't be unlocked: the password is wrong, or it's been changed (can_unlock checks first)");
}

/* ------------------------------------------------------------ public keys
 *
 * Ed25519 (as SSH's newest keys are, and many a service's): a private key
 * signs, and the public key it goes with checks, so anyone can check but
 * only the private key's owner can sign. Keys and signatures go to Cabby
 * as base64: a key is 32 bytes, a signature 64. They're OpenSSL's on every
 * system (CommonCrypto hasn't Ed25519): on a Mac, Homebrew's. */

#define ED25519 1087   /* (EVP_PKEY_ED25519) */

static struct {
	int tried, ok;
	void *(*pctx_new_id)(int, void *);
	int (*keygen_init)(void *);
	int (*keygen)(void *, void **);
	void (*pctx_free)(void *);
	void *(*new_private)(int, void *, const unsigned char *, size_t);
	void *(*new_public)(int, void *, const unsigned char *, size_t);
	int (*get_private)(const void *, unsigned char *, size_t *);
	int (*get_public)(const void *, unsigned char *, size_t *);
	void (*pkey_free)(void *);
	void *(*md_new)(void);
	void (*md_free)(void *);
	int (*sign_init)(void *, void **, const void *, void *, void *);
	int (*sign)(void *, unsigned char *, size_t *, const unsigned char *, size_t);
	int (*verify_init)(void *, void **, const void *, void *, void *);
	int (*verify)(void *, const unsigned char *, size_t, const unsigned char *, size_t);
	/* Argon2 (OpenSSL 3.2 and later) */
	void *(*kdf_fetch)(void *, const char *, const char *);
	void (*kdf_free)(void *);
	void *(*kdf_ctx_new)(void *);
	void (*kdf_ctx_free)(void *);
	int (*kdf_derive)(void *, unsigned char *, size_t, const void *);
} pk;

#define PK(field, h, name) do { void *p_ = dlsym(h, name); memcpy(&pk.field, &p_, sizeof p_); } while (0)

static void *keys_library(void)
{
	void *h = crypto();
	if (h && !pk.tried) {
		pk.tried = 1;
		PK(pctx_new_id, h, "EVP_PKEY_CTX_new_id");
		PK(keygen_init, h, "EVP_PKEY_keygen_init");
		PK(keygen, h, "EVP_PKEY_keygen");
		PK(pctx_free, h, "EVP_PKEY_CTX_free");
		PK(new_private, h, "EVP_PKEY_new_raw_private_key");
		PK(new_public, h, "EVP_PKEY_new_raw_public_key");
		PK(get_private, h, "EVP_PKEY_get_raw_private_key");
		PK(get_public, h, "EVP_PKEY_get_raw_public_key");
		PK(pkey_free, h, "EVP_PKEY_free");
		PK(md_new, h, "EVP_MD_CTX_new");
		PK(md_free, h, "EVP_MD_CTX_free");
		PK(sign_init, h, "EVP_DigestSignInit");
		PK(sign, h, "EVP_DigestSign");
		PK(verify_init, h, "EVP_DigestVerifyInit");
		PK(verify, h, "EVP_DigestVerify");
		pk.ok = pk.pctx_new_id && pk.keygen_init && pk.keygen && pk.pctx_free && pk.new_private && pk.new_public &&
			pk.get_private && pk.get_public && pk.pkey_free && pk.md_new && pk.md_free && pk.sign_init && pk.sign &&
			pk.verify_init && pk.verify;
		PK(kdf_fetch, h, "EVP_KDF_fetch");
		PK(kdf_free, h, "EVP_KDF_free");
		PK(kdf_ctx_new, h, "EVP_KDF_CTX_new");
		PK(kdf_ctx_free, h, "EVP_KDF_CTX_free");
		PK(kdf_derive, h, "EVP_KDF_derive");
	}
	return h;
}

static void need_keys(void)
{
	if (!keys_library() || !pk.ok)
		rt_panic(NULL, 0, 0, "%s",
#if defined(__APPLE__)
			 "public keys need OpenSSL (a Mac's own CommonCrypto hasn't Ed25519): brew install openssl"
#else
			 "public keys need OpenSSL 1.1.1 or later's libcrypto (the 'openssl' package on Arch, 'libssl3' on Debian)"
#endif
		);
}

int cby_secrets_can_use_keys(void) { return keys_library() && pk.ok; }

/* a 32-byte key from base64, or a panic that says what's wrong with it */
static void key_from(const char *text, const char *which, unsigned char out[32])
{
	size_t n = 0;
	unsigned char *k = unbase64(text, &n);
	if (!k || n != 32) {
		free(k);
		rt_panic(NULL, 0, 0, "that isn't a %s key (an Ed25519 key is 32 bytes, in base64: 44 letters)", which);
	}
	memcpy(out, k, 32);
	free(k);
}

/* a new private key, in base64 */
int64_t cby_secrets_make_private_key(void)
{
	need_keys();
	void *ctx = pk.pctx_new_id(ED25519, NULL), *key = NULL;
	unsigned char raw[32];
	size_t n = sizeof raw;
	int ok = ctx && pk.keygen_init(ctx) == 1 && pk.keygen(ctx, &key) == 1 && pk.get_private(key, raw, &n) == 1 && n == 32;
	if (key)
		pk.pkey_free(key);
	if (ctx)
		pk.pctx_free(ctx);
	if (!ok)
		rt_panic(NULL, 0, 0, "%s", "a key couldn't be made");
	char *b = base64(raw, sizeof raw);
	memset(raw, 0, sizeof raw);
	int64_t len = hand(b);
	free(b);
	return len;
}

static void *private_key(const char *text)
{
	need_keys();
	unsigned char raw[32];
	key_from(text, "private", raw);
	void *key = pk.new_private(ED25519, NULL, raw, sizeof raw);
	memset(raw, 0, sizeof raw);
	if (!key)
		rt_panic(NULL, 0, 0, "%s", "that isn't a private key");
	return key;
}

/* the public key that goes with a private one */
int64_t cby_secrets_public_key(const char *private)
{
	void *key = private_key(private);
	unsigned char raw[32];
	size_t n = sizeof raw;
	int ok = pk.get_public(key, raw, &n) == 1 && n == 32;
	pk.pkey_free(key);
	if (!ok)
		rt_panic(NULL, 0, 0, "%s", "the public key couldn't be worked out");
	char *b = base64(raw, sizeof raw);
	int64_t len = hand(b);
	free(b);
	return len;
}

/* the message's signature with a private key, in base64 */
int64_t cby_secrets_sign(const char *text, int64_t len, const char *private)
{
	void *key = private_key(private), *md = pk.md_new();
	unsigned char sig[64];
	size_t n = sizeof sig;
	int ok = md && pk.sign_init(md, NULL, NULL, NULL, key) == 1 &&
		 pk.sign(md, sig, &n, (const unsigned char *)text, (size_t)len) == 1 && n == 64;
	if (md)
		pk.md_free(md);
	pk.pkey_free(key);
	if (!ok)
		rt_panic(NULL, 0, 0, "%s", "the message couldn't be signed");
	char *b = base64(sig, sizeof sig);
	int64_t got = hand(b);
	free(b);
	return got;
}

/* 1 if it's the message's signature by the public key's private key */
int cby_secrets_check_signed(const char *text, int64_t len, const char *public, const char *signature)
{
	need_keys();
	unsigned char raw[32];
	key_from(public, "public", raw);
	size_t sn = 0;
	unsigned char *sig = unbase64(signature, &sn);
	if (!sig || sn != 64) {
		free(sig);
		return 0;
	}
	void *key = pk.new_public(ED25519, NULL, raw, sizeof raw), *md = pk.md_new();
	int ok = key && md && pk.verify_init(md, NULL, NULL, NULL, key) == 1 &&
		 pk.verify(md, sig, sn, (const unsigned char *)text, (size_t)len) == 1;
	if (md)
		pk.md_free(md);
	if (key)
		pk.pkey_free(key);
	free(sig);
	return ok;
}

/* ------------------------------------------------------------ Argon2
 *
 * Argon2id, which OWASP now prefers to PBKDF2: each guess at a password
 * needs 64 MB of memory as well as time, so a stolen hash can't be
 * guessed at by the million on a graphics card. It's OpenSSL 3.2's and
 * later (RFC 9106's second choice: 64 MB, 3 passes, 4 lanes), kept the
 * way every other program keeps it:
 * "$argon2id$v=19$m=65536,t=3,p=4$<salt>$<hash>" (base64 without =). */

#define ARGON_MEMORY 65536
#define ARGON_PASSES 3
#define ARGON_LANES 4

typedef struct {   /* (OSSL_PARAM) */
	const char *key;
	unsigned type;
	void *data;
	size_t size, got;
} Param;

enum { P_UINT = 2, P_OCTETS = 5 };

static void *argon_kdf(void)
{
	static int tried;
	static void *kdf;
	if (!tried) {
		tried = 1;
		if (keys_library() && pk.kdf_fetch && pk.kdf_free && pk.kdf_ctx_new && pk.kdf_ctx_free && pk.kdf_derive)
			kdf = pk.kdf_fetch(NULL, "ARGON2ID", NULL);
	}
	return kdf;
}

int cby_secrets_can_use_argon2(void)
{
	if (getenv("CABBY_NO_ARGON2"))   /* (for the tests: as if it were an older OpenSSL) */
		return 0;
	return argon_kdf() != NULL;
}

static int argon2(const char *pw, const unsigned char *salt, size_t saltlen, uint32_t memory, uint32_t passes, uint32_t lanes,
		  unsigned char *out, size_t outlen)
{
	void *c = pk.kdf_ctx_new(argon_kdf());
	if (!c)
		return 0;
	uint32_t threads = 1;
	Param params[] = {
		{ "pass", P_OCTETS, (void *)pw, strlen(pw), 0 },
		{ "salt", P_OCTETS, (void *)salt, saltlen, 0 },
		{ "iter", P_UINT, &passes, sizeof passes, 0 },
		{ "memcost", P_UINT, &memory, sizeof memory, 0 },
		{ "lanes", P_UINT, &lanes, sizeof lanes, 0 },
		{ "threads", P_UINT, &threads, sizeof threads, 0 },
		{ NULL, 0, NULL, 0, 0 },
	};
	bool was = rt_gil_drop();
	int ok = pk.kdf_derive(c, out, outlen, params) == 1;
	rt_gil_take(was);
	pk.kdf_ctx_free(c);
	return ok;
}

static char *unpadded(char *b64)
{
	char *eq = strchr(b64, '=');
	if (eq)
		*eq = 0;
	return b64;
}

int64_t cby_secrets_hash_password_argon2(const char *pw)
{
	if (!cby_secrets_can_use_argon2())
		rt_panic(NULL, 0, 0, "%s", "Argon2 needs OpenSSL 3.2 or later (secrets.can_use_argon2 checks)");
	unsigned char salt[16], hash[32];
	random_bytes(salt, sizeof salt);
	if (!argon2(pw, salt, sizeof salt, ARGON_MEMORY, ARGON_PASSES, ARGON_LANES, hash, sizeof hash))
		rt_panic(NULL, 0, 0, "%s", "the password couldn't be hashed (Argon2 needs 64 MB of memory)");
	char *s = unpadded(base64(salt, sizeof salt)), *h = unpadded(base64(hash, sizeof hash));
	char out[200];
	snprintf(out, sizeof out, "$argon2id$v=19$m=%d,t=%d,p=%d$%s$%s", ARGON_MEMORY, ARGON_PASSES, ARGON_LANES, s, h);
	free(s);
	free(h);
	return hand(out);
}

/* 1 if it's the password an Argon2id hash was made from (from Cabby or any
 * other program); 0 if not; a panic if this OpenSSL can't tell */
static int check_argon2(const char *pw, const char *kept)
{
	unsigned version = 0, memory = 0, passes = 0, lanes = 0;
	int used = 0;
	if (sscanf(kept, "$argon2id$v=%u$m=%u,t=%u,p=%u$%n", &version, &memory, &passes, &lanes, &used) != 4 || !used ||
	    version != 19 || memory < 8 || memory > 4194304 || passes < 1 || passes > 1000 || lanes < 1 || lanes > 64)
		return 0;
	if (!argon_kdf())
		rt_panic(NULL, 0, 0, "%s", "that password was hashed with Argon2, which needs OpenSSL 3.2 or later to check");
	char *copy = strdup(kept + used), *d = strchr(copy, '$');
	if (!d) {
		free(copy);
		return 0;
	}
	*d = 0;
	size_t sn = 0, hn = 0;
	unsigned char *salt = unbase64(copy, &sn), *hash = unbase64(d + 1, &hn);
	int ok = 0;
	if (salt && hash && sn >= 8 && hn >= 16 && hn <= 64) {
		unsigned char got[64];
		ok = argon2(pw, salt, sn, memory, passes, lanes, got, hn) && same(got, hn, hash, hn);
	}
	free(salt);
	free(hash);
	free(copy);
	return ok;
}
