/* tgame_terminal.h — the terminal side of `use tgame`: a game in a terminal.
 *
 * game.c is built a second time as the tgame library (with TERMINAL_GAME
 * defined, and cby_tgame_ names). Everything about the game is the same:
 * SDL still draws the stage, off screen, with no window. Each frame, the
 * picture is read back, made as small as the terminal (keeping its shape),
 * and shown with half blocks: '▀' in a character is two pixels, its top in
 * the text's color and its bottom in the background's. Only the characters
 * that changed since the last frame are written, so it's quick, even over
 * SSH.
 *
 * Text the stage's on_draw writes isn't drawn into the picture (it would
 * be too small to read): it's put in the terminal as real characters, at
 * its place, over the picture.
 *
 * Keys: terminals that follow kitty's keyboard protocol (kitty, foot,
 * WezTerm, Ghostty, Alacritty, iTerm2 with it turned on) say when a key
 * is let go, so key_hold and key_release work exactly. Others only send a
 * key again and again while it's held; then it's taken as let go when the
 * repeats stop. The mouse works where the terminal passes clicks on.
 * Ctrl+C or Ctrl+Q closes the game. */

#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

/* a character on the terminal: its two pixels, or a character of text */
typedef struct Cell {
	uint32_t top, bottom;     /* 0xRRGGBB */
	uint32_t fg;              /* text: its color */
	char ch[8];               /* text: the character ("" for the two pixels) */
} Cell;

static struct termios term_was;
static int term_on;                 /* the terminal is ours (raw, the other screen) */
static int kitty_keys;              /* it says when keys are let go */
static int truecolor;
static volatile sig_atomic_t term_resized = 1;
static int term_cols = 80, term_rows = 24;
static Cell *cells, *shown;         /* this frame's characters, and what's on the terminal */
static int ncells, shown_cols, shown_rows;
static double term_scale;           /* terminal pixels for each stage pixel */
static int pic_w, pic_h, pic_col, pic_row;   /* the picture's size in pixels, and where it starts */

/* text written by the stage's on_draw, this frame */
typedef struct TermText {
	double x, y, size;
	uint32_t color;
	char *text;
} TermText;

static TermText *texts;
static int ntexts;
static int text_as_cells;           /* while drawing for the terminal: text is kept here */

static char *message_box;           /* show_message's text, while it's showing */

/* What the program prints while the game runs is kept, and shown after it
 * ends (as tui does): the terminal is written to through term_fd. */
static int term_fd = 1, kept_stdout = -1;
static FILE *kept_output;

/* ---- writing to the terminal: gathered, then written at once */

static char *out;
static size_t out_len, out_cap;

static void put(const char *s, size_t n)
{
	if (out_len + n + 1 > out_cap) {
		out_cap = (out_len + n + 1) * 2;
		out = realloc(out, out_cap);
	}
	memcpy(out + out_len, s, n);
	out_len += n;
}

static void puts_(const char *s) { put(s, strlen(s)); }

static void putf(const char *f, ...)
{
	char b[96];
	va_list ap;
	va_start(ap, f);
	int n = vsnprintf(b, sizeof b, f, ap);
	va_end(ap);
	if (n > 0)
		put(b, (size_t)n < sizeof b ? (size_t)n : sizeof b - 1);
}

static void flush_out(int fd)
{
	size_t done = 0;
	while (done < out_len) {
		ssize_t n = write(fd, out + done, out_len - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		done += (size_t)n;
	}
	out_len = 0;
}

/* ---- colors: 24-bit where the terminal has them, the 256 otherwise */

static int color_256(uint32_t c)
{
	int r = (int)(c >> 16) & 255, g = (int)(c >> 8) & 255, b = (int)c & 255;
	if (abs(r - g) < 10 && abs(g - b) < 10) {   /* a gray: the 24 grays */
		if (r < 8)
			return 16;
		if (r > 238)
			return 231;
		return 232 + (r - 8) * 24 / 231;
	}
	int ri = (r * 5 + 127) / 255, gi = (g * 5 + 127) / 255, bi = (b * 5 + 127) / 255;
	return 16 + 36 * ri + 6 * gi + bi;
}

static void set_fg(uint32_t c)
{
	if (truecolor)
		putf("\x1b[38;2;%u;%u;%um", (c >> 16) & 255, (c >> 8) & 255, c & 255);
	else
		putf("\x1b[38;5;%dm", color_256(c));
}

static void set_bg(uint32_t c)
{
	if (truecolor)
		putf("\x1b[48;2;%u;%u;%um", (c >> 16) & 255, (c >> 8) & 255, c & 255);
	else
		putf("\x1b[48;5;%dm", color_256(c));
}

static int has_truecolor(void)
{
	const char *ct = getenv("COLORTERM"), *t = getenv("TERM"), *p = getenv("TERM_PROGRAM");
	if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit")))
		return 1;
	if (t && (strstr(t, "kitty") || strstr(t, "alacritty") || strstr(t, "foot") || strstr(t, "wezterm") ||
		  strstr(t, "ghostty") || strstr(t, "direct")))
		return 1;
	if (p && (!strcmp(p, "iTerm.app") || !strcmp(p, "WezTerm") || !strcmp(p, "ghostty") || !strcmp(p, "vscode")))
		return 1;
	return 0;
}

/* ---- the terminal: taken, and given back as it was */

static void term_stop(void)
{
	if (!term_on)
		return;
	term_on = 0;
	if (kitty_keys)
		puts_("\x1b[<u");
	puts_("\x1b[?1000l\x1b[?1003l\x1b[?1006l\x1b[0m\x1b[?25h\x1b[?1049l");
	flush_out(term_fd);
	/* keys pressed at the end go nowhere, not to whatever reads the terminal next */
	struct pollfd p = { 0, POLLIN, 0 };
	while (poll(&p, 1, 30) > 0) {
		char junk[256];
		if (read(0, junk, sizeof junk) <= 0)
			break;
	}
	tcsetattr(0, TCSAFLUSH, &term_was);
	/* what the program printed while the game ran */
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
	if (term_fd != 1)
		close(term_fd);
	term_fd = 1;
}

static void on_resize(int sig)
{
	(void)sig;
	term_resized = 1;
}

static void on_end_signal(int sig)
{
	term_stop();
	signal(sig, SIG_DFL);
	raise(sig);
}

/* reads what the terminal sends, for up to ms milliseconds */
static int read_some(char *buf, int size, int ms)
{
	struct pollfd p = { 0, POLLIN, 0 };
	if (poll(&p, 1, ms) <= 0)
		return 0;
	ssize_t n = read(0, buf, (size_t)size);
	return n > 0 ? (int)n : 0;
}

/* Does the terminal know kitty's keyboard protocol? It's asked (CSI ? u),
 * and asked something every terminal answers (CSI c) after: an answer to
 * the first, before the second's, is a yes. */
static int ask_kitty(void)
{
	puts_("\x1b[?u\x1b[c");
	flush_out(term_fd);
	char buf[256];
	int len = 0, yes = 0;
	for (int tries = 0; tries < 20 && len < (int)sizeof buf - 1; tries++) {
		int n = read_some(buf + len, (int)sizeof buf - 1 - len, 50);
		len += n;
		buf[len] = '\0';
		char *da = strstr(buf, "\x1b[?");
		while (da) {
			/* "\x1b[?<flags>u" (kitty) or "\x1b[?...c" (the other answer) */
			char *e = da + 3;
			while (*e && (isdigit((unsigned char)*e) || *e == ';'))
				e++;
			if (*e == 'u')
				yes = 1;
			else if (*e == 'c')
				return yes;
			da = strstr(e, "\x1b[?");
		}
	}
	return yes;
}

static void term_start(void)
{
	if (!isatty(0) || !isatty(1)) {
		fprintf(stderr, "tgame: a game in a terminal needs a terminal to run in\n");
		exit(1);
	}
	tcgetattr(0, &term_was);
	struct termios raw = term_was;
	raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
	raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(0, TCSAFLUSH, &raw);
	term_on = 1;
	fflush(stdout);
	term_fd = dup(1);
	kept_output = tmpfile();
	if (kept_output) {
		kept_stdout = dup(1);
		dup2(fileno(kept_output), 1);
	}
	atexit(term_stop);
	signal(SIGTERM, on_end_signal);
	signal(SIGHUP, on_end_signal);
	signal(SIGINT, on_end_signal);
	signal(SIGWINCH, on_resize);
	truecolor = has_truecolor();
	kitty_keys = ask_kitty();
	/* the other screen, no cursor, the mouse's clicks (as numbers), and
	 * kitty's keys: 1 + 2 + 8, each key's going down, repeating and
	 * coming up, as codes */
	puts_("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[?1000h\x1b[?1006h");
	if (kitty_keys)
		puts_("\x1b[>11u");
	flush_out(term_fd);
	term_resized = 1;
}

static void term_mouse_moves(int on)
{
	puts_(on ? "\x1b[?1003h" : "\x1b[?1003l");
}

/* ---- the picture, made into characters */

static uint32_t rgb_at(const unsigned char *p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }

static uint32_t mix(uint32_t a, uint32_t b)
{
	return (((a >> 16 & 255) + (b >> 16 & 255)) / 2) << 16 | (((a >> 8 & 255) + (b >> 8 & 255)) / 2) << 8 |
	       ((a & 255) + (b & 255)) / 2;
}

/* how the stage fits the terminal: as big as it can, keeping its shape */
static void fit(int stage_w, int stage_h)
{
	double sx = (double)term_cols / stage_w, sy = (double)term_rows * 2 / stage_h;
	term_scale = sx < sy ? sx : sy;
	pic_w = (int)(stage_w * term_scale);
	pic_h = (int)(stage_h * term_scale);
	if (pic_w < 1)
		pic_w = 1;
	if (pic_h < 2)
		pic_h = 2;
	pic_col = (term_cols - pic_w) / 2;
	pic_row = (term_rows - (pic_h + 1) / 2) / 2;
}

/* the stage's picture (RGBA, w by h), averaged down into the cells */
static void picture_to_cells(const unsigned char *px, int w, int h, int pitch)
{
	int rows = (pic_h + 1) / 2;
	uint32_t *line = malloc(sizeof *line * (size_t)pic_w * 2);
	for (int r = 0; r < rows; r++) {
		for (int half = 0; half < 2; half++) {
			int oy = r * 2 + half;
			if (oy >= pic_h)
				oy = pic_h - 1;
			int y0 = (int)(oy / term_scale), y1 = (int)((oy + 1) / term_scale);
			if (y1 <= y0)
				y1 = y0 + 1;
			if (y1 > h)
				y1 = h;
			for (int ox = 0; ox < pic_w; ox++) {
				int x0 = (int)(ox / term_scale), x1 = (int)((ox + 1) / term_scale);
				if (x1 <= x0)
					x1 = x0 + 1;
				if (x1 > w)
					x1 = w;
				unsigned long sr = 0, sg = 0, sb = 0, n = 0;
				for (int y = y0; y < y1; y++)
					for (int x = x0; x < x1; x++) {
						const unsigned char *p = px + (size_t)y * (size_t)pitch + (size_t)x * 4;
						sr += p[0];
						sg += p[1];
						sb += p[2];
						n++;
					}
				if (!n)
					n = 1;
				line[half * pic_w + ox] = (uint32_t)(sr / n) << 16 | (uint32_t)(sg / n) << 8 | (uint32_t)(sb / n);
			}
		}
		for (int ox = 0; ox < pic_w; ox++) {
			int c = pic_col + ox, rr = pic_row + r;
			if (c < 0 || c >= term_cols || rr < 0 || rr >= term_rows)
				continue;
			Cell *cell = &cells[rr * term_cols + c];
			cell->top = line[ox];
			cell->bottom = line[pic_w + ox];
			cell->ch[0] = '\0';
		}
	}
	free(line);
	(void)rgb_at;
}

/* a text's characters, over the picture where it was written */
static void text_to_cells(const TermText *t)
{
	int col = pic_col + (int)floor(t->x * term_scale);
	int row = pic_row + (int)floor((t->y + t->size * 0.55) * term_scale / 2);
	if (row < pic_row || row >= pic_row + (pic_h + 1) / 2 || row >= term_rows)
		return;
	const unsigned char *p = (const unsigned char *)t->text;
	while (*p) {
		const unsigned char *start = p;
		uint32_t code = next_char(&p);
		if (col >= pic_col + pic_w || col >= term_cols)
			break;
		if (col >= pic_col && col >= 0 && code >= 32) {
			Cell *cell = &cells[row * term_cols + col];
			size_t n = (size_t)(p - start);
			if (n >= sizeof cell->ch)
				n = sizeof cell->ch - 1;
			memcpy(cell->ch, start, n);
			cell->ch[n] = '\0';
			cell->fg = t->color;
			if (code == ' ')
				snprintf(cell->ch, sizeof cell->ch, " ");
		}
		col++;
	}
}

/* show_message's box: in the middle, the text wrapped, and how to close it */
static void message_to_cells(const char *text)
{
	int inner = term_cols - 8 < 50 ? term_cols - 8 : 50;
	if (inner < 10)
		inner = term_cols - 2 > 1 ? term_cols - 2 : 1;
	/* the lines, broken at spaces */
	char *lines[64];
	int nlines = 0;
	char *copy = strdup(text), *s = copy;
	while (*s && nlines < 62) {
		int len = 0, last_space = -1, chars = 0;
		while (s[len] && s[len] != '\n' && chars < inner) {
			if (s[len] == ' ')
				last_space = len;
			if (((unsigned char)s[len] & 0xC0) != 0x80)
				chars++;
			len++;
		}
		while (((unsigned char)s[len] & 0xC0) == 0x80)
			len++;
		if (s[len] && s[len] != '\n' && last_space > 0)
			len = last_space;
		char c = s[len];
		s[len] = '\0';
		lines[nlines++] = s;
		s += len + (c ? 1 : 0);
		if (c == ' ')
			while (*s == ' ')
				s++;
	}
	lines[nlines++] = "";
	lines[nlines++] = "(Enter to close)";
	int width = inner + 4, height = nlines + 2;
	int left = (term_cols - width) / 2, top = (term_rows - height) / 2;
	for (int r = 0; r < height; r++)
		for (int c = 0; c < width; c++) {
			int x = left + c, y = top + r;
			if (x < 0 || x >= term_cols || y < 0 || y >= term_rows)
				continue;
			Cell *cell = &cells[y * term_cols + x];
			int edge = r == 0 || r == height - 1 || c == 0 || c == width - 1;
			cell->top = cell->bottom = 0xffffff;
			cell->fg = 0x2b2b38;
			snprintf(cell->ch, sizeof cell->ch, "%s", !edge ? " " :
				 (r == 0 && c == 0) ? "┌" : (r == 0 && c == width - 1) ? "┐" :
				 (r == height - 1 && c == 0) ? "└" : (r == height - 1 && c == width - 1) ? "┘" :
				 (r == 0 || r == height - 1) ? "─" : "│");
		}
	for (int i = 0; i < nlines; i++) {
		TermText t = { 0, 0, 0, 0x2b2b38, lines[i] };
		int row = top + 1 + i, col = left + 2;
		const unsigned char *p = (const unsigned char *)t.text;
		while (*p && row < term_rows) {
			const unsigned char *start = p;
			next_char(&p);
			if (col >= 0 && col < term_cols) {
				Cell *cell = &cells[row * term_cols + col];
				size_t n = (size_t)(p - start);
				if (n >= sizeof cell->ch)
					n = sizeof cell->ch - 1;
				memcpy(cell->ch, start, n);
				cell->ch[n] = '\0';
			}
			col++;
		}
	}
	free(copy);
}

static int same_cell(const Cell *a, const Cell *b)
{
	return a->top == b->top && a->bottom == b->bottom && strcmp(a->ch, b->ch) == 0 && (!a->ch[0] || a->fg == b->fg);
}

/* writes the characters that changed (all of them after a resize) to fd */
static void cells_out(int fd, int all)
{
	int have_fg = -1, have_bg = -1, at_r = -1, at_c = -1;
	uint32_t fg = 0, bg = 0;
	for (int r = 0; r < term_rows; r++)
		for (int c = 0; c < term_cols; c++) {
			Cell *cell = &cells[r * term_cols + c];
			if (!all && shown && same_cell(cell, &shown[r * term_cols + c]))
				continue;
			if (at_r != r || at_c != c)
				putf("\x1b[%d;%dH", r + 1, c + 1);
			uint32_t want_fg = cell->ch[0] ? cell->fg : cell->top, want_bg = cell->ch[0] ? mix(cell->top, cell->bottom) : cell->bottom;
			int blank = !cell->ch[0] && cell->top == cell->bottom;
			if (!blank && (have_fg < 0 || fg != want_fg)) {
				set_fg(want_fg);
				fg = want_fg;
				have_fg = 1;
			}
			if (have_bg < 0 || bg != want_bg) {
				set_bg(want_bg);
				bg = want_bg;
				have_bg = 1;
			}
			puts_(cell->ch[0] ? cell->ch : blank ? " " : "\xe2\x96\x80");   /* ▀ */
			at_r = r;
			at_c = c + 1;
		}
	puts_("\x1b[0m");
	flush_out(fd);
	if (!shown || shown_cols != term_cols || shown_rows != term_rows) {
		free(shown);
		shown = malloc(sizeof *shown * (size_t)ncells);
		shown_cols = term_cols;
		shown_rows = term_rows;
	}
	memcpy(shown, cells, sizeof *cells * (size_t)ncells);
}

static void clear_texts(void)
{
	for (int i = 0; i < ntexts; i++)
		free(texts[i].text);
	ntexts = 0;
}

static void keep_text(double x, double y, double size, SDL_FColor c, const char *text)
{
	texts = realloc(texts, sizeof *texts * (size_t)(ntexts + 1));
	uint32_t rgb = (uint32_t)(c.r * 255 + 0.5) << 16 | (uint32_t)(c.g * 255 + 0.5) << 8 | (uint32_t)(c.b * 255 + 0.5);
	texts[ntexts++] = (TermText){ x, y, size, rgb, strdup(text) };
}

/* the whole frame, as characters: the picture, the text over it, and a
 * message box over everything. 1 if the terminal's size changed. */
static int frame_to_cells(SDL_Surface *s, int stage_w, int stage_h)
{
	int resized = 0;
	if (term_resized) {
		term_resized = 0;
		struct winsize ws;
		if (ioctl(term_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
			term_cols = ws.ws_col;
			term_rows = ws.ws_row;
		}
		resized = 1;
	}
	if (ncells != term_cols * term_rows) {
		ncells = term_cols * term_rows;
		cells = realloc(cells, sizeof *cells * (size_t)ncells);
		resized = 1;
	}
	for (int i = 0; i < ncells; i++)
		cells[i] = (Cell){ 0, 0, 0, "" };
	fit(stage_w, stage_h);
	picture_to_cells(s->pixels, s->w, s->h, s->pitch);
	for (int i = 0; i < ntexts; i++)
		text_to_cells(&texts[i]);
	if (message_box)
		message_to_cells(message_box);
	return resized;
}

/* ---- keys and the mouse, from what the terminal sends */

typedef void (*KeyFn)(const char *name, int ctrl, int shift, int alt, int kind);   /* kind: 1 down, 2 again, 3 up */
typedef void (*MouseFn)(int col, int row, int click);

static char in_buf[512];
static int in_len;
static double esc_since;            /* a lone Escape: sent once nothing follows it */

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* a kitty key code, or a legacy final letter, as the game names keys */
static const char *code_name(int code, char final, char *buf)
{
	switch (final) {
	case 'A': return "up";
	case 'B': return "down";
	case 'C': return "right";
	case 'D': return "left";
	case 'H': return "home";
	case 'F': return "end";
	case 'P': return "f1";
	case 'Q': return "f2";
	case 'R': return "f3";
	case 'S': return "f4";
	case '~':
		switch (code) {
		case 2: return "insert";
		case 3: return "delete";
		case 5: return "page_up";
		case 6: return "page_down";
		case 1: case 7: return "home";
		case 4: case 8: return "end";
		case 11: return "f1";
		case 12: return "f2";
		case 13: return "f3";
		case 14: return "f4";
		case 15: return "f5";
		case 17: return "f6";
		case 18: return "f7";
		case 19: return "f8";
		case 20: return "f9";
		case 21: return "f10";
		case 23: return "f11";
		case 24: return "f12";
		}
		return NULL;
	case 'u':
		switch (code) {
		case 13: return "enter";
		case 27: return "escape";
		case 9: return "tab";
		case 127: case 8: return "backspace";
		case 32: return "space";
		}
		if (code >= 57376 && code <= 57387) {   /* (kitty's F13 and on: not ours) */
			return NULL;
		}
		if (code > 32 && code < 127) {
			buf[0] = (char)(code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code);
			buf[1] = '\0';
			return buf;
		}
		return NULL;
	}
	return NULL;
}

/* one thing the terminal sent: a key, or the mouse */
typedef struct InEvent {
	int is_mouse;
	char name[16];
	int ctrl, shift, alt, kind;     /* a key */
	int col, row, click;            /* the mouse */
} InEvent;

static void key_ev(InEvent *ev, const char *name, int ctrl, int shift, int alt, int kind)
{
	ev->is_mouse = 0;
	snprintf(ev->name, sizeof ev->name, "%s", name);
	ev->ctrl = ctrl;
	ev->shift = shift;
	ev->alt = alt;
	ev->kind = kind;
}

/* The first whole thing in the buffer: how many bytes it took (0: nothing
 * whole yet), and whether it was something (ev). */
static int parse_one(InEvent *ev, int *got)
{
	*got = 0;
	if (in_len == 0)
		return 0;
	unsigned char c = (unsigned char)in_buf[0];
	char one[2];
	if (c == 0x1b) {
		if (in_len < 2) {
			/* Escape on its own, or the start of something: wait a moment */
			if (!esc_since)
				esc_since = now_ms();
			if (now_ms() - esc_since < 40)
				return 0;
			esc_since = 0;
			key_ev(ev, "escape", 0, 0, 0, 1);
			*got = 1;
			return 1;
		}
		esc_since = 0;
		char k = in_buf[1];
		if (k == '[' || k == 'O') {
			/* CSI (or SS3): numbers, then a final letter */
			int j = 2, mouse_sgr = 0;
			if (k == '[' && j < in_len && in_buf[j] == '<') {
				mouse_sgr = 1;
				j++;
			}
			int nums[8] = { 0 }, sub[8] = { 0 }, nn = 0, insub = 0, seen_digit = 0;
			while (j < in_len && ((in_buf[j] >= '0' && in_buf[j] <= '9') || in_buf[j] == ';' || in_buf[j] == ':' ||
					      in_buf[j] == '?' || in_buf[j] == '>' || in_buf[j] == '=')) {
				char d = in_buf[j];
				if (d >= '0' && d <= '9') {
					if (insub)
						sub[nn] = sub[nn] * 10 + (d - '0');
					else
						nums[nn] = nums[nn] * 10 + (d - '0');
					seen_digit = 1;
				} else if (d == ';') {
					if (nn < 7)
						nn++;
					insub = 0;
				} else if (d == ':') {
					insub = 1;
				}
				j++;
			}
			if (j >= in_len)
				return 0;   /* not all here yet */
			char final = in_buf[j];
			int count = nn + (seen_digit || nn ? 1 : 0);
			*got = j + 1;
			if (mouse_sgr) {
				/* button;col;row, then M (down, or a move) or m (up) */
				int b = nums[0];
				if (final == 'M' && count >= 3 && ((b & 32) || (b & 3) == 0)) {
					ev->is_mouse = 1;
					ev->col = nums[1];
					ev->row = nums[2];
					ev->click = !(b & 32);
					return 1;
				}
				return 0;
			}
			/* a key: code;modifiers:kind */
			int code = count >= 1 ? nums[0] : 1;
			int mods = count >= 2 ? nums[1] - 1 : 0, kind = count >= 2 && sub[1] ? sub[1] : 1;
			if (mods < 0)
				mods = 0;
			char buf[4];
			const char *name = code_name(code, final, buf);
			if (!name)
				return 0;
			key_ev(ev, name, (mods & 4) != 0, (mods & 1) != 0, (mods & 2) != 0, kind);
			return 1;
		}
		/* Alt and a key */
		*got = 2;
		if (k >= 32 && k < 127) {
			one[0] = k >= 'A' && k <= 'Z' ? (char)(k - 'A' + 'a') : k;
			one[1] = '\0';
			key_ev(ev, k == ' ' ? "space" : one, 0, 0, 1, 1);
			return 1;
		}
		return 0;
	}
	/* a plain byte: a letter, or Ctrl and a letter */
	*got = 1;
	const char *name = NULL;
	int ctrl = 0, shift = 0;
	if (c == 13 || c == 10)
		name = "enter";
	else if (c == 9)
		name = "tab";
	else if (c == 127 || c == 8)
		name = "backspace";
	else if (c == ' ')
		name = "space";
	else if (c >= 1 && c <= 26) {
		one[0] = (char)('a' + c - 1);
		one[1] = '\0';
		name = one;
		ctrl = 1;
	} else if (c > 32 && c < 127) {
		shift = c >= 'A' && c <= 'Z';
		one[0] = shift ? (char)(c - 'A' + 'a') : (char)c;
		one[1] = '\0';
		name = one;
	}
	if (!name)
		return 0;
	key_ev(ev, name, ctrl, shift, 0, 1);
	return 1;
}

/* Reads what the terminal sent, and calls key and mouse for each thing.
 * Each leaves the buffer before it's handled, so a handler may read more
 * (show_message does). */
static void read_input(KeyFn key, MouseFn mouse)
{
	in_len += read_some(in_buf + in_len, (int)sizeof in_buf - in_len, 0);
	for (;;) {
		InEvent ev;
		int got;
		int is = parse_one(&ev, &got);
		if (!got)
			break;
		memmove(in_buf, in_buf + got, (size_t)(in_len - got));
		in_len -= got;
		if (!is)
			continue;
		if (ev.is_mouse) {
			mouse(ev.col, ev.row, ev.click);
		} else {
			key(ev.name, ev.ctrl, ev.shift, ev.alt, ev.kind);
		}
	}
}
