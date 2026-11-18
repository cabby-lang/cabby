/* hashes.c — the C side of the hashes library (lib/hashes.hby): SHA-256,
 * SHA-1 and MD5, of text or of a file, as hexadecimal. Part of Cabby's
 * runtime, written here (from the published standards: FIPS 180-4 for the
 * SHAs, RFC 1321 for MD5), so nothing needs installing. A file is read a
 * piece at a time, so a big one never has to fit in memory. */
#include "rt.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SHA256 = 1, SHA1 = 2, MD5 = 3 };

typedef struct {
	int kind;
	uint32_t h[8];
	uint64_t length;           /* bytes so far */
	unsigned char block[64];
	size_t used;
} Hash;

static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static uint32_t big32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t little32(const unsigned char *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

/* ---- SHA-256 */

static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_block(Hash *x, const unsigned char *b)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = big32(b + 4 * i);
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = x->h[0], bb = x->h[1], c = x->h[2], d = x->h[3], e = x->h[4], f = x->h[5], g = x->h[6], h = x->h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
	}
	x->h[0] += a; x->h[1] += bb; x->h[2] += c; x->h[3] += d;
	x->h[4] += e; x->h[5] += f; x->h[6] += g; x->h[7] += h;
}

/* ---- SHA-1 */

/* (written out in its four phases, five rounds at a time, so the letters
 * needn't move along each round: three or four times as quick as a loop) */
#define SHA1_ROUND(f, k, a, b, c, d, e, i) \
	do { \
		e += rotl(a, 5) + (f) + (k) + w[i]; \
		b = rotl(b, 30); \
	} while (0)
#define SHA1_F1(b, c, d) (d ^ (b & (c ^ d)))
#define SHA1_F2(b, c, d) (b ^ c ^ d)
#define SHA1_F3(b, c, d) ((b & c) | (d & (b | c)))

static void sha1_block(Hash *x, const unsigned char *b)
{
	uint32_t w[80];
	for (int i = 0; i < 16; i++)
		w[i] = big32(b + 4 * i);
	for (int i = 16; i < 80; i++)
		w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t A = x->h[0], B = x->h[1], C = x->h[2], D = x->h[3], E = x->h[4];
	for (int i = 0; i < 20; i += 5) {
		SHA1_ROUND(SHA1_F1(B, C, D), 0x5a827999, A, B, C, D, E, i);
		SHA1_ROUND(SHA1_F1(A, B, C), 0x5a827999, E, A, B, C, D, i + 1);
		SHA1_ROUND(SHA1_F1(E, A, B), 0x5a827999, D, E, A, B, C, i + 2);
		SHA1_ROUND(SHA1_F1(D, E, A), 0x5a827999, C, D, E, A, B, i + 3);
		SHA1_ROUND(SHA1_F1(C, D, E), 0x5a827999, B, C, D, E, A, i + 4);
	}
	for (int i = 20; i < 40; i += 5) {
		SHA1_ROUND(SHA1_F2(B, C, D), 0x6ed9eba1, A, B, C, D, E, i);
		SHA1_ROUND(SHA1_F2(A, B, C), 0x6ed9eba1, E, A, B, C, D, i + 1);
		SHA1_ROUND(SHA1_F2(E, A, B), 0x6ed9eba1, D, E, A, B, C, i + 2);
		SHA1_ROUND(SHA1_F2(D, E, A), 0x6ed9eba1, C, D, E, A, B, i + 3);
		SHA1_ROUND(SHA1_F2(C, D, E), 0x6ed9eba1, B, C, D, E, A, i + 4);
	}
	for (int i = 40; i < 60; i += 5) {
		SHA1_ROUND(SHA1_F3(B, C, D), 0x8f1bbcdc, A, B, C, D, E, i);
		SHA1_ROUND(SHA1_F3(A, B, C), 0x8f1bbcdc, E, A, B, C, D, i + 1);
		SHA1_ROUND(SHA1_F3(E, A, B), 0x8f1bbcdc, D, E, A, B, C, i + 2);
		SHA1_ROUND(SHA1_F3(D, E, A), 0x8f1bbcdc, C, D, E, A, B, i + 3);
		SHA1_ROUND(SHA1_F3(C, D, E), 0x8f1bbcdc, B, C, D, E, A, i + 4);
	}
	for (int i = 60; i < 80; i += 5) {
		SHA1_ROUND(SHA1_F2(B, C, D), 0xca62c1d6, A, B, C, D, E, i);
		SHA1_ROUND(SHA1_F2(A, B, C), 0xca62c1d6, E, A, B, C, D, i + 1);
		SHA1_ROUND(SHA1_F2(E, A, B), 0xca62c1d6, D, E, A, B, C, i + 2);
		SHA1_ROUND(SHA1_F2(D, E, A), 0xca62c1d6, C, D, E, A, B, i + 3);
		SHA1_ROUND(SHA1_F2(C, D, E), 0xca62c1d6, B, C, D, E, A, i + 4);
	}
	x->h[0] += A; x->h[1] += B; x->h[2] += C; x->h[3] += D; x->h[4] += E;
}

/* The processor's own SHA-1, where it has it (Intel's and AMD's SHA
 * extensions, Apple's processors' crypto extension): several times as
 * quick. Checked against SHA-1("abc") the first time, and not used if it
 * doesn't agree. CABBY_SHA_HARDWARE=none uses the C above. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <immintrin.h>
#define HAVE_SHA1_HW 1
__attribute__((target("sha,sse4.1,ssse3"))) static void sha1_hw(uint32_t *h, const unsigned char *b)
{
	const __m128i flip = _mm_set_epi64x(0x0001020304050607LL, 0x08090a0b0c0d0e0fLL);
	__m128i abcd = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)h), 0x1B);
	__m128i e0 = _mm_set_epi32((int)h[4], 0, 0, 0), e1;
	__m128i abcd_was = abcd, e0_was = e0;
	__m128i m[4];
	for (int i = 0; i < 4; i++)
		m[i] = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(b + 16 * i)), flip);
	/* four rounds at a time: twenty groups, the message worked out ahead */
	e0 = _mm_add_epi32(e0, m[0]);
	e1 = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
	for (int g = 1; g < 20; g++) {
		__m128i *cur = g & 1 ? &e1 : &e0, *other = g & 1 ? &e0 : &e1;
		*cur = _mm_sha1nexte_epu32(*cur, m[g % 4]);
		*other = abcd;
		if (g >= 3 && g <= 18)
			m[(g + 1) % 4] = _mm_sha1msg2_epu32(m[(g + 1) % 4], m[g % 4]);
		switch (g / 5) {
		case 0: abcd = _mm_sha1rnds4_epu32(abcd, *cur, 0); break;
		case 1: abcd = _mm_sha1rnds4_epu32(abcd, *cur, 1); break;
		case 2: abcd = _mm_sha1rnds4_epu32(abcd, *cur, 2); break;
		default: abcd = _mm_sha1rnds4_epu32(abcd, *cur, 3); break;
		}
		if (g <= 16)
			m[(g + 3) % 4] = _mm_sha1msg1_epu32(m[(g + 3) % 4], m[g % 4]);
		if (g >= 2 && g <= 17)
			m[(g + 2) % 4] = _mm_xor_si128(m[(g + 2) % 4], m[g % 4]);
	}
	e0 = _mm_sha1nexte_epu32(e0, e0_was);
	abcd = _mm_add_epi32(abcd, abcd_was);
	_mm_storeu_si128((__m128i *)h, _mm_shuffle_epi32(abcd, 0x1B));
	h[4] = (uint32_t)_mm_extract_epi32(e0, 3);
}
static int sha1_hw_here(void)
{
	unsigned a, b, c, d;
	if (!__get_cpuid_count(7, 0, &a, &b, &c, &d) || !(b & (1u << 29)))
		return 0;
	return __builtin_cpu_supports("sse4.1") && __builtin_cpu_supports("ssse3");
}
#elif defined(__aarch64__) && (defined(__ARM_FEATURE_SHA2) || defined(__ARM_FEATURE_CRYPTO))
#include <arm_neon.h>
#define HAVE_SHA1_HW 1
static void sha1_hw(uint32_t *h, const unsigned char *b)
{
	static const uint32_t K[4] = { 0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6 };
	uint32x4_t abcd = vld1q_u32(h), abcd_was = abcd;
	uint32_t e = h[4], e_was = e;
	uint32x4_t w[20];
	for (int i = 0; i < 4; i++)
		w[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(b + 16 * i)));
	for (int i = 4; i < 20; i++)
		w[i] = vsha1su1q_u32(vsha1su0q_u32(w[i - 4], w[i - 3], w[i - 2]), w[i - 1]);
	for (int g = 0; g < 20; g++) {
		uint32x4_t wk = vaddq_u32(w[g], vdupq_n_u32(K[g / 5]));
		uint32_t next = vsha1h_u32(vgetq_lane_u32(abcd, 0));
		if (g < 5)
			abcd = vsha1cq_u32(abcd, e, wk);
		else if (g < 10 || g >= 15)
			abcd = vsha1pq_u32(abcd, e, wk);
		else
			abcd = vsha1mq_u32(abcd, e, wk);
		e = next;
	}
	vst1q_u32(h, vaddq_u32(abcd, abcd_was));
	h[4] = e + e_was;
}
static int sha1_hw_here(void) { return 1; }
#endif

static int sha1_use_hw = -1;

static void sha1_decide(void)
{
	sha1_use_hw = 0;
#ifdef HAVE_SHA1_HW
	const char *no = getenv("CABBY_SHA_HARDWARE");
	if ((no && strcmp(no, "none") == 0) || !sha1_hw_here())
		return;
	/* "abc", padded: one block; its SHA-1 is a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d */
	unsigned char blk[64] = { 'a', 'b', 'c', 0x80 };
	blk[63] = 24;
	uint32_t h[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
	sha1_hw(h, blk);
	if (h[0] == 0xa9993e36 && h[1] == 0x4706816a && h[2] == 0xba3e2571 && h[3] == 0x7850c26c && h[4] == 0x9cd0d89d)
		sha1_use_hw = 1;
#endif
}

static pthread_once_t sha1_decided = PTHREAD_ONCE_INIT;

/* ---- MD5 */

static const uint32_t KMD5[64] = {
	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
	0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
	0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
	0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
	0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
	0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
	0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
	0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const int SMD5[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static void md5_block(Hash *x, const unsigned char *b)
{
	uint32_t m[16];
	for (int i = 0; i < 16; i++)
		m[i] = little32(b + 4 * i);
	uint32_t a = x->h[0], bb = x->h[1], c = x->h[2], d = x->h[3];
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;
		if (i < 16) { f = (bb & c) | (~bb & d); g = i; }
		else if (i < 32) { f = (d & bb) | (~d & c); g = (5 * i + 1) % 16; }
		else if (i < 48) { f = bb ^ c ^ d; g = (3 * i + 5) % 16; }
		else { f = c ^ (bb | ~d); g = (7 * i) % 16; }
		uint32_t t = d;
		d = c;
		c = bb;
		bb = bb + rotl(a + f + KMD5[i] + m[g], SMD5[i]);
		a = t;
	}
	x->h[0] += a; x->h[1] += bb; x->h[2] += c; x->h[3] += d;
}

/* ---- any of them */

static void start(Hash *x, int kind)
{
	memset(x, 0, sizeof *x);
	x->kind = kind;
	if (kind == SHA256) {
		static const uint32_t h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
						0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
		memcpy(x->h, h0, sizeof h0);
	} else {
		x->h[0] = 0x67452301; x->h[1] = 0xefcdab89; x->h[2] = 0x98badcfe; x->h[3] = 0x10325476;
		x->h[4] = 0xc3d2e1f0;
	}
}

static void block(Hash *x, const unsigned char *b)
{
	if (x->kind == SHA256)
		sha256_block(x, b);
	else if (x->kind == SHA1) {
		pthread_once(&sha1_decided, sha1_decide);
#ifdef HAVE_SHA1_HW
		if (sha1_use_hw) {
			sha1_hw(x->h, b);
			return;
		}
#endif
		sha1_block(x, b);
	}
	else
		md5_block(x, b);
}

static void add(Hash *x, const unsigned char *p, size_t n)
{
	x->length += n;
	while (n) {
		if (x->used == 0 && n >= 64) {   /* (a whole block: hashed where it is) */
			block(x, p);
			p += 64;
			n -= 64;
			continue;
		}
		size_t take = 64 - x->used < n ? 64 - x->used : n;
		memcpy(x->block + x->used, p, take);
		x->used += take;
		p += take;
		n -= take;
		if (x->used == 64) {
			block(x, x->block);
			x->used = 0;
		}
	}
}

/* the end: padding, the length, and the answer as hexadecimal into out */
static void finish(Hash *x, char *out)
{
	uint64_t bits = x->length * 8;
	unsigned char pad = 0x80, zero = 0, len[8];
	uint64_t keep = x->length;
	add(x, &pad, 1);
	while (x->used != 56)
		add(x, &zero, 1);
	for (int i = 0; i < 8; i++)   /* the SHAs: big end first; MD5: little */
		len[i] = (unsigned char)(x->kind == MD5 ? bits >> (8 * i) : bits >> (56 - 8 * i));
	add(x, len, 8);
	x->length = keep;
	int words = x->kind == SHA256 ? 8 : x->kind == SHA1 ? 5 : 4;
	static const char hex[] = "0123456789abcdef";
	int k = 0;
	for (int i = 0; i < words; i++)
		for (int j = 0; j < 4; j++) {
			unsigned char byte = (unsigned char)(x->kind == MD5 ? x->h[i] >> (8 * j) : x->h[i] >> (24 - 8 * j));
			out[k++] = hex[byte >> 4];
			out[k++] = hex[byte & 15];
		}
	out[k] = '\0';
}

static _Thread_local char answer[65];

/* the hash of Length bytes of text: its length, in hexadecimal digits
 * (taken with cby_hashes_take) */
int64_t cby_hashes_text(int64_t kind, const char *text, int64_t length)
{
	Hash x;
	start(&x, (int)kind);
	add(&x, (const unsigned char *)text, (size_t)length);
	finish(&x, answer);
	return (int64_t)strlen(answer);
}

int64_t cby_hashes_file(int64_t kind, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, errno == ENOENT ? "it isn't there" : strerror(errno));
	Hash x;
	start(&x, (int)kind);
	static unsigned char buf[1 << 16];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		add(&x, buf, n);
	int failed = ferror(f);
	fclose(f);
	if (failed)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, strerror(errno));
	finish(&x, answer);
	return (int64_t)strlen(answer);
}

void cby_hashes_take(char *dst)
{
	memcpy(dst, answer, strlen(answer));
}

/* (archives.c's, for encrypted zips: SHA-1 a piece at a time, its answer
 * as 20 bytes) */
void *rt_sha1_start(void)
{
	Hash *x = malloc(sizeof *x);
	start(x, SHA1);
	return x;
}

void rt_sha1_add(void *h, const void *p, size_t n) { add(h, p, n); }

void rt_sha1_end(void *h, unsigned char out[20])
{
	char hex[65];
	Hash *x = h;
	finish(x, hex);
	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 4; j++)
			out[4 * i + j] = (unsigned char)(x->h[i] >> (24 - 8 * j));
	free(x);
}
