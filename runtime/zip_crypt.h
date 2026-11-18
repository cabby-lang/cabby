/* zip_crypt.h — zip files kept with a password, for archives.c.
 *
 * Made: WinZip's AES (the "AE-2" way, which 7-Zip, WinZip, the Mac's
 * Archive Utility with a helper, and most zip programs read): each file's
 * bytes, squeezed as usual, then encrypted with AES-256 in counter mode,
 * with a key made from the password by PBKDF2 (HMAC-SHA1, 1000 rounds, and
 * a salt of 16 random bytes), and an HMAC-SHA1 of them at the end, so a
 * file that's been changed, or a wrong password, is caught.
 *
 * Read: that, with AES-128 and -192 too, and the old "ZipCrypto" of PKZIP
 * (what `zip -e` makes), which is weak (it can be broken), but is what many
 * old zips have.
 *
 * AES is written here from its standard (FIPS-197): encryption only, as
 * counter mode needs no more, a round at a time from tables made once. */

/* ------------------------------------------------------------ AES */

typedef struct {
	uint32_t rk[60];
	uint8_t bk[15][16];   /* (the same round keys as bytes: for the processor's own AES) */
	int rounds;
} Aes;

static uint8_t aes_sbox[256];
static uint32_t aes_te[4][256];
static pthread_once_t aes_once = PTHREAD_ONCE_INIT;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (x & 0x80 ? 0x1B : 0)); }
static uint8_t rotl8(uint8_t x, int n) { return (uint8_t)((x << n) | (x >> (8 - n))); }

static void aes_tables(void)
{
	/* the S-box: each byte's inverse in GF(2^8), then the affine map */
	uint8_t p = 1, q = 1;
	do {
		p = (uint8_t)(p ^ xtime(p));   /* p * 3 */
		q ^= (uint8_t)(q << 1);        /* q / 3 */
		q ^= (uint8_t)(q << 2);
		q ^= (uint8_t)(q << 4);
		if (q & 0x80)
			q ^= 0x09;
		uint8_t x = (uint8_t)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4));
		aes_sbox[p] = x ^ 0x63;
	} while (p != 1);
	aes_sbox[0] = 0x63;
	for (int i = 0; i < 256; i++) {
		uint8_t s = aes_sbox[i], s2 = xtime(s), s3 = s2 ^ s;
		uint32_t t = (uint32_t)s2 << 24 | (uint32_t)s << 16 | (uint32_t)s << 8 | s3;
		for (int k = 0; k < 4; k++) {
			aes_te[k][i] = t;
			t = t >> 8 | t << 24;
		}
	}
}

static void aes_key(Aes *a, const uint8_t *key, int bytes)
{
	pthread_once(&aes_once, aes_tables);
	int nk = bytes / 4;
	a->rounds = nk + 6;
	int total = 4 * (a->rounds + 1);
	for (int i = 0; i < nk; i++)
		a->rk[i] = (uint32_t)key[4 * i] << 24 | (uint32_t)key[4 * i + 1] << 16 | (uint32_t)key[4 * i + 2] << 8 | key[4 * i + 3];
	uint8_t rcon = 1;
	for (int i = nk; i < total; i++) {
		uint32_t t = a->rk[i - 1];
		if (i % nk == 0) {
			t = t << 8 | t >> 24;
			t = (uint32_t)aes_sbox[t >> 24] << 24 | (uint32_t)aes_sbox[(t >> 16) & 255] << 16 |
			    (uint32_t)aes_sbox[(t >> 8) & 255] << 8 | aes_sbox[t & 255];
			t ^= (uint32_t)rcon << 24;
			rcon = xtime(rcon);
		} else if (nk > 6 && i % nk == 4) {
			t = (uint32_t)aes_sbox[t >> 24] << 24 | (uint32_t)aes_sbox[(t >> 16) & 255] << 16 |
			    (uint32_t)aes_sbox[(t >> 8) & 255] << 8 | aes_sbox[t & 255];
		}
		a->rk[i] = a->rk[i - nk] ^ t;
	}
	for (int i = 0; i < total; i++)
		for (int k = 0; k < 4; k++)
			a->bk[i / 4][4 * (i % 4) + k] = (uint8_t)(a->rk[i] >> (24 - 8 * k));
}

static inline uint32_t load_be(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void aes_encrypt(const Aes *a, const uint8_t in[16], uint8_t out[16])
{
	const uint32_t *rk = a->rk;
	const uint32_t *T0 = aes_te[0], *T1 = aes_te[1], *T2 = aes_te[2], *T3 = aes_te[3];
	uint32_t s0 = load_be(in) ^ rk[0], s1 = load_be(in + 4) ^ rk[1], s2 = load_be(in + 8) ^ rk[2],
		 s3 = load_be(in + 12) ^ rk[3];
	for (int r = 1; r < a->rounds; r++) {
		rk += 4;
		uint32_t t0 = T0[s0 >> 24] ^ T1[(s1 >> 16) & 255] ^ T2[(s2 >> 8) & 255] ^ T3[s3 & 255] ^ rk[0];
		uint32_t t1 = T0[s1 >> 24] ^ T1[(s2 >> 16) & 255] ^ T2[(s3 >> 8) & 255] ^ T3[s0 & 255] ^ rk[1];
		uint32_t t2 = T0[s2 >> 24] ^ T1[(s3 >> 16) & 255] ^ T2[(s0 >> 8) & 255] ^ T3[s1 & 255] ^ rk[2];
		uint32_t t3 = T0[s3 >> 24] ^ T1[(s0 >> 16) & 255] ^ T2[(s1 >> 8) & 255] ^ T3[s2 & 255] ^ rk[3];
		s0 = t0;
		s1 = t1;
		s2 = t2;
		s3 = t3;
	}
	rk += 4;
	const uint8_t *S = aes_sbox;
	uint32_t v[4] = {
		((uint32_t)S[s0 >> 24] << 24 | (uint32_t)S[(s1 >> 16) & 255] << 16 | (uint32_t)S[(s2 >> 8) & 255] << 8 | S[s3 & 255]) ^ rk[0],
		((uint32_t)S[s1 >> 24] << 24 | (uint32_t)S[(s2 >> 16) & 255] << 16 | (uint32_t)S[(s3 >> 8) & 255] << 8 | S[s0 & 255]) ^ rk[1],
		((uint32_t)S[s2 >> 24] << 24 | (uint32_t)S[(s3 >> 16) & 255] << 16 | (uint32_t)S[(s0 >> 8) & 255] << 8 | S[s1 & 255]) ^ rk[2],
		((uint32_t)S[s3 >> 24] << 24 | (uint32_t)S[(s0 >> 16) & 255] << 16 | (uint32_t)S[(s1 >> 8) & 255] << 8 | S[s2 & 255]) ^ rk[3],
	};
	for (int i = 0; i < 4; i++) {
		out[4 * i] = (uint8_t)(v[i] >> 24);
		out[4 * i + 1] = (uint8_t)(v[i] >> 16);
		out[4 * i + 2] = (uint8_t)(v[i] >> 8);
		out[4 * i + 3] = (uint8_t)v[i];
	}
}

/* The processor's own AES, where it has it (Intel's and AMD's AES-NI, the
 * crypto extension of Apple's processors): many times as quick. Checked
 * against the standard's own examples the first time, and not used if it
 * doesn't agree. CABBY_AES_HARDWARE=none uses the tables. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define HAVE_AES_HW 1
__attribute__((target("aes,sse2"))) static void aes_hw(const Aes *a, const uint8_t in[16], uint8_t out[16])
{
	__m128i s = _mm_xor_si128(_mm_loadu_si128((const __m128i *)in), _mm_loadu_si128((const __m128i *)a->bk[0]));
	for (int r = 1; r < a->rounds; r++)
		s = _mm_aesenc_si128(s, _mm_loadu_si128((const __m128i *)a->bk[r]));
	s = _mm_aesenclast_si128(s, _mm_loadu_si128((const __m128i *)a->bk[a->rounds]));
	_mm_storeu_si128((__m128i *)out, s);
}
static int aes_hw_here(void) { return __builtin_cpu_supports("aes"); }
#elif defined(__aarch64__) && (defined(__ARM_FEATURE_CRYPTO) || defined(__ARM_FEATURE_AES))
#include <arm_neon.h>
#define HAVE_AES_HW 1
static void aes_hw(const Aes *a, const uint8_t in[16], uint8_t out[16])
{
	uint8x16_t s = vld1q_u8(in);
	for (int r = 0; r < a->rounds - 1; r++)
		s = vaesmcq_u8(vaeseq_u8(s, vld1q_u8(a->bk[r])));
	s = vaeseq_u8(s, vld1q_u8(a->bk[a->rounds - 1]));
	vst1q_u8(out, veorq_u8(s, vld1q_u8(a->bk[a->rounds])));
}
static int aes_hw_here(void) { return 1; }
#endif

static int aes_use_hw = -1;   /* -1: not decided yet */

static void aes_decide(void)
{
	aes_use_hw = 0;
#ifdef HAVE_AES_HW
	const char *no = getenv("CABBY_AES_HARDWARE");
	if ((no && strcmp(no, "none") == 0) || !aes_hw_here())
		return;
	/* FIPS-197's examples, C.1 and C.3: AES-128 and AES-256 */
	static const uint8_t pt[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
	static const uint8_t c128[16] = { 0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a };
	static const uint8_t c256[16] = { 0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf, 0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89 };
	uint8_t key[32], out[16];
	for (int i = 0; i < 32; i++)
		key[i] = (uint8_t)i;
	Aes a;
	aes_key(&a, key, 16);
	aes_hw(&a, pt, out);
	if (memcmp(out, c128, 16) != 0)
		return;
	aes_key(&a, key, 32);
	aes_hw(&a, pt, out);
	if (memcmp(out, c256, 16) != 0)
		return;
	aes_use_hw = 1;
#endif
}

static pthread_once_t aes_decided = PTHREAD_ONCE_INIT;

/* a block encrypted: by the processor's AES, or the tables' */
static void aes_block(const Aes *a, const uint8_t in[16], uint8_t out[16])
{
	pthread_once(&aes_decided, aes_decide);
#ifdef HAVE_AES_HW
	if (aes_use_hw) {
		aes_hw(a, in, out);
		return;
	}
#endif
	aes_encrypt(a, in, out);
}

/* ------------------------------------------------------------ HMAC-SHA1 and PBKDF2 */

extern void *rt_sha1_start(void);
extern void rt_sha1_add(void *h, const void *p, size_t n);
extern void rt_sha1_end(void *h, unsigned char out[20]);

typedef struct {
	void *inner;
	uint8_t outer_key[64];
} Hmac;

static void hmac_start(Hmac *m, const uint8_t *key, size_t n)
{
	uint8_t k[64] = { 0 }, pad[64];
	if (n > 64) {
		void *h = rt_sha1_start();
		rt_sha1_add(h, key, n);
		rt_sha1_end(h, k);
	} else {
		memcpy(k, key, n);
	}
	for (int i = 0; i < 64; i++) {
		pad[i] = k[i] ^ 0x36;
		m->outer_key[i] = k[i] ^ 0x5c;
	}
	m->inner = rt_sha1_start();
	rt_sha1_add(m->inner, pad, 64);
}

static void hmac_add(Hmac *m, const void *p, size_t n) { rt_sha1_add(m->inner, p, n); }

static void hmac_end(Hmac *m, uint8_t out[20])
{
	uint8_t in[20];
	rt_sha1_end(m->inner, in);
	void *h = rt_sha1_start();
	rt_sha1_add(h, m->outer_key, 64);
	rt_sha1_add(h, in, 20);
	rt_sha1_end(h, out);
}

/* PBKDF2 (RFC 8018) with HMAC-SHA1: `want` bytes from the password and salt */
static void pbkdf2(const char *password, const uint8_t *salt, size_t salt_len, int rounds, uint8_t *out, size_t want)
{
	size_t plen = strlen(password);
	for (uint32_t block = 1; want; block++) {
		uint8_t u[20], t[20], be[4] = { (uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8),
						 (uint8_t)block };
		Hmac m;
		hmac_start(&m, (const uint8_t *)password, plen);
		hmac_add(&m, salt, salt_len);
		hmac_add(&m, be, 4);
		hmac_end(&m, u);
		memcpy(t, u, 20);
		for (int r = 1; r < rounds; r++) {
			hmac_start(&m, (const uint8_t *)password, plen);
			hmac_add(&m, u, 20);
			hmac_end(&m, u);
			for (int i = 0; i < 20; i++)
				t[i] ^= u[i];
		}
		size_t take = want < 20 ? want : 20;
		memcpy(out, t, take);
		out += take;
		want -= take;
	}
}

/* ------------------------------------------------------------ WinZip's AES */

/* a file's encryption, as it goes: AES in counter mode (the counter little
 * end first, from 1), and an HMAC of what's encrypted */
typedef struct {
	Aes aes;
	uint8_t counter[16], pad[16];
	int used;      /* bytes of pad used (16: a new one's needed) */
	Hmac mac;
} ZipAes;

static int aes_salt_len(int strength) { return strength == 1 ? 8 : strength == 2 ? 12 : 16; }

/* the keys from the password and salt: AES's, the HMAC's, and the two
 * bytes that say whether the password's right */
static void zip_aes_start(ZipAes *za, const char *password, const uint8_t *salt, int strength, uint8_t check[2])
{
	int klen = 8 + 8 * strength;   /* 16, 24 or 32 */
	uint8_t keys[66];
	pbkdf2(password, salt, (size_t)aes_salt_len(strength), 1000, keys, (size_t)(2 * klen + 2));
	aes_key(&za->aes, keys, klen);
	hmac_start(&za->mac, keys + klen, (size_t)klen);
	memcpy(check, keys + 2 * klen, 2);
	memset(za->counter, 0, 16);
	za->used = 16;
	memset(keys, 0, sizeof keys);
}

static void zip_aes_next(ZipAes *za)
{
	for (int k = 0; k < 16 && ++za->counter[k] == 0; k++) {
	}
	aes_block(&za->aes, za->counter, za->pad);
	za->used = 0;
}

static void zip_aes_xor(ZipAes *za, uint8_t *p, size_t n)
{
	size_t i = 0;
	/* (whole blocks, eight bytes at a time, when the last pad's used up) */
	if (za->used == 16)
		for (; i + 16 <= n; i += 16) {
			zip_aes_next(za);
			uint64_t a, b, c, d;
			memcpy(&a, p + i, 8);
			memcpy(&b, p + i + 8, 8);
			memcpy(&c, za->pad, 8);
			memcpy(&d, za->pad + 8, 8);
			a ^= c;
			b ^= d;
			memcpy(p + i, &a, 8);
			memcpy(p + i + 8, &b, 8);
			za->used = 16;
		}
	for (; i < n; i++) {
		if (za->used == 16) {
			for (int k = 0; k < 16 && ++za->counter[k] == 0; k++) {
			}
			aes_block(&za->aes, za->counter, za->pad);
			za->used = 0;
		}
		p[i] ^= za->pad[za->used++];
	}
}

/* encrypting: the bytes encrypted, then put in the HMAC */
static void zip_aes_encrypt(ZipAes *za, uint8_t *p, size_t n)
{
	zip_aes_xor(za, p, n);
	hmac_add(&za->mac, p, n);
}

/* decrypting: put in the HMAC as they are, then decrypted */
static void zip_aes_decrypt(ZipAes *za, uint8_t *p, size_t n)
{
	hmac_add(&za->mac, p, n);
	zip_aes_xor(za, p, n);
}

/* the 10 bytes at the end: the HMAC's first 10 */
static void zip_aes_end(ZipAes *za, uint8_t code[10])
{
	uint8_t full[20];
	hmac_end(&za->mac, full);
	memcpy(code, full, 10);
}

static void random_bytes(uint8_t *p, size_t n)
{
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f || fread(p, 1, n, f) != n)
		rt_panic(NULL, 0, 0, "archives: no random bytes for the password's salt (/dev/urandom)%s", "");
	fclose(f);
}

/* ------------------------------------------------------------ PKZIP's ZipCrypto (reading only) */

typedef struct {
	uint32_t k[3];
} ZipCrypto;

static uint32_t crc_byte(uint32_t c, uint8_t b)
{
	c ^= b;
	for (int i = 0; i < 8; i++)
		c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
	return c;
}

static void zc_update(ZipCrypto *zk, uint8_t b)
{
	zk->k[0] = crc_byte(zk->k[0], b);
	zk->k[1] = (zk->k[1] + (zk->k[0] & 0xFF)) * 134775813u + 1;
	zk->k[2] = crc_byte(zk->k[2], (uint8_t)(zk->k[1] >> 24));
}

static void zc_start(ZipCrypto *zk, const char *password)
{
	zk->k[0] = 0x12345678u;
	zk->k[1] = 0x23456789u;
	zk->k[2] = 0x34567890u;
	for (const char *p = password; *p; p++)
		zc_update(zk, (uint8_t)*p);
}

static void zc_decrypt(ZipCrypto *zk, uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		uint16_t t = (uint16_t)(zk->k[2] | 2);
		p[i] ^= (uint8_t)((t * (t ^ 1)) >> 8);
		zc_update(zk, p[i]);
	}
}
