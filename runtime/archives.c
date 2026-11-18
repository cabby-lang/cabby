/* archives.c — the C side of the archives library (lib/archives.hby): zip
 * files, tar files (plain, gzipped: .tar.gz, or xz: .tar.xz), and single
 * gzipped or xz files.
 * Part of Cabby's runtime, so the interpreter and compiled programs both
 * have it.
 *
 * Squeezing (deflate) is zlib's, which every Linux and Mac has, and xz's is
 * liblzma's: each is loaded
 * the first time it's needed, as networks loads libcurl, so neither
 * building Cabby nor a program that never squeezes anything needs it. The
 * formats around it (zip's headers and directory, tar's blocks) are
 * written here, from their specifications (PKWARE's APPNOTE for zip,
 * POSIX's ustar for tar). Tar files are read and written a piece at a time,
 * so a big backup never has to fit in memory.
 *
 * Unpacking never writes outside the folder it's given: a name inside an
 * archive that would (an absolute path, or one with ..) is a panic. */
#include "rt.h"
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------ zlib */

typedef struct {
	const unsigned char *next_in;
	unsigned avail_in;
	unsigned long total_in;
	unsigned char *next_out;
	unsigned avail_out;
	unsigned long total_out;
	const char *msg;
	void *state;
	void *zalloc, *zfree, *opaque;
	int data_type;
	unsigned long adler, reserved;
} ZStream;   /* zlib's z_stream, as it has been since 1.0 */

enum { Z_OK_ = 0, Z_STREAM_END_ = 1, Z_NO_FLUSH_ = 0, Z_FINISH_ = 4, Z_BUF_ERROR_ = -5 };

static struct {
	int tried, ok;
	int (*deflate_init)(ZStream *, int, int, int, int, int, const char *, int);
	int (*deflate)(ZStream *, int);
	int (*deflate_end)(ZStream *);
	int (*inflate_init)(ZStream *, int, const char *, int);
	int (*inflate)(ZStream *, int);
	int (*inflate_end)(ZStream *);
	unsigned long (*crc32)(unsigned long, const unsigned char *, unsigned);
} z;

static void need_zlib(void)
{
	if (!z.tried) {
		z.tried = 1;
		static const char *const names[] = { "libz.so.1", "libz.so", "libz.1.dylib", "libz.dylib", NULL };
		void *h = NULL;
		for (int i = 0; names[i] && !h; i++)
			h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (h) {
#define LOAD(field, name) do { void *p_ = dlsym(h, name); memcpy(&z.field, &p_, sizeof p_); } while (0)
			LOAD(deflate_init, "deflateInit2_");
			LOAD(deflate, "deflate");
			LOAD(deflate_end, "deflateEnd");
			LOAD(inflate_init, "inflateInit2_");
			LOAD(inflate, "inflate");
			LOAD(inflate_end, "inflateEnd");
			LOAD(crc32, "crc32");
#undef LOAD
			z.ok = z.deflate_init && z.deflate && z.deflate_end && z.inflate_init && z.inflate && z.inflate_end &&
			       z.crc32;
		}
	}
	if (!z.ok)
		rt_panic(NULL, 0, 0, "the archives library needs zlib (libz), which comes with nearly every system: "
				     "install the 'zlib' package and try again");
}

/* ------------------------------------------------------------ liblzma, for .xz */

/* lzma_stream, as liblzma 5 has it (its reserved fields kept, so it's the
 * size the library expects; some room more, to be safe) */
typedef struct {
	const unsigned char *next_in;
	size_t avail_in;
	uint64_t total_in;
	unsigned char *next_out;
	size_t avail_out;
	uint64_t total_out;
	void *allocator, *internal;
	void *reserved_ptr[4];
	uint64_t reserved_int[2];
	size_t reserved_size[2];
	int reserved_enum[2];
	char room[64];
} XzStream;

enum { LZMA_OK_ = 0, LZMA_STREAM_END_ = 1, LZMA_BUF_ERROR_ = 10, LZMA_RUN_ = 0, LZMA_FINISH_ = 3 };

static struct {
	int tried, ok;
	int (*easy_encoder)(XzStream *, uint32_t, int);
	int (*stream_decoder)(XzStream *, uint64_t, uint32_t);
	int (*code)(XzStream *, int);
	void (*end)(XzStream *);
} xz;

static void need_xz(void)
{
	if (!xz.tried) {
		xz.tried = 1;
		static const char *const names[] = { "liblzma.so.5", "liblzma.so", "liblzma.5.dylib", "/usr/lib/liblzma.5.dylib",
						      "/opt/homebrew/lib/liblzma.5.dylib", "/usr/local/lib/liblzma.5.dylib", NULL };
		void *h = NULL;
		for (int i = 0; names[i] && !h; i++)
			h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
		if (h) {
#define LOAD(field, name) do { void *p_ = dlsym(h, name); memcpy(&xz.field, &p_, sizeof p_); } while (0)
			LOAD(easy_encoder, "lzma_easy_encoder");
			LOAD(stream_decoder, "lzma_stream_decoder");
			LOAD(code, "lzma_code");
			LOAD(end, "lzma_end");
#undef LOAD
			xz.ok = xz.easy_encoder && xz.stream_decoder && xz.code && xz.end;
		}
	}
	if (!xz.ok)
		rt_panic(NULL, 0, 0, "the archives library needs liblzma for .xz files, which comes with nearly every system: "
				     "install the 'xz' package (xz-utils on Debian; brew install xz on a Mac) and try again");
}

/* window bits: -15 raw deflate (zip), 31 gzip, 47 (inflating) gzip or zlib */
static void squeeze_start(ZStream *s, int bits)
{
	need_zlib();
	memset(s, 0, sizeof *s);
	if (z.deflate_init(s, 6, 8, bits, 8, 0, "1", (int)sizeof *s) != Z_OK_)
		rt_panic(NULL, 0, 0, "archives: zlib couldn't start squeezing");
}

static void unsqueeze_start(ZStream *s, int bits)
{
	need_zlib();
	memset(s, 0, sizeof *s);
	if (z.inflate_init(s, bits, "1", (int)sizeof *s) != Z_OK_)
		rt_panic(NULL, 0, 0, "archives: zlib couldn't start unsqueezing");
}

static unsigned long crc(unsigned long c, const unsigned char *p, size_t n)
{
	need_zlib();
	while (n) {
		unsigned part = n > (1u << 30) ? 1u << 30 : (unsigned)n;
		c = z.crc32(c, p, part);
		p += part;
		n -= part;
	}
	return c;
}

#include "zip_crypt.h"

/* a password for the zip being made or read (archives.make_with_password
 * and the rest; "" none): the thread's own */
static _Thread_local char *zip_password;

void cby_archives_set_password(const char *password)
{
	free(zip_password);
	zip_password = *password ? strdup(password) : NULL;
}

/* ------------------------------------------------------------ bytes */

typedef struct {
	unsigned char *b;
	size_t n, cap;
} Buf;

static void buf_put(Buf *o, const void *p, size_t n)
{
	if (o->n + n > o->cap) {
		o->cap = (o->n + n) * 2 + 4096;
		o->b = realloc(o->b, o->cap);
	}
	memcpy(o->b + o->n, p, n);
	o->n += n;
}

static void put16(Buf *o, unsigned v) { unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) }; buf_put(o, b, 2); }

static void put32(Buf *o, unsigned long v)
{
	unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
	buf_put(o, b, 4);
}

static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned long get32(const unsigned char *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* ------------------------------------------------------------ paths */

/* the folders above path made, if they aren't there */
static void make_parents(const char *path)
{
	char *p = strdup(path);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = '\0';
			mkdir(p, 0755);
			*s = '/';
		}
	free(p);
}

/* a name inside an archive that would land outside the folder: absolute,
 * or with a .. part */
static int unsafe_name(const char *name)
{
	if (name[0] == '/' || name[0] == '\\')
		return 1;
	const char *p = name;
	while (*p) {
		const char *e = p;
		while (*e && *e != '/' && *e != '\\')
			e++;
		if (e - p == 2 && p[0] == '.' && p[1] == '.')
			return 1;
		p = *e ? e + 1 : e;
	}
	return 0;
}

static char *join(const char *folder, const char *name)
{
	size_t a = strlen(folder), b = strlen(name);
	char *s = malloc(a + b + 2);
	memcpy(s, folder, a);
	size_t k = a;
	if (a && folder[a - 1] != '/')
		s[k++] = '/';
	memcpy(s + k, name, b + 1);
	return s;
}

/* how a path given to make is named inside the archive: as it's written,
 * without a leading ./; an absolute one, or one with .., by its last part */
static const char *stored_name(const char *path)
{
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	if (path[0] == '/' || unsafe_name(path)) {
		size_t n = strlen(path);
		while (n > 1 && path[n - 1] == '/')
			n--;
		const char *s = path + n;
		while (s > path && s[-1] != '/')
			s--;
		return s;
	}
	return path;
}

/* ------------------------------------------------------------ results */

static _Thread_local char **res;
static _Thread_local size_t *reslen;
static _Thread_local int nres, capres;

static void results_clear(void)
{
	for (int i = 0; i < nres; i++)
		free(res[i]);
	nres = 0;
}

static void result_n(const void *s, size_t n)
{
	if (nres == capres) {
		capres = capres ? capres * 2 : 16;
		res = realloc(res, sizeof *res * (size_t)capres);
		reslen = realloc(reslen, sizeof *reslen * (size_t)capres);
	}
	res[nres] = malloc(n + 1);
	memcpy(res[nres], s, n);
	res[nres][n] = '\0';
	reslen[nres++] = n;
}

int64_t cby_archives_result_length(int64_t i) { return i >= 0 && i < nres ? (int64_t)reslen[i] : 0; }

void cby_archives_take(int64_t i, char *dst)
{
	if (i >= 0 && i < nres && reslen[i])
		memcpy(dst, res[i], reslen[i]);
}

/* ------------------------------------------------------------ kinds */

enum { ZIP = 1, TAR = 2, TAR_GZ = 3, TAR_XZ = 4 };

/* how a tar of that kind is squeezed (Stream's gz) */
static int squeezed(int kind) { return kind == TAR_GZ ? 1 : kind == TAR_XZ ? 2 : 0; }

static int ends_with(const char *s, const char *end)
{
	size_t a = strlen(s), b = strlen(end);
	return a >= b && strcasecmp(s + a - b, end) == 0;
}

static int kind_by_name(const char *path)
{
	if (ends_with(path, ".zip"))
		return ZIP;
	if (ends_with(path, ".tar.gz") || ends_with(path, ".tgz"))
		return TAR_GZ;
	if (ends_with(path, ".tar.xz") || ends_with(path, ".txz"))
		return TAR_XZ;
	if (ends_with(path, ".tar"))
		return TAR;
	rt_panic(NULL, 0, 0, "can't make '%s': archives makes .zip, .tar, .tar.gz and .tar.xz files, by their name", path);
}

/* what an archive is, by its first bytes */
static int kind_of_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, errno == ENOENT ? "it isn't there" : strerror(errno));
	unsigned char b[512] = { 0 };
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	if (n >= 4 && b[0] == 'P' && b[1] == 'K' && (b[2] == 3 || b[2] == 5))
		return ZIP;
	if (n >= 2 && b[0] == 0x1f && b[1] == 0x8b)
		return TAR_GZ;
	if (n >= 6 && memcmp(b, "\xfd" "7zXZ\0", 6) == 0)
		return TAR_XZ;
	if (n >= 262 && memcmp(b + 257, "ustar", 5) == 0)
		return TAR;
	rt_panic(NULL, 0, 0, "'%s' isn't an archive archives knows: a zip, a tar, a .tar.gz or a .tar.xz", path);
}

/* ------------------------------------------------------------ writing: what to put in */

typedef struct {
	char *path;   /* where it is */
	char *name;   /* its name in the archive ("photos/a.jpg"; a folder's ends with /) */
	int folder;
	struct stat st;
} Entry;

static _Thread_local Entry *entries;
static _Thread_local int nentries;
static char *making;          /* the archive being made */

static void entries_clear(void)
{
	for (int i = 0; i < nentries; i++) {
		free(entries[i].path);
		free(entries[i].name);
	}
	free(entries);
	entries = NULL;
	nentries = 0;
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void add_entry(const char *path, const char *name)
{
	struct stat st;
	if (stat(path, &st) != 0)
		rt_panic(NULL, 0, 0, "can't put '%s' in an archive: %s", path,
			 errno == ENOENT ? "it isn't there" : strerror(errno));
	entries = realloc(entries, sizeof *entries * (size_t)(nentries + 1));
	Entry *e = &entries[nentries++];
	e->path = strdup(path);
	e->folder = S_ISDIR(st.st_mode);
	e->st = st;
	size_t n = strlen(name);
	e->name = malloc(n + 2);
	memcpy(e->name, name, n + 1);
	while (n > 1 && e->name[n - 1] == '/')
		e->name[--n] = '\0';
	if (e->folder) {
		e->name[n] = '/';
		e->name[n + 1] = '\0';
		/* and everything in it, in order */
		DIR *d = opendir(path);
		if (!d)
			return;
		char **names = NULL;
		int count = 0;
		struct dirent *de;
		while ((de = readdir(d))) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			names = realloc(names, sizeof *names * (size_t)(count + 1));
			names[count++] = strdup(de->d_name);
		}
		closedir(d);
		qsort(names, (size_t)count, sizeof *names, by_name);
		char *mine = strdup(e->name);   /* (e moves as entries grows) */
		for (int i = 0; i < count; i++) {
			char *p = join(path, names[i]), *nm = join(mine, names[i]);
			add_entry(p, nm);
			free(p);
			free(nm);
			free(names[i]);
		}
		free(mine);
		free(names);
	}
}

void cby_archives_start(const char *archive)
{
	entries_clear();
	free(making);
	making = strdup(archive);
	kind_by_name(archive);   /* (a panic now, for a name it can't make) */
}

void cby_archives_add(const char *path)
{
	add_entry(path, stored_name(path));
}

/* ------------------------------------------------------------ zip */

/* Zip files are written and read a piece at a time, so a file of any size
 * goes in or comes out without having to fit in memory. Past 4 GB (a file,
 * or the whole archive, or 65535 files), zip's own fields are too small,
 * and zip64's records hold the real numbers: written only when they're
 * needed (or always, with CABBY_ZIP64=1, for the tests). */

static void dos_time(time_t t, unsigned *date, unsigned *clock)
{
	struct tm tm;
	localtime_r(&t, &tm);
	if (tm.tm_year < 80) {
		*date = (1 << 5) | 1;
		*clock = 0;
		return;
	}
	*date = (unsigned)((tm.tm_year - 80) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
	*clock = (unsigned)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
}

static void put64(Buf *o, uint64_t v)
{
	put32(o, (unsigned long)(v & 0xFFFFFFFFu));
	put32(o, (unsigned long)(v >> 32));
}

static uint64_t get64(const unsigned char *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

static int force_zip64(void)
{
	const char *f = getenv("CABBY_ZIP64");
	return f && *f == '1';
}

#define ZIP_MAX32 0xFFFFFFFFu

static _Noreturn void zip_write_failed(FILE *out)
{
	fclose(out);
	rt_panic(NULL, 0, 0, "can't make '%s': %s (is the disk full?)", making, strerror(errno));
}

static ZipAes *zip_encrypting;   /* the file being written's encryption (NULL: none) */

/* a file's bytes, onto the zip: encrypted first, with a password */
static void zip_put(FILE *out, unsigned char *p, size_t n)
{
	if (zip_encrypting)
		zip_aes_encrypt(zip_encrypting, p, n);
	if (n && fwrite(p, 1, n, out) != n)
		zip_write_failed(out);
}

/* with a password: a fresh salt, and the bytes that check the password,
 * at the file's start; its encryption begun */
static void zip_aes_begin(FILE *out, ZipAes *za)
{
	uint8_t salt[16], check[2];
	random_bytes(salt, 16);
	zip_aes_start(za, zip_password, salt, 3, check);
	if (fwrite(salt, 1, 16, out) != 16 || fwrite(check, 1, 2, out) != 2)
		zip_write_failed(out);
	zip_encrypting = za;
}

/* and at its end, the 10 bytes that check what's encrypted */
static void zip_aes_finish(FILE *out, ZipAes *za)
{
	uint8_t code[10];
	zip_aes_end(za, code);
	zip_encrypting = NULL;
	if (fwrite(code, 1, 10, out) != 10)
		zip_write_failed(out);
}

/* one file's bytes, squeezed (raw deflate), onto out: its CRC, and how
 * many bytes went in and came out */
static void zip_squeeze(FILE *out, Entry *e, unsigned long *sum, uint64_t *size, uint64_t *packed)
{
	FILE *in = fopen(e->path, "rb");
	if (!in) {
		fclose(out);
		rt_panic(NULL, 0, 0, "can't read '%s': %s", e->path, strerror(errno));
	}
	static unsigned char inb[65536], outb[65536];
	ZStream st;
	squeeze_start(&st, -15);
	*sum = 0;
	*size = *packed = 0;
	int finish = 0;
	while (!finish) {
		size_t n = fread(inb, 1, sizeof inb, in);
		finish = n < sizeof inb;
		*sum = crc(*sum, inb, n);
		*size += n;
		st.next_in = inb;
		st.avail_in = (unsigned)n;
		int r;
		do {
			st.next_out = outb;
			st.avail_out = sizeof outb;
			r = z.deflate(&st, finish ? Z_FINISH_ : Z_NO_FLUSH_);
			size_t got = sizeof outb - st.avail_out;
			zip_put(out, outb, got);
			*packed += got;
		} while (st.avail_out == 0 || (finish && r == Z_OK_));
	}
	z.deflate_end(&st);
	fclose(in);
	if ((unsigned long long)*size != (unsigned long long)e->st.st_size) {
		fclose(out);
		rt_panic(NULL, 0, 0, "'%s' changed while it was being put in the archive", e->path);
	}
}

/* the same file again, kept as it is (when squeezing made it no smaller) */
static void zip_store(FILE *out, Entry *e)
{
	FILE *in = fopen(e->path, "rb");
	if (!in) {
		fclose(out);
		rt_panic(NULL, 0, 0, "can't read '%s': %s", e->path, strerror(errno));
	}
	static unsigned char b[65536];
	size_t n;
	while ((n = fread(b, 1, sizeof b, in)) > 0)
		zip_put(out, b, n);
	fclose(in);
}

static void make_zip(void)
{
	FILE *out = fopen(making, "wb");
	if (!out)
		rt_panic(NULL, 0, 0, "can't make '%s': %s", making, strerror(errno));
	int always64 = force_zip64();
	Buf dir = { 0 };
	for (int i = 0; i < nentries; i++) {
		Entry *e = &entries[i];
		uint64_t local_at = (uint64_t)ftello(out);
		/* a file that may need zip64's sizes has room for them in its header */
		int big = always64 || (!e->folder && (uint64_t)e->st.st_size >= 0xFFFF0000u);
		int locked = zip_password && !e->folder;   /* (encrypted, with WinZip's AES) */
		unsigned date, clock;
		dos_time(e->st.st_mtime, &date, &clock);
		size_t nlen = strlen(e->name);
		Buf h = { 0 };
		put32(&h, 0x04034b50);
		put16(&h, locked ? 51 : big ? 45 : 20);
		put16(&h, 1 << 11 | (locked ? 1 : 0));   /* (names are UTF-8; encrypted) */
		put16(&h, 0);         /* how it's kept, its CRC and sizes: filled in after */
		put16(&h, clock);
		put16(&h, date);
		put32(&h, 0);
		put32(&h, big ? ZIP_MAX32 : 0);
		put32(&h, big ? ZIP_MAX32 : 0);
		put16(&h, (unsigned)nlen);
		put16(&h, (big ? 20 : 0) + (locked ? 11 : 0));
		buf_put(&h, e->name, nlen);
		if (big) {
			put16(&h, 1);   /* zip64's sizes */
			put16(&h, 16);
			put64(&h, 0);
			put64(&h, 0);
		}
		size_t aes_extra_at = h.n;
		if (locked) {
			put16(&h, 0x9901);   /* WinZip's AES: AE-2, AES-256, and how it's kept (filled in after) */
			put16(&h, 7);
			put16(&h, 2);
			buf_put(&h, "AE", 2);
			buf_put(&h, "\3", 1);
			put16(&h, 0);
		}
		if (fwrite(h.b, 1, h.n, out) != h.n)
			zip_write_failed(out);
		uint64_t data_at = local_at + h.n;
		free(h.b);
		unsigned method = 0;
		unsigned long sum = 0;
		uint64_t size = 0, packed = 0;
		ZipAes aes;
		if (!e->folder) {
			if (locked)
				zip_aes_begin(out, &aes);
			zip_squeeze(out, e, &sum, &size, &packed);
			method = 8;
			if (packed >= size) {
				/* (no smaller squeezed: kept as it is instead, encrypted
				 * afresh, with a salt of its own) */
				if (locked) {
					uint8_t unused[10];
					zip_aes_end(&aes, unused);   /* (the first try's code, let go of) */
					zip_encrypting = NULL;
				}
				fseeko(out, (off_t)data_at, SEEK_SET);
				if (locked)
					zip_aes_begin(out, &aes);
				zip_store(out, e);
				packed = size;
				method = 0;
			}
			if (locked) {
				zip_aes_finish(out, &aes);
				packed += 16 + 2 + 10;
			}
		}
		unsigned real_method = method;
		if (locked) {
			method = 99;   /* (AES: how it's really kept is in its extra field) */
			sum = 0;       /* (AE-2 keeps no CRC: the HMAC checks it instead) */
		}
		uint64_t end_at = data_at + packed;
		/* back to fill in the header */
		Buf f = { 0 };
		put16(&f, method);
		fseeko(out, (off_t)(local_at + 8), SEEK_SET);
		fwrite(f.b, 1, f.n, out);
		if (locked) {
			f.n = 0;
			put16(&f, real_method);
			fseeko(out, (off_t)(local_at + aes_extra_at + 9), SEEK_SET);
			fwrite(f.b, 1, f.n, out);
		}
		f.n = 0;
		put32(&f, sum);
		put32(&f, big ? ZIP_MAX32 : packed);
		put32(&f, big ? ZIP_MAX32 : size);
		fseeko(out, (off_t)(local_at + 14), SEEK_SET);
		fwrite(f.b, 1, f.n, out);
		if (big) {
			f.n = 0;
			put64(&f, size);
			put64(&f, packed);
			fseeko(out, (off_t)(local_at + 30 + nlen + 4), SEEK_SET);
			fwrite(f.b, 1, f.n, out);
		}
		free(f.b);
		if (fseeko(out, (off_t)end_at, SEEK_SET) != 0)
			zip_write_failed(out);
		/* and its line in the directory at the end: zip64's numbers in an
		 * extra field, for those too big for their own */
		int big_size = always64 || size >= ZIP_MAX32, big_packed = always64 || packed >= ZIP_MAX32,
		    big_at = always64 || local_at >= ZIP_MAX32;
		unsigned extra = (unsigned)(8 * (big_size + big_packed + big_at));
		put32(&dir, 0x02014b50);
		put16(&dir, 3 << 8 | (locked ? 51 : extra ? 45 : 20));   /* made on Unix, so its permissions are kept */
		put16(&dir, locked ? 51 : extra ? 45 : 20);
		put16(&dir, 1 << 11 | (locked ? 1 : 0));
		put16(&dir, method);
		put16(&dir, clock);
		put16(&dir, date);
		put32(&dir, sum);
		put32(&dir, big_packed ? ZIP_MAX32 : packed);
		put32(&dir, big_size ? ZIP_MAX32 : size);
		put16(&dir, (unsigned)nlen);
		put16(&dir, (extra ? extra + 4 : 0) + (locked ? 11 : 0));
		put16(&dir, 0);
		put16(&dir, 0);
		put16(&dir, 0);
		put32(&dir, ((unsigned long)(e->st.st_mode & 0xFFFF) << 16) | (e->folder ? 0x10 : 0));
		put32(&dir, big_at ? ZIP_MAX32 : local_at);
		buf_put(&dir, e->name, nlen);
		if (extra) {
			put16(&dir, 1);
			put16(&dir, extra);
			if (big_size)
				put64(&dir, size);
			if (big_packed)
				put64(&dir, packed);
			if (big_at)
				put64(&dir, local_at);
		}
		if (locked) {
			put16(&dir, 0x9901);
			put16(&dir, 7);
			put16(&dir, 2);
			buf_put(&dir, "AE", 2);
			buf_put(&dir, "\3", 1);
			put16(&dir, real_method);
		}
	}
	uint64_t dir_at = (uint64_t)ftello(out);
	if (dir.n && fwrite(dir.b, 1, dir.n, out) != dir.n)
		zip_write_failed(out);
	Buf end = { 0 };
	uint64_t count = (uint64_t)nentries;
	int end64 = always64 || count >= 0xFFFF || dir_at >= ZIP_MAX32 || dir.n >= ZIP_MAX32;
	if (end64) {
		uint64_t end64_at = dir_at + dir.n;
		put32(&end, 0x06064b50);   /* zip64's end of the directory */
		put64(&end, 44);
		put16(&end, 3 << 8 | 45);
		put16(&end, 45);
		put32(&end, 0);
		put32(&end, 0);
		put64(&end, count);
		put64(&end, count);
		put64(&end, dir.n);
		put64(&end, dir_at);
		put32(&end, 0x07064b50);   /* and where it is */
		put32(&end, 0);
		put64(&end, end64_at);
		put32(&end, 1);
	}
	put32(&end, 0x06054b50);
	put16(&end, 0);
	put16(&end, 0);
	put16(&end, end64 ? 0xFFFF : (unsigned)count);
	put16(&end, end64 ? 0xFFFF : (unsigned)count);
	put32(&end, end64 ? ZIP_MAX32 : dir.n);
	put32(&end, end64 ? ZIP_MAX32 : dir_at);
	put16(&end, 0);
	if (fwrite(end.b, 1, end.n, out) != end.n)
		zip_write_failed(out);
	free(end.b);
	free(dir.b);
	/* (a file kept as it was after it was squeezed may leave bytes past the end) */
	fflush(out);
	if (ftruncate(fileno(out), ftello(out)) != 0 || fclose(out) != 0)
		rt_panic(NULL, 0, 0, "can't make '%s': %s", making, strerror(errno));
}

/* a zip's directory: each entry's name, how it's kept, and where */
typedef struct {
	char *name;
	unsigned method;
	uint64_t packed, size, offset;
	unsigned long sum, attributes;
	unsigned flags;               /* (bit 0: encrypted; bit 3: its CRC comes after it) */
	int aes_strength, aes_method, aes_version;   /* WinZip's AES (strength 0: not) */
	unsigned dos_time;
} ZipItem;

static _Thread_local ZipItem *zip_items;
static _Thread_local int nzip;
static _Thread_local FILE *zip_file;

static void zip_close(void)
{
	for (int i = 0; i < nzip; i++)
		free(zip_items[i].name);
	free(zip_items);
	zip_items = NULL;
	nzip = 0;
	if (zip_file)
		fclose(zip_file);
	zip_file = NULL;
}

static int zip_read_at(uint64_t at, void *p, size_t n)
{
	return fseeko(zip_file, (off_t)at, SEEK_SET) == 0 && fread(p, 1, n, zip_file) == n;
}

static void zip_open(const char *path)
{
	zip_close();
	zip_file = fopen(path, "rb");
	if (!zip_file)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, errno == ENOENT ? "it isn't there" : strerror(errno));
	fseeko(zip_file, 0, SEEK_END);
	uint64_t len = (uint64_t)ftello(zip_file);
	/* the end of its directory: within the last 64 KB and a bit */
	size_t tail = len < 65557 ? (size_t)len : 65557;
	unsigned char *t = malloc(tail + 1);
	if (!zip_read_at(len - tail, t, tail)) {
		free(t);
		rt_panic(NULL, 0, 0, "'%s' can't be read", path);
	}
	long at = -1;
	for (long i = (long)tail - 22; i >= 0; i--)
		if (get32(t + i) == 0x06054b50) {
			at = i;
			break;
		}
	if (at < 0) {
		free(t);
		rt_panic(NULL, 0, 0, "'%s' isn't a zip file that can be read: its directory isn't there", path);
	}
	uint64_t count = get16(t + at + 10), dir_len = get32(t + at + 12), start = get32(t + at + 16);
	uint64_t end_at = len - tail + (uint64_t)at;
	if (at >= 20 && get32(t + at - 20) == 0x07064b50) {
		/* zip64: the real numbers are in its own end record */
		uint64_t end64 = get64(t + at - 20 + 8);
		unsigned char r[56];
		if (end64 + 56 <= end_at && zip_read_at(end64, r, 56) && get32(r) == 0x06064b50) {
			count = get64(r + 32);
			dir_len = get64(r + 40);
			start = get64(r + 48);
		}
	}
	free(t);
	if (start + dir_len > len || dir_len > ((uint64_t)1 << 32))
		rt_panic(NULL, 0, 0, "'%s' is broken: its directory doesn't add up", path);
	unsigned char *dir = malloc((size_t)dir_len + 1);
	if (!zip_read_at(start, dir, (size_t)dir_len)) {
		free(dir);
		rt_panic(NULL, 0, 0, "'%s' is broken: its directory is cut short", path);
	}
	size_t p = 0;
	for (uint64_t i = 0; i < count; i++) {
		if (p + 46 > dir_len || get32(dir + p) != 0x02014b50) {
			free(dir);
			rt_panic(NULL, 0, 0, "'%s' is broken: its directory doesn't add up", path);
		}
		const unsigned char *d = dir + p;
		unsigned nlen = get16(d + 28), xlen = get16(d + 30), clen = get16(d + 32);
		if (p + 46 + nlen + xlen + clen > dir_len) {
			free(dir);
			rt_panic(NULL, 0, 0, "'%s' is broken: its directory doesn't add up", path);
		}
		zip_items = realloc(zip_items, sizeof *zip_items * (size_t)(nzip + 1));
		ZipItem *it = &zip_items[nzip++];
		it->method = get16(d + 10);
		it->flags = get16(d + 8);
		it->dos_time = get16(d + 12);
		it->aes_strength = 0;
		it->sum = get32(d + 16);
		it->packed = get32(d + 20);
		it->size = get32(d + 24);
		it->attributes = get32(d + 38);
		it->offset = get32(d + 42);
		it->name = malloc(nlen + 1);
		memcpy(it->name, d + 46, nlen);
		it->name[nlen] = '\0';
		/* zip64's numbers, for each field that's too small for its own */
		const unsigned char *x = d + 46 + nlen, *xend = x + xlen;
		while (x + 4 <= xend) {
			unsigned id = get16(x), n = get16(x + 2);
			const unsigned char *v = x + 4, *vend = v + n;
			if (vend > xend)
				break;
			if (id == 0x9901 && n >= 7) {
				it->aes_version = (int)get16(v);
				it->aes_strength = v[4];
				it->aes_method = (int)get16(v + 5);
			}
			if (id == 1) {
				if (it->size == ZIP_MAX32 && v + 8 <= vend) { it->size = get64(v); v += 8; }
				if (it->packed == ZIP_MAX32 && v + 8 <= vend) { it->packed = get64(v); v += 8; }
				if (it->offset == ZIP_MAX32 && v + 8 <= vend) { it->offset = get64(v); v += 8; }
			}
			x = vend;
		}
		p += 46 + nlen + xlen + clen;
	}
	free(dir);
}

/* one entry's bytes, unsqueezed and checked, a piece at a time: to a file,
 * or kept in memory */
static void zip_get(const char *path, ZipItem *it, FILE *to, Buf *keep)
{
	unsigned char h[30];
	if (!zip_read_at(it->offset, h, 30) || get32(h) != 0x04034b50)
		rt_panic(NULL, 0, 0, "'%s' is broken at '%s'", path, it->name);
	uint64_t at = it->offset + 30 + get16(h + 26) + get16(h + 28);
	if (fseeko(zip_file, (off_t)at, SEEK_SET) != 0)
		rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it's cut short", path, it->name);
	/* kept with a password: WinZip's AES, or PKZIP's ZipCrypto */
	unsigned method = it->method;
	uint64_t left = it->packed;
	ZipAes aes;
	ZipCrypto zc;
	int locked = it->flags & 1, by_aes = locked && method == 99 && it->aes_strength >= 1 && it->aes_strength <= 3;
	if (locked && !zip_password)
		rt_panic(NULL, 0, 0, "'%s' in '%s' is kept with a password: give it with archives.unpack_with_password "
				     "(or read_with_password)", it->name, path);
	if (by_aes) {
		int sl = aes_salt_len(it->aes_strength);
		uint8_t salt[16], check[2], want[2];
		if (left < (uint64_t)sl + 12 || fread(salt, 1, (size_t)sl, zip_file) != (size_t)sl || fread(want, 1, 2, zip_file) != 2)
			rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it's cut short", path, it->name);
		zip_aes_start(&aes, zip_password, salt, it->aes_strength, check);
		if (memcmp(check, want, 2) != 0)
			rt_panic(NULL, 0, 0, "the password for '%s' is wrong", path);
		left -= (uint64_t)sl + 2 + 10;
		method = (unsigned)it->aes_method;
	} else if (locked) {
		if (method == 99 || (it->flags & 0x40))
			rt_panic(NULL, 0, 0, "'%s' keeps '%s' with a kind of encryption archives doesn't know", path, it->name);
		uint8_t head[12];
		if (left < 12 || fread(head, 1, 12, zip_file) != 12)
			rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it's cut short", path, it->name);
		zc_start(&zc, zip_password);
		zc_decrypt(&zc, head, 12);
		/* its last byte checks the password: the CRC's top byte (or the
		 * time's, when the CRC comes after the file) */
		uint8_t should = it->flags & 8 ? (uint8_t)(it->dos_time >> 8) : (uint8_t)(it->sum >> 24);
		if (head[11] != should)
			rt_panic(NULL, 0, 0, "the password for '%s' is wrong", path);
		left -= 12;
	}
	if (method != 0 && method != 8)
		rt_panic(NULL, 0, 0, "'%s' keeps '%s' in a way archives doesn't know (method %u)", path, it->name, method);
	static unsigned char inb[65536], outb[65536];
	ZStream st;
	if (method == 8)
		unsqueeze_start(&st, -15);
	uint64_t got = 0;
	unsigned long sum = 0;
	int ended = method == 0;
	while (left || (method == 8 && !ended)) {
		size_t n = left < sizeof inb ? (size_t)left : sizeof inb;
		if (n && fread(inb, 1, n, zip_file) != n) {
			if (method == 8)
				z.inflate_end(&st);
			rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it's cut short", path, it->name);
		}
		left -= n;
		if (by_aes)
			zip_aes_decrypt(&aes, inb, n);
		else if (locked)
			zc_decrypt(&zc, inb, n);
		if (method == 0) {
			sum = crc(sum, inb, n);
			got += n;
			if (to && fwrite(inb, 1, n, to) != n)
				rt_panic(NULL, 0, 0, "can't unpack '%s': %s", it->name, strerror(errno));
			if (keep)
				buf_put(keep, inb, n);
			continue;
		}
		st.next_in = inb;
		st.avail_in = (unsigned)n;
		do {
			st.next_out = outb;
			st.avail_out = sizeof outb;
			int r = z.inflate(&st, Z_NO_FLUSH_);
			size_t out = sizeof outb - st.avail_out;
			sum = crc(sum, outb, out);
			got += out;
			if (to && out && fwrite(outb, 1, out, to) != out)
				rt_panic(NULL, 0, 0, "can't unpack '%s': %s", it->name, strerror(errno));
			if (keep)
				buf_put(keep, outb, out);
			if (r == Z_STREAM_END_) {
				ended = 1;
				break;
			}
			if (r != Z_OK_ && !(r == Z_BUF_ERROR_ && !st.avail_in)) {
				z.inflate_end(&st);
				rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it won't unsqueeze", path, it->name);
			}
		} while (st.avail_in || st.avail_out == 0);
		if (!left && !ended && !n) {
			z.inflate_end(&st);
			rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it's cut short", path, it->name);
		}
	}
	if (method == 8)
		z.inflate_end(&st);
	if (by_aes) {
		/* the 10 bytes that check it: a wrong one, and it's been changed */
		uint8_t code[10], want[10];
		zip_aes_end(&aes, code);
		if (fread(want, 1, 10, zip_file) != 10 || memcmp(code, want, 10) != 0)
			rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it isn't what was put in (its check code is wrong)",
				 path, it->name);
		if (it->aes_version == 2)
			sum = it->sum;   /* (AE-2 keeps no CRC: the code is the check) */
	}
	if (got != it->size || sum != it->sum)
		rt_panic(NULL, 0, 0, "'%s' is broken at '%s': it isn't what was put in (its %s is wrong)", path, it->name,
			 got != it->size ? "size" : "CRC");
}

/* ------------------------------------------------------------ tar, a piece at a time */

/* where tar's bytes go, or come from: a file, gzipped or not */
typedef struct {
	FILE *f;
	int gz;         /* how it's squeezed: 0 not, 1 gzip, 2 xz */
	ZStream s;
	XzStream x;
	unsigned char buf[65536];
	int done;
} Stream;

static void stream_open_write(Stream *t, const char *path, int gz)
{
	memset(t, 0, sizeof *t);
	t->f = fopen(path, "wb");
	if (!t->f)
		rt_panic(NULL, 0, 0, "can't make '%s': %s", path, strerror(errno));
	t->gz = gz;
	if (gz == 1)
		squeeze_start(&t->s, 31);
	if (gz == 2) {
		need_xz();
		if (xz.easy_encoder(&t->x, 6, 4 /* CRC64 */) != LZMA_OK_)
			rt_panic(NULL, 0, 0, "archives: liblzma couldn't start squeezing");
	}
}

static void stream_write(Stream *t, const void *p, size_t n, int finish)
{
	if (!t->gz) {
		fwrite(p, 1, n, t->f);
		return;
	}
	if (t->gz == 2) {
		t->x.next_in = p;
		t->x.avail_in = n;
		int r;
		do {
			t->x.next_out = t->buf;
			t->x.avail_out = sizeof t->buf;
			r = xz.code(&t->x, finish ? LZMA_FINISH_ : LZMA_RUN_);
			fwrite(t->buf, 1, sizeof t->buf - t->x.avail_out, t->f);
			if (r != LZMA_OK_ && r != LZMA_STREAM_END_)
				rt_panic(NULL, 0, 0, "archives: liblzma couldn't squeeze (%d)", r);
		} while (t->x.avail_out == 0 || (finish && r == LZMA_OK_));
		return;
	}
	t->s.next_in = p;
	t->s.avail_in = (unsigned)n;
	int r;
	do {
		t->s.next_out = t->buf;
		t->s.avail_out = sizeof t->buf;
		r = z.deflate(&t->s, finish ? Z_FINISH_ : Z_NO_FLUSH_);
		fwrite(t->buf, 1, sizeof t->buf - t->s.avail_out, t->f);
	} while (t->s.avail_out == 0 || (finish && r == Z_OK_));
}

static void stream_close_write(Stream *t, const char *path)
{
	if (t->gz) {
		stream_write(t, "", 0, 1);
		if (t->gz == 2)
			xz.end(&t->x);
		else
			z.deflate_end(&t->s);
	}
	if (fclose(t->f) != 0)
		rt_panic(NULL, 0, 0, "can't make '%s': %s", path, strerror(errno));
}

static void stream_open_read(Stream *t, const char *path, int gz)
{
	memset(t, 0, sizeof *t);
	t->f = fopen(path, "rb");
	if (!t->f)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, errno == ENOENT ? "it isn't there" : strerror(errno));
	t->gz = gz;
	if (gz == 1)
		unsqueeze_start(&t->s, 47);
	if (gz == 2) {
		need_xz();
		if (xz.stream_decoder(&t->x, UINT64_MAX, 0x08 /* concatenated */) != LZMA_OK_)
			rt_panic(NULL, 0, 0, "archives: liblzma couldn't start unsqueezing");
	}
}

/* exactly n bytes (0: the end came first) */
static int stream_read(Stream *t, unsigned char *p, size_t n)
{
	if (!t->gz)
		return fread(p, 1, n, t->f) == n;
	if (t->gz == 2) {
		t->x.next_out = p;
		t->x.avail_out = n;
		while (t->x.avail_out && !t->done) {
			int action = LZMA_RUN_;
			if (!t->x.avail_in) {
				size_t got = fread(t->buf, 1, sizeof t->buf, t->f);
				t->x.next_in = t->buf;
				t->x.avail_in = got;
				if (!got)
					action = LZMA_FINISH_;   /* (the end: whatever's left) */
			}
			int r = xz.code(&t->x, action);
			if (r == LZMA_STREAM_END_)
				t->done = 1;
			else if (r != LZMA_OK_)
				return 0;
		}
		return t->x.avail_out == 0;
	}
	t->s.next_out = p;
	t->s.avail_out = (unsigned)n;
	while (t->s.avail_out && !t->done) {
		if (!t->s.avail_in) {
			size_t got = fread(t->buf, 1, sizeof t->buf, t->f);
			if (!got)
				return 0;
			t->s.next_in = t->buf;
			t->s.avail_in = (unsigned)got;
		}
		int r = z.inflate(&t->s, Z_NO_FLUSH_);
		if (r == Z_STREAM_END_)
			t->done = 1;
		else if (r != Z_OK_ && r != Z_BUF_ERROR_)
			return 0;
	}
	return t->s.avail_out == 0;
}

static void stream_close_read(Stream *t)
{
	if (t->gz == 2)
		xz.end(&t->x);
	else if (t->gz)
		z.inflate_end(&t->s);
	fclose(t->f);
}

/* a number in a tar header's field: octal digits and a 0 byte, or, when
 * it won't fit (a file over 8 GB), as bytes with the top bit set first, as
 * GNU tar does */
static void octal(char *field, size_t width, unsigned long long v)
{
	unsigned long long top = 1;
	for (size_t i = 0; i + 1 < width; i++)
		top *= 8;
	if (v >= top) {
		memset(field, 0, width);
		field[0] = (char)0x80;
		for (size_t i = width - 1; i > 0 && v; i--, v >>= 8)
			field[i] = (char)(v & 0xFF);
		return;
	}
	for (size_t i = width - 1; i-- > 0; v /= 8)
		field[i] = (char)('0' + v % 8);
	field[width - 1] = '\0';
}

/* a tar header for an entry (and, for a long name, GNU's long-name entry
 * before it) */
static void tar_header(Stream *t, const char *name, int folder, const struct stat *st, unsigned long long size)
{
	unsigned char h[512];
	size_t nlen = strlen(name);
	if (nlen > 100) {
		/* GNU's way: an entry "././@LongLink" holding the name */
		memset(h, 0, sizeof h);
		strcpy((char *)h, "././@LongLink");
		octal((char *)h + 100, 8, 0644);
		octal((char *)h + 108, 8, 0);
		octal((char *)h + 116, 8, 0);
		octal((char *)h + 124, 12, nlen + 1);
		octal((char *)h + 136, 12, 0);
		h[156] = 'L';
		memcpy(h + 257, "ustar  ", 8);
		memset(h + 148, ' ', 8);
		unsigned sum = 0;
		for (int i = 0; i < 512; i++)
			sum += h[i];
		snprintf((char *)h + 148, 8, "%06o", sum);
		stream_write(t, h, 512, 0);
		size_t padded = (nlen + 1 + 511) / 512 * 512;
		unsigned char *nb = calloc(1, padded);
		memcpy(nb, name, nlen);
		stream_write(t, nb, padded, 0);
		free(nb);
	}
	memset(h, 0, sizeof h);
	memcpy(h, name, nlen > 100 ? 100 : nlen);
	octal((char *)h + 100, 8, st->st_mode & 07777);
	octal((char *)h + 108, 8, 0);
	octal((char *)h + 116, 8, 0);
	octal((char *)h + 124, 12, size);
	octal((char *)h + 136, 12, (unsigned long long)st->st_mtime);
	h[156] = folder ? '5' : '0';
	memcpy(h + 257, nlen > 100 ? "ustar  " : "ustar", nlen > 100 ? 8 : 6);
	if (nlen <= 100)
		memcpy(h + 263, "00", 2);
	memset(h + 148, ' ', 8);
	unsigned sum = 0;
	for (int i = 0; i < 512; i++)
		sum += h[i];
	snprintf((char *)h + 148, 8, "%06o", sum);
	stream_write(t, h, 512, 0);
}

static void make_tar(int gz)
{
	Stream t;
	stream_open_write(&t, making, gz);
	for (int i = 0; i < nentries; i++) {
		Entry *e = &entries[i];
		unsigned long long size = e->folder ? 0 : (unsigned long long)e->st.st_size;
		tar_header(&t, e->name, e->folder, &e->st, size);
		if (e->folder)
			continue;
		FILE *f = fopen(e->path, "rb");
		if (!f)
			rt_panic(NULL, 0, 0, "can't read '%s': %s", e->path, strerror(errno));
		static unsigned char buf[65536];
		unsigned long long left = size;
		size_t n;
		while (left && (n = fread(buf, 1, left < sizeof buf ? (size_t)left : sizeof buf, f)) > 0) {
			stream_write(&t, buf, n, 0);
			left -= n;
		}
		fclose(f);
		if (left)
			rt_panic(NULL, 0, 0, "'%s' changed while it was being put in the archive", e->path);
		static const unsigned char zero[512];
		if (size % 512)
			stream_write(&t, zero, 512 - size % 512, 0);
	}
	static const unsigned char end[1024];
	stream_write(&t, end, sizeof end, 0);
	stream_close_write(&t, making);
}

void cby_archives_end(void)
{
	int kind = kind_by_name(making);
	if (zip_password && kind != ZIP) {
		entries_clear();
		rt_panic(NULL, 0, 0, "only a zip file can have a password, not '%s' (call it .zip)", making);
	}
	if (kind == ZIP)
		make_zip();
	else
		make_tar(squeezed(kind));
	entries_clear();
}

/* ---- reading a tar: each entry, with what to do with it */

static unsigned long long from_octal(const unsigned char *p, size_t n)
{
	unsigned long long v = 0;
	if (p[0] & 0x80) {   /* (GNU's way, for big numbers) */
		for (size_t i = 1; i < n; i++)
			v = v << 8 | p[i];
		return v;
	}
	for (size_t i = 0; i < n && p[i]; i++)
		if (p[i] >= '0' && p[i] <= '7')
			v = v * 8 + (unsigned)(p[i] - '0');
	return v;
}

/* what to do with each entry: 0 list it, 1 unpack it into folder, 2 keep
 * the one called want */
typedef struct {
	int what;
	const char *folder, *want;
	int found;
} TarJob;

static void tar_entry(const char *archive, TarJob *job, const char *name, int type, unsigned mode,
		      Stream *t, unsigned long long size)
{
	static unsigned char buf[65536];
	int file = type == '0' || type == 0 || type == '7';
	if (job->what == 0 && (file || type == '5'))
		result_n(name, strlen(name));
	FILE *out = NULL;
	char *dest = NULL;
	Buf keep = { 0 };
	if (job->what == 1 && (file || type == '5')) {
		if (unsafe_name(name))
			rt_panic(NULL, 0, 0, "won't unpack '%s' from '%s': it would go outside the folder", name, archive);
		dest = join(job->folder, name);
		make_parents(dest);
		if (type == '5') {
			mkdir(dest, 0755);
		} else {
			out = fopen(dest, "wb");
			if (!out)
				rt_panic(NULL, 0, 0, "can't unpack '%s': %s", dest, strerror(errno));
		}
	}
	int wanted = job->what == 2 && file && strcmp(name, job->want) == 0;
	unsigned long long left = (size + 511) / 512 * 512, data = size;
	while (left) {
		size_t n = left < sizeof buf ? (size_t)left : sizeof buf;
		if (!stream_read(t, buf, n))
			rt_panic(NULL, 0, 0, "'%s' is cut short, in '%s'", archive, name);
		size_t use = data < n ? (size_t)data : n;
		if (out)
			fwrite(buf, 1, use, out);
		if (wanted)
			buf_put(&keep, buf, use);
		data -= use;
		left -= n;
	}
	if (out) {
		fclose(out);
		chmod(dest, mode & 0777);
	}
	free(dest);
	if (wanted) {
		result_n(keep.b ? keep.b : (unsigned char *)"", keep.n);
		job->found = 1;
	}
	free(keep.b);
}

static void read_tar(const char *archive, int gz, TarJob *job)
{
	Stream t;
	stream_open_read(&t, archive, gz);
	unsigned char h[512];
	char *long_name = NULL;
	while (stream_read(&t, h, 512)) {
		int empty = 1;
		for (int i = 0; i < 512 && empty; i++)
			empty = !h[i];
		if (empty)
			break;
		unsigned long long size = from_octal(h + 124, 12);
		int type = h[156];
		if (type == 'L' || type == 'x') {
			/* a long name: GNU's, or POSIX's in "path=" */
			unsigned char *b = malloc(size + 1);
			size_t padded = (size + 511) / 512 * 512;
			unsigned char *all = malloc(padded ? padded : 1);
			if (padded && !stream_read(&t, all, padded))
				rt_panic(NULL, 0, 0, "'%s' is cut short", archive);
			memcpy(b, all, size);
			b[size] = '\0';
			free(all);
			if (type == 'L') {
				free(long_name);
				long_name = strdup((char *)b);
			} else {
				char *p = strstr((char *)b, " path=");
				if (p) {
					p += 6;
					char *e = strchr(p, '\n');
					free(long_name);
					long_name = e ? strndup(p, (size_t)(e - p)) : strdup(p);
				}
			}
			free(b);
			continue;
		}
		char name[260];
		if (long_name) {
			snprintf(name, sizeof name, "%s", long_name);
		} else if (memcmp(h + 257, "ustar", 5) == 0 && h[345]) {
			snprintf(name, sizeof name, "%.155s/%.100s", (char *)h + 345, (char *)h);
		} else {
			snprintf(name, sizeof name, "%.100s", (char *)h);
		}
		free(long_name);
		long_name = NULL;
		tar_entry(archive, job, name, type, (unsigned)from_octal(h + 100, 8), &t, type == '5' ? 0 : size);
		if (job->found)
			break;
	}
	free(long_name);
	stream_close_read(&t);
}

/* ------------------------------------------------------------ reading any of them */

int64_t cby_archives_list(const char *archive)
{
	results_clear();
	int kind = kind_of_file(archive);
	if (kind == ZIP) {
		zip_open(archive);
		for (int i = 0; i < nzip; i++)
			result_n(zip_items[i].name, strlen(zip_items[i].name));
		zip_close();
	} else {
		TarJob job = { 0, NULL, NULL, 0 };
		read_tar(archive, squeezed(kind), &job);
	}
	return nres;
}

void cby_archives_unpack(const char *archive, const char *folder)
{
	int kind = kind_of_file(archive);
	mkdir(folder, 0755);
	if (kind == ZIP) {
		zip_open(archive);
		for (int i = 0; i < nzip; i++) {
			ZipItem *it = &zip_items[i];
			if (unsafe_name(it->name))
				rt_panic(NULL, 0, 0, "won't unpack '%s' from '%s': it would go outside the folder", it->name,
					 archive);
			char *dest = join(folder, it->name);
			make_parents(dest);
			size_t n = strlen(it->name);
			if (n && it->name[n - 1] == '/') {
				mkdir(dest, 0755);
			} else {
				FILE *f = fopen(dest, "wb");
				if (!f)
					rt_panic(NULL, 0, 0, "can't unpack '%s': %s", dest, strerror(errno));
				zip_get(archive, it, f, NULL);
				if (fclose(f) != 0)
					rt_panic(NULL, 0, 0, "can't unpack '%s': %s", dest, strerror(errno));
				unsigned mode = (unsigned)(it->attributes >> 16) & 0777;
				if (mode)
					chmod(dest, mode);
			}
			free(dest);
		}
		zip_close();
	} else {
		TarJob job = { 1, folder, NULL, 0 };
		read_tar(archive, squeezed(kind), &job);
	}
}

/* whether the archive keeps anything with a password */
int cby_archives_locked(const char *archive)
{
	if (kind_of_file(archive) != ZIP)
		return 0;
	zip_open(archive);
	int locked = 0;
	for (int i = 0; i < nzip; i++)
		if (zip_items[i].flags & 1)
			locked = 1;
	zip_close();
	return locked;
}

/* one file from inside, without unpacking the rest: its length */
int64_t cby_archives_read(const char *archive, const char *name)
{
	results_clear();
	int kind = kind_of_file(archive);
	if (kind == ZIP) {
		zip_open(archive);
		for (int i = 0; i < nzip; i++)
			if (strcmp(zip_items[i].name, name) == 0) {
				Buf keep = { 0 };
				zip_get(archive, &zip_items[i], NULL, &keep);
				result_n(keep.b ? keep.b : (unsigned char *)"", keep.n);
				free(keep.b);
				break;
			}
		zip_close();
	} else {
		TarJob job = { 2, NULL, name, 0 };
		read_tar(archive, squeezed(kind), &job);
	}
	if (!nres)
		rt_panic(NULL, 0, 0, "there's no '%s' in '%s' (archives.list says what's in it)", name, archive);
	return (int64_t)reslen[0];
}

/* ------------------------------------------------------------ one file, gzipped */

void cby_archives_compress(const char *path, const char *to)
{
	FILE *in = fopen(path, "rb");
	if (!in)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, errno == ENOENT ? "it isn't there" : strerror(errno));
	Stream t;
	stream_open_write(&t, to, ends_with(to, ".xz") ? 2 : 1);   /* (xz by the name; gzip otherwise) */
	static unsigned char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		stream_write(&t, buf, n, 0);
	fclose(in);
	stream_close_write(&t, to);
}

void cby_archives_decompress(const char *path, const char *to)
{
	/* gzip or xz, by what's in it */
	FILE *f = fopen(path, "rb");
	if (!f)
		rt_panic(NULL, 0, 0, "can't read '%s': %s", path, errno == ENOENT ? "it isn't there" : strerror(errno));
	unsigned char head[6] = { 0 };
	size_t hn = fread(head, 1, sizeof head, f);
	fclose(f);
	int kind = hn >= 2 && head[0] == 0x1f && head[1] == 0x8b ? 1 : hn >= 6 && memcmp(head, "\xfd" "7zXZ\0", 6) == 0 ? 2 : 0;
	if (!kind)
		rt_panic(NULL, 0, 0, "'%s' isn't a gzipped (.gz) or xz (.xz) file", path);
	Stream t;
	stream_open_read(&t, path, kind);
	FILE *out = fopen(to, "wb");
	if (!out)
		rt_panic(NULL, 0, 0, "can't make '%s': %s", to, strerror(errno));
	static unsigned char buf[65536];
	for (;;) {
		/* (a piece at a time, whatever's there: the last piece may be short) */
		size_t want = sizeof buf, got = 0;
		if (kind == 2) {
			t.x.next_out = buf;
			t.x.avail_out = want;
			while (t.x.avail_out && !t.done) {
				int action = LZMA_RUN_;
				if (!t.x.avail_in) {
					size_t n = fread(t.buf, 1, sizeof t.buf, t.f);
					t.x.next_in = t.buf;
					t.x.avail_in = n;
					if (!n)
						action = LZMA_FINISH_;
				}
				int r = xz.code(&t.x, action);
				if (r == LZMA_STREAM_END_)
					t.done = 1;
				else if (r != LZMA_OK_) {
					fclose(out);
					rt_panic(NULL, 0, 0, "'%s' isn't an xz file that can be read", path);
				}
			}
			got = want - t.x.avail_out;
		} else {
			t.s.next_out = buf;
			t.s.avail_out = sizeof buf;
			if (!t.s.avail_in && !t.done) {
				size_t n = fread(t.buf, 1, sizeof t.buf, t.f);
				if (!n)
					break;
				t.s.next_in = t.buf;
				t.s.avail_in = (unsigned)n;
			}
			int r = z.inflate(&t.s, Z_NO_FLUSH_);
			got = sizeof buf - t.s.avail_out;
			if (r == Z_STREAM_END_)
				t.done = 1;
			else if (r != Z_OK_ && r != Z_BUF_ERROR_) {
				fclose(out);
				rt_panic(NULL, 0, 0, "'%s' isn't a gzipped file that can be read", path);
			}
		}
		if (got)
			fwrite(buf, 1, got, out);
		if (t.done && (kind == 1 || got < want))
			break;
	}
	fclose(out);
	stream_close_read(&t);
}

__attribute__((destructor)) static void archives_end(void)
{
	results_clear();
	free(res);
	free(reslen);
	entries_clear();
	free(making);
	zip_close();
}

/* a panic from archives.hby's Cabby code */
void cby_archives_panic(const char *why)
{
	rt_panic(NULL, 0, 0, "%s", why);
}

/* a worker's thread, ending: what it kept for itself */
static void archives_thread_end(void)
{
	results_clear();
	free(res);
	free(reslen);
	res = NULL;
	reslen = NULL;
	capres = 0;
	entries_clear();
	zip_close();
}
__attribute__((constructor)) static void archives_threads(void) { rt_on_thread_end(archives_thread_end); }
