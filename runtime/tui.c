/* tui.c — the C side of Cabby's `tui` library: windows drawn in a terminal.
 *
 * lib/tui.hby wraps these functions in objects (tui.window, tui.button,
 * ...) with the same names and methods as the gui library, so a program can
 * move between the two by changing `gui` to `tui`. Everything is handed to
 * Cabby as a small integer id, an index into the table below.
 *
 * Drawing: the window fills the terminal (or, after set_size, a box of
 * about that many pixels at 8 by 16 per character, centered). Things stack
 * top to bottom; a row puts them side by side. The whole screen is drawn
 * into a buffer of cells and written in one go after each change.
 *
 * Keys: Tab / Shift-Tab (and Up / Down outside lists and text areas) move
 * between things; Enter or Space presses a button or ticks a check box;
 * Left / Right change a drop-down or slider and move in text; Esc or
 * Ctrl-Q closes the window. A mouse click focuses and presses.
 *
 * What the program prints while a window is open would land on the
 * screen, so it is kept and written out when the last window closes.
 *
 * Handlers are called through cby_invoke_hook, as in gui.c: compiled
 * programs call the method directly; the interpreter points the hook at a
 * function that interprets it.
 *
 * Testing: if CABBY_TUI_SCRIPT is set, windows follow it instead of the
 * keyboard (see run_script), in a 60 by 20 screen (or CABBY_TUI_SIZE,
 * "WxH"), drawing nothing unless the script says `screen`.
 */
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------- handlers */

static void call_directly(void *obj, void *fn) { ((void (*)(void *))fn)(obj); }

/* The interpreter replaces this when it loads the library. (Weak, so a
 * program can link both this and the gui library.) */
__attribute__((weak)) void (*cby_invoke_hook)(void *obj, void *fn) = call_directly;

typedef struct Handler {
	void *obj;
	void *fn;
	int64_t win;   /* the window it belongs to */
} Handler;

static bool window_alive(int64_t win);

/* Cabby's runtime, when the program is compiled (the interpreter's hook
 * does this itself): what handlers printed into files is written out, since
 * the window then waits */
extern void rt_flush_files(void) __attribute__((weak));

static void invoke(Handler h)
{
	if (h.fn && (h.win == 0 || window_alive(h.win)))
		cby_invoke_hook(h.obj, h.fn);
	if (rt_flush_files)
		rt_flush_files();
}

/* ------------------------------------------------------------- the table */

enum { K_FREE, K_WINDOW, K_BOX, K_LABEL, K_BUTTON, K_ENTRY, K_CHECK, K_CHOICE, K_TEXT_AREA, K_LIST, K_SLIDER,
       K_PROGRESS, K_SEPARATOR, K_IMAGE, K_CANVAS, K_MENU, K_MENU_ITEM, K_SHORTCUT, K_TABS, K_TABLE,
       K_NUMBER, K_COLOR, K_FONT };

typedef struct Item {
	int kind;
	int64_t win;          /* the window it is in */
	int64_t parent;       /* the box it is in (0 for a window) */
	bool row;             /* a box: side by side */
	int64_t *kids;
	int nkids, capkids;
	char *text;           /* what it shows, or what is typed in it; an image's path */
	char **opts;          /* a drop-down's options, a list's items */
	int nopts, capopts;
	int selected;         /* -1: none */
	int value, min, max;  /* check box (0/1), slider */
	double fraction;      /* progress */
	bool enabled, visible, editable;
	int min_w, min_h;     /* in cells */
	int cur;              /* the cursor, a byte in text */
	int scroll;           /* the first line shown (list, text area) or column (entry) */
	Handler on_click, on_change, on_enter, on_toggle, on_select;
	int x, y, w, h;       /* where layout put it */
	bool has_ink, has_paper;   /* colors of the program's own (set_text_color, set_background) */
	uint32_t ink, paper_rgb;
	/* a window */
	char *title;
	int want_w, want_h;   /* set_size, in cells (0: the whole screen) */
	bool open;            /* shown, and not yet closed */
	int64_t box;          /* what it holds */
	int64_t focus;        /* the thing keys go to */
	char *chosen;         /* the file the last file dialog chose */
	/* a canvas (its key, as text, in `text`) */
	int cw, ch;           /* its size in pixels, as added */
	uint32_t *pix;        /* what on_draw drew: cw * ch pixels, 0xRRGGBB */
	uint32_t paper;       /* the color it was filled with (white to begin with) */
	bool dirty;           /* on_draw has to draw it again */
	struct Writing *writing;   /* text drawn on it, shown as characters */
	int nwriting;
	Handler on_draw, on_move, on_key;   /* (and on_click); a window's on_key too, its key in `text` */
	int mouse_x, mouse_y;
	/* a window's menus (and its shortcuts), a menu's items; a menu item or
	 * shortcut's keys ("ctrl+s"), and as they were written, to show */
	int64_t *menus;
	int nmenus;
	char *keys, *keys_shown;
	bool separator;
	int bar_x;            /* a menu: where its title is on the bar */
	bool scrolls;         /* a column that scrolls: `scroll` rows are above what's shown */
	int content_h;        /* a column that scrolls: how tall what's in it is */
	/* a table: its headings (in opts), its rows as added, the order they
	 * show in, and what they're sorted by (-1: nothing) */
	struct TRow *trows;
	int ntrows, captrows;
	int *order;
	int sort_col;
	bool sort_down;
	char *cache;          /* the last cell handed out */
} Item;

typedef struct TRow {
	char **cell;
	int n;
} TRow;

static Item *items;
static int nitems, capitems;

static Item *item(int64_t id)
{
	if (id < 1 || id > nitems || items[id - 1].kind == K_FREE) {
		fprintf(stderr, "tui: bad id %lld\n", (long long)id);
		exit(101);
	}
	return &items[id - 1];
}

static bool window_alive(int64_t win)
{
	return win >= 1 && win <= nitems && items[win - 1].kind == K_WINDOW;
}

static char *dupstr(const char *s)
{
	size_t n = strlen(s ? s : "");
	char *d = malloc(n + 1);
	memcpy(d, s ? s : "", n + 1);
	return d;
}

static int64_t new_item(int kind, int64_t win)
{
	if (nitems == capitems) {
		capitems = capitems ? capitems * 2 : 64;
		items = realloc(items, sizeof *items * (size_t)capitems);
	}
	Item *it = &items[nitems++];
	memset(it, 0, sizeof *it);
	it->kind = kind;
	it->win = win;
	it->selected = -1;
	it->enabled = it->visible = it->editable = true;
	it->text = dupstr("");
	return nitems;
}

/* the program's own colors, for what's being drawn now (from the thing
 * being drawn, or the row, column or window it's in) */
static bool ink_on, paper_on;
static uint32_t ink_now, paper_now;

static void add_kid(int64_t box, int64_t kid)
{
	Item *b = item(box);
	if (b->nkids == b->capkids) {
		b->capkids = b->capkids ? b->capkids * 2 : 8;
		b->kids = realloc(b->kids, sizeof *b->kids * (size_t)b->capkids);
	}
	b->kids[b->nkids++] = kid;
	item(kid)->parent = box;
}

static int64_t add_to(int64_t box, int kind, const char *text)
{
	int64_t win = item(box)->win;
	int64_t id = new_item(kind, win);
	free(item(id)->text);
	item(id)->text = dupstr(text);
	add_kid(box, id);
	return id;
}

static void add_opt(Item *it, const char *s)
{
	if (it->nopts == it->capopts) {
		it->capopts = it->capopts ? it->capopts * 2 : 8;
		it->opts = realloc(it->opts, sizeof *it->opts * (size_t)it->capopts);
	}
	it->opts[it->nopts++] = dupstr(s);
}

/* ------------------------------------------------------------- text */

/* How many columns UTF-8 text takes (one per character) */
static int text_cols(const char *s, int n)
{
	int c = 0;
	for (int i = 0; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80)
			c++;
	return c;
}

static int next_char(const char *s, int i)
{
	int n = (int)strlen(s);
	if (i >= n)
		return n;
	i++;
	while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80)
		i++;
	return i;
}

static int prev_char(const char *s, int i)
{
	if (i <= 0)
		return 0;
	i--;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
		i--;
	return i;
}

static void text_insert(Item *it, const char *s, int n)
{
	int len = (int)strlen(it->text);
	it->text = realloc(it->text, (size_t)(len + n + 1));
	memmove(it->text + it->cur + n, it->text + it->cur, (size_t)(len - it->cur + 1));
	memcpy(it->text + it->cur, s, (size_t)n);
	it->cur += n;
}

static void text_delete(Item *it, int from, int to)
{
	int len = (int)strlen(it->text);
	if (from < 0 || to > len || from >= to)
		return;
	memmove(it->text + from, it->text + to, (size_t)(len - to + 1));
	if (it->cur > from)
		it->cur = it->cur >= to ? it->cur - (to - from) : from;
}

/* Lines of a text: line k starts at the k-th byte after a '\n' */
static int line_count(const char *s)
{
	int n = 1;
	for (; *s; s++)
		n += *s == '\n';
	return n;
}

static int line_start(const char *s, int k)
{
	int i = 0;
	while (k > 0 && s[i]) {
		if (s[i] == '\n')
			k--;
		i++;
	}
	return i;
}

static int line_len(const char *s, int start)
{
	int i = start;
	while (s[i] && s[i] != '\n')
		i++;
	return i - start;
}

/* A text area's rows on the screen: its lines, wrapped at spaces to fit
 * `width` columns (a word longer than that is split). rows[2k] is where row
 * k starts and rows[2k+1] how many bytes it has; the caller frees it. */
static int wrap_rows(const char *s, int width, int **rows_out)
{
	if (width < 2)
		width = 2;
	int cap = 16, n = 0;
	int *rows = malloc(sizeof(int) * 2 * (size_t)cap);
	int i = 0;
	for (;;) {
		int end = i;                 /* the end of this line */
		while (s[end] && s[end] != '\n')
			end++;
		int st = i;
		do {
			/* as many characters as fit, then back to just after a space */
			int j = st, cols = 0, brk = -1;
			while (j < end && cols < width) {
				if (s[j] == ' ')
					brk = j + 1;
				j = next_char(s, j);
				cols++;
			}
			if (j < end && s[j] == ' ')
				brk = j + 1;      /* the space just after a full row stays on it */
			if (j < end && brk > st)
				j = brk;
			if (n == cap) {
				cap *= 2;
				rows = realloc(rows, sizeof(int) * 2 * (size_t)cap);
			}
			rows[2 * n] = st;
			rows[2 * n + 1] = j - st;
			n++;
			st = j;
		} while (st < end);
		if (!s[end])
			break;
		i = end + 1;
	}
	*rows_out = rows;
	return n;
}

/* the position `col` columns into row r (not past its end; on a wrapped
 * row, not onto the next row's first character) */
static int col_in_row(const char *s, const int *rows, int n, int r, int col)
{
	int st = rows[2 * r], e = st + rows[2 * r + 1], i = st;
	if (r + 1 < n && rows[2 * (r + 1)] == e && e > st)
		e = prev_char(s, e);
	for (int c = 0; c < col && i < e; c++)
		i = next_char(s, i);
	return i;
}

/* the row the position `pos` is shown on */
static int row_of(const int *rows, int n, int pos)
{
	int r = 0;
	while (r + 1 < n && rows[2 * (r + 1)] <= pos)
		r++;
	return r;
}

/* ------------------------------------------------------------- the screen */

enum { S_NORMAL, S_BORDER, S_TITLE, S_FOCUS, S_FIELD, S_DIM, S_SELECTED, S_FILL, S_BUTTON, S_DIALOG };

static const char *const style_codes[] = {
	[S_NORMAL] = "\x1b[0m", [S_BORDER] = "\x1b[0;36m", [S_TITLE] = "\x1b[0;1;36m", [S_FOCUS] = "\x1b[0;30;46m",
	[S_FIELD] = "\x1b[0;39;100m", [S_DIM] = "\x1b[0;2m", [S_SELECTED] = "\x1b[0;7m", [S_FILL] = "\x1b[0;32m",
	[S_BUTTON] = "\x1b[0;1m", [S_DIALOG] = "\x1b[0;1;33m",
};

typedef struct Cell {
	char ch[5];   /* one UTF-8 character */
	unsigned char style;
	bool rgb;     /* in these colors instead of the style (a canvas) */
	bool own_fg, own_bg;   /* the style, but with the program's own colors */
	uint32_t fg, bg;
} Cell;

static Cell *screen;
static int scr_w, scr_h;
static int cursor_x = -1, cursor_y = -1;   /* where the text cursor shows (-1: hidden) */

static bool script_mode;
static char *script_left;
static char *script_text;        /* the whole script, freed at exit */
static int out_fd = 1;           /* the terminal, while stdout is kept aside */

static void screen_size(int *w, int *h)
{
	if (script_mode) {
		const char *s = getenv("CABBY_TUI_SIZE");
		*w = 60;
		*h = 20;
		if (s)
			sscanf(s, "%dx%d", w, h);
		return;
	}
	struct winsize ws;
	if (ioctl(out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
		*w = ws.ws_col;
		*h = ws.ws_row;
	} else {
		*w = 80;
		*h = 24;
	}
}

static void clear_screen(void)
{
	int w, h;
	screen_size(&w, &h);
	if (w != scr_w || h != scr_h) {
		free(screen);
		scr_w = w;
		scr_h = h;
		screen = malloc(sizeof *screen * (size_t)(w * h));
	}
	for (int i = 0; i < scr_w * scr_h; i++) {
		strcpy(screen[i].ch, " ");
		screen[i].style = S_NORMAL;
		screen[i].rgb = false;
		screen[i].own_fg = screen[i].own_bg = false;
	}
	cursor_x = cursor_y = -1;
}

/* What may be drawn on: the screen, or the part of a scrolling column
 * that shows */
static int clip_x0, clip_y0, clip_x1 = INT_MAX, clip_y1 = INT_MAX;

static bool clipped(int x, int y) { return x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1; }

static void put(int x, int y, const char *ch, int n, int style)
{
	if (x < 0 || y < 0 || x >= scr_w || y >= scr_h || n <= 0 || n > 4 || clipped(x, y))
		return;
	Cell *c = &screen[y * scr_w + x];
	memcpy(c->ch, ch, (size_t)n);
	c->ch[n] = '\0';
	c->style = (unsigned char)style;
	c->rgb = false;
	/* (what has the keys, what's chosen and what's grayed out keep their
	 * look, so they still show) */
	bool own = style != S_FOCUS && style != S_SELECTED && style != S_DIM;
	c->own_fg = own && ink_on;
	c->own_bg = own && paper_on;
	c->fg = ink_now;
	c->bg = paper_now;
}

/* A character in colors of its own (a canvas's) */
static void put_rgb(int x, int y, const char *ch, int n, uint32_t fg, uint32_t bg)
{
	if (x < 0 || y < 0 || x >= scr_w || y >= scr_h || n <= 0 || n > 4 || clipped(x, y))
		return;
	Cell *c = &screen[y * scr_w + x];
	memcpy(c->ch, ch, (size_t)n);
	c->ch[n] = '\0';
	c->style = S_NORMAL;
	c->rgb = true;
	c->own_fg = c->own_bg = false;
	c->fg = fg;
	c->bg = bg;
}

/* Text from x, at most `max` columns; gives back how many it took */
static int put_text(int x, int y, const char *s, int n, int max, int style)
{
	int col = 0;
	for (int i = 0; i < n && col < max;) {
		int k = next_char(s, i);
		if (k > n)
			k = n;
		put(x + col, y, s + i, k - i, style);
		col++;
		i = k;
	}
	return col;
}

static void fill(int x, int y, int w, int style)
{
	for (int i = 0; i < w; i++)
		put(x + i, y, " ", 1, style);
}

static void frame(int x, int y, int w, int h, const char *title, int style)
{
	if (w < 2 || h < 2)
		return;
	put(x, y, "┌", 3, style);
	put(x + w - 1, y, "┐", 3, style);
	put(x, y + h - 1, "└", 3, style);
	put(x + w - 1, y + h - 1, "┘", 3, style);
	for (int i = 1; i < w - 1; i++) {
		put(x + i, y, "─", 3, style);
		put(x + i, y + h - 1, "─", 3, style);
	}
	for (int j = 1; j < h - 1; j++) {
		put(x, y + j, "│", 3, style);
		put(x + w - 1, y + j, "│", 3, style);
	}
	if (title && *title) {
		int tn = (int)strlen(title), tc = text_cols(title, tn);
		if (tc > w - 6)
			tc = w - 6;
		if (tc > 0) {
			int tx = x + (w - tc - 2) / 2;
			put(tx, y, " ", 1, style);
			put_text(tx + 1, y, title, tn, tc, S_TITLE);
			put(tx + 1 + tc, y, " ", 1, style);
		}
	}
}

/* The screen to the terminal in one write (script mode: nothing) */
static void flush_screen(void)
{
	if (script_mode)
		return;
	size_t cap = (size_t)(scr_w * scr_h) * 8 + 64, n = 0;
	char *out = malloc(cap);
	n += (size_t)sprintf(out + n, "\x1b[?25l\x1b[H");
	int style = -1;
	for (int y = 0; y < scr_h; y++) {
		n += (size_t)sprintf(out + n, "\x1b[%d;1H", y + 1);
		for (int x = 0; x < scr_w; x++) {
			Cell *c = &screen[y * scr_w + x];
			if (c->rgb) {
				n += (size_t)sprintf(out + n, "\x1b[0;38;2;%u;%u;%u;48;2;%u;%u;%um", c->fg >> 16, (c->fg >> 8) & 255,
						     c->fg & 255, c->bg >> 16, (c->bg >> 8) & 255, c->bg & 255);
				style = -1;
			} else if (c->own_fg || c->own_bg) {
				size_t k = strlen(style_codes[c->style]);
				memcpy(out + n, style_codes[c->style], k);
				n += k;
				if (c->own_fg)
					n += (size_t)sprintf(out + n, "\x1b[38;2;%u;%u;%um", c->fg >> 16, (c->fg >> 8) & 255, c->fg & 255);
				if (c->own_bg)
					n += (size_t)sprintf(out + n, "\x1b[48;2;%u;%u;%um", c->bg >> 16, (c->bg >> 8) & 255, c->bg & 255);
				style = -1;
			} else if (c->style != style) {
				style = c->style;
				size_t k = strlen(style_codes[style]);
				memcpy(out + n, style_codes[style], k);
				n += k;
			}
			size_t k = strlen(c->ch);
			memcpy(out + n, c->ch, k);
			n += k;
			if (n + 64 > cap) {
				cap *= 2;
				out = realloc(out, cap);
			}
		}
	}
	n += (size_t)sprintf(out + n, "\x1b[0m");
	if (cursor_x >= 0)
		n += (size_t)sprintf(out + n, "\x1b[%d;%dH\x1b[?25h", cursor_y + 1, cursor_x + 1);
	for (size_t done = 0; done < n;) {
		ssize_t k = write(out_fd, out + done, n - done);
		if (k <= 0) {
			if (k < 0 && errno == EINTR)
				continue;
			break;
		}
		done += (size_t)k;
	}
	free(out);
}

static bool blank_row(int y)
{
	for (int x = 0; x < scr_w; x++)
		if (strcmp(screen[y * scr_w + x].ch, " ") != 0)
			return false;
	return true;
}

/* The screen as plain text, for a script's `screen` (without the blank
 * rows above and below a window smaller than the screen) */
static void print_screen(void)
{
	int top = 0, bottom = scr_h;
	while (top < bottom && blank_row(top))
		top++;
	while (bottom > top && blank_row(bottom - 1))
		bottom--;
	int left = scr_w;   /* and the blank columns on the left */
	for (int y = top; y < bottom; y++) {
		int x = 0;
		while (x < left && strcmp(screen[y * scr_w + x].ch, " ") == 0)
			x++;
		if (x < left)
			left = x;
	}
	for (int y = top; y < bottom; y++) {
		char line[4096];
		int n = 0;
		for (int x = left; x < scr_w && n < 4000; x++) {
			const char *c = screen[y * scr_w + x].ch;
			size_t k = strlen(c);
			memcpy(line + n, c, k);
			n += (int)k;
		}
		while (n > 0 && line[n - 1] == ' ')
			n--;
		line[n] = '\0';
		printf("%s\n", line);
	}
	fflush(stdout);
}

/* ------------------------------------------------------------- layout */

static bool focusable(const Item *it)
{
	switch (it->kind) {
	case K_BUTTON: case K_ENTRY: case K_CHECK: case K_CHOICE: case K_TEXT_AREA: case K_LIST: case K_SLIDER:
	case K_TABLE: case K_NUMBER: case K_COLOR: case K_FONT:
		return it->enabled;
	case K_CANVAS:
		return it->enabled && it->on_key.fn;   /* keys can go to it */
	case K_TABS:
		return it->enabled && it->nkids > 0;   /* Left and Right change the tab */
	default:
		return false;
	}
}

static int max_opt_cols(const Item *it)
{
	int m = 0;
	for (int i = 0; i < it->nopts; i++) {
		int c = text_cols(it->opts[i], (int)strlen(it->opts[i]));
		if (c > m)
			m = c;
	}
	return m;
}

static int label_width(const char *s)
{
	int m = 0;
	for (int k = 0, n = line_count(s); k < n; k++) {
		int st = line_start(s, k);
		int c = text_cols(s + st, line_len(s, st));
		if (c > m)
			m = c;
	}
	return m;
}

static void table_widths(Item *t, int w, int *cw);

/* The size a thing would like to be */
static void natural(int64_t id, int *w, int *h)
{
	Item *it = item(id);
	*w = *h = 0;
	if (!it->visible)
		return;
	int tc = text_cols(it->text, (int)strlen(it->text));
	switch (it->kind) {
	case K_TABS: {
		for (int i = 0; i < it->nopts; i++)
			*w += text_cols(it->opts[i], (int)strlen(it->opts[i])) + 3;
		int ph = 0;
		for (int i = 0; i < it->nkids; i++) {
			int cw, ch;
			natural(it->kids[i], &cw, &ch);
			if (cw > *w)
				*w = cw;
			if (ch > ph)
				ph = ch;
		}
		*h = 1 + ph;
		break;
	}
	case K_BOX: {
		for (int i = 0; i < it->nkids; i++) {
			int cw, ch;
			natural(it->kids[i], &cw, &ch);
			if (!item(it->kids[i])->visible)
				continue;
			if (it->row) {
				*w += cw + (*w ? 1 : 0);
				if (ch > *h)
					*h = ch;
			} else {
				*h += ch;
				if (cw > *w)
					*w = cw;
			}
		}
		if (it->scrolls) {
			/* it asks for a few rows, and scrolls through the rest */
			if (*h > 3)
				*h = 3;
			*w += 1;   /* the scroll bar */
		}
		break;
	}
	case K_LABEL:     *w = label_width(it->text); *h = line_count(it->text); break;
	case K_BUTTON:    *w = tc + 4; *h = 1; break;
	case K_ENTRY:     *w = tc + 2 > 20 ? tc + 2 : 20; *h = 1; break;
	case K_CHECK:     *w = tc + 4; *h = 1; break;
	case K_CHOICE:    *w = max_opt_cols(it) + 4 > 8 ? max_opt_cols(it) + 4 : 8; *h = 1; break;
	case K_TEXT_AREA: *w = 30; *h = 5; break;
	case K_LIST:      *w = max_opt_cols(it) + 2 > 20 ? max_opt_cols(it) + 2 : 20; *h = 5; break;
	case K_TABLE: {
		int cw[64], total = 0;
		table_widths(it, 10000, cw);
		for (int c = 0; c < it->nopts && c < 64; c++)
			total += cw[c] + 2;
		*w = total + 1 > 20 ? total + 1 : 20;
		*h = 6;
		break;
	}
	case K_SLIDER:    *w = 26; *h = 1; break;
	case K_NUMBER: {
		char a[24], b[24];
		int na = snprintf(a, sizeof a, "%d", it->min), nb = snprintf(b, sizeof b, "%d", it->max);
		*w = (na > nb ? na : nb) + 6;
		*h = 1;
		break;
	}
	case K_COLOR:    *w = 14; *h = 1; break;
	case K_FONT:      *w = tc + 4 > 14 ? tc + 4 : 14; *h = 1; break;
	case K_PROGRESS:  *w = 20; *h = 1; break;
	case K_SEPARATOR: *w = 1; *h = 1; break;
	case K_IMAGE:     *w = text_cols(it->text, (int)strlen(it->text)) + 11; *h = 1; break;
	case K_CANVAS:    *w = (it->cw + 7) / 8; *h = (it->ch + 15) / 16; break;   /* 8 by 16 pixels a character */
	}
	if (it->min_w > *w)
		*w = it->min_w;
	if (it->min_h > *h)
		*h = it->min_h;
}

static bool grows_wide(const Item *it)
{
	return it->kind == K_ENTRY || it->kind == K_TEXT_AREA || it->kind == K_LIST || it->kind == K_PROGRESS ||
	       it->kind == K_SLIDER || it->kind == K_SEPARATOR || it->kind == K_BOX || it->kind == K_TABS ||
	       it->kind == K_TABLE;
}

static bool grows_tall(int64_t id)
{
	Item *it = item(id);
	if (!it->visible)
		return false;
	if (it->kind == K_TEXT_AREA || it->kind == K_LIST || it->kind == K_TABLE || (it->kind == K_BOX && it->scrolls))
		return true;
	if (it->kind == K_BOX || it->kind == K_TABS)
		for (int i = 0; i < it->nkids; i++)
			if (grows_tall(it->kids[i]))
				return true;
	return false;
}

static void place_kids(int64_t id, int x, int y, int w, int h);

static void place(int64_t id, int x, int y, int w, int h)
{
	Item *it = item(id);
	it->x = x;
	it->y = y;
	it->w = w;
	it->h = h;
	if (it->kind == K_TABS) {
		/* the tabs along the top; every page below them, though only the
		 * chosen one shows */
		for (int i = 0; i < it->nkids; i++)
			place(it->kids[i], x, y + 1, w, h - 1);
		return;
	}
	if (it->kind != K_BOX)
		return;
	if (it->scrolls) {
		/* what's in it, at its full height, moved up by `scroll` rows; only
		 * its own w by h shows */
		bool was = it->scrolls;
		it->scrolls = false;
		int nw, nh;
		natural(id, &nw, &nh);
		it->scrolls = was;
		it->content_h = nh > h ? nh : h;
		if (it->scroll > it->content_h - h)
			it->scroll = it->content_h - h;
		if (it->scroll < 0)
			it->scroll = 0;
		place_kids(id, x, y - it->scroll, w - 1, it->content_h);
		return;
	}
	place_kids(id, x, y, w, h);
}

static void place_kids(int64_t id, int x, int y, int w, int h)
{
	Item *it = item(id);
	int n = it->nkids, total = 0, growers = 0;
	int *size = calloc((size_t)(n ? n : 1), sizeof *size);
	for (int i = 0; i < n; i++) {
		int cw, ch;
		natural(it->kids[i], &cw, &ch);
		Item *k = item(it->kids[i]);
		if (!k->visible)
			continue;
		size[i] = it->row ? cw : ch;
		total += size[i] + (it->row && total ? 1 : 0);
		if (it->row ? grows_wide(k) && k->kind != K_SEPARATOR : grows_tall(it->kids[i]))
			growers++;
	}
	int extra = (it->row ? w : h) - total;
	if (extra < 0 && !it->row) {
		/* too tall: lists and text areas give up rows first, down to 2 each */
		for (bool any = true; extra < 0 && any;) {
			any = false;
			for (int i = 0; i < n && extra < 0; i++)
				if (item(it->kids[i])->visible && grows_tall(it->kids[i]) && size[i] > 2) {
					size[i]--;
					extra++;
					any = true;
				}
		}
	}
	for (int i = 0; i < n && extra > 0 && growers > 0; i++) {
		Item *k = item(it->kids[i]);
		if (k->visible && (it->row ? grows_wide(k) && k->kind != K_SEPARATOR : grows_tall(it->kids[i]))) {
			int add = extra / growers;
			size[i] += add;
			extra -= add;
			growers--;
		}
	}
	int at = it->row ? x : y;
	for (int i = 0; i < n; i++) {
		Item *k = item(it->kids[i]);
		if (!k->visible)
			continue;
		if (it->row) {
			int cw, ch;
			natural(it->kids[i], &cw, &ch);
			place(it->kids[i], at, y, size[i], k->kind == K_BOX ? h : ch);
			at += size[i] + 1;
		} else {
			int cw, ch;
			natural(it->kids[i], &cw, &ch);
			/* in a column, things that can widen take the whole width */
			place(it->kids[i], x, at, grows_wide(k) ? w : (cw < w ? cw : w), size[i]);
			at += size[i];
		}
	}
	free(size);
}

/* The window's own box on the screen */
static void window_frame(Item *win, int *x, int *y, int *w, int *h)
{
	*w = scr_w;
	*h = scr_h;
	if (win->want_w > 0 || win->want_h > 0) {
		int nw, nh;
		natural(win->box, &nw, &nh);
		nh += win->nmenus > 0;
		int ww = win->want_w > nw + 4 ? win->want_w : nw + 4, wh = win->want_h > nh + 2 ? win->want_h : nh + 2;
		*w = ww < scr_w ? ww : scr_w;
		*h = wh < scr_h ? wh : scr_h;
	}
	*x = (scr_w - *w) / 2;
	*y = (scr_h - *h) / 2;
}

static void layout(int64_t win)
{
	Item *w = item(win);
	int x, y, ww, wh;
	window_frame(w, &x, &y, &ww, &wh);
	int bar = w->nmenus > 0;   /* a menu bar takes the first row */
	place(w->box, x + 2, y + 1 + bar, ww - 4, wh - 2 - bar);
}

/* ------------------------------------------------------------- focus */

static void collect_focusable(int64_t id, int64_t *out, int *n)
{
	Item *it = item(id);
	if (!it->visible)
		return;
	if (it->kind == K_TABS) {
		if (focusable(it) && *n < 4096)
			out[(*n)++] = id;
		if (it->selected >= 0 && it->selected < it->nkids)
			collect_focusable(it->kids[it->selected], out, n);
	} else if (it->kind == K_BOX) {
		for (int i = 0; i < it->nkids; i++)
			collect_focusable(it->kids[i], out, n);
	} else if (focusable(it) && *n < 4096) {
		out[(*n)++] = id;
	}
}

static void move_focus(int64_t win, int step)
{
	static int64_t list[4096];
	int n = 0;
	collect_focusable(item(win)->box, list, &n);
	if (n == 0) {
		item(win)->focus = 0;
		return;
	}
	int at = -1;
	for (int i = 0; i < n; i++)
		if (list[i] == item(win)->focus)
			at = i;
	if (at < 0)
		at = step > 0 ? -1 : 0;
	item(win)->focus = list[((at + step) % n + n) % n];
}

/* Keeps the focus on something that can take it */
static void check_focus(int64_t win)
{
	Item *w = item(win);
	if (w->focus) {
		Item *f = item(w->focus);
		bool shown = f->visible;
		for (int64_t c = w->focus, p = f->parent; p && shown; c = p, p = item(p)->parent) {
			shown = item(p)->visible;
			if (shown && item(p)->kind == K_TABS)   /* a page that isn't the one shown */
				shown = item(p)->selected >= 0 && item(p)->selected < item(p)->nkids &&
					item(p)->kids[item(p)->selected] == c;
		}
		if (focusable(f) && shown)
			return;
	}
	w->focus = 0;
	move_focus(win, 1);
}

/* ------------------------------------------------------------- dialogs */

enum { D_NONE, D_MESSAGE, D_ASK, D_OPEN, D_SAVE };

static struct {
	int kind;
	int64_t win;
	char *text;
	Handler h;
	int choice;           /* ask: 0 yes, 1 no */
	int64_t entry;        /* file dialogs: the name being typed */
	int64_t list;         /* file dialogs: what's in the directory */
	char *dir;            /* file dialogs: the directory shown */
	bool on_list;         /* file dialogs: the list has the keys */
} dialog;

static void dialog_fill_list(void)
{
	Item *l = item(dialog.list);
	for (int i = 0; i < l->nopts; i++)
		free(l->opts[i]);
	l->nopts = 0;
	l->selected = -1;
	l->scroll = 0;
	add_opt(l, "../");
	DIR *d = opendir(dialog.dir);
	if (!d)
		return;
	char **names = NULL;
	int n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char full[PATH_MAX];
		snprintf(full, sizeof full, "%s/%s", dialog.dir, e->d_name);
		struct stat st;
		bool isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
		if (n == cap) {
			cap = cap ? cap * 2 : 32;
			names = realloc(names, sizeof *names * (size_t)cap);
		}
		size_t k = strlen(e->d_name);
		names[n] = malloc(k + 2);
		memcpy(names[n], e->d_name, k);
		names[n][k] = isdir ? '/' : '\0';
		names[n][k + (isdir ? 1 : 0)] = '\0';
		n++;
	}
	closedir(d);
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
			char *t = names[j];
			names[j] = names[j - 1];
			names[j - 1] = t;
		}
	for (int i = 0; i < n; i++) {
		add_opt(l, names[i]);
		free(names[i]);
	}
	free(names);
}

static void dialog_start(int kind, int64_t win, const char *text, void *obj, void *fn)
{
	free(dialog.text);
	dialog.kind = kind;
	dialog.win = win;
	dialog.text = dupstr(text);
	dialog.h = (Handler){ obj, fn, win };
	dialog.choice = 0;
	dialog.on_list = false;
	if (kind == D_OPEN || kind == D_SAVE) {
		if (!dialog.entry) {
			/* the dialog's own things, outside any window's boxes */
			dialog.entry = new_item(K_ENTRY, 0);
			dialog.list = new_item(K_LIST, 0);
		}
		Item *e = item(dialog.entry);
		free(e->text);
		e->text = dupstr(kind == D_SAVE ? text : "");
		e->cur = (int)strlen(e->text);
		e->scroll = 0;
		free(dialog.dir);
		char cwd[PATH_MAX];
		dialog.dir = dupstr(getcwd(cwd, sizeof cwd) ? cwd : ".");
		dialog_fill_list();
		dialog.on_list = kind == D_OPEN;
	}
}

static void set_chosen(int64_t win, const char *path)
{
	if (!window_alive(win))
		return;
	char full[PATH_MAX * 2];
	if (path[0] == '/')
		snprintf(full, sizeof full, "%s", path);
	else
		snprintf(full, sizeof full, "%s/%s", dialog.dir ? dialog.dir : ".", path);
	free(item(win)->chosen);
	item(win)->chosen = dupstr(full);
}

/* The dialog is answered: close it, then call the handler if it says so */
static void dialog_finish(bool yes)
{
	int kind = dialog.kind;
	Handler h = dialog.h;
	dialog.kind = D_NONE;
	if (kind == D_ASK && yes)
		invoke(h);
	if ((kind == D_OPEN || kind == D_SAVE) && yes)
		invoke(h);
}

enum {
	KEY_NONE = 0, KEY_TAB = 1000, KEY_BACKTAB, KEY_ENTER, KEY_ESC, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_BACKSPACE, KEY_DELETE, KEY_QUIT, KEY_MOUSE,
	KEY_F1 = 1100,          /* to KEY_F1 + 11: F1 to F12 */
	KEY_CTRL = 1200,        /* + 'a' ... 'z': Ctrl and a letter */
	KEY_ALT = 1400          /* + a character: Alt and it (Esc, then it) */
};

/* ------------------------------------------------------------- canvases
 *
 * A canvas is drawn the way gui draws one: its on_draw handler draws it,
 * with the same calls, in pixels. Here they're drawn into a picture of
 * its own (one 0xRRGGBB number a pixel, without smoothing), which is then
 * shown in the terminal at two blocks to a character (`▀`, with the top
 * pixel's color in front and the bottom's behind), each block standing
 * for the pixels under it. Text is shown as characters on top. So a
 * canvas looks rougher than in a window, but the same program draws it. */

typedef struct Writing {
	double x, y;
	char *text;
	uint32_t color;
} Writing;

static void term_restore(void);

/* Cabby's panic (the interpreter, and compiled programs, have it) */
extern void (*rt_before_panic)(void) __attribute__((weak));
extern _Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...) __attribute__((weak));

static _Noreturn void fail(const char *fmt, const char *a)
{
	term_restore();   /* the terminal back as it was, to show the message */
	if (rt_panic)
		rt_panic(NULL, 0, 0, fmt, a);
	fprintf(stderr, "tui: ");
	fprintf(stderr, fmt, a);
	fputc('\n', stderr);
	exit(101);
}

/* The CSS color names (every name a web page or GTK knows) */
static const struct { const char *name; uint32_t rgb; } color_names[] = {
	{ "aliceblue", 0xf0f8ff }, { "antiquewhite", 0xfaebd7 }, { "aqua", 0x00ffff }, { "aquamarine", 0x7fffd4 },
	{ "azure", 0xf0ffff }, { "beige", 0xf5f5dc }, { "bisque", 0xffe4c4 }, { "black", 0x000000 },
	{ "blanchedalmond", 0xffebcd }, { "blue", 0x0000ff }, { "blueviolet", 0x8a2be2 }, { "brown", 0xa52a2a },
	{ "burlywood", 0xdeb887 }, { "cadetblue", 0x5f9ea0 }, { "chartreuse", 0x7fff00 }, { "chocolate", 0xd2691e },
	{ "coral", 0xff7f50 }, { "cornflowerblue", 0x6495ed }, { "cornsilk", 0xfff8dc }, { "crimson", 0xdc143c },
	{ "cyan", 0x00ffff }, { "darkblue", 0x00008b }, { "darkcyan", 0x008b8b }, { "darkgoldenrod", 0xb8860b },
	{ "darkgray", 0xa9a9a9 }, { "darkgreen", 0x006400 }, { "darkgrey", 0xa9a9a9 }, { "darkkhaki", 0xbdb76b },
	{ "darkmagenta", 0x8b008b }, { "darkolivegreen", 0x556b2f }, { "darkorange", 0xff8c00 },
	{ "darkorchid", 0x9932cc }, { "darkred", 0x8b0000 }, { "darksalmon", 0xe9967a }, { "darkseagreen", 0x8fbc8f },
	{ "darkslateblue", 0x483d8b }, { "darkslategray", 0x2f4f4f }, { "darkslategrey", 0x2f4f4f },
	{ "darkturquoise", 0x00ced1 }, { "darkviolet", 0x9400d3 }, { "deeppink", 0xff1493 },
	{ "deepskyblue", 0x00bfff }, { "dimgray", 0x696969 }, { "dimgrey", 0x696969 }, { "dodgerblue", 0x1e90ff },
	{ "firebrick", 0xb22222 }, { "floralwhite", 0xfffaf0 }, { "forestgreen", 0x228b22 }, { "fuchsia", 0xff00ff },
	{ "gainsboro", 0xdcdcdc }, { "ghostwhite", 0xf8f8ff }, { "gold", 0xffd700 }, { "goldenrod", 0xdaa520 },
	{ "gray", 0x808080 }, { "green", 0x008000 }, { "greenyellow", 0xadff2f }, { "grey", 0x808080 },
	{ "honeydew", 0xf0fff0 }, { "hotpink", 0xff69b4 }, { "indianred", 0xcd5c5c }, { "indigo", 0x4b0082 },
	{ "ivory", 0xfffff0 }, { "khaki", 0xf0e68c }, { "lavender", 0xe6e6fa }, { "lavenderblush", 0xfff0f5 },
	{ "lawngreen", 0x7cfc00 }, { "lemonchiffon", 0xfffacd }, { "lightblue", 0xadd8e6 }, { "lightcoral", 0xf08080 },
	{ "lightcyan", 0xe0ffff }, { "lightgoldenrodyellow", 0xfafad2 }, { "lightgray", 0xd3d3d3 },
	{ "lightgreen", 0x90ee90 }, { "lightgrey", 0xd3d3d3 }, { "lightpink", 0xffb6c1 }, { "lightsalmon", 0xffa07a },
	{ "lightseagreen", 0x20b2aa }, { "lightskyblue", 0x87cefa }, { "lightslategray", 0x778899 },
	{ "lightslategrey", 0x778899 }, { "lightsteelblue", 0xb0c4de }, { "lightyellow", 0xffffe0 },
	{ "lime", 0x00ff00 }, { "limegreen", 0x32cd32 }, { "linen", 0xfaf0e6 }, { "magenta", 0xff00ff },
	{ "maroon", 0x800000 }, { "mediumaquamarine", 0x66cdaa }, { "mediumblue", 0x0000cd },
	{ "mediumorchid", 0xba55d3 }, { "mediumpurple", 0x9370db }, { "mediumseagreen", 0x3cb371 },
	{ "mediumslateblue", 0x7b68ee }, { "mediumspringgreen", 0x00fa9a }, { "mediumturquoise", 0x48d1cc },
	{ "mediumvioletred", 0xc71585 }, { "midnightblue", 0x191970 }, { "mintcream", 0xf5fffa },
	{ "mistyrose", 0xffe4e1 }, { "moccasin", 0xffe4b5 }, { "navajowhite", 0xffdead }, { "navy", 0x000080 },
	{ "oldlace", 0xfdf5e6 }, { "olive", 0x808000 }, { "olivedrab", 0x6b8e23 }, { "orange", 0xffa500 },
	{ "orangered", 0xff4500 }, { "orchid", 0xda70d6 }, { "palegoldenrod", 0xeee8aa }, { "palegreen", 0x98fb98 },
	{ "paleturquoise", 0xafeeee }, { "palevioletred", 0xdb7093 }, { "papayawhip", 0xffefd5 },
	{ "peachpuff", 0xffdab9 }, { "peru", 0xcd853f }, { "pink", 0xffc0cb }, { "plum", 0xdda0dd },
	{ "powderblue", 0xb0e0e6 }, { "purple", 0x800080 }, { "rebeccapurple", 0x663399 }, { "red", 0xff0000 },
	{ "rosybrown", 0xbc8f8f }, { "royalblue", 0x4169e1 }, { "saddlebrown", 0x8b4513 }, { "salmon", 0xfa8072 },
	{ "sandybrown", 0xf4a460 }, { "seagreen", 0x2e8b57 }, { "seashell", 0xfff5ee }, { "sienna", 0xa0522d },
	{ "silver", 0xc0c0c0 }, { "skyblue", 0x87ceeb }, { "slateblue", 0x6a5acd }, { "slategray", 0x708090 },
	{ "slategrey", 0x708090 }, { "snow", 0xfffafa }, { "springgreen", 0x00ff7f }, { "steelblue", 0x4682b4 },
	{ "tan", 0xd2b48c }, { "teal", 0x008080 }, { "thistle", 0xd8bfd8 }, { "tomato", 0xff6347 },
	{ "turquoise", 0x40e0d0 }, { "violet", 0xee82ee }, { "wheat", 0xf5deb3 }, { "white", 0xffffff },
	{ "whitesmoke", 0xf5f5f5 }, { "yellow", 0xffff00 }, { "yellowgreen", 0x9acd32 },
};

static int hex_digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* "red", "#3366cc", "#36c", "rgb(51, 102, 204)": its color, with its
 * opacity (0 to 1) in *alpha; or a panic */
static uint32_t color_of(const char *spec, double *alpha)
{
	char low[64];
	size_t n = 0;
	for (const char *p = spec; *p && n + 1 < sizeof low; p++)
		if (*p != ' ')
			low[n++] = (char)(*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p);
	low[n] = '\0';
	*alpha = 1;
	if (low[0] == '#') {
		int d[8], k = 0;
		for (const char *p = low + 1; *p && k < 8; p++)
			if ((d[k++] = hex_digit(*p)) < 0)
				k = 99;
		if (k == 3 || k == 4) {
			if (k == 4)
				*alpha = (d[3] * 17) / 255.0;
			return (uint32_t)(d[0] * 17) << 16 | (uint32_t)(d[1] * 17) << 8 | (uint32_t)(d[2] * 17);
		}
		if (k == 6 || k == 8) {
			if (k == 8)
				*alpha = (d[6] * 16 + d[7]) / 255.0;
			return (uint32_t)(d[0] * 16 + d[1]) << 16 | (uint32_t)(d[2] * 16 + d[3]) << 8 | (uint32_t)(d[4] * 16 + d[5]);
		}
	} else if (strncmp(low, "rgb(", 4) == 0 || strncmp(low, "rgba(", 5) == 0) {
		double r, g, b, a = 1;
		const char *args = strchr(low, '(') + 1;
		int got = sscanf(args, "%lf,%lf,%lf,%lf", &r, &g, &b, &a);
		if (got >= 3) {
			*alpha = a < 0 ? 0 : a > 1 ? 1 : a;
			r = r < 0 ? 0 : r > 255 ? 255 : r;
			g = g < 0 ? 0 : g > 255 ? 255 : g;
			b = b < 0 ? 0 : b > 255 ? 255 : b;
			return (uint32_t)(r + 0.5) << 16 | (uint32_t)(g + 0.5) << 8 | (uint32_t)(b + 0.5);
		}
	} else if (strcmp(low, "transparent") == 0) {
		*alpha = 0;
		return 0;
	} else {
		for (size_t i = 0; i < sizeof color_names / sizeof *color_names; i++)
			if (strcmp(color_names[i].name, low) == 0)
				return color_names[i].rgb;
	}
	fail("'%s' isn't a color (write a name, such as \"red\" or \"steelblue\", or \"#3366cc\")", spec);
}

/* What's being drawn: the canvas whose on_draw runs, and the pen */
static int64_t drawing_id;
static uint32_t pen_color;
static double pen_alpha = 1, pen_width = 1, font_size = 14;

static Item *pen(int64_t id)
{
	Item *it = item(id);
	if (it->kind != K_CANVAS)
		fail("%s", "that isn't a canvas");
	if (drawing_id != id)
		fail("%s", "a canvas can only be drawn on in its on_draw handler (to draw again, call its redraw)");
	return it;
}

/* one pixel, mixed with what's there by the pen's opacity */
static void dot(Item *it, int x, int y, uint32_t c, double a)
{
	if (x < 0 || y < 0 || x >= it->cw || y >= it->ch || a <= 0)
		return;
	uint32_t *p = &it->pix[y * it->cw + x];
	if (a >= 1) {
		*p = c;
		return;
	}
	uint32_t o = *p;
	uint32_t r = (uint32_t)(((c >> 16) & 255) * a + ((o >> 16) & 255) * (1 - a) + 0.5);
	uint32_t g = (uint32_t)(((c >> 8) & 255) * a + ((o >> 8) & 255) * (1 - a) + 0.5);
	uint32_t b = (uint32_t)((c & 255) * a + (o & 255) * (1 - a) + 0.5);
	*p = r << 16 | g << 8 | b;
}

/* the pixels from x0 to x1, y0 to y1 (inclusive, clipped), as ints */
static void clip_box(Item *it, double x0, double y0, double x1, double y1, int *a, int *b, int *c, int *d)
{
	*a = (int)floor(x0);
	*b = (int)floor(y0);
	*c = (int)ceil(x1);
	*d = (int)ceil(y1);
	if (*a < 0) *a = 0;
	if (*b < 0) *b = 0;
	if (*c > it->cw - 1) *c = it->cw - 1;
	if (*d > it->ch - 1) *d = it->ch - 1;
}

static double seg_dist(double px, double py, double x1, double y1, double x2, double y2)
{
	double dx = x2 - x1, dy = y2 - y1, len2 = dx * dx + dy * dy;
	double t = len2 > 0 ? ((px - x1) * dx + (py - y1) * dy) / len2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	double qx = x1 + t * dx - px, qy = y1 + t * dy - py;
	return sqrt(qx * qx + qy * qy);
}

/* A line pen_width wide, with round ends */
static void stroke_line(Item *it, double x1, double y1, double x2, double y2)
{
	double half = pen_width / 2 < 0.5 ? 0.5 : pen_width / 2;
	int a, b, c, d;
	clip_box(it, fmin(x1, x2) - half, fmin(y1, y2) - half, fmax(x1, x2) + half, fmax(y1, y2) + half, &a, &b, &c, &d);
	for (int y = b; y <= d; y++)
		for (int x = a; x <= c; x++)
			if (seg_dist(x + 0.5, y + 0.5, x1, y1, x2, y2) <= half + 0.01)
				dot(it, x, y, pen_color, pen_alpha);
}

/* An ellipse's outline or inside; with `arc`, only from..to (clock degrees) */
static void ellipse_pixels(Item *it, double cx, double cy, double rx, double ry, bool filled, bool arc, double from,
			   double to)
{
	if (rx <= 0 || ry <= 0)
		return;
	double half = pen_width / 2 < 0.5 ? 0.5 : pen_width / 2;
	double grow = filled ? 0 : half;
	int a, b, c, d;
	clip_box(it, cx - rx - grow, cy - ry - grow, cx + rx + grow, cy + ry + grow, &a, &b, &c, &d);
	double r = (rx + ry) / 2;
	while (to < from)
		to += 360;
	for (int y = b; y <= d; y++)
		for (int x = a; x <= c; x++) {
			double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry, k = sqrt(dx * dx + dy * dy);
			bool in = filled ? k <= 1 : fabs(k - 1) * r <= half + 0.01;
			if (in && arc) {
				/* clock angle: 0 at the top, clockwise */
				double ang = atan2(x + 0.5 - cx, -(y + 0.5 - cy)) * 180 / M_PI;
				while (ang < from)
					ang += 360;
				in = ang <= to;
			}
			if (in)
				dot(it, x, y, pen_color, pen_alpha);
		}
}

int64_t cby_tui_add_canvas(int64_t box, int width, int height)
{
	int64_t id = add_to(box, K_CANVAS, "");
	Item *it = item(id);
	it->cw = width < 1 ? 1 : width;
	it->ch = height < 1 ? 1 : height;
	it->pix = malloc(sizeof *it->pix * (size_t)(it->cw * it->ch));
	for (int i = 0; i < it->cw * it->ch; i++)
		it->pix[i] = 0xffffff;
	it->paper = 0xffffff;
	it->dirty = true;
	return id;
}

int cby_tui_canvas_width(int64_t id) { return item(id)->cw; }
int cby_tui_canvas_height(int64_t id) { return item(id)->ch; }

static Handler handler(int64_t id, void *obj, void *fn);

void cby_tui_canvas_on_draw(int64_t id, void *obj, void *fn)
{
	item(id)->on_draw = handler(id, obj, fn);
	item(id)->dirty = true;
}

void cby_tui_canvas_redraw(int64_t id) { item(id)->dirty = true; }

static void clear_writing(Item *it)
{
	for (int i = 0; i < it->nwriting; i++)
		free(it->writing[i].text);
	free(it->writing);
	it->writing = NULL;
	it->nwriting = 0;
}

/* Calls on_draw, on a fresh white canvas */
static void render_canvas(int64_t id)
{
	Item *it = item(id);
	it->dirty = false;
	for (int i = 0; i < it->cw * it->ch; i++)
		it->pix[i] = 0xffffff;
	it->paper = 0xffffff;
	clear_writing(it);
	if (!it->on_draw.fn)
		return;
	int64_t was = drawing_id;
	uint32_t was_c = pen_color;
	double was_a = pen_alpha, was_w = pen_width, was_f = font_size;
	drawing_id = id;
	pen_color = 0;
	pen_alpha = 1;
	pen_width = 1;
	font_size = 14;
	invoke(it->on_draw);
	drawing_id = was;
	pen_color = was_c;
	pen_alpha = was_a;
	pen_width = was_w;
	font_size = was_f;
}

/* Draws the window's canvases that need it (before its layout: on_draw is
 * the program's own code, and may change things) */
static void render_canvases(int64_t win)
{
	for (int i = 0; i < nitems; i++)
		if (items[i].kind == K_CANVAS && items[i].win == win && items[i].dirty && items[i].visible)
			render_canvas(i + 1);
}

void cby_tui_canvas_set_color(int64_t id, const char *color)
{
	pen(id);
	pen_color = color_of(color, &pen_alpha);
}

void cby_tui_canvas_fill_with(int64_t id, const char *color)
{
	Item *it = pen(id);
	double a;
	uint32_t c = color_of(color, &a);
	for (int y = 0; y < it->ch; y++)
		for (int x = 0; x < it->cw; x++)
			dot(it, x, y, c, a);
	if (a >= 1)
		it->paper = c;
}

void cby_tui_canvas_set_line_width(int64_t id, double width)
{
	pen(id);
	pen_width = width > 0 ? width : 1;
}

void cby_tui_canvas_set_font_size(int64_t id, double size)
{
	pen(id);
	font_size = size > 0 ? size : 14;
}

/* "Serif Bold 16": a terminal has its own font, so only the size counts */
void cby_tui_canvas_set_font(int64_t id, const char *font)
{
	pen(id);
	const char *last = strrchr(font, ' ');
	double size = atof(last ? last + 1 : font);
	if (size > 0)
		font_size = size;
}

void cby_tui_canvas_line(int64_t id, double x1, double y1, double x2, double y2)
{
	stroke_line(pen(id), x1, y1, x2, y2);
}

void cby_tui_canvas_rectangle(int64_t id, double x, double y, double w, double h, int filled)
{
	Item *it = pen(id);
	if (filled) {
		int a, b, c, d;
		clip_box(it, x, y, x + w - 1, y + h - 1, &a, &b, &c, &d);
		for (int py = b; py <= d; py++)
			for (int px = a; px <= c; px++)
				if (px + 0.5 >= x && px + 0.5 <= x + w && py + 0.5 >= y && py + 0.5 <= y + h)
					dot(it, px, py, pen_color, pen_alpha);
		return;
	}
	stroke_line(it, x, y, x + w, y);
	stroke_line(it, x + w, y, x + w, y + h);
	stroke_line(it, x + w, y + h, x, y + h);
	stroke_line(it, x, y + h, x, y);
}

void cby_tui_canvas_ellipse(int64_t id, double x, double y, double rx, double ry, int filled)
{
	ellipse_pixels(pen(id), x, y, rx, ry, filled != 0, false, 0, 0);
}

void cby_tui_canvas_arc(int64_t id, double x, double y, double r, double from, double to)
{
	ellipse_pixels(pen(id), x, y, r, r, false, true, from, to);
}

static double *corners;
static int ncorners, capcorners;

void cby_tui_canvas_corner(int64_t id, double x, double y)
{
	pen(id);
	if (ncorners == capcorners) {
		capcorners = capcorners ? capcorners * 2 : 16;
		corners = realloc(corners, sizeof *corners * 2 * (size_t)capcorners);
	}
	corners[2 * ncorners] = x;
	corners[2 * ncorners + 1] = y;
	ncorners++;
}

void cby_tui_canvas_polygon(int64_t id, int filled, int64_t numbers)
{
	Item *it = pen(id);
	int n = ncorners;
	ncorners = 0;
	if (numbers % 2 != 0 || numbers < 6)
		fail("%s", numbers % 2 != 0 ? "a polygon's corners come in pairs: {x, y, x, y, ...}"
					    : "a polygon needs at least 3 corners: {x, y, x, y, x, y}");
	if (!filled) {
		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			stroke_line(it, corners[2 * i], corners[2 * i + 1], corners[2 * j], corners[2 * j + 1]);
		}
		return;
	}
	double x0 = corners[0], x1 = corners[0], y0 = corners[1], y1 = corners[1];
	for (int i = 1; i < n; i++) {
		x0 = fmin(x0, corners[2 * i]);
		x1 = fmax(x1, corners[2 * i]);
		y0 = fmin(y0, corners[2 * i + 1]);
		y1 = fmax(y1, corners[2 * i + 1]);
	}
	int a, b, c, d;
	clip_box(it, x0, y0, x1, y1, &a, &b, &c, &d);
	for (int py = b; py <= d; py++)
		for (int px = a; px <= c; px++) {
			/* inside if a ray to the right crosses the edges an odd number of times */
			double qx = px + 0.5, qy = py + 0.5;
			bool in = false;
			for (int i = 0, j = n - 1; i < n; j = i++) {
				double xi = corners[2 * i], yi = corners[2 * i + 1], xj = corners[2 * j], yj = corners[2 * j + 1];
				if ((yi > qy) != (yj > qy) && qx < (xj - xi) * (qy - yi) / (yj - yi) + xi)
					in = !in;
			}
			if (in)
				dot(it, px, py, pen_color, pen_alpha);
		}
}

void cby_tui_canvas_text(int64_t id, double x, double y, const char *text)
{
	Item *it = pen(id);
	it->writing = realloc(it->writing, sizeof *it->writing * (size_t)(it->nwriting + 1));
	it->writing[it->nwriting++] = (Writing){ x, y, dupstr(text), pen_color };
}

/* A terminal can't show a picture: its name, in a frame of its own */
void cby_tui_canvas_image(int64_t id, double x, double y, const char *path)
{
	Item *it = pen(id);
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	char label[300];
	snprintf(label, sizeof label, "[%s]", name);
	it->writing = realloc(it->writing, sizeof *it->writing * (size_t)(it->nwriting + 1));
	it->writing[it->nwriting++] = (Writing){ x, y, dupstr(label), 0x808080 };
}

/* a picture of the images library's: its pixels onto the canvas's, as
 * the terminal shows them (a block for two of them) */
extern const uint8_t *cby_image_pixels(int64_t id, int *w, int *h, uint64_t *serial, uint64_t *version)
	__attribute__((weak));

void cby_tui_canvas_picture(int64_t id, double x, double y, int64_t pic)
{
	Item *it = pen(id);
	int w = 0, h = 0;
	uint64_t serial, version;
	const uint8_t *px = cby_image_pixels ? cby_image_pixels(pic, &w, &h, &serial, &version) : NULL;
	if (!px)
		fail("%s", "that picture is empty: it was handed on, or never made");
	int x0 = (int)floor(x), y0 = (int)floor(y);
	for (int r = 0; r < h; r++) {
		int ty = y0 + r;
		if (ty < 0 || ty >= it->ch)
			continue;
		for (int c = 0; c < w; c++) {
			const uint8_t *p = px + ((size_t)r * (size_t)w + (size_t)c) * 4;
			if (p[3])
				dot(it, x0 + c, ty, (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2], p[3] / 255.0);
		}
	}
}

/* ---- saving: a PNG file, written plainly (its picture data stored, not
 * squeezed: a little bigger, but nothing else is needed) */

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const unsigned char *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t k = i;
			for (int j = 0; j < 8; j++)
				k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
			crc_table[i] = k;
		}
	c ^= 0xffffffffu;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
	unsigned char head[8];
	put32(head, (uint32_t)n);
	memcpy(head + 4, type, 4);
	fwrite(head, 1, 8, f);
	if (n)
		fwrite(data, 1, n, f);
	uint32_t c = crc(crc(0, (const unsigned char *)type, 4), data, n);
	unsigned char tail[4];
	put32(tail, c);
	fwrite(tail, 1, 4, f);
}

static bool write_png(const char *path, const uint32_t *pix, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	unsigned char ihdr[13];
	put32(ihdr, (uint32_t)w);
	put32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8;   /* bits */
	ihdr[9] = 2;   /* red, green, blue */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	chunk(f, "IHDR", ihdr, 13);
	/* the rows, each after a 0 (no filter) */
	size_t raw_n = (size_t)h * (size_t)(1 + 3 * w);
	unsigned char *raw = malloc(raw_n);
	size_t k = 0;
	for (int y = 0; y < h; y++) {
		raw[k++] = 0;
		for (int x = 0; x < w; x++) {
			uint32_t c = pix[y * w + x];
			raw[k++] = (unsigned char)(c >> 16);
			raw[k++] = (unsigned char)(c >> 8);
			raw[k++] = (unsigned char)c;
		}
	}
	/* zlib: a header, stored blocks of up to 65535 bytes, the Adler-32 sum */
	size_t blocks = raw_n / 65535 + 1;
	unsigned char *z = malloc(2 + raw_n + blocks * 5 + 4);
	size_t zn = 0;
	z[zn++] = 0x78;
	z[zn++] = 0x01;
	for (size_t at = 0; at < raw_n || at == 0;) {
		size_t len = raw_n - at > 65535 ? 65535 : raw_n - at;
		z[zn++] = at + len >= raw_n ? 1 : 0;
		z[zn++] = (unsigned char)len;
		z[zn++] = (unsigned char)(len >> 8);
		z[zn++] = (unsigned char)~len;
		z[zn++] = (unsigned char)(~len >> 8);
		memcpy(z + zn, raw + at, len);
		zn += len;
		at += len;
		if (len == 0)
			break;
	}
	uint32_t s1 = 1, s2 = 0;
	for (size_t i = 0; i < raw_n; i++) {
		s1 = (s1 + raw[i]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	put32(z + zn, s2 << 16 | s1);
	zn += 4;
	chunk(f, "IDAT", z, zn);
	chunk(f, "IEND", NULL, 0);
	free(raw);
	free(z);
	return fclose(f) == 0;
}

void cby_tui_canvas_save(int64_t id, const char *path)
{
	Item *it = item(id);
	if (drawing_id)
		fail("%s", "a canvas can't be saved while it's being drawn (save it from another handler)");
	render_canvas(id);
	if (!write_png(path, it->pix, it->cw, it->ch))
		fail("can't save the canvas as '%s'", path);
}

int cby_tui_canvas_mouse_x(int64_t id) { return item(id)->mouse_x; }
int cby_tui_canvas_mouse_y(int64_t id) { return item(id)->mouse_y; }
int64_t cby_tui_canvas_key_length(int64_t id) { return (int64_t)strlen(item(id)->text); }
void cby_tui_canvas_on_click(int64_t id, void *obj, void *fn) { item(id)->on_click = handler(id, obj, fn); }
void cby_tui_canvas_on_mouse_move(int64_t id, void *obj, void *fn) { item(id)->on_move = handler(id, obj, fn); }
void cby_tui_canvas_on_key(int64_t id, void *obj, void *fn) { item(id)->on_key = handler(id, obj, fn); }

/* the canvas's pixel under a character on the screen */
static void canvas_mouse(Item *it, int x, int y)
{
	it->mouse_x = (int)((x - it->x + 0.5) * it->cw / (it->w > 0 ? it->w : 1));
	it->mouse_y = (int)((y - it->y + 0.5) * it->ch / (it->h > 0 ? it->h : 1));
}

/* the canvas's key, by name as read_key names it */
static void canvas_key(Item *it, const char *name)
{
	free(it->text);
	it->text = dupstr(name);
	invoke(it->on_key);
}

/* The color standing for the pixels from (x0, y0) to (x1, y1): the most
 * common one that isn't the paper, so thin lines still show */
static uint32_t block_color(Item *it, int x0, int y0, int x1, int y1, bool *ink)
{
	uint32_t seen[8];
	int count[8], n = 0;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			uint32_t c = it->pix[y * it->cw + x];
			if (c == it->paper)
				continue;
			int i = 0;
			while (i < n && seen[i] != c)
				i++;
			if (i == n && n < 8) {
				seen[n] = c;
				count[n++] = 0;
			}
			if (i < n)
				count[i]++;
		}
	*ink = n > 0;
	if (!n)
		return it->paper;
	int best = 0;
	for (int i = 1; i < n; i++)
		if (count[i] > count[best])
			best = i;
	return seen[best];
}

static void draw_canvas(Item *it)
{
	int gw = it->w, gh = it->h * 2;   /* blocks across and down */
	if (gw <= 0 || gh <= 0)
		return;
	for (int cy = 0; cy < it->h; cy++)
		for (int cx = 0; cx < it->w; cx++) {
			int x0 = cx * it->cw / gw, x1 = (cx + 1) * it->cw / gw;
			if (x1 <= x0)
				x1 = x0 + 1;
			uint32_t col[2];
			bool ink[2];
			for (int half = 0; half < 2; half++) {
				int by = cy * 2 + half;
				int y0 = by * it->ch / gh, y1 = (by + 1) * it->ch / gh;
				if (y1 <= y0)
					y1 = y0 + 1;
				if (x1 > it->cw)
					x1 = it->cw;
				if (y1 > it->ch)
					y1 = it->ch;
				col[half] = x0 < it->cw && y0 < it->ch ? block_color(it, x0, y0, x1, y1, &ink[half]) : it->paper;
				if (x0 >= it->cw || y0 >= it->ch)
					ink[half] = false;
			}
			/* the characters say where the drawing is when only the text is
			 * looked at (a script's `screen`); the colors say the rest */
			const char *ch = ink[0] && ink[1] ? (col[0] == col[1] ? "█" : "▀") : ink[0] ? "▀" : ink[1] ? "▄" : " ";
			if (ink[0] && ink[1] && col[0] == col[1])
				put_rgb(it->x + cx, it->y + cy, ch, 3, col[0], col[0]);
			else if (ink[1] && !ink[0])
				put_rgb(it->x + cx, it->y + cy, ch, 3, col[1], col[0]);
			else if (!ink[0] && !ink[1])
				put_rgb(it->x + cx, it->y + cy, ch, 1, col[0], col[0]);
			else
				put_rgb(it->x + cx, it->y + cy, ch, 3, col[0], col[1]);
		}
	for (int i = 0; i < it->nwriting; i++) {
		Writing *w = &it->writing[i];
		int x = it->x + (int)(w->x * it->w / it->cw), y = it->y + (int)(w->y * it->h / it->ch);
		if (y < it->y || y >= it->y + it->h)
			continue;
		const char *t = w->text;
		for (int k = 0, col = 0; t[k] && x + col < it->x + it->w; col++) {
			int e = next_char(t, k);
			if (x + col >= it->x)
				put_rgb(x + col, y, t + k, e - k, w->color, it->paper);
			k = e;
		}
	}
}

/* ------------------------------------------------------------- tables */

static const char *tcell(Item *t, int row, int col)
{
	if (row < 0 || row >= t->ntrows || col < 0 || col >= t->trows[row].n)
		return "";
	return t->trows[row].cell[col];
}

static int compare_text(const char *a, const char *b)
{
	char *ea, *eb;
	double x = strtod(a, &ea), y = strtod(b, &eb);
	if (*a && *b && !*ea && !*eb)
		return x < y ? -1 : x > y ? 1 : 0;
	return strcasecmp(a, b);
}

/* The order the rows show in: as added, or sorted by sort_col */
static void table_order(Item *t)
{
	t->order = realloc(t->order, sizeof *t->order * (size_t)(t->ntrows ? t->ntrows : 1));
	for (int i = 0; i < t->ntrows; i++)
		t->order[i] = i;
	if (t->sort_col < 0)
		return;
	/* (a simple stable insertion sort: tables are for people to read) */
	for (int i = 1; i < t->ntrows; i++) {
		int v = t->order[i], j = i;
		while (j > 0) {
			int c = compare_text(tcell(t, t->order[j - 1], t->sort_col), tcell(t, v, t->sort_col));
			if (t->sort_down ? c >= 0 : c <= 0)
				break;
			t->order[j] = t->order[j - 1];
			j--;
		}
		t->order[j] = v;
	}
}

static int table_pos(Item *t, int row)
{
	for (int i = 0; i < t->ntrows; i++)
		if (t->order[i] == row)
			return i;
	return -1;
}

static void table_sort(Item *t, int col, bool down)
{
	t->sort_col = col;
	t->sort_down = down;
	table_order(t);
}

/* the selected row becomes `row` (-1: none), and on_select hears */
static void table_pick(Item *t, int row)
{
	if (row < -1 || row >= t->ntrows || row == t->selected)
		return;
	t->selected = row;
	invoke(t->on_select);
}

/* each column's width, fitting w (the last ones give up room first) */
static void table_widths(Item *t, int w, int *cw)
{
	int total = 0;
	for (int c = 0; c < t->nopts; c++) {
		int m = text_cols(t->opts[c], (int)strlen(t->opts[c])) + 2;   /* room for a sort mark */
		for (int r = 0; r < t->ntrows; r++) {
			int k = text_cols(tcell(t, r, c), (int)strlen(tcell(t, r, c)));
			if (k > m)
				m = k;
		}
		cw[c] = m > 30 ? 30 : m;
		total += cw[c] + (c ? 2 : 0);
	}
	for (int c = t->nopts - 1; c >= 0 && total > w - 1; c--) {
		int give = total - (w - 1);
		int can = cw[c] - 3;
		if (can > give)
			can = give;
		if (can > 0) {
			cw[c] -= can;
			total -= can;
		}
	}
}

static void draw_table(Item *it, bool focused)
{
	int cw[64], nc = it->nopts < 64 ? it->nopts : 64;
	table_widths(it, it->w, cw);
	/* the headings */
	fill(it->x, it->y, it->w, S_FIELD);
	for (int c = 0, at = it->x + 1; c < nc; c++) {
		int n = (int)strlen(it->opts[c]);
		int k = put_text(at, it->y, it->opts[c], n, cw[c], S_FIELD);
		if (c == it->sort_col)
			put(at + k + 1 < at + cw[c] ? at + k + 1 : at + cw[c] - 1, it->y, it->sort_down ? "▼" : "▲", 3, S_FIELD);
		at += cw[c] + 2;
	}
	int body = it->h - 1;
	int sel = it->selected >= 0 ? table_pos(it, it->selected) : -1;
	if (sel >= 0) {
		if (sel < it->scroll)
			it->scroll = sel;
		if (sel >= it->scroll + body)
			it->scroll = sel - body + 1;
	}
	if (it->scroll > it->ntrows - body)
		it->scroll = it->ntrows - body > 0 ? it->ntrows - body : 0;
	for (int r = 0; r < body; r++) {
		int k = it->scroll + r;
		bool is_sel = k == sel && k < it->ntrows;
		int st = is_sel ? (focused ? S_FOCUS : S_SELECTED) : S_NORMAL;
		fill(it->x, it->y + 1 + r, it->w, st);
		if (k >= it->ntrows)
			continue;
		int row = it->order[k];
		for (int c = 0, at = it->x + 1; c < nc; c++) {
			const char *t = tcell(it, row, c);
			put_text(at, it->y + 1 + r, t, (int)strlen(t), cw[c], st);
			at += cw[c] + 2;
		}
	}
	if (it->ntrows > body && body > 0) {
		int pos = body * it->scroll / (it->ntrows - body + 1);
		if (pos >= body)
			pos = body - 1;
		put(it->x + it->w - 1, it->y + 1 + pos, "▐", 3, S_FIELD);
	}
}

static bool table_key(Item *it, int key)
{
	int pos = it->selected >= 0 ? table_pos(it, it->selected) : -1, body = it->h - 1 > 1 ? it->h - 1 : 1;
	int to = pos;
	if (key == KEY_UP) to = pos < 0 ? 0 : pos - 1;
	else if (key == KEY_DOWN) to = pos + 1;
	else if (key == KEY_PGUP) to = pos - body;
	else if (key == KEY_PGDN) to = pos + body;
	else if (key == KEY_HOME) to = 0;
	else if (key == KEY_END) to = it->ntrows - 1;
	else if (key == KEY_LEFT || key == KEY_RIGHT) {
		/* sort by the column before or after */
		int c = it->sort_col + (key == KEY_LEFT ? -1 : 1);
		if (c < -1)
			c = it->nopts - 1;
		if (c >= it->nopts)
			c = -1;
		table_sort(it, c, false);
		return true;
	} else
		return false;
	if (it->ntrows == 0)
		return true;
	if (to < 0)
		to = 0;
	if (to >= it->ntrows)
		to = it->ntrows - 1;
	table_pick(it, it->order[to]);
	return true;
}

static void table_click(Item *it, int x, int y)
{
	if (y == it->y) {
		/* a heading: sort by it, or the other way if it already is */
		int cw[64];
		table_widths(it, it->w, cw);
		for (int c = 0, at = it->x + 1; c < it->nopts && c < 64; c++) {
			if (x >= at - 1 && x < at + cw[c] + 1) {
				table_sort(it, c, c == it->sort_col ? !it->sort_down : false);
				return;
			}
			at += cw[c] + 2;
		}
		return;
	}
	int k = it->scroll + (y - it->y - 1);
	if (k >= 0 && k < it->ntrows)
		table_pick(it, it->order[k]);
}

static void table_fail(const char *what, int n)
{
	char s[24];
	snprintf(s, sizeof s, "%d", n);
	fail(what, s);
}

int64_t cby_tui_add_table(int64_t box)
{
	int64_t id = add_to(box, K_TABLE, "");
	item(id)->sort_col = -1;
	return id;
}

void cby_tui_table_heading(int64_t id, const char *heading) { add_opt(item(id), heading); }

void cby_tui_table_row_start(int64_t id)
{
	Item *t = item(id);
	if (t->ntrows == t->captrows) {
		t->captrows = t->captrows ? t->captrows * 2 : 16;
		t->trows = realloc(t->trows, sizeof *t->trows * (size_t)t->captrows);
	}
	t->trows[t->ntrows++] = (TRow){ NULL, 0 };
}

void cby_tui_table_row_cell(int64_t id, const char *text)
{
	Item *t = item(id);
	TRow *r = &t->trows[t->ntrows - 1];
	r->cell = realloc(r->cell, sizeof *r->cell * (size_t)(r->n + 1));
	r->cell[r->n++] = dupstr(text);
}

void cby_tui_table_row_end(int64_t id) { table_order(item(id)); }

void cby_tui_table_set_cell(int64_t id, int row, int col, const char *text)
{
	Item *t = item(id);
	if (row < 0 || row >= t->ntrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	if (col < 0 || col >= t->nopts)
		table_fail("the table has no column %s (columns count from 0)", col);
	TRow *r = &t->trows[row];
	while (r->n <= col) {
		r->cell = realloc(r->cell, sizeof *r->cell * (size_t)(r->n + 1));
		r->cell[r->n++] = dupstr("");
	}
	free(r->cell[col]);
	r->cell[col] = dupstr(text);
	table_order(t);
}

int64_t cby_tui_table_cell_length(int64_t id, int row, int col)
{
	Item *t = item(id);
	if (row < 0 || row >= t->ntrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	free(t->cache);
	t->cache = dupstr(tcell(t, row, col));
	return (int64_t)strlen(t->cache);
}

char cby_tui_table_cell_char(int64_t id, int64_t i)
{
	Item *t = item(id);
	return t->cache && i >= 0 && i < (int64_t)strlen(t->cache) ? t->cache[i] : 0;
}

static void free_rows(Item *t)
{
	for (int r = 0; r < t->ntrows; r++) {
		for (int i = 0; i < t->trows[r].n; i++)
			free(t->trows[r].cell[i]);
		free(t->trows[r].cell);
	}
	t->ntrows = 0;
}

void cby_tui_table_remove_row(int64_t id, int row)
{
	Item *t = item(id);
	if (row < 0 || row >= t->ntrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	for (int i = 0; i < t->trows[row].n; i++)
		free(t->trows[row].cell[i]);
	free(t->trows[row].cell);
	memmove(&t->trows[row], &t->trows[row + 1], sizeof *t->trows * (size_t)(t->ntrows - row - 1));
	t->ntrows--;
	table_order(t);
	if (t->selected == row) {
		t->selected = -1;
		invoke(t->on_select);
	} else if (t->selected > row) {
		t->selected--;
	}
}

void cby_tui_table_clear(int64_t id)
{
	Item *t = item(id);
	free_rows(t);
	table_order(t);
	if (t->selected >= 0) {
		t->selected = -1;
		invoke(t->on_select);
	}
}

int cby_tui_table_rows(int64_t id) { return item(id)->ntrows; }
int cby_tui_table_columns(int64_t id) { return item(id)->nopts; }

static void set_slider(Item *it, int v);

/* ------------------------------------------------------------- number boxes, colors and fonts
 *
 * A number box is `‹ 7 ›`: Left and Right (or a click on either end) make
 * it one less or more. A color or font button shows its choice; Enter or
 * a click opens a list to choose from (a terminal can't show fonts, so a
 * font button only keeps the font's name, for the program to use). */

static const char *const picker_colors[] = {
	"black", "dimgray", "gray", "silver", "white", "maroon", "red", "crimson", "orange", "gold", "yellow",
	"olive", "green", "lime", "forestgreen", "teal", "cyan", "skyblue", "steelblue", "blue", "navy",
	"purple", "magenta", "pink", "brown", "tan", NULL };
static const char *const picker_fonts[] = {
	"Sans 12", "Sans Bold 12", "Serif 12", "Serif Italic 12", "Monospace 12", "Sans 16", "Serif 16",
	"Monospace 16", "Sans 24", "Serif 24", NULL };

static struct {
	int64_t target;   /* 0: no list is open */
	int sel, scroll, n;
	const char *const *opts;
	int x, y, h;      /* where it was last drawn */
} picker;

int64_t cby_tui_add_number(int64_t box, int min, int max)
{
	int64_t id = add_to(box, K_NUMBER, "");
	Item *it = item(id);
	it->min = min < max ? min : max;
	it->max = min < max ? max : min;
	it->value = it->min;
	return id;
}

int cby_tui_number_get(int64_t id) { return item(id)->value; }
void cby_tui_number_set(int64_t id, int value) { set_slider(item(id), value); }

/* a color as "#rrggbb", from any way of writing one */
static char *color_text(const char *spec)
{
	double a;
	uint32_t c = color_of(spec, &a);
	char buf[16];
	if (a < 1)
		snprintf(buf, sizeof buf, "#%06x%02x", c, (int)(a * 255 + 0.5));
	else
		snprintf(buf, sizeof buf, "#%06x", c);
	return dupstr(buf);
}

int64_t cby_tui_add_color_button(int64_t box)
{
	int64_t id = add_to(box, K_COLOR, "");
	free(item(id)->text);
	item(id)->text = dupstr("#000000");
	return id;
}

int64_t cby_tui_color_length(int64_t id) { return (int64_t)strlen(item(id)->text); }

void cby_tui_color_set(int64_t id, const char *color)
{
	Item *it = item(id);
	char *t = color_text(color);
	bool same = !strcmp(t, it->text);
	free(it->text);
	it->text = t;
	if (!same)
		invoke(it->on_change);
}

int64_t cby_tui_add_font_button(int64_t box)
{
	int64_t id = add_to(box, K_FONT, "");
	free(item(id)->text);
	item(id)->text = dupstr("Sans 12");
	return id;
}

int64_t cby_tui_font_length(int64_t id) { return (int64_t)strlen(item(id)->text); }

void cby_tui_font_set(int64_t id, const char *font)
{
	Item *it = item(id);
	bool same = !strcmp(font, it->text);
	free(it->text);
	it->text = dupstr(font);
	if (!same)
		invoke(it->on_change);
}

static void open_picker(int64_t id)
{
	Item *it = item(id);
	picker.target = id;
	picker.opts = it->kind == K_COLOR ? picker_colors : picker_fonts;
	picker.n = 0;
	while (picker.opts[picker.n])
		picker.n++;
	picker.sel = 0;
	picker.scroll = 0;
	for (int i = 0; i < picker.n; i++) {
		if (it->kind == K_FONT ? !strcmp(picker.opts[i], it->text) : 0)
			picker.sel = i;
		if (it->kind == K_COLOR) {
			char *t = color_text(picker.opts[i]);
			if (!strcmp(t, it->text))
				picker.sel = i;
			free(t);
		}
	}
}

static void draw_picker(void)
{
	if (!picker.target)
		return;
	Item *it = item(picker.target);
	/* under the button, or over it if there's more room there */
	int w = 24, below = scr_h - (it->y + 1), above = it->y;
	int room = below >= picker.n + 2 || below >= above ? below : above;
	int h = picker.n + 2 < room ? picker.n + 2 : room;
	if (h < 3)
		h = 3;
	int x = it->x, y = room == below ? it->y + 1 : it->y - h;
	if (x + w > scr_w)
		x = scr_w - w > 0 ? scr_w - w : 0;
	if (y < 0)
		y = 0;
	picker.x = x;
	picker.y = y;
	picker.h = h;
	for (int j = 0; j < h; j++)
		fill(x, y + j, w, S_NORMAL);
	frame(x, y, w, h, it->kind == K_COLOR ? "Color" : "Font", S_DIALOG);
	int rows = h - 2;
	if (picker.sel < picker.scroll)
		picker.scroll = picker.sel;
	if (picker.sel >= picker.scroll + rows)
		picker.scroll = picker.sel - rows + 1;
	for (int r = 0; r < rows; r++) {
		int i = picker.scroll + r;
		if (i >= picker.n)
			break;
		int st = i == picker.sel ? S_FOCUS : S_NORMAL;
		fill(x + 1, y + 1 + r, w - 2, st);
		int at = x + 2;
		if (it->kind == K_COLOR) {
			double a;
			uint32_t c = color_of(picker.opts[i], &a);
			put_rgb(at, y + 1 + r, "█", 3, c, c);
			put_rgb(at + 1, y + 1 + r, "█", 3, c, c);
			at += 3;
		}
		put_text(at, y + 1 + r, picker.opts[i], (int)strlen(picker.opts[i]), w - (at - x) - 1, st);
	}
	cursor_x = cursor_y = -1;
}

static void picker_key(int key, const char *ch, int chn)
{
	if (key == KEY_ESC) {
		picker.target = 0;
	} else if (key == KEY_UP && picker.sel > 0) {
		picker.sel--;
	} else if (key == KEY_DOWN && picker.sel < picker.n - 1) {
		picker.sel++;
	} else if (key == KEY_ENTER || (key == 0 && chn == 1 && ch[0] == ' ')) {
		int64_t t = picker.target;
		picker.target = 0;
		if (item(t)->kind == K_COLOR)
			cby_tui_color_set(t, picker.opts[picker.sel]);
		else
			cby_tui_font_set(t, picker.opts[picker.sel]);
	}
}

/* ------------------------------------------------------------- menus and shortcuts
 *
 * A window's menu bar is its first row inside the border. F10 (or a click
 * on a title, or Alt and a title's first letter) opens a menu: Up and Down
 * choose, Enter runs an item, Left and Right go to the next menu, Esc
 * closes it. A shortcut ("Ctrl+S", "F5") runs its item from anywhere in
 * the window. */

static struct {
	int64_t win;   /* 0: no menu is open */
	int menu;      /* which, in the window's menus */
	int sel;       /* the item chosen in it (-1: none) */
} open_menu;

/* A shortcut as written ("Ctrl+Shift+S", "F5") in the one way it's kept
 * ("ctrl+shift+s"); false if it isn't one. As in gui.c. */
static bool parse_keys(const char *spec, char *norm, size_t nsize)
{
	static const char *const keys[] = { "enter", "return", "escape", "esc", "space", "tab", "backspace", "delete",
					    "insert", "home", "end", "page_up", "pageup", "page_down", "pagedown", "up",
					    "down", "left", "right" };
	bool ctrl = false, shift = false, alt = false;
	char key[32] = "";
	const char *p = spec;
	while (*p) {
		const char *plus = strchr(p, '+');
		if (plus == p)
			plus = strchr(p + 1, '+');
		size_t n = plus ? (size_t)(plus - p) : strlen(p);
		char part[32];
		if (n == 0 || n >= sizeof part)
			return false;
		for (size_t i = 0; i < n; i++)
			part[i] = (char)(p[i] >= 'A' && p[i] <= 'Z' ? p[i] - 'A' + 'a' : p[i]);
		part[n] = '\0';
		if (plus && (!strcmp(part, "ctrl") || !strcmp(part, "control")))
			ctrl = true;
		else if (plus && !strcmp(part, "shift"))
			shift = true;
		else if (plus && !strcmp(part, "alt"))
			alt = true;
		else if (plus)
			return false;
		else {
			if (n == 1 && (unsigned char)part[0] > ' ' && (unsigned char)part[0] < 127)
				snprintf(key, sizeof key, "%s", part);
			else if (part[0] == 'f' && n <= 3 && atoi(part + 1) >= 1 && atoi(part + 1) <= 12)
				snprintf(key, sizeof key, "f%d", atoi(part + 1));
			else
				for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
					if (!strcmp(part, keys[i]))
						snprintf(key, sizeof key, "%s", !strcmp(part, "return") ? "enter" : !strcmp(part, "esc") ? "escape"
							 : !strcmp(part, "pageup") ? "page_up" : !strcmp(part, "pagedown") ? "page_down" : part);
			break;
		}
		p = plus + 1;
	}
	if (!key[0])
		return false;
	snprintf(norm, nsize, "%s%s%s%s", ctrl ? "ctrl+" : "", alt ? "alt+" : "", shift ? "shift+" : "", key);
	return true;
}

/* A key from the terminal, named as parse_keys names shortcuts */
static void key_shortcut_name(int key, const char *ch, int chn, char *out, size_t size)
{
	static const struct { int key; const char *name; } names[] = {
		{ KEY_ENTER, "enter" }, { KEY_ESC, "escape" }, { KEY_TAB, "tab" }, { KEY_BACKSPACE, "backspace" },
		{ KEY_DELETE, "delete" }, { KEY_HOME, "home" }, { KEY_END, "end" }, { KEY_PGUP, "page_up" },
		{ KEY_PGDN, "page_down" }, { KEY_UP, "up" }, { KEY_DOWN, "down" }, { KEY_LEFT, "left" },
		{ KEY_RIGHT, "right" },
	};
	out[0] = '\0';
	if (key >= KEY_F1 && key < KEY_F1 + 12)
		snprintf(out, size, "f%d", key - KEY_F1 + 1);
	else if (key >= KEY_CTRL && key < KEY_CTRL + 128)
		snprintf(out, size, "ctrl+%c", key - KEY_CTRL);
	else if (key >= KEY_ALT && key < KEY_ALT + 128)
		snprintf(out, size, "alt+%c", key - KEY_ALT);
	else if (key == 0 && chn == 1 && ch[0] == ' ')
		snprintf(out, size, "space");
	else if (key == 0 && chn == 1 && ch[0] >= 'A' && ch[0] <= 'Z')
		snprintf(out, size, "shift+%c", ch[0] - 'A' + 'a');
	else if (key == 0 && chn > 0 && chn < (int)size) {
		memcpy(out, ch, (size_t)chn);
		out[chn] = '\0';
	} else
		for (size_t i = 0; i < sizeof names / sizeof *names; i++)
			if (names[i].key == key)
				snprintf(out, size, "%s", names[i].name);
}

static void add_member(Item *owner, int64_t id)
{
	owner->menus = realloc(owner->menus, sizeof *owner->menus * (size_t)(owner->nmenus + 1));
	owner->menus[owner->nmenus++] = id;
}

static void run_item(int64_t id)
{
	Item *it = item(id);
	if (it->enabled && !it->separator)
		invoke(it->on_click);
}

/* The item (in the window's menus or shortcuts) with these keys, or 0 */
static int64_t shortcut_item(int64_t win, const char *name)
{
	Item *w = item(win);
	for (int i = 0; i < w->nmenus; i++) {
		Item *m = item(w->menus[i]);
		if (m->kind == K_SHORTCUT && m->keys && !strcmp(m->keys, name))
			return w->menus[i];
		if (m->kind == K_MENU)
			for (int j = 0; j < m->nmenus; j++) {
				Item *it = item(m->menus[j]);
				if (it->keys && it->keys[0] && !strcmp(it->keys, name))
					return m->menus[j];
			}
	}
	return 0;
}

static const char *keys_fail = "'%s' isn't a shortcut: write keys such as \"Ctrl+S\", \"Ctrl+Shift+Z\", \"Alt+X\" or \"F5\"";

int64_t cby_tui_add_menu(int64_t win, const char *title)
{
	int64_t id = new_item(K_MENU, win);
	free(item(id)->text);
	item(id)->text = dupstr(title);
	add_member(item(win), id);
	return id;
}

int64_t cby_tui_menu_add_item(int64_t menu, const char *text, const char *keys, void *obj, void *fn)
{
	char norm[64] = "";
	if (keys && *keys && !parse_keys(keys, norm, sizeof norm))
		fail(keys_fail, keys);
	int64_t id = new_item(K_MENU_ITEM, item(menu)->win);
	Item *it = item(id);
	free(it->text);
	it->text = dupstr(text);
	it->keys = dupstr(norm);
	it->keys_shown = dupstr(keys ? keys : "");
	it->on_click = (Handler){ obj, fn, it->win };
	it->parent = menu;
	add_member(item(menu), id);
	return id;
}

void cby_tui_menu_add_separator(int64_t menu)
{
	int64_t id = new_item(K_MENU_ITEM, item(menu)->win);
	item(id)->separator = true;
	item(id)->parent = menu;
	add_member(item(menu), id);
}

void cby_tui_menu_item_set_enabled(int64_t id, int on) { item(id)->enabled = on != 0; }

void cby_tui_add_shortcut(int64_t win, const char *keys, void *obj, void *fn)
{
	char norm[64];
	if (!parse_keys(keys, norm, sizeof norm))
		fail(keys_fail, keys);
	int64_t id = new_item(K_SHORTCUT, win);
	item(id)->keys = dupstr(norm);
	item(id)->on_click = (Handler){ obj, fn, win };
	add_member(item(win), id);
}

/* the window's menus, as the numbers of their items in w->menus */
static int menu_count(Item *w)
{
	int n = 0;
	for (int i = 0; i < w->nmenus; i++)
		n += item(w->menus[i])->kind == K_MENU;
	return n;
}

static Item *nth_menu(Item *w, int k)
{
	for (int i = 0; i < w->nmenus; i++)
		if (item(w->menus[i])->kind == K_MENU && k-- == 0)
			return item(w->menus[i]);
	return NULL;
}

static bool usable(Item *m, int i)
{
	return i >= 0 && i < m->nmenus && !item(m->menus[i])->separator && item(m->menus[i])->enabled;
}

/* the next item that can be chosen, from `from` going `step` (-1 or 1) */
static int next_usable(Item *m, int from, int step)
{
	for (int k = 0, i = from; k < m->nmenus; k++) {
		i = ((i + step) % m->nmenus + m->nmenus) % m->nmenus;
		if (usable(m, i))
			return i;
	}
	return -1;
}

static void open_menu_at(int64_t win, int k)
{
	Item *w = item(win);
	int n = menu_count(w);
	if (n == 0)
		return;
	k = (k % n + n) % n;
	open_menu.win = win;
	open_menu.menu = k;
	open_menu.sel = next_usable(nth_menu(w, k), -1, 1);
}

static void close_menu(void) { open_menu.win = 0; }

/* The window's menu bar; the open menu's box below it */
static void draw_menus(int64_t win, int x, int y, int w)
{
	Item *wi = item(win);
	if (!wi->nmenus || !menu_count(wi))
		return;
	fill(x + 1, y + 1, w - 2, S_FIELD);
	int at = x + 2;
	for (int k = 0; k < menu_count(wi); k++) {
		Item *m = nth_menu(wi, k);
		bool open = open_menu.win == win && open_menu.menu == k;
		int n = (int)strlen(m->text);
		m->bar_x = at;
		put(at, y + 1, " ", 1, open ? S_FOCUS : S_FIELD);
		int c = put_text(at + 1, y + 1, m->text, n, w - (at - x) - 2, open ? S_FOCUS : S_FIELD);
		put(at + 1 + c, y + 1, " ", 1, open ? S_FOCUS : S_FIELD);
		at += c + 3;
	}
	if (open_menu.win != win)
		return;
	Item *m = nth_menu(wi, open_menu.menu);
	if (!m)
		return;
	int lw = 0, kw = 0;
	for (int i = 0; i < m->nmenus; i++) {
		Item *it = item(m->menus[i]);
		int a = text_cols(it->text, (int)strlen(it->text)), b = it->keys_shown ? (int)strlen(it->keys_shown) : 0;
		if (a > lw)
			lw = a;
		if (b > kw)
			kw = b;
	}
	int bw = lw + (kw ? kw + 3 : 0) + 4, bh = m->nmenus + 2;
	int bx = m->bar_x, by = y + 2;
	if (bx + bw > scr_w)
		bx = scr_w - bw;
	if (bx < 0)
		bx = 0;
	for (int j = 0; j < bh; j++)
		fill(bx, by + j, bw, S_NORMAL);
	frame(bx, by, bw, bh, NULL, S_BORDER);
	for (int i = 0; i < m->nmenus; i++) {
		Item *it = item(m->menus[i]);
		int ry = by + 1 + i;
		if (it->separator) {
			put(bx, ry, "├", 3, S_BORDER);
			for (int k = 1; k < bw - 1; k++)
				put(bx + k, ry, "─", 3, S_BORDER);
			put(bx + bw - 1, ry, "┤", 3, S_BORDER);
			continue;
		}
		int st = i == open_menu.sel ? S_FOCUS : it->enabled ? S_NORMAL : S_DIM;
		fill(bx + 1, ry, bw - 2, st);
		put_text(bx + 2, ry, it->text, (int)strlen(it->text), lw, st);
		if (it->keys_shown && *it->keys_shown) {
			int kl = (int)strlen(it->keys_shown);
			put_text(bx + bw - 2 - kl, ry, it->keys_shown, kl, kl, i == open_menu.sel ? S_FOCUS : S_DIM);
		}
	}
	cursor_x = cursor_y = -1;
}

/* A key while a menu is open */
static void menu_key(int key, const char *ch, int chn)
{
	int64_t win = open_menu.win;
	Item *m = nth_menu(item(win), open_menu.menu);
	if (!m) {
		close_menu();
		return;
	}
	if (key == KEY_ESC || key == KEY_F1 + 9) {
		close_menu();
	} else if (key == KEY_LEFT || key == KEY_RIGHT) {
		open_menu_at(win, open_menu.menu + (key == KEY_LEFT ? -1 : 1));
	} else if (key == KEY_UP || key == KEY_DOWN || key == KEY_TAB || key == KEY_BACKTAB) {
		int step = key == KEY_UP || key == KEY_BACKTAB ? -1 : 1;
		int n = next_usable(m, open_menu.sel < 0 ? (step > 0 ? -1 : 0) : open_menu.sel, step);
		if (n >= 0)
			open_menu.sel = n;
	} else if (key == KEY_ENTER || (key == 0 && chn == 1 && ch[0] == ' ')) {
		int sel = open_menu.sel;
		close_menu();
		if (usable(m, sel))
			run_item(m->menus[sel]);
	}
}

/* A click while the window has menus: on the bar, or in the open menu.
 * True if it was for them. */
static bool menu_click(int64_t win, int x, int y)
{
	Item *w = item(win);
	if (!menu_count(w))
		return false;
	int fx, fy, fw, fh;
	window_frame(w, &fx, &fy, &fw, &fh);
	if (y == fy + 1) {
		for (int k = menu_count(w) - 1; k >= 0; k--) {
			Item *m = nth_menu(w, k);
			if (x >= m->bar_x) {
				if (open_menu.win == win && open_menu.menu == k)
					close_menu();
				else
					open_menu_at(win, k);
				return true;
			}
		}
		close_menu();
		return true;
	}
	if (open_menu.win != win)
		return false;
	Item *m = nth_menu(w, open_menu.menu);
	int i = y - (fy + 3);
	close_menu();
	if (m && i >= 0 && i < m->nmenus && x >= m->bar_x - 1 && usable(m, i))
		run_item(m->menus[i]);
	return true;
}

/* ------------------------------------------------------------- drawing */

static void draw_item(int64_t id, int64_t focus);

static void draw_entry(Item *it, bool focused, int style_field)
{
	int st = focused ? S_FOCUS : style_field;
	int w = it->w;
	fill(it->x, it->y, w, st);
	int len = (int)strlen(it->text);
	int ccol = text_cols(it->text, it->cur);
	if (ccol - it->scroll >= w - 1)
		it->scroll = ccol - w + 2;
	if (ccol < it->scroll)
		it->scroll = ccol;
	/* skip `scroll` columns */
	int i = 0;
	for (int c = 0; c < it->scroll && i < len; c++)
		i = next_char(it->text, i);
	put_text(it->x, it->y, it->text + i, len - i, w - 1, st);
	if (focused) {
		cursor_x = it->x + ccol - it->scroll;
		cursor_y = it->y;
	}
}

static void draw_lines(Item *it, bool focused)
{
	/* a text area: its lines, scrolled to keep the cursor in view */
	int st = focused ? S_FOCUS : S_FIELD;
	int *rows;
	int n = wrap_rows(it->text, it->w - 1, &rows);
	int cl = row_of(rows, n, it->cur);
	if (cl < it->scroll)
		it->scroll = cl;
	if (cl >= it->scroll + it->h)
		it->scroll = cl - it->h + 1;
	if (it->scroll > 0 && it->scroll + it->h > n)   /* rows taken away: no empty space below */
		it->scroll = n > it->h ? n - it->h : 0;
	for (int r = 0; r < it->h; r++) {
		fill(it->x, it->y + r, it->w, st);
		int k = it->scroll + r;
		if (k >= n)
			continue;
		put_text(it->x, it->y + r, it->text + rows[2 * k], rows[2 * k + 1], it->w, st);
	}
	if (focused) {
		int s = rows[2 * cl];
		cursor_x = it->x + text_cols(it->text + s, it->cur - s);
		if (cursor_x >= it->x + it->w)
			cursor_x = it->x + it->w - 1;
		cursor_y = it->y + cl - it->scroll;
	}
	free(rows);
}

static void draw_list(Item *it, bool focused)
{
	if (it->selected >= 0) {
		if (it->selected < it->scroll)
			it->scroll = it->selected;
		if (it->selected >= it->scroll + it->h)
			it->scroll = it->selected - it->h + 1;
	}
	for (int r = 0; r < it->h; r++) {
		int k = it->scroll + r;
		bool sel = k == it->selected;
		int st = sel ? (focused ? S_FOCUS : S_SELECTED) : S_FIELD;
		fill(it->x, it->y + r, it->w, st);
		if (k < it->nopts)
			put_text(it->x + 1, it->y + r, it->opts[k], (int)strlen(it->opts[k]), it->w - 2, st);
	}
	if (it->nopts > it->h) {
		/* where in the list we are */
		int pos = it->h * it->scroll / (it->nopts - it->h + 1);
		if (pos >= it->h)
			pos = it->h - 1;
		put(it->x + it->w - 1, it->y + pos, "▐", 3, S_FIELD);
	}
}

static void draw_item_itself(int64_t id, int64_t focus);

static void draw_item(int64_t id, int64_t focus)
{
	Item *it = item(id);
	if (!it->visible)
		return;
	bool had_ink = ink_on, had_paper = paper_on;
	uint32_t was_ink = ink_now, was_paper = paper_now;
	if (it->has_ink) {
		ink_on = true;
		ink_now = it->ink;
	} else if (it->has_paper && !ink_on) {
		/* a background with no text color: black or white, whichever reads */
		uint32_t c = it->paper_rgb;
		double light = 0.299 * (c >> 16) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255);
		ink_on = true;
		ink_now = light < 128 ? 0xf5f5f5 : 0x1a1a1a;
	}
	if (it->has_paper) {
		paper_on = true;
		paper_now = it->paper_rgb;
		/* its whole place in the background color (a row's gaps too) */
		for (int r = 0; r < it->h; r++)
			for (int c = 0; c < it->w; c++)
				put(it->x + c, it->y + r, " ", 1, S_NORMAL);
	}
	draw_item_itself(id, focus);
	ink_on = had_ink;
	paper_on = had_paper;
	ink_now = was_ink;
	paper_now = was_paper;
}

static void draw_item_itself(int64_t id, int64_t focus)
{
	Item *it = item(id);
	bool f = id == focus;
	int len = (int)strlen(it->text);
	int dim = it->enabled ? S_NORMAL : S_DIM;
	char buf[64];
	switch (it->kind) {
	case K_BOX:
		if (it->scrolls) {
			/* only what shows in its own rows, with a mark for where it is */
			int sx0 = clip_x0, sy0 = clip_y0, sx1 = clip_x1, sy1 = clip_y1;
			if (it->x > clip_x0) clip_x0 = it->x;
			if (it->y > clip_y0) clip_y0 = it->y;
			if (it->x + it->w < clip_x1) clip_x1 = it->x + it->w;
			if (it->y + it->h < clip_y1) clip_y1 = it->y + it->h;
			for (int i = 0; i < it->nkids; i++)
				draw_item(it->kids[i], focus);
			if (it->content_h > it->h && it->h > 0) {
				for (int r = 0; r < it->h; r++)
					put(it->x + it->w - 1, it->y + r, "│", 3, S_DIM);
				int pos = it->h * it->scroll / (it->content_h - it->h + 1);
				if (pos >= it->h)
					pos = it->h - 1;
				put(it->x + it->w - 1, it->y + pos, "█", 3, S_BORDER);
			}
			clip_x0 = sx0;
			clip_y0 = sy0;
			clip_x1 = sx1;
			clip_y1 = sy1;
			break;
		}
		for (int i = 0; i < it->nkids; i++)
			draw_item(it->kids[i], focus);
		break;
	case K_TABS: {
		int at = it->x;
		for (int i = 0; i < it->nopts; i++) {
			bool sel = i == it->selected;
			int st = sel ? (f ? S_FOCUS : S_SELECTED) : it->enabled ? S_NORMAL : S_DIM;
			int n = (int)strlen(it->opts[i]);
			put(at, it->y, " ", 1, st);
			int c = put_text(at + 1, it->y, it->opts[i], n, it->x + it->w - at - 2, st);
			put(at + 1 + c, it->y, " ", 1, st);
			at += c + 2;
			if (i + 1 < it->nopts)
				put(at++, it->y, "│", 3, S_BORDER);
		}
		for (int i = at; i < it->x + it->w; i++)
			put(i, it->y, "─", 3, S_BORDER);
		if (it->selected >= 0 && it->selected < it->nkids)
			draw_item(it->kids[it->selected], focus);
		break;
	}
	case K_LABEL:
		for (int k = 0; k < it->h && k < line_count(it->text); k++) {
			int s = line_start(it->text, k);
			put_text(it->x, it->y + k, it->text + s, line_len(it->text, s), it->w, dim);
		}
		break;
	case K_BUTTON: {
		int st = f ? S_FOCUS : it->enabled ? S_BUTTON : S_DIM;
		put(it->x, it->y, "[", 1, st);
		put(it->x + 1, it->y, " ", 1, st);
		int c = put_text(it->x + 2, it->y, it->text, len, it->w - 4, st);
		put(it->x + 2 + c, it->y, " ", 1, st);
		put(it->x + 3 + c, it->y, "]", 1, st);
		break;
	}
	case K_ENTRY:
		draw_entry(it, f, it->enabled ? S_FIELD : S_DIM);
		break;
	case K_CHECK: {
		int st = f ? S_FOCUS : dim;
		put_text(it->x, it->y, it->value ? "[x] " : "[ ] ", 4, 4, st);
		put_text(it->x + 4, it->y, it->text, len, it->w - 4, st);
		break;
	}
	case K_CHOICE: {
		int st = f ? S_FOCUS : it->enabled ? S_FIELD : S_DIM;
		fill(it->x, it->y, it->w, st);
		put(it->x, it->y, "<", 1, st);
		put(it->x + it->w - 1, it->y, ">", 1, st);
		if (it->selected >= 0 && it->selected < it->nopts)
			put_text(it->x + 2, it->y, it->opts[it->selected], (int)strlen(it->opts[it->selected]), it->w - 4, st);
		break;
	}
	case K_TEXT_AREA:
		draw_lines(it, f);
		break;
	case K_LIST:
		draw_list(it, f);
		break;
	case K_TABLE:
		draw_table(it, f);
		break;
	case K_NUMBER: {
		int st = f ? S_FOCUS : it->enabled ? S_FIELD : S_DIM;
		fill(it->x, it->y, it->w, st);
		int n = snprintf(buf, sizeof buf, "%d", it->value);
		put(it->x, it->y, "‹", 3, st);
		put_text(it->x + 2, it->y, buf, n, it->w - 4, st);
		put(it->x + it->w - 1, it->y, "›", 3, st);
		break;
	}
	case K_COLOR: {
		int st = f ? S_FOCUS : it->enabled ? S_BUTTON : S_DIM;
		double a;
		uint32_t c = color_of(it->text, &a);
		put(it->x, it->y, "[", 1, st);
		put_rgb(it->x + 1, it->y, "█", 3, c, c);
		put_rgb(it->x + 2, it->y, "█", 3, c, c);
		put(it->x + 3, it->y, " ", 1, st);
		int k = put_text(it->x + 4, it->y, it->text, len, it->w - 5, st);
		put(it->x + 4 + k, it->y, "]", 1, st);
		break;
	}
	case K_FONT: {
		int st = f ? S_FOCUS : it->enabled ? S_BUTTON : S_DIM;
		put(it->x, it->y, "[", 1, st);
		put(it->x + 1, it->y, " ", 1, st);
		int k = put_text(it->x + 2, it->y, it->text, len, it->w - 4, st);
		put(it->x + 2 + k, it->y, " ", 1, st);
		put(it->x + 3 + k, it->y, "]", 1, st);
		break;
	}
	case K_SLIDER: {
		int st = f ? S_FOCUS : dim;
		int n = snprintf(buf, sizeof buf, " %d", it->value);
		int track = it->w - 2 - n;
		if (track < 3)
			track = 3;
		int span = it->max - it->min;
		int at = span > 0 ? (it->value - it->min) * (track - 1) / span : 0;
		put(it->x, it->y, "[", 1, st);
		for (int i = 0; i < track; i++)
			put(it->x + 1 + i, it->y, i == at ? "●" : "─", 3, i == at ? st : dim);
		put(it->x + 1 + track, it->y, "]", 1, st);
		put_text(it->x + 2 + track, it->y, buf, n, n, dim);
		break;
	}
	case K_PROGRESS: {
		double fr = it->fraction < 0 ? 0 : it->fraction > 1 ? 1 : it->fraction;
		int n = snprintf(buf, sizeof buf, " %3d%%", (int)(fr * 100 + 0.5));
		int track = it->w - n;
		if (track < 3)
			track = 3;
		int done = (int)(fr * track + 0.5);
		for (int i = 0; i < track; i++)
			put(it->x + i, it->y, i < done ? "█" : "░", 3, i < done ? S_FILL : S_DIM);
		put_text(it->x + track, it->y, buf, n, n, S_NORMAL);
		break;
	}
	case K_SEPARATOR:
		for (int i = 0; i < it->w; i++)
			put(it->x + i, it->y, "─", 3, S_BORDER);
		break;
	case K_CANVAS:
		draw_canvas(it);
		if (f)
			put(it->x + it->w - 1, it->y, "◆", 3, S_FOCUS);   /* keys go here */
		break;
	case K_IMAGE: {
		const char *name = strrchr(it->text, '/');
		name = name ? name + 1 : it->text;
		int n = snprintf(buf, sizeof buf, "[picture: ");
		put_text(it->x, it->y, buf, n, it->w, S_DIM);
		int c = put_text(it->x + n, it->y, name, (int)strlen(name), it->w - n - 1, S_DIM);
		put(it->x + n + c, it->y, "]", 1, S_DIM);
		break;
	}
	}
}

static void draw_dialog(void)
{
	if (dialog.kind == D_NONE || (dialog.win != 0 && !window_alive(dialog.win)))
		return;
	const char *t = dialog.text ? dialog.text : "";
	int tw = label_width(t), th = line_count(t);
	bool files = dialog.kind == D_OPEN || dialog.kind == D_SAVE;
	int w = tw + 6, h = th + 4;
	if (dialog.kind == D_ASK && w < 20)
		w = 20;
	if (files) {
		w = scr_w - 8 < 60 ? scr_w - 8 : 60;
		h = scr_h - 4 < 18 ? scr_h - 4 : 18;
	}
	if (w > scr_w)
		w = scr_w;
	if (h > scr_h)
		h = scr_h;
	int x = (scr_w - w) / 2, y = (scr_h - h) / 2;
	for (int j = 0; j < h; j++)
		fill(x, y + j, w, S_NORMAL);
	const char *title = dialog.kind == D_OPEN ? "Open" : dialog.kind == D_SAVE ? "Save as" : NULL;
	frame(x, y, w, h, title, S_DIALOG);
	if (files) {
		Item *d = item(dialog.list), *e = item(dialog.entry);
		put_text(x + 2, y + 1, dialog.dir, (int)strlen(dialog.dir), w - 4, S_DIM);
		d->x = x + 2;
		d->y = y + 2;
		d->w = w - 4;
		d->h = h - 6;
		draw_list(d, dialog.on_list);
		put_text(x + 2, y + h - 3, "Name:", 5, 5, S_NORMAL);
		e->x = x + 8;
		e->y = y + h - 3;
		e->w = w - 10;
		e->h = 1;
		draw_entry(e, !dialog.on_list, S_FIELD);
		if (dialog.on_list)
			cursor_x = cursor_y = -1;
		const char *keys = "Enter: choose   Tab: list or name   Esc: cancel";
		put_text(x + 2, y + h - 2, keys, (int)strlen(keys), w - 4, S_DIM);
		return;
	}
	for (int k = 0; k < th; k++) {
		int s = line_start(t, k);
		put_text(x + 3, y + 1 + k, t + s, line_len(t, s), w - 6, S_NORMAL);
	}
	int by = y + h - 2;
	if (dialog.kind == D_MESSAGE) {
		put_text(x + (w - 6) / 2, by, "[ OK ]", 6, 6, S_FOCUS);
	} else {
		int bx = x + (w - 17) / 2;
		put_text(bx, by, "[ Yes ]", 7, 7, dialog.choice == 0 ? S_FOCUS : S_BUTTON);
		put_text(bx + 10, by, "[ No ]", 6, 6, dialog.choice == 1 ? S_FOCUS : S_BUTTON);
	}
	cursor_x = cursor_y = -1;
}

static int64_t showing;   /* the window on the screen */

static void draw(int64_t win)
{
	if (!window_alive(win))
		return;
	render_canvases(win);
	if (!window_alive(win))
		return;
	clear_screen();
	check_focus(win);
	layout(win);
	/* the thing with the keys, scrolled into view */
	if (item(win)->focus) {
		Item *f = item(item(win)->focus);
		bool moved = false;
		for (int64_t p = f->parent; p; p = item(p)->parent) {
			Item *b = item(p);
			if (b->kind != K_BOX || !b->scrolls)
				continue;
			if (f->y < b->y) {
				b->scroll -= b->y - f->y;
				moved = true;
			} else if (f->y + f->h > b->y + b->h) {
				b->scroll += f->y + f->h - (b->y + b->h);
				moved = true;
			}
		}
		if (moved)
			layout(win);
	}
	Item *w = item(win);
	int x, y, ww, wh;
	window_frame(w, &x, &y, &ww, &wh);
	frame(x, y, ww, wh, w->title, S_BORDER);
	draw_item(w->box, dialog.kind == D_NONE && open_menu.win != win ? w->focus : 0);
	draw_menus(win, x, y, ww);
	if (picker.target && item(picker.target)->win == win)
		draw_picker();
	if (dialog.kind != D_NONE)
		draw_dialog();
	flush_screen();
}

/* ------------------------------------------------------------- the terminal */

static struct termios saved_tio;
static int term_depth;            /* windows running */
static int kept_stdout = -1;      /* stdout, while it's kept aside */
static FILE *kept_output;         /* what the program printed meanwhile */
static volatile sig_atomic_t resized;

static void on_winch(int sig) { (void)sig; resized = 1; }

static void term_restore(void)
{
	if (term_depth <= 0 || script_mode)
		return;
	const char *off = "\x1b[?1006l\x1b[?1003l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?1049l";
	(void)!write(out_fd, off, strlen(off));
	/* a mouse button's release (or a key) still on its way belongs to the
	 * window, not to whatever reads the terminal next */
	struct pollfd p = { 0, POLLIN, 0 };
	while (poll(&p, 1, 30) > 0) {
		char junk[256];
		if (read(0, junk, sizeof junk) <= 0)
			break;
	}
	tcsetattr(0, TCSAFLUSH, &saved_tio);
	/* what the program printed while the window was open */
	fflush(stdout);
	if (kept_stdout >= 0) {
		dup2(kept_stdout, 1);
		close(kept_stdout);
		kept_stdout = -1;
		if (kept_output) {
			rewind(kept_output);
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, kept_output)) > 0)
				(void)!write(1, buf, n);
			fclose(kept_output);
			kept_output = NULL;
		}
	}
	out_fd = 1;
	term_depth = 0;
}

static void term_setup(void)
{
	if (script_mode)
		return;
	if (!isatty(0) || !isatty(1)) {
		fprintf(stderr, "tui: a tui window needs a terminal (standard input and output can't be a pipe or a file)\n");
		exit(101);
	}
	static bool once;
	if (!once) {
		once = true;
		atexit(term_restore);
		struct sigaction sa;
		memset(&sa, 0, sizeof sa);
		sa.sa_handler = on_winch;
		sigaction(SIGWINCH, &sa, NULL);
	}
	/* a panic gives the terminal back before it says what happened */
	if (&rt_before_panic)
		rt_before_panic = term_restore;
	tcgetattr(0, &saved_tio);
	struct termios raw = saved_tio;
	raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cflag |= CS8;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &raw);
	/* keep what the program prints, to show after the window closes */
	fflush(stdout);
	out_fd = dup(1);
	kept_output = tmpfile();
	if (kept_output) {
		kept_stdout = dup(1);
		dup2(fileno(kept_output), 1);
	}
	const char *on = "\x1b[?1049h\x1b[?1000h\x1b[?1006h\x1b[?25l";
	(void)!write(out_fd, on, strlen(on));
	/* every movement of the mouse, if a canvas wants to know */
	for (int i = 0; i < nitems; i++)
		if (items[i].kind == K_CANVAS && items[i].on_move.fn) {
			(void)!write(out_fd, "\x1b[?1003h", 8);
			break;
		}
	term_depth = 1;
}

/* ------------------------------------------------------------- timers */

typedef struct Timer {
	int64_t win;
	long long due;     /* in ms */
	int every;         /* ms; 0: once */
	Handler h;
	bool live;
} Timer;

static Timer *timers;
static int ntimers, captimers;
static long long fake_now;   /* script mode's clock */

static long long now_ms(void)
{
	if (script_mode)
		return fake_now;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void add_timer(int64_t win, int ms, int every, void *obj, void *fn)
{
	if (ntimers == captimers) {
		captimers = captimers ? captimers * 2 : 8;
		timers = realloc(timers, sizeof *timers * (size_t)captimers);
	}
	if (ms < 1)
		ms = 1;
	timers[ntimers++] = (Timer){ win, now_ms() + ms, every ? ms : 0, { obj, fn, win }, true };
}

/* Runs the timers that are due; gives back ms until the next one (-1: none) */
static int run_timers(int64_t win)
{
	long long now = now_ms();
	for (int i = 0; i < ntimers; i++) {
		Timer *t = &timers[i];
		if (!t->live || !window_alive(t->win) || !item(t->win)->open || t->due > now)
			continue;
		Handler h = t->h;
		if (t->every)
			t->due += t->every > 0 ? t->every : 1;
		else
			t->live = false;
		if (t->every && t->due <= now)
			t->due = now + t->every;   /* behind: catch up, not burst */
		invoke(h);
		now = now_ms();
	}
	long long next = -1;
	for (int i = 0; i < ntimers; i++)
		if (timers[i].live && window_alive(timers[i].win) && timers[i].win == win)
			if (next < 0 || timers[i].due < next)
				next = timers[i].due;
	/* tidy finished ones */
	int k = 0;
	for (int i = 0; i < ntimers; i++)
		if (timers[i].live && window_alive(timers[i].win))
			timers[k++] = timers[i];
	ntimers = k;
	return next < 0 ? -1 : next - now < 0 ? 0 : (int)(next - now);
}

/* ------------------------------------------------------------- keys */


static int mouse_x, mouse_y, mouse_b;

static void changed(Item *it, Handler h) { (void)it; invoke(h); }

/* A printable key typed into an entry or text area */
static void type_text(int64_t id, const char *s, int n)
{
	Item *it = item(id);
	if (!it->editable || !it->enabled)
		return;
	text_insert(it, s, n);
	changed(it, it->on_change);
}

static void select_option(Item *it, int i, bool list)
{
	if (it->nopts == 0)
		return;
	if (i < 0)
		i = list ? 0 : it->nopts - 1;
	if (i >= it->nopts)
		i = list ? it->nopts - 1 : 0;
	if (i == it->selected)
		return;
	it->selected = i;
	changed(it, list ? it->on_select : it->on_change);
}

static void set_slider(Item *it, int v)
{
	if (v < it->min)
		v = it->min;
	if (v > it->max)
		v = it->max;
	if (v != it->value) {
		it->value = v;
		changed(it, it->on_change);
	}
}

static void press(int64_t id)
{
	Item *it = item(id);
	if (!it->enabled)
		return;
	if (it->kind == K_BUTTON)
		invoke(it->on_click);
	else if (it->kind == K_CHECK) {
		it->value = !it->value;
		invoke(it->on_toggle);
	}
}

/* A key to the thing with the focus; false if it didn't use it */
static bool key_to(int64_t id, int key, const char *ch, int chn)
{
	Item *it = item(id);
	int len = (int)strlen(it->text);
	switch (it->kind) {
	case K_NUMBER: {
		if (key == KEY_LEFT) set_slider(it, it->value - 1);
		else if (key == KEY_RIGHT) set_slider(it, it->value + 1);
		else if (key == KEY_PGDN) set_slider(it, it->value - 10);
		else if (key == KEY_PGUP) set_slider(it, it->value + 10);
		else if (key == KEY_HOME) set_slider(it, it->min);
		else if (key == KEY_END) set_slider(it, it->max);
		else if (key == KEY_BACKSPACE) set_slider(it, it->value / 10);
		else if (key == 0 && chn == 1 && ((ch[0] >= '0' && ch[0] <= '9') || ch[0] == '-')) {
			/* typed: the digit on the end */
			long v = ch[0] == '-' ? -it->value : (long)it->value * 10 + (it->value < 0 ? -(ch[0] - '0') : ch[0] - '0');
			if (v > it->max && ch[0] != '-')
				v = ch[0] - '0';
			set_slider(it, (int)(v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : v));
		} else
			return false;
		return true;
	}
	case K_COLOR: case K_FONT:
		if (key == KEY_ENTER || (key == 0 && chn == 1 && ch[0] == ' ')) {
			if (it->enabled)
				open_picker(id);
			return true;
		}
		return false;
	case K_TABLE:
		return table_key(it, key);
	case K_TABS:
		if (key == KEY_LEFT || key == KEY_RIGHT) {
			int i = it->selected + (key == KEY_LEFT ? -1 : 1);
			if (i >= 0 && i < it->nkids && i != it->selected) {
				it->selected = i;
				invoke(it->on_change);
			}
			return true;
		}
		return false;
	case K_CANVAS: {
		static const struct { int key; const char *name; } names[] = {
			{ KEY_ENTER, "enter" }, { KEY_UP, "up" }, { KEY_DOWN, "down" }, { KEY_LEFT, "left" },
			{ KEY_RIGHT, "right" }, { KEY_HOME, "home" }, { KEY_END, "end" }, { KEY_PGUP, "page_up" },
			{ KEY_PGDN, "page_down" }, { KEY_BACKSPACE, "backspace" }, { KEY_DELETE, "delete" },
		};
		if (!it->on_key.fn)
			return false;
		if (key == 0 && chn > 0) {
			char name[8];
			memcpy(name, ch, (size_t)chn);
			name[chn] = '\0';
			canvas_key(it, name);
			return true;
		}
		for (size_t i = 0; i < sizeof names / sizeof *names; i++)
			if (names[i].key == key) {
				canvas_key(it, names[i].name);
				return true;
			}
		return false;   /* Tab still moves between things */
	}
	case K_BUTTON: case K_CHECK:
		if (key == KEY_ENTER || (key == 0 && chn == 1 && ch[0] == ' ')) {
			press(id);
			return true;
		}
		return false;
	case K_CHOICE:
		if (key == KEY_LEFT)
			select_option(it, it->selected - 1, false);
		else if (key == KEY_RIGHT || key == KEY_ENTER || (key == 0 && chn == 1 && ch[0] == ' '))
			select_option(it, it->selected + 1, false);
		else
			return false;
		return true;
	case K_SLIDER: {
		int step = (it->max - it->min) / 10 > 1 ? (it->max - it->min) / 10 : 1;
		if (key == KEY_LEFT) set_slider(it, it->value - 1);
		else if (key == KEY_RIGHT) set_slider(it, it->value + 1);
		else if (key == KEY_PGDN) set_slider(it, it->value - step);
		else if (key == KEY_PGUP) set_slider(it, it->value + step);
		else if (key == KEY_HOME) set_slider(it, it->min);
		else if (key == KEY_END) set_slider(it, it->max);
		else return false;
		return true;
	}
	case K_LIST:
		if (key == KEY_UP) select_option(it, it->selected < 0 ? 0 : it->selected - 1, true);
		else if (key == KEY_DOWN) select_option(it, it->selected + 1, true);
		else if (key == KEY_PGUP) select_option(it, it->selected - it->h, true);
		else if (key == KEY_PGDN) select_option(it, it->selected + it->h, true);
		else if (key == KEY_HOME) select_option(it, 0, true);
		else if (key == KEY_END) select_option(it, it->nopts - 1, true);
		else return false;
		return true;
	case K_ENTRY: case K_TEXT_AREA: {
		bool area = it->kind == K_TEXT_AREA;
		if (key == 0 && chn > 0) {
			type_text(id, ch, chn);
		} else if (key == KEY_LEFT) {
			it->cur = prev_char(it->text, it->cur);
		} else if (key == KEY_RIGHT) {
			it->cur = next_char(it->text, it->cur);
		} else if ((key == KEY_HOME || key == KEY_END) && area) {
			/* the start or end of the row on the screen */
			int *rows;
			int n = wrap_rows(it->text, it->w - 1, &rows), r = row_of(rows, n, it->cur);
			int e = rows[2 * r] + rows[2 * r + 1];
			if (key == KEY_END && r + 1 < n && rows[2 * (r + 1)] == e && e > rows[2 * r])
				e = prev_char(it->text, e);   /* a wrapped row: stay on it */
			it->cur = key == KEY_HOME ? rows[2 * r] : e;
			free(rows);
		} else if (key == KEY_HOME) {
			it->cur = 0;
		} else if (key == KEY_END) {
			it->cur = len;
		} else if (key == KEY_BACKSPACE) {
			if (it->cur > 0 && it->editable) {
				text_delete(it, prev_char(it->text, it->cur), it->cur);
				changed(it, it->on_change);
			}
		} else if (key == KEY_DELETE) {
			if (it->cur < len && it->editable) {
				text_delete(it, it->cur, next_char(it->text, it->cur));
				changed(it, it->on_change);
			}
		} else if (key == KEY_ENTER && area) {
			type_text(id, "\n", 1);
		} else if (key == KEY_ENTER) {
			invoke(it->on_enter);
		} else if (area && (key == KEY_UP || key == KEY_DOWN)) {
			int *rows;
			int n = wrap_rows(it->text, it->w - 1, &rows), l = row_of(rows, n, it->cur);
			int col = text_cols(it->text + rows[2 * l], it->cur - rows[2 * l]);
			int nl = l + (key == KEY_UP ? -1 : 1);
			if (nl < 0 || nl >= n) {
				free(rows);
				return false;   /* off the top or bottom: on to the next thing */
			}
			it->cur = col_in_row(it->text, rows, n, nl, col);
			free(rows);
		} else {
			return false;
		}
		return true;
	}
	default:
		return false;
	}
}

static int64_t item_at(int64_t id, int x, int y)
{
	Item *it = item(id);
	if (!it->visible || x < it->x || y < it->y || x >= it->x + it->w || y >= it->y + it->h)
		return 0;
	if (it->kind == K_TABS) {
		if (y == it->y)
			return id;
		return it->selected >= 0 && it->selected < it->nkids ? item_at(it->kids[it->selected], x, y) : 0;
	}
	if (it->kind == K_BOX) {
		for (int i = 0; i < it->nkids; i++) {
			int64_t f = item_at(it->kids[i], x, y);
			if (f)
				return f;
		}
		return 0;
	}
	return id;
}

static void click(int64_t win, int x, int y)
{
	int64_t id = item_at(item(win)->box, x, y);
	if (id && item(id)->kind == K_CANVAS && item(id)->enabled) {
		Item *c = item(id);
		if (focusable(c))
			item(win)->focus = id;
		canvas_mouse(c, x, y);
		invoke(c->on_click);
		return;
	}
	if (!id || !focusable(item(id)))
		return;
	Item *it = item(id);
	item(win)->focus = id;
	switch (it->kind) {
	case K_NUMBER:
		set_slider(it, it->value + (x < it->x + it->w / 2 ? -1 : 1));
		break;
	case K_COLOR: case K_FONT:
		open_picker(id);
		break;
	case K_TABLE:
		table_click(it, x, y);
		break;
	case K_TABS: {
		int at = it->x;
		for (int i = 0; i < it->nopts; i++) {
			int c = text_cols(it->opts[i], (int)strlen(it->opts[i]));
			if (x >= at && x < at + c + 2) {
				if (i != it->selected && i < it->nkids) {
					it->selected = i;
					invoke(it->on_change);
				}
				break;
			}
			at += c + 3;
		}
		break;
	}
	case K_BUTTON: case K_CHECK:
		press(id);
		break;
	case K_CHOICE:
		select_option(it, it->selected + (x < it->x + it->w / 2 ? -1 : 1), false);
		break;
	case K_LIST:
		select_option(it, it->scroll + (y - it->y), true);
		break;
	case K_SLIDER: {
		int track = it->w - 2 - snprintf(NULL, 0, " %d", it->value);
		if (track > 1 && x > it->x)
			set_slider(it, it->min + (x - it->x - 1) * (it->max - it->min) / (track - 1));
		break;
	}
	case K_TEXT_AREA: {
		int *rows;
		int n = wrap_rows(it->text, it->w - 1, &rows), r = it->scroll + (y - it->y);
		it->cur = col_in_row(it->text, rows, n, r < n ? r : n - 1, x - it->x);
		free(rows);
		break;
	}
	case K_ENTRY: {
		int len = (int)strlen(it->text), i = 0;
		for (int c = 0; c < x - it->x + it->scroll && i < len; c++)
			i = next_char(it->text, i);
		it->cur = i;
		break;
	}
	}
}

static int read_byte_timeout(int ms)
{
	struct pollfd p = { 0, POLLIN, 0 };
	int r = poll(&p, 1, ms);
	if (r <= 0)
		return -1;
	unsigned char c;
	return read(0, &c, 1) == 1 ? c : -1;
}

/* Reads one key (or -2 when `ms` passed with none, -3 on a resize) */
/* Cabby's event loop (in its runtime): what it watches (processes' output)
 * is watched alongside the keyboard, and its timers and handlers run between
 * keys. */
extern int cby_events_fds(int *fds, int max) __attribute__((weak));
extern int cby_events_next_ms(void) __attribute__((weak));
extern void cby_events_step(int wait_ms) __attribute__((weak));

static int (*ev_fds)(int *, int);
static int (*ev_next)(void);
static void (*ev_step)(int);

static void find_events(void)
{
	static bool looked;
	if (looked)
		return;
	looked = true;
	ev_fds = cby_events_fds;
	ev_next = cby_events_next_ms;
	ev_step = cby_events_step;
	if (!ev_fds || !ev_next || !ev_step) {
		/* in the interpreter: the cabby program itself has them */
		ev_fds = (int (*)(int *, int))dlsym(RTLD_DEFAULT, "cby_events_fds");
		ev_next = (int (*)(void))dlsym(RTLD_DEFAULT, "cby_events_next_ms");
		ev_step = (void (*)(int))dlsym(RTLD_DEFAULT, "cby_events_step");
		if (!ev_fds || !ev_next || !ev_step)
			ev_fds = NULL, ev_next = NULL, ev_step = NULL;
	}
}

static int read_key(int ms, char *ch, int *chn)
{
	*chn = 0;
	find_events();
	struct pollfd p[65] = { { 0, POLLIN, 0 } };
	int np = 1;
	if (ev_fds) {
		int fds[64];
		int n = ev_fds(fds, 64);
		for (int i = 0; i < n; i++)
			p[np++] = (struct pollfd){ fds[i], POLLIN, 0 };
		int next = ev_next();   /* the event loop's next timer */
		if (next >= 0 && (ms < 0 || next < ms))
			ms = next;
	}
	int r = poll(p, (nfds_t)np, ms);
	if (r < 0 && errno == EINTR)
		return -3;
	if (ev_step && !(r > 0 && (p[0].revents & POLLIN))) {
		ev_step(0);   /* a process printed, or a timer is due: its handlers */
		return -2;
	}
	if (r <= 0)
		return -2;
	unsigned char c;
	if (read(0, &c, 1) != 1)
		return KEY_QUIT;
	switch (c) {
	case 9:   return KEY_TAB;
	case 13: case 10: return KEY_ENTER;
	case 127: case 8: return KEY_BACKSPACE;
	case 3:   return KEY_QUIT;   /* Ctrl-C */
	case 27: {
		int a = read_byte_timeout(30);
		if (a < 0)
			return KEY_ESC;
		if (a != '[' && a != 'O')
			return a > ' ' && a < 127 ? KEY_ALT + (a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) : KEY_ESC;
		int b = read_byte_timeout(30);
		if (a == 'O' && b >= 'P' && b <= 'S')
			return KEY_F1 + (b - 'P');   /* F1 to F4 */
		if (b == '<') {
			/* a mouse report: ESC [ < button ; x ; y M (press) or m (release) */
			char buf[32];
			int n = 0, k;
			while (n < 30 && (k = read_byte_timeout(30)) >= 0 && k != 'M' && k != 'm')
				buf[n++] = (char)k;
			buf[n] = '\0';
			if (k == 'M' && sscanf(buf, "%d;%d;%d", &mouse_b, &mouse_x, &mouse_y) == 3)
				return KEY_MOUSE;
			return KEY_NONE;
		}
		switch (b) {
		case 'A': return KEY_UP;
		case 'B': return KEY_DOWN;
		case 'C': return KEY_RIGHT;
		case 'D': return KEY_LEFT;
		case 'H': return KEY_HOME;
		case 'F': return KEY_END;
		case 'Z': return KEY_BACKTAB;
		}
		int num = 0;
		while (b >= '0' && b <= '9') {
			num = num * 10 + (b - '0');
			b = read_byte_timeout(30);
		}
		while (b == ';' || (b >= '0' && b <= '9'))
			b = read_byte_timeout(30);
		if (b != '~')
			return KEY_NONE;
		switch (num) {
		case 1: case 7: return KEY_HOME;
		case 4: case 8: return KEY_END;
		case 3: return KEY_DELETE;
		case 5: return KEY_PGUP;
		case 6: return KEY_PGDN;
		case 11: case 12: case 13: case 14: case 15: return KEY_F1 + (num - 11);
		case 17: case 18: case 19: case 20: case 21: return KEY_F1 + 5 + (num - 17);
		case 23: case 24: return KEY_F1 + 10 + (num - 23);
		}
		return KEY_NONE;
	}
	}
	if (c >= 1 && c <= 26)
		return KEY_CTRL + 'a' + (c - 1);   /* Ctrl and a letter (Ctrl-Q closes, unless it's a shortcut) */
	if (c < 32)
		return KEY_NONE;
	ch[0] = (char)c;
	*chn = 1;
	int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	for (int i = 0; i < more; i++) {
		int k = read_byte_timeout(30);
		if (k < 0)
			break;
		ch[(*chn)++] = (char)k;
	}
	return 0;
}

/* A key while a dialog is open */
static void dialog_key(int key, const char *ch, int chn)
{
	if (dialog.kind == D_MESSAGE) {
		if (key == KEY_ENTER || key == KEY_ESC || (key == 0 && chn == 1 && ch[0] == ' '))
			dialog_finish(false);
		return;
	}
	if (dialog.kind == D_ASK) {
		if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB || key == KEY_BACKTAB)
			dialog.choice = !dialog.choice;
		else if (key == KEY_ENTER || (key == 0 && chn == 1 && ch[0] == ' '))
			dialog_finish(dialog.choice == 0);
		else if (key == KEY_ESC)
			dialog_finish(false);
		else if (key == 0 && chn == 1 && (ch[0] == 'y' || ch[0] == 'Y'))
			dialog_finish(true);
		else if (key == 0 && chn == 1 && (ch[0] == 'n' || ch[0] == 'N'))
			dialog_finish(false);
		return;
	}
	/* a file dialog */
	if (key == KEY_ESC) {
		dialog_finish(false);
		return;
	}
	if (key == KEY_TAB || key == KEY_BACKTAB) {
		dialog.on_list = !dialog.on_list;
		return;
	}
	Item *l = item(dialog.list), *e = item(dialog.entry);
	if (dialog.on_list && key == KEY_ENTER) {
		if (l->selected < 0)
			return;
		const char *name = l->opts[l->selected];
		size_t n = strlen(name);
		if (strcmp(name, "../") == 0 || (n > 0 && name[n - 1] == '/')) {
			char path[PATH_MAX * 2];
			if (strcmp(name, "../") == 0) {
				snprintf(path, sizeof path, "%s", dialog.dir);
				char *s = strrchr(path, '/');
				if (s && s != path)
					*s = '\0';
				else
					strcpy(path, "/");
			} else {
				snprintf(path, sizeof path, "%s%s%.*s", dialog.dir, strcmp(dialog.dir, "/") ? "/" : "",
					 (int)n - 1, name);
			}
			free(dialog.dir);
			dialog.dir = dupstr(path);
			dialog_fill_list();
			return;
		}
		set_chosen(dialog.win, name);
		dialog_finish(true);
		return;
	}
	if (dialog.on_list) {
		if (key_to(dialog.list, key, ch, chn) && l->selected >= 0) {
			const char *name = l->opts[l->selected];
			size_t n = strlen(name);
			if (n > 0 && name[n - 1] != '/') {   /* a file: its name goes in the box */
				free(e->text);
				e->text = dupstr(name);
				e->cur = (int)n;
			}
		}
		return;
	}
	if (key == KEY_ENTER) {
		if (!*e->text)
			return;
		set_chosen(dialog.win, e->text);
		dialog_finish(true);
		return;
	}
	key_to(dialog.entry, key, ch, chn);
}

/* ------------------------------------------------------------- scripts */

static Item *nth(int kind, int n)
{
	int k = 0;
	for (int i = 0; i < nitems; i++)
		if (items[i].kind == kind && items[i].win && window_alive(items[i].win) && ++k == n)
			return &items[i];
	return NULL;
}

static int number_and_text(const char *s, const char **text)
{
	int n = atoi(s);
	while (*s && *s != ' ')
		s++;
	*text = *s ? s + 1 : s;
	return n;
}

static int key_named(const char *s, char *ch, int *chn)
{
	static const struct { const char *name; int key; } names[] = {
		{ "tab", KEY_TAB }, { "backtab", KEY_BACKTAB }, { "enter", KEY_ENTER }, { "esc", KEY_ESC },
		{ "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT },
		{ "home", KEY_HOME }, { "end", KEY_END }, { "pageup", KEY_PGUP }, { "pagedown", KEY_PGDN },
		{ "backspace", KEY_BACKSPACE }, { "delete", KEY_DELETE }, { "quit", KEY_QUIT },
		{ "escape", KEY_ESC }, { "page_up", KEY_PGUP }, { "page_down", KEY_PGDN },
		{ "f1", KEY_F1 }, { "f2", KEY_F1 + 1 }, { "f3", KEY_F1 + 2 }, { "f4", KEY_F1 + 3 }, { "f5", KEY_F1 + 4 },
		{ "f6", KEY_F1 + 5 }, { "f7", KEY_F1 + 6 }, { "f8", KEY_F1 + 7 }, { "f9", KEY_F1 + 8 },
		{ "f10", KEY_F1 + 9 }, { "f11", KEY_F1 + 10 }, { "f12", KEY_F1 + 11 },
	};
	*chn = 0;
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (strcmp(s, names[i].name) == 0)
			return names[i].key;
	if (strncmp(s, "ctrl+", 5) == 0 && s[5] && !s[6])
		return KEY_CTRL + (s[5] >= 'A' && s[5] <= 'Z' ? s[5] - 'A' + 'a' : s[5]);
	if (strncmp(s, "alt+", 4) == 0 && s[4] && !s[5])
		return KEY_ALT + (s[4] >= 'A' && s[4] <= 'Z' ? s[4] - 'A' + 'a' : s[4]);
	if (strcmp(s, "space") == 0) {
		ch[0] = ' ';
		*chn = 1;
		return 0;
	}
	size_t n = strlen(s);
	if (n > 0 && n <= 4) {
		memcpy(ch, s, n);
		*chn = (int)n;
		return 0;
	}
	return KEY_NONE;
}

static void handle_key(int64_t win, int key, const char *ch, int chn);
void cby_tui_select(int64_t id, int index);

/* One command of CABBY_TUI_SCRIPT; false when the script is over */
static bool run_script_step(int64_t win)
{
	if (!script_left || !*script_left)
		return false;
	char *cmd = script_left;
	char *semi = strchr(cmd, ';');
	if (semi) {
		*semi = '\0';
		script_left = semi + 1;
	} else {
		script_left = NULL;
	}
	while (*cmd == ' ')
		cmd++;
	size_t cl = strlen(cmd);
	while (cl > 0 && cmd[cl - 1] == ' ')
		cmd[--cl] = '\0';
	const char *text;
	char ch[8];
	int chn;
	if (strcmp(cmd, "processes") == 0) {
		/* lets the processes being watched finish, handing on what they print */
		find_events();
		int fds[64];
		while (ev_fds && ev_fds(fds, 64) > 0)
			ev_step(1000);
	} else if (strcmp(cmd, "screen") == 0) {
		draw(win);
		print_screen();
	} else if (strncmp(cmd, "click ", 6) == 0) {
		bool found = false;
		for (int i = 0; i < nitems && !found; i++)
			if (items[i].kind == K_BUTTON && items[i].win == win && strcmp(items[i].text, cmd + 6) == 0) {
				found = true;
				item(win)->focus = i + 1;
				press(i + 1);
			}
		if (!found)
			fprintf(stderr, "tui script: no button '%s'\n", cmd + 6);
	} else if (strncmp(cmd, "check ", 6) == 0) {
		bool found = false;
		for (int i = 0; i < nitems && !found; i++)
			if (items[i].kind == K_CHECK && items[i].win == win && strcmp(items[i].text, cmd + 6) == 0) {
				found = true;
				press(i + 1);
			}
		if (!found)
			fprintf(stderr, "tui script: no check box '%s'\n", cmd + 6);
	} else if (strncmp(cmd, "type ", 5) == 0) {
		Item *e = nth(K_ENTRY, number_and_text(cmd + 5, &text));
		if (e) {
			int64_t id = (int64_t)(e - items) + 1;
			free(e->text);
			e->text = dupstr("");
			e->cur = 0;
			for (const char *c = text; *c;) {
				int k = next_char(c, 0);
				type_text(id, c, k);
				c += k;
				if (!window_alive(win))
					break;
			}
		} else {
			fprintf(stderr, "tui script: no text box %s\n", cmd + 5);
		}
	} else if (strncmp(cmd, "enter ", 6) == 0) {
		Item *e = nth(K_ENTRY, atoi(cmd + 6));
		if (e)
			invoke(e->on_enter);
	} else if (strncmp(cmd, "write ", 6) == 0) {
		Item *a = nth(K_TEXT_AREA, number_and_text(cmd + 6, &text));
		if (a) {
			char *t = dupstr(text), *o = t;
			for (const char *c = text; *c; c++) {
				if (c[0] == '\\' && c[1] == 'n') {
					*o++ = '\n';
					c++;
				} else {
					*o++ = *c;
				}
			}
			*o = '\0';
			free(a->text);
			a->text = t;
			a->cur = (int)strlen(t);
			invoke(a->on_change);
		}
	} else if (strncmp(cmd, "choose ", 7) == 0 || strncmp(cmd, "select ", 7) == 0) {
		bool list = cmd[0] == 's';
		Item *it = nth(list ? K_LIST : K_CHOICE, number_and_text(cmd + 7, &text));
		int found = -1;
		for (int i = 0; it && i < it->nopts; i++)
			if (strcmp(it->opts[i], text) == 0)
				found = i;
		if (found >= 0)
			select_option(it, found, list);
		else
			fprintf(stderr, "tui script: no %s entry '%s'\n", list ? "list" : "drop-down", text);
	} else if (strncmp(cmd, "number ", 7) == 0) {
		Item *n = nth(K_NUMBER, number_and_text(cmd + 7, &text));
		if (n)
			set_slider(n, atoi(text));
		else
			fprintf(stderr, "tui script: no number box %s\n", cmd + 7);
	} else if (strncmp(cmd, "color ", 6) == 0 || strncmp(cmd, "font ", 5) == 0) {
		bool color = cmd[0] == 'c';
		Item *b = nth(color ? K_COLOR : K_FONT, number_and_text(cmd + (color ? 6 : 5), &text));
		if (b && color)
			cby_tui_color_set((int64_t)(b - items) + 1, text);
		else if (b)
			cby_tui_font_set((int64_t)(b - items) + 1, text);
		else
			fprintf(stderr, "tui script: no %s button for '%s'\n", color ? "color" : "font", cmd);
	} else if (strncmp(cmd, "row ", 4) == 0) {
		Item *t = nth(K_TABLE, number_and_text(cmd + 4, &text));
		int found = -1;
		for (int r = 0; t && r < t->ntrows && found < 0; r++)
			if (!strcmp(tcell(t, r, 0), text))
				found = r;
		if (found >= 0)
			table_pick(t, found);
		else
			fprintf(stderr, "tui script: no row '%s'\n", text);
	} else if (strncmp(cmd, "sort ", 5) == 0) {
		Item *t = nth(K_TABLE, number_and_text(cmd + 5, &text));
		int found = -1;
		for (int c = 0; t && c < t->nopts; c++)
			if (!strcmp(t->opts[c], text))
				found = c;
		if (found >= 0)
			table_sort(t, found, false);
		else
			fprintf(stderr, "tui script: no heading '%s'\n", text);
	} else if (strncmp(cmd, "tab ", 4) == 0) {
		Item *t = nth(K_TABS, number_and_text(cmd + 4, &text));
		int found = -1;
		for (int i = 0; t && i < t->nopts; i++)
			if (strcmp(t->opts[i], text) == 0)
				found = i;
		if (found >= 0)
			cby_tui_select((int64_t)(t - items) + 1, found);
		else
			fprintf(stderr, "tui script: no tab '%s'\n", text);
	} else if (strncmp(cmd, "slide ", 6) == 0) {
		Item *s = nth(K_SLIDER, number_and_text(cmd + 6, &text));
		if (s)
			set_slider(s, atoi(text));
	} else if (strncmp(cmd, "menu ", 5) == 0) {
		/* menu <Menu> > <Item>: chooses that item */
		const char *sep = strstr(cmd + 5, " > ");
		bool found = false;
		Item *w = item(win);
		for (int k = 0; sep && k < menu_count(w) && !found; k++) {
			Item *m = nth_menu(w, k);
			if (strlen(m->text) != (size_t)(sep - (cmd + 5)) || strncmp(m->text, cmd + 5, (size_t)(sep - (cmd + 5))))
				continue;
			for (int i = 0; i < m->nmenus && !found; i++)
				if (!item(m->menus[i])->separator && !strcmp(item(m->menus[i])->text, sep + 3)) {
					found = true;
					if (!item(m->menus[i])->enabled)
						fprintf(stderr, "tui script: '%s' is grayed out\n", sep + 3);
					run_item(m->menus[i]);
				}
		}
		if (!found)
			fprintf(stderr, "tui script: no menu item '%s'\n", cmd + 5);
	} else if (strncmp(cmd, "shortcut ", 9) == 0) {
		char norm[64];
		int64_t sc = parse_keys(cmd + 9, norm, sizeof norm) ? shortcut_item(win, norm) : 0;
		if (sc)
			run_item(sc);
		else
			fprintf(stderr, "tui script: no shortcut '%s'\n", cmd + 9);
	} else if (strncmp(cmd, "press ", 6) == 0 || strncmp(cmd, "point ", 6) == 0) {
		/* press <n> <x> <y>: a click on the n-th canvas, at that pixel; point: the mouse moves there */
		int n = 0;
		double x = 0, y = 0;
		Item *c = sscanf(cmd + 6, "%d %lf %lf", &n, &x, &y) == 3 ? nth(K_CANVAS, n) : NULL;
		if (c) {
			c->mouse_x = (int)floor(x);
			c->mouse_y = (int)floor(y);
			invoke(cmd[1] == 'r' ? c->on_click : c->on_move);
		} else {
			fprintf(stderr, "tui script: no canvas for '%s'\n", cmd);
		}
	} else if (strncmp(cmd, "key ", 4) == 0) {
		/* to the first canvas that takes keys, as in gui; or to what has the focus */
		Item *c = NULL;
		for (int i = 0; i < nitems && !c; i++)
			if (items[i].kind == K_CANVAS && items[i].win == win && items[i].on_key.fn)
				c = &items[i];
		if (c)
			canvas_key(c, strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
		else {
			int key = key_named(cmd + 4, ch, &chn);
			handle_key(win, key, ch, chn);
		}
	} else if (strncmp(cmd, "wait ", 5) == 0) {
		long long until = fake_now + atoi(cmd + 5);
		for (;;) {
			int next = run_timers(win);
			if (!window_alive(win) || !item(win)->open || next < 0 || fake_now + next > until)
				break;
			fake_now += next;
		}
		fake_now = until;
		run_timers(win);
	} else if (strncmp(cmd, "answer ", 7) == 0) {
		if (dialog.kind == D_NONE)
			fprintf(stderr, "tui script: 'answer' but no dialog is open\n");
		else
			dialog_finish(strcmp(cmd + 7, "yes") == 0);
	} else if (strncmp(cmd, "pick ", 5) == 0) {
		if (dialog.kind != D_OPEN && dialog.kind != D_SAVE) {
			fprintf(stderr, "tui script: 'pick' but no file dialog is open\n");
		} else {
			set_chosen(dialog.win, cmd + 5);
			dialog_finish(true);
		}
	} else if (strcmp(cmd, "cancel") == 0) {
		dialog_finish(false);
	} else if (strncmp(cmd, "cell ", 5) == 0) {
		/* what a character on the screen is, and its colors */
		int cx, cy;
		draw(win);
		if (sscanf(cmd + 5, "%d %d", &cx, &cy) == 2 && cx >= 0 && cy >= 0 && cx < scr_w && cy < scr_h) {
			Cell *c = &screen[cy * scr_w + cx];
			printf("cell %d %d: '%s'", cx, cy, c->ch);
			if (c->rgb || c->own_fg)
				printf(" text #%06x", c->fg);
			if (c->rgb || c->own_bg)
				printf(" background #%06x", c->bg);
			if (!c->rgb && !c->own_fg && !c->own_bg)
				printf(" (the usual colors)");
			printf("\n");
		}
	} else if (strcmp(cmd, "close") == 0) {
		item(win)->open = false;
	} else if (*cmd) {
		fprintf(stderr, "tui script: unknown command '%s'\n", cmd);
	}
	return true;
}

/* ------------------------------------------------------------- running */

static void handle_key(int64_t win, int key, const char *ch, int chn)
{
	Item *w = item(win);
	if (key == KEY_QUIT) {
		w->open = false;
		return;
	}
	if (dialog.kind != D_NONE) {
		dialog_key(key, ch, chn);
		return;
	}
	if (open_menu.win == win && key != KEY_MOUSE) {
		menu_key(key, ch, chn);
		return;
	}
	if (picker.target) {
		if (key == KEY_MOUSE) {
			/* a click on the list chooses; anywhere else, closes it */
			if (mouse_b == 0) {
				int row = mouse_y - 1 - (picker.y + 1);
				if (row >= 0 && row < picker.h - 2 && picker.scroll + row < picker.n &&
				    mouse_x - 1 > picker.x && mouse_x - 1 < picker.x + 23) {
					picker.sel = picker.scroll + row;
					picker_key(KEY_ENTER, ch, 0);
				} else {
					picker.target = 0;
				}
			}
		} else {
			picker_key(key, ch, chn);
		}
		return;
	}
	if (key == KEY_MOUSE && mouse_b == 0 && menu_click(win, mouse_x - 1, mouse_y - 1))
		return;
	/* shortcuts: Ctrl, Alt and the F keys before anything else; other keys
	 * after what has the focus has had the chance to use them */
	char name[16];
	key_shortcut_name(key, ch, chn, name, sizeof name);
	bool special = key >= KEY_F1;
	int64_t sc = name[0] ? shortcut_item(win, name) : 0;
	if (sc && special) {
		run_item(sc);
		return;
	}
	if (key == KEY_F1 + 9 && menu_count(w)) {   /* F10: the menus */
		open_menu_at(win, 0);
		return;
	}
	if (key >= KEY_ALT && key < KEY_ALT + 128 && menu_count(w)) {
		/* Alt and a menu's first letter */
		for (int k = 0; k < menu_count(w); k++) {
			char c = nth_menu(w, k)->text[0];
			if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) == key - KEY_ALT) {
				open_menu_at(win, k);
				return;
			}
		}
	}
	if (key == KEY_CTRL + 'q' || (key == KEY_ESC && !sc)) {
		w->open = false;   /* Ctrl-Q or Esc closes the window, unless it's a shortcut */
		return;
	}
	if (key == KEY_MOUSE) {
		if (mouse_b & 32) {
			/* the mouse moved: to a canvas that wants to know */
			int64_t id = item_at(w->box, mouse_x - 1, mouse_y - 1);
			if (id && item(id)->kind == K_CANVAS && item(id)->on_move.fn) {
				canvas_mouse(item(id), mouse_x - 1, mouse_y - 1);
				invoke(item(id)->on_move);
			}
		} else if (mouse_b == 0)
			click(win, mouse_x - 1, mouse_y - 1);
		else if (mouse_b == 64 || mouse_b == 65) {
			int64_t id = item_at(w->box, mouse_x - 1, mouse_y - 1);
			if (id && (item(id)->kind == K_LIST || item(id)->kind == K_TABLE))
				key_to(id, mouse_b == 64 ? KEY_UP : KEY_DOWN, ch, 0);
			else if (id && item(id)->kind == K_TEXT_AREA) {
				Item *a = item(id);
				a->scroll += mouse_b == 64 ? -1 : 1;
				if (a->scroll < 0)
					a->scroll = 0;
			} else {
				/* a column that scrolls, around what's under the mouse */
				int64_t p = id ? id : 0;
				if (!p) {
					/* between things: the scrolling column there */
					for (int i = 0; i < nitems; i++)
						if (items[i].kind == K_BOX && items[i].scrolls && items[i].win == win &&
						    mouse_x - 1 >= items[i].x && mouse_x - 1 < items[i].x + items[i].w &&
						    mouse_y - 1 >= items[i].y && mouse_y - 1 < items[i].y + items[i].h)
							p = i + 1;
				}
				while (p && !(item(p)->kind == K_BOX && item(p)->scrolls))
					p = item(p)->parent;
				if (p)
					item(p)->scroll += mouse_b == 64 ? -1 : 1;   /* (place keeps it in range) */
			}
		}
		return;
	}
	check_focus(win);
	if (w->focus && key_to(w->focus, key, ch, chn))
		return;
	if (sc) {
		run_item(sc);
		return;
	}
	if (w->on_key.fn && key != KEY_TAB && key != KEY_BACKTAB && key != KEY_MOUSE) {
		/* a key nothing used: the window's own (arrows too, when it wants them) */
		char kn[16];
		key_shortcut_name(key, ch, chn, kn, sizeof kn);
		if (strcmp(kn, "space") == 0)
			strcpy(kn, " ");
		if (strncmp(kn, "shift+", 6) == 0 && kn[7] == 0) {   /* (a capital, as itself) */
			kn[0] = (char)(kn[6] - 'a' + 'A');
			kn[1] = 0;
		}
		if (kn[0]) {
			free(w->text);
			w->text = dupstr(kn);
			invoke(w->on_key);
			return;
		}
	}
	if (key == KEY_TAB || key == KEY_DOWN || key == KEY_RIGHT)
		move_focus(win, 1);
	else if (key == KEY_BACKTAB || key == KEY_UP || key == KEY_LEFT)
		move_focus(win, -1);
}

/* Script mode is known from the start, since timers made in `build`
 * already need the script's clock. */
__attribute__((constructor)) static void read_script(void)
{
	const char *s = getenv("CABBY_TUI_SCRIPT");
	if (s) {
		script_mode = true;
		script_left = script_text = dupstr(s);
	}
}

__attribute__((destructor)) static void free_script(void)
{
	free(script_text);
}

static int64_t window_stack[64];
static int window_depth;

void cby_tui_window_run(int64_t win)
{
	Item *w = item(win);
	if (w->open)
		return;   /* already running */
	w->open = true;
	if (window_depth == 0)
		term_setup();
	if (window_depth < 64)
		window_stack[window_depth] = win;
	window_depth++;
	showing = win;
	while (window_alive(win) && item(win)->open) {
		draw(win);
		int wait = run_timers(win);
		if (!window_alive(win) || !item(win)->open)
			break;
		if (script_mode) {
			if (!run_script_step(win)) {
				item(win)->open = false;   /* the script is over */
				break;
			}
			continue;
		}
		char ch[8];
		int chn;
		int key = read_key(wait, ch, &chn);
		resized = 0;      /* the next draw fits the new size */
		if (key == -3 || key == -2)
			continue;
		handle_key(win, key, ch, chn);
	}
	if (dialog.kind != D_NONE && dialog.win == win)
		dialog.kind = D_NONE;
	if (open_menu.win == win)
		close_menu();
	if (picker.target && item(picker.target)->win == win)
		picker.target = 0;
	window_depth--;
	if (window_depth > 0) {
		showing = window_stack[window_depth - 1];
		if (window_alive(showing))
			draw(showing);
	} else {
		term_restore();
		showing = 0;
	}
}

/* ------------------------------------------------------------- windows */

int64_t cby_tui_window_new(void)
{
	int64_t win = new_item(K_WINDOW, 0);
	item(win)->win = win;
	item(win)->title = dupstr("");
	int64_t box = new_item(K_BOX, win);
	item(win)->box = box;
	return win;
}

int64_t cby_tui_window_box(int64_t win) { return item(win)->box; }

void cby_tui_window_set_title(int64_t win, const char *title)
{
	free(item(win)->title);
	item(win)->title = dupstr(title);
}

/* gui's sizes are pixels: about 8 by 16 to a character */
void cby_tui_window_set_size(int64_t win, int width, int height)
{
	item(win)->want_w = width > 0 ? width / 8 : 0;
	item(win)->want_h = height > 0 ? height / 16 : 0;
}

void cby_tui_window_close(int64_t win)
{
	if (window_alive(win))
		item(win)->open = false;
}

static void free_item(int64_t id)
{
	if (id < 1 || id > nitems || items[id - 1].kind == K_FREE)
		return;
	Item *it = item(id);
	for (int i = 0; i < it->nkids; i++)
		free_item(it->kids[i]);
	for (int i = 0; i < it->nopts; i++)
		free(it->opts[i]);
	free(it->opts);
	free(it->kids);
	free(it->text);
	free(it->title);
	free(it->chosen);
	free(it->pix);
	clear_writing(it);
	free(it->keys);
	free(it->keys_shown);
	free_rows(it);
	free(it->trows);
	free(it->order);
	free(it->cache);
	for (int i = 0; i < it->nmenus; i++)
		if (it->menus[i] >= 1 && it->menus[i] <= nitems && items[it->menus[i] - 1].kind != K_FREE)
			free_item(it->menus[i]);
	free(it->menus);
	memset(it, 0, sizeof *it);
	it->kind = K_FREE;
}

void cby_tui_window_destroy(int64_t win)
{
	if (!window_alive(win))
		return;
	item(win)->open = false;
	free_item(item(win)->box);
	free_item(win);
}

/* ------------------------------------------------------------- adding */

int64_t cby_tui_add_label(int64_t box, const char *text) { return add_to(box, K_LABEL, text); }
int64_t cby_tui_add_button(int64_t box, const char *text) { return add_to(box, K_BUTTON, text); }
int64_t cby_tui_add_entry(int64_t box) { return add_to(box, K_ENTRY, ""); }
int64_t cby_tui_add_check(int64_t box, const char *text) { return add_to(box, K_CHECK, text); }
int64_t cby_tui_add_choice(int64_t box) { return add_to(box, K_CHOICE, ""); }
int64_t cby_tui_add_text_area(int64_t box) { return add_to(box, K_TEXT_AREA, ""); }
int64_t cby_tui_add_list(int64_t box) { return add_to(box, K_LIST, ""); }
int64_t cby_tui_add_progress(int64_t box) { return add_to(box, K_PROGRESS, ""); }
int64_t cby_tui_add_image(int64_t box, const char *path) { return add_to(box, K_IMAGE, path); }
void cby_tui_add_separator(int64_t box) { add_to(box, K_SEPARATOR, ""); }

int64_t cby_tui_add_slider(int64_t box, int min, int max)
{
	int64_t id = add_to(box, K_SLIDER, "");
	Item *s = item(id);
	s->min = min < max ? min : max;
	s->max = min < max ? max : min;
	s->value = s->min;
	return id;
}

int64_t cby_tui_add_row(int64_t box)
{
	int64_t id = add_to(box, K_BOX, "");
	item(id)->row = true;
	return id;
}

int64_t cby_tui_add_column(int64_t box) { return add_to(box, K_BOX, ""); }

int64_t cby_tui_add_scrolling_column(int64_t box)
{
	int64_t id = add_to(box, K_BOX, "");
	item(id)->scrolls = true;
	return id;
}

int64_t cby_tui_add_tabs(int64_t box) { return add_to(box, K_TABS, ""); }

int64_t cby_tui_tabs_add_tab(int64_t tabs, const char *title)
{
	Item *t = item(tabs);
	add_opt(t, title);
	if (t->selected < 0)
		t->selected = 0;
	return add_to(tabs, K_BOX, "");
}

/* ------------------------------------------------------------- reading and changing */

/* The text a thing shows: for a drop-down or list, its chosen entry */
static const char *text_of(int64_t id)
{
	Item *it = item(id);
	if (it->kind == K_WINDOW)
		return it->chosen ? it->chosen : "";
	if (it->kind == K_TABLE)
		return it->selected >= 0 ? tcell(it, it->selected, 0) : "";
	if (it->kind == K_CHOICE || it->kind == K_LIST || it->kind == K_TABS)
		return it->selected >= 0 && it->selected < it->nopts ? it->opts[it->selected] : "";
	return it->text;
}

void cby_tui_set_text(int64_t id, const char *text)
{
	Item *it = item(id);
	if (it->kind == K_CHOICE || it->kind == K_LIST) {
		for (int i = 0; i < it->nopts; i++)
			if (strcmp(it->opts[i], text) == 0)
				select_option(it, i, it->kind == K_LIST);
		return;
	}
	free(it->text);
	it->text = dupstr(text);
	it->cur = (int)strlen(it->text);
	if (it->kind == K_ENTRY || it->kind == K_TEXT_AREA)
		invoke(it->on_change);
}

int64_t cby_tui_text_length(int64_t id) { return (int64_t)strlen(text_of(id)); }

char cby_tui_text_char(int64_t id, int64_t i)
{
	const char *t = text_of(id);
	return i >= 0 && i < (int64_t)strlen(t) ? t[i] : 0;
}

void cby_tui_set_enabled(int64_t id, int on) { item(id)->enabled = on != 0; }

/* colors of the program's own: what it's written in, and behind it ("":
 * the usual again). Rows, columns and windows pass them on to what's in
 * them. */
void cby_tui_set_text_color(int64_t id, const char *color)
{
	Item *it = item(id);
	double a;
	it->has_ink = color && *color;
	it->ink = it->has_ink ? color_of(color, &a) : 0;
}

void cby_tui_set_background(int64_t id, const char *color)
{
	Item *it = item(id);
	double a;
	it->has_paper = color && *color;
	it->paper_rgb = it->has_paper ? color_of(color, &a) : 0;
}

/* the window's own keys: those nothing in it uses */
void cby_tui_window_on_key(int64_t win, void *obj, void *fn) { item(win)->on_key = handler(win, obj, fn); }
int64_t cby_tui_window_key_length(int64_t win) { return (int64_t)strlen(item(win)->text); }

char cby_tui_window_key_char(int64_t win, int64_t i)
{
	const char *t = item(win)->text;
	return i >= 0 && i < (int64_t)strlen(t) ? t[i] : 0;
}
void cby_tui_set_visible(int64_t id, int on) { item(id)->visible = on != 0; }

void cby_tui_set_min_size(int64_t id, int width, int height)
{
	item(id)->min_w = width > 0 ? (width + 7) / 8 : 0;
	item(id)->min_h = height > 0 ? (height + 15) / 16 : 0;
}

int cby_tui_check_get(int64_t id) { return item(id)->value; }

void cby_tui_check_set(int64_t id, int on)
{
	Item *it = item(id);
	if ((on != 0) != (it->value != 0)) {
		it->value = on != 0;
		invoke(it->on_toggle);
	}
}

void cby_tui_choice_add(int64_t id, const char *text)
{
	Item *it = item(id);
	add_opt(it, text);
	if (it->selected < 0)
		it->selected = 0;   /* the first option is chosen to begin with */
}

int cby_tui_selected(int64_t id) { return item(id)->selected; }

void cby_tui_select(int64_t id, int index)
{
	Item *it = item(id);
	if (it->kind == K_TABLE) {
		table_pick(it, index);
		return;
	}
	if (it->kind == K_TABS && index < 0)
		return;
	if (index >= -1 && index < it->nopts && index != it->selected) {
		it->selected = index;
		invoke(it->kind == K_LIST ? it->on_select : it->on_change);
	}
}

int cby_tui_count(int64_t id) { return item(id)->nopts; }
void cby_tui_list_add(int64_t id, const char *text) { add_opt(item(id), text); }

void cby_tui_list_clear(int64_t id)
{
	Item *it = item(id);
	for (int i = 0; i < it->nopts; i++)
		free(it->opts[i]);
	it->nopts = 0;
	it->scroll = 0;
	if (it->selected != -1) {
		it->selected = -1;
		invoke(it->on_select);
	}
}

void cby_tui_text_area_set_editable(int64_t id, int on) { item(id)->editable = on != 0; }
int cby_tui_slider_get(int64_t id) { return item(id)->value; }
void cby_tui_slider_set(int64_t id, int value) { set_slider(item(id), value); }
void cby_tui_progress_set(int64_t id, double fraction) { item(id)->fraction = fraction; }

void cby_tui_image_set(int64_t id, const char *path)
{
	free(item(id)->text);
	item(id)->text = dupstr(path);
}

/* ------------------------------------------------------------- handlers */

static Handler handler(int64_t id, void *obj, void *fn) { return (Handler){ obj, fn, item(id)->win }; }

void cby_tui_on_click(int64_t id, void *obj, void *fn) { item(id)->on_click = handler(id, obj, fn); }
void cby_tui_on_change(int64_t id, void *obj, void *fn) { item(id)->on_change = handler(id, obj, fn); }
void cby_tui_on_enter(int64_t id, void *obj, void *fn) { item(id)->on_enter = handler(id, obj, fn); }
void cby_tui_on_toggle(int64_t id, void *obj, void *fn) { item(id)->on_toggle = handler(id, obj, fn); }
void cby_tui_on_select(int64_t id, void *obj, void *fn) { item(id)->on_select = handler(id, obj, fn); }

void cby_tui_every(int64_t win, int ms, void *obj, void *fn) { add_timer(win, ms, 1, obj, fn); }
void cby_tui_after(int64_t win, int ms, void *obj, void *fn) { add_timer(win, ms, 0, obj, fn); }

void cby_tui_stop_timers(int64_t win)
{
	for (int i = 0; i < ntimers; i++)
		if (timers[i].win == win)
			timers[i].live = false;
}

/* ------------------------------------------------------------- dialogs */

void cby_tui_show_message(int64_t win, const char *text) { dialog_start(D_MESSAGE, win, text, NULL, NULL); }

void cby_tui_ask(int64_t win, const char *question, void *obj, void *fn)
{
	dialog_start(D_ASK, win, question, obj, fn);
}

void cby_tui_ask_open_file(int64_t win, void *obj, void *fn) { dialog_start(D_OPEN, win, "", obj, fn); }

void cby_tui_ask_save_file(int64_t win, const char *suggested, void *obj, void *fn)
{
	dialog_start(D_SAVE, win, suggested, obj, fn);
}

int64_t cby_tui_chosen_length(int64_t win) { return cby_tui_text_length(win); }
char cby_tui_chosen_char(int64_t win, int64_t i) { return cby_tui_text_char(win, i); }
