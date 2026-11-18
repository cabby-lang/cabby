/* gui.c — the gui library (`use gui`): windows and everything in them,
 * drawn by Cabby itself with SDL 3. The same objects and methods are drawn
 * by GTK 4 in gtk.c (`use gtk`), kept as an option.
 *
 * SDL gives a window, the keys, the mouse and a renderer, but no buttons:
 * every widget is drawn here, text in the computer's own fonts, shapes as
 * triangles, from sdl_common.h, which the game library shares.
 *
 * The model is gtk.c's: widgets are numbered items in one table, boxes
 * (columns and rows) hold them, and handlers are a Cabby object and one
 * of its methods. Sizes are in pixels; on a high-density screen everything
 * is drawn at the screen's own size, so it stays sharp. */

#define SDL_LIB_NAME "gui"
#include "sdl_common.h"
#include "video_source.h"
#include <ctype.h>
#include <dlfcn.h>

/* ------------------------------------------------------------- the look */

#define UI_SIZE 14            /* text */
#define SPACING 8             /* between things in a column or row */
#define MARGIN 12             /* round a window's contents */
#define FIELD_H 34            /* buttons, text boxes, drop-downs */
#define ROW_H 30              /* a list's rows */
#define RADIUS 6
#define TAB_H 36              /* the tabs along the top of a set of tabs */
#define BAR_H 30              /* the menu bar */

/* light, or dark when the desktop is (or CABBY_THEME=dark) */
static SDL_FColor C_WINDOW, C_TEXT, C_DIM, C_FIELD, C_BORDER, C_BUTTON, C_BUTTON_HOVER, C_BUTTON_DOWN, C_ACCENT,
	C_SELECTION, C_TRACK, C_SHADOW, C_BAR;
static const SDL_FColor C_WHITE = { 1, 1, 1, 1 };
static int dark = -1;

static void set_look(int is_dark)
{
	dark = is_dark;
	C_ACCENT = (SDL_FColor){ 0.21f, 0.52f, 0.89f, 1 };
	if (!dark) {
		C_WINDOW = (SDL_FColor){ 0.965f, 0.961f, 0.957f, 1 };
		C_TEXT = (SDL_FColor){ 0.18f, 0.20f, 0.21f, 1 };
		C_DIM = (SDL_FColor){ 0.57f, 0.58f, 0.58f, 1 };
		C_FIELD = (SDL_FColor){ 1, 1, 1, 1 };
		C_BORDER = (SDL_FColor){ 0.80f, 0.78f, 0.76f, 1 };
		C_BUTTON = (SDL_FColor){ 0.93f, 0.92f, 0.91f, 1 };
		C_BUTTON_HOVER = (SDL_FColor){ 0.97f, 0.96f, 0.96f, 1 };
		C_BUTTON_DOWN = (SDL_FColor){ 0.85f, 0.84f, 0.83f, 1 };
		C_SELECTION = (SDL_FColor){ 0.21f, 0.52f, 0.89f, 0.30f };
		C_TRACK = (SDL_FColor){ 0.85f, 0.84f, 0.83f, 1 };
		C_SHADOW = (SDL_FColor){ 0, 0, 0, 0.12f };
		C_BAR = (SDL_FColor){ 0.92f, 0.91f, 0.90f, 1 };
	} else {
		C_WINDOW = (SDL_FColor){ 0.14f, 0.14f, 0.14f, 1 };
		C_TEXT = (SDL_FColor){ 0.96f, 0.96f, 0.96f, 1 };
		C_DIM = (SDL_FColor){ 0.55f, 0.55f, 0.55f, 1 };
		C_FIELD = (SDL_FColor){ 0.19f, 0.19f, 0.19f, 1 };
		C_BORDER = (SDL_FColor){ 0.32f, 0.32f, 0.32f, 1 };
		C_BUTTON = (SDL_FColor){ 0.23f, 0.23f, 0.23f, 1 };
		C_BUTTON_HOVER = (SDL_FColor){ 0.28f, 0.28f, 0.28f, 1 };
		C_BUTTON_DOWN = (SDL_FColor){ 0.19f, 0.19f, 0.19f, 1 };
		C_SELECTION = (SDL_FColor){ 0.21f, 0.52f, 0.89f, 0.45f };
		C_TRACK = (SDL_FColor){ 0.30f, 0.30f, 0.30f, 1 };
		C_SHADOW = (SDL_FColor){ 0, 0, 0, 0.40f };
		C_BAR = (SDL_FColor){ 0.18f, 0.18f, 0.18f, 1 };
	}
}

/* what the desktop says, unless CABBY_THEME says otherwise (a test script
 * is always light, so it looks the same everywhere) */
static void choose_look(void)
{
	const char *t = getenv("CABBY_THEME");
	if (t && !strcmp(t, "dark"))
		set_look(1);
	else if (t && !strcmp(t, "light"))
		set_look(0);
	else if (getenv("CABBY_GUI_SCRIPT"))
		set_look(0);
	else
		set_look(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
}

static int ui_font = -1;

static int uifont(void)
{
	if (ui_font < 0)
		ui_font = font_for("Sans", 0, 0);
	return ui_font;
}

/* ------------------------------------------------------------- items */

enum {
	K_WINDOW = 1, K_BOX, K_LABEL, K_BUTTON, K_ENTRY, K_CHECK, K_CHOICE, K_TEXT_AREA, K_LIST, K_SLIDER,
	K_PROGRESS, K_IMAGE, K_SEPARATOR, K_CANVAS, K_MENU, K_MENU_ITEM, K_SHORTCUT, K_TABS, K_TABLE, K_NUMBER,
	K_COLOR, K_FONT, K_VIDEO
};

typedef struct TRow {
	char **cell;
	int n;
} TRow;

typedef struct Timer {
	Uint64 next, every;   /* milliseconds (every 0: once) */
	Handler h;
	int alive;
} Timer;

/* a text as it was, to go back to */
typedef struct Snap {
	char *text;
	int cursor, anchor;
} Snap;

typedef struct Item {
	int kind;
	int64_t win;          /* the window it's in */
	int64_t parent;       /* the box it's in */
	int64_t *kids;        /* boxes: what's in them, in order */
	int nkids;
	int is_row;
	int x, y, w, h;       /* where it was placed, in the window */
	int min_w, min_h;     /* set_min_size (-1: none) */
	int hidden, disabled;
	char *text;           /* labels, buttons, text boxes, check boxes, text areas */
	char *cache;          /* the last text handed out */
	int checked;
	int value, min, max;  /* sliders */
	double fraction;      /* progress bars */
	char **opts;          /* drop-downs and lists */
	int nopts, selected;
	double scroll;        /* lists and text areas: pixels scrolled down; text boxes: across */
	int cursor, anchor;   /* text boxes and text areas: bytes */
	struct Snap *undo, *redo;   /* text boxes and text areas: what Ctrl+Z goes back to */
	int nundo, nredo;
	int typing_end;       /* where the last letter typed went (-1: the last change wasn't typing) */
	int read_only;
	char *described;      /* set_description: what a screen reader says it is */
	int picture;          /* images: (index; -1: none) */
	Handler on_click, on_change, on_enter, on_toggle, on_select;
	int has_ink, has_paper;   /* colors of the program's own (set_text_color, set_background) */
	SDL_FColor ink, paper;
	/* canvases (and a window's own keys: keyed and key) */
	int cw, ch;
	Handler draw, clicked, moved, keyed;
	int mouse_x, mouse_y;
	char key[32];
	/* windows */
	SDL_Window *sw;
	SDL_Renderer *r;
	char *title;
	int width, height;    /* asked for with set_size */
	int64_t box, focus, hover, pressed, popup;
	int open, destroyed, closing, dirty;
	Timer *timers;
	int ntimers;
	char *chosen;
	float density;
	int dragging;         /* the mouse button is down on `pressed` */
	int dialog;           /* windows: a message or question showing over them (D_...) */
	char *dialog_text;
	Handler dialog_h;
	int dialog_button;    /* the button with the keys: 0 the first */
	int64_t *menus;       /* windows: their menus, in order on the bar */
	int nmenus;
	/* menus: their items (kids); menu items and shortcuts */
	double bar_x, bar_w;  /* menus: where their title is on the bar */
	char keys[48];        /* the shortcut, written one way: "ctrl+shift+s" ("": none) */
	char *keys_label;     /* as it was given: "Ctrl+Shift+S" */
	int separator;        /* a menu's dividing line */
	int64_t in_menu;
	/* scrolling columns */
	int scrolls, content_h;
	/* tables: the rows as added, the order they're shown in, the column
	 * they're sorted by */
	TRow *rows;
	int nrows, *order, sort_col, sort_down, adding;
	/* videos: what's shown (the videos library's, by video_source.h), and
	 * the picture of it last drawn */
	CbyVideoSource *video;
	SDL_Texture *vtex;
	SDL_Renderer *vtex_r;
	int vtw, vth, controls;
	int64_t vserial, vends;
	SDL_AudioStream *vsound;
	Handler ended;
} Item;

/* (videos: below) */
static void videos_end(int64_t win);
static void draw_video(int64_t id, struct Item *it, int focused);
static void video_click(struct Item *it, double mx, double my);
static int video_key(struct Item *it, SDL_Keycode k);
static int videos_tick(int64_t win);

static Item *items;
static int nitems;

static Item *item(int64_t id, int kind)
{
	if (id < 1 || id > nitems)
		fail("%s", "this widget hasn't been added to a window yet");
	Item *it = &items[id - 1];
	if (kind && it->kind != kind)
		fail("%s", "this isn't that kind of widget");
	return it;
}

static int64_t add_item(int kind, int64_t win, int64_t parent)
{
	items = realloc(items, sizeof *items * (size_t)(nitems + 1));
	items[nitems] = (Item){ .kind = kind, .win = win, .parent = parent, .min_w = -1, .min_h = -1, .picture = -1,
				.selected = -1, .sort_col = -1 };
	int64_t id = ++nitems;
	if (parent) {
		Item *p = &items[parent - 1];
		p->kids = realloc(p->kids, sizeof *p->kids * (size_t)(p->nkids + 1));
		p->kids[p->nkids++] = id;
	}
	return id;
}

static int window_alive(int64_t win) { return win >= 1 && win <= nitems && !items[win - 1].destroyed; }

static void dirty(int64_t id)
{
	if (id >= 1 && id <= nitems) {
		int64_t w = items[id - 1].win;
		if (w >= 1 && w <= nitems)
			items[w - 1].dirty = 1;
	}
}

static void call(Handler h, int64_t win)
{
	if (h.fn && window_alive(win))
		invoke(h);
	dirty(win);
}

static void set_str(char **s, const char *v)
{
	char *n = strdup(v ? v : "");
	free(*s);
	*s = n;
}

/* Text is handed to Cabby a byte at a time: its length, then each byte. */
static int64_t hand_out(Item *it, const char *t)
{
	set_str(&it->cache, t);
	return (int64_t)strlen(it->cache);
}

static int scripted(void) { return getenv("CABBY_GUI_SCRIPT") != NULL; }

/* ------------------------------------------------------------- shapes */

static void fill_rect(double x, double y, double w, double h, SDL_FColor c)
{
	SDL_SetRenderDrawColorFloat(renderer, c.r, c.g, c.b, c.a);
	SDL_FRect r = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderFillRect(renderer, &r);
}

static void fill_round(double x, double y, double w, double h, double r, SDL_FColor c)
{
	if (r * 2 > h)
		r = h / 2;
	if (r * 2 > w)
		r = w / 2;
	fill_rect(x + r, y, w - 2 * r, h, c);
	fill_rect(x, y + r, r, h - 2 * r, c);
	fill_rect(x + w - r, y + r, r, h - 2 * r, c);
	if (r > 0) {
		/* the corners: quarter circles */
		double cx[4] = { x + r, x + w - r, x + w - r, x + r }, cy[4] = { y + r, y + r, y + h - r, y + h - r };
		double from[4] = { M_PI, 1.5 * M_PI, 0, 0.5 * M_PI };
		for (int k = 0; k < 4; k++) {
			int n = 8;
			float xy[2 * 10];
			int ix[3 * 8];
			xy[0] = (float)cx[k];
			xy[1] = (float)cy[k];
			for (int i = 0; i <= n; i++) {
				double a = from[k] + 0.5 * M_PI * i / n;
				xy[2 + 2 * i] = (float)(cx[k] + r * cos(a));
				xy[3 + 2 * i] = (float)(cy[k] + r * sin(a));
			}
			for (int i = 0; i < n; i++) {
				ix[3 * i] = 0;
				ix[3 * i + 1] = 1 + i;
				ix[3 * i + 2] = 2 + i;
			}
			triangles(xy, n + 2, ix, n * 3, c);
		}
	}
}

/* a rounded box with a border: the border's color, then the inside */
static void boxed(double x, double y, double w, double h, double r, SDL_FColor border, SDL_FColor inside, double bw)
{
	fill_round(x, y, w, h, r, border);
	fill_round(x + bw, y + bw, w - 2 * bw, h - 2 * bw, r > bw ? r - bw : 0, inside);
}

/* just the edge of a rounded box, bw wide (for showing where the keys go) */
static void outline(double x, double y, double w, double h, double r, SDL_FColor c, double bw)
{
	fill_rect(x + r, y, w - 2 * r, bw, c);
	fill_rect(x + r, y + h - bw, w - 2 * r, bw, c);
	fill_rect(x, y + r, bw, h - 2 * r, c);
	fill_rect(x + w - bw, y + r, bw, h - 2 * r, c);
	double rr = r - bw / 2;
	ring(x + r, y + r, rr, rr, bw, M_PI, 1.5 * M_PI, c);
	ring(x + w - r, y + r, rr, rr, bw, 1.5 * M_PI, 2 * M_PI, c);
	ring(x + w - r, y + h - r, rr, rr, bw, 0, 0.5 * M_PI, c);
	ring(x + r, y + h - r, rr, rr, bw, 0.5 * M_PI, M_PI, c);
}

/* clipping, inside what's clipped already (a scrolling column's) */
static SDL_Rect clips[16];
static int nclips;

static void clip(int x, int y, int w, int h)
{
	SDL_Rect r = { x, y, w < 0 ? 0 : w, h < 0 ? 0 : h };
	if (nclips > 0) {
		SDL_Rect in;
		if (!SDL_GetRectIntersection(&r, &clips[nclips - 1], &in))
			in = (SDL_Rect){ x, y, 0, 0 };
		r = in;
	}
	if (nclips < 16)
		clips[nclips++] = r;
	SDL_SetRenderClipRect(renderer, &r);
}

static void unclip(void)
{
	if (nclips > 0)
		nclips--;
	SDL_SetRenderClipRect(renderer, nclips ? &clips[nclips - 1] : NULL);
}

/* ------------------------------------------------------------- text, in lines */

static double lh(void) { return line_height(uifont(), UI_SIZE); }
static double tw(const char *s) { return text_width(uifont(), UI_SIZE, s); }
static double twn(const char *s, int n) { return text_width_n(uifont(), UI_SIZE, s, n); }

/* where the text cursor goes before byte `at` of a line (its first n
 * bytes; n < 0: all of it): in Hebrew or Arabic, from the right */
static double tcx(const char *s, int n, int at) { return text_caret(uifont(), UI_SIZE, s, n, at); }

/* the widest of the text's lines, and how many there are */
static void text_size(const char *s, double *w, int *lines)
{
	*w = 0;
	*lines = 1;
	const char *start = s;
	for (const char *p = s;; p++)
		if (*p == '\n' || !*p) {
			double lw = twn(start, (int)(p - start));
			if (lw > *w)
				*w = lw;
			if (!*p)
				break;
			(*lines)++;
			start = p + 1;
		}
}

static void draw_lines(const char *s, double x, double y, SDL_FColor c)
{
	const char *start = s;
	for (const char *p = s;; p++)
		if (*p == '\n' || !*p) {
			draw_text_n(uifont(), UI_SIZE, x, y, start, (int)(p - start), c);
			if (!*p)
				break;
			y += lh();
			start = p + 1;
		}
}

/* A text area's text, wrapped to a width: where each shown line starts and
 * ends (bytes). Long lines break at spaces, or anywhere a word won't fit. */
typedef struct Wrapped {
	int *start, *end;
	int n;
} Wrapped;

static Wrapped wrap(const char *s, double width)
{
	Wrapped w = { 0 };
	int cap = 0, len = (int)strlen(s), i = 0;
	for (;;) {
		int line_end = i;
		while (line_end < len && s[line_end] != '\n')
			line_end++;
		int a = i;
		do {
			/* as much of a..line_end as fits */
			int b = a, last_space = -1;
			while (b < line_end) {
				int nb = b + 1;
				while (nb < line_end && (s[nb] & 0xC0) == 0x80)
					nb++;
				if (twn(s + a, nb - a) > width && b > a)
					break;
				if (s[b] == ' ')
					last_space = b;
				b = nb;
			}
			if (b < line_end && last_space > a)
				b = last_space + 1;
			if (w.n == cap) {
				cap = cap ? cap * 2 : 16;
				w.start = realloc(w.start, sizeof *w.start * (size_t)cap);
				w.end = realloc(w.end, sizeof *w.end * (size_t)cap);
			}
			w.start[w.n] = a;
			w.end[w.n] = b;
			w.n++;
			a = b;
		} while (a < line_end);
		if (line_end >= len)
			break;
		i = line_end + 1;
		if (i == len) {   /* a new line at the very end: an empty line after it */
			if (w.n == cap) {
				cap = cap * 2;
				w.start = realloc(w.start, sizeof *w.start * (size_t)cap);
				w.end = realloc(w.end, sizeof *w.end * (size_t)cap);
			}
			w.start[w.n] = w.end[w.n] = len;
			w.n++;
			break;
		}
	}
	return w;
}

static void unwrap(Wrapped *w)
{
	free(w->start);
	free(w->end);
}

/* ------------------------------------------------------------- layout
 *
 * As GTK's boxes do it: a column gives everything its whole width and its
 * own height, and shares spare height out to what can grow taller (text
 * areas, lists); a row gives everything its own width and the row's
 * height, and shares spare width out to what can grow wider (text boxes,
 * sliders, progress bars). */

static double slider_value_h(void) { return lh() + 2; }

#define VIDEO_BAR 36   /* a video's controls, under it: their height */

static void natural(int64_t id, int *w, int *h)
{
	Item *it = &items[id - 1];
	double t;
	int lines;
	switch (it->kind) {
	case K_LABEL:
		text_size(it->text, &t, &lines);
		*w = (int)ceil(t);
		*h = (int)ceil(lines * lh());
		break;
	case K_BUTTON:
		*w = (int)ceil(tw(it->text)) + 32;
		*h = FIELD_H;
		break;
	case K_ENTRY:
		*w = 160;
		*h = FIELD_H;
		break;
	case K_CHECK:
		*w = 20 + 8 + (int)ceil(tw(it->text));
		*h = 24;
		break;
	case K_CHOICE: {
		double m = 40;
		for (int i = 0; i < it->nopts; i++)
			if (tw(it->opts[i]) > m)
				m = tw(it->opts[i]);
		*w = (int)ceil(m) + 52;
		*h = FIELD_H;
		break;
	}
	case K_TEXT_AREA:
		*w = 160;
		*h = 100;
		break;
	case K_LIST:
		*w = 160;
		*h = 120;
		break;
	case K_SLIDER:
		*w = 160;
		*h = (int)ceil(slider_value_h()) + 24;
		break;
	case K_PROGRESS:
		*w = 160;
		*h = 8;
		break;
	case K_VIDEO: {
		/* its picture's shape, up to 480 wide to begin with, and its
		 * controls under it */
		int vw = 0, vh = 0;
		if (!it->video->size(it->video->self, &vw, &vh) || vw < 1 || vh < 1) {
			vw = 320;
			vh = it->controls ? 0 : 40;
		}
		if (vw > 480) {
			vh = (int)lround((double)vh * 480 / vw);
			vw = 480;
		}
		*w = vw;
		*h = vh + (it->controls ? VIDEO_BAR : 0);
		break;
	}
	case K_IMAGE:
		if (it->picture >= 0) {
			*w = pictures[it->picture].w;
			*h = pictures[it->picture].h;
		} else {
			*w = *h = 0;
		}
		break;
	case K_SEPARATOR:
		*w = *h = 1;
		break;
	case K_CANVAS:
		*w = it->cw;
		*h = it->ch;
		break;
	case K_TABS: {
		int mw = 0, mh = 0;
		double strip = 0;
		for (int i = 0; i < it->nkids; i++) {
			int kw, kh;
			natural(it->kids[i], &kw, &kh);
			if (kw > mw) mw = kw;
			if (kh > mh) mh = kh;
			strip += tw(it->opts[i]) + 28;
		}
		*w = (int)fmax(mw + 2 * SPACING + 2, strip + 4);
		*h = mh + 2 * SPACING + TAB_H + 2;
		break;
	}
	case K_TABLE:
		*w = 0;
		for (int i = 0; i < it->nopts; i++)
			*w += (int)ceil(tw(it->opts[i])) + 40;
		if (*w < 200)
			*w = 200;
		*h = 160;
		break;
	case K_NUMBER: {
		char a[24], b[24];
		snprintf(a, sizeof a, "%d", it->min);
		snprintf(b, sizeof b, "%d", it->max);
		*w = (int)ceil(fmax(tw(a), tw(b))) + 24 + 2 * FIELD_H;
		*h = FIELD_H;
		break;
	}
	case K_COLOR:
		*w = 64;
		*h = FIELD_H;
		break;
	case K_FONT:
		*w = (int)ceil(tw(it->text)) + 32;
		*h = FIELD_H;
		break;
	case K_BOX: {
		int tw_ = 0, th = 0, n = 0;
		for (int i = 0; i < it->nkids; i++) {
			Item *k = &items[it->kids[i] - 1];
			if (k->hidden)
				continue;
			int kw, kh;
			natural(it->kids[i], &kw, &kh);
			if (it->is_row) {
				tw_ += kw;
				if (kh > th)
					th = kh;
			} else {
				th += kh;
				if (kw > tw_)
					tw_ = kw;
			}
			n++;
		}
		if (n > 1) {
			if (it->is_row)
				tw_ += SPACING * (n - 1);
			else
				th += SPACING * (n - 1);
		}
		*w = tw_;
		*h = th;
		if (it->scrolls) {   /* a scrolling column: as tall as asked, the rest scrolls */
			*w += 12 + 10;
			*h = 120;
		}
		break;
	}
	default:
		*w = *h = 0;
	}
	if (it->min_w > *w)
		*w = it->min_w;
	if (it->min_h > *h)
		*h = it->min_h;
}

static int grows_wide(int64_t id)
{
	Item *it = &items[id - 1];
	switch (it->kind) {
	case K_ENTRY: case K_TEXT_AREA: case K_LIST: case K_SLIDER: case K_PROGRESS: case K_TABS: case K_TABLE:
	case K_VIDEO:
		return 1;
	case K_BOX:
		if (it->is_row || it->scrolls)
			return 1;
		for (int i = 0; i < it->nkids; i++)
			if (!items[it->kids[i] - 1].hidden && grows_wide(it->kids[i]))
				return 1;
		return 0;
	}
	return 0;
}

static int grows_tall(int64_t id)
{
	Item *it = &items[id - 1];
	switch (it->kind) {
	case K_TEXT_AREA: case K_LIST: case K_TABS: case K_TABLE: case K_VIDEO:
		return 1;
	case K_BOX:
		if (it->scrolls)
			return 1;
		for (int i = 0; i < it->nkids; i++)
			if (!items[it->kids[i] - 1].hidden && grows_tall(it->kids[i]))
				return 1;
		return 0;
	}
	return 0;
}

/* things that keep their own width (and height, in a row): canvases,
 * pictures, number boxes, color and font buttons */
static int own_size(int kind)
{
	return kind == K_CANVAS || kind == K_IMAGE || kind == K_NUMBER || kind == K_COLOR || kind == K_FONT;
}

static void place(int64_t id, int x, int y, int w, int h);

static void place_kids(Item *it, int x, int y, int w, int h)
{
	int n = 0, natural_total = 0, growers = 0;
	for (int i = 0; i < it->nkids; i++) {
		int64_t k = it->kids[i];
		if (items[k - 1].hidden)
			continue;
		int kw, kh;
		natural(k, &kw, &kh);
		natural_total += it->is_row ? kw : kh;
		if (it->is_row ? grows_wide(k) : grows_tall(k))
			growers++;
		n++;
	}
	int spare = (it->is_row ? w : h) - natural_total - (n > 1 ? SPACING * (n - 1) : 0);
	if (spare < 0)
		spare = 0;
	int at = it->is_row ? x : y, given = 0;
	for (int i = 0; i < it->nkids; i++) {
		int64_t k = it->kids[i];
		Item *ki = &items[k - 1];
		if (ki->hidden)
			continue;
		int kw, kh;
		natural(k, &kw, &kh);
		int extra = 0;
		if (growers && (it->is_row ? grows_wide(k) : grows_tall(k))) {
			extra = spare / growers;
			if (++given == growers)
				extra = spare - extra * (growers - 1);   /* (the last takes what's left over) */
		}
		if (it->is_row) {
			int ch = own_size(ki->kind) ? kh : h;
			place(k, at, y, kw + extra, ch);
			at += kw + extra + SPACING;
		} else {
			int cw = own_size(ki->kind) ? kw : w;
			place(k, x, at, cw, kh + extra);
			at += kh + extra + SPACING;
		}
	}
}

static void place(int64_t id, int x, int y, int w, int h)
{
	Item *it = &items[id - 1];
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	if (it->kind == K_TABS) {
		/* the page showing, under the tabs; the others nowhere */
		for (int i = 0; i < it->nkids; i++) {
			if (i == it->selected)
				place(it->kids[i], x + 1 + SPACING, y + TAB_H + 1 + SPACING, w - 2 - 2 * SPACING,
				      h - TAB_H - 2 - 2 * SPACING);
			else
				items[it->kids[i] - 1].w = items[it->kids[i] - 1].h = 0;
		}
		return;
	}
	if (it->kind != K_BOX)
		return;
	if (it->scrolls) {
		int cw, ch;
		it->scrolls = 0;   /* (its natural size inside, not the size it shows) */
		natural(id, &cw, &ch);
		it->scrolls = 1;
		int inner = h - 12;
		it->content_h = ch;
		if (it->scroll > ch - inner)
			it->scroll = ch - inner;
		if (it->scroll < 0)
			it->scroll = 0;
		place_kids(it, x + 6, y + 6 - (int)it->scroll, w - 12 - 10, ch > inner ? ch : inner);
		return;
	}
	place_kids(it, x, y, w, h);
}

static int bar_h(int64_t win) { return items[win - 1].nmenus ? BAR_H : 0; }

static void layout(int64_t win)
{
	Item *w = &items[win - 1];
	int ww, wh;
	SDL_GetWindowSize(w->sw, &ww, &wh);
	int top = bar_h(win);
	place(w->box, MARGIN, top + MARGIN, ww - 2 * MARGIN, wh - top - 2 * MARGIN);
	double x = 6;
	for (int i = 0; i < w->nmenus; i++) {
		Item *m = &items[w->menus[i] - 1];
		m->bar_x = x;
		m->bar_w = tw(m->text) + 20;
		x += m->bar_w;
	}
}

/* ------------------------------------------------------------- windows */

int64_t cby_gui_window_new(void)
{
	int64_t id = add_item(K_WINDOW, 0, 0);
	items[id - 1].win = id;
	items[id - 1].width = 420;
	items[id - 1].height = 300;
	items[id - 1].title = strdup("");
	int64_t box = add_item(K_BOX, id, 0);
	items[id - 1].box = box;
	return id;
}

int64_t cby_gui_window_box(int64_t win) { return item(win, K_WINDOW)->box; }

void cby_gui_window_set_title(int64_t win, const char *title)
{
	Item *w = item(win, K_WINDOW);
	set_str(&w->title, title);
	if (w->sw)
		SDL_SetWindowTitle(w->sw, title);
}

void cby_gui_window_set_size(int64_t win, int width, int height)
{
	Item *w = item(win, K_WINDOW);
	w->width = width;
	w->height = height;
	if (w->sw)
		SDL_SetWindowSize(w->sw, width, height);
}

static void stop_timers(Item *w)
{
	for (int i = 0; i < w->ntimers; i++)
		w->timers[i].alive = 0;
}

void cby_gui_window_close(int64_t win)
{
	Item *w = item(win, K_WINDOW);
	if (w->open)
		w->closing = 1;
}

static void end_window(int64_t win)
{
	Item *w = &items[win - 1];
	videos_end(win);
	if (w->sw) {
		forget_textures(w->r);
		SDL_DestroyRenderer(w->r);
		SDL_DestroyWindow(w->sw);
		w->sw = NULL;
		w->r = NULL;
		/* Let the window system hear that it's gone, now: on macOS (and
		 * Wayland) it only goes when events are next looked at, and the
		 * program may not look again (the interactive session waits for
		 * typing instead), leaving a frozen window on the screen. */
		for (int i = 0; i < 10; i++) {
			SDL_PumpEvents();
			SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
			SDL_Delay(5);
		}
	}
	w->open = 0;
	w->destroyed = 1;
	stop_timers(w);
}

void cby_gui_window_destroy(int64_t win)
{
	Item *w = item(win, K_WINDOW);
	if (!w->destroyed)
		end_window(win);
}

/* ------------------------------------------------------------- adding */

static int64_t add_to(int64_t box, int kind, const char *text)
{
	Item *b = item(box, K_BOX);
	int64_t id = add_item(kind, b->win, box);
	items[id - 1].text = strdup(text ? text : "");
	items[id - 1].typing_end = -1;
	dirty(id);
	return id;
}

int64_t cby_gui_add_label(int64_t box, const char *text) { return add_to(box, K_LABEL, text); }
int64_t cby_gui_add_button(int64_t box, const char *text) { return add_to(box, K_BUTTON, text); }
int64_t cby_gui_add_entry(int64_t box) { return add_to(box, K_ENTRY, ""); }
int64_t cby_gui_add_check(int64_t box, const char *text) { return add_to(box, K_CHECK, text); }
int64_t cby_gui_add_choice(int64_t box) { return add_to(box, K_CHOICE, ""); }
int64_t cby_gui_add_text_area(int64_t box) { return add_to(box, K_TEXT_AREA, ""); }
int64_t cby_gui_add_list(int64_t box) { return add_to(box, K_LIST, ""); }
int64_t cby_gui_add_progress(int64_t box) { return add_to(box, K_PROGRESS, ""); }
void cby_gui_add_separator(int64_t box) { add_to(box, K_SEPARATOR, ""); }

static int64_t add_box(int64_t box, int row)
{
	int64_t id = add_to(box, K_BOX, "");
	items[id - 1].is_row = row;
	return id;
}

int64_t cby_gui_add_row(int64_t box) { return add_box(box, 1); }
int64_t cby_gui_add_column(int64_t box) { return add_box(box, 0); }

int64_t cby_gui_add_slider(int64_t box, int min, int max)
{
	int64_t id = add_to(box, K_SLIDER, "");
	Item *it = &items[id - 1];
	it->min = min < max ? min : max;
	it->max = min < max ? max : min;
	if (it->max == it->min)
		it->max = it->min + 1;
	it->value = it->min;
	return id;
}

int64_t cby_gui_add_image(int64_t box, const char *path)
{
	int64_t id = add_to(box, K_IMAGE, "");
	if (path && *path)
		items[id - 1].picture = picture(path);
	return id;
}

int64_t cby_gui_add_canvas(int64_t box, int width, int height)
{
	int64_t id = add_to(box, K_CANVAS, "");
	items[id - 1].cw = width < 1 ? 1 : width;
	items[id - 1].ch = height < 1 ? 1 : height;
	return id;
}

/* ------------------------------------------------------------- every widget */

void cby_gui_set_description(int64_t id, const char *text) { set_str(&item(id, 0)->described, text); }

void cby_gui_set_enabled(int64_t id, int on) { item(id, 0)->disabled = !on; dirty(id); }
void cby_gui_set_visible(int64_t id, int on) { item(id, 0)->hidden = !on; dirty(id); }

/* colors of the program's own ("": the usual again); rows, columns and
 * windows pass them on to what's in them */
void cby_gui_set_text_color(int64_t id, const char *c)
{
	Item *it = item(id, 0);
	it->has_ink = c && *c;
	if (it->has_ink)
		it->ink = fcolor(color(c));
	dirty(id);
}

void cby_gui_set_background(int64_t id, const char *c)
{
	Item *it = item(id, 0);
	it->has_paper = c && *c;
	if (it->has_paper)
		it->paper = fcolor(color(c));
	dirty(id);
}

/* the window's own keys: those nothing in it uses */
void cby_gui_window_on_key(int64_t win, void *obj, void *fn) { item(win, K_WINDOW)->keyed = (Handler){ obj, fn }; }
int64_t cby_gui_window_key_length(int64_t win) { return (int64_t)strlen(item(win, K_WINDOW)->key); }

char cby_gui_window_key_char(int64_t win, int64_t i)
{
	const char *t = item(win, K_WINDOW)->key;
	return i >= 0 && i < (int64_t)strlen(t) ? t[i] : 0;
}

void cby_gui_set_min_size(int64_t id, int width, int height)
{
	Item *it = item(id, 0);
	it->min_w = width;
	it->min_h = height;
	dirty(id);
}

static void forget_undo(Item *it);

static void changed(int64_t id)
{
	Item *it = &items[id - 1];
	call(it->on_change, it->win);
}

void cby_gui_set_text(int64_t id, const char *text)
{
	Item *it = item(id, 0);
	switch (it->kind) {
	case K_LABEL: case K_BUTTON: case K_CHECK:
		set_str(&it->text, text);
		break;
	case K_ENTRY: case K_TEXT_AREA:
		set_str(&it->text, text);
		forget_undo(it);
		it->cursor = it->anchor = (int)strlen(it->text);
		it->scroll = 0;
		changed(id);
		break;
	case K_WINDOW:
		cby_gui_window_set_title(id, text);
		break;
	}
	dirty(id);
}

int64_t cby_gui_text_length(int64_t id)
{
	Item *it = item(id, 0);
	switch (it->kind) {
	case K_CHOICE: case K_LIST: case K_TABS:
		return hand_out(it, it->selected >= 0 && it->selected < it->nopts ? it->opts[it->selected] : "");
	case K_TABLE:
		return hand_out(it, it->selected >= 0 ? (it->selected < it->nrows && it->rows[it->selected].n ? it->rows[it->selected].cell[0] : "") : "");
	case K_WINDOW:
		return hand_out(it, it->title);
	}
	return hand_out(it, it->text ? it->text : "");
}

char cby_gui_text_char(int64_t id, int64_t i)
{
	Item *it = item(id, 0);
	return it->cache && i >= 0 && i < (int64_t)strlen(it->cache) ? it->cache[i] : 0;
}

/* ------------------------------------------------------------- particular widgets */

int cby_gui_check_get(int64_t id) { return item(id, K_CHECK)->checked; }

void cby_gui_check_set(int64_t id, int on)
{
	Item *it = item(id, K_CHECK);
	if (it->checked != (on != 0)) {
		it->checked = on != 0;
		call(it->on_toggle, it->win);
	}
	dirty(id);
}

void cby_gui_choice_add(int64_t id, const char *text)
{
	Item *it = item(id, K_CHOICE);
	it->opts = realloc(it->opts, sizeof *it->opts * (size_t)(it->nopts + 1));
	it->opts[it->nopts++] = strdup(text);
	if (it->selected < 0) {   /* the first is chosen to begin with */
		it->selected = 0;
		changed(id);
	}
	dirty(id);
}

int cby_gui_selected(int64_t id)
{
	Item *it = item(id, 0);
	return it->kind == K_CHOICE || it->kind == K_LIST || it->kind == K_TABS || it->kind == K_TABLE ? it->selected : -1;
}

static void list_into_view(Item *it);
static void show_tab(int64_t id, int i);
static void select_row(int64_t id, int row);

void cby_gui_select(int64_t id, int index)
{
	Item *it = item(id, 0);
	if (it->kind == K_TABS) {
		show_tab(id, index);
		return;
	}
	if (it->kind == K_TABLE) {
		select_row(id, index);
		return;
	}
	if (it->kind != K_CHOICE && it->kind != K_LIST)
		return;
	if (index < 0 || index >= it->nopts)
		index = -1;
	if (index == it->selected)
		return;
	it->selected = index;
	if (it->kind == K_LIST) {
		list_into_view(it);
		call(it->on_select, it->win);
	} else {
		changed(id);
	}
	dirty(id);
}

int cby_gui_count(int64_t id) { return item(id, 0)->nopts; }

void cby_gui_list_add(int64_t id, const char *text)
{
	Item *it = item(id, K_LIST);
	it->opts = realloc(it->opts, sizeof *it->opts * (size_t)(it->nopts + 1));
	it->opts[it->nopts++] = strdup(text);
	dirty(id);
}

void cby_gui_list_clear(int64_t id)
{
	Item *it = item(id, K_LIST);
	for (int i = 0; i < it->nopts; i++)
		free(it->opts[i]);
	it->nopts = 0;
	it->scroll = 0;
	int had = it->selected >= 0;
	it->selected = -1;
	if (had)
		call(it->on_select, it->win);
	dirty(id);
}

void cby_gui_text_area_set_editable(int64_t id, int on) { item(id, K_TEXT_AREA)->read_only = !on; }

int cby_gui_slider_get(int64_t id) { return item(id, K_SLIDER)->value; }

static void set_slider(Item *it, int v)
{
	if (v < it->min)
		v = it->min;
	if (v > it->max)
		v = it->max;
	if (v != it->value) {
		it->value = v;
		call(it->on_change, it->win);
	}
	dirty((int64_t)(it - items) + 1);
}

void cby_gui_slider_set(int64_t id, int value) { set_slider(item(id, K_SLIDER), value); }

void cby_gui_progress_set(int64_t id, double fraction)
{
	Item *it = item(id, K_PROGRESS);
	it->fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
	dirty(id);
}

void cby_gui_image_set(int64_t id, const char *path)
{
	Item *it = item(id, K_IMAGE);
	it->picture = path && *path ? picture(path) : -1;
	dirty(id);
}

/* ------------------------------------------------------------- handlers */

void cby_gui_on_click(int64_t id, void *obj, void *fn) { item(id, K_BUTTON)->on_click = (Handler){ obj, fn }; }
void cby_gui_on_enter(int64_t id, void *obj, void *fn) { item(id, K_ENTRY)->on_enter = (Handler){ obj, fn }; }
void cby_gui_on_toggle(int64_t id, void *obj, void *fn) { item(id, K_CHECK)->on_toggle = (Handler){ obj, fn }; }
void cby_gui_on_select(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, 0);
	if (it->kind != K_LIST && it->kind != K_TABLE)
		fail("%s", "on_select works on lists and tables");
	it->on_select = (Handler){ obj, fn };
}

void cby_gui_on_change(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, 0);
	switch (it->kind) {
	case K_ENTRY: case K_TEXT_AREA: case K_CHOICE: case K_SLIDER: case K_TABS: case K_NUMBER: case K_COLOR: case K_FONT:
		it->on_change = (Handler){ obj, fn };
		break;
	default:
		fail("%s", "on_change works on text boxes, text areas, drop-down lists, sliders and tabs");
	}
}

/* ------------------------------------------------------------- timers
 *
 * By the clock; under a test script, by the script's own clock, which only
 * moves when it says (`wait 500`). */

static Uint64 fake_now;

static Uint64 now_ms(void) { return scripted() ? fake_now : SDL_GetTicks(); }

static void add_timer(int64_t win, int ms, int once, void *obj, void *fn)
{
	Item *w = item(win, K_WINDOW);
	if (ms < 1)
		ms = 1;
	w->timers = realloc(w->timers, sizeof *w->timers * (size_t)(w->ntimers + 1));
	w->timers[w->ntimers++] = (Timer){ now_ms() + (Uint64)ms, once ? 0 : (Uint64)ms, { obj, fn }, 1 };
}

void cby_gui_every(int64_t win, int ms, void *obj, void *fn) { add_timer(win, ms, 0, obj, fn); }
void cby_gui_after(int64_t win, int ms, void *obj, void *fn) { add_timer(win, ms, 1, obj, fn); }
void cby_gui_stop_timers(int64_t win) { stop_timers(item(win, K_WINDOW)); }

/* runs the timers that are due, in time order; the time of the next one,
 * or 0 if none is waiting */
static Uint64 run_timers(Uint64 now)
{
	for (;;) {
		int64_t best_w = 0;
		int best_i = -1;
		Uint64 best = 0;
		for (int64_t w = 1; w <= nitems; w++) {
			Item *it = &items[w - 1];
			if (it->kind != K_WINDOW || !it->open)
				continue;
			for (int i = 0; i < it->ntimers; i++)
				if (it->timers[i].alive && (best_i < 0 || it->timers[i].next < best)) {
					best = it->timers[i].next;
					best_w = w;
					best_i = i;
				}
		}
		if (best_i < 0)
			return 0;
		if (best > now)
			return best;
		Timer *t = &items[best_w - 1].timers[best_i];
		Handler h = t->h;
		if (t->every)
			t->next += t->every;
		else
			t->alive = 0;
		call(h, best_w);
	}
}

/* ------------------------------------------------------------- dialogs */

enum { D_NONE, D_ASK, D_OPEN, D_SAVE };
static struct {
	int kind;
	Handler h;
	int64_t win;
} pending;

static void set_chosen(int64_t win, const char *path)
{
	set_str(&item(win, K_WINDOW)->chosen, path);
}

/* A message, or a yes-or-no question, is drawn over the window itself
 * (as GTK's are, attached to it), and the program goes on: the question's
 * handler is called when it's answered yes. */
enum { D_MESSAGE = 10 };

static void open_dialog(int64_t win, int kind, const char *text, Handler h)
{
	Item *w = item(win, K_WINDOW);
	w->dialog = kind;
	set_str(&w->dialog_text, text);
	w->dialog_h = h;
	w->dialog_button = kind == D_ASK ? 1 : 0;   /* (Yes, or OK) */
	w->dirty = 1;
}

void cby_gui_show_message(int64_t win, const char *text)
{
	item(win, K_WINDOW);
	if (scripted())
		return;   /* nobody to read it */
	open_dialog(win, D_MESSAGE, text, (Handler){ 0 });
}

void cby_gui_ask(int64_t win, const char *question, void *obj, void *fn)
{
	item(win, K_WINDOW);
	if (scripted()) {
		pending.kind = D_ASK;
		pending.h = (Handler){ obj, fn };
		pending.win = win;
		return;
	}
	open_dialog(win, D_ASK, question, (Handler){ obj, fn });
}

/* the dialog answered: button 1 of a question is Yes */
static void answer_dialog(int64_t win, int button)
{
	Item *w = &items[win - 1];
	int kind = w->dialog;
	Handler h = w->dialog_h;
	w->dialog = 0;
	w->dirty = 1;
	if (kind == D_ASK && button == 1)
		call(h, win);
}

/* A file dialog answers later (perhaps from another thread): its answer
 * waits here, and the window's loop calls the handler. */
static SDL_Mutex *answer_lock;
static struct {
	int ready;
	char *path;
	Handler h;
	int64_t win;
} answer;
static Uint32 answer_event;

static void SDLCALL on_file(void *data, const char *const *files, int filter)
{
	(void)data;
	(void)filter;
	SDL_LockMutex(answer_lock);
	free(answer.path);
	answer.path = files && files[0] ? strdup(files[0]) : NULL;
	answer.ready = 1;
	SDL_UnlockMutex(answer_lock);
	SDL_Event e = { 0 };
	e.type = answer_event;
	SDL_PushEvent(&e);
}

static void file_dialog(int64_t win, int kind, const char *suggested, void *obj, void *fn)
{
	Item *w = item(win, K_WINDOW);
	if (scripted()) {
		pending.kind = kind;
		pending.h = (Handler){ obj, fn };
		pending.win = win;
		return;
	}
	if (!answer_lock) {
		answer_lock = SDL_CreateMutex();
		answer_event = SDL_RegisterEvents(1);
	}
	answer.h = (Handler){ obj, fn };
	answer.win = win;
	if (kind == D_OPEN)
		SDL_ShowOpenFileDialog(on_file, NULL, w->sw, NULL, 0, NULL, false);
	else
		SDL_ShowSaveFileDialog(on_file, NULL, w->sw, NULL, 0, suggested && *suggested ? suggested : NULL);
}

static void file_answered(void)
{
	SDL_LockMutex(answer_lock);
	char *path = answer.ready ? answer.path : NULL;
	answer.path = NULL;
	answer.ready = 0;
	SDL_UnlockMutex(answer_lock);
	if (path && window_alive(answer.win)) {
		set_chosen(answer.win, path);
		call(answer.h, answer.win);
	}
	free(path);
}

void cby_gui_ask_open_file(int64_t win, void *obj, void *fn) { file_dialog(win, D_OPEN, NULL, obj, fn); }
void cby_gui_ask_save_file(int64_t win, const char *suggested, void *obj, void *fn)
{
	file_dialog(win, D_SAVE, suggested, obj, fn);
}

int64_t cby_gui_chosen_length(int64_t win)
{
	Item *w = item(win, K_WINDOW);
	return hand_out(w, w->chosen ? w->chosen : "");
}

char cby_gui_chosen_char(int64_t win, int64_t i) { return cby_gui_text_char(win, i); }

/* ------------------------------------------------------------- canvases
 *
 * A canvas is drawn by its on_draw handler whenever its window is drawn.
 * The drawing calls work only while that handler runs, on the canvas's
 * own part of the window (0, 0 is its top left corner). Each drawing
 * starts on white, in black, with lines 1 pixel wide. */

static int64_t drawing;        /* the canvas being drawn (0: none) */
static SDL_FColor pen;
static float pen_width = 1, font_size = 14;
static int pen_font;

static Item *pen_for(int64_t id)
{
	Item *it = item(id, K_CANVAS);
	if (drawing != id || !renderer)
		fail("%s", "a canvas can only be drawn on in its on_draw handler (to draw again, call its redraw)");
	return it;
}

/* Draws the canvas on its own texture, `scale` pixels for each of its
 * own, kept with it to put in the window (and to save). */
static SDL_Texture *paint_canvas(int64_t id, float scale)
{
	Item *it = &items[id - 1];
	int pw = (int)ceil(it->cw * scale), ph = (int)ceil(it->ch * scale);
	SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, pw, ph);
	if (!t)
		return NULL;
	SDL_Texture *was_t = SDL_GetRenderTarget(renderer);
	float was_d = density;
	SDL_SetRenderTarget(renderer, t);
	SDL_SetRenderScale(renderer, scale, scale);
	density = scale;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	fill_rect(0, 0, it->cw, it->ch, C_WHITE);
	pen = (SDL_FColor){ 0, 0, 0, 1 };
	pen_width = 1;
	font_size = 14;
	pen_font = font_for("Sans", 0, 0);
	int64_t was = drawing;
	drawing = id;
	Handler h = it->draw;
	if (h.fn)
		invoke(h);
	drawing = was;
	SDL_SetRenderTarget(renderer, was_t);
	density = was_d;
	return t;
}

int cby_gui_canvas_width(int64_t id) { return item(id, K_CANVAS)->cw; }
int cby_gui_canvas_height(int64_t id) { return item(id, K_CANVAS)->ch; }
void cby_gui_canvas_on_draw(int64_t id, void *obj, void *fn) { item(id, K_CANVAS)->draw = (Handler){ obj, fn }; dirty(id); }
void cby_gui_canvas_redraw(int64_t id) { item(id, K_CANVAS); dirty(id); }
void cby_gui_canvas_set_color(int64_t id, const char *c) { pen_for(id); pen = fcolor(color(c)); }

void cby_gui_canvas_fill_with(int64_t id, const char *c)
{
	Item *it = pen_for(id);
	fill_rect(0, 0, it->cw, it->ch, fcolor(color(c)));
}

void cby_gui_canvas_set_line_width(int64_t id, double w) { pen_for(id); pen_width = (float)(w > 0 ? w : 1); }
void cby_gui_canvas_set_font_size(int64_t id, double size) { pen_for(id); font_size = (float)(size > 0 ? size : 14); }

void cby_gui_canvas_set_font(int64_t id, const char *spec)
{
	pen_for(id);
	double size;
	pen_font = font_named(spec, &size);
	if (size > 0)
		font_size = (float)size;
}

void cby_gui_canvas_line(int64_t id, double x1, double y1, double x2, double y2)
{
	pen_for(id);
	thick_line(x1, y1, x2, y2, pen_width, pen);
}

void cby_gui_canvas_rectangle(int64_t id, double x, double y, double w, double h, int filled)
{
	pen_for(id);
	if (filled) {
		fill_rect(x, y, w, h, pen);
		return;
	}
	double pw = pen_width / 2;
	fill_rect(x - pw, y - pw, w + 2 * pw, pen_width, pen);
	fill_rect(x - pw, y + h - pw, w + 2 * pw, pen_width, pen);
	fill_rect(x - pw, y + pw, pen_width, h - 2 * pw, pen);
	fill_rect(x + w - pw, y + pw, pen_width, h - 2 * pw, pen);
}

void cby_gui_canvas_ellipse(int64_t id, double x, double y, double rx, double ry, int filled)
{
	pen_for(id);
	if (rx <= 0 || ry <= 0)
		return;
	if (filled)
		fill_ellipse(x, y, rx, ry, pen);
	else
		ring(x, y, rx, ry, pen_width, 0, 2 * M_PI, pen);
}

void cby_gui_canvas_arc(int64_t id, double x, double y, double r, double from, double to)
{
	pen_for(id);
	if (r <= 0)
		return;
	while (to < from)
		to += 360;
	ring(x, y, r, r, pen_width, (from - 90) * M_PI / 180, (to - 90) * M_PI / 180, pen);
}

static double *corners;
static int ncorners, capcorners;

void cby_gui_canvas_corner(int64_t id, double x, double y)
{
	(void)id;
	if (ncorners == capcorners) {
		capcorners = capcorners ? capcorners * 2 : 16;
		corners = realloc(corners, sizeof *corners * (size_t)capcorners * 2);
	}
	corners[2 * ncorners] = x;
	corners[2 * ncorners + 1] = y;
	ncorners++;
}

void cby_gui_canvas_polygon(int64_t id, int filled, int64_t numbers)
{
	int n = ncorners;
	ncorners = 0;
	pen_for(id);
	if (numbers % 2 != 0 || numbers < 6)
		fail("%s", numbers % 2 != 0 ? "a polygon's corners come in pairs: {x, y, x, y, ...}"
					    : "a polygon needs at least 3 corners: {x, y, x, y, x, y}");
	if (filled)
		fill_polygon(corners, n, pen);
	else
		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			thick_line(corners[2 * i], corners[2 * i + 1], corners[2 * j], corners[2 * j + 1], pen_width, pen);
		}
}

void cby_gui_canvas_text(int64_t id, double x, double y, const char *text)
{
	pen_for(id);
	draw_text(pen_font, font_size, x, y, text, pen);
}

void cby_gui_canvas_image(int64_t id, double x, double y, const char *path)
{
	pen_for(id);
	int pic = picture(path);
	SDL_Texture *t = picture_texture(pic);
	SDL_FRect r = { (float)x, (float)y, (float)pictures[pic].w, (float)pictures[pic].h };
	if (t)
		SDL_RenderTexture(renderer, t, NULL, &r);
}

/* a picture of the images library's (runtime/images.c), drawn: a texture
 * kept for each picture shown, its pixels sent again only once they've
 * changed, so one shown every frame costs no more than a file's */
extern const uint8_t *cby_image_pixels(int64_t id, int *w, int *h, uint64_t *serial, uint64_t *version)
	__attribute__((weak));

typedef struct {
	uint64_t serial, version, used;
	SDL_Renderer *owner;
	SDL_Texture *texture;
	int w, h;
} PictureShown;

static PictureShown pictures_shown[64];
static uint64_t pictures_clock;

void cby_gui_canvas_picture(int64_t id, double x, double y, int64_t pic)
{
	pen_for(id);
	int w = 0, h = 0;
	uint64_t serial = 0, version = 0;
	const uint8_t *px = cby_image_pixels ? cby_image_pixels(pic, &w, &h, &serial, &version) : NULL;
	if (!px)
		fail("%s", "that picture is empty: it was handed on, or never made");
	PictureShown *s = NULL, *oldest = &pictures_shown[0];
	for (int i = 0; i < 64; i++) {
		if (pictures_shown[i].texture && pictures_shown[i].serial == serial && pictures_shown[i].owner == renderer) {
			s = &pictures_shown[i];
			break;
		}
		if (pictures_shown[i].used < oldest->used)
			oldest = &pictures_shown[i];
	}
	if (s && (s->w != w || s->h != h)) {
		SDL_DestroyTexture(s->texture);
		s->texture = NULL;
	}
	if (!s || !s->texture) {
		if (!s) {
			s = oldest;
			if (s->texture && s->owner == renderer)
				SDL_DestroyTexture(s->texture);   /* (another window's goes with its window) */
		}
		*s = (PictureShown){ serial, version - 1, 0, renderer, NULL, w, h };
		s->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
		if (!s->texture)
			return;
		SDL_SetTextureBlendMode(s->texture, SDL_BLENDMODE_BLEND);
	}
	if (s->version != version) {
		SDL_UpdateTexture(s->texture, NULL, px, w * 4);
		s->version = version;
	}
	s->used = ++pictures_clock;
	SDL_FRect r = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderTexture(renderer, s->texture, NULL, &r);
}

/* what on_draw draws, saved as a PNG file */
void cby_gui_canvas_save(int64_t id, const char *path)
{
	Item *it = item(id, K_CANVAS);
	if (drawing)
		fail("%s", "a canvas can't be saved while it's being drawn (save it from another handler)");
	Item *w = &items[it->win - 1];
	if (!w->r)
		fail("%s", "a canvas can only be saved while its window is open");
	SDL_Renderer *was_r = renderer;
	renderer = w->r;
	SDL_Texture *t = paint_canvas(id, 1);
	int ok = 0;
	if (t) {
		SDL_Texture *was_t = SDL_GetRenderTarget(renderer);
		SDL_SetRenderTarget(renderer, t);
		SDL_Surface *s = SDL_RenderReadPixels(renderer, NULL);
		SDL_SetRenderTarget(renderer, was_t);
		SDL_DestroyTexture(t);
		if (s) {
			ok = save_png(s, path);
			SDL_DestroySurface(s);
		}
	}
	renderer = was_r;
	if (!ok)
		fail("can't save the canvas as '%s'", path);
}

int cby_gui_canvas_mouse_x(int64_t id) { return item(id, K_CANVAS)->mouse_x; }
int cby_gui_canvas_mouse_y(int64_t id) { return item(id, K_CANVAS)->mouse_y; }

int64_t cby_gui_canvas_key_length(int64_t id)
{
	Item *it = item(id, K_CANVAS);
	return hand_out(it, it->key);
}

void cby_gui_canvas_on_click(int64_t id, void *obj, void *fn) { item(id, K_CANVAS)->clicked = (Handler){ obj, fn }; }
void cby_gui_canvas_on_mouse_move(int64_t id, void *obj, void *fn) { item(id, K_CANVAS)->moved = (Handler){ obj, fn }; }
void cby_gui_canvas_on_key(int64_t id, void *obj, void *fn) { item(id, K_CANVAS)->keyed = (Handler){ obj, fn }; }

static void canvas_event(int64_t id, int which, double x, double y)
{
	Item *it = &items[id - 1];
	if (which != 2) {
		it->mouse_x = (int)floor(x);
		it->mouse_y = (int)floor(y);
	}
	call(which == 0 ? it->clicked : which == 1 ? it->moved : it->keyed, it->win);
}

static void canvas_key(int64_t id, const char *name)
{
	snprintf(items[id - 1].key, sizeof items[id - 1].key, "%s", name);
	canvas_event(id, 2, 0, 0);
}

/* ------------------------------------------------------------- menus and shortcuts
 *
 * A window's menus are on a bar along its top; each opens a list of its
 * items when clicked (or with F10). An item, or a shortcut on its own,
 * runs its handler when its keys are pressed anywhere in the window. */

int64_t cby_gui_add_menu(int64_t win, const char *title)
{
	Item *w = item(win, K_WINDOW);
	int64_t id = add_item(K_MENU, win, 0);
	items[id - 1].text = strdup(title);
	w = &items[win - 1];
	w->menus = realloc(w->menus, sizeof *w->menus * (size_t)(w->nmenus + 1));
	w->menus[w->nmenus++] = id;
	w->dirty = 1;
	return id;
}

/* shortcuts shown as a Mac shows them (⇧⌘S), on a Mac, or when
 * CABBY_DESKTOP_AS=macos says to (for the tests) */
static int shown_as_mac(void)
{
	const char *as = getenv("CABBY_DESKTOP_AS");
	if (as && !strcmp(as, "macos"))
		return 1;
	if (as && !strcmp(as, "linux"))
		return 0;
#if defined(__APPLE__)
	return 1;
#else
	return 0;
#endif
}

/* "ctrl+shift+s" as a Mac's menus write it: ⇧⌘S. Ctrl is Cmd there (as
 * the keys are), Alt is Option, and the modifiers go in the Mac's own
 * order, then the key. */
static void mac_label(const char *norm, char *out, size_t size)
{
	static const struct { const char *name, *shown; } keys[] = {
		{ "enter", "↩" }, { "return", "↩" }, { "escape", "⎋" }, { "tab", "⇥" }, { "backspace", "⌫" },
		{ "delete", "⌦" }, { "up", "↑" }, { "down", "↓" }, { "left", "←" }, { "right", "→" },
		{ "home", "↖" }, { "end", "↘" }, { "page_up", "⇞" }, { "page_down", "⇟" }, { "space", "Space" },
		{ "plus", "+" }, { "minus", "-" },
	};
	int shift = 0, alt = 0, cmd = 0;
	const char *key = norm;
	for (;;) {
		if (!strncmp(key, "ctrl+", 5)) { cmd = 1; key += 5; }
		else if (!strncmp(key, "shift+", 6)) { shift = 1; key += 6; }
		else if (!strncmp(key, "alt+", 4)) { alt = 1; key += 4; }
		else break;
	}
	char k[48];
	snprintf(k, sizeof k, "%s", key);
	for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
		if (!strcmp(key, keys[i].name))
			snprintf(k, sizeof k, "%s", keys[i].shown);
	if (k[0] >= 'a' && k[0] <= 'z' && (!k[1] || (k[0] == 'f' && k[1] >= '1' && k[1] <= '9')))
		k[0] = (char)(k[0] - 'a' + 'A');   /* (S, and F5) */
	snprintf(out, size, "%s%s%s%s", alt ? "⌥" : "", shift ? "⇧" : "", cmd ? "⌘" : "", k);
}

static void shortcut_keys(Item *it, const char *keys)
{
	it->keys[0] = '\0';
	set_str(&it->keys_label, keys);
	if (keys && *keys && !parse_keys(keys, it->keys, sizeof it->keys))
		fail("'%s' isn't a shortcut: write it like \"Ctrl+S\", \"Ctrl+Shift+Z\", \"Alt+X\" or \"F5\"", keys);
	if (keys && *keys && shown_as_mac()) {
		char shown[64];
		mac_label(it->keys, shown, sizeof shown);
		set_str(&it->keys_label, shown);
	}
}

int64_t cby_gui_menu_add_item(int64_t menu, const char *label, const char *keys, void *obj, void *fn)
{
	Item *m = item(menu, K_MENU);
	int64_t win = m->win;
	char norm[48];
	if (keys && *keys && !parse_keys(keys, norm, sizeof norm))
		fail("'%s' isn't a shortcut: write it like \"Ctrl+S\", \"Ctrl+Shift+Z\", \"Alt+X\" or \"F5\"", keys);
	int64_t id = add_item(K_MENU_ITEM, win, menu);
	Item *it = &items[id - 1];
	it->text = strdup(label);
	it->in_menu = menu;
	it->on_click = (Handler){ obj, fn };
	shortcut_keys(it, keys);
	return id;
}

void cby_gui_menu_add_separator(int64_t menu)
{
	Item *m = item(menu, K_MENU);
	int64_t id = add_item(K_MENU_ITEM, m->win, menu);
	items[id - 1].separator = 1;
	items[id - 1].text = strdup("");
	items[id - 1].in_menu = menu;
}

void cby_gui_menu_item_set_enabled(int64_t id, int on)
{
	item(id, K_MENU_ITEM)->disabled = !on;
	dirty(id);
}

void cby_gui_add_shortcut(int64_t win, const char *keys, void *obj, void *fn)
{
	item(win, K_WINDOW);
	char norm[48];
	if (!parse_keys(keys, norm, sizeof norm))
		fail("'%s' isn't a shortcut: write it like \"Ctrl+S\", \"Ctrl+Shift+Z\", \"Alt+X\" or \"F5\"", keys);
	int64_t id = add_item(K_SHORTCUT, win, 0);
	items[id - 1].on_click = (Handler){ obj, fn };
	shortcut_keys(&items[id - 1], keys);
}

/* runs the menu item or shortcut for these keys ("ctrl+s"); 1 if there was one */
static int run_shortcut(int64_t win, const char *norm)
{
	for (int i = 0; i < nitems; i++) {
		Item *it = &items[i];
		if ((it->kind == K_MENU_ITEM || it->kind == K_SHORTCUT) && it->win == win && it->keys[0] &&
		    !strcmp(it->keys, norm)) {
			if (!it->disabled)
				call(it->on_click, win);
			return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------- tabs and scrolling columns */

int64_t cby_gui_add_tabs(int64_t box) { return add_to(box, K_TABS, ""); }

int64_t cby_gui_tabs_add_tab(int64_t tabs, const char *title)
{
	Item *t = item(tabs, K_TABS);
	int64_t win = t->win;
	t->opts = realloc(t->opts, sizeof *t->opts * (size_t)(t->nopts + 1));
	t->opts[t->nopts++] = strdup(title);
	int64_t page = add_item(K_BOX, win, tabs);
	t = &items[tabs - 1];
	if (t->selected < 0)
		t->selected = 0;   /* (the first page shows, quietly) */
	dirty(tabs);
	return page;
}

static void show_tab(int64_t id, int i)
{
	Item *t = &items[id - 1];
	if (i < 0 || i >= t->nopts || i == t->selected)
		return;
	t->selected = i;
	call(t->on_change, t->win);
}

int64_t cby_gui_add_scrolling_column(int64_t box)
{
	int64_t id = add_to(box, K_BOX, "");
	items[id - 1].scrolls = 1;
	return id;
}

/* ------------------------------------------------------------- tables
 *
 * Rows count from 0 in the order they were added; sorting only changes
 * the order they're shown in (`order`), so `selected` and `cell` always
 * mean the same row. */

int64_t cby_gui_add_table(int64_t box) { return add_to(box, K_TABLE, ""); }

void cby_gui_table_heading(int64_t id, const char *heading)
{
	Item *t = item(id, K_TABLE);
	t->opts = realloc(t->opts, sizeof *t->opts * (size_t)(t->nopts + 1));
	t->opts[t->nopts++] = strdup(heading);
	dirty(id);
}

static const char *cell(Item *t, int row, int col)
{
	return row >= 0 && row < t->nrows && col >= 0 && col < t->rows[row].n ? t->rows[row].cell[col] : "";
}

static int compare_text(const char *a, const char *b)
{
	char *ea, *eb;
	double x = strtod(a, &ea), y = strtod(b, &eb);
	if (*a && *b && !*ea && !*eb)   /* numbers as numbers */
		return x < y ? -1 : x > y;
	return strcasecmp(a, b);
}

static Item *sorting;

static int compare_rows(const void *pa, const void *pb)
{
	int a = *(const int *)pa, b = *(const int *)pb;
	int c = compare_text(cell(sorting, a, sorting->sort_col), cell(sorting, b, sorting->sort_col));
	if (sorting->sort_down)
		c = -c;
	return c ? c : a - b;   /* (rows that are the same stay in the order they were added) */
}

/* the order the rows are shown in, again */
static void table_order(Item *t)
{
	t->order = realloc(t->order, sizeof *t->order * (size_t)(t->nrows ? t->nrows : 1));
	for (int i = 0; i < t->nrows; i++)
		t->order[i] = i;
	if (t->sort_col >= 0) {
		sorting = t;
		qsort(t->order, (size_t)t->nrows, sizeof *t->order, compare_rows);
	}
	dirty((int64_t)(t - items) + 1);
}

static void sort_table(int64_t id, int col, int down)
{
	Item *t = &items[id - 1];
	t->sort_col = col;
	t->sort_down = down;
	table_order(t);
}

void cby_gui_table_row_start(int64_t id)
{
	Item *t = item(id, K_TABLE);
	t->rows = realloc(t->rows, sizeof *t->rows * (size_t)(t->nrows + 1));
	t->rows[t->nrows++] = (TRow){ NULL, 0 };
}

void cby_gui_table_row_cell(int64_t id, const char *text)
{
	Item *t = item(id, K_TABLE);
	TRow *r = &t->rows[t->nrows - 1];
	r->cell = realloc(r->cell, sizeof *r->cell * (size_t)(r->n + 1));
	r->cell[r->n++] = strdup(text);
}

void cby_gui_table_row_end(int64_t id) { table_order(item(id, K_TABLE)); }

static void table_fail(const char *what, int row)
{
	char n[24];
	snprintf(n, sizeof n, "%d", row);
	fail(what, n);
}

void cby_gui_table_set_cell(int64_t id, int row, int col, const char *text)
{
	Item *t = item(id, K_TABLE);
	if (row < 0 || row >= t->nrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	if (col < 0 || col >= t->nopts)
		table_fail("the table has no column %s (columns count from 0)", col);
	TRow *r = &t->rows[row];
	while (r->n <= col) {
		r->cell = realloc(r->cell, sizeof *r->cell * (size_t)(r->n + 1));
		r->cell[r->n++] = strdup("");
	}
	set_str(&r->cell[col], text);
	table_order(t);
}

int64_t cby_gui_table_cell_length(int64_t id, int row, int col)
{
	Item *t = item(id, K_TABLE);
	if (row < 0 || row >= t->nrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	return hand_out(t, cell(t, row, col));
}

char cby_gui_table_cell_char(int64_t id, int64_t i) { return cby_gui_text_char(id, i); }

void cby_gui_table_remove_row(int64_t id, int row)
{
	Item *t = item(id, K_TABLE);
	if (row < 0 || row >= t->nrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	for (int i = 0; i < t->rows[row].n; i++)
		free(t->rows[row].cell[i]);
	free(t->rows[row].cell);
	memmove(&t->rows[row], &t->rows[row + 1], sizeof *t->rows * (size_t)(t->nrows - row - 1));
	t->nrows--;
	int had = t->selected == row;
	if (t->selected > row)
		t->selected--;
	else if (had)
		t->selected = -1;
	table_order(t);
	if (had)   /* the selected row went: nothing is selected now */
		call(t->on_select, t->win);
}

void cby_gui_table_clear(int64_t id)
{
	Item *t = item(id, K_TABLE);
	for (int r = 0; r < t->nrows; r++) {
		for (int i = 0; i < t->rows[r].n; i++)
			free(t->rows[r].cell[i]);
		free(t->rows[r].cell);
	}
	t->nrows = 0;
	t->scroll = 0;
	int had = t->selected >= 0;
	t->selected = -1;
	table_order(t);
	if (had)
		call(t->on_select, t->win);
}

int cby_gui_table_rows(int64_t id) { return item(id, K_TABLE)->nrows; }
int cby_gui_table_columns(int64_t id) { return item(id, K_TABLE)->nopts; }

static void table_into_view(Item *t)
{
	int at_ = -1;
	for (int i = 0; i < t->nrows; i++)
		if (t->order[i] == t->selected)
			at_ = i;
	if (at_ < 0)
		return;
	double top = at_ * ROW_H, inner = t->h - 2 - ROW_H;
	if (top < t->scroll)
		t->scroll = top;
	if (top + ROW_H - t->scroll > inner)
		t->scroll = top + ROW_H - inner;
	if (t->scroll < 0)
		t->scroll = 0;
}

static void select_row(int64_t id, int row)
{
	Item *t = &items[id - 1];
	if (row < -1 || row >= t->nrows)
		row = -1;
	if (row == t->selected)
		return;
	t->selected = row;
	table_into_view(t);
	call(t->on_select, t->win);
}

/* ------------------------------------------------------------- number boxes, colors and fonts */

int64_t cby_gui_add_number(int64_t box, int min, int max)
{
	int64_t id = add_to(box, K_NUMBER, "");
	Item *it = &items[id - 1];
	it->min = min < max ? min : max;
	it->max = min < max ? max : min;
	it->value = it->min;
	return id;
}

int cby_gui_number_get(int64_t id) { return item(id, K_NUMBER)->value; }

static void set_number(Item *it, long v)
{
	if (v < it->min)
		v = it->min;
	if (v > it->max)
		v = it->max;
	if (v != it->value) {
		it->value = (int)v;
		call(it->on_change, it->win);
	}
	dirty((int64_t)(it - items) + 1);
}

void cby_gui_number_set(int64_t id, int value) { set_number(item(id, K_NUMBER), value); }

int64_t cby_gui_add_color_button(int64_t box) { return add_to(box, K_COLOR, "#000000"); }

int64_t cby_gui_color_length(int64_t id)
{
	Item *it = item(id, K_COLOR);
	return hand_out(it, it->text);
}

void cby_gui_color_set(int64_t id, const char *spec)
{
	Item *it = item(id, K_COLOR);
	double a;
	uint32_t c = color_of(spec, &a);
	char buf[16];
	if (a < 1)
		snprintf(buf, sizeof buf, "#%06x%02x", c, (int)(a * 255 + 0.5));
	else
		snprintf(buf, sizeof buf, "#%06x", c);
	if (strcmp(buf, it->text) != 0) {
		set_str(&it->text, buf);
		call(it->on_change, it->win);
	}
	dirty(id);
}

int64_t cby_gui_add_font_button(int64_t box) { return add_to(box, K_FONT, "Sans 12"); }

int64_t cby_gui_font_length(int64_t id)
{
	Item *it = item(id, K_FONT);
	return hand_out(it, it->text);
}

void cby_gui_font_set(int64_t id, const char *font)
{
	Item *it = item(id, K_FONT);
	if (strcmp(font, it->text) != 0) {
		set_str(&it->text, font);
		call(it->on_change, it->win);
	}
	dirty(id);
}

/* what a color or font button's list offers */
static const char *const offered_colors[] = {
	"#000000", "#3d3846", "#77767b", "#9a9996", "#c0bfbc", "#deddda", "#f6f5f4", "#ffffff",
	"#a51d2d", "#e01b24", "#ff7800", "#f6d32d", "#33d17a", "#26a269", "#1c71d8", "#613583",
	"#63452c", "#986a44", "#f66151", "#ffbe6f", "#f9f06b", "#8ff0a4", "#99c1f1", "#dc8add",
	"#4682b4", "#dc143c", "#ffd700", "#228b22", "#008080", "#000080", "#800080", "#ffc0cb", NULL };
static const char *const offered_fonts[] = {
	"Sans 12", "Sans Bold 12", "Serif 12", "Serif Italic 12", "Monospace 12", "Sans 16", "Serif 16",
	"Monospace 16", "Sans 24", "Serif 24", NULL };
#define SWATCH 28
#define SWATCHES_ACROSS 8

static int count_of(const char *const *list)
{
	int n = 0;
	while (list[n])
		n++;
	return n;
}

/* ------------------------------------------------------------- what's where */

static int shown(int64_t id)   /* it, and the boxes it's in, aren't hidden (or on a tab that isn't showing) */
{
	for (; id; id = items[id - 1].parent) {
		if (items[id - 1].hidden)
			return 0;
		int64_t p = items[id - 1].parent;
		if (p && items[p - 1].kind == K_TABS && items[p - 1].kids[items[p - 1].selected] != id)
			return 0;
	}
	return 1;
}

static int usable(int64_t id)   /* shown, and it and its boxes aren't greyed out */
{
	if (!shown(id))
		return 0;
	for (int64_t p = id; p; p = items[p - 1].parent)
		if (items[p - 1].disabled)
			return 0;
	return 1;
}

static int takes_focus(int64_t id)
{
	switch (items[id - 1].kind) {
	case K_BUTTON: case K_ENTRY: case K_CHECK: case K_CHOICE: case K_TEXT_AREA: case K_LIST: case K_SLIDER:
	case K_TABS: case K_TABLE: case K_NUMBER: case K_COLOR: case K_FONT: case K_VIDEO:
		return usable(id);
	case K_CANVAS:
		return usable(id) && (items[id - 1].keyed.fn || items[id - 1].clicked.fn);
	}
	return 0;
}

/* the deepest usable thing at x, y in the box */
static int64_t at(int64_t box, double x, double y)
{
	Item *b = &items[box - 1];
	for (int i = b->nkids - 1; i >= 0; i--) {
		int64_t k = b->kids[i];
		Item *it = &items[k - 1];
		if (it->hidden || x < it->x || y < it->y || x >= it->x + it->w || y >= it->y + it->h)
			continue;
		if (it->kind == K_BOX) {
			int64_t in = at(k, x, y);
			return in ? in : (it->scrolls ? k : 0);
		}
		if (it->kind == K_TABS && y >= it->y + TAB_H && it->selected >= 0) {
			int64_t in = at(it->kids[it->selected], x, y);
			return in ? in : k;
		}
		return k;
	}
	return 0;
}

/* in order, everything in the window that takes the keys */
static int focus_order(int64_t box, int64_t *out, int n)
{
	Item *b = &items[box - 1];
	for (int i = 0; i < b->nkids; i++) {
		int64_t k = b->kids[i];
		if (items[k - 1].kind == K_BOX)
			n = focus_order(k, out, n);
		else if (takes_focus(k)) {
			out[n++] = k;
			if (items[k - 1].kind == K_TABS && items[k - 1].selected >= 0)
				n = focus_order(items[k - 1].kids[items[k - 1].selected], out, n);
		}
	}
	return n;
}

/* ---- text being composed: an accent waiting for its letter, or an input
 * method's word not yet chosen. It's shown at the cursor, underlined, but
 * isn't in the text until it's done (then it comes as typed text). */
static char *composing;      /* (NULL or "": nothing) */
static int composing_cursor; /* bytes into it */
static int64_t composing_win;

static void composing_done(int64_t win)
{
	if (composing && composing[0] && win > 0 && win <= nitems)
		items[win - 1].dirty = 1;
	free(composing);
	composing = NULL;
	composing_win = 0;
}

static void composing_now(int64_t win, const char *text, int start_chars)
{
	free(composing);
	composing = strdup(text ? text : "");
	composing_win = win;
	int b = 0;
	for (int c = 0; c < start_chars && composing[b]; c++) {
		b++;
		while (composing[b] && (composing[b] & 0xC0) == 0x80)
			b++;
	}
	composing_cursor = start_chars < 0 ? (int)strlen(composing) : b;
	items[win - 1].dirty = 1;
}

static void set_focus(int64_t win, int64_t id)
{
	Item *w = &items[win - 1];
	if (w->focus == id)
		return;
	if (composing_win == win)
		composing_done(win);
	w->focus = id;
	/* in a scrolling column, scrolled to */
	for (int64_t p = id ? items[id - 1].parent : 0; p; p = items[p - 1].parent) {
		Item *b = &items[p - 1];
		if (!b->scrolls)
			continue;
		Item *it = &items[id - 1];
		double inner = b->h - 12, top = it->y - (b->y + 6) + b->scroll;
		if (top < b->scroll)
			b->scroll = top;
		if (top + it->h > b->scroll + inner)
			b->scroll = top + it->h - inner;
	}
	int editing = id && (items[id - 1].kind == K_ENTRY || items[id - 1].kind == K_TEXT_AREA || items[id - 1].kind == K_NUMBER);
	if (w->sw) {
		if (editing)
			SDL_StartTextInput(w->sw);
		else
			SDL_StopTextInput(w->sw);
	}
	w->dirty = 1;
}

static void move_focus(int64_t win, int by)
{
	int64_t *order = malloc(sizeof *order * (size_t)(nitems + 1));
	int n = focus_order(items[win - 1].box, order, 0);
	if (n) {
		int at_ = -1;
		for (int i = 0; i < n; i++)
			if (order[i] == items[win - 1].focus)
				at_ = i;
		at_ = at_ < 0 ? (by > 0 ? 0 : n - 1) : (at_ + by + n) % n;
		set_focus(win, order[at_]);
	}
	free(order);
}

/* ------------------------------------------------------------- text boxes and text areas */

static int prev_char(const char *s, int i)
{
	if (i <= 0)
		return 0;
	i--;
	while (i > 0 && (s[i] & 0xC0) == 0x80)
		i--;
	return i;
}

static int next_char_at(const char *s, int i)
{
	int n = (int)strlen(s);
	if (i >= n)
		return n;
	i++;
	while (i < n && (s[i] & 0xC0) == 0x80)
		i++;
	return i;
}

static void sel_range(Item *it, int *a, int *b)
{
	*a = it->cursor < it->anchor ? it->cursor : it->anchor;
	*b = it->cursor < it->anchor ? it->anchor : it->cursor;
}

/* takes out the selection (if any); 1 if there was one */
static int cut_selection(Item *it)
{
	int a, b;
	sel_range(it, &a, &b);
	if (a == b)
		return 0;
	memmove(it->text + a, it->text + b, strlen(it->text + b) + 1);
	it->cursor = it->anchor = a;
	return 1;
}

/* ---- undo and redo: the text is kept as it was before each change (a run
 * of letters typed counts as one change, up to a space) */

#define UNDO_STEPS 100

static void push_snap(Snap **list, int *n, Item *it)
{
	if (*n == UNDO_STEPS) {
		free((*list)[0].text);
		memmove(*list, *list + 1, sizeof **list * (UNDO_STEPS - 1));
		(*n)--;
	}
	*list = realloc(*list, sizeof **list * (size_t)(*n + 1));
	(*list)[(*n)++] = (Snap){ strdup(it->text), it->cursor, it->anchor };
}

static void forget_redo(Item *it)
{
	for (int i = 0; i < it->nredo; i++)
		free(it->redo[i].text);
	it->nredo = 0;
}

static void forget_undo(Item *it)
{
	for (int i = 0; i < it->nundo; i++)
		free(it->undo[i].text);
	it->nundo = 0;
	forget_redo(it);
	it->typing_end = -1;
}

/* before a change: typing letters on from the last ones joins their change */
static void remember(Item *it, int typing)
{
	if (!(typing && it->typing_end >= 0 && it->typing_end == it->cursor && it->cursor == it->anchor))
		push_snap(&it->undo, &it->nundo, it);
	forget_redo(it);
	it->typing_end = -1;
}

/* back (or forward again): 0 if there's nothing to go to */
static int undo_redo(int64_t id, int back)
{
	Item *it = &items[id - 1];
	Snap **from = back ? &it->undo : &it->redo, **to = back ? &it->redo : &it->undo;
	int *nfrom = back ? &it->nundo : &it->nredo, *nto = back ? &it->nredo : &it->nundo;
	if (it->read_only || *nfrom == 0)
		return 0;
	push_snap(to, nto, it);
	Snap sn = (*from)[--*nfrom];
	free(it->text);
	it->text = sn.text;
	it->cursor = sn.cursor;
	it->anchor = sn.anchor;
	it->typing_end = -1;
	changed(id);
	return 1;
}

static void insert_text_as(int64_t id, const char *s, int typing)
{
	Item *it = &items[id - 1];
	if (it->read_only)
		return;
	remember(it, typing && strcmp(s, " ") != 0 && strcmp(s, "\n") != 0);
	cut_selection(it);
	size_t n = strlen(s), len = strlen(it->text);
	if (it->kind == K_ENTRY) {   /* one line: new lines become spaces */
		char *t = strdup(s);
		for (char *c = t; *c; c++)
			if (*c == '\n' || *c == '\r')
				*c = ' ';
		it->text = realloc(it->text, len + n + 1);
		memmove(it->text + it->cursor + n, it->text + it->cursor, len - (size_t)it->cursor + 1);
		memcpy(it->text + it->cursor, t, n);
		free(t);
	} else {
		it->text = realloc(it->text, len + n + 1);
		memmove(it->text + it->cursor + n, it->text + it->cursor, len - (size_t)it->cursor + 1);
		memcpy(it->text + it->cursor, s, n);
	}
	it->cursor += (int)n;
	it->anchor = it->cursor;
	if (typing)
		it->typing_end = it->cursor;
	changed(id);
}

static void insert_text(int64_t id, const char *s) { insert_text_as(id, s, 0); }

static void delete_around(int64_t id, int forward)
{
	Item *it = &items[id - 1];
	if (it->read_only)
		return;
	remember(it, 0);
	if (!cut_selection(it)) {
		int a = forward ? it->cursor : prev_char(it->text, it->cursor);
		int b = forward ? next_char_at(it->text, it->cursor) : it->cursor;
		if (a == b)
			return;
		memmove(it->text + a, it->text + b, strlen(it->text + b) + 1);
		it->cursor = it->anchor = a;
	}
	changed(id);
}

static char *selected_text(Item *it)
{
	int a, b;
	sel_range(it, &a, &b);
	char *t = malloc((size_t)(b - a) + 1);
	memcpy(t, it->text + a, (size_t)(b - a));
	t[b - a] = '\0';
	return t;
}

/* the text area's inside: where its text goes */
static void area_inside(Item *it, double *x, double *y, double *w, double *h)
{
	*x = it->x + 8;
	*y = it->y + 6;
	*w = it->w - 16 - 8;   /* (room for the scroll bar) */
	*h = it->h - 12;
}

/* which wrapped line the byte is on */
static int line_of(Wrapped *wr, int byte)
{
	for (int i = 0; i < wr->n; i++)
		if (byte >= wr->start[i] && (byte < wr->end[i] || (byte == wr->end[i] && (i == wr->n - 1 || wr->start[i + 1] != byte))))
			return i;
	return wr->n ? wr->n - 1 : 0;
}

/* the byte nearest x along a line */
static int byte_at_x(const char *s, int a, int b, double x)
{
	int best = a;
	double bestd = 1e9;
	for (int i = a;; i = next_char_at(s, i)) {
		double d = fabs(tcx(s + a, b - a, i - a) - x);
		if (d < bestd) {
			bestd = d;
			best = i;
		}
		if (i >= b)
			break;
	}
	return best;
}

static void keep_cursor_in_view(Item *it)
{
	if (it->kind == K_ENTRY) {
		double inner = it->w - 20, cx = tcx(it->text, -1, it->cursor);
		if (cx - it->scroll > inner)
			it->scroll = cx - inner;
		if (cx < it->scroll)
			it->scroll = cx;
		if (it->scroll < 0)
			it->scroll = 0;
	} else if (it->kind == K_TEXT_AREA) {
		double x, y, w, h;
		area_inside(it, &x, &y, &w, &h);
		Wrapped wr = wrap(it->text, w);
		double top = line_of(&wr, it->cursor) * lh();
		if (top - it->scroll > h - lh())
			it->scroll = top - (h - lh());
		if (top < it->scroll)
			it->scroll = top;
		unwrap(&wr);
	}
}

/* where a click at x, y puts the cursor */
static int hit_text(Item *it, double mx, double my)
{
	if (it->kind == K_ENTRY)
		return byte_at_x(it->text, 0, (int)strlen(it->text), mx - (it->x + 10) + it->scroll);
	double x, y, w, h;
	area_inside(it, &x, &y, &w, &h);
	Wrapped wr = wrap(it->text, w);
	int line = (int)floor((my - y + it->scroll) / lh());
	if (line < 0)
		line = 0;
	if (line >= wr.n)
		line = wr.n - 1;
	int b = byte_at_x(it->text, wr.start[line], wr.end[line], mx - x);
	unwrap(&wr);
	return b;
}

/* words, for moving by a word and double clicks: letters and digits (and
 * any letter beyond ASCII), or else a run of anything else but spaces */
static int word_char(const char *s, int i)
{
	unsigned char c = (unsigned char)s[i];
	return c >= 0x80 || isalnum(c) || c == '_' || c == '\'';
}

static int word_start(const char *s, int i)
{
	while (i > 0 && isspace((unsigned char)s[prev_char(s, i)]))
		i = prev_char(s, i);
	if (i > 0) {
		int w = word_char(s, prev_char(s, i));
		while (i > 0 && !isspace((unsigned char)s[prev_char(s, i)]) && word_char(s, prev_char(s, i)) == w)
			i = prev_char(s, i);
	}
	return i;
}

static int word_end(const char *s, int i)
{
	int n = (int)strlen(s);
	while (i < n && isspace((unsigned char)s[i]))
		i = next_char_at(s, i);
	if (i < n) {
		int w = word_char(s, i);
		while (i < n && !isspace((unsigned char)s[i]) && word_char(s, i) == w)
			i = next_char_at(s, i);
	}
	return i;
}

/* the word round byte i (a double click), or the line (a triple click) */
static void select_word_at(Item *it, int i)
{
	const char *s = it->text;
	int n = (int)strlen(s), a = i, b = i;
	if (i < n && !isspace((unsigned char)s[i])) {
		int w = word_char(s, i);
		while (a > 0 && !isspace((unsigned char)s[prev_char(s, a)]) && word_char(s, prev_char(s, a)) == w)
			a = prev_char(s, a);
		while (b < n && !isspace((unsigned char)s[b]) && word_char(s, b) == w)
			b = next_char_at(s, b);
	}
	it->anchor = a;
	it->cursor = b;
}

static void select_line_at(Item *it, int i)
{
	const char *s = it->text;
	int n = (int)strlen(s), a = i, b = i;
	while (a > 0 && s[a - 1] != '\n')
		a--;
	while (b < n && s[b] != '\n')
		b++;
	it->anchor = a;
	it->cursor = b;
}

static int edit_key(int64_t id, SDL_Keycode k, SDL_Keymod mods)
{
	Item *it = &items[id - 1];
	int shift = (mods & SDL_KMOD_SHIFT) != 0, ctrl = (mods & SDL_KMOD_CTRL) != 0;
#if defined(__APPLE__)
	int by_word = (mods & SDL_KMOD_ALT) != 0;   /* Option+arrow, as on a Mac */
#else
	int by_word = ctrl;
#endif
	int len = (int)strlen(it->text);
	if (ctrl && (k == SDLK_Z || k == SDLK_Y)) {
		undo_redo(id, k == SDLK_Z && !shift);
	} else if (by_word && (k == SDLK_BACKSPACE || k == SDLK_DELETE) && !it->read_only) {
		/* a word at a time */
		if (it->cursor == it->anchor)
			it->cursor = k == SDLK_BACKSPACE ? word_start(it->text, it->cursor) : word_end(it->text, it->cursor);
		delete_around(id, 0);
	} else if (by_word && (k == SDLK_LEFT || k == SDLK_RIGHT)) {
		it->cursor = k == SDLK_LEFT ? word_start(it->text, it->cursor) : word_end(it->text, it->cursor);
		if (!shift)
			it->anchor = it->cursor;
#if defined(__APPLE__)
	} else if (ctrl && (k == SDLK_LEFT || k == SDLK_RIGHT)) {   /* Cmd+arrow: the line's start or end */
		return edit_key(id, k == SDLK_LEFT ? SDLK_HOME : SDLK_END, mods & ~SDL_KMOD_CTRL);
	} else if (ctrl && (k == SDLK_UP || k == SDLK_DOWN)) {   /* Cmd+up or down: the start or end */
		return edit_key(id, k == SDLK_UP ? SDLK_HOME : SDLK_END, mods);
#endif
	} else if (ctrl && k == SDLK_A) {
		it->anchor = 0;
		it->cursor = len;
	} else if (ctrl && (k == SDLK_C || k == SDLK_X)) {
		char *t = selected_text(it);
		if (*t)
			SDL_SetClipboardText(t);
		free(t);
		if (k == SDLK_X && !it->read_only) {
			int a, b;
			sel_range(it, &a, &b);
			if (a != b) {
				remember(it, 0);
				cut_selection(it);
				changed(id);
			}
		}
	} else if (ctrl && k == SDLK_V) {
		char *t = SDL_GetClipboardText();
		if (t && *t)
			insert_text(id, t);
		SDL_free(t);
	} else if (k == SDLK_BACKSPACE) {
		delete_around(id, 0);
	} else if (k == SDLK_DELETE) {
		delete_around(id, 1);
	} else if (k == SDLK_LEFT || k == SDLK_RIGHT) {
		int a, b;
		sel_range(it, &a, &b);
		if (!shift && a != b)
			it->cursor = k == SDLK_LEFT ? a : b;
		else
			it->cursor = k == SDLK_LEFT ? prev_char(it->text, it->cursor) : next_char_at(it->text, it->cursor);
		if (!shift)
			it->anchor = it->cursor;
	} else if (k == SDLK_HOME || k == SDLK_END || ((k == SDLK_UP || k == SDLK_DOWN) && it->kind == K_TEXT_AREA)) {
		if (it->kind == K_ENTRY || (ctrl && (k == SDLK_HOME || k == SDLK_END))) {
			it->cursor = k == SDLK_HOME ? 0 : len;
		} else {
			double x, y, w, h;
			area_inside(it, &x, &y, &w, &h);
			Wrapped wr = wrap(it->text, w);
			int line = line_of(&wr, it->cursor);
			if (k == SDLK_HOME) {
				it->cursor = wr.start[line];
			} else if (k == SDLK_END) {
				it->cursor = wr.end[line];
				if (it->cursor > wr.start[line] && line < wr.n - 1 && wr.end[line] == wr.start[line + 1] &&
				    it->text[it->cursor - 1] == ' ')
					it->cursor = prev_char(it->text, it->cursor);   /* (before a wrapped line's last space) */
			} else {
				int to = line + (k == SDLK_UP ? -1 : 1);
				if (to >= 0 && to < wr.n) {
					double cx = tcx(it->text + wr.start[line], wr.end[line] - wr.start[line], it->cursor - wr.start[line]);
					it->cursor = byte_at_x(it->text, wr.start[to], wr.end[to], cx);
				} else {
					it->cursor = to < 0 ? 0 : len;
				}
			}
			unwrap(&wr);
		}
		if (!shift)
			it->anchor = it->cursor;
	} else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && it->kind == K_TEXT_AREA) {
		insert_text(id, "\n");
	} else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && it->kind == K_ENTRY) {
		call(it->on_enter, it->win);
	} else if (k == SDLK_TAB && it->kind == K_TEXT_AREA && ctrl) {
		insert_text(id, "\t");
	} else {
		return 0;
	}
	if (id <= nitems) {
		keep_cursor_in_view(&items[id - 1]);
		dirty(id);
	}
	return 1;
}

/* ------------------------------------------------------------- lists */

static void list_into_view(Item *it)
{
	if (it->selected < 0)
		return;
	double top = it->selected * ROW_H, inner = it->h - 2;
	if (top < it->scroll)
		it->scroll = top;
	if (top + ROW_H - it->scroll > inner)
		it->scroll = top + ROW_H - inner;
	if (it->scroll < 0)
		it->scroll = 0;
}

static void scroll_by(Item *it, double dy)
{
	double content = 0, inner = it->h - 2;
	if (it->kind == K_LIST) {
		content = it->nopts * ROW_H;
	} else if (it->kind == K_TEXT_AREA) {
		double x, y, w, h;
		area_inside(it, &x, &y, &w, &h);
		Wrapped wr = wrap(it->text, w);
		content = wr.n * lh();
		inner = h;
		unwrap(&wr);
	}
	it->scroll += dy;
	if (it->scroll > content - inner)
		it->scroll = content - inner;
	if (it->scroll < 0)
		it->scroll = 0;
	dirty((int64_t)(it - items) + 1);
}

/* ------------------------------------------------------------- drawing */

static int focus_visible = 0;   /* show where the keys go (after Tab) */

static void scroll_bar(double x, double y, double h, double content, double scroll)
{
	if (content <= h)
		return;
	double bar = h * h / content, top = y + (h - bar) * scroll / (content - h);
	fill_round(x, top, 6, bar, 3, (SDL_FColor){ 0, 0, 0, 0.25f });
}

static void draw_check_mark(double x, double y, SDL_FColor c)
{
	thick_line(x + 4, y + 9, x + 8, y + 13, 2.2, c);
	thick_line(x + 8, y + 13, x + 14, y + 5, 2.2, c);
}

/* the composed text's place in what's drawn (bytes; -1: none) */
static int compose_a = -1, compose_b = -1;

static void draw_item_now(int64_t id, int64_t win);

/* Draws an item; a text box with text being composed in it is drawn with
 * that text put in at the cursor for the while. */
static void draw_item_composed(int64_t id, int64_t win);
static int ink_given;   /* a text color of the program's own is in force */

static SDL_FColor shade(SDL_FColor c, float by)
{
	SDL_FColor o = c;
	o.r = by > 0 ? c.r + (1 - c.r) * by : c.r * (1 + by);
	o.g = by > 0 ? c.g + (1 - c.g) * by : c.g * (1 + by);
	o.b = by > 0 ? c.b + (1 - c.b) * by : c.b * (1 + by);
	return o;
}

/* An item, in the program's own colors if it (or what it's in) has
 * them: its text in its text color, and its background (a button's, a
 * field's) in its own. What has the keys still shows the accent. */
static void draw_item(int64_t id, int64_t win)
{
	Item *it = &items[id - 1];
	if (it->hidden || (!it->has_ink && !it->has_paper)) {
		draw_item_composed(id, win);
		return;
	}
	SDL_FColor text = C_TEXT, field = C_FIELD, button = C_BUTTON, hover = C_BUTTON_HOVER, down = C_BUTTON_DOWN, window = C_WINDOW;
	int had_ink = ink_given;
	if (it->has_ink) {
		C_TEXT = it->ink;
		ink_given = 1;
	} else if (it->has_paper && !ink_given) {
		/* a background with no text color: black or white, whichever reads */
		float light = 0.299f * it->paper.r + 0.587f * it->paper.g + 0.114f * it->paper.b;
		C_TEXT = light < 0.5f ? (SDL_FColor){ 0.96f, 0.96f, 0.96f, 1 } : (SDL_FColor){ 0.10f, 0.10f, 0.10f, 1 };
	}
	if (it->has_paper) {
		C_FIELD = C_BUTTON = C_WINDOW = it->paper;
		C_BUTTON_HOVER = shade(it->paper, 0.10f);
		C_BUTTON_DOWN = shade(it->paper, -0.12f);
		if (it->kind == K_BOX || it->kind == K_LABEL || it->kind == K_CHECK || it->kind == K_SLIDER ||
		    it->kind == K_PROGRESS || it->kind == K_IMAGE || it->kind == K_SEPARATOR)
			fill_rect(it->x, it->y, it->w, it->h, it->paper);
	}
	draw_item_composed(id, win);
	ink_given = had_ink;
	C_TEXT = text;
	C_FIELD = field;
	C_BUTTON = button;
	C_BUTTON_HOVER = hover;
	C_BUTTON_DOWN = down;
	C_WINDOW = window;
}

static void draw_item_composed(int64_t id, int64_t win)
{
	Item *it = &items[id - 1];
	if (!(composing && composing[0] && composing_win == win && items[win - 1].focus == id &&
	      (it->kind == K_ENTRY || it->kind == K_TEXT_AREA))) {
		draw_item_now(id, win);
		return;
	}
	char *was = it->text;
	int cur = it->cursor, anc = it->anchor, a = cur < anc ? cur : anc, b = cur < anc ? anc : cur;
	size_t n = strlen(composing), len = strlen(was);
	char *shown = malloc(len - (size_t)(b - a) + n + 1);   /* (what's selected is replaced) */
	memcpy(shown, was, (size_t)a);
	memcpy(shown + a, composing, n);
	strcpy(shown + a + n, was + b);
	it->text = shown;
	it->cursor = it->anchor = a + composing_cursor;
	compose_a = a;
	compose_b = a + (int)n;
	draw_item_now(id, win);
	compose_a = compose_b = -1;
	it->text = was;
	it->cursor = cur;
	it->anchor = anc;
	free(shown);
}

/* tells the input method where the cursor is, so its choices show by it */
static void tell_input_area(Item *w, double x, double y, double h, double cursor_x)
{
	static int lx = -1, ly = -1, lh_ = -1, lc = -1;
	SDL_Rect r = { (int)x, (int)y, 1, (int)h };
	int c = (int)cursor_x;
	if (r.x == lx && r.y == ly && r.h == lh_ && c == lc)
		return;
	lx = r.x; ly = r.y; lh_ = r.h; lc = c;
	r.x = c;
	SDL_SetTextInputArea(w->sw, &r, 0);
}

static void draw_item_now(int64_t id, int64_t win)
{
	Item *it = &items[id - 1];
	Item *w = &items[win - 1];
	if (it->hidden)
		return;
	int ok = usable(id), focused = w->focus == id;
	SDL_FColor text = ok ? C_TEXT : C_DIM;
	int hover = w->hover == id && ok, down = w->pressed == id && w->dragging && hover;
	switch (it->kind) {
	case K_BOX:
		if (it->scrolls) {
			outline(it->x, it->y, it->w, it->h, RADIUS, C_BORDER, 1);
			clip(it->x + 1, it->y + 1, it->w - 2, it->h - 2);
		}
		for (int i = 0; i < it->nkids; i++)
			draw_item(it->kids[i], win);
		if (it->scrolls) {
			scroll_bar(it->x + it->w - 10, it->y + 4, it->h - 8, it->content_h + 12, it->scroll);
			unclip();
		}
		break;
	case K_TABS: {
		/* the tabs along the top, the page showing framed below */
		outline(it->x, it->y + TAB_H, it->w, it->h - TAB_H, RADIUS, C_BORDER, 1);
		double x = it->x + 4;
		for (int i = 0; i < it->nopts; i++) {
			double w_ = tw(it->opts[i]) + 28;
			int sel = i == it->selected;
			if (sel)
				fill_rect(x, it->y + TAB_H - 3, w_, 3, ok ? C_ACCENT : C_DIM);
			draw_text(uifont(), UI_SIZE, x + 14, it->y + (TAB_H - lh()) / 2 - 1, it->opts[i], sel ? text : C_DIM);
			if (sel && focused && focus_visible)
				outline(x, it->y + 2, w_, TAB_H - 6, RADIUS, C_ACCENT, 2);
			x += w_;
		}
		if (it->selected >= 0 && it->selected < it->nkids)
			draw_item(it->kids[it->selected], win);
		break;
	}
	case K_TABLE: {
		boxed(it->x, it->y, it->w, it->h, RADIUS, focused ? C_ACCENT : C_BORDER, ok ? C_FIELD : C_WINDOW, focused ? 2 : 1);
		int cols = it->nopts ? it->nopts : 1;
		double cw = (it->w - 2) / (double)cols;
		clip(it->x + 2, it->y + 2, it->w - 4, it->h - 4);
		fill_rect(it->x + 1, it->y + 1, it->w - 2, ROW_H, C_BAR);
		for (int c = 0; c < it->nopts; c++) {
			double cx = it->x + 1 + c * cw;
			clip((int)cx, it->y, (int)cw - 4, ROW_H + 2);
			draw_text(uifont(), UI_SIZE, cx + 10, it->y + 1 + (ROW_H - lh()) / 2, it->opts[c], text);
			unclip();
			if (c == it->sort_col) {
				double ax = cx + cw - 16, ay = it->y + 1 + ROW_H / 2.0;
				if (it->sort_down) {
					thick_line(ax - 4, ay - 2, ax, ay + 2, 1.6, text);
					thick_line(ax, ay + 2, ax + 4, ay - 2, 1.6, text);
				} else {
					thick_line(ax - 4, ay + 2, ax, ay - 2, 1.6, text);
					thick_line(ax, ay - 2, ax + 4, ay + 2, 1.6, text);
				}
			}
			if (c > 0)
				fill_rect(cx, it->y + 6, 1, ROW_H - 10, C_BORDER);
		}
		fill_rect(it->x + 1, it->y + ROW_H, it->w - 2, 1, C_BORDER);
		unclip();
		clip(it->x + 2, it->y + ROW_H + 1, it->w - 4, it->h - ROW_H - 3);
		for (int i = 0; i < it->nrows; i++) {
			int row = it->order[i];
			double ry = it->y + ROW_H + 1 + i * ROW_H - it->scroll;
			if (ry + ROW_H < it->y || ry > it->y + it->h)
				continue;
			int sel = row == it->selected;
			if (sel)
				fill_rect(it->x + 1, ry, it->w - 2, ROW_H, ok ? C_ACCENT : C_DIM);
			for (int c = 0; c < it->nopts; c++) {
				double cx = it->x + 1 + c * cw;
				clip((int)cx, (int)ry, (int)cw - 4, ROW_H);
				draw_text(uifont(), UI_SIZE, cx + 10, ry + (ROW_H - lh()) / 2, cell(it, row, c), sel ? C_WHITE : text);
				unclip();
			}
		}
		scroll_bar(it->x + it->w - 10, it->y + ROW_H + 4, it->h - ROW_H - 8, it->nrows * ROW_H + 8, it->scroll);
		unclip();
		break;
	}
	case K_NUMBER: {
		boxed(it->x, it->y, it->w, it->h, RADIUS, focused ? C_ACCENT : C_BORDER, ok ? C_FIELD : C_WINDOW, focused ? 2 : 1);
		char v[24];
		snprintf(v, sizeof v, "%d", it->value);
		draw_text(uifont(), UI_SIZE, it->x + 10, it->y + (it->h - lh()) / 2, v, text);
		double bx = it->x + it->w - 2 * FIELD_H, cy = it->y + it->h / 2.0;
		fill_rect(bx, it->y + 1, 1, it->h - 2, C_BORDER);
		fill_rect(bx + FIELD_H, it->y + 1, 1, it->h - 2, C_BORDER);
		SDL_FColor minus = it->value > it->min && ok ? text : C_DIM, plus = it->value < it->max && ok ? text : C_DIM;
		thick_line(bx + FIELD_H / 2.0 - 5, cy, bx + FIELD_H / 2.0 + 5, cy, 1.6, minus);
		thick_line(bx + FIELD_H * 1.5 - 5, cy, bx + FIELD_H * 1.5 + 5, cy, 1.6, plus);
		thick_line(bx + FIELD_H * 1.5, cy - 5, bx + FIELD_H * 1.5, cy + 5, 1.6, plus);
		break;
	}
	case K_COLOR: case K_FONT: {
		SDL_FColor bg = !ok ? C_WINDOW : w->popup == id ? C_BUTTON_DOWN : hover ? C_BUTTON_HOVER : C_BUTTON;
		boxed(it->x, it->y, it->w, it->h, RADIUS, C_BORDER, bg, 1);
		if (focused && focus_visible)
			outline(it->x - 2, it->y - 2, it->w + 4, it->h + 4, RADIUS + 2, C_ACCENT, 2);
		if (it->kind == K_COLOR)
			boxed(it->x + 8, it->y + 8, it->w - 16, it->h - 16, 3, C_BORDER, fcolor(color(it->text)), 1);
		else
			draw_text(uifont(), UI_SIZE, it->x + 16, it->y + (it->h - lh()) / 2, it->text, text);
		break;
	}
	case K_LABEL: {
		double tw_;
		int lines;
		text_size(it->text, &tw_, &lines);
		double x = it->x + (it->w - tw_) / 2, y = it->y + (it->h - lines * lh()) / 2;
		draw_lines(it->text, x, y, text);
		break;
	}
	case K_BUTTON: {
		SDL_FColor bg = !ok ? C_WINDOW : down ? C_BUTTON_DOWN : hover ? C_BUTTON_HOVER : C_BUTTON;
		boxed(it->x, it->y, it->w, it->h, RADIUS, C_BORDER, bg, 1);
		if (focused && focus_visible)
			outline(it->x - 2, it->y - 2, it->w + 4, it->h + 4, RADIUS + 2, C_ACCENT, 2);
		draw_text(uifont(), UI_SIZE, it->x + (it->w - tw(it->text)) / 2, it->y + (it->h - lh()) / 2, it->text, text);
		break;
	}
	case K_ENTRY: {
		boxed(it->x, it->y, it->w, it->h, RADIUS, focused ? C_ACCENT : C_BORDER, ok ? C_FIELD : C_WINDOW, focused ? 2 : 1);
		clip(it->x + 4, it->y, it->w - 8, it->h);
		double tx = it->x + 10 - it->scroll, ty = it->y + (it->h - lh()) / 2;
		if (focused) {
			int a, b;
			sel_range(it, &a, &b);
			if (a != b) {
				double xa = tcx(it->text, -1, a), xb = tcx(it->text, -1, b);
				fill_rect(tx + fmin(xa, xb), ty, fabs(xb - xa), lh(), C_SELECTION);
			}
		}
		draw_text(uifont(), UI_SIZE, tx, ty, it->text, text);
		if (compose_a >= 0)   /* text being composed: underlined */
			fill_rect(tx + fmin(tcx(it->text, -1, compose_a), tcx(it->text, -1, compose_b)), ty + lh() - 1,
				  fabs(tcx(it->text, -1, compose_b) - tcx(it->text, -1, compose_a)), 1, text);
		if (focused) {
			fill_rect(floor(tx + tcx(it->text, -1, it->cursor)), ty, 1.5, lh(), C_TEXT);
			tell_input_area(w, it->x, it->y, it->h, tx + tcx(it->text, -1, it->cursor));
		}
		unclip();
		break;
	}
	case K_CHECK: {
		double bx = it->x, by = it->y + (it->h - 20) / 2;
		if (it->checked)
			fill_round(bx, by, 20, 20, 4, ok ? C_ACCENT : C_DIM);
		else
			boxed(bx, by, 20, 20, 4, hover ? C_DIM : C_BORDER, C_FIELD, 1.5);
		if (it->checked)
			draw_check_mark(bx + 1, by + 1, C_WHITE);
		if (focused && focus_visible)
			outline(bx - 3, by - 3, 26, 26, 6, C_ACCENT, 2);
		draw_text(uifont(), UI_SIZE, it->x + 28, it->y + (it->h - lh()) / 2, it->text, text);
		break;
	}
	case K_CHOICE: {
		SDL_FColor bg = !ok ? C_WINDOW : w->popup == id ? C_BUTTON_DOWN : hover ? C_BUTTON_HOVER : C_BUTTON;
		boxed(it->x, it->y, it->w, it->h, RADIUS, C_BORDER, bg, 1);
		if (focused && focus_visible)
			outline(it->x - 2, it->y - 2, it->w + 4, it->h + 4, RADIUS + 2, C_ACCENT, 2);
		const char *t = it->selected >= 0 && it->selected < it->nopts ? it->opts[it->selected] : "";
		clip(it->x, it->y, it->w - 30, it->h);
		draw_text(uifont(), UI_SIZE, it->x + 12, it->y + (it->h - lh()) / 2, t, text);
		unclip();
		double cx = it->x + it->w - 18, cy = it->y + it->h / 2.0;
		thick_line(cx - 4, cy - 2, cx, cy + 2, 1.6, text);
		thick_line(cx, cy + 2, cx + 4, cy - 2, 1.6, text);
		break;
	}
	case K_TEXT_AREA: {
		boxed(it->x, it->y, it->w, it->h, RADIUS, focused ? C_ACCENT : C_BORDER, ok ? C_FIELD : C_WINDOW, focused ? 2 : 1);
		double x, y, ww, h;
		area_inside(it, &x, &y, &ww, &h);
		Wrapped wr = wrap(it->text, ww);
		clip(it->x + 2, it->y + 2, it->w - 4, it->h - 4);
		int a, b;
		sel_range(it, &a, &b);
		for (int i = 0; i < wr.n; i++) {
			double ly = y + i * lh() - it->scroll;
			if (ly + lh() < it->y || ly > it->y + it->h)
				continue;
			int s0 = wr.start[i], s1 = wr.end[i];
			if (focused && a != b && b > s0 && a < s1 + (i < wr.n - 1 && wr.end[i] != wr.start[i + 1] ? 1 : 0)) {
				int sa = a > s0 ? a : s0, sb = b < s1 ? b : s1;
				double xa = x + tcx(it->text + s0, s1 - s0, sa - s0), xb = x + tcx(it->text + s0, s1 - s0, sb - s0);
				if (b > s1)
					xb += 6;
				fill_rect(fmin(xa, xb), ly, fabs(xb - xa), lh(), C_SELECTION);
			}
			int n = s1 - s0;
			while (n > 0 && it->text[s0 + n - 1] == '\t')
				n--;
			draw_text_n(uifont(), UI_SIZE, x, ly, it->text + s0, n, text);
			if (compose_a >= 0 && compose_b > s0 && compose_a < s1) {   /* text being composed: underlined */
				int ca = compose_a > s0 ? compose_a : s0, cb = compose_b < s1 ? compose_b : s1;
				double xa = x + tcx(it->text + s0, s1 - s0, ca - s0), xb = x + tcx(it->text + s0, s1 - s0, cb - s0);
				fill_rect(fmin(xa, xb), ly + lh() - 1, fabs(xb - xa), 1, text);
			}
		}
		if (focused && !it->read_only) {
			int line = line_of(&wr, it->cursor);
			double cx = x + tcx(it->text + wr.start[line], wr.end[line] - wr.start[line], it->cursor - wr.start[line]);
			double cy = y + line * lh() - it->scroll;
			fill_rect(floor(cx), cy, 1.5, lh(), C_TEXT);
			tell_input_area(w, it->x, cy, lh(), cx);
		}
		scroll_bar(it->x + it->w - 10, it->y + 4, it->h - 8, wr.n * lh() + 12, it->scroll);
		unwrap(&wr);
		unclip();
		break;
	}
	case K_LIST: {
		boxed(it->x, it->y, it->w, it->h, RADIUS, focused ? C_ACCENT : C_BORDER, ok ? C_FIELD : C_WINDOW, focused ? 2 : 1);
		clip(it->x + 2, it->y + 2, it->w - 4, it->h - 4);
		for (int i = 0; i < it->nopts; i++) {
			double ry = it->y + 1 + i * ROW_H - it->scroll;
			if (ry + ROW_H < it->y || ry > it->y + it->h)
				continue;
			int sel = i == it->selected;
			if (sel)
				fill_rect(it->x + 1, ry, it->w - 2, ROW_H, ok ? C_ACCENT : C_DIM);
			draw_text(uifont(), UI_SIZE, it->x + 10, ry + (ROW_H - lh()) / 2, it->opts[i], sel ? C_WHITE : text);
		}
		scroll_bar(it->x + it->w - 10, it->y + 4, it->h - 8, it->nopts * ROW_H + 8, it->scroll);
		unclip();
		break;
	}
	case K_SLIDER: {
		double vh = slider_value_h(), ty = it->y + vh + 12;
		double x0 = it->x + 10, x1 = it->x + it->w - 10;
		double f = (double)(it->value - it->min) / (it->max - it->min), kx = x0 + (x1 - x0) * f;
		fill_round(x0, ty - 2, x1 - x0, 4, 2, C_TRACK);
		fill_round(x0, ty - 2, kx - x0, 4, 2, ok ? C_ACCENT : C_DIM);
		fill_ellipse(kx, ty, 10, 10, (focused && focus_visible) || hover || down ? C_ACCENT : C_BORDER);
		fill_ellipse(kx, ty, 8.5, 8.5, C_WHITE);
		char v[32];
		snprintf(v, sizeof v, "%d", it->value);
		double vw = tw(v), vx = kx - vw / 2;
		if (vx < it->x)
			vx = it->x;
		if (vx + vw > it->x + it->w)
			vx = it->x + it->w - vw;
		draw_text(uifont(), UI_SIZE, vx, it->y, v, text);
		break;
	}
	case K_PROGRESS:
		fill_round(it->x, it->y + (it->h - 8) / 2.0, it->w, 8, 4, C_TRACK);
		if (it->fraction > 0)
			fill_round(it->x, it->y + (it->h - 8) / 2.0, it->w * it->fraction, 8, 4, C_ACCENT);
		break;
	case K_VIDEO:
		draw_video(id, it, focused);
		break;
	case K_IMAGE:
		if (it->picture >= 0) {
			Picture *p = &pictures[it->picture];
			double k = fmin((double)it->w / p->w, (double)it->h / p->h);
			if (k > 1)
				k = 1;
			SDL_FRect r = { (float)(it->x + (it->w - p->w * k) / 2), (float)(it->y + (it->h - p->h * k) / 2),
					(float)(p->w * k), (float)(p->h * k) };
			SDL_Texture *t = picture_texture(it->picture);
			if (t)
				SDL_RenderTexture(renderer, t, NULL, &r);
		}
		break;
	case K_SEPARATOR: {
		Item *p = &items[it->parent - 1];
		if (p->is_row)
			fill_rect(it->x + it->w / 2.0, it->y, 1, it->h, C_BORDER);
		else
			fill_rect(it->x, it->y + it->h / 2.0, it->w, 1, C_BORDER);
		break;
	}
	case K_CANVAS: {
		SDL_Texture *t = paint_canvas(id, density);
		if (t) {
			SDL_FRect r = { (float)it->x, (float)it->y, (float)it->cw, (float)it->ch };
			SDL_RenderTexture(renderer, t, NULL, &r);
			SDL_DestroyTexture(t);
		}
		if (focused && focus_visible)
			outline(it->x - 2, it->y - 2, it->cw + 4, it->ch + 4, 2, C_ACCENT, 2);
		break;
	}
	}
}

/* ---- what opens over the window: a drop-down's list, a color or font
 * button's choices, or a menu */

static int popup_hover = -1;

/* the menu's rows: an item is ROW_H tall, a dividing line 9 */
static double menu_row_y(Item *m, int i)
{
	double y = 4;
	for (int k = 0; k < i; k++)
		y += items[m->kids[k] - 1].separator ? 9 : ROW_H;
	return y;
}

static int popup_count(Item *p)
{
	switch (p->kind) {
	case K_COLOR: return count_of(offered_colors);
	case K_FONT: return count_of(offered_fonts);
	case K_MENU: return p->nkids;
	}
	return p->nopts;
}

static void popup_rect(int64_t win, double *x, double *y, double *w, double *h)
{
	Item *wi = &items[win - 1], *p = &items[wi->popup - 1];
	int ww, wh;
	SDL_GetWindowSize(wi->sw, &ww, &wh);
	int n = popup_count(p);
	double ax = p->x, ay = p->y + p->h + 4, above = p->y - 4;
	if (p->kind == K_MENU) {
		double widest = 120, keys = 0;
		for (int i = 0; i < p->nkids; i++) {
			Item *k = &items[p->kids[i] - 1];
			if (tw(k->text) > widest)
				widest = tw(k->text);
			if (k->keys_label && tw(k->keys_label) > keys)
				keys = tw(k->keys_label);
		}
		*w = widest + (keys ? keys + 40 : 0) + 40;
		*h = menu_row_y(p, n) + 4;
		ax = p->bar_x;
		ay = BAR_H;
		above = -1;
	} else if (p->kind == K_COLOR) {
		*w = SWATCHES_ACROSS * (SWATCH + 6) + 12;
		*h = ((n + SWATCHES_ACROSS - 1) / SWATCHES_ACROSS) * (SWATCH + 6) + 12;
	} else if (p->kind == K_FONT) {
		*w = 240;
		*h = n * ROW_H + 8;
	} else {
		*w = p->w;
		*h = n * ROW_H + 8;
	}
	if (*h > wh - 8)
		*h = wh - 8;
	*x = ax + *w > ww - 4 ? ww - 4 - *w : ax;
	if (*x < 4)
		*x = 4;
	*y = ay;
	if (*y + *h > wh - 4)
		*y = above >= 0 && above - *h >= 4 ? above - *h : wh - 4 - *h;
}

/* which of the popup's choices is at mx, my (-1: none) */
static int popup_at(int64_t win, double mx, double my)
{
	double x, y, w, h;
	popup_rect(win, &x, &y, &w, &h);
	if (mx < x || mx >= x + w || my < y || my >= y + h)
		return -1;
	Item *p = &items[items[win - 1].popup - 1];
	if (p->kind == K_COLOR) {
		int c = (int)floor((mx - x - 6) / (SWATCH + 6)), r = (int)floor((my - y - 6) / (SWATCH + 6));
		int i = r * SWATCHES_ACROSS + c;
		return c >= 0 && c < SWATCHES_ACROSS && i >= 0 && i < popup_count(p) ? i : -1;
	}
	if (p->kind == K_MENU) {
		for (int i = 0; i < p->nkids; i++) {
			double ry = y + menu_row_y(p, i), rh = items[p->kids[i] - 1].separator ? 9 : ROW_H;
			if (my >= ry && my < ry + rh)
				return items[p->kids[i] - 1].separator ? -1 : i;
		}
		return -1;
	}
	int i = (int)floor((my - y - 4) / ROW_H);
	return i >= 0 && i < popup_count(p) ? i : -1;
}

static void draw_popup(int64_t win)
{
	Item *p = &items[items[win - 1].popup - 1];
	double x, y, w, h;
	popup_rect(win, &x, &y, &w, &h);
	fill_round(x - 1, y + 1, w + 2, h + 2, RADIUS + 1, C_SHADOW);
	boxed(x, y, w, h, RADIUS, C_BORDER, C_FIELD, 1);
	clip((int)x, (int)y, (int)w, (int)h);
	if (p->kind == K_COLOR) {
		for (int i = 0; offered_colors[i]; i++) {
			double sx = x + 6 + (i % SWATCHES_ACROSS) * (SWATCH + 6), sy = y + 6 + (i / SWATCHES_ACROSS) * (SWATCH + 6);
			if (i == popup_hover || !strcmp(offered_colors[i], p->text))
				outline(sx - 2, sy - 2, SWATCH + 4, SWATCH + 4, 5, i == popup_hover ? C_ACCENT : C_TEXT, 2);
			boxed(sx, sy, SWATCH, SWATCH, 4, C_BORDER, fcolor(color(offered_colors[i])), 1);
		}
	} else if (p->kind == K_MENU) {
		for (int i = 0; i < p->nkids; i++) {
			Item *k = &items[p->kids[i] - 1];
			double ry = y + menu_row_y(p, i);
			if (k->separator) {
				fill_rect(x + 8, ry + 4, w - 16, 1, C_BORDER);
				continue;
			}
			if (i == popup_hover && !k->disabled)
				fill_rect(x + 1, ry, w - 2, ROW_H, C_SELECTION);
			SDL_FColor c = k->disabled ? C_DIM : C_TEXT;
			draw_text(uifont(), UI_SIZE, x + 14, ry + (ROW_H - lh()) / 2, k->text, c);
			if (k->keys_label && *k->keys_label)
				draw_text(uifont(), UI_SIZE, x + w - 14 - tw(k->keys_label), ry + (ROW_H - lh()) / 2, k->keys_label, C_DIM);
		}
	} else {
		int n = popup_count(p);
		for (int i = 0; i < n; i++) {
			double ry = y + 4 + i * ROW_H;
			if (i == popup_hover)
				fill_rect(x + 1, ry, w - 2, ROW_H, C_SELECTION);
			if (p->kind == K_FONT) {
				double size;
				int f = font_named(offered_fonts[i], &size);
				draw_text(f, 15, x + 12, ry + (ROW_H - line_height(f, 15)) / 2, offered_fonts[i], C_TEXT);
				if (!strcmp(offered_fonts[i], p->text))
					draw_check_mark(x + w - 28, ry + (ROW_H - 18) / 2, C_TEXT);
			} else {
				draw_text(uifont(), UI_SIZE, x + 12, ry + (ROW_H - lh()) / 2, p->opts[i], C_TEXT);
				if (i == p->selected)
					draw_check_mark(x + w - 28, ry + (ROW_H - 18) / 2, C_TEXT);
			}
		}
	}
	unclip();
}

/* where the dialog's card and buttons are */
static void dialog_rects(int64_t win, double *x, double *y, double *w, double *h, Wrapped *lines, double bx[2], double *by,
			 double *bw)
{
	Item *wi = &items[win - 1];
	int ww, wh;
	SDL_GetWindowSize(wi->sw, &ww, &wh);
	*w = ww - 40 < 400 ? ww - 40 : 400;
	*lines = wrap(wi->dialog_text, *w - 48);
	*bw = 90;
	*h = 24 + lines->n * lh() + 24 + FIELD_H + 20;
	*x = (ww - *w) / 2;
	*y = (wh - *h) / 2;
	*by = *y + *h - 20 - FIELD_H;
	bx[1] = *x + *w - 24 - *bw;
	bx[0] = wi->dialog == D_ASK ? bx[1] - 8 - *bw : bx[1];
}

static void draw_dialog(int64_t win)
{
	Item *wi = &items[win - 1];
	int ww, wh;
	SDL_GetWindowSize(wi->sw, &ww, &wh);
	fill_rect(0, 0, ww, wh, (SDL_FColor){ 0, 0, 0, dark ? 0.45f : 0.25f });
	double x, y, w, h, bx[2], by, bw;
	Wrapped lines;
	dialog_rects(win, &x, &y, &w, &h, &lines, bx, &by, &bw);
	fill_round(x - 2, y + 2, w + 4, h + 4, 14, C_SHADOW);
	boxed(x, y, w, h, 12, C_BORDER, C_WINDOW, 1);
	for (int i = 0; i < lines.n; i++)
		draw_text_n(uifont(), UI_SIZE, x + 24, y + 24 + i * lh(), wi->dialog_text + lines.start[i],
			    lines.end[i] - lines.start[i], C_TEXT);
	unwrap(&lines);
	const char *labels[2] = { "No", wi->dialog == D_ASK ? "Yes" : "OK" };
	for (int b = wi->dialog == D_ASK ? 0 : 1; b < 2; b++) {
		int on = (wi->dialog == D_ASK ? b : 0) == wi->dialog_button;
		boxed(bx[b], by, bw, FIELD_H, RADIUS, on ? C_ACCENT : C_BORDER, on ? C_ACCENT : C_BUTTON, 1);
		draw_text(uifont(), UI_SIZE, bx[b] + (bw - tw(labels[b])) / 2, by + (FIELD_H - lh()) / 2, labels[b],
			  on ? C_WHITE : C_TEXT);
	}
}

static void draw_menu_bar(int64_t win)
{
	Item *w = &items[win - 1];
	if (!w->nmenus)
		return;
	int ww, wh;
	SDL_GetWindowSize(w->sw, &ww, &wh);
	fill_rect(0, 0, ww, BAR_H, C_BAR);
	fill_rect(0, BAR_H - 1, ww, 1, C_BORDER);
	for (int i = 0; i < w->nmenus; i++) {
		Item *m = &items[w->menus[i] - 1];
		if (w->popup == w->menus[i])
			fill_round(m->bar_x, 3, m->bar_w, BAR_H - 6, 4, C_BUTTON_DOWN);
		draw_text(uifont(), UI_SIZE, m->bar_x + 10, (BAR_H - lh()) / 2, m->text, C_TEXT);
	}
}

static void draw_window(int64_t win)
{
	Item *w = &items[win - 1];
	if (!w->r)
		return;
	renderer = w->r;
	density = w->density;
	layout(win);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	Item *box = &items[w->box - 1];
	SDL_FColor bg = box->has_paper ? box->paper : C_WINDOW;   /* (set_background on the window: all of it) */
	SDL_SetRenderDrawColorFloat(renderer, bg.r, bg.g, bg.b, 1);
	SDL_RenderClear(renderer);
	draw_item(w->box, win);
	draw_menu_bar(win);
	if (w->popup)
		draw_popup(win);
	if (w->dialog)
		draw_dialog(win);
	w->dirty = 0;
}

static void show(int64_t win)
{
	draw_window(win);
	if (items[win - 1].r)
		SDL_RenderPresent(items[win - 1].r);
}

/* a picture of the whole window, saved as a PNG file */
static int screenshot(int64_t win, const char *path)
{
	Item *w = &items[win - 1];
	if (!w->r)
		return 0;
	int ww, wh;
	SDL_GetWindowSize(w->sw, &ww, &wh);
	renderer = w->r;
	SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, ww, wh);
	if (!t)
		return 0;
	SDL_SetRenderTarget(renderer, t);
	float was = w->density;
	w->density = 1;
	SDL_SetRenderScale(renderer, 1, 1);
	draw_window(win);
	SDL_Surface *s = SDL_RenderReadPixels(renderer, NULL);
	SDL_SetRenderTarget(renderer, NULL);
	w->density = was;
	SDL_SetRenderScale(renderer, was, was);
	SDL_DestroyTexture(t);
	int ok = 0;
	if (s) {
		ok = save_png(s, path);
		SDL_DestroySurface(s);
	}
	return ok;
}

/* ------------------------------------------------------------- the mouse and keys */

static void open_popup(int64_t win, int64_t id)
{
	Item *p = &items[id - 1];
	if (popup_count(p) == 0)
		return;
	items[win - 1].popup = id;
	popup_hover = -1;
	if (p->kind == K_CHOICE)
		popup_hover = p->selected;
	else if (p->kind == K_FONT)
		for (int i = 0; offered_fonts[i]; i++)
			if (!strcmp(offered_fonts[i], p->text))
				popup_hover = i;
	items[win - 1].dirty = 1;
}

static void close_popup(int64_t win)
{
	items[win - 1].popup = 0;
	items[win - 1].dirty = 1;
}

/* the popup's choice i, chosen */
static void choose(int64_t win, int i)
{
	int64_t id = items[win - 1].popup;
	close_popup(win);
	if (i < 0)
		return;
	Item *p = &items[id - 1];
	switch (p->kind) {
	case K_CHOICE: cby_gui_select(id, i); break;
	case K_COLOR: cby_gui_color_set(id, offered_colors[i]); break;
	case K_FONT: cby_gui_font_set(id, offered_fonts[i]); break;
	case K_MENU: {
		Item *k = &items[p->kids[i] - 1];
		if (!k->separator && !k->disabled)
			call(k->on_click, win);
		break;
	}
	}
}

/* the menu whose title is at x on the bar (0: none) */
static int64_t menu_at(int64_t win, double x)
{
	Item *w = &items[win - 1];
	for (int i = 0; i < w->nmenus; i++) {
		Item *m = &items[w->menus[i] - 1];
		if (x >= m->bar_x && x < m->bar_x + m->bar_w)
			return w->menus[i];
	}
	return 0;
}

static void slide_to(Item *it, double mx)
{
	double x0 = it->x + 10, x1 = it->x + it->w - 10, f = (mx - x0) / (x1 - x0);
	if (f < 0)
		f = 0;
	if (f > 1)
		f = 1;
	set_slider(it, it->min + (int)floor(f * (it->max - it->min) + 0.5));
}

static int tab_at(Item *t, double mx)
{
	double x = t->x + 4;
	for (int i = 0; i < t->nopts; i++) {
		double w = tw(t->opts[i]) + 28;
		if (mx >= x && mx < x + w)
			return i;
		x += w;
	}
	return -1;
}

static int clicks = 1;   /* 2: a double click, 3: a triple one */

static void mouse_down(int64_t win, double mx, double my)
{
	Item *w = &items[win - 1];
	focus_visible = 0;
	if (w->dialog) {
		double x, y, dw, dh, bx[2], by, bw;
		Wrapped lines;
		dialog_rects(win, &x, &y, &dw, &dh, &lines, bx, &by, &bw);
		unwrap(&lines);
		for (int b = w->dialog == D_ASK ? 0 : 1; b < 2; b++)
			if (mx >= bx[b] && mx < bx[b] + bw && my >= by && my < by + FIELD_H)
				answer_dialog(win, w->dialog == D_ASK ? b : 0);
		return;
	}
	if (my < bar_h(win)) {
		int64_t m = menu_at(win, mx);
		if (m && w->popup == m)
			close_popup(win);
		else if (m)
			open_popup(win, m);
		return;
	}
	if (w->popup) {
		choose(win, popup_at(win, mx, my));
		return;
	}
	int64_t id = at(w->box, mx, my);
	if (!id || !usable(id))
		return;
	if (takes_focus(id))
		set_focus(win, id);
	w = &items[win - 1];
	w->pressed = id;
	w->dragging = 1;
	w->dirty = 1;
	Item *it = &items[id - 1];
	switch (it->kind) {
	case K_CHOICE: case K_COLOR: case K_FONT:
		open_popup(win, id);
		break;
	case K_SLIDER:
		slide_to(it, mx);
		break;
	case K_LIST: {
		int i = (int)floor((my - it->y - 1 + it->scroll) / ROW_H);
		if (i >= 0 && i < it->nopts)
			cby_gui_select(id, i);
		break;
	}
	case K_TABS:
		if (my < it->y + TAB_H)
			show_tab(id, tab_at(it, mx));
		break;
	case K_TABLE:
		if (my < it->y + ROW_H + 1) {
			int cols = it->nopts ? it->nopts : 1, c = (int)floor((mx - it->x - 1) / ((it->w - 2) / (double)cols));
			if (c >= 0 && c < it->nopts)
				sort_table(id, c, it->sort_col == c ? !it->sort_down : 0);
		} else {
			int i = (int)floor((my - it->y - ROW_H - 1 + it->scroll) / ROW_H);
			if (i >= 0 && i < it->nrows)
				select_row(id, it->order[i]);
		}
		break;
	case K_NUMBER: {
		double bx = it->x + it->w - 2 * FIELD_H;
		if (mx >= bx + FIELD_H)
			set_number(it, (long)it->value + 1);
		else if (mx >= bx)
			set_number(it, (long)it->value - 1);
		break;
	}
	case K_ENTRY: case K_TEXT_AREA: {
		int at_ = hit_text(it, mx, my);
		if (clicks == 2)
			select_word_at(it, at_);
		else if (clicks >= 3)
			select_line_at(it, at_);
		else
			it->cursor = it->anchor = at_;
		it->typing_end = -1;
		break;
	}
	case K_CANVAS:
		canvas_event(id, 0, mx - it->x, my - it->y);
		break;
	case K_VIDEO:
		video_click(it, mx, my);
		break;
	}
}

static void mouse_up(int64_t win, double mx, double my)
{
	Item *w = &items[win - 1];
	int64_t id = w->pressed;
	w->dragging = 0;
	w->pressed = 0;
	w->dirty = 1;
	if (!id || !usable(id))
		return;
	Item *it = &items[id - 1];
	int inside = mx >= it->x && my >= it->y && mx < it->x + it->w && my < it->y + it->h;
	if (it->kind == K_BUTTON && inside)
		call(it->on_click, win);
	else if (it->kind == K_CHECK && inside)
		cby_gui_check_set(id, !it->checked);
}

static void mouse_move(int64_t win, double mx, double my)
{
	Item *w = &items[win - 1];
	if (w->dialog)
		return;
	if (w->popup) {
		/* over another menu's title, with a menu open: that one opens */
		if (items[w->popup - 1].kind == K_MENU && my < bar_h(win)) {
			int64_t m = menu_at(win, mx);
			if (m && m != w->popup)
				open_popup(win, m);
			return;
		}
		int i = popup_at(win, mx, my);
		if (i != popup_hover) {
			popup_hover = i;
			w->dirty = 1;
		}
		return;
	}
	int64_t h = at(w->box, mx, my);
	if (h != w->hover) {
		w->hover = h;
		w->dirty = 1;
	}
	if (w->dragging && w->pressed) {
		Item *it = &items[w->pressed - 1];
		if (it->kind == K_SLIDER)
			slide_to(it, mx);
		else if (it->kind == K_ENTRY || it->kind == K_TEXT_AREA) {
			it->cursor = hit_text(it, mx, my);
			keep_cursor_in_view(it);
			w->dirty = 1;
		}
	}
	if (h && items[h - 1].kind == K_CANVAS && items[h - 1].moved.fn)
		canvas_event(h, 1, mx - items[h - 1].x, my - items[h - 1].y);
}

static void wheel(int64_t win, double mx, double my, double dy)
{
	if (items[win - 1].dialog)
		return;
	int64_t id = at(items[win - 1].box, mx, my);
	for (; id; id = items[id - 1].parent) {
		Item *it = &items[id - 1];
		if (it->kind == K_LIST || it->kind == K_TEXT_AREA) {
			scroll_by(it, -dy * 3 * lh());
			return;
		}
		if (it->kind == K_TABLE || (it->kind == K_BOX && it->scrolls)) {
			double content = it->kind == K_TABLE ? it->nrows * ROW_H : it->content_h;
			double inner = it->kind == K_TABLE ? it->h - ROW_H - 2 : it->h - 12;
			it->scroll -= dy * 3 * lh();
			if (it->scroll > content - inner)
				it->scroll = content - inner;
			if (it->scroll < 0)
				it->scroll = 0;
			items[win - 1].dirty = 1;
			return;
		}
	}
}

/* the gui library's name for a key: a character as itself, others by name */
static void gui_key_name(SDL_Keycode k, SDL_Keymod mods, char *out, size_t size)
{
	sdl_key_name(k, out, size);
	if (strcmp(out, "space") == 0)
		snprintf(out, size, " ");
	else if (out[0] >= 'a' && out[0] <= 'z' && !out[1] && (mods & SDL_KMOD_SHIFT))
		out[0] = (char)(out[0] - 'a' + 'A');
}

static int takes_text(int64_t id)
{
	int k = id ? items[id - 1].kind : 0;
	return k == K_ENTRY || k == K_TEXT_AREA || k == K_NUMBER;
}

/* whether what has the keys does something with this key (so it isn't
 * the window's own) */
static int uses_key(int64_t id, SDL_Keycode k)
{
	if (!id || !usable(id))
		return 0;
	Item *it = &items[id - 1];
	int activate = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE;
	int arrows = k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT;
	int moves = arrows || k == SDLK_HOME || k == SDLK_END || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN;
	switch (it->kind) {
	case K_ENTRY: case K_TEXT_AREA:
		return k != SDLK_ESCAPE && !(k >= SDLK_F1 && k <= SDLK_F12);
	case K_NUMBER:
		return moves || k == SDLK_BACKSPACE || k == SDLK_MINUS || (k >= SDLK_0 && k <= SDLK_9);
	case K_BUTTON: case K_CHECK:
		return activate;
	case K_CHOICE: case K_COLOR: case K_FONT:
		return activate || k == SDLK_DOWN;
	case K_SLIDER: case K_LIST: case K_TABLE:
		return moves;
	case K_TABS:
		return k == SDLK_LEFT || k == SDLK_RIGHT;
	case K_CANVAS:
		return it->keyed.fn != NULL;
	}
	return 0;
}

/* a menu's items, up or down from i, past dividing lines */
static int next_menu_row(Item *m, int i, int by)
{
	for (int k = 0; k < m->nkids; k++) {
		i = (i + by + m->nkids) % m->nkids;
		if (!items[m->kids[i] - 1].separator)
			return i;
	}
	return -1;
}

static void popup_key(int64_t win, SDL_Keycode k)
{
	Item *w = &items[win - 1], *p = &items[w->popup - 1];
	int n = popup_count(p), across = p->kind == K_COLOR ? SWATCHES_ACROSS : 1;
	if (k == SDLK_ESCAPE) {
		close_popup(win);
	} else if (p->kind == K_MENU && (k == SDLK_UP || k == SDLK_DOWN)) {
		popup_hover = next_menu_row(p, popup_hover < 0 ? (k == SDLK_DOWN ? -1 : 0) : popup_hover, k == SDLK_DOWN ? 1 : -1);
	} else if (p->kind == K_MENU && (k == SDLK_LEFT || k == SDLK_RIGHT)) {
		for (int i = 0; i < w->nmenus; i++)
			if (w->menus[i] == w->popup) {
				open_popup(win, w->menus[(i + (k == SDLK_RIGHT ? 1 : -1) + w->nmenus) % w->nmenus]);
				break;
			}
	} else if (k == SDLK_UP) {
		popup_hover = popup_hover < 0 ? 0 : popup_hover - across >= 0 ? popup_hover - across : popup_hover;
	} else if (k == SDLK_DOWN) {
		popup_hover = popup_hover < 0 ? 0 : popup_hover + across < n ? popup_hover + across : popup_hover;
	} else if (k == SDLK_LEFT && across > 1) {
		popup_hover = popup_hover > 0 ? popup_hover - 1 : 0;
	} else if (k == SDLK_RIGHT && across > 1) {
		popup_hover = popup_hover < n - 1 ? popup_hover + 1 : n - 1;
	} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
		choose(win, popup_hover);
	}
	items[win - 1].dirty = 1;
}

static void key_down(int64_t win, SDL_Keycode k, SDL_Keymod mods)
{
	Item *w = &items[win - 1];
	w->dirty = 1;
	if (w->dialog) {
		if (k == SDLK_ESCAPE)
			answer_dialog(win, 0);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE)
			answer_dialog(win, w->dialog_button);
		else if (w->dialog == D_ASK && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_TAB))
			w->dialog_button = !w->dialog_button;
		return;
	}
	if (w->popup) {
		popup_key(win, k);
		return;
	}
	if (k == SDLK_F10 && w->nmenus) {
		open_popup(win, w->menus[0]);
		popup_hover = next_menu_row(&items[w->menus[0] - 1], -1, 1);
		return;
	}
	/* a shortcut: with Ctrl or Alt, an F key, or (when the keys aren't
	 * going to something typed in) a key on its own */
	char name[16];
	sdl_key_name(k, name, sizeof name);
	int fkey = name[0] == 'f' && name[1] >= '1' && name[1] <= '9';
	if (name[0] && ((mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) || fkey || !takes_text(w->focus))) {
		char norm[48];
		snprintf(norm, sizeof norm, "%s%s%s%s", (mods & SDL_KMOD_CTRL) ? "ctrl+" : "", (mods & SDL_KMOD_SHIFT) ? "shift+" : "",
			 (mods & SDL_KMOD_ALT) ? "alt+" : "", name);
		if (run_shortcut(win, norm))
			return;
	}
	if (k == SDLK_TAB && !(mods & SDL_KMOD_CTRL)) {
		focus_visible = 1;
		move_focus(win, (mods & SDL_KMOD_SHIFT) ? -1 : 1);
		return;
	}
	int64_t id = w->focus;
	if (w->keyed.fn && !uses_key(id, k)) {
		/* a key nothing used: the window's own */
		char kn[16], full[32];
		gui_key_name(k, mods, kn, sizeof kn);
		if (kn[0]) {
			snprintf(full, sizeof full, "%s%s%s", (mods & SDL_KMOD_CTRL) ? "ctrl+" : "", (mods & SDL_KMOD_ALT) ? "alt+" : "", kn);
			snprintf(w->key, sizeof w->key, "%s", full);
			call(w->keyed, win);
			return;
		}
	}
	if (!id || !usable(id))
		return;
	Item *it = &items[id - 1];
	int activate = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE;
	switch (it->kind) {
	case K_VIDEO:
		if (video_key(it, k))
			dirty(id);
		break;
	case K_BUTTON:
		if (activate) {
			focus_visible = 1;
			call(it->on_click, win);
		}
		break;
	case K_CHECK:
		if (activate)
			cby_gui_check_set(id, !it->checked);
		break;
	case K_CHOICE: case K_COLOR: case K_FONT:
		if (activate || k == SDLK_DOWN)
			open_popup(win, id);
		break;
	case K_SLIDER:
		if (k == SDLK_LEFT || k == SDLK_DOWN) set_slider(it, it->value - 1);
		else if (k == SDLK_RIGHT || k == SDLK_UP) set_slider(it, it->value + 1);
		else if (k == SDLK_PAGEDOWN) set_slider(it, it->value - 10);
		else if (k == SDLK_PAGEUP) set_slider(it, it->value + 10);
		else if (k == SDLK_HOME) set_slider(it, it->min);
		else if (k == SDLK_END) set_slider(it, it->max);
		break;
	case K_NUMBER:
		if (k == SDLK_DOWN) set_number(it, (long)it->value - 1);
		else if (k == SDLK_UP) set_number(it, (long)it->value + 1);
		else if (k == SDLK_PAGEDOWN) set_number(it, (long)it->value - 10);
		else if (k == SDLK_PAGEUP) set_number(it, (long)it->value + 10);
		else if (k == SDLK_HOME) set_number(it, it->min);
		else if (k == SDLK_END) set_number(it, it->max);
		else if (k == SDLK_BACKSPACE) set_number(it, it->value / 10);
		else if (k == SDLK_MINUS) set_number(it, -(long)it->value);
		break;
	case K_TABS:
		if (k == SDLK_LEFT) show_tab(id, it->selected - 1);
		else if (k == SDLK_RIGHT) show_tab(id, it->selected + 1);
		break;
	case K_LIST: {
		int n = it->nopts, s = it->selected;
		if (!n)
			break;
		if (k == SDLK_UP) s = s <= 0 ? 0 : s - 1;
		else if (k == SDLK_DOWN) s = s < 0 ? 0 : s + 1 < n ? s + 1 : s;
		else if (k == SDLK_HOME) s = 0;
		else if (k == SDLK_END) s = n - 1;
		else if (k == SDLK_PAGEUP) s = s - 5 < 0 ? 0 : s - 5;
		else if (k == SDLK_PAGEDOWN) s = s + 5 >= n ? n - 1 : s + 5;
		cby_gui_select(id, s);
		break;
	}
	case K_TABLE: {
		int n = it->nrows, at_ = -1;
		if (!n)
			break;
		for (int i = 0; i < n; i++)
			if (it->order[i] == it->selected)
				at_ = i;
		if (k == SDLK_UP) at_ = at_ <= 0 ? 0 : at_ - 1;
		else if (k == SDLK_DOWN) at_ = at_ < 0 ? 0 : at_ + 1 < n ? at_ + 1 : at_;
		else if (k == SDLK_HOME) at_ = 0;
		else if (k == SDLK_END) at_ = n - 1;
		else if (k == SDLK_PAGEUP) at_ = at_ - 5 < 0 ? 0 : at_ - 5;
		else if (k == SDLK_PAGEDOWN) at_ = at_ + 5 >= n ? n - 1 : at_ + 5;
		else break;
		select_row(id, it->order[at_]);
		break;
	}
	case K_ENTRY: case K_TEXT_AREA:
		edit_key(id, k, mods);
		break;
	case K_CANVAS:
		if (it->keyed.fn) {
			char cname[16];
			gui_key_name(k, mods, cname, sizeof cname);
			if (cname[0])
				canvas_key(id, cname);
		}
		break;
	}
}

static void text_typed(int64_t win, const char *text)
{
	if (items[win - 1].dialog)
		return;
	int64_t id = items[win - 1].focus;
	if (id && usable(id) && items[id - 1].kind == K_NUMBER) {
		/* digits on the end of the number */
		Item *it = &items[id - 1];
		for (const char *c = text; *c; c++)
			if (*c >= '0' && *c <= '9') {
				long v = (long)it->value * 10 + (it->value < 0 ? -(*c - '0') : *c - '0');
				if (v > it->max || v < it->min)
					v = *c - '0';
				set_number(it, v);
			}
		return;
	}
	composing_done(win);
	if (id && usable(id) && (items[id - 1].kind == K_ENTRY || items[id - 1].kind == K_TEXT_AREA)) {
		insert_text_as(id, text, 1);
		if (id <= nitems)
			keep_cursor_in_view(&items[id - 1]);
		dirty(id);
	}
}

/* ------------------------------------------------------------- processes
 *
 * Cabby's event loop (in its runtime): while a window is open, what it
 * watches (processes' output) is looked at every few milliseconds, and
 * their handlers called. */

static int (*ev_fds)(int *, int);
static void (*ev_step)(int);

static void find_events(void)
{
	static int looked;
	if (looked)
		return;
	looked = 1;
	ev_fds = (int (*)(int *, int))dlsym(RTLD_DEFAULT, "cby_events_fds");
	ev_step = (void (*)(int))dlsym(RTLD_DEFAULT, "cby_events_step");
	if (!ev_fds || !ev_step)
		ev_fds = NULL, ev_step = NULL;
}

static void settle_processes(void)
{
	find_events();
	int fds[64];
	while (ev_fds && ev_fds(fds, 64) > 0)
		ev_step(50);
}

/* ------------------------------------------------------------- running */


/* While the interactive session waits for typing, the window system is
 * still answered (see rt_idle_hook in the runtime). */
static void keep_in_touch(void)
{
	SDL_PumpEvents();
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}

static void set_idle_hook(void)
{
	void (**hook)(void) = (void (**)(void))dlsym(RTLD_DEFAULT, "rt_idle_hook");
	if (hook)
		*hook = keep_in_touch;
}

static void start_sdl(void)
{
	static int started;
	if (started)
		return;
	if (scripted()) {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");   /* drawn, but not shown */
		/* and by SDL's own drawing, which needs no graphics library (a Mac
		 * has no OpenGL ES for the offscreen driver to find) and draws the
		 * same on every computer */
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	}
	SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");   /* text being composed is drawn in the text box */
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "gui: cannot open a window: %s\n", SDL_GetError());
		ssh_instead("gui", "tui");
		exit(1);
	}
	ssh_note("gui", "tui");
	started = 1;
	choose_look();
	set_idle_hook();
}

static int64_t window_of(SDL_WindowID id)
{
	for (int64_t i = 1; i <= nitems; i++)
		if (items[i - 1].kind == K_WINDOW && items[i - 1].sw && SDL_GetWindowID(items[i - 1].sw) == id)
			return i;
	return 0;
}

/* On a Mac, Cmd does what Ctrl does elsewhere: Cmd+C copies, Cmd+Z undoes,
 * and a "Ctrl+S" shortcut is Cmd+S. (Ctrl itself then does nothing extra.) */
static SDL_Keymod mac_keys(SDL_Keymod m)
{
#if defined(__APPLE__)
	SDL_Keymod out = (SDL_Keymod)(m & ~(SDL_KMOD_CTRL | SDL_KMOD_GUI));
	if (m & SDL_KMOD_GUI)
		out |= SDL_KMOD_CTRL;
	return out;
#else
	return m;
#endif
}

static void handle(SDL_Event *e)
{
	if (answer_event && e->type == answer_event) {
		file_answered();
		return;
	}
	if (e->type == SDL_EVENT_SYSTEM_THEME_CHANGED) {   /* the desktop went dark, or light */
		choose_look();
		for (int64_t i = 1; i <= nitems; i++)
			if (items[i - 1].kind == K_WINDOW)
				items[i - 1].dirty = 1;
		return;
	}
	int64_t win = 0;
	switch (e->type) {
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED: case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		win = window_of(e->window.windowID);
		break;
	case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
		win = window_of(e->key.windowID);
		break;
	case SDL_EVENT_TEXT_INPUT:
		win = window_of(e->text.windowID);
		break;
	case SDL_EVENT_TEXT_EDITING:
		win = window_of(e->edit.windowID);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
		win = window_of(e->button.windowID);
		break;
	case SDL_EVENT_MOUSE_MOTION:
		win = window_of(e->motion.windowID);
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		win = window_of(e->wheel.windowID);
		break;
	}
	if (!win || !items[win - 1].open)
		return;
	Item *w = &items[win - 1];
	switch (e->type) {
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		w->closing = 1;
		SDL_HideWindow(w->sw);
		break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		w->density = SDL_GetWindowPixelDensity(w->sw);
		SDL_SetRenderScale(w->r, w->density, w->density);
		w->dirty = 1;
		break;
	case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_EXPOSED:
		w->dirty = 1;
		break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		w->hover = 0;
		w->dirty = 1;
		break;
	case SDL_EVENT_KEY_DOWN:
		if (composing && composing[0] && composing_win == win)
			break;   /* (the keys are the input method's while it's composing) */
		key_down(win, e->key.key, mac_keys(e->key.mod));
		break;
	case SDL_EVENT_TEXT_INPUT:
		text_typed(win, e->text.text);
		break;
	case SDL_EVENT_TEXT_EDITING:
		composing_now(win, e->edit.text, e->edit.start);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (e->button.button == SDL_BUTTON_LEFT) {
			clicks = e->button.clicks;
			mouse_down(win, e->button.x, e->button.y);
			clicks = 1;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (e->button.button == SDL_BUTTON_LEFT)
			mouse_up(win, e->button.x, e->button.y);
		break;
	case SDL_EVENT_MOUSE_MOTION:
		mouse_move(win, e->motion.x, e->motion.y);
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		wheel(win, e->wheel.mouse_x, e->wheel.mouse_y, e->wheel.y);
		break;
	}
}

/* draws every open window that has changed */
static void a11y_tick(void);
static void a11y_start(void);

static void show_changed(void)
{
	a11y_tick();
	for (int64_t i = 1; i <= nitems; i++)
		if (items[i - 1].kind == K_WINDOW && items[i - 1].open && items[i - 1].dirty && !items[i - 1].closing)
			show(i);
}

static void run_script(int64_t win);
static char *script_left;
static char *script_all;      /* (kept, so it's freed with the program, not lost) */
static int script_loaded;

void cby_gui_window_run(int64_t win)
{
	Item *w = item(win, K_WINDOW);
	if (w->destroyed || w->open)
		return;
	start_sdl();
	if (!w->sw) {
		SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
		if (!make_window(w->title, w->width, w->height, flags, scripted(), &w->sw, &w->r)) {
			fprintf(stderr, "gui: cannot open a window: %s\n", SDL_GetError());
			ssh_instead("gui", "tui");
			exit(1);
		}
		w = &items[win - 1];
		w->density = SDL_GetWindowPixelDensity(w->sw);
		if (w->density <= 0)
			w->density = 1;
		SDL_SetRenderScale(w->r, w->density, w->density);
		if (!scripted())
			SDL_SetRenderVSync(w->r, 1);   /* (not for a test's: SDL would wait a 60th of a second a frame) */
	}
	/* big enough for what's in it */
	renderer = w->r;
	density = w->density;
	int nw, nh;
	natural(w->box, &nw, &nh);
	nw += 2 * MARGIN;
	nh += 2 * MARGIN + bar_h(win);
	SDL_SetWindowMinimumSize(w->sw, nw, nh);
	SDL_SetWindowSize(w->sw, w->width > nw ? w->width : nw, w->height > nh ? w->height : nh);
	w->open = 1;
	w->closing = 0;
	w->dirty = 1;
	SDL_ShowWindow(w->sw);
	if (!w->focus)
		move_focus(win, 1);
	show(win);
	a11y_start();
	a11y_tick();
	if (scripted()) {
		if (!script_loaded) {
			script_loaded = 1;
			script_left = script_all = strdup(getenv("CABBY_GUI_SCRIPT"));
		}
		run_script(win);
	} else {
		find_events();
		while (!items[win - 1].closing && !items[win - 1].destroyed) {
			Uint64 next = run_timers(SDL_GetTicks());
			Sint32 wait = -1;
			if (next) {
				Uint64 now = SDL_GetTicks();
				wait = next > now ? (Sint32)(next - now) : 0;
			}
			if (ev_step && (wait < 0 || wait > 10))
				wait = 10;
			if (videos_tick(win) && (wait < 0 || wait > 5))
				wait = 5;
			SDL_Event e;
			if (SDL_WaitEventTimeout(&e, wait)) {
				handle(&e);
				while (SDL_PollEvent(&e))
					handle(&e);
			}
			if (ev_step)
				ev_step(0);
			show_changed();
		}
	}
	if (!items[win - 1].destroyed)
		end_window(win);
	fflush(stdout);
}

/* ------------------------------------------------------------- videos
 *
 * What's shown comes from the videos library, through video_source.h: the
 * widget asks it for the picture for now, as it's drawn, and plays its
 * sound (SDL's, on a thread of its own, asking it for more as it goes). It
 * plays by the window's clock: under a test script, the script's (and
 * without its sound). */

static double video_clock(void) { return (double)now_ms() / 1000.0; }

int64_t cby_gui_add_video(int64_t box, int64_t source)
{
	CbyVideoSource *v = (CbyVideoSource *)(intptr_t)source;
	if (!v || v->magic != CBY_VIDEO_MAGIC)
		fail("%s", "add_video needs a video's source: Window.add_video Film.source (use videos)");
	int64_t id = add_to(box, K_VIDEO, "");
	items[id - 1].video = v;
	items[id - 1].vends = v->ends(v->self);
	v->attach(v->self, video_clock);
	return id;
}

void cby_gui_video_controls(int64_t id, int show)
{
	item(id, K_VIDEO)->controls = show;
	dirty(id);
}

void cby_gui_video_on_end(int64_t id, void *obj, void *fn) { item(id, K_VIDEO)->ended = (Handler){ obj, fn }; }

/* "1:05", or "1:02:05" */
static void clock_text(double t, char *out, size_t n)
{
	long s = t > 0 ? (long)floor(t + 1e-6) : 0;
	if (s >= 3600)
		snprintf(out, n, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
	else
		snprintf(out, n, "%ld:%02ld", s / 60, s % 60);
}

/* where its controls' parts are: the play button, then the track to seek
 * along, then the time */
static void video_parts(Item *it, double *bar_y, double *track_x, double *track_w)
{
	*bar_y = it->y + it->h - VIDEO_BAR;
	char t[32];
	clock_text(it->video->duration(it->video->self), t, sizeof t);
	double timew = 2 * tw(t) + tw(" / ");
	*track_x = it->x + VIDEO_BAR + 8;
	*track_w = it->x + it->w - 12 - timew - 12 - *track_x;
	if (*track_w < 20)
		*track_w = 20;
}

static void draw_video(int64_t id, Item *it, int focused)
{
	CbyVideoSource *v = it->video;
	double ph = it->h - (it->controls ? VIDEO_BAR : 0);
	fill_rect(it->x, it->y, it->w, ph, (SDL_FColor){ 0, 0, 0, 1 });
	int w = 0, h = 0;
	int64_t serial = 0;
	const uint8_t *px = v->picture(v->self, &w, &h, &serial);
	if (px && w > 0 && h > 0) {
		if (it->vtex && (it->vtex_r != renderer || it->vtw != w || it->vth != h)) {
			if (it->vtex_r == renderer)
				SDL_DestroyTexture(it->vtex);
			it->vtex = NULL;
		}
		if (!it->vtex) {
			it->vtex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
			it->vtex_r = renderer;
			it->vtw = w;
			it->vth = h;
			it->vserial = 0;
		}
		if (it->vtex && serial != it->vserial) {
			SDL_UpdateTexture(it->vtex, NULL, px, w * 4);
			it->vserial = serial;
		}
		if (it->vtex) {
			double k = fmin(it->w / (double)w, ph / (double)h);
			SDL_FRect r = { (float)(it->x + (it->w - w * k) / 2), (float)(it->y + (ph - h * k) / 2), (float)(w * k),
					(float)(h * k) };
			SDL_RenderTexture(renderer, it->vtex, NULL, &r);
		}
	}
	if (it->controls) {
		double by, track_x, track_w;
		video_parts(it, &by, &track_x, &track_w);
		fill_rect(it->x, by, it->w, VIDEO_BAR, C_BAR);
		/* play (a triangle), or pause (two bars) */
		double cx = it->x + VIDEO_BAR / 2.0, cy = by + VIDEO_BAR / 2.0;
		if (v->playing(v->self)) {
			fill_rect(cx - 6, cy - 7, 4, 14, C_TEXT);
			fill_rect(cx + 2, cy - 7, 4, 14, C_TEXT);
		} else {
			float xy[6] = { (float)(cx - 5), (float)(cy - 8), (float)(cx - 5), (float)(cy + 8), (float)(cx + 8), (float)cy };
			int ix[3] = { 0, 1, 2 };
			triangles(xy, 3, ix, 3, C_TEXT);
		}
		double pos = v->position(v->self), len = v->duration(v->self);
		char a[32], b[32], both[80];
		clock_text(pos, a, sizeof a);
		clock_text(len, b, sizeof b);
		snprintf(both, sizeof both, "%s / %s", a, b);
		draw_text(uifont(), UI_SIZE, track_x + track_w + 12, by + (VIDEO_BAR - lh()) / 2 - 1, both, C_TEXT);
		double f = len > 0 ? pos / len : 0;
		if (f > 1)
			f = 1;
		fill_round(track_x, cy - 3, track_w, 6, 3, C_TRACK);
		if (f > 0)
			fill_round(track_x, cy - 3, track_w * f, 6, 3, C_ACCENT);
		fill_round(track_x + track_w * f - 6, cy - 6, 12, 12, 6, C_ACCENT);
	}
	if (focused && focus_visible)
		outline(it->x, it->y, it->w, it->h, RADIUS, C_ACCENT, 2);
	(void)id;
}

/* a click on it: on its picture, playing or pausing; on its controls, the
 * button does the same, and the track goes to that place */
static void video_click(Item *it, double mx, double my)
{
	CbyVideoSource *v = it->video;
	double by, track_x, track_w;
	if (it->controls) {
		video_parts(it, &by, &track_x, &track_w);
		if (my >= by && mx >= track_x - 6 && mx <= track_x + track_w + 6) {
			double f = (mx - track_x) / track_w;
			f = f < 0 ? 0 : f > 1 ? 1 : f;
			v->seek(v->self, f * v->duration(v->self));
			return;
		}
		if (my >= by && mx >= it->x + VIDEO_BAR)
			return;
	}
	if (v->playing(v->self))
		v->pause(v->self);
	else
		v->play(v->self);
}

/* its keys: space plays or pauses; left and right go back and on five
 * seconds; Home and End go to its start and end */
static int video_key(Item *it, SDL_Keycode k)
{
	CbyVideoSource *v = it->video;
	double pos = v->position(v->self);
	if (k == SDLK_SPACE || k == SDLK_K) {
		if (v->playing(v->self))
			v->pause(v->self);
		else
			v->play(v->self);
	} else if (k == SDLK_LEFT) {
		v->seek(v->self, pos - 5);
	} else if (k == SDLK_RIGHT) {
		v->seek(v->self, pos + 5);
	} else if (k == SDLK_HOME) {
		v->seek(v->self, 0);
	} else if (k == SDLK_END) {
		v->seek(v->self, v->duration(v->self));
	} else {
		return 0;
	}
	return 1;
}

/* CABBY_SOUND_REPORT=1: what a video's sound did, on stderr (for finding
 * out why one can't be heard) */
static struct {
	int on;
	long long calls, frames, heard;
	float peak;
	double most_ahead;
} vreport;

static void SDLCALL video_sound(void *data, SDL_AudioStream *stream, int wanted, int total)
{
	CbyVideoSource *v = data;
	int frames = wanted / 8;
	if (frames <= 0)
		return;
	float buf[2 * 1024];
	vreport.calls++;
	while (frames > 0) {
		int n = frames > 1024 ? 1024 : frames;
		double ahead = SDL_GetAudioStreamQueued(stream) / 8.0 / CBY_VIDEO_RATE + 0.02;
		v->sound(v->self, buf, n, ahead);
		if (vreport.on) {
			if (ahead > vreport.most_ahead)
				vreport.most_ahead = ahead;
			for (int i = 0; i < 2 * n; i++) {
				float a = buf[i] < 0 ? -buf[i] : buf[i];
				if (a > vreport.peak)
					vreport.peak = a;
				if (a > 0.001f) {
					vreport.heard++;
					break;
				}
			}
			long long before = vreport.frames;
			vreport.frames += n;
			if (vreport.frames / (2 * CBY_VIDEO_RATE) != before / (2 * CBY_VIDEO_RATE))
				fprintf(stderr, "sound report: %.0f seconds given, %lld blocks with sound in them, loudest %.3f\n",
					vreport.frames / (double)CBY_VIDEO_RATE, vreport.heard, vreport.peak);
		}
		SDL_PutAudioStreamData(stream, buf, n * 8);
		frames -= n;
	}
	(void)total;
}

/* each of the window's videos: its sound playing (once it plays), drawn
 * again when its picture changes, and on_end when it's come to its end.
 * True if one is playing (so the window keeps looking). */
static int videos_tick(int64_t win)
{
	int any = 0;
	for (int i = 0; i < nitems; i++) {
		Item *it = &items[i];
		if (it->kind != K_VIDEO || it->win != win || !it->video)
			continue;
		CbyVideoSource *v = it->video;
		int playing = v->playing(v->self);
		if (playing && !it->vsound && v->has_sound(v->self) && !scripted()) {
			static int sound_ready;
			if (!sound_ready) {
				sound_ready = SDL_InitSubSystem(SDL_INIT_AUDIO) ? 1 : -1;
				vreport.on = getenv("CABBY_SOUND_REPORT") != NULL;
				if (vreport.on)
					fprintf(stderr, "sound report: SDL %d.%d.%d, audio driver %s%s%s\n", SDL_MAJOR_VERSION,
						SDL_MINOR_VERSION, SDL_MICRO_VERSION, sound_ready > 0 ? SDL_GetCurrentAudioDriver() : "(none)",
						sound_ready > 0 ? "" : ": ", sound_ready > 0 ? "" : SDL_GetError());
			}
			if (sound_ready > 0) {
				SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, CBY_VIDEO_RATE };
				it->vsound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, video_sound, v);
				if (it->vsound && !SDL_ResumeAudioStreamDevice(it->vsound) && vreport.on)
					fprintf(stderr, "sound report: it didn't start: %s\n", SDL_GetError());
				if (vreport.on && !it->vsound)
					fprintf(stderr, "sound report: no sound device: %s\n", SDL_GetError());
				if (vreport.on && it->vsound) {
					SDL_AudioSpec dev;
					int frames = 0;
					SDL_AudioDeviceID id = SDL_GetAudioStreamDevice(it->vsound);
					if (SDL_GetAudioDeviceFormat(id, &dev, &frames))
						fprintf(stderr, "sound report: playing to \"%s\": %d a second, %d channels, %d frames at a time\n",
							SDL_GetAudioDeviceName(id) ? SDL_GetAudioDeviceName(id) : "?", dev.freq, dev.channels, frames);
					fprintf(stderr, "sound report: its gain %.2f, the device's %.2f\n", SDL_GetAudioStreamGain(it->vsound),
						SDL_GetAudioDeviceGain(id));
				}
			}
		}
		if (playing && !it->hidden) {
			int w, h;
			int64_t serial = 0;
			v->picture(v->self, &w, &h, &serial);
			if (serial != it->vserial || it->controls)
				dirty(i + 1);
			any = 1;
		}
		int64_t ends = v->ends(v->self);
		if (ends != it->vends) {
			it->vends = ends;
			dirty(i + 1);
			call(it->ended, win);
			it = &items[i];   /* (the handler may have added widgets) */
		}
	}
	return any;
}

/* a window closing: its videos pause, and their sound stops */
static void videos_end(int64_t win)
{
	for (int i = 0; i < nitems; i++) {
		Item *it = &items[i];
		if (it->kind != K_VIDEO || it->win != win || !it->video)
			continue;
		it->video->pause(it->video->self);
		if (it->vsound) {
			if (vreport.on)
				fprintf(stderr, "sound report: asked for sound %lld times, %lld frames (%.1f seconds), %lld blocks of them with sound in them"
					", loudest %.3f, at most %.3f seconds ahead\n", vreport.calls, vreport.frames,
					vreport.frames / (double)CBY_VIDEO_RATE, vreport.heard, vreport.peak, vreport.most_ahead);
			SDL_DestroyAudioStream(it->vsound);
			it->vsound = NULL;
		}
		if (it->vtex && it->vtex_r == items[win - 1].r)
			SDL_DestroyTexture(it->vtex);
		it->vtex = NULL;
		it->vtex_r = NULL;
	}
}

/* ------------------------------------------------------------- test scripts
 *
 * CABBY_GUI_SCRIPT: `;` between commands, as for the GTK gui library. The
 * window is drawn off screen, and the clock is the script's: `wait 500`
 * moves it on, running the timers that fall due. */

static Item *nth(int kind, int n)
{
	int k = 0;
	for (int i = 0; i < nitems; i++)
		if (items[i].kind == kind && window_alive(items[i].win) && items[items[i].win - 1].open && ++k == n)
			return &items[i];
	return NULL;
}

static int number_and_text(const char *s, const char **text)
{
	int n = atoi(s);
	const char *sp = strchr(s, ' ');
	*text = sp ? sp + 1 : "";
	return n;
}

static int64_t id_of(Item *it) { return (int64_t)(it - items) + 1; }

static void type_into(Item *e, const char *text)
{
	int64_t id = id_of(e);
	if (*e->text)
		cby_gui_set_text(id, "");
	for (const char *c = text; *c;) {
		int len = 1;
		while ((c[len] & 0xC0) == 0x80)
			len++;
		char one[8];
		memcpy(one, c, (size_t)len);
		one[len] = '\0';
		items[id - 1].cursor = items[id - 1].anchor = (int)strlen(items[id - 1].text);
		insert_text(id, one);
		c += len;
	}
}

static void wait_ms(int64_t win, int ms)
{
	Uint64 until = fake_now + (Uint64)(ms < 0 ? 0 : ms);
	for (;;) {
		Uint64 next = run_timers(fake_now);
		if (!next || next > until || items[win - 1].closing)
			break;
		fake_now = next;
	}
	fake_now = until;
	run_timers(fake_now);
	videos_tick(win);
	find_events();
	if (ev_step)
		ev_step(0);
	a11y_tick();
}

static void run_script_steps(int64_t win);

/* the script's commands, until it ends or the window closes; at its end,
 * the copy of it is freed */
static void run_script(int64_t win)
{
	run_script_steps(win);
	if (!script_left || !*script_left) {
		free(script_all);
		script_all = script_left = NULL;
	}
}

static void run_script_steps(int64_t win)
{
	while (script_left && *script_left && !items[win - 1].closing && !items[win - 1].destroyed) {
		char *cmd = script_left, *semi = strchr(cmd, ';');
		if (semi) {
			*semi = '\0';
			script_left = semi + 1;
		} else {
			script_left = NULL;
		}
		while (*cmd == ' ')
			cmd++;
		char *end = cmd + strlen(cmd);
		while (end > cmd && end[-1] == ' ')
			*--end = '\0';
		draw_window(win);   /* (laid out, as it would be on screen) */
		const char *text;
		if (strncmp(cmd, "click ", 6) == 0) {
			int found = 0;
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_BUTTON && window_alive(items[i].win) && items[items[i].win - 1].open &&
				    strcmp(items[i].text, cmd + 6) == 0) {
					found = 1;
					if (usable(i + 1))
						call(items[i].on_click, items[i].win);
				}
			if (!found)
				fprintf(stderr, "gui script: no button '%s'\n", cmd + 6);
		} else if (strncmp(cmd, "check ", 6) == 0) {
			int found = 0;
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_CHECK && window_alive(items[i].win) && items[items[i].win - 1].open &&
				    strcmp(items[i].text, cmd + 6) == 0) {
					found = 1;
					cby_gui_check_set(i + 1, !items[i].checked);
				}
			if (!found)
				fprintf(stderr, "gui script: no check box '%s'\n", cmd + 6);
		} else if (strncmp(cmd, "type ", 5) == 0) {
			Item *e = nth(K_ENTRY, number_and_text(cmd + 5, &text));
			if (e)
				type_into(e, text);
			else
				fprintf(stderr, "gui script: no text box %s\n", cmd + 5);
		} else if (strncmp(cmd, "enter ", 6) == 0) {
			Item *e = nth(K_ENTRY, atoi(cmd + 6));
			if (e)
				call(e->on_enter, e->win);
		} else if (strncmp(cmd, "write ", 6) == 0) {
			Item *a = nth(K_TEXT_AREA, number_and_text(cmd + 6, &text));
			if (a) {
				char *t = strdup(text), *o = t;
				for (const char *c = text; *c; c++) {
					if (c[0] == '\\' && c[1] == 'n') {
						*o++ = '\n';
						c++;
					} else {
						*o++ = *c;
					}
				}
				*o = '\0';
				cby_gui_set_text(id_of(a), t);
				free(t);
			} else {
				fprintf(stderr, "gui script: no text area %s\n", cmd + 6);
			}
		} else if (strncmp(cmd, "choose ", 7) == 0 || strncmp(cmd, "select ", 7) == 0) {
			int kind = cmd[1] == 'h' ? K_CHOICE : K_LIST;
			Item *it = nth(kind, number_and_text(cmd + 7, &text));
			int i = -1;
			for (int k = 0; it && k < it->nopts; k++)
				if (strcmp(it->opts[k], text) == 0) {
					i = k;
					break;
				}
			if (i >= 0)
				cby_gui_select(id_of(it), i);
			else
				fprintf(stderr, "gui script: no %s entry '%s'\n", kind == K_CHOICE ? "drop-down" : "list", text);
		} else if (strncmp(cmd, "slide ", 6) == 0) {
			Item *s = nth(K_SLIDER, number_and_text(cmd + 6, &text));
			if (s)
				set_slider(s, atoi(text));
		} else if (strncmp(cmd, "press ", 6) == 0 || strncmp(cmd, "point ", 6) == 0) {
			int n = 0;
			double x = 0, y = 0;
			Item *c = sscanf(cmd + 6, "%d %lf %lf", &n, &x, &y) == 3 ? nth(K_CANVAS, n) : NULL;
			if (c)
				canvas_event(id_of(c), cmd[1] == 'r' ? 0 : 1, x, y);
			else
				fprintf(stderr, "gui script: no canvas for '%s'\n", cmd);
		} else if (strncmp(cmd, "key ", 4) == 0) {
			int found = 0;
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_CANVAS && window_alive(items[i].win) && items[i].keyed.fn) {
					canvas_key(i + 1, strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
					found = 1;
				}
			if (!found && window_alive(win) && items[win - 1].keyed.fn) {
				/* the window's own keys */
				snprintf(items[win - 1].key, sizeof items[win - 1].key, "%s", strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
				call(items[win - 1].keyed, win);
				found = 1;
			}
			if (!found)
				fprintf(stderr, "gui script: nothing takes keys\n");
		} else if (strncmp(cmd, "wait ", 5) == 0) {
			wait_ms(win, atoi(cmd + 5));
		} else if (strcmp(cmd, "processes") == 0) {
			settle_processes();
		} else if (strncmp(cmd, "pause ", 6) == 0) {
			/* pause <ms>: really waits that long, answering what's asked
			 * meanwhile (a screen reader's questions, say) */
			Uint64 until = SDL_GetTicks() + (Uint64)atoi(cmd + 6);
			find_events();
			while (SDL_GetTicks() < until) {
				if (ev_step)
					ev_step(10);
				else
					SDL_Delay(10);
				a11y_tick();
			}
		} else if (strncmp(cmd, "answer ", 7) == 0 || strncmp(cmd, "pick ", 5) == 0 || strcmp(cmd, "cancel") == 0) {
			int kind = pending.kind;
			Handler h = pending.h;
			int64_t pw = pending.win;
			pending.kind = D_NONE;
			if (kind == D_NONE) {
				fprintf(stderr, "gui script: '%s' but no dialog is open\n", cmd);
			} else if (cmd[0] == 'a') {
				if (kind == D_ASK && strcmp(cmd + 7, "yes") == 0)
					call(h, pw);
			} else if (cmd[0] == 'p' && kind != D_ASK) {
				set_chosen(pw, cmd + 5);
				call(h, pw);
			}
		} else if (strncmp(cmd, "pixel ", 6) == 0) {
			/* pixel <x> <y>: the color drawn there, as #rrggbb */
			int px = 0, py = 0;
			sscanf(cmd + 6, "%d %d", &px, &py);
			draw_window(win);
			Item *w = &items[win - 1];
			SDL_Rect r = { (int)(px * w->density), (int)(py * w->density), 1, 1 };
			SDL_Surface *got = w->r ? SDL_RenderReadPixels(w->r, &r) : NULL;
			if (got) {
				Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
				SDL_ReadSurfacePixel(got, 0, 0, &cr, &cg, &cb, &ca);
				printf("pixel %d %d: #%02x%02x%02x\n", px, py, cr, cg, cb);
				SDL_DestroySurface(got);
			} else {
				fprintf(stderr, "gui script: could not read the pixel at %d %d\n", px, py);
			}
		} else if (strncmp(cmd, "shot ", 5) == 0) {
			if (!screenshot(win, cmd + 5))
				fprintf(stderr, "gui script: could not save %s\n", cmd + 5);
		} else if (strncmp(cmd, "mouse ", 6) == 0) {
			/* mouse <x> <y>: a click at that place in the window, as a person
			 * would make it (down, then up) */
			double x = 0, y = 0;
			sscanf(cmd + 6, "%lf %lf", &x, &y);
			mouse_move(win, x, y);
			mouse_down(win, x, y);
			mouse_up(win, x, y);
		} else if (strncmp(cmd, "double ", 7) == 0 || strncmp(cmd, "triple ", 7) == 0) {
			/* double <x> <y>, triple <x> <y>: a double (or triple) click there */
			double x = 0, y = 0;
			sscanf(cmd + 7, "%lf %lf", &x, &y);
			mouse_move(win, x, y);
			clicks = cmd[0] == 'd' ? 2 : 3;
			mouse_down(win, x, y);
			clicks = 1;
			mouse_up(win, x, y);
		} else if (strncmp(cmd, "compose", 7) == 0 && (cmd[7] == ' ' || !cmd[7])) {
			/* compose <text>: an input method composing (shown, not yet
			 * typed); compose alone: it stops, as when it's let go */
			if (cmd[7])
				composing_now(win, cmd + 8, -1);
			else
				composing_done(win);
		} else if (strncmp(cmd, "drag ", 5) == 0) {
			/* drag <x1> <y1> <x2> <y2> */
			double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
			sscanf(cmd + 5, "%lf %lf %lf %lf", &x1, &y1, &x2, &y2);
			mouse_down(win, x1, y1);
			draw_window(win);
			mouse_move(win, x2, y2);
			mouse_up(win, x2, y2);
		} else if (strncmp(cmd, "press-key ", 10) == 0) {
			/* press-key <name>: a key pressed in the window, as SDL names it
			 * ("Return", "Tab", "Left", "Ctrl+A" ...) */
			const char *k = cmd + 10;
			SDL_Keymod mods = 0;
			for (;;) {
				if (strncmp(k, "Ctrl+", 5) == 0) { mods |= SDL_KMOD_LCTRL; k += 5; }
				else if (strncmp(k, "Shift+", 6) == 0) { mods |= SDL_KMOD_LSHIFT; k += 6; }
				else if (strncmp(k, "Alt+", 4) == 0) { mods |= SDL_KMOD_LALT; k += 4; }
				else break;
			}
			SDL_Keycode code = SDL_GetKeyFromName(k);
			if (code == SDLK_UNKNOWN)
				fprintf(stderr, "gui script: no key '%s'\n", k);
			else
				key_down(win, code, mods);
		} else if (strncmp(cmd, "text ", 5) == 0) {
			/* text <words>: typed into what has the keys */
			text_typed(win, cmd + 5);
		} else if (strcmp(cmd, "close") == 0) {
			items[win - 1].closing = 1;
			break;
		} else if (strncmp(cmd, "menu ", 5) == 0) {
			/* menu <Menu> > <Item>: chooses that item */
			const char *sep = strstr(cmd + 5, " > ");
			int found = 0;
			for (int i = 0; sep && i < nitems && !found; i++) {
				Item *it = &items[i];
				if (it->kind != K_MENU_ITEM || it->separator || !window_alive(it->win))
					continue;
				Item *m = &items[it->in_menu - 1];
				size_t tl = (size_t)(sep - (cmd + 5));
				if (strlen(m->text) == tl && !strncmp(m->text, cmd + 5, tl) && !strcmp(it->text, sep + 3)) {
					found = 1;
					if (it->disabled)
						fprintf(stderr, "gui script: '%s' is greyed out\n", sep + 3);
					else
						call(it->on_click, it->win);
				}
			}
			if (!found)
				fprintf(stderr, "gui script: no menu item '%s'\n", cmd + 5);
		} else if (strncmp(cmd, "shortcut ", 9) == 0) {
			char norm[48];
			if (!parse_keys(cmd + 9, norm, sizeof norm) || !run_shortcut(win, norm))
				fprintf(stderr, "gui script: no shortcut '%s'\n", cmd + 9);
		} else if (strncmp(cmd, "tab ", 4) == 0) {
			Item *t = nth(K_TABS, number_and_text(cmd + 4, &text));
			int i = -1;
			for (int k = 0; t && k < t->nopts; k++)
				if (!strcmp(t->opts[k], text))
					i = k;
			if (i >= 0)
				show_tab(id_of(t), i);
			else
				fprintf(stderr, "gui script: no tab '%s'\n", text);
		} else if (strncmp(cmd, "row ", 4) == 0) {
			Item *t = nth(K_TABLE, number_and_text(cmd + 4, &text));
			int found = -1;
			for (int r = 0; t && r < t->nrows && found < 0; r++)
				if (!strcmp(cell(t, r, 0), text))
					found = r;
			if (found >= 0)
				select_row(id_of(t), found);
			else
				fprintf(stderr, "gui script: no row '%s'\n", text);
		} else if (strncmp(cmd, "sort ", 5) == 0) {
			Item *t = nth(K_TABLE, number_and_text(cmd + 5, &text));
			int i = -1;
			for (int k = 0; t && k < t->nopts; k++)
				if (!strcmp(t->opts[k], text))
					i = k;
			if (i >= 0)
				sort_table(id_of(t), i, 0);
			else
				fprintf(stderr, "gui script: no heading '%s'\n", text);
		} else if (strncmp(cmd, "number ", 7) == 0) {
			Item *n = nth(K_NUMBER, number_and_text(cmd + 7, &text));
			if (n)
				set_number(n, atol(text));
			else
				fprintf(stderr, "gui script: no number box %s\n", cmd + 7);
		} else if (strncmp(cmd, "color ", 6) == 0 || strncmp(cmd, "font ", 5) == 0) {
			int is_color = cmd[0] == 'c';
			Item *b = nth(is_color ? K_COLOR : K_FONT, number_and_text(cmd + (is_color ? 6 : 5), &text));
			if (b && is_color)
				cby_gui_color_set(id_of(b), text);
			else if (b)
				cby_gui_font_set(id_of(b), text);
			else
				fprintf(stderr, "gui script: no %s button for '%s'\n", is_color ? "color" : "font", cmd);
		} else if (*cmd) {
			fprintf(stderr, "gui script: unknown command '%s'\n", cmd);
		}
		if (win <= nitems && items[win - 1].open)
			draw_window(win);
	}
}

#include "accessibility.h"
