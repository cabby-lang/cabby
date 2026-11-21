/* sdl_common.h — what the libraries drawn with SDL 3 share (gui.c and
 * game.c): colors written as on the web, pictures (read by stb_image),
 * text in the computer's own fonts (drawn by stb_truetype), shapes made of
 * triangles, and key names. Included by each library's one C file, which
 * defines SDL_LIB_NAME first ("game"), for its messages. */

#ifndef SDL_LIB_NAME
#error "define SDL_LIB_NAME before including sdl_common.h"
#endif

#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef __wasi__
/* (in a browser no other program runs, fc-match neither: fonts are where
 * the page puts them, in the places looked in after it) */
#define popen(cmd, mode) ((void)(cmd), (void)(mode), (FILE *)NULL)
#define pclose(p) ((void)(p), 0)
/* and it's one program, with nothing to load: what the libraries look up
 * by name (in the runtime) is there by its own name */
#include <dlfcn.h>
extern void (*rt_idle_hook)(void);
void cby_events_step(int wait_ms);
int cby_events_fds(int *fds, int max);
static inline void *cby_wasm_dlsym(void *lib, const char *name)
{
	(void)lib;
	if (strcmp(name, "rt_idle_hook") == 0)
		return (void *)&rt_idle_hook;
	if (strcmp(name, "cby_events_step") == 0)
		return (void *)cby_events_step;
	if (strcmp(name, "cby_events_fds") == 0)
		return (void *)cby_events_fds;
	return NULL;
}
#define dlsym(lib, name) cby_wasm_dlsym(lib, name)
#define dlopen(name, flags) ((void)(name), (void)(flags), (void *)NULL)
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#ifdef __wasi__
/* (in WebAssembly the libraries are one program: each keeps its own) */
#define STB_IMAGE_STATIC
#define STB_IMAGE_WRITE_STATIC
#define STBTT_STATIC
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "game_font.h"

/* (each library uses some of what's here, not all) */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif


/* ------------------------------------------------------------- panics and handlers */

extern _Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...) __attribute__((weak));
extern void rt_flush_files(void) __attribute__((weak));

static _Noreturn void fail(const char *fmt, const char *a)
{
	if (rt_panic)
		rt_panic(NULL, 0, 0, fmt, a);
	fprintf(stderr, SDL_LIB_NAME ": ");
	fprintf(stderr, fmt, a);
	fputc('\n', stderr);
	exit(101);
}

/* Calling a Cabby method: the interpreter points this at its own way of
 * calling one; a compiled program calls the function directly. */
#ifdef __wasi__
extern void (*cby_invoke_hook)(void *obj, void *fn);   /* (one for all the libraries: in rt.c) */
#else
static void call_directly(void *obj, void *fn) { ((void (*)(void *))fn)(obj); }
void (*cby_invoke_hook)(void *obj, void *fn) = call_directly;
#endif

typedef struct Handler {
	void *obj, *fn;
} Handler;

static void invoke(Handler h)
{
	if (!h.fn)
		return;
	cby_invoke_hook(h.obj, h.fn);
	if (rt_flush_files)
		rt_flush_files();
}

/* ------------------------------------------------------------- colors */

#include "colors.h"

typedef struct Color {
	float r, g, b, a;
} Color;

static Color color(const char *spec)
{
	double a;
	uint32_t c = color_of(spec, &a);
	return (Color){ ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, (float)a };
}

/* ------------------------------------------------------------- pictures
 *
 * Read once each (with stb_image), and kept: their pixels (for sizes and
 * for touching), and a texture on the graphics card once there's a window
 * to draw in. */

typedef struct Picture {
	char *path;
	int w, h;
	unsigned char *rgba;
	SDL_Texture *texture;
	SDL_Renderer *owner;  /* the renderer the texture is for */
	int x0, y0, x1, y1;   /* the box round the pixels that show (x1, y1 past the end) */
} Picture;

static Picture *pictures;
static int npictures;

static int picture(const char *path)
{
	for (int i = 0; i < npictures; i++)
		if (strcmp(pictures[i].path, path) == 0)
			return i;
	int w, h, n;
	unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
	if (!rgba)
		fail("can't show the picture '%s': it isn't there, or isn't a picture (PNG, JPEG, BMP or GIF)", path);
	Picture p = { strdup(path), w, h, rgba, NULL, NULL, w, h, 0, 0 };
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (rgba[((size_t)y * w + x) * 4 + 3] > 16) {
				if (x < p.x0) p.x0 = x;
				if (y < p.y0) p.y0 = y;
				if (x + 1 > p.x1) p.x1 = x + 1;
				if (y + 1 > p.y1) p.y1 = y + 1;
			}
	if (p.x1 <= p.x0 || p.y1 <= p.y0)
		p.x0 = p.y0 = p.x1 = p.y1 = 0;   /* nothing shows: it touches nothing */
	pictures = realloc(pictures, sizeof *pictures * (size_t)(npictures + 1));
	pictures[npictures] = p;
	return npictures++;
}

static SDL_Renderer *renderer;   /* what's being drawn with (NULL: nothing) */
static float density = 1;        /* its pixels for each of ours (2 on a high-density screen) */

static SDL_Texture *picture_texture(int i)
{
	Picture *p = &pictures[i];
	if (p->owner != renderer)
		p->texture = NULL;   /* (made for another window) */
	if (!p->texture && renderer) {
		p->owner = renderer;
		SDL_Surface *s = SDL_CreateSurfaceFrom(p->w, p->h, SDL_PIXELFORMAT_RGBA32, p->rgba, p->w * 4);
		if (s) {
			p->texture = SDL_CreateTextureFromSurface(renderer, s);
			SDL_DestroySurface(s);
		}
	}
	return p->texture;
}

/* ------------------------------------------------------------- over SSH
 *
 * A window from an SSH session: on Linux there's no screen to open it on
 * (unless the session forwards one), and on a Mac it opens on the Mac's own
 * screen, not where the person typing is. Either way, the terminal twin
 * (tgame for game, tui for gui) is what they want; these say so. */

static int over_ssh(void) { return getenv("SSH_CONNECTION") != NULL || getenv("SSH_TTY") != NULL; }

/* A window and its renderer. Drawn off screen (offscreen: for Cabby's
 * tests), with SDL's own drawing, and saying the window's graphics are
 * the program's own: otherwise, on macOS, SDL makes every window an
 * OpenGL one, and the offscreen driver looks for OpenGL ES (EGL), which a
 * Mac doesn't have. Shown, as SDL_CreateWindowAndRenderer does. */
static bool make_window(const char *title, int w, int h, SDL_WindowFlags flags, bool offscreen, SDL_Window **window,
			SDL_Renderer **renderer)
{
	if (!offscreen)
		return SDL_CreateWindowAndRenderer(title, w, h, flags, window, renderer);
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, (Sint64)flags);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
	*window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);
	if (!*window)
		return false;
	*renderer = SDL_CreateRenderer(*window, "software");
	if (!*renderer) {
		SDL_DestroyWindow(*window);
		*window = NULL;
		return false;
	}
	return true;
}

/* after "cannot open a window": what to do instead, over SSH */
static void ssh_instead(const char *lib, const char *twin)
{
	if (over_ssh())
		fprintf(stderr, "%s: (this is an SSH session: to run it in this terminal, use %s instead of %s)\n", lib, twin, lib);
}

/* a window that opens fine, but on a screen that isn't the one in front
 * of whoever started it over SSH (a Mac's own screen) */
static void ssh_note(const char *lib, const char *twin)
{
#if defined(__APPLE__)
	if (over_ssh() && !getenv("CABBY_GUI_SCRIPT"))
		fprintf(stderr, "%s: started over SSH, so the window opens on the Mac's own screen; to run it in "
			"this terminal, use %s instead of %s\n", lib, twin, lib);
#else
	(void)lib;
	(void)twin;
#endif
}

/* ------------------------------------------------------------- fonts
 *
 * A font is written as a font button gives it: "Serif Bold 16". The font
 * file comes from fontconfig (fc-match), as the desktop chooses it, or
 * from a few usual places; with none at all, a small font built in. */

typedef struct Font {
	char *key;            /* "serif bold" */
	unsigned char *data;  /* NULL: the built-in font */
	stbtt_fontinfo info;
	uint32_t len, offset; /* the file's length, and where the font is in it (a collection's first) */
	int color;            /* a color emoji font, which stb_truetype can't open: 1 CBDT, 2 sbix */
	int units, ascent, descent, nglyphs;   /* (a color font's measures) */
	void *hb;             /* HarfBuzz's font, once it's been needed (text_layout.h) */
} Font;

static Font *fonts;
static int nfonts;

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f)
		fclose(f);
	return f != NULL;
}

static uint32_t read_len;   /* (the length of what read_file read last) */

static unsigned char *read_file(const char *path)
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
	read_len = d ? (uint32_t)n : 0;
	return d;
}

static char *font_file(const char *family, int bold, int italic)
{
	char pattern[200], cmd[300], path[1024] = "";
	snprintf(pattern, sizeof pattern, "%s%s%s", family, bold ? ":bold" : "", italic ? ":italic" : "");
	snprintf(cmd, sizeof cmd, "fc-match -f '%%{file}' '%s:fontformat=TrueType' 2>/dev/null", pattern);
	FILE *p = popen(cmd, "r");
	if (p) {
		if (!fgets(path, sizeof path, p))
			path[0] = '\0';
		pclose(p);
	}
	if (path[0] && file_exists(path))
		return strdup(path);
	static const char *const places[] = {
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
		"/Library/Fonts/Arial.ttf", NULL };
	for (int i = 0; places[i]; i++)
		if (file_exists(places[i]))
			return strdup(places[i]);
	return NULL;
}

static int font_for(const char *family, int bold, int italic)
{
	char key[200];
	snprintf(key, sizeof key, "%s%s%s", family, bold ? " bold" : "", italic ? " italic" : "");
	for (int i = 0; i < nfonts; i++)
		if (strcmp(fonts[i].key, key) == 0)
			return i;
	Font f = { .key = strdup(key) };
	char *file = font_file(family, bold, italic);
	if (file) {
		f.data = read_file(file);
		f.len = read_len;
		f.offset = f.data ? (uint32_t)stbtt_GetFontOffsetForIndex(f.data, 0) : 0;
		if (f.data && !stbtt_InitFont(&f.info, f.data, (int)f.offset)) {
			free(f.data);
			f.data = NULL;
		}
		free(file);
	}
	fonts = realloc(fonts, sizeof *fonts * (size_t)(nfonts + 1));
	fonts[nfonts] = f;
	return nfonts++;
}

/* A font from its file (key "@path"), for borrowing characters; -1 if it
 * can't be read. */
static int font_from_file(const char *path)
{
	char key[1100];
	snprintf(key, sizeof key, "@%s", path);
	for (int i = 0; i < nfonts; i++)
		if (strcmp(fonts[i].key, key) == 0)
			return fonts[i].data ? i : -1;
	Font f = { .key = strdup(key), .data = read_file(path) };
	f.len = read_len;
	f.offset = f.data ? (uint32_t)stbtt_GetFontOffsetForIndex(f.data, 0) : 0;
	if (f.data && !stbtt_InitFont(&f.info, f.data, (int)f.offset)) {
		free(f.data);
		f.data = NULL;
	}
	fonts = realloc(fonts, sizeof *fonts * (size_t)(nfonts + 1));
	fonts[nfonts] = f;
	nfonts++;
	return f.data ? nfonts - 1 : -1;
}

/* whether the font can draw the character: it has a glyph for it, with
 * lines to draw (a colored emoji font has the glyph, but only as a
 * picture, which stb_truetype can't draw) */
static int font_has(int font, uint32_t code)
{
	Font *f = &fonts[font];
	if (!f->data || f->color)
		return 0;
	int g = stbtt_FindGlyphIndex(&f->info, (int)code);
	if (!g)
		return 0;
	if (code == ' ' || code == 0xa0 || (code >= 0x2000 && code <= 0x200f))
		return 1;
	stbtt_vertex *v = NULL;
	int n = stbtt_GetGlyphShape(&f->info, g, &v);
	stbtt_FreeShape(&f->info, v);
	return n > 0;
}

/* Which font draws the character: the font itself if it can, or else
 * another one that can (Greek with accents, Old English letters, symbols),
 * as GTK borrows them. fontconfig is asked for fonts that have it; without
 * fontconfig (as on a Mac), a few usual fonts are tried. The answers are
 * kept. */
typedef struct Borrowed {
	int font;
	uint32_t code;
	int from;   /* the font that draws it */
	int used;
} Borrowed;

#define BORROWED 2048
static Borrowed borrowed[BORROWED];

static int borrow_from(uint32_t code)
{
	/* a font already borrowed from, first: Greek text then needs asking
	 * about once, not for every letter */
	for (int i = 0; i < nfonts; i++)
		if (fonts[i].key[0] == '@' && font_has(i, code))
			return i;
	char cmd[200], path[1024];
	snprintf(cmd, sizeof cmd, "fc-match -s -f '%%{file}\\n' 'sans:charset=%x:color=false' 2>/dev/null", (unsigned)code);
	FILE *p = popen(cmd, "r");
	int found = -1, tries = 0;
	if (p) {
		while (found < 0 && tries < 12 && fgets(path, sizeof path, p)) {
			path[strcspn(path, "\n")] = '\0';
			if (!path[0])
				continue;
			tries++;
			int fo = font_from_file(path);
			if (fo >= 0 && font_has(fo, code))
				found = fo;
		}
		pclose(p);
	}
	static const char *const places[] = {
		"/System/Library/Fonts/Supplemental/Arial Unicode.ttf", "/Library/Fonts/Arial Unicode.ttf",
		"/System/Library/Fonts/Supplemental/Times New Roman.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
		"/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/Supplemental/STIXGeneral.otf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
		"/usr/share/fonts/TTF/Symbola.ttf", NULL };
	for (int i = 0; found < 0 && places[i]; i++)
		if (file_exists(places[i])) {
			int fo = font_from_file(places[i]);
			if (fo >= 0 && font_has(fo, code))
				found = fo;
		}
	return found;
}

static int font_with(int font, uint32_t code)
{
	if (code < 0x80 || !fonts[font].data || font_has(font, code))
		return font;
	uint32_t h = (code * 2654435761u) ^ (uint32_t)font * 40503u;
	for (int k = 0; k < BORROWED; k++) {
		Borrowed *b = &borrowed[(h + (uint32_t)k) % BORROWED];
		if (b->used && b->font == font && b->code == code)
			return b->from;
		if (!b->used) {
			int from = borrow_from(code);
			*b = (Borrowed){ font, code, from >= 0 ? from : font, 1 };
			return b->from;
		}
	}
	return font;   /* (the table is full: drawn as the font draws it) */
}

/* glyphs, made into textures as they're first written */
typedef struct Glyph {
	SDL_Renderer *owner;
	int font, size;       /* (size in the screen's own pixels) */
	uint32_t code;
	SDL_Texture *texture;
	int x, y, w, h;       /* where its picture goes, from the pen on the baseline */
	float advance;
	int used;
} Glyph;

#define GLYPHS 4096
static Glyph glyphs[GLYPHS];

static Glyph *glyph(int font, int size, uint32_t code)
{
	uint32_t hsh = (code * 2654435761u) ^ (uint32_t)(font * 97 + size * 131) ^ (uint32_t)(uintptr_t)renderer;
	for (int k = 0; k < GLYPHS; k++) {
		Glyph *g = &glyphs[(hsh + (uint32_t)k) % GLYPHS];
		if (g->used && g->owner == renderer && g->font == font && g->size == size && g->code == code)
			return g;
		if (!g->used) {
			Font *f = &fonts[font];
			*g = (Glyph){ renderer, font, size, code, NULL, 0, 0, 0, 0, 0, 1 };
			float scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)size);
			int adv, lsb;
			unsigned char *bits;
			if (code & 0x80000000u) {   /* (a glyph by its number: laid out text) */
				int gi = (int)(code & 0x7FFFFFFFu);
				stbtt_GetGlyphHMetrics(&f->info, gi, &adv, &lsb);
				bits = stbtt_GetGlyphBitmap(&f->info, scale, scale, gi, &g->w, &g->h, &g->x, &g->y);
			} else {
				stbtt_GetCodepointHMetrics(&f->info, (int)code, &adv, &lsb);
				bits = stbtt_GetCodepointBitmap(&f->info, scale, scale, (int)code, &g->w, &g->h, &g->x, &g->y);
			}
			g->advance = adv * scale;
			if (bits && g->w > 0 && g->h > 0 && renderer) {
				unsigned char *rgba = malloc((size_t)g->w * g->h * 4);
				for (int i = 0; i < g->w * g->h; i++) {
					rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
					rgba[i * 4 + 3] = bits[i];
				}
				SDL_Surface *s = SDL_CreateSurfaceFrom(g->w, g->h, SDL_PIXELFORMAT_RGBA32, rgba, g->w * 4);
				if (s) {
					g->texture = SDL_CreateTextureFromSurface(renderer, s);
					SDL_DestroySurface(s);
				}
				free(rgba);
			}
			if (bits)
				stbtt_FreeBitmap(bits, NULL);
			return g;
		}
	}
	return NULL;   /* (full: written without a picture) */
}

/* a renderer is going: what was made for it goes with it */
static void forget_textures(SDL_Renderer *r)
{
	for (int i = 0; i < npictures; i++)
		if (pictures[i].owner == r) {
			pictures[i].texture = NULL;   /* (the renderer destroys its own) */
			pictures[i].owner = NULL;
		}
	int any = 0;
	for (int i = 0; i < GLYPHS; i++)
		if (glyphs[i].used && glyphs[i].owner == r)
			any = 1;
	if (any)   /* (cleared together: the table is searched in runs) */
		for (int i = 0; i < GLYPHS; i++)
			glyphs[i].used = 0;
}

/* a key's name as the game library writes it: "a", "space", "left" */
static void key_name_of(const char *in, char *out, size_t size)
{
	if (strcmp(in, " ") == 0)
		in = "space";
	else if (strcmp(in, "esc") == 0)
		in = "escape";
	else if (strcmp(in, "return") == 0)
		in = "enter";
	else if (strcmp(in, "pageup") == 0)
		in = "page_up";
	else if (strcmp(in, "pagedown") == 0)
		in = "page_down";
	snprintf(out, size, "%s", in);
	for (char *c = out; *c; c++)
		if (*c >= 'A' && *c <= 'Z')
			*c = (char)(*c - 'A' + 'a');
}

/* SDL's name for a key, as the game library writes it */
static void sdl_key_name(SDL_Keycode k, char *out, size_t size)
{
	out[0] = '\0';
	switch (k) {
	case SDLK_RETURN: case SDLK_KP_ENTER: snprintf(out, size, "enter"); return;
	case SDLK_ESCAPE: snprintf(out, size, "escape"); return;
	case SDLK_SPACE: snprintf(out, size, "space"); return;
	case SDLK_TAB: snprintf(out, size, "tab"); return;
	case SDLK_BACKSPACE: snprintf(out, size, "backspace"); return;
	case SDLK_DELETE: snprintf(out, size, "delete"); return;
	case SDLK_INSERT: snprintf(out, size, "insert"); return;
	case SDLK_HOME: snprintf(out, size, "home"); return;
	case SDLK_END: snprintf(out, size, "end"); return;
	case SDLK_PAGEUP: snprintf(out, size, "page_up"); return;
	case SDLK_PAGEDOWN: snprintf(out, size, "page_down"); return;
	case SDLK_UP: snprintf(out, size, "up"); return;
	case SDLK_DOWN: snprintf(out, size, "down"); return;
	case SDLK_LEFT: snprintf(out, size, "left"); return;
	case SDLK_RIGHT: snprintf(out, size, "right"); return;
	}
	if (k >= SDLK_F1 && k <= SDLK_F12) {
		snprintf(out, size, "f%d", (int)(k - SDLK_F1) + 1);
		return;
	}
	if (k > 32 && k < 127) {
		out[0] = (char)(k >= 'A' && k <= 'Z' ? k - 'A' + 'a' : k);
		out[1] = '\0';
	}
}


static int parse_keys(const char *spec, char *out, size_t size)
{
	int ctrl = 0, shift = 0, alt = 0;
	char key[24] = "";
	const char *p = spec;
	while (*p) {
		const char *plus = strchr(p, '+');
		if (plus == p)
			plus = strchr(p + 1, '+');
		size_t n = plus ? (size_t)(plus - p) : strlen(p);
		char part[24];
		if (n == 0 || n >= sizeof part)
			return 0;
		for (size_t i = 0; i < n; i++)
			part[i] = (char)(p[i] >= 'A' && p[i] <= 'Z' ? p[i] - 'A' + 'a' : p[i]);
		part[n] = '\0';
		if (plus && (!strcmp(part, "ctrl") || !strcmp(part, "control")))
			ctrl = 1;
		else if (plus && !strcmp(part, "shift"))
			shift = 1;
		else if (plus && !strcmp(part, "alt"))
			alt = 1;
		else if (plus)
			return 0;
		else
			key_name_of(part, key, sizeof key);
		p = plus ? plus + 1 : p + n;
	}
	if (!key[0])
		return 0;
	snprintf(out, size, "%s%s%s%s", ctrl ? "ctrl+" : "", shift ? "shift+" : "", alt ? "alt+" : "", key);
	return 1;
}

/* ------------------------------------------------------------- shapes, as triangles */

static SDL_FColor fcolor(Color c) { return (SDL_FColor){ c.r, c.g, c.b, c.a }; }

static void triangles(const float *xy, int n, const int *index, int nindex, SDL_FColor c)
{
	SDL_Vertex *v = malloc(sizeof *v * (size_t)n);
	for (int i = 0; i < n; i++)
		v[i] = (SDL_Vertex){ { xy[2 * i], xy[2 * i + 1] }, c, { 0, 0 } };
	SDL_RenderGeometry(renderer, NULL, v, n, index, nindex);
	free(v);
}

/* a filled ellipse (or circle), as a fan of triangles */
static void fill_ellipse(double x, double y, double rx, double ry, SDL_FColor c)
{
	int n = (int)(fmax(rx, ry) / 2) + 16;
	if (n > 180)
		n = 180;
	float *xy = malloc(sizeof *xy * (size_t)(n + 1) * 2);
	int *ix = malloc(sizeof *ix * (size_t)n * 3);
	xy[0] = (float)x;
	xy[1] = (float)y;
	for (int i = 0; i < n; i++) {
		double a = 2 * M_PI * i / n;
		xy[2 + 2 * i] = (float)(x + rx * cos(a));
		xy[3 + 2 * i] = (float)(y + ry * sin(a));
		ix[3 * i] = 0;
		ix[3 * i + 1] = 1 + i;
		ix[3 * i + 2] = 1 + (i + 1) % n;
	}
	triangles(xy, n + 1, ix, n * 3, c);
	free(xy);
	free(ix);
}

/* a line Width wide, with round ends */
static void thick_line(double x1, double y1, double x2, double y2, double w, SDL_FColor c)
{
	double dx = x2 - x1, dy = y2 - y1, len = sqrt(dx * dx + dy * dy);
	if (w <= 1.01 && (dx == 0 || dy == 0)) {   /* a thin straight line: as SDL draws one */
		SDL_SetRenderDrawColorFloat(renderer, c.r, c.g, c.b, c.a);
		SDL_RenderLine(renderer, (float)x1, (float)y1, (float)x2, (float)y2);
		return;
	}
	if (len > 0) {
		double nx = -dy / len * w / 2, ny = dx / len * w / 2;
		float xy[8] = { (float)(x1 + nx), (float)(y1 + ny), (float)(x2 + nx), (float)(y2 + ny),
				(float)(x2 - nx), (float)(y2 - ny), (float)(x1 - nx), (float)(y1 - ny) };
		int ix[6] = { 0, 1, 2, 0, 2, 3 };
		triangles(xy, 4, ix, 6, c);
	}
	if (w > 2) {
		fill_ellipse(x1, y1, w / 2, w / 2, c);
		fill_ellipse(x2, y2, w / 2, w / 2, c);
	}
}

/* a ring between two ellipses, from one angle to another (radians) */
static void ring(double x, double y, double rx, double ry, double w, double from, double to, SDL_FColor c)
{
	int n = (int)(fmax(rx, ry) * fabs(to - from) / 6) + 12;
	if (n > 360)
		n = 360;
	float *xy = malloc(sizeof *xy * (size_t)(n + 1) * 4);
	int *ix = malloc(sizeof *ix * (size_t)n * 6);
	for (int i = 0; i <= n; i++) {
		double a = from + (to - from) * i / n;
		xy[4 * i] = (float)(x + (rx + w / 2) * cos(a));
		xy[4 * i + 1] = (float)(y + (ry + w / 2) * sin(a));
		xy[4 * i + 2] = (float)(x + fmax(0, rx - w / 2) * cos(a));
		xy[4 * i + 3] = (float)(y + fmax(0, ry - w / 2) * sin(a));
		if (i < n) {
			int k = 2 * i;
			ix[6 * i] = k; ix[6 * i + 1] = k + 1; ix[6 * i + 2] = k + 2;
			ix[6 * i + 3] = k + 1; ix[6 * i + 4] = k + 3; ix[6 * i + 5] = k + 2;
		}
	}
	triangles(xy, (n + 1) * 2, ix, n * 6, c);
	free(xy);
	free(ix);
}

/* a filled polygon (any shape that doesn't cross itself), cut into
 * triangles by clipping off its ears */
static void fill_polygon(const double *pts, int n, SDL_FColor c)
{
	if (n < 3)
		return;
	double area = 0;
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		area += pts[2 * i] * pts[2 * j + 1] - pts[2 * j] * pts[2 * i + 1];
	}
	int *left = malloc(sizeof *left * (size_t)n);
	for (int i = 0; i < n; i++)
		left[i] = area > 0 ? i : n - 1 - i;   /* turning the same way every time */
	int *ix = malloc(sizeof *ix * (size_t)(n - 2) * 3), nix = 0, m = n, guard = 0;
	while (m > 3 && guard++ < n * n) {
		int found_ear = 0;
		for (int i = 0; i < m && !found_ear; i++) {
			int a = left[(i + m - 1) % m], b = left[i], d = left[(i + 1) % m];
			double ax = pts[2 * a], ay = pts[2 * a + 1], bx = pts[2 * b], by = pts[2 * b + 1];
			double cx = pts[2 * d], cy = pts[2 * d + 1];
			double cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
			if (cross <= 0)
				continue;   /* a dent, not an ear */
			int inside = 0;
			for (int k = 0; k < m && !inside; k++) {
				int p = left[k];
				if (p == a || p == b || p == d)
					continue;
				double px = pts[2 * p], py = pts[2 * p + 1];
				double d1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
				double d2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
				double d3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
				inside = d1 >= 0 && d2 >= 0 && d3 >= 0;
			}
			if (inside)
				continue;
			ix[nix++] = a; ix[nix++] = b; ix[nix++] = d;
			memmove(left + i, left + i + 1, sizeof *left * (size_t)(m - i - 1));
			m--;
			found_ear = 1;
		}
		if (!found_ear)
			break;
	}
	if (m == 3) {
		ix[nix++] = left[0]; ix[nix++] = left[1]; ix[nix++] = left[2];
	}
	float *xy = malloc(sizeof *xy * (size_t)n * 2);
	for (int i = 0; i < 2 * n; i++)
		xy[i] = (float)pts[i];
	triangles(xy, n, ix, nix, c);
	free(xy);
	free(ix);
	free(left);
}

static uint32_t next_char(const unsigned char **p)
{
	const unsigned char *s = *p;
	uint32_t c = *s++;
	int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	if (more)
		c &= 0x3F >> more;
	while (more-- && (*s & 0xC0) == 0x80)
		c = (c << 6) | (*s++ & 0x3F);
	*p = s;
	return c;
}

/* ------------------------------------------------------------- text
 *
 * Sizes are in our pixels (14 is the usual size of a window's text); on a
 * high-density screen glyphs are made at the screen's own size, so they're
 * sharp. */

static float text_scale(int font, double size)
{
	return stbtt_ScaleForMappingEmToPixels(&fonts[font].info, (float)size);
}

/* how far down the first line's baseline is, and a line's height */
static double text_ascent(int font, double size)
{
	if (!fonts[font].data)
		return size * 12 / 14;
	int a, d, g;
	stbtt_GetFontVMetrics(&fonts[font].info, &a, &d, &g);
	return a * text_scale(font, size);
}

static double line_height(int font, double size)
{
	if (!fonts[font].data)
		return size * 16 / 14;
	int a, d, g;
	stbtt_GetFontVMetrics(&fonts[font].info, &a, &d, &g);
	return (a - d + g) * text_scale(font, size);
}

#include "text_layout.h"

/* the width of the text's first n bytes (n < 0: all of it) */
static double text_width_n(int font, double size, const char *text, int n)
{
	const unsigned char *p = (const unsigned char *)text, *end = n < 0 ? NULL : p + n;
	if (fonts[font].data && needs_layout(text, n))
		return laid_out(font, size, text, n)->width;
	if (!fonts[font].data) {
		double w = 0;
		while (*p && (!end || p < end)) {
			next_char(&p);
			w += 8 * size / 14;
		}
		return w;
	}
	double w = 0;
	uint32_t last = 0;
	int last_font = -1;
	while (*p && (!end || p < end)) {
		uint32_t c = next_char(&p);
		int fo = font_with(font, c);
		Font *f = &fonts[fo];
		float scale = text_scale(fo, size);
		int adv, lsb;
		if (last && fo == last_font)
			w += stbtt_GetCodepointKernAdvance(&f->info, (int)last, (int)c) * scale;
		stbtt_GetCodepointHMetrics(&f->info, (int)c, &adv, &lsb);
		w += adv * scale;
		last = c;
		last_font = fo;
	}
	return w;
}

static double text_width(int font, double size, const char *text) { return text_width_n(font, size, text, -1); }

/* text (its first n bytes; n < 0: all), its top left corner at x, y */
static void draw_text_n(int font, double size, double x, double y, const char *text, int n, SDL_FColor c)
{
	const unsigned char *p = (const unsigned char *)text, *end = n < 0 ? NULL : p + n;
	Font *f = &fonts[font];
	if (!f->data) {
		/* the font built in: 8 by 16, made bigger or smaller */
		double k = size / 14.0, px = x;
		SDL_SetRenderDrawColorFloat(renderer, c.r, c.g, c.b, c.a);
		while (*p && (!end || p < end)) {
			uint32_t ch = next_char(&p);
			if (ch >= 32 && ch < 127)
				for (int r = 0; r < 16; r++)
					for (int b = 0; b < 8; b++)
						if (game_font[ch - 32][r] & (0x80 >> b)) {
							SDL_FRect d = { (float)(px + b * k), (float)(y + r * k), (float)k, (float)k };
							SDL_RenderFillRect(renderer, &d);
						}
			px += 8 * k;
		}
		return;
	}
	if (needs_layout(text, n)) {
		draw_layout(laid_out(font, size, text, n), font, size, x, y, c);
		return;
	}
	int pixels = (int)(size * density + 0.5);   /* glyphs at the screen's own size */
	double baseline = y + text_ascent(font, size), px = x;
	uint32_t last = 0;
	int last_font = -1;
	while (*p && (!end || p < end)) {
		uint32_t ch = next_char(&p);
		int fo = font_with(font, ch);   /* (another font, for a character this one lacks) */
		f = &fonts[fo];
		float scale = text_scale(fo, size);
		if (last && fo == last_font)
			px += stbtt_GetCodepointKernAdvance(&f->info, (int)last, (int)ch) * scale;
		Glyph *gl = glyph(fo, pixels, ch);
		if (gl && gl->texture) {
			SDL_SetTextureColorModFloat(gl->texture, c.r, c.g, c.b);
			SDL_SetTextureAlphaModFloat(gl->texture, c.a);
			SDL_FRect d = { (float)(floor((px + gl->x / density) * density + 0.5) / density),
					(float)(floor((baseline + gl->y / density) * density + 0.5) / density),
					(float)(gl->w / density), (float)(gl->h / density) };
			SDL_RenderTexture(renderer, gl->texture, NULL, &d);
		}
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&f->info, (int)ch, &adv, &lsb);
		px += adv * scale;
		last = ch;
		last_font = fo;
	}
}

static void draw_text(int font, double size, double x, double y, const char *text, SDL_FColor c)
{
	draw_text_n(font, size, x, y, text, -1, c);
}

/* where the text cursor goes before byte `at` of the text's first n bytes:
 * from the text's left edge (left to right, the width of what's before
 * it; in Hebrew or Arabic, from the right) */
static double text_caret(int font, double size, const char *text, int n, int at)
{
	if (fonts[font].data && needs_layout(text, n))
		return layout_caret(laid_out(font, size, text, n), at);
	return text_width_n(font, size, text, at);
}

/* "Serif Bold 16": the font, and its size (0 if it gives none) */
static int font_named(const char *spec, double *size)
{
	char family[100] = "";
	int bold = 0, italic = 0;
	*size = 0;
	char *copy = strdup(spec), *save = NULL;
	for (char *w = strtok_r(copy, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
		char *end;
		double n = strtod(w, &end);
		if (!*end && end != w)
			*size = n;
		else if (!strcasecmp(w, "bold") || !strcasecmp(w, "semibold") || !strcasecmp(w, "heavy"))
			bold = 1;
		else if (!strcasecmp(w, "italic") || !strcasecmp(w, "oblique"))
			italic = 1;
		else if (strcasecmp(w, "regular") && strlen(family) + strlen(w) + 2 < sizeof family) {
			if (family[0])
				strcat(family, " ");
			strcat(family, w);
		}
	}
	free(copy);
	return font_for(family[0] ? family : "Sans", bold, italic);
}

/* saving a picture of what's drawn: the renderer's target, as a PNG file */
static int save_png(SDL_Surface *s, const char *path)
{
	SDL_Surface *rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
	if (!rgba)
		return 0;
	int ok = stbi_write_png(path, rgba->w, rgba->h, 4, rgba->pixels, rgba->pitch);
	SDL_DestroySurface(rgba);
	return ok;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
