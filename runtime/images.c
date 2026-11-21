/* images.c — pictures in memory, for the images library: drawn on a pixel
 * at a time or in shapes, read from and saved to files, shown on a
 * window's canvas, and handed from a worker to the main program whole.
 *
 * A picture is its pixels, four bytes each (red, green, blue, and how
 * opaque: RGBA), a row after another from the top. It belongs to one
 * thread at a time (the Cabby object that holds it), so drawing takes no
 * lock: only the table of pictures has one, for making and dropping them
 * from several threads at once. A worker's picture, handed on with
 * tell_picture, moves to the main program as it is: no pixel is copied.
 *
 * Each picture counts its changes, so a window shows a picture it has
 * shown before without sending its pixels to the screen again unless
 * they've changed. */
#include "rt.h"
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static _Noreturn void fail(const char *fmt, const char *a) { rt_panic(NULL, 0, 0, fmt, a); }

#include "colors.h"
#include "game_font.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
/* (their own, kept to this file: the window libraries have theirs) */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef struct {
	int w, h;
	uint8_t *px;            /* w * h * 4 */
	uint8_t pen[4];         /* set_color's */
	uint64_t version;       /* counted up with each change */
	uint64_t serial;        /* never the same for two pictures, even one made after another's gone */
} Picture;

static pthread_mutex_t table = PTHREAD_MUTEX_INITIALIZER;
static Picture **pics;
static int64_t npics;

static Picture *pic(int64_t id)
{
	pthread_mutex_lock(&table);
	Picture *p = id >= 1 && id <= npics ? pics[id - 1] : NULL;
	pthread_mutex_unlock(&table);
	if (!p)
		rt_panic(NULL, 0, 0, "%s", id == 0 ? "that picture is empty: it was handed on (tell_picture), or never made"
						    : "that picture has been dropped");
	return p;
}

static uint64_t serials;

static int64_t add(Picture *p)
{
	pthread_mutex_lock(&table);
	p->serial = ++serials;
	int64_t i = 0;
	while (i < npics && pics[i])
		i++;
	if (i == npics) {
		pics = realloc(pics, sizeof *pics * (size_t)(npics + 1));
		npics++;
	}
	pics[i] = p;
	pthread_mutex_unlock(&table);
	return i + 1;
}

static Picture *make(int64_t w, int64_t h)
{
	if (w < 1 || h < 1 || w > 32768 || h > 32768) {
		char size[64];
		snprintf(size, sizeof size, "%lld by %lld", (long long)w, (long long)h);
		rt_panic(NULL, 0, 0, "a picture can't be %s pixels: from 1 to 32768 each way", size);
	}
	Picture *p = calloc(1, sizeof *p);
	p->w = (int)w;
	p->h = (int)h;
	p->px = calloc((size_t)w * (size_t)h, 4);   /* (transparent, to begin with) */
	if (!p->px)
		rt_panic(NULL, 0, 0, "out of memory for a picture of %s pixels", "that many");
	p->pen[3] = 255;   /* black */
	return p;
}

/* a new one, every pixel transparent */
int64_t cby_image_new(int64_t w, int64_t h) { return add(make(w, h)); }

int64_t cby_image_load(const char *path)
{
	int w, h, n;
	unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
	if (!rgba)
		rt_panic(NULL, 0, 0, "can't read the picture '%s': it isn't there, or isn't a picture (PNG, JPEG, BMP or GIF)", path);
	Picture *p = calloc(1, sizeof *p);
	p->w = w;
	p->h = h;
	p->px = malloc((size_t)w * (size_t)h * 4);
	memcpy(p->px, rgba, (size_t)w * (size_t)h * 4);
	stbi_image_free(rgba);
	p->pen[3] = 255;
	return add(p);
}

/* a picture from RGBA pixels (w * h * 4), copied: for other libraries'
 * C (a video's frame, from the videos library) */
int64_t cby_image_from_rgba(int64_t w, int64_t h, const uint8_t *px)
{
	Picture *p = make(w, h);
	memcpy(p->px, px, (size_t)w * (size_t)h * 4);
	return add(p);
}

static int ends_with(const char *s, const char *end)
{
	size_t a = strlen(s), b = strlen(end);
	return a >= b && strcasecmp(s + a - b, end) == 0;
}

/* as a PNG, or by its name a JPEG (.jpg, .jpeg) or a BMP */
void cby_image_save(int64_t id, const char *path)
{
	Picture *p = pic(id);
	int ok;
	if (ends_with(path, ".jpg") || ends_with(path, ".jpeg"))
		ok = stbi_write_jpg(path, p->w, p->h, 4, p->px, 92);
	else if (ends_with(path, ".bmp"))
		ok = stbi_write_bmp(path, p->w, p->h, 4, p->px);
	else
		ok = stbi_write_png(path, p->w, p->h, 4, p->px, p->w * 4);
	if (!ok)
		rt_panic(NULL, 0, 0, "can't save the picture as '%s': the folder isn't there, or can't be written to", path);
}

int64_t cby_image_copy(int64_t id)
{
	Picture *p = pic(id);
	Picture *c = make(p->w, p->h);
	memcpy(c->px, p->px, (size_t)p->w * (size_t)p->h * 4);
	memcpy(c->pen, p->pen, 4);
	return add(c);
}

void cby_image_free(int64_t id)
{
	pthread_mutex_lock(&table);
	Picture *p = id >= 1 && id <= npics ? pics[id - 1] : NULL;
	if (p)
		pics[id - 1] = NULL;
	pthread_mutex_unlock(&table);
	if (p) {
		free(p->px);
		free(p);
	}
}

int64_t cby_image_width(int64_t id) { return pic(id)->w; }
int64_t cby_image_height(int64_t id) { return pic(id)->h; }

/* (the window libraries': its pixels, size, which picture it is, and how
 * many times it's changed; NULL if it's gone) */
const uint8_t *cby_image_pixels(int64_t id, int *w, int *h, uint64_t *serial, uint64_t *version)
{
	pthread_mutex_lock(&table);
	Picture *p = id >= 1 && id <= npics ? pics[id - 1] : NULL;
	pthread_mutex_unlock(&table);
	if (!p)
		return NULL;
	*w = p->w;
	*h = p->h;
	*serial = p->serial;
	*version = p->version;
	return p->px;
}

/* ------------------------------------------------------------- drawing */

static void parse(const char *spec, uint8_t out[4])
{
	double a;
	uint32_t c = color_of(spec, &a);
	out[0] = (uint8_t)(c >> 16);
	out[1] = (uint8_t)(c >> 8);
	out[2] = (uint8_t)c;
	out[3] = (uint8_t)(a * 255 + 0.5);
}

void cby_image_set_color(int64_t id, const char *spec) { parse(spec, pic(id)->pen); }

static inline void put(Picture *p, int64_t x, int64_t y, const uint8_t c[4])
{
	if (x >= 0 && y >= 0 && x < p->w && y < p->h)
		memcpy(p->px + ((size_t)y * (size_t)p->w + (size_t)x) * 4, c, 4);
}

static void span(Picture *p, int64_t x0, int64_t x1, int64_t y, const uint8_t c[4])
{
	if (y < 0 || y >= p->h)
		return;
	if (x0 < 0)
		x0 = 0;
	if (x1 > p->w - 1)
		x1 = p->w - 1;
	uint8_t *row = p->px + (size_t)y * (size_t)p->w * 4;
	for (int64_t x = x0; x <= x1; x++)
		memcpy(row + x * 4, c, 4);
}

void cby_image_fill_with(int64_t id, const char *spec)
{
	Picture *p = pic(id);
	uint8_t c[4];
	parse(spec, c);
	size_t n = (size_t)p->w * (size_t)p->h;
	for (size_t i = 0; i < n; i++)
		memcpy(p->px + i * 4, c, 4);
	p->version++;
}

void cby_image_set_pixel(int64_t id, int64_t x, int64_t y)
{
	Picture *p = pic(id);
	put(p, x, y, p->pen);
	p->version++;
}

static uint8_t clamp(int64_t v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

void cby_image_set_pixel_rgb(int64_t id, int64_t x, int64_t y, int64_t r, int64_t g, int64_t b)
{
	Picture *p = pic(id);
	uint8_t c[4] = { clamp(r), clamp(g), clamp(b), 255 };
	put(p, x, y, c);
	p->version++;
}

/* 0xRRGGBBAA (0 outside it) */
int64_t cby_image_pixel(int64_t id, int64_t x, int64_t y)
{
	Picture *p = pic(id);
	if (x < 0 || y < 0 || x >= p->w || y >= p->h)
		return 0;
	const uint8_t *c = p->px + ((size_t)y * (size_t)p->w + (size_t)x) * 4;
	return (int64_t)((uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | c[3]);
}

void cby_image_fill_rectangle(int64_t id, int64_t x, int64_t y, int64_t w, int64_t h)
{
	Picture *p = pic(id);
	for (int64_t r = y; r < y + h; r++)
		span(p, x, x + w - 1, r, p->pen);
	p->version++;
}

void cby_image_rectangle(int64_t id, int64_t x, int64_t y, int64_t w, int64_t h)
{
	Picture *p = pic(id);
	if (w < 1 || h < 1)
		return;
	span(p, x, x + w - 1, y, p->pen);
	span(p, x, x + w - 1, y + h - 1, p->pen);
	for (int64_t r = y + 1; r < y + h - 1; r++) {
		put(p, x, r, p->pen);
		put(p, x + w - 1, r, p->pen);
	}
	p->version++;
}

/* a line a pixel wide, from one end to the other (Bresenham's) */
void cby_image_line(int64_t id, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
	Picture *p = pic(id);
	int64_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int64_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	int64_t err = dx + dy;
	for (;;) {
		put(p, x0, y0, p->pen);
		if (x0 == x1 && y0 == y1)
			break;
		int64_t e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
	p->version++;
}

void cby_image_fill_circle(int64_t id, int64_t cx, int64_t cy, int64_t r)
{
	Picture *p = pic(id);
	for (int64_t y = -r; y <= r; y++) {
		int64_t half = (int64_t)sqrt((double)(r * r - y * y) + 0.25);
		span(p, cx - half, cx + half, cy + y, p->pen);
	}
	p->version++;
}

/* its edge, a pixel wide (the midpoint way) */
void cby_image_circle(int64_t id, int64_t cx, int64_t cy, int64_t r)
{
	Picture *p = pic(id);
	int64_t x = r, y = 0, err = 1 - r;
	while (x >= y) {
		int64_t pts[8][2] = { { x, y }, { y, x }, { -y, x }, { -x, y }, { -x, -y }, { -y, -x }, { y, -x }, { x, -y } };
		for (int i = 0; i < 8; i++)
			put(p, cx + pts[i][0], cy + pts[i][1], p->pen);
		y++;
		if (err < 0)
			err += 2 * y + 1;
		else {
			x--;
			err += 2 * (y - x) + 1;
		}
	}
	p->version++;
}

/* another picture drawn on this one, its top left corner at x, y: what's
 * see-through in it lets this one show through */
void cby_image_draw(int64_t id, int64_t other, int64_t x, int64_t y)
{
	Picture *p = pic(id), *o = pic(other);
	for (int r = 0; r < o->h; r++) {
		int64_t ty = y + r;
		if (ty < 0 || ty >= p->h)
			continue;
		for (int c = 0; c < o->w; c++) {
			int64_t tx = x + c;
			if (tx < 0 || tx >= p->w)
				continue;
			const uint8_t *s = o->px + ((size_t)r * o->w + c) * 4;
			uint8_t *d = p->px + ((size_t)ty * p->w + tx) * 4;
			unsigned a = s[3];
			if (a == 255)
				memcpy(d, s, 4);
			else if (a) {
				for (int k = 0; k < 3; k++)
					d[k] = (uint8_t)((s[k] * a + d[k] * (255 - a)) / 255);
				d[3] = (uint8_t)(a + d[3] * (255 - a) / 255);
			}
		}
	}
	p->version++;
}


/* ------------------------------------------------------------ changed copies
 *
 * Each makes a new picture, and leaves the one it's made from as it was:
 * Photo = Photo.scaled 800, 600. Colors are worked with "premultiplied"
 * (each by how opaque it is), so a see-through edge doesn't darken. */

static int64_t made(Picture *p) { return add(p); }

/* one way (across, or down): n samples into m, each a weighted mix: a
 * box as wide as a pixel of the source's when shrinking (every pixel
 * counted, as it should be), a line between neighbours when growing */
static void resample(const float *in, int n, int in_stride, float *out, int m, int out_stride, int channels)
{
	double scale = (double)m / n;
	for (int j = 0; j < m; j++) {
		double acc[4] = { 0, 0, 0, 0 }, total = 0;
		if (scale < 1) {
			double from = j / scale, to = (j + 1) / scale;
			for (int i = (int)floor(from); i < (int)ceil(to) && i < n; i++) {
				double w = fmin(to, i + 1.0) - fmax(from, (double)i);
				if (w <= 0)
					continue;
				for (int c = 0; c < channels; c++)
					acc[c] += w * in[(size_t)i * in_stride + c];
				total += w;
			}
		} else {
			double at = (j + 0.5) / scale - 0.5;
			int i0 = (int)floor(at);
			double f = at - i0;
			int a = i0 < 0 ? 0 : i0 >= n ? n - 1 : i0, b = i0 + 1 >= n ? n - 1 : i0 + 1 < 0 ? 0 : i0 + 1;
			for (int c = 0; c < channels; c++)
				acc[c] = (1 - f) * in[(size_t)a * in_stride + c] + f * in[(size_t)b * in_stride + c];
			total = 1;
		}
		for (int c = 0; c < channels; c++)
			out[(size_t)j * out_stride + c] = (float)(total > 0 ? acc[c] / total : 0);
	}
}

static float *premultiplied(const Picture *p)
{
	float *f = malloc(sizeof *f * (size_t)p->w * (size_t)p->h * 4);
	for (size_t i = 0; i < (size_t)p->w * (size_t)p->h; i++) {
		float a = p->px[i * 4 + 3] / 255.0f;
		for (int c = 0; c < 3; c++)
			f[i * 4 + c] = p->px[i * 4 + c] * a;
		f[i * 4 + 3] = p->px[i * 4 + 3];
	}
	return f;
}

static void unpremultiply(const float *f, Picture *p)
{
	for (size_t i = 0; i < (size_t)p->w * (size_t)p->h; i++) {
		float a = f[i * 4 + 3];
		for (int c = 0; c < 3; c++)
			p->px[i * 4 + c] = a > 0.5f ? (uint8_t)fminf(255, fmaxf(0, f[i * 4 + c] * 255.0f / a + 0.5f)) : 0;
		p->px[i * 4 + 3] = (uint8_t)fminf(255, fmaxf(0, a + 0.5f));
	}
}

/* at a new size */
int64_t cby_image_scaled(int64_t id, int64_t w, int64_t h)
{
	Picture *p = pic(id);
	Picture *q = make(w, h);
	float *src = premultiplied(p);
	float *mid = malloc(sizeof *mid * (size_t)w * (size_t)p->h * 4);
	for (int y = 0; y < p->h; y++)
		resample(src + (size_t)y * p->w * 4, p->w, 4, mid + (size_t)y * w * 4, (int)w, 4, 4);
	float *out = malloc(sizeof *out * (size_t)w * (size_t)h * 4);
	for (int x = 0; x < w; x++)
		resample(mid + (size_t)x * 4, p->h, (int)w * 4, out + (size_t)x * 4, (int)h, (int)w * 4, 4);
	unpremultiply(out, q);
	free(src);
	free(mid);
	free(out);
	return made(q);
}

/* a part of it: the rectangle at x, y (what's of it inside the picture) */
int64_t cby_image_cropped(int64_t id, int64_t x, int64_t y, int64_t w, int64_t h)
{
	Picture *p = pic(id);
	int64_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int64_t x1 = x + w > p->w ? p->w : x + w, y1 = y + h > p->h ? p->h : y + h;
	if (x1 <= x0 || y1 <= y0) {
		char what[96];
		snprintf(what, sizeof what, "%lld, %lld (%lld by %lld)", (long long)x, (long long)y, (long long)w, (long long)h);
		rt_panic(NULL, 0, 0, "there's nothing of the picture at %s to cut out", what);
	}
	Picture *q = make(x1 - x0, y1 - y0);
	for (int64_t r = y0; r < y1; r++)
		memcpy(q->px + (size_t)(r - y0) * q->w * 4, p->px + ((size_t)r * p->w + (size_t)x0) * 4, (size_t)q->w * 4);
	return made(q);
}

/* turned clockwise by so many degrees: a quarter turn exactly; any other
 * angle on a picture big enough to hold it, what's outside see-through */
int64_t cby_image_turned(int64_t id, double degrees)
{
	Picture *p = pic(id);
	double d = fmod(degrees, 360);
	if (d < 0)
		d += 360;
	int quarter = fabs(d - round(d / 90) * 90) < 1e-9 ? ((int)round(d / 90)) % 4 : -1;
	if (quarter >= 0) {
		int64_t w = quarter % 2 ? p->h : p->w, h = quarter % 2 ? p->w : p->h;
		Picture *q = make(w, h);
		for (int y = 0; y < p->h; y++)
			for (int x = 0; x < p->w; x++) {
				int64_t nx = x, ny = y;
				if (quarter == 1) {
					nx = p->h - 1 - y;
					ny = x;
				} else if (quarter == 2) {
					nx = p->w - 1 - x;
					ny = p->h - 1 - y;
				} else if (quarter == 3) {
					nx = y;
					ny = p->w - 1 - x;
				}
				memcpy(q->px + ((size_t)ny * w + nx) * 4, p->px + ((size_t)y * p->w + x) * 4, 4);
			}
		return made(q);
	}
	double a = d * M_PI / 180, c = cos(a), s = sin(a);
	int64_t w = (int64_t)ceil(fabs(p->w * c) + fabs(p->h * s) - 1e-6), h = (int64_t)ceil(fabs(p->w * s) + fabs(p->h * c) - 1e-6);
	Picture *q = make(w, h);
	float *src = premultiplied(p);
	float *out = calloc((size_t)w * (size_t)h * 4, sizeof *out);
	double cx = p->w / 2.0, cy = p->h / 2.0, ox = w / 2.0, oy = h / 2.0;
	for (int64_t y = 0; y < h; y++)
		for (int64_t x = 0; x < w; x++) {
			/* (where it comes from: turned back) */
			double dx = x + 0.5 - ox, dy = y + 0.5 - oy;
			double sx = c * dx + s * dy + cx - 0.5, sy = -s * dx + c * dy + cy - 0.5;
			int x0 = (int)floor(sx), y0 = (int)floor(sy);
			double fx = sx - x0, fy = sy - y0;
			float *o = out + ((size_t)y * w + x) * 4;
			for (int k = 0; k < 4; k++) {
				int xx = x0 + (k & 1), yy = y0 + (k >> 1);
				if (xx < 0 || yy < 0 || xx >= p->w || yy >= p->h)
					continue;
				double wt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
				const float *sp = src + ((size_t)yy * p->w + xx) * 4;
				for (int ch = 0; ch < 4; ch++)
					o[ch] += (float)(wt * sp[ch]);
			}
		}
	unpremultiply(out, q);
	free(src);
	free(out);
	return made(q);
}

/* as in a mirror (left to right), or upside down */
int64_t cby_image_mirrored(int64_t id, int64_t upside_down)
{
	Picture *p = pic(id);
	Picture *q = make(p->w, p->h);
	for (int y = 0; y < p->h; y++)
		for (int x = 0; x < p->w; x++) {
			int nx = upside_down ? x : p->w - 1 - x, ny = upside_down ? p->h - 1 - y : y;
			memcpy(q->px + ((size_t)ny * p->w + nx) * 4, p->px + ((size_t)y * p->w + x) * 4, 4);
		}
	return made(q);
}

/* in grays: how light each color looks (Rec. 709's weights) */
int64_t cby_image_gray(int64_t id)
{
	Picture *p = pic(id);
	Picture *q = make(p->w, p->h);
	for (size_t i = 0; i < (size_t)p->w * (size_t)p->h; i++) {
		const uint8_t *c = p->px + i * 4;
		uint8_t g = (uint8_t)(0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2] + 0.5);
		q->px[i * 4] = q->px[i * 4 + 1] = q->px[i * 4 + 2] = g;
		q->px[i * 4 + 3] = c[3];
	}
	return made(q);
}

/* blurred, by so many pixels: three box blurs one after another, which
 * look as a Gaussian blur does */
static void box_line(const float *in, float *out, int n, int stride, int r)
{
	for (int i = 0; i < n; i++) {
		double acc[4] = { 0, 0, 0, 0 };
		int count = 0;
		for (int k = i - r; k <= i + r; k++) {
			int kk = k < 0 ? 0 : k >= n ? n - 1 : k;   /* (the edges carry on as they are) */
			for (int c = 0; c < 4; c++)
				acc[c] += in[(size_t)kk * stride + c];
			count++;
		}
		for (int c = 0; c < 4; c++)
			out[(size_t)i * stride + c] = (float)(acc[c] / count);
	}
}

int64_t cby_image_blurred(int64_t id, int64_t radius)
{
	Picture *p = pic(id);
	if (radius < 0)
		rt_panic(NULL, 0, 0, "%s", "a blur's radius is 0 or more pixels");
	Picture *q = make(p->w, p->h);
	float *a = premultiplied(p);
	float *b = malloc(sizeof *b * (size_t)p->w * (size_t)p->h * 4);
	int r = (int)(radius / 3 + (radius % 3 ? 1 : 0));
	for (int pass = 0; pass < 3 && radius > 0; pass++) {
		for (int y = 0; y < p->h; y++)
			box_line(a + (size_t)y * p->w * 4, b + (size_t)y * p->w * 4, p->w, 4, r);
		for (int x = 0; x < p->w; x++)
			box_line(b + (size_t)x * 4, a + (size_t)x * 4, p->h, p->w * 4, r);
	}
	unpremultiply(a, q);
	free(a);
	free(b);
	return made(q);
}

/* ------------------------------------------------------------ text
 *
 * In a font of the computer's (fontconfig's choice for a name, as the
 * desktop chooses: "Sans", "Serif Bold"), or a font's file (a .ttf, .otf
 * or .ttc); with no font to be found (or CABBY_IMAGES_FONT=builtin, for
 * the tests), a small one built in, its letters 8 by 16, made bigger. */

typedef struct {
	char *name;
	unsigned char *data;
	stbtt_fontinfo info;
} ImgFont;

static ImgFont *img_fonts;
static int nimg_fonts;
static _Thread_local int pen_font = -2;   /* (the font set_font chose, for this thread's pictures: -1 the built-in one, -2 none yet) */
static _Thread_local double pen_size = 16;

static unsigned char *whole(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *d = n > 0 ? malloc((size_t)n) : NULL;
	if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
		free(d);
		d = NULL;
	}
	fclose(f);
	return d;
}

static int path_is_font(const char *s)
{
	size_t n = strlen(s);
	return n > 4 && (strcasecmp(s + n - 4, ".ttf") == 0 || strcasecmp(s + n - 4, ".otf") == 0 || strcasecmp(s + n - 4, ".ttc") == 0);
}

/* (strcasestr isn't everywhere) */
static char *find_word(char *s, const char *word)
{
	size_t n = strlen(word);
	for (; *s; s++)
		if (strncasecmp(s, word, n) == 0)
			return s;
	return NULL;
}

/* the font's file, for a name: fontconfig's, or a few usual places */
static char *img_font_file(const char *name)
{
	if (path_is_font(name))
		return strdup(name);
	char cmd[400], path[1024] = "";
	/* (the name as fontconfig takes it: "Serif Bold" is "Serif:bold") */
	char pattern[200];
	snprintf(pattern, sizeof pattern, "%s", name);
	for (char *w = pattern; *w; w++)
		if (*w == '\'')
			*w = ' ';
	char *bold = find_word(pattern, " bold"), *italic = find_word(pattern, " italic");
	if (bold)
		*bold = 0;
	if (italic)
		*italic = 0;
	snprintf(cmd, sizeof cmd, "fc-match -f '%%{file}' '%s%s%s:fontformat=TrueType' 2>/dev/null", pattern, bold ? ":bold" : "",
		 italic ? ":italic" : "");
#ifndef __wasi__   /* (fontconfig is another program: none in a browser) */
	FILE *p = popen(cmd, "r");
	if (p) {
		if (!fgets(path, sizeof path, p))
			path[0] = 0;
		pclose(p);
	}
#endif
	FILE *f = path[0] ? fopen(path, "rb") : NULL;
	if (f) {
		fclose(f);
		return strdup(path);
	}
	static const char *const places[] = {
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
		"/Library/Fonts/Arial.ttf", NULL };
	for (int i = 0; places[i]; i++)
		if ((f = fopen(places[i], "rb"))) {
			fclose(f);
			return strdup(places[i]);
		}
	return NULL;
}

/* its number in img_fonts; -1: the built-in font */
static int img_font(const char *name)
{
	const char *b = getenv("CABBY_IMAGES_FONT");
	if ((b && strcmp(b, "builtin") == 0) || strcasecmp(name, "builtin") == 0)
		return -1;
	for (int i = 0; i < nimg_fonts; i++)
		if (strcmp(img_fonts[i].name, name) == 0)
			return img_fonts[i].data ? i : -1;
	ImgFont f = { strdup(name), NULL, { 0 } };
	char *file = img_font_file(name);
	if (path_is_font(name) && !file)
		rt_panic(NULL, 0, 0, "can't read the font '%s'", name);
	if (file) {
		f.data = whole(file);
		if (!f.data && path_is_font(name))
			rt_panic(NULL, 0, 0, "can't read the font '%s'", name);
		if (f.data && !stbtt_InitFont(&f.info, f.data, stbtt_GetFontOffsetForIndex(f.data, 0))) {
			free(f.data);
			f.data = NULL;
			if (path_is_font(name))
				rt_panic(NULL, 0, 0, "'%s' isn't a font this can read (TrueType or OpenType)", name);
		}
		free(file);
	}
	img_fonts = realloc(img_fonts, sizeof *img_fonts * (size_t)(nimg_fonts + 1));
	img_fonts[nimg_fonts] = f;
	return f.data ? nimg_fonts++ : (nimg_fonts++, -1);
}

void cby_image_set_font(const char *name, double size)
{
	if (size <= 0)
		rt_panic(NULL, 0, 0, "%s", "a font's size is more than 0 pixels");
	pen_font = img_font(*name ? name : "Sans");
	pen_size = size;
}

static uint32_t next_code(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = *p++;
	int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
	if (more)
		c &= 0x3f >> more;
	while (more-- && (*p & 0xc0) == 0x80)
		c = c << 6 | (*p++ & 0x3f);
	*s = (const char *)p;
	return c;
}

static void blend(Picture *p, int64_t x, int64_t y, const uint8_t c[4], unsigned cover)
{
	if (x < 0 || y < 0 || x >= p->w || y >= p->h || !cover)
		return;
	uint8_t *d = p->px + ((size_t)y * p->w + x) * 4;
	unsigned a = c[3] * cover / 255;
	for (int k = 0; k < 3; k++)
		d[k] = (uint8_t)((c[k] * a + d[k] * (255 - a)) / 255);
	d[3] = (uint8_t)(a + d[3] * (255 - a) / 255);
}

/* draws (or, with p NULL, only measures) a line of text, its top left at
 * x, y: its width */
static double text_run(Picture *p, double x, double y, const char *text)
{
	if (pen_font == -2)
		pen_font = img_font("Sans");
	int fi = pen_font;
	double start = x;
	if (fi < 0) {
		/* the built-in font: each letter 8 by 16, made as big as the size says */
		double k = pen_size / 16;
		for (const char *s = text; *s;) {
			uint32_t c = next_code(&s);
			if (c >= 32 && c < 127 && p)
				for (int r = 0; r < 16; r++)
					for (int b = 0; b < 8; b++)
						if (game_font[c - 32][r] & (0x80 >> b)) {
							int64_t x0 = (int64_t)floor(x + b * k), x1 = (int64_t)floor(x + (b + 1) * k);
							int64_t y0 = (int64_t)floor(y + r * k), y1 = (int64_t)floor(y + (r + 1) * k);
							for (int64_t yy = y0; yy < (y1 > y0 ? y1 : y0 + 1); yy++)
								for (int64_t xx = x0; xx < (x1 > x0 ? x1 : x0 + 1); xx++)
									blend(p, xx, yy, p->pen, 255);
						}
			x += 8 * k;
		}
		return x - start;
	}
	stbtt_fontinfo *f = &img_fonts[fi].info;
	float scale = stbtt_ScaleForPixelHeight(f, (float)pen_size);
	int ascent, descent, gap;
	stbtt_GetFontVMetrics(f, &ascent, &descent, &gap);
	double base = y + ascent * scale;
	uint32_t prev = 0;
	for (const char *s = text; *s;) {
		uint32_t c = next_code(&s);
		if (prev)
			x += scale * stbtt_GetCodepointKernAdvance(f, (int)prev, (int)c);
		int adv, lsb;
		stbtt_GetCodepointHMetrics(f, (int)c, &adv, &lsb);
		if (p) {
			int w = 0, h = 0, ox = 0, oy = 0;
			float sub = (float)(x - floor(x));
			unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(f, scale, scale, sub, 0, (int)c, &w, &h, &ox, &oy);
			for (int r = 0; r < h; r++)
				for (int b = 0; b < w; b++)
					blend(p, (int64_t)floor(x) + ox + b, (int64_t)floor(base) + oy + r, p->pen, bm[r * w + b]);
			stbtt_FreeBitmap(bm, NULL);
		}
		x += adv * scale;
		prev = c;
	}
	return x - start;
}

/* text, in the pen's color, its top left at x, y ("\n" between lines) */
void cby_image_text(int64_t id, double x, double y, const char *text)
{
	Picture *p = pic(id);
	char *copy = strdup(text), *line = copy;
	if (pen_font == -2)
		pen_font = img_font("Sans");
	double h = pen_font >= 0 ? pen_size * 1.25 : pen_size;
	for (;;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		text_run(p, x, y, line);
		if (!nl)
			break;
		line = nl + 1;
		y += h;
	}
	free(copy);
	p->version++;
}

/* how wide its longest line would be, in the font set, in pixels */
double cby_image_text_width(const char *text)
{
	char *copy = strdup(text), *line = copy;
	double most = 0;
	for (;;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		double w = text_run(NULL, 0, 0, line);
		if (w > most)
			most = w;
		if (!nl)
			break;
		line = nl + 1;
	}
	free(copy);
	return most;
}

__attribute__((destructor)) static void images_end(void)
{
	for (int i = 0; i < nimg_fonts; i++) {
		free(img_fonts[i].name);
		free(img_fonts[i].data);
	}
	free(img_fonts);
	img_fonts = NULL;
	nimg_fonts = 0;
	for (int64_t i = 0; i < npics; i++)
		if (pics[i]) {
			free(pics[i]->px);
			free(pics[i]);
		}
	free(pics);
	pics = NULL;
	npics = 0;
}
