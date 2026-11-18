/* text_layout.h — text in other scripts, and colored emoji, for the window
 * libraries (included by sdl_common.h, after its fonts and glyphs).
 *
 * Text of Latin, Greek and Cyrillic letters is written a character at a
 * time, as it always was (quick, and all it needs). Text with anything
 * past them (Hebrew, Arabic, Indian scripts, emoji...) is laid out first:
 *
 *   - its direction, by Unicode's bidirectional rules (the usual part of
 *     them: no embeddings): Hebrew and Arabic go right to left, with
 *     numbers and Latin words in them left to right, in their places;
 *   - shaped: by HarfBuzz where there is one (Linux has it; a Mac has it
 *     with Homebrew's harfbuzz), which joins Arabic's letters, puts marks
 *     in their places, and makes an emoji of a sequence (a family, a flag,
 *     a skin tone); without it, Arabic's letters are joined by Cabby
 *     itself, with Unicode's own joined forms;
 *   - and drawn, a glyph at a time: an emoji from a color font's own
 *     pictures (Noto Color Emoji's CBDT table, Apple Color Emoji's sbix),
 *     which stb_truetype can't read, so they're read here.
 *
 * CABBY_HARFBUZZ=none lays text out without HarfBuzz, and
 * CABBY_EMOJI_FONT=file.ttf (or none) chooses the emoji font: for the
 * tests, and to see a difference. */

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------- font tables */

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* a table of the font's: where it starts in the file (0: it hasn't one) */
static uint32_t font_table(const Font *f, const char *tag, uint32_t *len)
{
	const uint8_t *d = f->data;
	uint32_t at = f->offset;
	if (!d || at + 12 > f->len)
		return 0;
	int n = be16(d + at + 4);
	for (int i = 0; i < n; i++) {
		const uint8_t *r = d + at + 12 + 16 * i;
		if (r + 16 > d + f->len)
			return 0;
		if (memcmp(r, tag, 4) == 0) {
			uint32_t off = be32(r + 8), l = be32(r + 12);
			if (off + l > f->len)
				return 0;
			if (len)
				*len = l;
			return off;
		}
	}
	return 0;
}

/* the glyph for a character, from the font's cmap (formats 4 and 12): for
 * a color font, which stb_truetype can't open */
static int cmap_glyph(const Font *f, uint32_t code)
{
	uint32_t len, cmap = font_table(f, "cmap", &len);
	if (!cmap)
		return 0;
	const uint8_t *d = f->data;
	int n = be16(d + cmap + 2);
	uint32_t best4 = 0, best12 = 0;
	for (int i = 0; i < n; i++) {
		const uint8_t *r = d + cmap + 4 + 8 * i;
		uint32_t sub = cmap + be32(r + 4);
		if (sub + 4 > f->len)
			continue;
		int fmt = be16(d + sub);
		if (fmt == 12 && !best12)
			best12 = sub;
		else if (fmt == 4 && !best4)
			best4 = sub;
	}
	if (best12) {
		uint32_t groups = be32(d + best12 + 12);
		for (uint32_t g = 0; g < groups; g++) {
			const uint8_t *r = d + best12 + 16 + 12 * g;
			if (r + 12 > d + f->len)
				break;
			uint32_t lo = be32(r), hi = be32(r + 4);
			if (code >= lo && code <= hi)
				return (int)(be32(r + 8) + (code - lo));
		}
		return 0;
	}
	if (best4 && code <= 0xFFFF) {
		int segs = be16(d + best4 + 6) / 2;
		const uint8_t *ends = d + best4 + 14, *starts = ends + 2 * segs + 2, *deltas = starts + 2 * segs,
			      *ranges = deltas + 2 * segs;
		for (int s = 0; s < segs; s++) {
			if (code > be16(ends + 2 * s))
				continue;
			uint16_t start = be16(starts + 2 * s);
			if (code < start)
				return 0;
			uint16_t delta = be16(deltas + 2 * s), range = be16(ranges + 2 * s);
			if (!range)
				return (uint16_t)(code + delta);
			const uint8_t *gp = ranges + 2 * s + range + 2 * (code - start);
			if (gp + 2 > d + f->len)
				return 0;
			uint16_t g = be16(gp);
			return g ? (uint16_t)(g + delta) : 0;
		}
	}
	return 0;
}

/* a color font's measures: its units to the em, ascent, and a glyph's
 * advance (from hmtx) */
static void color_font_measures(Font *f)
{
	uint32_t head = font_table(f, "head", NULL), hhea = font_table(f, "hhea", NULL), maxp = font_table(f, "maxp", NULL);
	f->units = head ? be16(f->data + head + 18) : 2048;
	f->ascent = hhea ? (int16_t)be16(f->data + hhea + 4) : f->units * 4 / 5;
	f->descent = hhea ? (int16_t)be16(f->data + hhea + 6) : -f->units / 5;
	f->nglyphs = maxp ? be16(f->data + maxp + 4) : 0;
	if (!f->units)
		f->units = 2048;
}

static int color_advance(const Font *f, int glyph)
{
	uint32_t hhea = font_table(f, "hhea", NULL), hmtx = font_table(f, "hmtx", NULL);
	if (!hhea || !hmtx)
		return f->units;
	int n = be16(f->data + hhea + 34);
	if (n <= 0)
		return f->units;
	int i = glyph < n ? glyph : n - 1;
	if (hmtx + 4 * (uint32_t)i + 2 > f->len)
		return f->units;
	return be16(f->data + hmtx + 4 * i);
}

/* A color glyph's picture: its PNG, and where it goes, in pixels of the
 * strike it's in (ppem: the strike's size). 0 if the font hasn't one. */
typedef struct ColorPicture {
	const uint8_t *png;
	uint32_t len;
	int ppem;
	int left, top;   /* from the pen on the baseline: its top left corner (top: above the baseline) */
	int bottom_up;   /* (sbix: top is the picture's bottom, counted up from the baseline) */
} ColorPicture;

/* (a strike's size: the smallest at least as big as wanted, or the biggest) */
static int pick_strike(const int *sizes, int n, int want)
{
	int best = -1;
	for (int i = 0; i < n; i++)
		if (sizes[i] >= want && (best < 0 || sizes[i] < sizes[best] || sizes[best] < want))
			best = i;
	if (best < 0)
		for (int i = 0; i < n; i++)
			if (best < 0 || sizes[i] > sizes[best])
				best = i;
	return best;
}

static int cbdt_picture(const Font *f, int glyph, int want, ColorPicture *out)
{
	uint32_t cblc_len, cbdt_len;
	uint32_t cblc = font_table(f, "CBLC", &cblc_len), cbdt = font_table(f, "CBDT", &cbdt_len);
	if (!cblc || !cbdt)
		return 0;
	const uint8_t *d = f->data;
	int nsizes = (int)be32(d + cblc + 4);
	if (nsizes <= 0 || nsizes > 64)
		return 0;
	int sizes[64];
	for (int i = 0; i < nsizes; i++)
		sizes[i] = d[cblc + 8 + 48 * i + 45];   /* ppemY */
	int s = pick_strike(sizes, nsizes, want);
	const uint8_t *bs = d + cblc + 8 + 48 * s;
	uint32_t array = cblc + be32(bs);
	uint32_t nsub = be32(bs + 8);
	for (uint32_t k = 0; k < nsub; k++) {
		const uint8_t *e = d + array + 8 * k;
		int first = be16(e), last = be16(e + 2);
		if (glyph < first || glyph > last)
			continue;
		uint32_t sub = array + be32(e + 4);
		int ifmt = be16(d + sub), imgfmt = be16(d + sub + 2);
		uint32_t data_at = cbdt + be32(d + sub + 4);
		uint32_t off = 0, size = 0;
		const uint8_t *big = NULL;   /* (bigGlyphMetrics, for image format 19) */
		if (ifmt == 1) {
			off = be32(d + sub + 8 + 4 * (glyph - first));
			size = be32(d + sub + 8 + 4 * (glyph - first + 1)) - off;
		} else if (ifmt == 3) {
			off = be16(d + sub + 8 + 2 * (glyph - first));
			size = be16(d + sub + 8 + 2 * (glyph - first + 1)) - off;
		} else if (ifmt == 2) {
			size = be32(d + sub + 8);
			big = d + sub + 12;
			off = size * (uint32_t)(glyph - first);
		} else if (ifmt == 4 || ifmt == 5) {
			/* (glyphs listed one by one) */
			uint32_t n = ifmt == 4 ? be32(d + sub + 8) : be32(d + sub + 20);
			if (ifmt == 4) {
				for (uint32_t j = 0; j < n; j++) {
					const uint8_t *p = d + sub + 12 + 4 * j;
					if (be16(p) == glyph) {
						off = be16(p + 2);
						size = be16(p + 6) - off;
						break;
					}
				}
			} else {
				size = be32(d + sub + 8);
				big = d + sub + 12;
				for (uint32_t j = 0; j < n; j++)
					if (be16(d + sub + 24 + 2 * j) == glyph) {
						off = size * j;
						break;
					}
			}
			if (!size)
				return 0;
		} else {
			return 0;
		}
		const uint8_t *g = d + data_at + off;
		if (data_at + off + size > f->len)
			return 0;
		out->ppem = sizes[s];
		out->bottom_up = 0;
		if (imgfmt == 17) {   /* smallGlyphMetrics: height, width, bearingX, bearingY, advance */
			out->left = (int8_t)g[2];
			out->top = (int8_t)g[3];
			out->len = be32(g + 5);
			out->png = g + 9;
		} else if (imgfmt == 18) {   /* bigGlyphMetrics: height, width, horiBearingX, horiBearingY, ... */
			out->left = (int8_t)g[2];
			out->top = (int8_t)g[3];
			out->len = be32(g + 8);
			out->png = g + 12;
		} else if (imgfmt == 19 && big) {
			out->left = (int8_t)big[2];
			out->top = (int8_t)big[3];
			out->len = be32(g);
			out->png = g + 4;
		} else {
			return 0;
		}
		return out->png + out->len <= d + f->len;
	}
	return 0;
}

static int sbix_picture(const Font *f, int glyph, int want, ColorPicture *out)
{
	uint32_t len, sbix = font_table(f, "sbix", &len);
	if (!sbix || glyph >= f->nglyphs)
		return 0;
	const uint8_t *d = f->data;
	int n = (int)be32(d + sbix + 4);
	if (n <= 0 || n > 64)
		return 0;
	int sizes[64];
	for (int i = 0; i < n; i++)
		sizes[i] = be16(d + sbix + be32(d + sbix + 8 + 4 * i));
	int s = pick_strike(sizes, n, want);
	uint32_t strike = sbix + be32(d + sbix + 8 + 4 * s);
	for (int hops = 0; hops < 4; hops++) {   /* ('dupe': the same picture as another glyph's) */
		uint32_t a = be32(d + strike + 4 + 4 * glyph), b = be32(d + strike + 4 + 4 * (glyph + 1));
		if (b <= a + 8 || strike + b > f->len)
			return 0;
		const uint8_t *g = d + strike + a;
		if (memcmp(g + 4, "dupe", 4) == 0) {
			glyph = be16(g + 8);
			continue;
		}
		if (memcmp(g + 4, "png ", 4) != 0)
			return 0;
		out->ppem = sizes[s];
		out->left = (int16_t)be16(g);
		out->top = (int16_t)be16(g + 2);
		out->bottom_up = 1;
		out->png = g + 8;
		out->len = b - a - 8;
		return 1;
	}
	return 0;
}

static int color_picture(const Font *f, int glyph, int want, ColorPicture *out)
{
	return f->color == 1 ? cbdt_picture(f, glyph, want, out) : f->color == 2 && sbix_picture(f, glyph, want, out);
}

/* ------------------------------------------------------------- the emoji font */

static int emoji_font = -2;   /* -2: not looked for yet; -1: none */

/* (mapped, not read: Apple Color Emoji is nearly 200 MB, of which a few
 * pictures are wanted; the system reads in only what's looked at) */
static unsigned char *map_file(const char *path, uint32_t *len)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	struct stat st;
	void *p = MAP_FAILED;
	if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size < 0x7fffffff)
		p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		return NULL;
	*len = (uint32_t)st.st_size;
	return p;
}

static int open_color_font(const char *path)
{
	Font f = { 0 };
	f.data = map_file(path, &f.len);
	if (!f.data)
		return -1;
	f.offset = (uint32_t)stbtt_GetFontOffsetForIndex(f.data, 0);
	if (font_table(&f, "CBDT", NULL) && font_table(&f, "CBLC", NULL))
		f.color = 1;
	else if (font_table(&f, "sbix", NULL))
		f.color = 2;
	if (!f.color) {
		munmap(f.data, f.len);
		return -1;
	}
	color_font_measures(&f);
	char key[1100];
	snprintf(key, sizeof key, "@emoji %s", path);
	f.key = strdup(key);
	fonts = realloc(fonts, sizeof *fonts * (size_t)(nfonts + 1));
	fonts[nfonts] = f;
	return nfonts++;
}

static int find_emoji_font(void)
{
	if (emoji_font != -2)
		return emoji_font;
	emoji_font = -1;
	const char *chosen = getenv("CABBY_EMOJI_FONT");
	if (chosen && *chosen) {
		if (strcmp(chosen, "none") != 0)
			emoji_font = open_color_font(chosen);
		return emoji_font;
	}
	char path[1024] = "";
	FILE *p = popen("fc-match -f '%{file}' 'emoji:color=true' 2>/dev/null", "r");
	if (p) {
		if (!fgets(path, sizeof path, p))
			path[0] = '\0';
		pclose(p);
	}
	if (path[0])
		emoji_font = open_color_font(path);
	static const char *const places[] = {
		"/System/Library/Fonts/Apple Color Emoji.ttc", "/usr/share/fonts/noto/NotoColorEmoji.ttf",
		"/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", "/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf",
		"/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf", "/usr/share/fonts/TTF/NotoColorEmoji.ttf", NULL };
	for (int i = 0; emoji_font < 0 && places[i]; i++)
		if (file_exists(places[i]))
			emoji_font = open_color_font(places[i]);
	return emoji_font;
}

/* ------------------------------------------------------------- HarfBuzz, loaded when first needed */

typedef struct {
	uint32_t codepoint, mask, cluster, var1, var2;
} HbInfo;
typedef struct {
	int32_t x_advance, y_advance, x_offset, y_offset;
	uint32_t var;
} HbPos;

static struct {
	int tried, ready;
	void *(*blob_create)(const char *data, unsigned len, int mode, void *user, void *destroy);
	void *(*face_create)(void *blob, unsigned index);
	void *(*font_create)(void *face);
	void (*font_set_scale)(void *font, int x, int y);
	void *(*buffer_create)(void);
	void (*buffer_destroy)(void *buf);
	void (*buffer_clear_contents)(void *buf);
	void (*buffer_add_utf8)(void *buf, const char *text, int len, unsigned offset, int length);
	void (*buffer_set_direction)(void *buf, int dir);
	void (*buffer_guess_segment_properties)(void *buf);
	void (*shape)(void *font, void *buf, const void *features, unsigned n);
	HbInfo *(*buffer_get_glyph_infos)(void *buf, unsigned *n);
	HbPos *(*buffer_get_glyph_positions)(void *buf, unsigned *n);
	void *buffer;
} hb;

static int hb_load(void)
{
	if (hb.tried)
		return hb.ready;
	hb.tried = 1;
	const char *want = getenv("CABBY_HARFBUZZ");
	if (want && strcmp(want, "none") == 0)
		return 0;
	static const char *const names[] = { "libharfbuzz.so.0", "libharfbuzz.so", "/opt/homebrew/lib/libharfbuzz.0.dylib",
					     "/usr/local/lib/libharfbuzz.0.dylib", "libharfbuzz.0.dylib", NULL };
	void *lib = NULL;
	for (int i = 0; !lib && names[i]; i++)
		lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!lib)
		return 0;
#define HB(field, symbol) \
	if (!(*(void **)&hb.field = dlsym(lib, symbol))) \
		return 0;
	HB(blob_create, "hb_blob_create")
	HB(face_create, "hb_face_create")
	HB(font_create, "hb_font_create")
	HB(font_set_scale, "hb_font_set_scale")
	HB(buffer_create, "hb_buffer_create")
	HB(buffer_destroy, "hb_buffer_destroy")
	HB(buffer_clear_contents, "hb_buffer_clear_contents")
	HB(buffer_add_utf8, "hb_buffer_add_utf8")
	HB(buffer_set_direction, "hb_buffer_set_direction")
	HB(buffer_guess_segment_properties, "hb_buffer_guess_segment_properties")
	HB(shape, "hb_shape")
	HB(buffer_get_glyph_infos, "hb_buffer_get_glyph_infos")
	HB(buffer_get_glyph_positions, "hb_buffer_get_glyph_positions")
#undef HB
	hb.buffer = hb.buffer_create();
	hb.ready = hb.buffer != NULL;
	return hb.ready;
}

/* the font, as HarfBuzz has it (made the first time), at its own units */
static void *hb_font_of(int font)
{
	Font *f = &fonts[font];
	if (!f->hb) {
		int index = 0;   /* (the first font in a collection, as stb_truetype has it) */
		void *blob = hb.blob_create((const char *)f->data, f->len, 1 /* read only */, NULL, NULL);
		void *face = hb.face_create(blob, (unsigned)index);
		f->hb = hb.font_create(face);
		int units = f->color ? f->units : (int)(1.0f / stbtt_ScaleForMappingEmToPixels(&f->info, 1.0f) + 0.5f);
		hb.font_set_scale(f->hb, units, units);
	}
	return f->hb;
}

/* ------------------------------------------------------------- directions */

enum { B_L, B_R, B_AL, B_EN, B_AN, B_ES, B_ET, B_CS, B_NSM, B_WS, B_ON };

static int bidi_class(uint32_t c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
		return B_L;
	if (c >= '0' && c <= '9')
		return B_EN;
	if (c == ' ' || c == '\t' || c == 0x3000 || (c >= 0x2000 && c <= 0x200A))
		return B_WS;
	if (c == '+' || c == '-' || c == 0x2212)
		return B_ES;
	if (c == '#' || c == '$' || c == '%' || c == 0xB0 || c == 0xB1 || (c >= 0xA2 && c <= 0xA5) || c == 0x20AC)
		return B_ET;
	if (c == ',' || c == '.' || c == '/' || c == ':' || c == 0xA0)
		return B_CS;
	if (c < 0x80)
		return B_ON;
	if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 ||
	    c == 0x05C4 || c == 0x05C5 || c == 0x05C7 || (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) ||
	    c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 || c == 0x06E8 ||
	    (c >= 0x06EA && c <= 0x06ED) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x200C || c == 0x200D ||
	    (c >= 0xE0020 && c <= 0xE007F) || c == 0x20E3)
		return B_NSM;
	if ((c >= 0x0660 && c <= 0x0669) || c == 0x066B || c == 0x066C)
		return B_AN;
	if (c >= 0x06F0 && c <= 0x06F9)
		return B_EN;
	if ((c >= 0x0590 && c <= 0x05FF) || (c >= 0x07C0 && c <= 0x085F) || (c >= 0xFB1D && c <= 0xFB4F) ||
	    (c >= 0x10800 && c <= 0x10FFF) || c == 0x200F)
		return B_R;
	if ((c >= 0x0600 && c <= 0x07BF) || (c >= 0x0860 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) ||
	    (c >= 0xFE70 && c <= 0xFEFF))
		return B_AL;
	if (c == 0x200E)
		return B_L;
	if ((c >= 0x00A1 && c <= 0x00BF) || c == 0xD7 || c == 0xF7 || (c >= 0x2010 && c <= 0x2BFF) ||
	    (c >= 0x3001 && c <= 0x3003) || (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0xFE10 && c <= 0xFE6F))
		return B_ON;
	return B_L;
}

/* each character's level: even left to right, odd right to left (the
 * paragraph's own is *base) */
static void bidi_levels(const uint32_t *cs, int n, uint8_t *lv, int *base)
{
	uint8_t *t = malloc((size_t)n + 1);
	int b = 0;
	for (int i = 0; i < n; i++) {
		int k = bidi_class(cs[i]);
		if (k == B_L || k == B_R || k == B_AL) {
			b = k == B_L ? 0 : 1;
			break;
		}
	}
	*base = b;
	for (int i = 0; i < n; i++)
		t[i] = (uint8_t)bidi_class(cs[i]);
	/* W1: a mark takes what's before it */
	for (int i = 0; i < n; i++)
		if (t[i] == B_NSM)
			t[i] = i ? t[i - 1] : (uint8_t)(b ? B_R : B_L);
	/* W2, W3: a number after Arabic is Arabic's; Arabic letters are R */
	int strong = b ? B_R : B_L;
	for (int i = 0; i < n; i++) {
		if (t[i] == B_L || t[i] == B_R || t[i] == B_AL)
			strong = t[i];
		if (t[i] == B_EN && strong == B_AL)
			t[i] = B_AN;
	}
	for (int i = 0; i < n; i++)
		if (t[i] == B_AL)
			t[i] = B_R;
	/* W4, W5, W6: a sign between numbers, and currency beside them, are the
	 * number's; any left are other neutrals */
	for (int i = 1; i + 1 < n; i++) {
		if (t[i] == B_ES && t[i - 1] == B_EN && t[i + 1] == B_EN)
			t[i] = B_EN;
		else if (t[i] == B_CS && (t[i - 1] == B_EN || t[i - 1] == B_AN) && t[i + 1] == t[i - 1])
			t[i] = t[i - 1];
	}
	for (int i = 0; i < n; i++)
		if (t[i] == B_ET) {
			int j = i;
			while (j < n && t[j] == B_ET)
				j++;
			int en = (i > 0 && t[i - 1] == B_EN) || (j < n && t[j] == B_EN);
			for (int k = i; k < j; k++)
				t[k] = en ? B_EN : B_ON;
			i = j - 1;
		}
	for (int i = 0; i < n; i++)
		if (t[i] == B_ES || t[i] == B_ET || t[i] == B_CS)
			t[i] = B_ON;
	/* W7: a number after a left to right word is left to right */
	strong = b ? B_R : B_L;
	for (int i = 0; i < n; i++) {
		if (t[i] == B_L || t[i] == B_R)
			strong = t[i];
		if (t[i] == B_EN && strong == B_L)
			t[i] = B_L;
	}
	/* N1, N2: neutrals between two of the same direction are that; others
	 * are the paragraph's */
	for (int i = 0; i < n; i++) {
		if (t[i] != B_WS && t[i] != B_ON)
			continue;
		int j = i;
		while (j < n && (t[j] == B_WS || t[j] == B_ON))
			j++;
		int before = i ? (t[i - 1] == B_L ? 0 : 1) : b;
		int after = j < n ? (t[j] == B_L ? 0 : 1) : b;
		int d = before == after ? before : b;
		for (int k = i; k < j; k++)
			t[k] = (uint8_t)(d ? B_R : B_L);
		i = j - 1;
	}
	/* I1, I2: the levels */
	for (int i = 0; i < n; i++) {
		if (b == 0)
			lv[i] = t[i] == B_L ? 0 : t[i] == B_R ? 1 : 2;
		else
			lv[i] = t[i] == B_R ? 1 : 2;
	}
	/* L1: spaces at the end are the paragraph's */
	for (int i = n - 1; i >= 0 && bidi_class(cs[i]) == B_WS; i--)
		lv[i] = (uint8_t)b;
	free(t);
}

/* ------------------------------------------------------------- Arabic, joined by Cabby (without HarfBuzz) */

/* the joined forms of U+0621 to U+064A: the first of them (isolated), and
 * how many there are (1: it doesn't join; 2: it joins to the letter
 * before only; 4: on both sides) */
static const struct { uint16_t first; uint8_t forms; } arabic_forms[] = {
	{ 0xFE80, 1 }, { 0xFE81, 2 }, { 0xFE83, 2 }, { 0xFE85, 2 }, { 0xFE87, 2 }, { 0xFE89, 4 }, { 0xFE8D, 2 },
	{ 0xFE8F, 4 }, { 0xFE93, 2 }, { 0xFE95, 4 }, { 0xFE99, 4 }, { 0xFE9D, 4 }, { 0xFEA1, 4 }, { 0xFEA5, 4 },
	{ 0xFEA9, 2 }, { 0xFEAB, 2 }, { 0xFEAD, 2 }, { 0xFEAF, 2 }, { 0xFEB1, 4 }, { 0xFEB5, 4 }, { 0xFEB9, 4 },
	{ 0xFEBD, 4 }, { 0xFEC1, 4 }, { 0xFEC5, 4 }, { 0xFEC9, 4 }, { 0xFECD, 4 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 4 } /* (0640, the joining line: joins, keeps its shape) */,
	{ 0xFED1, 4 }, { 0xFED5, 4 }, { 0xFED9, 4 }, { 0xFEDD, 4 }, { 0xFEE1, 4 }, { 0xFEE5, 4 }, { 0xFEE9, 4 },
	{ 0xFEED, 2 }, { 0xFEEF, 2 }, { 0xFEF1, 4 },
};

static int arabic_joins(uint32_t c) { return c >= 0x0621 && c <= 0x064A ? arabic_forms[c - 0x0621].forms : 0; }
static int arabic_mark(uint32_t c) { return (c >= 0x064B && c <= 0x065F) || c == 0x0670; }

/* the characters to draw for Arabic, in place: each letter its joined
 * form, and lam with alef as one (the alef then 0, left out) */
static void arabic_join(uint32_t *cs, int n)
{
	for (int i = 0; i < n; i++) {
		uint32_t c = cs[i];
		int forms = arabic_joins(c);
		if (!forms)
			continue;
		int before = 0, after = 0;
		for (int j = i - 1; j >= 0; j--) {
			if (arabic_mark(cs[j]))
				continue;
			before = arabic_joins(cs[j]) == 4;
			break;
		}
		int k = i + 1;
		while (k < n && arabic_mark(cs[k]))
			k++;
		after = k < n && arabic_joins(cs[k]) >= 2 && forms == 4;
		if (!(forms > 1))
			before = 0;
		/* lam and an alef after it: one glyph */
		if (c == 0x0644 && k < n && (cs[k] == 0x0622 || cs[k] == 0x0623 || cs[k] == 0x0625 || cs[k] == 0x0627)) {
			uint32_t base = cs[k] == 0x0622 ? 0xFEF5 : cs[k] == 0x0623 ? 0xFEF7 : cs[k] == 0x0625 ? 0xFEF9 : 0xFEFB;
			cs[i] = base + (before ? 1 : 0);
			cs[k] = 0;
			continue;
		}
		uint16_t first = arabic_forms[c - 0x0621].first;
		if (!first)
			continue;   /* (the joining line) */
		int form = 0;   /* isolated */
		if (before && after)
			form = 3;   /* medial */
		else if (after)
			form = 2;   /* initial */
		else if (before)
			form = 1;   /* final */
		if (form >= forms)
			form = before ? 1 : 0;
		cs[i] = first + (uint32_t)form;
	}
}

/* brackets face the other way, right to left */
static uint32_t mirrored(uint32_t c)
{
	switch (c) {
	case '(': return ')';
	case ')': return '(';
	case '[': return ']';
	case ']': return '[';
	case '{': return '}';
	case '}': return '{';
	case '<': return '>';
	case '>': return '<';
	case 0xAB: return 0xBB;
	case 0xBB: return 0xAB;
	}
	return c;
}

/* ------------------------------------------------------------- emoji */

static int is_pictograph(uint32_t c)
{
	return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x1F1E6 && c <= 0x1F1FF);
}

/* a character that's an emoji when a variation selector 16 follows it */
static int may_be_emoji(uint32_t c)
{
	return (c >= 0x2600 && c <= 0x27BF) || (c >= 0x2300 && c <= 0x23FF) || (c >= 0x2B00 && c <= 0x2BFF) ||
	       c == 0xA9 || c == 0xAE || c == 0x203C || c == 0x2049 || c == 0x2122 || c == 0x2139 ||
	       (c >= 0x2194 && c <= 0x21AA) || c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299 ||
	       (c >= '0' && c <= '9') || c == '#' || c == '*';
}

static int emoji_part(uint32_t c)
{
	return c == 0x200D || c == 0xFE0F || c == 0xFE0E || (c >= 0x1F3FB && c <= 0x1F3FF) || c == 0x20E3 ||
	       (c >= 0xE0020 && c <= 0xE007F);
}

/* ------------------------------------------------------------- laying text out */

typedef struct Placed {
	int font;
	int glyph;
	float x, y;       /* from the start of the line, on the baseline (y down), in our pixels */
	float advance;
	int cluster;      /* the byte it came from */
	int rtl;          /* in a right to left run */
} Placed;

typedef struct Layout {
	Placed *g;
	int n;
	float width;
	int base;         /* the paragraph's direction (1: right to left) */
	int len;          /* the text's length, in bytes */
} Layout;

/* whether the text needs laying out: anything past Cyrillic, or an emoji */
static int needs_layout(const char *text, int n)
{
	const unsigned char *p = (const unsigned char *)text, *end = n < 0 ? NULL : p + n;
	while (*p && (!end || p < end)) {
		if (*p < 0xD6) {   /* (below U+0590) */
			p++;
			continue;
		}
		const unsigned char *q = p;
		uint32_t c = next_char(&q);
		if (c >= 0x0590 && c != 0x2018 && c != 0x2019 && c != 0x201C && c != 0x201D && c != 0x2013 && c != 0x2014 &&
		    c != 0x2026 && c != 0x20AC && c != 0x2022)
			return 1;
		p = q;
	}
	return 0;
}

static void layout_place(Layout *L, int font, int glyph, float x, float y, float adv, int cluster, int rtl)
{
	L->g = realloc(L->g, sizeof *L->g * (size_t)(L->n + 1));
	L->g[L->n++] = (Placed){ font, glyph, x, y, adv, cluster, rtl };
}

/* a run of characters, all in one font and direction, onto the layout at
 * pen *x: shaped by HarfBuzz, or (without it) a character at a time */
static void shape_run(Layout *L, const char *text, int len, const uint32_t *cs, const int *at, int a, int b, int font,
		      int rtl, double size, float *x)
{
	Font *f = &fonts[font];
	float k = f->color ? (float)(size / f->units) : text_scale(font, size);
	if (hb_load() && f->data && f->len) {
		void *buf = hb.buffer;
		hb.buffer_clear_contents(buf);
		int end = b < 0 ? len : at[b];
		hb.buffer_add_utf8(buf, text, len, (unsigned)at[a], end - at[a]);
		hb.buffer_set_direction(buf, rtl ? 5 : 4);
		hb.buffer_guess_segment_properties(buf);
		hb.shape(hb_font_of(font), buf, NULL, 0);
		unsigned n;
		HbInfo *info = hb.buffer_get_glyph_infos(buf, &n);
		HbPos *pos = hb.buffer_get_glyph_positions(buf, &n);
		for (unsigned i = 0; i < n; i++) {
			layout_place(L, font, (int)info[i].codepoint, *x + pos[i].x_offset * k, -pos[i].y_offset * k,
			      pos[i].x_advance * k, (int)info[i].cluster, rtl);
			*x += pos[i].x_advance * k;
		}
		return;
	}
	/* without HarfBuzz: Arabic joined by Cabby, brackets mirrored, and the
	 * glyphs put right to left by putting them in backwards */
	int m = b - a;
	uint32_t *u = malloc(sizeof *u * (size_t)(m + 1));
	for (int i = 0; i < m; i++)
		u[i] = rtl ? mirrored(cs[a + i]) : cs[a + i];
	arabic_join(u, m);
	int first = L->n;
	int last_glyph = 0;
	for (int i = 0; i < m; i++) {
		if (!u[i] || (u[i] >= 0xFE00 && u[i] <= 0xFE0F) || u[i] == 0x200D || u[i] == 0x200C)
			continue;
		int g = f->color ? cmap_glyph(f, u[i]) : stbtt_FindGlyphIndex(&f->info, (int)u[i]);
		if (!g && !f->color && u[i] != cs[a + i])
			g = stbtt_FindGlyphIndex(&f->info, (int)cs[a + i]);   /* (the font lacks the joined form) */
		float adv;
		if (f->color)
			adv = color_advance(f, g) * k;
		else {
			int aw, lsb;
			stbtt_GetGlyphHMetrics(&f->info, g, &aw, &lsb);
			adv = aw * k;
			if (last_glyph && !rtl)
				*x += stbtt_GetGlyphKernAdvance(&f->info, last_glyph, g) * k;
		}
		/* a mark sits on the letter before it, rather than after it */
		if (arabic_mark(cs[a + i]) || (cs[a + i] >= 0x0591 && cs[a + i] <= 0x05C7 && bidi_class(cs[a + i]) == B_NSM)) {
			float back = L->n > first ? L->g[L->n - 1].advance : 0;
			layout_place(L, font, g, *x - back / 2 - adv / 2, 0, 0, at[a + i], rtl);
			continue;
		}
		layout_place(L, font, g, *x, 0, adv, at[a + i], rtl);
		*x += adv;
		last_glyph = g;
	}
	free(u);
	if (rtl) {
		/* (laid out left to right: turned round, in place) */
		float start = L->n > first ? L->g[first].x : *x, w = *x - start;
		for (int i = first; i < L->n; i++)
			L->g[i].x = start + w - (L->g[i].x - start) - L->g[i].advance;
		for (int i = first, j = L->n - 1; i < j; i++, j--) {
			Placed t = L->g[i];
			L->g[i] = L->g[j];
			L->g[j] = t;
		}
	}
}

/* the font a character's drawn with, here: the emoji font for an emoji,
 * if there is one */
static int font_here(int font, const uint32_t *cs, int i, int n)
{
	uint32_t c = cs[i];
	int emoji = is_pictograph(c) || (may_be_emoji(c) && i + 1 < n && (cs[i + 1] == 0xFE0F || cs[i + 1] == 0x20E3));
	if (emoji && find_emoji_font() >= 0)
		return emoji_font;
	return font_with(font, c);
}

/* CABBY_TEXT_ORDER=file: each text laid out, and its characters in the
 * order they're shown, left to right, onto the end of the file (for the
 * tests: the order is the same whatever the fonts) */
static void tell_order(const Layout *L, const char *text, int len)
{
	const char *path = getenv("CABBY_TEXT_ORDER");
	if (!path || !*path)
		return;
	FILE *out = fopen(path, "a");
	if (!out)
		return;
	fprintf(out, "%.*s => ", len, text);
	int last = -1;
	for (int i = 0; i < L->n; i++) {
		int c = L->g[i].cluster;
		if (c == last)
			continue;
		last = c;
		int end = len;   /* (the cluster's bytes: to the next cluster after it in the text) */
		for (int j = 0; j < L->n; j++)
			if (L->g[j].cluster > c && L->g[j].cluster < end)
				end = L->g[j].cluster;
		fprintf(out, "%.*s", end - c, text + c);
	}
	fprintf(out, "\n");
	fclose(out);
}

static void layout_text(int font, double size, const char *text, int len, Layout *L)
{
	memset(L, 0, sizeof *L);
	L->len = len;
	int n = 0;
	uint32_t *cs = malloc(sizeof *cs * (size_t)(len + 1));
	int *at = malloc(sizeof *at * (size_t)(len + 1));
	const unsigned char *p = (const unsigned char *)text, *end = p + len;
	while (p < end && *p) {
		at[n] = (int)(p - (const unsigned char *)text);
		cs[n++] = next_char(&p);
	}
	at[n] = (int)(p - (const unsigned char *)text);
	uint8_t *lv = malloc((size_t)n + 1);
	bidi_levels(cs, n, lv, &L->base);
	/* each character's font: an emoji's parts go with it, as a mark goes
	 * with its letter */
	int *fo = malloc(sizeof *fo * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		if (i && (emoji_part(cs[i]) || (cs[i - 1] == 0x200D && fo[i - 1] == emoji_font && emoji_font >= 0) ||
			  (bidi_class(cs[i]) == B_NSM && cs[i] < 0xFE00)))
			fo[i] = fo[i - 1];
		else
			fo[i] = font_here(font, cs, i, n);
		/* (an emoji's parts are left to right with it) */
		if (i && fo[i] == emoji_font && emoji_font >= 0 && fo[i - 1] == emoji_font)
			lv[i] = lv[i - 1];
	}
	/* runs: a font and a level each, in the text's order */
	int nruns = 0;
	int *ra = malloc(sizeof *ra * (size_t)(n + 1)), *rb = malloc(sizeof *rb * (size_t)(n + 1));
	for (int i = 0; i < n;) {
		int j = i + 1;
		while (j < n && fo[j] == fo[i] && lv[j] == lv[i])
			j++;
		ra[nruns] = i;
		rb[nruns++] = j;
		i = j;
	}
	/* L2: the runs in the order they're seen: from the highest level down
	 * to the lowest odd one, each stretch at that level or more turned
	 * round */
	int *order = malloc(sizeof *order * (size_t)(nruns + 1));
	for (int r = 0; r < nruns; r++)
		order[r] = r;
	int high = 0, low_odd = 99;
	for (int r = 0; r < nruns; r++) {
		int l = lv[ra[r]];
		if (l > high)
			high = l;
		if ((l & 1) && l < low_odd)
			low_odd = l;
	}
	for (int l = high; l >= low_odd && l > 0; l--)
		for (int r = 0; r < nruns;) {
			if (lv[ra[order[r]]] < l) {
				r++;
				continue;
			}
			int s = r;
			while (r < nruns && lv[ra[order[r]]] >= l)
				r++;
			for (int x = s, y = r - 1; x < y; x++, y--) {
				int t = order[x];
				order[x] = order[y];
				order[y] = t;
			}
		}
	float x = 0;
	for (int r = 0; r < nruns; r++) {
		int i = order[r];
		shape_run(L, text, len, cs, at, ra[i], rb[i], fo[ra[i]], lv[ra[i]] & 1, size, &x);
	}
	L->width = x;
	tell_order(L, text, len);
	free(cs);
	free(at);
	free(lv);
	free(fo);
	free(ra);
	free(rb);
	free(order);
}

/* Layouts, kept: a window draws the same text every frame */
#define LAYOUTS 256
static struct {
	char *text;
	int len, font;
	double size;
	uint64_t used;
	Layout L;
} layouts[LAYOUTS];
static uint64_t layout_clock;

static const Layout *laid_out(int font, double size, const char *text, int n)
{
	int len = n < 0 ? (int)strlen(text) : n;
	int oldest = 0;
	for (int i = 0; i < LAYOUTS; i++) {
		if (layouts[i].text && layouts[i].len == len && layouts[i].font == font && layouts[i].size == size &&
		    memcmp(layouts[i].text, text, (size_t)len) == 0) {
			layouts[i].used = ++layout_clock;
			return &layouts[i].L;
		}
		if (layouts[i].used < layouts[oldest].used)
			oldest = i;
	}
	free(layouts[oldest].text);
	free(layouts[oldest].L.g);
	layouts[oldest].text = malloc((size_t)len + 1);
	memcpy(layouts[oldest].text, text, (size_t)len);
	layouts[oldest].text[len] = 0;
	layouts[oldest].len = len;
	layouts[oldest].font = font;
	layouts[oldest].size = size;
	layouts[oldest].used = ++layout_clock;
	layout_text(font, size, layouts[oldest].text, len, &layouts[oldest].L);
	return &layouts[oldest].L;
}

/* where the text cursor goes, before byte `at` of the text: x from the
 * text's left edge */
static double layout_caret(const Layout *L, int at)
{
	if (!L->n)
		return 0;
	int best = -1;
	for (int i = 0; i < L->n; i++) {
		if (L->g[i].cluster == at)
			return L->g[i].rtl ? L->g[i].x + L->g[i].advance : L->g[i].x;
		if (L->g[i].cluster < at && (best < 0 || L->g[i].cluster > L->g[best].cluster))
			best = i;
	}
	if (best < 0)
		return L->base ? L->width : 0;
	/* after the character before it */
	const Placed *g = &L->g[best];
	return g->rtl ? g->x : g->x + g->advance;
}

/* ------------------------------------------------------------- drawing a color glyph */

typedef struct ColorGlyph {
	SDL_Renderer *owner;
	int font, glyph, pixels;
	SDL_Texture *texture;
	float x, y, w, h;   /* where its picture goes, from the pen on the baseline, in the screen's pixels */
	uint64_t used;
} ColorGlyph;

#define COLOR_GLYPHS 512
static ColorGlyph color_glyphs[COLOR_GLYPHS];
static uint64_t color_clock;

static ColorGlyph *color_glyph(int font, int glyph, int pixels)
{
	int oldest = 0;
	for (int i = 0; i < COLOR_GLYPHS; i++) {
		ColorGlyph *c = &color_glyphs[i];
		if (c->texture && c->owner == renderer && c->font == font && c->glyph == glyph && c->pixels == pixels) {
			c->used = ++color_clock;
			return c;
		}
		if (c->used < color_glyphs[oldest].used)
			oldest = i;
	}
	ColorGlyph *c = &color_glyphs[oldest];
	if (c->texture && c->owner == renderer)
		SDL_DestroyTexture(c->texture);
	memset(c, 0, sizeof *c);
	c->owner = renderer;
	c->font = font;
	c->glyph = glyph;
	c->pixels = pixels;
	c->used = ++color_clock;
	Font *f = &fonts[font];
	ColorPicture pic;
	if (!color_picture(f, glyph, pixels, &pic))
		return c;
	int w, h, comp;
	unsigned char *rgba = stbi_load_from_memory(pic.png, (int)pic.len, &w, &h, &comp, 4);
	if (!rgba)
		return c;
	float k = (float)pixels / (float)pic.ppem;
	c->x = pic.left * k;
	c->y = pic.bottom_up ? -(pic.top + h) * k : -pic.top * k;
	c->w = w * k;
	c->h = h * k;
	SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba, w * 4);
	if (s) {
		c->texture = SDL_CreateTextureFromSurface(renderer, s);
		if (c->texture)
			SDL_SetTextureScaleMode(c->texture, SDL_SCALEMODE_LINEAR);
		SDL_DestroySurface(s);
	}
	stbi_image_free(rgba);
	return c;
}

/* a layout drawn, its top left corner at x, y */
static void draw_layout(const Layout *L, int font, double size, double x, double y, SDL_FColor c)
{
	int pixels = (int)(size * density + 0.5);
	double baseline = y + text_ascent(font, size);
	for (int i = 0; i < L->n; i++) {
		const Placed *p = &L->g[i];
		Font *f = &fonts[p->font];
		double gx = x + p->x, gy = baseline + p->y;
		if (f->color) {
			ColorGlyph *cg = color_glyph(p->font, p->glyph, pixels);
			if (cg->texture) {
				SDL_SetTextureAlphaModFloat(cg->texture, c.a);
				SDL_FRect d = { (float)(gx + cg->x / density), (float)(gy + cg->y / density), cg->w / density,
						cg->h / density };
				SDL_RenderTexture(renderer, cg->texture, NULL, &d);
			}
			continue;
		}
		Glyph *gl = glyph(p->font, pixels, 0x80000000u | (uint32_t)p->glyph);
		if (gl && gl->texture) {
			SDL_SetTextureColorModFloat(gl->texture, c.r, c.g, c.b);
			SDL_SetTextureAlphaModFloat(gl->texture, c.a);
			SDL_FRect d = { (float)(floor((gx + gl->x / density) * density + 0.5) / density),
					(float)(floor((gy + gl->y / density) * density + 0.5) / density),
					(float)(gl->w / density), (float)(gl->h / density) };
			SDL_RenderTexture(renderer, gl->texture, NULL, &d);
		}
	}
}
