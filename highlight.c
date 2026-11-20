/* highlight.c — see highlight.h. */
#include "highlight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const keywords[] = {
	"if", "else", "switch", "while", "until", "do", "for", "in", "to", "down", "by",
	"return", "break", "continue", "goto", "case", "default", "sizeof", NULL,
};
static const char *const types[] = {
	"char", "short", "int", "long", "float", "double", "void", "bool", "str",
	"signed", "unsigned", "handler", "list", "link", "lookup", "file", "directory", "process", "address", "timer", "moment", "duration", "date", "time_of_day", "date_time", "weekday",
	"month", "json", "bytes", "element", "html", "xml", "const", "static", "register", "volatile",
	"extern", "struct", "union", "enum", "object", "aka", "callback", "inner", NULL,
};
static const char *const constants[] = { "true", "false", NULL };
static const char *const functions[] = {
	"print", "println", "input", "read_line", "read_char", "read_key", "end_of_input", "stdout", "stderr", "abs", "close_to", "random", "random_seed", "exit", NULL,
};
/* members of strings, numbers, lists and lookups: shown after a '.' */
static const char *const members[] = {
	"length", "copy", "text", "find", "contains", "starts_with", "ends_with", "replace", "trim",
	"upper", "lower", "repeat", "pad_left", "pad_right", "centered", "split", "join", "add", "insert", "remove_at", "take_last", "clear", "sort", "sort_by", "sort_with", "reverse",
	"remove", "keys", "values", "read_line", "at_end",
	"is_int", "is_long", "is_number", "to_int", "to_long", "to_double",
	"call", "is_set", "value", "is_none", "number", "handle", NULL,   /* (handlers, addresses, files) */
};

static void hl_tokens(const char *s, int n, unsigned char *cls, bool *in_comment);

static bool in_list(const char *const *list, const char *s, int n)
{
	for (; *list; list++)
		if ((int)strlen(*list) == n && memcmp(*list, s, (size_t)n) == 0)
			return true;
	return false;
}

/* libraries whose `name.something` is a function: files.read, archives.make */
static const char *const lib_function_names[] = {
	"files", "directories", "shell", "addresses", "events", "time", "dates", "json", "cabby", "networks",
	"html", "xml", "bytes", "sound", "archives", "patterns", "hashes", "databases", "threads", "dbus",
	"desktop", "power", "volume", "system", "users", "mime", "media", "images", "brightness", "printing", "speech",
	"bluetooth", "emails", "secrets", "videos", "os", "linux", "screens", "mouse", "keyboard", "csv", NULL
};

/* and the types among them, by their whole names: threads.worker */
static const char *const lib_type_names[] = {
	"databases.database", "databases.statement", "dbus.bus", "dbus.message", "networks.url", "patterns.pattern", "sound.note",
	"sound.part", "sound.recording", "sound.song", "sound.track", "sound.keyboard", "sound.synthesizer",
	"networks.request", "networks.connection", "networks.server", "images.picture", "threads.worker", NULL
};

static bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }
static bool digit(char c) { return c >= '0' && c <= '9'; }

static void fill(unsigned char *cls, int from, int to, int c);
static void fill(unsigned char *cls, int from, int to, int c)
{
	for (int i = from; i < to; i++)
		cls[i] = (unsigned char)c;
}

/* A string or character literal from s[i] (the quote); gives the end. */
static int literal(const char *s, int n, int i, unsigned char *cls)
{
	char q = s[i];
	cls[i] = HL_CONSTANT;
	int j = i + 1;
	while (j < n && s[j] != q) {
		if (s[j] == '\\' && j + 1 < n) {
			int k = j + 2;
			if (s[j + 1] == 'x')
				while (k < n && k < j + 4 && ((s[k] >= '0' && s[k] <= '9') || (s[k] >= 'a' && s[k] <= 'f') ||
							      (s[k] >= 'A' && s[k] <= 'F')))
					k++;
			fill(cls, j, k, HL_SPECIAL);
			j = k;
			continue;
		}
		if (q == '"' && s[j] == '%' && j + 1 < n) {
			/* a printf code: %d, %-5s, %.2f, %% */
			int k = j + 1;
			while (k < n && strchr("-+ #0123456789.lhL", s[k]))
				k++;
			if (k < n && strchr("diouxXeEfgGcsp%", s[k])) {
				fill(cls, j, k + 1, HL_SPECIAL);
				j = k + 1;
				continue;
			}
		}
		cls[j++] = HL_CONSTANT;
	}
	if (j < n)
		cls[j++] = HL_CONSTANT;
	return j;
}

static int skip_space(const char *s, int n, int i)
{
	while (i < n && (s[i] == ' ' || s[i] == '\t'))
		i++;
	return i;
}

static int word_end(const char *s, int n, int i)
{
	while (i < n && (ident_char(s[i]) || (s[i] == '.' && i + 1 < n && ident_start(s[i + 1]))))
		i++;
	return i;
}

/* A function or method being defined: `int main int Argc ... {`,
 * `void show const {`, `list int evens int n {`, `~window {`. Its name is
 * colored like a function. Lines that open an if/while/object... aren't. */
static void definition(const char *s, int n, unsigned char *cls)
{
	int end = n;
	while (end > 0 && cls[end - 1] == HL_COMMENT)
		end--;
	while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t'))
		end--;
	if (end == 0 || s[end - 1] != '{')
		return;
	for (int i = 0; i < end; i++)
		if (s[i] == '=' && cls[i] == HL_NONE)
			return;   /* `list int Grid = {`: an initializer */
	int i = skip_space(s, n, 0);
	if (i < end && s[i] == '~') {
		int e = word_end(s, n, i + 1);
		fill(cls, i, e, HL_FUNCTION);
		return;
	}
	/* qualifiers, then the return type */
	static const char *const quals[] = { "static", "const", "extern", "volatile", "register", "inner", NULL };
	static const char *const not_defs[] = { "if", "else", "while", "until", "do", "for", "switch", "object", "struct",
						"union", "enum", "aka", "use", "return", "case", "default", NULL };
	int e = word_end(s, n, i);
	if (e == i || in_list(not_defs, s + i, e - i))
		return;
	while (e > i && in_list(quals, s + i, e - i)) {
		i = skip_space(s, n, e);
		e = word_end(s, n, i);
	}
	if (e == i)
		return;
	/* the type: `list` ..., `lookup` and its key ..., a run of type keywords, or one name */
	for (;;) {
		bool lk = e - i == 6 && memcmp(s + i, "lookup", 6) == 0;
		if (!lk && !(e - i == 4 && memcmp(s + i, "list", 4) == 0))
			break;
		i = skip_space(s, n, e);
		e = word_end(s, n, i);
		if (lk) {   /* the key: one word, or signed/unsigned and one word */
			bool sign = (e - i == 6 && memcmp(s + i, "signed", 6) == 0) ||
				    (e - i == 8 && memcmp(s + i, "unsigned", 8) == 0);
			i = skip_space(s, n, e);
			e = word_end(s, n, i);
			if (sign && e > i && cls[i] == HL_TYPE) {
				i = skip_space(s, n, e);
				e = word_end(s, n, i);
			}
		}
	}
	if (cls[i] == HL_TYPE)
		while (e > i && cls[i] == HL_TYPE) {
			i = skip_space(s, n, e);
			e = word_end(s, n, i);
		}
	else {
		i = skip_space(s, n, e);
		e = word_end(s, n, i);
	}
	if (i < end && s[i] == '&') {
		i = skip_space(s, n, i + 1);
		e = word_end(s, n, i);
	}
	if (e > i && ident_start(s[i]) && cls[i] == HL_NONE)
		fill(cls, i, e, HL_FUNCTION);
}

/* `strict long kind duration` and `long kind moment, steps duration`:
 * kind, strict and steps are words only there, so only there colored. */
static void kind_words(const char *s, int n, unsigned char *cls)
{
	int w[16], we[16], nw = 0;
	for (int i = 0; i < n && nw < 16;) {
		if (s[i] == '/' && i + 1 < n && s[i + 1] == '/')
			break;
		if (ident_start(s[i])) {
			int j = i;
			while (j < n && ident_char(s[j]))
				j++;
			w[nw] = i;
			we[nw++] = j;
			i = j;
		} else if (s[i] == ' ' || s[i] == '\t' || s[i] == ',') {
			i++;
		} else {
			return;   /* anything else: not a kind's line */
		}
	}
#define IS(k, word) (we[k] - w[k] == (int)strlen(word) && memcmp(s + w[k], word, strlen(word)) == 0)
	for (int k = 1; k + 1 < nw; k++) {
		if (!IS(k, "kind") || cls[w[k - 1]] != HL_TYPE)
			continue;
		bool ok = k + 2 == nw || (k + 3 < nw && IS(k + 2, "steps"));
		if (!ok)
			return;
		fill(cls, w[k], we[k], HL_TYPE);
		if (IS(0, "strict"))
			fill(cls, w[0], we[0], HL_TYPE);
		if (k + 2 < nw)
			fill(cls, w[k + 2], we[k + 2], HL_TYPE);
		return;
	}
#undef IS
}

void hl_classify(const char *s, int n, unsigned char *cls, bool *in_comment)
{
	bool started_in_comment = *in_comment;
	hl_tokens(s, n, cls, in_comment);
	if (!started_in_comment) {
		definition(s, n, cls);
		kind_words(s, n, cls);
	}
}

static void hl_tokens(const char *s, int n, unsigned char *cls, bool *in_comment)
{
	memset(cls, HL_NONE, (size_t)n);
	int i = 0;
	/* the start of the line: indentation, then perhaps `use` */
	int first = 0;
	while (first < n && (s[first] == '\t' || s[first] == ' '))
		first++;
	while (i < n) {
		if (*in_comment) {
			int j = i;
			while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/'))
				j++;
			if (j + 1 < n) {
				fill(cls, i, j + 2, HL_COMMENT);
				i = j + 2;
				*in_comment = false;
			} else {
				fill(cls, i, n, HL_COMMENT);
				return;
			}
			continue;
		}
		char c = s[i];
		if (c == '/' && i + 1 < n && s[i + 1] == '/') {
			fill(cls, i, n, HL_COMMENT);
			return;
		}
		if (c == '/' && i + 1 < n && s[i + 1] == '*') {
			fill(cls, i, i + 2, HL_COMMENT);
			i += 2;
			*in_comment = true;
			continue;
		}
		if (c == '"' || c == '\'') {
			i = literal(s, n, i, cls);
			continue;
		}
		if (digit(c) || (c == '.' && i + 1 < n && digit(s[i + 1]) && (i == 0 || !ident_char(s[i - 1])))) {
			int j = i;
			while (j < n && (ident_char(s[j]) || s[j] == '.' ||
					 ((s[j] == '-' || s[j] == '+') && (s[j - 1] == 'e' || s[j - 1] == 'E') && s[i] != '0')))
				j++;
			fill(cls, i, j, HL_CONSTANT);
			i = j;
			continue;
		}
		if (ident_start(c)) {
			int j = i;
			while (j < n && ident_char(s[j]))
				j++;
			const char *w = s + i;
			int len = j - i;
			bool after_dot = i > 0 && s[i - 1] == '.';
			int k = HL_NONE;
			if (i == first && len == 3 && memcmp(w, "use", 3) == 0) {
				/* use name / use local name: the name like a string */
				fill(cls, i, j, HL_USE);
				int p = j;
				while (p < n && s[p] == ' ')
					p++;
				if (p + 5 <= n && memcmp(s + p, "local", 5) == 0 && (p + 5 == n || s[p + 5] == ' ')) {
					fill(cls, p, p + 5, HL_USE);
					p += 5;
					while (p < n && s[p] == ' ')
						p++;
				}
				int q = p;
				while (q < n && s[q] != ' ' && s[q] != '\t' && s[q] != '/')
					q++;
				fill(cls, p, q, HL_CONSTANT);
				i = q;
				continue;
			}
			if (after_dot) {
				if (in_list(members, w, len))
					k = HL_SPECIAL;
			} else if (((len == 3 && (memcmp(w, "gui", 3) == 0 || memcmp(w, "tui", 3) == 0 || memcmp(w, "gtk", 3) == 0)) ||
				    (len == 4 && memcmp(w, "game", 4) == 0) || (len == 5 && memcmp(w, "tgame", 5) == 0) ||
				    (len == 5 && memcmp(w, "world", 5) == 0)) &&
				   j + 1 < n && s[j] == '.' &&
				   ident_start(s[j + 1])) {
				/* gui.window, tui.window: a library type */
				int e = j + 1;
				while (e < n && ident_char(s[e]))
					e++;
				fill(cls, i, e, HL_TYPE);
				i = e;
				continue;
			} else if (in_list(lib_function_names, w, len) && j + 1 < n && s[j] == '.' && ident_start(s[j + 1])) {
				/* files.read, archives.make: a library's functions; threads.worker,
				 * dbus.bus: its types */
				int e = j + 1;
				while (e < n && ident_char(s[e]))
					e++;
				fill(cls, i, e, in_list(lib_type_names, s + i, e - i) ? HL_TYPE : HL_FUNCTION);
				i = e;
				continue;
			} else if (in_list(keywords, w, len)) {
				k = HL_KEYWORD;
			} else if (in_list(types, w, len)) {
				k = HL_TYPE;
			} else if (in_list(constants, w, len)) {
				k = HL_CONSTANT;
			} else if (in_list(functions, w, len)) {
				k = HL_FUNCTION;
			} else if (len == 4 && memcmp(w, "self", 4) == 0) {
				k = HL_SPECIAL;
			} else if ((len == 6 && memcmp(w, "cement", 6) == 0) || (len == 4 && memcmp(w, "auto", 4) == 0) ||
				   (len == 8 && memcmp(w, "extended", 8) == 0) || (len == 9 && memcmp(w, "quadruple", 9) == 0) ||
				   (len == 7 && memcmp(w, "typedef", 7) == 0) ||   /* (reserved: Cabby has aka) */
				   (len == 2 && memcmp(w, "is", 2) == 0) || (len == 3 && memcmp(w, "not", 3) == 0) ||
				   (len == 3 && memcmp(w, "and", 3) == 0) || (len == 2 && memcmp(w, "or", 2) == 0)) {
				/* (and the words Cabby had for ==, !, && and ||) */
				k = HL_ERROR;
			}
			fill(cls, i, j, k);
			i = j;
			continue;
		}
		if (c == ';') {
			/* Cabby needs no ';' at the end of a line */
			int j = i + 1;
			while (j < n && (s[j] == ' ' || s[j] == '\t'))
				j++;
			if (j == n || (j + 1 < n && s[j] == '/' && s[j + 1] == '/'))
				cls[i] = HL_ERROR;
		}
		i++;
	}
}

bool hl_comment_open_at(const char *src, const char *upto)
{
	bool in = false;
	const char *line = src;
	while (line < upto) {
		const char *e = line;
		while (e < upto && *e != '\n')
			e++;
		int n = (int)(e - line);
		unsigned char *cls = malloc((size_t)(n ? n : 1));
		hl_classify(line, n, cls, &in);
		free(cls);
		line = e < upto ? e + 1 : e;
	}
	return in;
}

bool hl_enabled(int fd)
{
	if (!isatty(fd))
		return false;
	const char *no = getenv("NO_COLOR");
	if (no && *no)
		return false;
	const char *term = getenv("TERM");
	return !term || strcmp(term, "dumb") != 0;
}

/* Vim's default colors for these groups, in the terminal's 16-color
 * palette: built-ins bold, as Vim draws functions, and comments blue (Vim's
 * light-background color for them) so they never look like built-ins. */
static char g_codes[HL__COUNT][24] = {
	[HL_NONE] = "\x1b[0m",
	[HL_KEYWORD] = "\x1b[33m",
	[HL_TYPE] = "\x1b[32m",
	[HL_CONSTANT] = "\x1b[35m",
	[HL_SPECIAL] = "\x1b[95m",
	[HL_FUNCTION] = "\x1b[1;36m",
	[HL_USE] = "\x1b[94m",
	[HL_COMMENT] = "\x1b[34m",
	[HL_ERROR] = "\x1b[37;41m",
};

static void read_settings(void)
{
	static bool done = false;
	if (done)
		return;
	done = true;
	const char *v = getenv("CABBY_COLORS");
	if (!v)
		return;
	static const char *const names[HL__COUNT] = {
		[HL_KEYWORD] = "keyword", [HL_TYPE] = "type", [HL_CONSTANT] = "constant",
		[HL_SPECIAL] = "special", [HL_FUNCTION] = "function", [HL_USE] = "use",
		[HL_COMMENT] = "comment", [HL_ERROR] = "error",
	};
	while (*v) {
		const char *eq = strchr(v, '='), *end = strchr(v, ':');
		if (!end)
			end = v + strlen(v);
		if (eq && eq < end) {
			for (int c = 1; c < HL__COUNT; c++)
				if ((int)strlen(names[c]) == eq - v && memcmp(names[c], v, (size_t)(eq - v)) == 0) {
					int len = (int)(end - eq - 1);
					bool ok = len > 0 && len < 12;
					for (int k = 0; ok && k < len; k++)
						ok = (eq[1 + k] >= '0' && eq[1 + k] <= '9') || eq[1 + k] == ';';
					if (ok)
						snprintf(g_codes[c], sizeof g_codes[c], "\x1b[0;%.*sm", len, eq + 1);
				}
		}
		v = *end ? end + 1 : end;
	}
}

const char *hl_start(int c)
{
	read_settings();
	return g_codes[c >= 0 && c < HL__COUNT ? c : HL_NONE];
}
