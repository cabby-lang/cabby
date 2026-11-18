/* lineedit.c — see lineedit.h. */
#include "lineedit.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define TAB_WIDTH 4
#define HISTORY_MAX 1000

struct LineEditor {
	char *history_path;
	char **hist;
	int nhist, caphist;
	bool pasting;            /* inside a bracketed paste */
	int added;               /* lines added this session */
};

typedef struct {
	char *buf;
	int len, cap;
	int pos;                 /* cursor, as a byte offset */
	int scroll;              /* first display column shown */
	bool untouched;          /* still exactly the prefill */
} Line;

/* ------------------------------------------------------------- history */

static void hist_push(LineEditor *le, const char *line)
{
	if (le->nhist == le->caphist) {
		le->caphist = le->caphist ? le->caphist * 2 : 64;
		le->hist = realloc(le->hist, sizeof *le->hist * (size_t)le->caphist);
	}
	le->hist[le->nhist++] = strdup(line);
}

LineEditor *le_new(const char *history_path)
{
	LineEditor *le = calloc(1, sizeof *le);
	if (!le)
		return NULL;
	if (history_path) {
		le->history_path = strdup(history_path);
		FILE *f = fopen(history_path, "r");
		if (f) {
			char *line = NULL;
			size_t cap = 0;
			ssize_t n;
			while ((n = getline(&line, &cap, f)) > 0) {
				if (line[n - 1] == '\n')
					line[--n] = '\0';
				if (n > 0)
					hist_push(le, line);
			}
			free(line);
			fclose(f);
		}
	}
	return le;
}

void le_history_add(LineEditor *le, const char *line)
{
	bool blank = true;
	for (const char *p = line; *p; p++)
		if (*p != ' ' && *p != '\t')
			blank = false;
	if (blank)
		return;
	if (le->nhist && strcmp(le->hist[le->nhist - 1], line) == 0)
		return;
	hist_push(le, line);
	le->added++;
}

void le_free(LineEditor *le)
{
	if (!le)
		return;
	if (le->history_path && le->added) {
		FILE *f = fopen(le->history_path, "w");
		if (f) {
			int start = le->nhist > HISTORY_MAX ? le->nhist - HISTORY_MAX : 0;
			for (int i = start; i < le->nhist; i++)
				fprintf(f, "%s\n", le->hist[i]);
			fclose(f);
		}
	}
	for (int i = 0; i < le->nhist; i++)
		free(le->hist[i]);
	free(le->hist);
	free(le->history_path);
	free(le);
}

/* ------------------------------------------------------------- terminal */

static struct termios g_saved;

static bool raw_on(void)
{
	if (tcgetattr(0, &g_saved) < 0)
		return false;
	struct termios raw = g_saved;
	raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cflag |= CS8;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	/* TCSANOW, not TCSAFLUSH: keys typed while the last input ran are kept */
	if (tcsetattr(0, TCSANOW, &raw) < 0)
		return false;
	fputs("\x1b[?2004h", stdout);   /* bracketed paste on */
	fflush(stdout);
	return true;
}

static void raw_off(void)
{
	fputs("\x1b[?2004l", stdout);
	fflush(stdout);
	tcsetattr(0, TCSANOW, &g_saved);
}

/* Keys that arrived while fresh_line waited for the terminal's answer:
 * read_byte gives them back first, in order. */
static char *g_ahead;
static int g_ahead_len, g_ahead_pos, g_ahead_cap;

static void ahead_push(char c)
{
	if (g_ahead_pos == g_ahead_len)
		g_ahead_pos = g_ahead_len = 0;
	if (g_ahead_len == g_ahead_cap) {
		g_ahead_cap = g_ahead_cap ? g_ahead_cap * 2 : 256;
		g_ahead = realloc(g_ahead, (size_t)g_ahead_cap);
	}
	g_ahead[g_ahead_len++] = c;
}

extern void (*rt_idle_hook)(void);

static int read_byte(void)
{
	if (g_ahead_pos < g_ahead_len)
		return (unsigned char)g_ahead[g_ahead_pos++];
	unsigned char c;
	for (;;) {
		if (rt_idle_hook) {
			/* while waiting, let a window library keep in touch with the
			 * window system */
			struct pollfd pfd = { .fd = 0, .events = POLLIN };
			int r = poll(&pfd, 1, 50);
			if (r == 0) {
				rt_idle_hook();
				continue;
			}
		}
		ssize_t n = read(0, &c, 1);
		if (n == 1)
			return c;
		if (n < 0 && errno == EINTR)
			continue;
		return -1;
	}
}

/* Program output that didn't end its line (printf without "\n", putchar)
 * would be overwritten when the prompt is drawn from the start of the line.
 * Ask the terminal where the cursor is (ESC [ 6 n; it answers
 * ESC [ row ; col R), and start a fresh line if it isn't at the left edge.
 * A terminal that doesn't answer within a moment is left alone.
 *
 * Keys typed before the answer (typing ahead while the last input ran, or
 * a pasted block) come in first: they are kept for read_byte, never lost.
 * An answer that comes too late is an escape sequence le_read ignores. */
static void fresh_line(void)
{
	if (write(1, "\x1b[6n", 4) != 4)
		return;
	char seq[24];      /* a possible answer: ESC [ digits ; digits R */
	int n = 0;
	struct pollfd pfd = { .fd = 0, .events = POLLIN };
	while (poll(&pfd, 1, 200) > 0) {
		char c;
		if (read(0, &c, 1) != 1)
			break;
		bool fits = n == 0 ? c == '\x1b' : n == 1 ? c == '[' : (c >= '0' && c <= '9') || c == ';' || c == 'R';
		if (fits && n < (int)sizeof seq - 1) {
			seq[n++] = c;
			if (c != 'R')
				continue;
			seq[n] = '\0';
			int row, col;
			if (sscanf(seq + 2, "%d;%dR", &row, &col) == 2) {
				if (col > 1)
					(void)!write(1, "\r\n", 2);
				return;
			}
		}
		/* not the answer after all: those bytes were typed keys */
		for (int i = 0; i < n; i++)
			ahead_push(seq[i]);
		n = 0;
		if (c == '\x1b') {
			seq[n++] = c;
			continue;
		}
		ahead_push(c);
	}
	for (int i = 0; i < n; i++)
		ahead_push(seq[i]);
}

static int term_cols(void)
{
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return ws.ws_col;
	return 80;
}

/* ------------------------------------------------------------- editing */

static void line_reserve(Line *L, int extra)
{
	if (L->len + extra + 1 > L->cap) {
		while (L->len + extra + 1 > L->cap)
			L->cap = L->cap ? L->cap * 2 : 128;
		L->buf = realloc(L->buf, (size_t)L->cap);
	}
}

static void line_insert(Line *L, const char *s, int n)
{
	line_reserve(L, n);
	memmove(L->buf + L->pos + n, L->buf + L->pos, (size_t)(L->len - L->pos));
	memcpy(L->buf + L->pos, s, (size_t)n);
	L->len += n;
	L->pos += n;
	L->buf[L->len] = '\0';
	L->untouched = false;
}

static void line_delete(Line *L, int from, int to)
{
	if (from < 0)
		from = 0;
	if (to > L->len)
		to = L->len;
	if (from >= to)
		return;
	memmove(L->buf + from, L->buf + to, (size_t)(L->len - to));
	L->len -= to - from;
	L->buf[L->len] = '\0';
	if (L->pos > to)
		L->pos -= to - from;
	else if (L->pos > from)
		L->pos = from;
	L->untouched = false;
}

static void line_set(Line *L, const char *s)
{
	L->len = 0;
	L->pos = 0;
	int n = (int)strlen(s);
	line_reserve(L, n);
	memcpy(L->buf, s, (size_t)n + 1);
	L->len = L->pos = n;
}

static bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

static int prev_char(const Line *L, int pos)
{
	if (pos <= 0)
		return 0;
	pos--;
	while (pos > 0 && is_cont((unsigned char)L->buf[pos]))
		pos--;
	return pos;
}

static int next_char(const Line *L, int pos)
{
	if (pos >= L->len)
		return L->len;
	pos++;
	while (pos < L->len && is_cont((unsigned char)L->buf[pos]))
		pos++;
	return pos;
}

static int width_of(char c) { return c == '\t' ? TAB_WIDTH : is_cont((unsigned char)c) ? 0 : 1; }

/* Colors, if the session set them (le_set_colors). */
static LeClassify g_classify;
static LeColor g_color;

void le_set_colors(LineEditor *le, LeClassify classify, LeColor color)
{
	(void)le;
	g_classify = classify;
	g_color = color;
}

static LeComplete g_complete;

void le_set_completer(LineEditor *le, LeComplete complete)
{
	(void)le;
	g_complete = complete;
}

static void refresh(Line *L, const char *prompt);

static bool word_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* Tab after a name or a dot: complete it, or list what it could become.
 * Returns false when there was nothing to complete (then Tab is a tab). */
static bool complete(Line *L, const char *prompt)
{
	int start = L->pos;
	char **items = NULL;
	int n = g_complete(L->buf, L->pos, &start, &items);
	if (n <= 0 || (n == 1 && (int)strlen(items[0]) == L->pos - start)) {
		/* nothing fits, or the name is already whole */
		for (int i = 0; i < n; i++)
			free(items[i]);
		free(items);
		return false;
	}
	int typed = L->pos - start;
	/* what every candidate shares */
	int common = (int)strlen(items[0]);
	for (int i = 1; i < n; i++) {
		int k = 0;
		while (k < common && items[i][k] && items[i][k] == items[0][k])
			k++;
		common = k;
	}
	if (common > typed) {
		line_insert(L, items[0] + typed, common - typed);
	} else if (n > 1) {
		/* nothing more to fill in: show them all, in columns, under the line */
		int width = 0;
		for (int i = 0; i < n; i++)
			if ((int)strlen(items[i]) > width)
				width = (int)strlen(items[i]);
		width += 2;
		int cols = term_cols() / width;
		if (cols < 1)
			cols = 1;
		int rows = (n + cols - 1) / cols;
		fputs("\r\n", stdout);
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				int i = c * rows + r;
				if (i < n)
					printf("%-*s", c + 1 < cols && (c + 1) * rows + r < n ? width : 0, items[i]);
			}
			fputs("\r\n", stdout);
		}
		fflush(stdout);
	}
	for (int i = 0; i < n; i++)
		free(items[i]);
	free(items);
	refresh(L, prompt);
	return true;
}

static void refresh(Line *L, const char *prompt)
{
	int pw = (int)strlen(prompt);
	int avail = term_cols() - pw - 1;
	if (avail < 8)
		avail = 8;
	int cx = 0;
	for (int i = 0; i < L->pos; i++)
		cx += width_of(L->buf[i]);
	if (cx - L->scroll >= avail)
		L->scroll = cx - avail + 1;
	if (cx < L->scroll)
		L->scroll = cx;

	unsigned char *cls = NULL;
	if (g_classify && L->len > 0) {
		cls = malloc((size_t)L->len);
		g_classify(L->buf, L->len, cls);
	}
	char *out = malloc((size_t)(pw + avail * 4 + 64) + (cls ? (size_t)avail * 32 + 64 : 0));
	int n = 0;
	int shown = -1;   /* the color last written */
	out[n++] = '\r';
	memcpy(out + n, prompt, (size_t)pw);
	n += pw;
	int col = 0;
	for (int i = 0; i < L->len; i++) {
		char c = L->buf[i];
		int w = width_of(c);
		if (cls && cls[i] != shown && col >= L->scroll && col < L->scroll + avail && c != '\t' && w > 0) {
			const char *code = g_color(cls[i]);
			size_t k = strlen(code);
			memcpy(out + n, code, k);
			n += (int)k;
			shown = cls[i];
		}
		if (c == '\t') {
			for (int k = 0; k < w; k++, col++)
				if (col >= L->scroll && col < L->scroll + avail)
					out[n++] = ' ';
			continue;
		}
		if (w == 0) {   /* continuation byte: goes with its lead byte */
			if (col - 1 >= L->scroll && col - 1 < L->scroll + avail)
				out[n++] = c;
			continue;
		}
		if (col >= L->scroll && col < L->scroll + avail)
			out[n++] = c;
		col++;
	}
	if (shown > 0)
		n += sprintf(out + n, "\x1b[0m");
	free(cls);
	n += sprintf(out + n, "\x1b[K\r");
	if (pw + cx - L->scroll > 0)
		n += sprintf(out + n, "\x1b[%dC", pw + cx - L->scroll);
	fwrite(out, 1, (size_t)n, stdout);
	fflush(stdout);
	free(out);
}

static bool all_tabs(const Line *L)
{
	for (int i = 0; i < L->len; i++)
		if (L->buf[i] != '\t')
			return false;
	return L->len > 0;
}

LeStatus le_read(LineEditor *le, const char *prompt, const char *prefill, bool auto_dedent, char **out)
{
	*out = NULL;
	if (!raw_on())
		return LE_EOF;
	fresh_line();

	Line L = { 0 };
	line_reserve(&L, 0);
	L.buf[0] = '\0';
	if (prefill && !le->pasting)
		line_set(&L, prefill);
	L.untouched = true;

	/* history browsing: index nhist is the line being edited */
	int hidx = le->nhist;
	char *edit = NULL;

	LeStatus status = LE_OK;
	bool last_tab_listed = false;   /* the last key was a Tab that only listed */
	int tab_len = 0, tab_pos = 0;
	refresh(&L, prompt);
	for (;;) {
		int c = read_byte();
		if (c < 0) {
			status = LE_EOF;
			break;
		}
		if (c != 9)
			last_tab_listed = false;
		if (c == '\r' || c == '\n')
			break;
		switch (c) {
		case 1:  L.pos = 0; break;                          /* Ctrl-A */
		case 2:  L.pos = prev_char(&L, L.pos); break;       /* Ctrl-B */
		case 3:                                             /* Ctrl-C */
			status = LE_INTERRUPT;
			le->pasting = false;
			goto done;
		case 4:                                             /* Ctrl-D */
			if (L.len == 0) {
				status = LE_EOF;
				goto done;
			}
			line_delete(&L, L.pos, next_char(&L, L.pos));
			break;
		case 5:  L.pos = L.len; break;                      /* Ctrl-E */
		case 6:  L.pos = next_char(&L, L.pos); break;       /* Ctrl-F */
		case 8:
		case 127:
			line_delete(&L, prev_char(&L, L.pos), L.pos);
			break;
		case 9: {
			/* Tab completes right after a name or a dot. It is a tab when
			 * there's nothing to complete, and when it follows a Tab that
			 * changed nothing (one that listed the choices): Tab, Tab. */
			bool again = last_tab_listed && L.len == tab_len && L.pos == tab_pos;
			last_tab_listed = false;
			if (g_complete && !le->pasting && !again && L.pos > 0 &&
			    (word_char(L.buf[L.pos - 1]) || strchr("./ ", L.buf[L.pos - 1]))) {
				int before = L.len;
				if (complete(&L, prompt)) {
					last_tab_listed = L.len == before;
					tab_len = L.len;
					tab_pos = L.pos;
					continue;
				}
			}
			line_insert(&L, "\t", 1);
			break;
		}
		case 11: line_delete(&L, L.pos, L.len); break;      /* Ctrl-K */
		case 12:                                            /* Ctrl-L */
			fputs("\x1b[H\x1b[2J", stdout);
			break;
		case 14:                                            /* Ctrl-N */
		case 16:                                            /* Ctrl-P */
			goto history;
		case 21: line_delete(&L, 0, L.pos); break;          /* Ctrl-U */
		case 23: {                                          /* Ctrl-W */
			int p = L.pos;
			while (p > 0 && (L.buf[p - 1] == ' ' || L.buf[p - 1] == '\t'))
				p--;
			while (p > 0 && L.buf[p - 1] != ' ' && L.buf[p - 1] != '\t')
				p--;
			line_delete(&L, p, L.pos);
			break;
		}
		case 27: {                                          /* escape sequences */
			int a = read_byte();
			if (a != '[' && a != 'O')
				break;
			int b = read_byte();
			if (a == 'O' || (b >= 'A' && b <= 'Z')) {
				if (b == 'A' || b == 'B') {
					c = b == 'A' ? 16 : 14;
					goto history;
				}
				if (b == 'C') L.pos = next_char(&L, L.pos);
				if (b == 'D') L.pos = prev_char(&L, L.pos);
				if (b == 'H') L.pos = 0;
				if (b == 'F') L.pos = L.len;
				break;
			}
			int num = 0;
			while (b >= '0' && b <= '9') {
				num = num * 10 + (b - '0');
				b = read_byte();
			}
			while (b == ';' || (b >= '0' && b <= '9'))   /* modifiers: ignored */
				b = read_byte();
			if (b != '~')
				break;
			if (num == 1 || num == 7) L.pos = 0;
			else if (num == 4 || num == 8) L.pos = L.len;
			else if (num == 3) line_delete(&L, L.pos, next_char(&L, L.pos));
			else if (num == 200) {
				le->pasting = true;
				if (L.untouched) {   /* a pasted block brings its own indentation */
					L.len = L.pos = 0;
					L.buf[0] = '\0';
				}
			} else if (num == 201) {
				le->pasting = false;
			}
			break;
		}
		default:
			if (c >= 32) {
				if (c == '}' && auto_dedent && !le->pasting && L.pos == L.len && all_tabs(&L))
					line_delete(&L, L.len - 1, L.len);
				char ch = (char)c;
				line_insert(&L, &ch, 1);
			}
			break;
		}
		refresh(&L, prompt);
		continue;

	history:
		if (c == 16 && hidx > 0) {
			if (hidx == le->nhist) {
				free(edit);
				edit = strdup(L.buf);
			}
			hidx--;
			line_set(&L, le->hist[hidx]);
		} else if (c == 14 && hidx < le->nhist) {
			hidx++;
			line_set(&L, hidx == le->nhist ? (edit ? edit : "") : le->hist[hidx]);
		}
		L.scroll = 0;
		refresh(&L, prompt);
	}
done:
	if (status == LE_INTERRUPT)
		fputs("^C", stdout);
	fputs("\r\n", stdout);
	raw_off();
	free(edit);
	if (status == LE_OK)
		*out = L.buf;
	else
		free(L.buf);
	return status;
}
