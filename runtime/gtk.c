/* gtk.c — the C side of Cabby's `gtk` library: the gui library's objects
 * and methods, drawn by GTK 4 (`use gtk`). `use gui`, the standard one, is
 * drawn by Cabby itself with SDL 3 (gui.c); this one is kept for those who
 * want GTK's own widgets, or a screen reader.
 *
 * Cabby programs never see GTK: lib/gtk.hby (made from lib/gui.hby by the
 * Makefile) wraps these functions in objects (gtk.window, gtk.button, ...). Everything is handed to Cabby as
 * a small integer id, an index into the table below, so a stale id can
 * never reach freed memory.
 *
 * Handlers: a Cabby handler is a window object and one of its methods,
 * passed here as two pointers. They are called through cby_invoke_hook:
 * compiled programs call the method directly; the interpreter points the
 * hook at a function that interprets it. A handler belonging to a window
 * that has been destroyed is never called.
 *
 * Testing: if CABBY_GUI_SCRIPT is set, windows follow it instead of
 * waiting for a person: commands separated by ';' (see run_script). Dialogs
 * are not shown then; the script answers them instead.
 */
#if defined(__has_include) && __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#else
#include "gtk_min.h"
#endif

#include <dlfcn.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* ------------------------------------------------------------- handlers */

static void call_directly(void *obj, void *fn) { ((void (*)(void *))fn)(obj); }

/* The interpreter replaces this when it loads the library. */
void (*cby_invoke_hook)(void *obj, void *fn) = call_directly;

typedef struct Handler {
	void *obj;
	void *fn;
	int64_t win;   /* the window it belongs to (0: not known) */
} Handler;

static int window_alive(int64_t win);

/* Cabby's runtime, when the program is compiled (the interpreter's hook
 * does this itself): what handlers printed into files is written out, since
 * the window then waits */
extern void rt_flush_files(void) __attribute__((weak));

static void invoke(Handler *h)
{
	if (h->win == 0 || window_alive(h->win))
		cby_invoke_hook(h->obj, h->fn);
	if (rt_flush_files)
		rt_flush_files();
}

/* GTK signals pass one or two arguments before the handler's data. */
static void on_signal(gpointer widget, gpointer data)
{
	(void)widget;
	invoke(data);
}

static void on_signal2(gpointer widget, gpointer arg, gpointer data)
{
	(void)widget;
	(void)arg;
	invoke(data);
}

static void free_handler(gpointer data, void *closure)
{
	(void)closure;
	free(data);
}

/* ------------------------------------------------------------- the table */

enum {
	K_WINDOW = 1, K_BOX, K_LABEL, K_BUTTON, K_ENTRY,
	K_CHECK, K_CHOICE, K_TEXT_AREA, K_LIST, K_SLIDER, K_PROGRESS, K_IMAGE, K_SEPARATOR, K_CANVAS,
	K_MENU, K_MENU_ITEM, K_SHORTCUT, K_TABS, K_TABLE, K_NUMBER, K_COLOR, K_FONT
};

typedef struct Item {
	GtkWidget *w;        /* the widget itself */
	GtkWidget *outer;    /* what was put in the window (a scrolling frame), or w */
	int kind;
	int64_t win;         /* the window it is in */
	GtkWidget *box;      /* windows: their contents */
	int open;            /* windows: shown and not yet closed */
	int destroyed;       /* windows */
	char *text_cache;    /* the last text handed out, for text_char */
	char **strings;      /* drop-downs and lists: their entries */
	int nstrings;
	int is_row;          /* boxes: side by side */
	guint *timers;       /* windows: running timers (0: finished) */
	int ntimers;
	char *chosen;        /* windows: the file a dialog chose */
	/* canvases */
	Handler draw;        /* on_draw (fn NULL: none) */
	int width, height;   /* the size it was added with */
	int mouse_x, mouse_y;
	char key[32];        /* the last key pressed in it (or, for a window, its own keys) */
	Handler clicked, moved, keyed;   /* on_click, on_mouse_move, on_key (fn NULL: none); a window's on_key */
	/* colors of the program's own (set_text_color, set_background) */
	GtkCssProvider *css;
	int has_ink, has_paper;
	GdkRGBA ink, paper;
	/* windows: their menus and shortcuts */
	GtkWidget *frame;    /* windows: holds the menu bar, then the contents */
	GMenu *bar;          /* the menu bar's menus (NULL: none yet) */
	GSimpleActionGroup *actions;
	GtkEventController *keys;   /* the shortcuts */
	/* menus: their items, in sections between separators */
	GMenu *menu, *section;
	char *title;
	/* menu items and shortcuts */
	int64_t in_menu;     /* the menu it's in (0: a shortcut on its own) */
	GSimpleAction *action;
	char *label;
	char *keys_name;     /* its shortcut, written the one way: "ctrl+shift+s" ("": none) */
	int grayed;
	Handler run;
	int adding;          /* tabs: a page is being added */
	/* tables: the cells, row by row (as added: sorting only changes the
	 * order they're shown in), and GTK's models of them */
	struct Row *rows;
	int nrows, caprows;
	int ncols;
	GtkStringList *row_list;   /* each row's number, as text: "0", "1", ... */
	GtkSingleSelection *picked;
	GtkColumnViewColumn **columns;
	int last_row;        /* the row on_select last heard of (sorting moves it, but it's the same row) */
} Item;

typedef struct Row {
	char **cell;
	int n;
} Row;

static const char *cell_text(Item *t, int row, int col);
static int table_selected(Item *t);
static void table_select(Item *t, int row);

static Item *items;
static int nitems, capitems;

static int64_t add_item(GtkWidget *w, int kind, int64_t win)
{
	if (nitems == capitems) {
		capitems = capitems ? capitems * 2 : 64;
		items = realloc(items, sizeof *items * (size_t)capitems);
	}
	g_object_ref(G_OBJECT(w));   /* kept alive while Cabby holds the id */
	items[nitems] = (Item){ .w = w, .outer = w, .kind = kind, .win = win };
	return ++nitems;
}

static const char *kind_name(int kind)
{
	switch (kind) {
	case K_WINDOW:    return "a window";
	case K_BOX:       return "a window, row or column";
	case K_BUTTON:    return "a button";
	case K_ENTRY:     return "a text box";
	case K_CHECK:     return "a check box";
	case K_CHOICE:    return "a drop-down list";
	case K_TEXT_AREA: return "a text area";
	case K_LIST:      return "a list";
	case K_SLIDER:    return "a slider";
	case K_PROGRESS:  return "a progress bar";
	case K_IMAGE:     return "an image";
	case K_CANVAS:    return "a canvas";
	case K_MENU:      return "a menu";
	case K_TABS:      return "tabs";
	case K_TABLE:     return "a table";
	case K_NUMBER:    return "a number box";
	case K_COLOR:    return "a color button";
	case K_FONT:      return "a font button";
	case K_MENU_ITEM: return "a menu item";
	default:          return "a widget";
	}
}

static Item *item(int64_t id, int kind)
{
	if (id < 1 || id > nitems || (kind && items[id - 1].kind != kind)) {
		fprintf(stderr, "gtk: %s used before it was added to a window\n", kind_name(kind));
		exit(1);
	}
	return &items[id - 1];
}

static int window_alive(int64_t win)
{
	return win >= 1 && win <= nitems && !items[win - 1].destroyed;
}

static void add_string(Item *it, const char *s)
{
	it->strings = realloc(it->strings, sizeof *it->strings * (size_t)(it->nstrings + 1));
	it->strings[it->nstrings++] = strdup(s);
}

static void clear_strings(Item *it)
{
	for (int i = 0; i < it->nstrings; i++)
		free(it->strings[i]);
	free(it->strings);
	it->strings = NULL;
	it->nstrings = 0;
}

static int started;

static void start(void)
{
	if (started)
		return;
	if (!gtk_init_check()) {
		fprintf(stderr, "gtk: can't open a window: no display was found (is a graphical session running?)\n");
		exit(1);
	}
	started = 1;
}

static void pump(void)
{
	for (int i = 0; i < 50 && g_main_context_pending(NULL); i++)
		g_main_context_iteration(NULL, FALSE);
}

/* After a window closes: deliver what GTK still has waiting, and send it to
 * the display server now. Wayland (Sway, GNOME) only hears that a window has
 * gone on GTK's next turn round its event loop, and once run has returned
 * there may never be one: the window would stay on screen, frozen, until
 * the program ended (in the interactive session, until the session did). */
static void settle(void)
{
	pump();
	GdkDisplay *d = gdk_display_get_default();
	if (d)
		gdk_display_flush(d);
}

static int scripted(void);

/* ------------------------------------------------------------- windows */

static void stop_timers(Item *it)
{
	for (int i = 0; i < it->ntimers; i++)
		if (it->timers[i]) {
			g_source_remove(it->timers[i]);
			it->timers[i] = 0;
		}
}

static gboolean on_close_request(GtkWindow *w, gpointer data)
{
	(void)w;
	Item *it = &items[(intptr_t)data - 1];
	it->open = 0;
	stop_timers(it);
	it->destroyed = 1;   /* GTK destroys the window after this */
	return FALSE;
}

int64_t cby_gtk_window_new(void)
{
	start();
	GtkWidget *w = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(w), 420, 300);
	GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);   /* a menu bar goes above the box */
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_vexpand(box, TRUE);
	gtk_box_append(GTK_BOX(frame), box);
	gtk_window_set_child(GTK_WINDOW(w), frame);
	int64_t id = add_item(w, K_WINDOW, 0);
	items[id - 1].win = id;
	items[id - 1].box = box;
	items[id - 1].frame = frame;
	g_signal_connect_data(w, "close-request", G_CALLBACK(on_close_request), (gpointer)(intptr_t)id, NULL, 0);
	return id;
}

int64_t cby_gtk_window_box(int64_t win)
{
	GtkWidget *box = item(win, K_WINDOW)->box;
	return add_item(box, K_BOX, win);   /* (add_item may move the table) */
}

void cby_gtk_window_set_title(int64_t win, const char *title)
{
	gtk_window_set_title(GTK_WINDOW(item(win, K_WINDOW)->w), title);
}

void cby_gtk_window_set_size(int64_t win, int width, int height)
{
	gtk_window_set_default_size(GTK_WINDOW(item(win, K_WINDOW)->w), width, height);
}

void cby_gtk_window_close(int64_t win)
{
	Item *it = item(win, K_WINDOW);
	if (it->open)
		gtk_window_close(GTK_WINDOW(it->w));   /* run settles once its loop ends */
}

void cby_gtk_window_destroy(int64_t win)
{
	Item *it = item(win, K_WINDOW);
	if (!it->destroyed) {
		it->destroyed = 1;
		it->open = 0;
		stop_timers(it);
		gtk_window_destroy(GTK_WINDOW(it->w));
		settle();
	}
}

/* ------------------------------------------------------------- adding widgets */

static Item *box_item(int64_t box)
{
	return item(box, K_BOX);
}

/* Puts `outer` into the container and records `w` as a new item. */
static int64_t place(int64_t box, GtkWidget *w, GtkWidget *outer, int kind)
{
	Item *b = box_item(box);
	int64_t win = b->win;
	gtk_box_append(GTK_BOX(b->w), outer);
	int64_t id = add_item(w, kind, win);
	items[id - 1].outer = outer;
	return id;
}

int64_t cby_gtk_add_label(int64_t box, const char *text)
{
	GtkWidget *l = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(l), TRUE);
	return place(box, l, l, K_LABEL);
}

int64_t cby_gtk_add_button(int64_t box, const char *text)
{
	GtkWidget *b = gtk_button_new_with_label(text);
	return place(box, b, b, K_BUTTON);
}

int64_t cby_gtk_add_entry(int64_t box)
{
	GtkWidget *e = gtk_entry_new();
	gtk_widget_set_hexpand(e, TRUE);
	return place(box, e, e, K_ENTRY);
}

static int64_t add_box(int64_t box, GtkOrientation o)
{
	GtkWidget *r = gtk_box_new(o, 8);
	if (o == GTK_ORIENTATION_VERTICAL)
		gtk_widget_set_hexpand(r, TRUE);
	int64_t id = place(box, r, r, K_BOX);
	items[id - 1].is_row = o == GTK_ORIENTATION_HORIZONTAL;
	return id;
}

int64_t cby_gtk_add_row(int64_t box) { return add_box(box, GTK_ORIENTATION_HORIZONTAL); }

static GtkWidget *scrolling(GtkWidget *inner, int height);

/* A column inside a frame that scrolls, when what's in it is too much */
int64_t cby_gtk_add_scrolling_column(int64_t box)
{
	GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(c, 6);
	gtk_widget_set_margin_bottom(c, 6);
	gtk_widget_set_margin_start(c, 6);
	gtk_widget_set_margin_end(c, 6);
	GtkWidget *s = scrolling(c, 120);
	int64_t id = place(box, c, s, K_BOX);
	return id;
}

/* Tabs: pages one at a time, each a column, chosen by its tab */
int64_t cby_gtk_add_tabs(int64_t box)
{
	GtkWidget *n = gtk_notebook_new();
	gtk_widget_set_hexpand(n, TRUE);
	gtk_widget_set_vexpand(n, TRUE);
	return place(box, n, n, K_TABS);
}

int64_t cby_gtk_tabs_add_tab(int64_t tabs, const char *title)
{
	Item *t = item(tabs, K_TABS);
	add_string(t, title);
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(page, 12);
	gtk_widget_set_margin_bottom(page, 12);
	gtk_widget_set_margin_start(page, 12);
	gtk_widget_set_margin_end(page, 12);
	t->adding = 1;
	gtk_notebook_append_page(GTK_NOTEBOOK(t->w), page, gtk_label_new(title));
	items[tabs - 1].adding = 0;
	return add_item(page, K_BOX, t->win);
}
int64_t cby_gtk_add_column(int64_t box) { return add_box(box, GTK_ORIENTATION_VERTICAL); }

int64_t cby_gtk_add_check(int64_t box, const char *text)
{
	GtkWidget *c = gtk_check_button_new_with_label(text);
	return place(box, c, c, K_CHECK);
}

int64_t cby_gtk_add_choice(int64_t box)
{
	const char *none[] = { NULL };
	GtkWidget *d = gtk_drop_down_new_from_strings(none);
	return place(box, d, d, K_CHOICE);
}

static GtkWidget *scrolling(GtkWidget *inner, int height)
{
	GtkWidget *s = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s), inner);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(s), height);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(s), TRUE);
	gtk_widget_set_hexpand(s, TRUE);
	gtk_widget_set_vexpand(s, TRUE);
	return s;
}

int64_t cby_gtk_add_text_area(int64_t box)
{
	GtkWidget *v = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(v), 6);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(v), 6);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(v), 4);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(v), 4);
	return place(box, v, scrolling(v, 100), K_TEXT_AREA);
}

int64_t cby_gtk_add_list(int64_t box)
{
	GtkWidget *l = gtk_list_box_new();
	return place(box, l, scrolling(l, 120), K_LIST);
}

int64_t cby_gtk_add_slider(int64_t box, int min, int max)
{
	if (max < min) {
		int t = min;
		min = max;
		max = t;
	}
	GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max == min ? min + 1 : max, 1);
	gtk_scale_set_digits(GTK_SCALE(s), 0);
	gtk_scale_set_draw_value(GTK_SCALE(s), TRUE);
	gtk_widget_set_hexpand(s, TRUE);
	return place(box, s, s, K_SLIDER);
}

int64_t cby_gtk_add_progress(int64_t box)
{
	GtkWidget *p = gtk_progress_bar_new();
	gtk_widget_set_hexpand(p, TRUE);
	return place(box, p, p, K_PROGRESS);
}

int64_t cby_gtk_add_image(int64_t box, const char *path)
{
	GtkWidget *p = gtk_picture_new_for_filename(path && *path ? path : NULL);
	return place(box, p, p, K_IMAGE);
}

/* A dividing line: across a window or column, upright in a row. */
void cby_gtk_add_separator(int64_t box)
{
	Item *b = box_item(box);
	int in_row = b->is_row;
	gtk_box_append(GTK_BOX(b->w), gtk_separator_new(in_row ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL));
}

/* ------------------------------------------------------------- every widget */

void cby_gtk_set_enabled(int64_t id, int on) { gtk_widget_set_sensitive(item(id, 0)->outer, on != 0); }
void cby_gtk_set_visible(int64_t id, int on) { gtk_widget_set_visible(item(id, 0)->outer, on != 0); }

/* what a screen reader says it is (GTK tells the reader the rest itself) */
void cby_gtk_set_description(int64_t id, const char *text)
{
	GtkWidget *w = item(id, 0)->w;
	gtk_accessible_update_property(GTK_ACCESSIBLE(w), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, text, -1);
}

/* ---- colors of the program's own: CSS of its own for each thing given
 * one (`.cby-5 { ... }`), passed on to what's inside a row, column or
 * window as GTK passes on CSS */

static GdkRGBA color_of(const char *spec);

static void restyle(int64_t id)
{
	Item *it = item(id, 0);
	char cls[32];
	snprintf(cls, sizeof cls, "cby-%lld", (long long)id);
	if (!it->css) {
		it->css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(it->css),
							   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		gtk_widget_add_css_class(it->w, cls);
		if (it->kind == K_BOX && it->win && items[it->win - 1].box == it->w)
			gtk_widget_add_css_class(items[it->win - 1].w, cls);   /* (the window's: all of it) */
	}
	char rules[2048] = "";
	size_t n = 0;
	GdkRGBA ink = it->ink;
	int has_ink = it->has_ink;
	if (!has_ink && it->has_paper) {
		/* a background with no text color: black or white, whichever reads */
		double light = 0.299 * it->paper.red + 0.587 * it->paper.green + 0.114 * it->paper.blue;
		ink = light < 0.5 ? (GdkRGBA){ 0.96f, 0.96f, 0.96f, 1 } : (GdkRGBA){ 0.10f, 0.10f, 0.10f, 1 };
		has_ink = 1;
	}
	if (it->has_paper) {
		char *c = gdk_rgba_to_string(&it->paper);
		n += (size_t)snprintf(rules + n, sizeof rules - n,
				      ".%s, .%s button, .%s entry, .%s text, .%s textview, .%s spinbutton, .%s dropdown > button "
				      "{ background-image: none; background-color: %s; }\n",
				      cls, cls, cls, cls, cls, cls, cls, c);
		g_free(c);
	}
	if (has_ink && n < sizeof rules) {
		char *c = gdk_rgba_to_string(&ink);
		snprintf(rules + n, sizeof rules - n, ".%s, .%s label, .%s text, .%s button, .%s entry { color: %s; }\n",
			 cls, cls, cls, cls, cls, c);
		g_free(c);
	}
#if GTK_CHECK_VERSION(4, 12, 0)
	gtk_css_provider_load_from_string(it->css, rules);
#else
	gtk_css_provider_load_from_data(it->css, rules, -1);
#endif
}

void cby_gtk_set_text_color(int64_t id, const char *c)
{
	Item *it = item(id, 0);
	it->has_ink = c && *c;
	if (it->has_ink)
		it->ink = color_of(c);
	restyle(id);
}

void cby_gtk_set_background(int64_t id, const char *c)
{
	Item *it = item(id, 0);
	it->has_paper = c && *c;
	if (it->has_paper)
		it->paper = color_of(c);
	restyle(id);
}

int64_t cby_gtk_window_key_length(int64_t win) { return (int64_t)strlen(item(win, K_WINDOW)->key); }

char cby_gtk_window_key_char(int64_t win, int64_t i)
{
	const char *t = item(win, K_WINDOW)->key;
	return i >= 0 && i < (int64_t)strlen(t) ? t[i] : 0;
}
void cby_gtk_set_min_size(int64_t id, int width, int height)
{
	gtk_widget_set_size_request(item(id, 0)->outer, width, height);
}

static GtkTextBuffer *buffer_of(Item *it) { return gtk_text_view_get_buffer(GTK_TEXT_VIEW(it->w)); }

static int selected_index(Item *it)
{
	if (it->kind == K_TABS)
		return gtk_notebook_get_current_page(GTK_NOTEBOOK(it->w));
	if (it->kind == K_CHOICE) {
		guint s = gtk_drop_down_get_selected(GTK_DROP_DOWN(it->w));
		return s == GTK_INVALID_LIST_POSITION || (int)s >= it->nstrings ? -1 : (int)s;
	}
	if (it->kind == K_LIST) {
		GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(it->w));
		return row ? gtk_list_box_row_get_index(row) : -1;
	}
	return -1;
}

void cby_gtk_set_text(int64_t id, const char *text)
{
	Item *it = item(id, 0);
	switch (it->kind) {
	case K_LABEL:     gtk_label_set_text(GTK_LABEL(it->w), text); break;
	case K_BUTTON:    gtk_button_set_label(GTK_BUTTON(it->w), text); break;
	case K_ENTRY:     gtk_editable_set_text(GTK_EDITABLE(it->w), text); break;
	case K_CHECK:     gtk_check_button_set_label(GTK_CHECK_BUTTON(it->w), text); break;
	case K_TEXT_AREA: gtk_text_buffer_set_text(buffer_of(it), text, -1); break;
	case K_WINDOW:    gtk_window_set_title(GTK_WINDOW(it->w), text); break;
	default: break;
	}
}

/* Text is handed to Cabby a byte at a time: its length, then each byte. */
static int64_t hand_out(Item *it, char *owned)
{
	free(it->text_cache);
	it->text_cache = owned ? owned : strdup("");
	return (int64_t)strlen(it->text_cache);
}

int64_t cby_gtk_text_length(int64_t id)
{
	Item *it = item(id, 0);
	const char *t = NULL;
	switch (it->kind) {
	case K_LABEL:  t = gtk_label_get_text(GTK_LABEL(it->w)); break;
	case K_BUTTON: t = gtk_button_get_label(GTK_BUTTON(it->w)); break;
	case K_ENTRY:  t = gtk_editable_get_text(GTK_EDITABLE(it->w)); break;
	case K_CHECK:  t = gtk_check_button_get_label(GTK_CHECK_BUTTON(it->w)); break;
	case K_TABLE: {
		/* the chosen row's first cell */
		int s = table_selected(it);
		t = s >= 0 ? cell_text(it, s, 0) : "";
		break;
	}
	case K_CHOICE:
	case K_TABS:
	case K_LIST: {
		int s = selected_index(it);
		t = s >= 0 && s < it->nstrings ? it->strings[s] : "";
		break;
	}
	case K_TEXT_AREA: {
		char *all = NULL;
		g_object_get(buffer_of(it), "text", &all, NULL);
		char *copy = strdup(all ? all : "");
		g_free(all);
		return hand_out(it, copy);
	}
	default: break;
	}
	return hand_out(it, strdup(t ? t : ""));
}

char cby_gtk_text_char(int64_t id, int64_t i)
{
	Item *it = item(id, 0);
	if (!it->text_cache || i < 0 || i >= (int64_t)strlen(it->text_cache))
		return 0;
	return it->text_cache[i];
}

/* ------------------------------------------------------------- particular widgets */

int cby_gtk_check_get(int64_t id) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(item(id, K_CHECK)->w)) != 0; }
void cby_gtk_check_set(int64_t id, int on) { gtk_check_button_set_active(GTK_CHECK_BUTTON(item(id, K_CHECK)->w), on != 0); }

void cby_gtk_choice_add(int64_t id, const char *text)
{
	Item *it = item(id, K_CHOICE);
	add_string(it, text);
	gtk_string_list_append(GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(it->w))), text);
}

int cby_gtk_selected(int64_t id)
{
	Item *it = item(id, 0);
	if (it->kind == K_TABLE)
		return table_selected(it);
	if (it->kind != K_CHOICE && it->kind != K_LIST && it->kind != K_TABS)
		return -1;
	return selected_index(it);
}

void cby_gtk_select(int64_t id, int index)
{
	Item *it = item(id, 0);
	if (it->kind == K_CHOICE) {
		gtk_drop_down_set_selected(GTK_DROP_DOWN(it->w),
					   index >= 0 && index < it->nstrings ? (guint)index : GTK_INVALID_LIST_POSITION);
	} else if (it->kind == K_TABS) {
		if (index >= 0 && index < it->nstrings)
			gtk_notebook_set_current_page(GTK_NOTEBOOK(it->w), index);
	} else if (it->kind == K_TABLE) {
		table_select(it, index);
	} else if (it->kind == K_LIST) {
		GtkListBoxRow *row = index >= 0 ? gtk_list_box_get_row_at_index(GTK_LIST_BOX(it->w), index) : NULL;
		if (row)
			gtk_list_box_select_row(GTK_LIST_BOX(it->w), row);
		else
			gtk_list_box_unselect_all(GTK_LIST_BOX(it->w));
	}
}

int cby_gtk_count(int64_t id)
{
	Item *it = item(id, 0);
	return it->nstrings;
}

void cby_gtk_list_add(int64_t id, const char *text)
{
	Item *it = item(id, K_LIST);
	add_string(it, text);
	GtkWidget *l = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_widget_set_margin_top(l, 4);
	gtk_widget_set_margin_bottom(l, 4);
	gtk_widget_set_margin_start(l, 8);
	gtk_widget_set_margin_end(l, 8);
	gtk_list_box_append(GTK_LIST_BOX(it->w), l);
}

void cby_gtk_list_clear(int64_t id)
{
	Item *it = item(id, K_LIST);
	clear_strings(it);
	gtk_list_box_remove_all(GTK_LIST_BOX(it->w));
}

void cby_gtk_text_area_set_editable(int64_t id, int on)
{
	gtk_text_view_set_editable(GTK_TEXT_VIEW(item(id, K_TEXT_AREA)->w), on != 0);
}

int cby_gtk_slider_get(int64_t id)
{
	double v = gtk_range_get_value(GTK_RANGE(item(id, K_SLIDER)->w));
	return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

void cby_gtk_slider_set(int64_t id, int value) { gtk_range_set_value(GTK_RANGE(item(id, K_SLIDER)->w), value); }

void cby_gtk_progress_set(int64_t id, double fraction)
{
	if (fraction < 0)
		fraction = 0;
	if (fraction > 1)
		fraction = 1;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item(id, K_PROGRESS)->w), fraction);
}

/* An empty path clears the picture. */
void cby_gtk_image_set(int64_t id, const char *path)
{
	gtk_picture_set_filename(GTK_PICTURE(item(id, K_IMAGE)->w), path && *path ? path : NULL);
}

/* ------------------------------------------------------------- canvases
 *
 * A canvas is drawn by its on_draw handler, whenever GTK needs it drawn
 * (and after `redraw`). The drawing calls work only while that handler
 * runs: they draw on `drawing`, the cairo context GTK handed over. Each
 * drawing starts on white, in black, with lines 1 pixel wide. */

/* Cabby's panic, when the program has its runtime (it always does: the
 * interpreter has it, and compiled programs are linked with it) */
extern _Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...) __attribute__((weak));

static _Noreturn void fail(const char *fmt, const char *a)
{
	if (rt_panic)
		rt_panic(NULL, 0, 0, fmt, a);
	fprintf(stderr, "gtk: ");
	fprintf(stderr, fmt, a);
	fputc('\n', stderr);
	exit(101);
}

static cairo_t *drawing;       /* while an on_draw handler runs */
static int64_t drawing_id;
static double font_size = 14;

static void draw_canvas(int64_t id, cairo_t *cr, int width, int height)
{
	cairo_t *was = drawing;
	int64_t was_id = drawing_id;
	double was_size = font_size;
	cairo_save(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_set_line_width(cr, 1);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	font_size = 14;
	cairo_set_font_size(cr, font_size);
	drawing = cr;
	drawing_id = id;
	Handler h = items[id - 1].draw;   /* (a copy: the handler may add to the table) */
	if (h.fn)
		invoke(&h);
	drawing = was;
	drawing_id = was_id;
	font_size = was_size;
	cairo_restore(cr);
}

static void on_canvas_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
	(void)area;
	int64_t id = (int64_t)(intptr_t)data;
	if (id >= 1 && id <= nitems && window_alive(items[id - 1].win))
		draw_canvas(id, cr, width, height);
}

/* the context to draw on, or a panic: drawing works only in on_draw */
static cairo_t *pen(int64_t id)
{
	item(id, K_CANVAS);
	if (!drawing || drawing_id != id)
		fail("%s", "a canvas can only be drawn on in its on_draw handler (to draw again, call its redraw)");
	return drawing;
}

int64_t cby_gtk_add_canvas(int64_t box, int width, int height)
{
	GtkWidget *a = gtk_drawing_area_new();
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a), width);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a), height);
	gtk_widget_set_focusable(a, TRUE);
	gtk_widget_set_halign(a, GTK_ALIGN_START);   /* its own size, not stretched to fit */
	gtk_widget_set_valign(a, GTK_ALIGN_START);
	int64_t id = place(box, a, a, K_CANVAS);
	items[id - 1].width = width;
	items[id - 1].height = height;
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a), on_canvas_draw, (gpointer)(intptr_t)id, NULL);
	return id;
}

int cby_gtk_canvas_width(int64_t id) { return item(id, K_CANVAS)->width; }
int cby_gtk_canvas_height(int64_t id) { return item(id, K_CANVAS)->height; }

void cby_gtk_canvas_on_draw(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, K_CANVAS);
	it->draw = (Handler){ obj, fn, it->win };
	gtk_widget_queue_draw(it->w);
}

void cby_gtk_canvas_redraw(int64_t id) { gtk_widget_queue_draw(item(id, K_CANVAS)->w); }

static GdkRGBA color_of(const char *spec)
{
	GdkRGBA c;
	if (!gdk_rgba_parse(&c, spec))
		fail("'%s' isn't a color (write a name, such as \"red\" or \"steelblue\", or \"#3366cc\")", spec);
	return c;
}

void cby_gtk_canvas_set_color(int64_t id, const char *color)
{
	cairo_t *cr = pen(id);
	GdkRGBA c = color_of(color);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
}

/* the whole canvas, in one color (the current color stays as it was) */
void cby_gtk_canvas_fill_with(int64_t id, const char *color)
{
	cairo_t *cr = pen(id);
	GdkRGBA c = color_of(color);
	Item *it = item(id, K_CANVAS);
	cairo_save(cr);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
	cairo_rectangle(cr, 0, 0, it->width, it->height);
	cairo_fill(cr);
	cairo_restore(cr);
}

void cby_gtk_canvas_set_line_width(int64_t id, double width)
{
	cairo_set_line_width(pen(id), width > 0 ? width : 1);
}

void cby_gtk_canvas_set_font_size(int64_t id, double size)
{
	font_size = size > 0 ? size : 14;
	cairo_set_font_size(pen(id), font_size);
}

/* "Serif Bold 16": the family, bold or italic, and the size in pixels */
void cby_gtk_canvas_set_font(int64_t id, const char *font)
{
	cairo_t *cr = pen(id);
	PangoFontDescription *d = pango_font_description_from_string(font);
	const char *family = pango_font_description_get_family(d);
	cairo_select_font_face(cr, family && *family ? family : "Sans",
			       pango_font_description_get_style(d) == PANGO_STYLE_NORMAL ? CAIRO_FONT_SLANT_NORMAL
			       : pango_font_description_get_style(d) == PANGO_STYLE_ITALIC ? CAIRO_FONT_SLANT_ITALIC
										    : CAIRO_FONT_SLANT_OBLIQUE,
			       pango_font_description_get_weight(d) >= PANGO_WEIGHT_SEMIBOLD ? CAIRO_FONT_WEIGHT_BOLD
											     : CAIRO_FONT_WEIGHT_NORMAL);
	int size = pango_font_description_get_size(d);
	if (size > 0) {
		font_size = (double)size / PANGO_SCALE;
		cairo_set_font_size(cr, font_size);
	}
	pango_font_description_free(d);
}

void cby_gtk_canvas_line(int64_t id, double x1, double y1, double x2, double y2)
{
	cairo_t *cr = pen(id);
	cairo_new_path(cr);
	cairo_move_to(cr, x1, y1);
	cairo_line_to(cr, x2, y2);
	cairo_stroke(cr);
}

static void finish(cairo_t *cr, int filled)
{
	if (filled)
		cairo_fill(cr);
	else
		cairo_stroke(cr);
}

void cby_gtk_canvas_rectangle(int64_t id, double x, double y, double w, double h, int filled)
{
	cairo_t *cr = pen(id);
	cairo_new_path(cr);
	cairo_rectangle(cr, x, y, w, h);
	finish(cr, filled);
}

void cby_gtk_canvas_ellipse(int64_t id, double x, double y, double rx, double ry, int filled)
{
	cairo_t *cr = pen(id);
	if (rx <= 0 || ry <= 0)
		return;
	cairo_new_path(cr);
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, rx, ry);
	cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
	cairo_restore(cr);   /* (so a line's width isn't stretched with it) */
	finish(cr, filled);
}

/* Angles in degrees as on a clock: 0 at the top, going clockwise */
void cby_gtk_canvas_arc(int64_t id, double x, double y, double r, double from, double to)
{
	cairo_t *cr = pen(id);
	if (r <= 0)
		return;
	while (to < from)
		to += 360;
	cairo_new_path(cr);
	cairo_arc(cr, x, y, r, (from - 90) * M_PI / 180, (to - 90) * M_PI / 180);
	cairo_stroke(cr);
}

/* A polygon's corners arrive one at a time, then it's drawn */
static double *corners;
static int ncorners, capcorners;

void cby_gtk_canvas_corner(int64_t id, double x, double y)
{
	(void)pen(id);
	if (ncorners == capcorners) {
		capcorners = capcorners ? capcorners * 2 : 16;
		corners = realloc(corners, sizeof *corners * 2 * (size_t)capcorners);
	}
	corners[2 * ncorners] = x;
	corners[2 * ncorners + 1] = y;
	ncorners++;
}

void cby_gtk_canvas_polygon(int64_t id, int filled, int64_t numbers)
{
	cairo_t *cr = pen(id);
	if (numbers % 2 != 0 || numbers < 6) {
		ncorners = 0;
		fail("%s", numbers % 2 != 0 ? "a polygon's corners come in pairs: {x, y, x, y, ...}"
					    : "a polygon needs at least 3 corners: {x, y, x, y, x, y}");
	}
	cairo_new_path(cr);
	for (int i = 0; i < ncorners; i++)
		(i ? cairo_line_to : cairo_move_to)(cr, corners[2 * i], corners[2 * i + 1]);
	if (ncorners > 0)
		cairo_close_path(cr);
	ncorners = 0;
	finish(cr, filled);
}

/* Text with its top left corner at x, y */
void cby_gtk_canvas_text(int64_t id, double x, double y, const char *text)
{
	cairo_t *cr = pen(id);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	cairo_new_path(cr);
	cairo_move_to(cr, x, y + fe.ascent);
	cairo_show_text(cr, text);
	cairo_new_path(cr);
}

/* Pictures, loaded once each and kept */
typedef struct Picture {
	char *path;
	cairo_surface_t *surface;
	unsigned char *data;
} Picture;
static Picture *pictures;
static int npictures;

static cairo_surface_t *picture(const char *path)
{
	for (int i = 0; i < npictures; i++)
		if (strcmp(pictures[i].path, path) == 0)
			return pictures[i].surface;
	GError *err = NULL;
	GdkTexture *t = gdk_texture_new_from_filename(path, &err);
	if (err)
		g_error_free(err);
	if (!t)
		fail("can't show the picture '%s': it isn't there, or isn't a picture", path);
	int w = gdk_texture_get_width(t), h = gdk_texture_get_height(t);
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
	unsigned char *data = malloc((size_t)stride * (size_t)h);
	gdk_texture_download(t, data, (gsize)stride);
	g_object_unref(t);
	pictures = realloc(pictures, sizeof *pictures * (size_t)(npictures + 1));
	pictures[npictures] = (Picture){ strdup(path),
		cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride), data };
	return pictures[npictures++].surface;
}

void cby_gtk_canvas_image(int64_t id, double x, double y, const char *path)
{
	cairo_t *cr = pen(id);
	cairo_surface_t *p = picture(path);
	cairo_save(cr);
	cairo_set_source_surface(cr, p, x, y);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* a picture of the images library's: a cairo surface kept for each shown,
 * its pixels copied again only once they've changed (cairo's are
 * premultiplied, and in the computer's own order: BGRA on Intel and Apple's
 * processors alike) */
extern const uint8_t *cby_image_pixels(int64_t id, int *w, int *h, uint64_t *serial, uint64_t *version)
	__attribute__((weak));

typedef struct {
	uint64_t serial, version, used;
	cairo_surface_t *surface;
	int w, h;
} PictureShown;

static PictureShown pictures_shown[64];
static uint64_t pictures_clock;

void cby_gtk_canvas_picture(int64_t id, double x, double y, int64_t pic)
{
	cairo_t *cr = pen(id);
	int w = 0, h = 0;
	uint64_t serial = 0, version = 0;
	const uint8_t *px = cby_image_pixels ? cby_image_pixels(pic, &w, &h, &serial, &version) : NULL;
	if (!px)
		fail("%s", "that picture is empty: it was handed on, or never made");
	PictureShown *s = NULL, *oldest = &pictures_shown[0];
	for (int i = 0; i < 64; i++) {
		if (pictures_shown[i].surface && pictures_shown[i].serial == serial) {
			s = &pictures_shown[i];
			break;
		}
		if (pictures_shown[i].used < oldest->used)
			oldest = &pictures_shown[i];
	}
	if (s && (s->w != w || s->h != h)) {
		cairo_surface_destroy(s->surface);
		s->surface = NULL;
	}
	if (!s || !s->surface) {
		if (!s) {
			s = oldest;
			if (s->surface)
				cairo_surface_destroy(s->surface);
		}
		*s = (PictureShown){ serial, version - 1, 0, cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h), w, h };
	}
	if (s->version != version) {
		cairo_surface_flush(s->surface);
		unsigned char *d = cairo_image_surface_get_data(s->surface);
		int stride = cairo_image_surface_get_stride(s->surface);
		for (int r = 0; r < h; r++) {
			uint32_t *row = (uint32_t *)(d + (size_t)r * (size_t)stride);
			for (int c = 0; c < w; c++) {
				const uint8_t *p = px + ((size_t)r * (size_t)w + (size_t)c) * 4;
				uint32_t a = p[3];
				row[c] = a << 24 | (p[0] * a / 255) << 16 | (p[1] * a / 255) << 8 | (p[2] * a / 255);
			}
		}
		cairo_surface_mark_dirty(s->surface);
		s->version = version;
	}
	s->used = ++pictures_clock;
	cairo_save(cr);
	cairo_set_source_surface(cr, s->surface, x, y);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* What on_draw draws, saved as a PNG file */
void cby_gtk_canvas_save(int64_t id, const char *path)
{
	Item *it = item(id, K_CANVAS);
	if (drawing)
		fail("%s", "a canvas can't be saved while it's being drawn (save it from another handler)");
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, it->width, it->height);
	cairo_t *cr = cairo_create(s);
	draw_canvas(id, cr, it->width, it->height);
	cairo_destroy(cr);
	cairo_status_t st = cairo_surface_write_to_png(s, path);
	cairo_surface_destroy(s);
	if (st != CAIRO_STATUS_SUCCESS)
		fail("can't save the canvas as '%s'", path);
}

/* ---- the mouse and keys */

int cby_gtk_canvas_mouse_x(int64_t id) { return item(id, K_CANVAS)->mouse_x; }
int cby_gtk_canvas_mouse_y(int64_t id) { return item(id, K_CANVAS)->mouse_y; }

int64_t cby_gtk_canvas_key_length(int64_t id)
{
	Item *it = item(id, K_CANVAS);
	return hand_out(it, strdup(it->key));
}

/* The handlers are kept with the canvas (so a test script can call them
 * too); GTK's controllers just say which canvas. */
static void canvas_event(int64_t id, int which, double x, double y)
{
	if (id < 1 || id > nitems || !window_alive(items[id - 1].win))
		return;
	Item *it = &items[id - 1];
	if (which != 2) {
		it->mouse_x = (int)floor(x);
		it->mouse_y = (int)floor(y);
	}
	Handler h = which == 0 ? it->clicked : which == 1 ? it->moved : it->keyed;
	if (h.fn)
		invoke(&h);
}

static void on_canvas_press(GtkGesture *g, int n, double x, double y, gpointer data)
{
	(void)g;
	(void)n;
	int64_t id = (int64_t)(intptr_t)data;
	if (id >= 1 && id <= nitems)
		gtk_widget_grab_focus(items[id - 1].w);   /* keys go to what was clicked */
	canvas_event(id, 0, x, y);
}

static void on_canvas_motion(GtkEventController *m, double x, double y, gpointer data)
{
	(void)m;
	canvas_event((int64_t)(intptr_t)data, 1, x, y);
}

/* A key's name, as read_key gives it: a character as itself ("a", " "),
 * others by name ("up", "enter", "f1"). "" for keys on their own, like
 * Shift. */
static void key_name(guint keyval, char *out, size_t size)
{
	static const struct { const char *gdk, *cabby; } names[] = {
		{ "Return", "enter" }, { "KP_Enter", "enter" }, { "Tab", "tab" }, { "ISO_Left_Tab", "tab" },
		{ "BackSpace", "backspace" }, { "Escape", "escape" }, { "Up", "up" }, { "Down", "down" },
		{ "Left", "left" }, { "Right", "right" }, { "Home", "home" }, { "End", "end" },
		{ "Insert", "insert" }, { "Delete", "delete" }, { "Page_Up", "page_up" }, { "Page_Down", "page_down" },
		{ "KP_Up", "up" }, { "KP_Down", "down" }, { "KP_Left", "left" }, { "KP_Right", "right" },
	};
	out[0] = '\0';
	const char *n = gdk_keyval_name(keyval);
	if (n) {
		for (size_t i = 0; i < sizeof names / sizeof *names; i++)
			if (strcmp(n, names[i].gdk) == 0) {
				snprintf(out, size, "%s", names[i].cabby);
				return;
			}
		if (n[0] == 'F' && n[1] >= '1' && n[1] <= '9' && (!n[2] || (n[2] >= '0' && n[2] <= '2' && !n[3]))) {
			snprintf(out, size, "f%s", n + 1);
			return;
		}
	}
	guint32 u = gdk_keyval_to_unicode(keyval);
	if (u < 32 || u == 127)
		return;
	/* as UTF-8 */
	unsigned char *o = (unsigned char *)out;
	if (u < 0x80) {
		o[0] = (unsigned char)u;
		o[1] = 0;
	} else if (u < 0x800) {
		o[0] = (unsigned char)(0xC0 | (u >> 6));
		o[1] = (unsigned char)(0x80 | (u & 0x3F));
		o[2] = 0;
	} else if (u < 0x10000) {
		o[0] = (unsigned char)(0xE0 | (u >> 12));
		o[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
		o[2] = (unsigned char)(0x80 | (u & 0x3F));
		o[3] = 0;
	} else {
		o[0] = (unsigned char)(0xF0 | (u >> 18));
		o[1] = (unsigned char)(0x80 | ((u >> 12) & 0x3F));
		o[2] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
		o[3] = (unsigned char)(0x80 | (u & 0x3F));
		o[4] = 0;
	}
}

static void canvas_key(int64_t id, const char *name)
{
	if (id < 1 || id > nitems)
		return;
	snprintf(items[id - 1].key, sizeof items[id - 1].key, "%s", name);
	canvas_event(id, 2, 0, 0);
}

static gboolean on_canvas_key(GtkEventController *k, guint keyval, guint keycode, GdkModifierType state,
			      gpointer data)
{
	(void)k;
	(void)keycode;
	(void)state;
	char name[16];
	key_name(keyval, name, sizeof name);
	if (!name[0] || strcmp(name, "tab") == 0)
		return FALSE;   /* Tab still moves between things */
	canvas_key((int64_t)(intptr_t)data, name);
	return TRUE;
}

void cby_gtk_canvas_on_click(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, K_CANVAS);
	if (!it->clicked.fn) {
		GtkGesture *g = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);   /* any button */
		gtk_widget_add_controller(it->w, GTK_EVENT_CONTROLLER(g));
		g_signal_connect_data(g, "pressed", G_CALLBACK(on_canvas_press), (gpointer)(intptr_t)id, NULL, 0);
	}
	it->clicked = (Handler){ obj, fn, it->win };
}

void cby_gtk_canvas_on_mouse_move(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, K_CANVAS);
	if (!it->moved.fn) {
		GtkEventController *m = gtk_event_controller_motion_new();
		gtk_widget_add_controller(it->w, m);
		g_signal_connect_data(m, "motion", G_CALLBACK(on_canvas_motion), (gpointer)(intptr_t)id, NULL, 0);
	}
	it->moved = (Handler){ obj, fn, it->win };
}

/* a key that reached the window: nothing in it used it */
static gboolean on_window_key(GtkEventController *k, guint keyval, guint keycode, GdkModifierType state, gpointer data)
{
	(void)k;
	(void)keycode;
	int64_t win = (int64_t)(intptr_t)data;
	char name[16];
	key_name(keyval, name, sizeof name);
	if (!name[0] || strcmp(name, "tab") == 0 || win < 1 || win > nitems)
		return FALSE;   /* Tab still moves between things */
	Item *w = &items[win - 1];
	snprintf(w->key, sizeof w->key, "%s%s%s", (state & GDK_CONTROL_MASK) ? "ctrl+" : "", (state & GDK_ALT_MASK) ? "alt+" : "", name);
	if (!w->keyed.fn)
		return FALSE;
	invoke(&w->keyed);
	return TRUE;
}

void cby_gtk_window_on_key(int64_t win, void *obj, void *fn)
{
	Item *w = item(win, K_WINDOW);
	if (!w->keyed.fn) {
		GtkEventController *k = gtk_event_controller_key_new();
		gtk_widget_add_controller(w->w, k);
		g_signal_connect_data(k, "key-pressed", G_CALLBACK(on_window_key), (gpointer)(intptr_t)win, NULL, 0);
	}
	w->keyed = (Handler){ obj, fn, win };
}

void cby_gtk_canvas_on_key(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, K_CANVAS);
	if (!it->keyed.fn) {
		GtkEventController *k = gtk_event_controller_key_new();
		gtk_widget_add_controller(it->w, k);
		g_signal_connect_data(k, "key-pressed", G_CALLBACK(on_canvas_key), (gpointer)(intptr_t)id, NULL, 0);
	}
	it->keyed = (Handler){ obj, fn, it->win };
}

/* ------------------------------------------------------------- tables
 *
 * A GtkColumnView. Its rows are a list of row numbers as text ("0", "1",
 * ...), sorted by GTK when a heading is clicked; each cell's label is
 * filled from the cells kept here, so `selected` and `cell` always count
 * rows in the order they were added. */

typedef struct Column {
	int64_t table;
	int col;
} Column;

static const char *cell_text(Item *t, int row, int col)
{
	if (row < 0 || row >= t->nrows || col < 0 || col >= t->rows[row].n)
		return "";
	return t->rows[row].cell[col];
}

static int row_of_item(gpointer obj)
{
	return obj ? atoi(gtk_string_object_get_string(GTK_STRING_OBJECT(obj))) : -1;
}

static void on_cell_setup(gpointer factory, gpointer list_item, gpointer data)
{
	(void)factory;
	(void)data;
	GtkWidget *l = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_list_item_set_child(GTK_LIST_ITEM(list_item), l);
}

static void on_cell_bind(gpointer factory, gpointer list_item, gpointer data)
{
	(void)factory;
	Column *c = data;
	Item *t = &items[c->table - 1];
	int row = row_of_item(gtk_list_item_get_item(GTK_LIST_ITEM(list_item)));
	gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(GTK_LIST_ITEM(list_item))), cell_text(t, row, c->col));
}

/* Numbers as numbers (9 before 10), the rest as text, ignoring capitals */
static int compare_text(const char *a, const char *b)
{
	char *ea, *eb;
	double x = strtod(a, &ea), y = strtod(b, &eb);
	if (*a && *b && !*ea && !*eb)
		return x < y ? -1 : x > y ? 1 : 0;
	return strcasecmp(a, b);
}

static int compare_rows(gconstpointer a, gconstpointer b, gpointer data)
{
	Column *c = data;
	Item *t = &items[c->table - 1];
	int r = compare_text(cell_text(t, row_of_item((gpointer)a), c->col), cell_text(t, row_of_item((gpointer)b), c->col));
	return r < 0 ? -1 : r > 0 ? 1 : 0;
}

int64_t cby_gtk_add_table(int64_t box)
{
	GtkStringList *rows = gtk_string_list_new(NULL);
	GtkWidget *v = gtk_column_view_new(NULL);
	g_object_ref(rows);
	GtkSortListModel *sorted = gtk_sort_list_model_new(G_LIST_MODEL(rows), g_object_ref(gtk_column_view_get_sorter(GTK_COLUMN_VIEW(v))));
	GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(sorted));
	gtk_single_selection_set_autoselect(sel, FALSE);
	gtk_single_selection_set_can_unselect(sel, TRUE);
	gtk_single_selection_set_selected(sel, GTK_INVALID_LIST_POSITION);
	gtk_column_view_set_model(GTK_COLUMN_VIEW(v), GTK_SELECTION_MODEL(sel));
	int64_t id = place(box, v, scrolling(v, 160), K_TABLE);
	items[id - 1].row_list = rows;
	items[id - 1].picked = sel;
	items[id - 1].last_row = -1;
	return id;
}

void cby_gtk_table_heading(int64_t id, const char *heading)
{
	Item *t = item(id, K_TABLE);
	Column *c = malloc(sizeof *c);
	c->table = id;
	c->col = t->ncols;
	GtkListItemFactory *f = gtk_signal_list_item_factory_new();
	g_signal_connect_data(f, "setup", G_CALLBACK(on_cell_setup), NULL, NULL, 0);
	g_signal_connect_data(f, "bind", G_CALLBACK(on_cell_bind), c, NULL, 0);
	GtkColumnViewColumn *col = gtk_column_view_column_new(heading, f);
	gtk_column_view_column_set_expand(col, TRUE);
	GtkCustomSorter *sorter = gtk_custom_sorter_new(compare_rows, c, NULL);
	gtk_column_view_column_set_sorter(col, GTK_SORTER(sorter));
	g_object_unref(sorter);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(t->w), col);
	t->columns = realloc(t->columns, sizeof *t->columns * (size_t)(t->ncols + 1));
	t->columns[t->ncols++] = col;
	add_string(t, heading);   /* (the headings, for a test script's `sort`) */
}

/* a row arrives a cell at a time: start, cell, cell ..., then end */
void cby_gtk_table_row_start(int64_t id)
{
	Item *t = item(id, K_TABLE);
	if (t->nrows == t->caprows) {
		t->caprows = t->caprows ? t->caprows * 2 : 16;
		t->rows = realloc(t->rows, sizeof *t->rows * (size_t)t->caprows);
	}
	t->rows[t->nrows++] = (Row){ NULL, 0 };
}

void cby_gtk_table_row_cell(int64_t id, const char *text)
{
	Item *t = item(id, K_TABLE);
	Row *r = &t->rows[t->nrows - 1];
	r->cell = realloc(r->cell, sizeof *r->cell * (size_t)(r->n + 1));
	r->cell[r->n++] = strdup(text);
}

void cby_gtk_table_row_end(int64_t id)
{
	Item *t = item(id, K_TABLE);
	char num[24];
	snprintf(num, sizeof num, "%d", t->nrows - 1);
	const char *one[] = { num, NULL };
	gtk_string_list_splice(t->row_list, (guint)(t->nrows - 1), 0, one);
}

static int table_selected(Item *t)
{
	guint pos = gtk_single_selection_get_selected(t->picked);
	if (pos == GTK_INVALID_LIST_POSITION)
		return -1;
	gpointer obj = g_list_model_get_item(G_LIST_MODEL(t->picked), pos);
	int row = row_of_item(obj);
	if (obj)
		g_object_unref(obj);
	return row;
}

static void table_select(Item *t, int row)
{
	guint n = g_list_model_get_n_items(G_LIST_MODEL(t->picked)), at = GTK_INVALID_LIST_POSITION;
	for (guint i = 0; i < n && at == GTK_INVALID_LIST_POSITION; i++) {
		gpointer obj = g_list_model_get_item(G_LIST_MODEL(t->picked), i);
		if (row_of_item(obj) == row)
			at = i;
		if (obj)
			g_object_unref(obj);
	}
	gtk_single_selection_set_selected(t->picked, at);
}

/* GTK's list of rows made again (after a change), keeping what was selected */
static void table_refresh(Item *t, int keep)
{
	t->adding = 1;   /* (quiet: the rows are the same rows) */
	char **nums = malloc(sizeof *nums * (size_t)(t->nrows + 1));
	for (int i = 0; i < t->nrows; i++) {
		char num[24];
		snprintf(num, sizeof num, "%d", i);
		nums[i] = strdup(num);
	}
	nums[t->nrows] = NULL;
	gtk_string_list_splice(t->row_list, 0, g_list_model_get_n_items(G_LIST_MODEL(t->row_list)),
			       (const char *const *)nums);
	for (int i = 0; i < t->nrows; i++)
		free(nums[i]);
	free(nums);
	table_select(t, keep);
	t->last_row = table_selected(t);
	t->adding = 0;
}

static void table_fail(const char *what, int row)
{
	char n[24];
	snprintf(n, sizeof n, "%d", row);
	fail(what, n);
}

void cby_gtk_table_set_cell(int64_t id, int row, int col, const char *text)
{
	Item *t = item(id, K_TABLE);
	if (row < 0 || row >= t->nrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	if (col < 0 || col >= t->ncols)
		table_fail("the table has no column %s (columns count from 0)", col);
	Row *r = &t->rows[row];
	while (r->n <= col) {
		r->cell = realloc(r->cell, sizeof *r->cell * (size_t)(r->n + 1));
		r->cell[r->n++] = strdup("");
	}
	free(r->cell[col]);
	r->cell[col] = strdup(text);
	table_refresh(t, table_selected(t));
}

int64_t cby_gtk_table_cell_length(int64_t id, int row, int col)
{
	Item *t = item(id, K_TABLE);
	if (row < 0 || row >= t->nrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	return hand_out(t, strdup(cell_text(t, row, col)));
}

void cby_gtk_table_remove_row(int64_t id, int row)
{
	Item *t = item(id, K_TABLE);
	if (row < 0 || row >= t->nrows)
		table_fail("the table has no row %s (rows count from 0)", row);
	int keep = table_selected(t);
	for (int i = 0; i < t->rows[row].n; i++)
		free(t->rows[row].cell[i]);
	free(t->rows[row].cell);
	memmove(&t->rows[row], &t->rows[row + 1], sizeof *t->rows * (size_t)(t->nrows - row - 1));
	t->nrows--;
	table_refresh(t, keep == row ? -1 : keep > row ? keep - 1 : keep);
	if (keep == row && t->run.fn) {   /* the selected row went: nothing is selected now */
		Handler h = t->run;
		invoke(&h);
	}
}

void cby_gtk_table_clear(int64_t id)
{
	Item *t = item(id, K_TABLE);
	for (int r = 0; r < t->nrows; r++) {
		for (int i = 0; i < t->rows[r].n; i++)
			free(t->rows[r].cell[i]);
		free(t->rows[r].cell);
	}
	int had = table_selected(t);
	t->nrows = 0;
	table_refresh(t, -1);
	if (had >= 0 && t->run.fn) {
		Handler h = t->run;
		invoke(&h);
	}
}

char cby_gtk_table_cell_char(int64_t id, int64_t i) { return cby_gtk_text_char(id, i); }

int cby_gtk_table_rows(int64_t id) { return item(id, K_TABLE)->nrows; }
int cby_gtk_table_columns(int64_t id) { return item(id, K_TABLE)->ncols; }

/* ------------------------------------------------------------- number boxes, colors and fonts */

int64_t cby_gtk_add_number(int64_t box, int min, int max)
{
	if (max < min) {
		int t = min;
		min = max;
		max = t;
	}
	GtkWidget *n = gtk_spin_button_new_with_range(min, max, 1);
	gtk_widget_set_halign(n, GTK_ALIGN_START);
	return place(box, n, n, K_NUMBER);
}

int cby_gtk_number_get(int64_t id)
{
	double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(item(id, K_NUMBER)->w));
	return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

void cby_gtk_number_set(int64_t id, int value) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(item(id, K_NUMBER)->w), value); }

int64_t cby_gtk_add_color_button(int64_t box)
{
	GtkWidget *b = gtk_color_dialog_button_new(gtk_color_dialog_new());
	GdkRGBA black = { 0, 0, 0, 1 };
	gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(b), &black);
	gtk_widget_set_halign(b, GTK_ALIGN_START);
	return place(box, b, b, K_COLOR);
}

/* its color, as "#rrggbb" ("#rrggbbaa" if it's see-through) */
int64_t cby_gtk_color_length(int64_t id)
{
	Item *it = item(id, K_COLOR);
	const GdkRGBA *c = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(it->w));
	char buf[16];
	int r = (int)(c->red * 255 + 0.5), g = (int)(c->green * 255 + 0.5), b = (int)(c->blue * 255 + 0.5),
	    a = (int)(c->alpha * 255 + 0.5);
	if (a < 255)
		snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", r, g, b, a);
	else
		snprintf(buf, sizeof buf, "#%02x%02x%02x", r, g, b);
	return hand_out(it, strdup(buf));
}

void cby_gtk_color_set(int64_t id, const char *color)
{
	GdkRGBA c = color_of(color);
	gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(item(id, K_COLOR)->w), &c);
}

int64_t cby_gtk_add_font_button(int64_t box)
{
	GtkWidget *b = gtk_font_dialog_button_new(gtk_font_dialog_new());
	PangoFontDescription *d = pango_font_description_from_string("Sans 12");
	gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(b), d);
	pango_font_description_free(d);
	gtk_widget_set_halign(b, GTK_ALIGN_START);
	return place(box, b, b, K_FONT);
}

/* its font, as a name and size: "Serif Bold 14" */
int64_t cby_gtk_font_length(int64_t id)
{
	Item *it = item(id, K_FONT);
	PangoFontDescription *d = gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(it->w));
	char *s = d ? pango_font_description_to_string(d) : NULL;
	char *copy = strdup(s ? s : "");
	g_free(s);
	return hand_out(it, copy);
}

void cby_gtk_font_set(int64_t id, const char *font)
{
	PangoFontDescription *d = pango_font_description_from_string(font);
	gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(item(id, K_FONT)->w), d);
	pango_font_description_free(d);
}

/* ------------------------------------------------------------- menus and shortcuts
 *
 * A window's menus are GTK's: a menu bar made from a model (GMenu), whose
 * items run actions (GSimpleAction) in a group the window holds, "cby".
 * A shortcut ("Ctrl+O") is a GtkShortcut on the window. Both call the
 * item's handler, which is kept here, so a test script can call it too. */

/* For things in the table that aren't widgets (menus, menu items) */
static int64_t add_thing(int kind, int64_t win)
{
	if (nitems == capitems) {
		capitems = capitems ? capitems * 2 : 64;
		items = realloc(items, sizeof *items * (size_t)capitems);
	}
	items[nitems] = (Item){ .kind = kind, .win = win };
	return ++nitems;
}

/* A shortcut as written ("Ctrl+Shift+S", "F5", "Alt+Left") in the one
 * way it's kept ("ctrl+shift+s"), and as GTK writes it ("<Control><Shift>s");
 * false if it isn't one. */
static int parse_keys(const char *spec, char *norm, size_t nsize, char *gtk, size_t gsize)
{
	static const struct { const char *name, *gtk; } keys[] = {
		{ "enter", "Return" }, { "return", "Return" }, { "escape", "Escape" }, { "esc", "Escape" },
		{ "space", "space" }, { "tab", "Tab" }, { "backspace", "BackSpace" }, { "delete", "Delete" },
		{ "insert", "Insert" }, { "home", "Home" }, { "end", "End" }, { "page_up", "Page_Up" },
		{ "pageup", "Page_Up" }, { "page_down", "Page_Down" }, { "pagedown", "Page_Down" }, { "up", "Up" },
		{ "down", "Down" }, { "left", "Left" }, { "right", "Right" },
	};
	int ctrl = 0, shift = 0, alt = 0;
	char key[32] = "", keygtk[32] = "";
	const char *p = spec;
	while (*p) {
		const char *plus = strchr(p, '+');
		if (plus == p) {   /* "Ctrl++" */
			plus = strchr(p + 1, '+');
		}
		size_t n = plus ? (size_t)(plus - p) : strlen(p);
		char part[32];
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
			return 0;   /* only Ctrl, Shift and Alt go before the key */
		else {
			if (n == 1 && (unsigned char)part[0] > ' ' && (unsigned char)part[0] < 127) {
				snprintf(key, sizeof key, "%s", part);
				snprintf(keygtk, sizeof keygtk, "%s", part);
			} else if (part[0] == 'f' && n <= 3 && atoi(part + 1) >= 1 && atoi(part + 1) <= 12) {
				snprintf(key, sizeof key, "f%d", atoi(part + 1));
				snprintf(keygtk, sizeof keygtk, "F%d", atoi(part + 1));
			} else {
				for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
					if (!strcmp(part, keys[i].name)) {
						snprintf(key, sizeof key, "%s", keys[i].name[0] == 'e' && keys[i].name[1] == 's' &&
							 !keys[i].name[3] ? "escape" : keys[i].name);
						snprintf(keygtk, sizeof keygtk, "%s", keys[i].gtk);
					}
				if (!strcmp(key, "return"))
					strcpy(key, "enter");
				if (!strcmp(key, "pageup"))
					strcpy(key, "page_up");
				if (!strcmp(key, "pagedown"))
					strcpy(key, "page_down");
				if (!key[0])
					return 0;
			}
			break;
		}
		p = plus + 1;
	}
	if (!key[0])
		return 0;
	snprintf(norm, nsize, "%s%s%s%s", ctrl ? "ctrl+" : "", alt ? "alt+" : "", shift ? "shift+" : "", key);
	snprintf(gtk, gsize, "%s%s%s%s", ctrl ? "<Control>" : "", alt ? "<Alt>" : "", shift ? "<Shift>" : "", keygtk);
	return 1;
}

static void activate(int64_t id)
{
	if (id < 1 || id > nitems)
		return;
	Item *it = &items[id - 1];
	if (it->grayed || !window_alive(it->win))
		return;
	Handler h = it->run;
	if (h.fn)
		invoke(&h);
}

static void on_menu_action(GSimpleAction *a, GVariant *param, gpointer data)
{
	(void)a;
	(void)param;
	activate((int64_t)(intptr_t)data);
}

static gboolean on_shortcut(GtkWidget *w, GVariant *args, gpointer data)
{
	(void)w;
	(void)args;
	activate((int64_t)(intptr_t)data);
	return TRUE;
}

/* the window's shortcut: when these keys are pressed, item id runs */
static void add_keys(int64_t win, const char *gtk_keys, int64_t id)
{
	Item *w = item(win, K_WINDOW);
	if (!w->keys) {
		w->keys = gtk_shortcut_controller_new();
		/* before the window's on_key, which takes every key (and before
		 * what has the focus, as a menu's shortcuts are) */
		gtk_event_controller_set_propagation_phase(w->keys, GTK_PHASE_CAPTURE);
		gtk_widget_add_controller(w->w, w->keys);
	}
	GtkShortcutTrigger *t = gtk_shortcut_trigger_parse_string(gtk_keys);
	if (!t)
		fail("'%s' isn't a shortcut GTK understands", gtk_keys);
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(w->keys),
					     gtk_shortcut_new(t, gtk_callback_action_new(on_shortcut, (gpointer)(intptr_t)id, NULL)));
}

static const char *keys_fail = "'%s' isn't a shortcut: write keys such as \"Ctrl+S\", \"Ctrl+Shift+Z\", \"Alt+X\" or \"F5\"";

int64_t cby_gtk_add_menu(int64_t win, const char *title)
{
	Item *w = item(win, K_WINDOW);
	if (!w->bar) {
		w->bar = g_menu_new();
		w->actions = g_simple_action_group_new();
		gtk_widget_insert_action_group(w->w, "cby", G_ACTION_GROUP(w->actions));
		GtkWidget *bar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(w->bar));
		gtk_box_prepend(GTK_BOX(w->frame), bar);
	}
	int64_t id = add_thing(K_MENU, win);
	Item *m = &items[id - 1];
	m->menu = g_menu_new();
	m->section = g_menu_new();
	m->title = strdup(title);
	g_menu_append_section(m->menu, NULL, G_MENU_MODEL(m->section));
	g_menu_append_submenu(items[win - 1].bar, title, G_MENU_MODEL(m->menu));
	return id;
}

int64_t cby_gtk_menu_add_item(int64_t menu, const char *text, const char *keys, void *obj, void *fn)
{
	Item *m = item(menu, K_MENU);
	int64_t win = m->win;
	char norm[64] = "", gtk[96] = "";
	if (keys && *keys && !parse_keys(keys, norm, sizeof norm, gtk, sizeof gtk))
		fail(keys_fail, keys);
	int64_t id = add_thing(K_MENU_ITEM, win);
	Item *it = &items[id - 1];
	it->in_menu = menu;
	it->label = strdup(text);
	it->keys_name = strdup(norm);
	it->run = (Handler){ obj, fn, win };
	char name[32], detailed[40];
	snprintf(name, sizeof name, "a%lld", (long long)id);
	snprintf(detailed, sizeof detailed, "cby.%s", name);
	it->action = g_simple_action_new(name, NULL);
	g_signal_connect_data(it->action, "activate", G_CALLBACK(on_menu_action), (gpointer)(intptr_t)id, NULL, 0);
	g_action_map_add_action(G_ACTION_MAP(items[win - 1].actions), G_ACTION(it->action));
	GMenuItem *mi = g_menu_item_new(text, detailed);
	if (*gtk)
		g_menu_item_set_attribute(mi, "accel", "s", gtk);   /* shown beside it */
	g_menu_append_item(items[menu - 1].section, mi);
	g_object_unref(mi);
	if (*gtk)
		add_keys(win, gtk, id);
	return id;
}

void cby_gtk_menu_add_separator(int64_t menu)
{
	Item *m = item(menu, K_MENU);
	m->section = g_menu_new();
	g_menu_append_section(m->menu, NULL, G_MENU_MODEL(m->section));
}

void cby_gtk_menu_item_set_enabled(int64_t id, int on)
{
	Item *it = item(id, K_MENU_ITEM);
	it->grayed = !on;
	g_simple_action_set_enabled(it->action, on != 0);
}

void cby_gtk_add_shortcut(int64_t win, const char *keys, void *obj, void *fn)
{
	item(win, K_WINDOW);
	char norm[64], gtk[96];
	if (!parse_keys(keys, norm, sizeof norm, gtk, sizeof gtk))
		fail(keys_fail, keys);
	int64_t id = add_thing(K_SHORTCUT, win);
	items[id - 1].keys_name = strdup(norm);
	items[id - 1].run = (Handler){ obj, fn, win };
	add_keys(win, gtk, id);
}

/* ------------------------------------------------------------- events */

static Handler *new_handler(int64_t id, void *obj, void *fn)
{
	Handler *h = malloc(sizeof *h);
	h->obj = obj;
	h->fn = fn;
	h->win = item(id, 0)->win;
	return h;
}

static void connect(int64_t id, int kind, gpointer instance, const char *signal, int two_args, void *obj, void *fn)
{
	item(id, kind);
	Handler *h = new_handler(id, obj, fn);
	g_signal_connect_data(instance, signal, two_args ? G_CALLBACK(on_signal2) : G_CALLBACK(on_signal),
			      h, (GClosureNotify)free_handler, 0);
}

void cby_gtk_on_click(int64_t id, void *obj, void *fn)
{
	connect(id, K_BUTTON, item(id, K_BUTTON)->w, "clicked", 0, obj, fn);
}

typedef struct TabHandler {
	Handler h;
	int64_t id;
} TabHandler;

static void on_tab_change(gpointer notebook, gpointer pspec, gpointer data)
{
	(void)notebook;
	(void)pspec;
	TabHandler *t = data;
	if (!items[t->id - 1].adding)
		invoke(&t->h);
}

void cby_gtk_on_change(int64_t id, void *obj, void *fn)
{
	Item *it = item(id, 0);
	switch (it->kind) {
	case K_ENTRY:     connect(id, K_ENTRY, it->w, "changed", 0, obj, fn); break;
	case K_TEXT_AREA: connect(id, K_TEXT_AREA, buffer_of(it), "changed", 0, obj, fn); break;
	case K_CHOICE:    connect(id, K_CHOICE, it->w, "notify::selected", 1, obj, fn); break;
	case K_SLIDER:    connect(id, K_SLIDER, it->w, "value-changed", 0, obj, fn); break;
	case K_NUMBER:    connect(id, K_NUMBER, it->w, "value-changed", 0, obj, fn); break;
	case K_COLOR:    connect(id, K_COLOR, it->w, "notify::rgba", 1, obj, fn); break;
	case K_FONT:      connect(id, K_FONT, it->w, "notify::font-desc", 1, obj, fn); break;
	case K_TABS: {
		/* (not when GTK shows the first page as it's added: tui doesn't either) */
		TabHandler *t = malloc(sizeof *t);
		t->h = (Handler){ obj, fn, it->win };
		t->id = id;
		g_signal_connect_data(it->w, "notify::page", G_CALLBACK(on_tab_change), t, (GClosureNotify)free_handler, 0);
		break;
	}
	default:
		fprintf(stderr, "gtk: on_change works on text boxes, text areas, drop-down lists, sliders and tabs\n");
		exit(1);
	}
}

void cby_gtk_on_enter(int64_t id, void *obj, void *fn)
{
	connect(id, K_ENTRY, item(id, K_ENTRY)->w, "activate", 0, obj, fn);
}

void cby_gtk_on_toggle(int64_t id, void *obj, void *fn)
{
	connect(id, K_CHECK, item(id, K_CHECK)->w, "toggled", 0, obj, fn);
}

static void on_table_select(gpointer sel, gpointer pspec, gpointer data)
{
	(void)sel;
	(void)pspec;
	int64_t id = (int64_t)(intptr_t)data;
	if (id < 1 || id > nitems || items[id - 1].adding)
		return;
	int row = table_selected(&items[id - 1]);
	if (row == items[id - 1].last_row)
		return;   /* the same row, moved by sorting */
	items[id - 1].last_row = row;
	if (items[id - 1].run.fn) {
		Handler h = items[id - 1].run;
		invoke(&h);
	}
}

void cby_gtk_on_select(int64_t id, void *obj, void *fn)
{
	if (item(id, 0)->kind == K_TABLE) {
		/* kept with the table, so a change the table makes itself (after
		 * set_cell) is quiet, as in tui */
		Item *t = item(id, K_TABLE);
		if (!t->run.fn)
			g_signal_connect_data(t->picked, "notify::selected", G_CALLBACK(on_table_select), (gpointer)(intptr_t)id,
					      NULL, 0);
		t->run = (Handler){ obj, fn, t->win };
	} else
		connect(id, K_LIST, item(id, K_LIST)->w, "row-selected", 1, obj, fn);
}

/* ------------------------------------------------------------- timers */

typedef struct Timer {
	Handler h;
	int slot;     /* index in its window's timer list */
	int once;
} Timer;

static gboolean on_timer(gpointer data)
{
	Timer *t = data;
	if (!window_alive(t->h.win)) {
		items[t->h.win - 1].timers[t->slot] = 0;
		return G_SOURCE_REMOVE;
	}
	if (t->once)
		items[t->h.win - 1].timers[t->slot] = 0;   /* finished: nothing to stop later */
	invoke(&t->h);
	return t->once ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static void add_timer(int64_t win, int ms, int once, void *obj, void *fn)
{
	Item *w = item(win, K_WINDOW);
	Timer *t = malloc(sizeof *t);
	t->h = (Handler){ obj, fn, win };
	t->once = once;
	t->slot = w->ntimers;
	w->timers = realloc(w->timers, sizeof *w->timers * (size_t)(w->ntimers + 1));
	w->timers[w->ntimers++] = 0;
	guint src = g_timeout_add_full(G_PRIORITY_DEFAULT, ms < 1 ? 1 : (guint)ms, on_timer, t, free);
	items[win - 1].timers[t->slot] = src;
}

void cby_gtk_every(int64_t win, int ms, void *obj, void *fn) { add_timer(win, ms, 0, obj, fn); }
void cby_gtk_after(int64_t win, int ms, void *obj, void *fn) { add_timer(win, ms, 1, obj, fn); }
void cby_gtk_stop_timers(int64_t win) { stop_timers(item(win, K_WINDOW)); }

/* ------------------------------------------------------------- dialogs */

/* In a test script, a dialog waits here for the script's `answer`, `pick`
 * or `cancel`. */
enum { D_NONE, D_ASK, D_OPEN, D_SAVE };
static struct {
	int kind;
	Handler h;
} pending;

typedef struct Dialog {
	Handler h;
	int kind;
} Dialog;

static void set_chosen(int64_t win, const char *path)
{
	Item *w = item(win, K_WINDOW);
	free(w->chosen);
	w->chosen = strdup(path ? path : "");
}

void cby_gtk_show_message(int64_t win, const char *text)
{
	Item *w = item(win, K_WINDOW);
	if (scripted())
		return;   /* nobody to read it */
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", text);
	gtk_alert_dialog_show(d, GTK_WINDOW(w->w));
	g_object_unref(d);
}

static void on_answer(GObject *source, GAsyncResult *res, gpointer data)
{
	Dialog *dl = data;
	int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);
	if (button == 1)
		invoke(&dl->h);
	free(dl);
}

static void on_file(GObject *source, GAsyncResult *res, gpointer data)
{
	Dialog *dl = data;
	GError *err = NULL;
	GFile *f = dl->kind == D_OPEN
		? gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &err)
		: gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, &err);
	if (err)
		g_error_free(err);
	if (f) {
		char *path = g_file_get_path(f);
		if (path && window_alive(dl->h.win)) {
			set_chosen(dl->h.win, path);
			invoke(&dl->h);
		}
		g_free(path);
		g_object_unref(f);
	}
	free(dl);
}

static Dialog *new_dialog(int64_t win, int kind, void *obj, void *fn)
{
	Dialog *dl = malloc(sizeof *dl);
	dl->h = (Handler){ obj, fn, win };
	dl->kind = kind;
	return dl;
}

void cby_gtk_ask(int64_t win, const char *question, void *obj, void *fn)
{
	Item *w = item(win, K_WINDOW);
	if (scripted()) {
		pending.kind = D_ASK;
		pending.h = (Handler){ obj, fn, win };
		return;
	}
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", question);
	const char *buttons[] = { "No", "Yes", NULL };
	gtk_alert_dialog_set_buttons(d, buttons);
	gtk_alert_dialog_set_cancel_button(d, 0);
	gtk_alert_dialog_set_default_button(d, 1);
	gtk_alert_dialog_choose(d, GTK_WINDOW(w->w), NULL, on_answer, new_dialog(win, D_ASK, obj, fn));
	g_object_unref(d);
}

static void file_dialog(int64_t win, int kind, const char *suggested, void *obj, void *fn)
{
	Item *w = item(win, K_WINDOW);
	if (scripted()) {
		pending.kind = kind;
		pending.h = (Handler){ obj, fn, win };
		return;
	}
	GtkFileDialog *d = gtk_file_dialog_new();
	gtk_file_dialog_set_title(d, kind == D_OPEN ? "Open" : "Save");
	if (kind == D_OPEN) {
		gtk_file_dialog_open(d, GTK_WINDOW(w->w), NULL, on_file, new_dialog(win, kind, obj, fn));
	} else {
		if (suggested && *suggested)
			gtk_file_dialog_set_initial_name(d, suggested);
		gtk_file_dialog_save(d, GTK_WINDOW(w->w), NULL, on_file, new_dialog(win, kind, obj, fn));
	}
	g_object_unref(d);
}

void cby_gtk_ask_open_file(int64_t win, void *obj, void *fn) { file_dialog(win, D_OPEN, NULL, obj, fn); }
void cby_gtk_ask_save_file(int64_t win, const char *suggested, void *obj, void *fn)
{
	file_dialog(win, D_SAVE, suggested, obj, fn);
}

int64_t cby_gtk_chosen_length(int64_t win)
{
	Item *w = item(win, K_WINDOW);
	return hand_out(w, strdup(w->chosen ? w->chosen : ""));
}

char cby_gtk_chosen_char(int64_t win, int64_t i) { return cby_gtk_text_char(win, i); }

/* ------------------------------------------------------------- test scripts */

static int screenshot(GtkWidget *w, const char *file)
{
	GdkPaintable *p = gtk_widget_paintable_new(w);
	GtkSnapshot *s = gtk_snapshot_new();
	gdk_paintable_snapshot(p, s, gtk_widget_get_width(w), gtk_widget_get_height(w));
	GskRenderNode *node = gtk_snapshot_free_to_node(s);
	int ok = 0;
	if (node) {
		GdkTexture *t = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(w)), node, NULL);
		if (t) {
			ok = gdk_texture_save_to_png(t, file);
			g_object_unref(t);
		}
		gsk_render_node_unref(node);
	}
	g_object_unref(p);
	return ok;
}

/* The script's commands not yet done: each window that runs takes them
 * up to and including a `close`; the rest go to the next window.
 * `win` is an id, not a pointer: handlers may add widgets and move the table. */
static char *script_left;
static int script_loaded;

static int scripted(void)
{
	if (!script_loaded) {
		script_loaded = 1;
		const char *script = getenv("CABBY_GUI_SCRIPT");
		if (script)
			script_left = strdup(script);
	}
	return getenv("CABBY_GUI_SCRIPT") != NULL;
}

/* The n-th widget of a kind (counting from 1, in the order they were added). */
static Item *nth(int kind, int n)
{
	int k = 0;
	for (int i = 0; i < nitems; i++)
		if (items[i].kind == kind && window_alive(items[i].win) && ++k == n)
			return &items[i];
	return NULL;
}

/* "3 some text": the number, and where the text starts ("" if none). */
static int number_and_text(const char *s, const char **text)
{
	int n = atoi(s);
	const char *sp = strchr(s, ' ');
	*text = sp ? sp + 1 : "";
	return n;
}

static void type_into(GtkEditable *e, const char *text)
{
	if (*gtk_editable_get_text(e))
		gtk_editable_set_text(e, "");
	int pos = 0;
	for (const char *c = text; *c; ) {
		int len = 1;
		while ((c[len] & 0xC0) == 0x80)
			len++;
		gtk_editable_insert_text(e, c, len, &pos);
		c += len;
		pump();
	}
}

static void wait_ms(int ms)
{
	gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
	while (g_get_monotonic_time() < end) {
		pump();
		usleep(1000);
	}
	pump();
}

static int index_of(Item *it, const char *text)
{
	for (int i = 0; i < it->nstrings; i++)
		if (strcmp(it->strings[i], text) == 0)
			return i;
	return -1;
}

static void settle_processes(void);

static void run_script(int64_t win)
{
	char *cmd;
	while (window_alive(win) && items[win - 1].open && script_left && *script_left) {
		cmd = script_left;
		char *semi = strchr(cmd, ';');
		if (semi) {
			*semi = '\0';
			script_left = semi + 1;
		} else {
			script_left = NULL;
		}
		while (*cmd == ' ')
			cmd++;
		pump();
		const char *text;
		if (strncmp(cmd, "click ", 6) == 0) {
			int found = 0;
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_BUTTON && window_alive(items[i].win) &&
				    strcmp(gtk_button_get_label(GTK_BUTTON(items[i].w)), cmd + 6) == 0) {
					g_signal_emit_by_name(items[i].w, "clicked");
					found = 1;
				}
			if (!found)
				fprintf(stderr, "gui script: no button '%s'\n", cmd + 6);
		} else if (strncmp(cmd, "check ", 6) == 0) {
			int found = 0;
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_CHECK && window_alive(items[i].win) &&
				    strcmp(gtk_check_button_get_label(GTK_CHECK_BUTTON(items[i].w)), cmd + 6) == 0) {
					GtkCheckButton *c = GTK_CHECK_BUTTON(items[i].w);
					gtk_check_button_set_active(c, !gtk_check_button_get_active(c));
					found = 1;
				}
			if (!found)
				fprintf(stderr, "gui script: no check box '%s'\n", cmd + 6);
		} else if (strncmp(cmd, "type ", 5) == 0) {
			Item *e = nth(K_ENTRY, number_and_text(cmd + 5, &text));
			if (e)
				type_into(GTK_EDITABLE(e->w), text);
			else
				fprintf(stderr, "gui script: no text box %s\n", cmd + 5);
		} else if (strncmp(cmd, "enter ", 6) == 0) {
			Item *e = nth(K_ENTRY, atoi(cmd + 6));
			if (e)
				g_signal_emit_by_name(e->w, "activate");
		} else if (strncmp(cmd, "write ", 6) == 0) {
			Item *a = nth(K_TEXT_AREA, number_and_text(cmd + 6, &text));
			if (a) {
				/* "\n" in a script stands for a new line */
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
				gtk_text_buffer_set_text(buffer_of(a), t, -1);
				free(t);
			} else {
				fprintf(stderr, "gui script: no text area %s\n", cmd + 6);
			}
		} else if (strncmp(cmd, "choose ", 7) == 0 || strncmp(cmd, "select ", 7) == 0) {
			int kind = cmd[1] == 'h' ? K_CHOICE : K_LIST;
			Item *it = nth(kind, number_and_text(cmd + 7, &text));
			int i = it ? index_of(it, text) : -1;
			if (i >= 0)
				cby_gtk_select((int64_t)(it - items) + 1, i);
			else
				fprintf(stderr, "gui script: no %s entry '%s'\n", kind == K_CHOICE ? "drop-down" : "list", text);
		} else if (strncmp(cmd, "number ", 7) == 0) {
			Item *n = nth(K_NUMBER, number_and_text(cmd + 7, &text));
			if (n)
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(n->w), atof(text));
			else
				fprintf(stderr, "gui script: no number box %s\n", cmd + 7);
		} else if (strncmp(cmd, "color ", 6) == 0 || strncmp(cmd, "font ", 5) == 0) {
			/* color <n> <color>, font <n> <font>: as if chosen in its dialog */
			int color = cmd[0] == 'c';
			Item *b = nth(color ? K_COLOR : K_FONT, number_and_text(cmd + (color ? 6 : 5), &text));
			if (b && color)
				cby_gtk_color_set((int64_t)(b - items) + 1, text);
			else if (b)
				cby_gtk_font_set((int64_t)(b - items) + 1, text);
			else
				fprintf(stderr, "gui script: no %s button for '%s'\n", color ? "color" : "font", cmd);
		} else if (strncmp(cmd, "row ", 4) == 0) {
			/* row <n> <first cell>: selects that row of the n-th table */
			Item *t = nth(K_TABLE, number_and_text(cmd + 4, &text));
			int found = -1;
			for (int r = 0; t && r < t->nrows && found < 0; r++)
				if (!strcmp(cell_text(t, r, 0), text))
					found = r;
			if (found >= 0)
				table_select(t, found);
			else
				fprintf(stderr, "gui script: no row '%s'\n", text);
		} else if (strncmp(cmd, "sort ", 5) == 0) {
			/* sort <n> <heading>: as if that heading were clicked */
			Item *t = nth(K_TABLE, number_and_text(cmd + 5, &text));
			int i = t ? index_of(t, text) : -1;
			if (i >= 0)
				gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(t->w), t->columns[i], GTK_SORT_ASCENDING);
			else
				fprintf(stderr, "gui script: no heading '%s'\n", text);
		} else if (strncmp(cmd, "tab ", 4) == 0) {
			/* tab <n> <title>: shows that tab of the n-th set of tabs */
			Item *t = nth(K_TABS, number_and_text(cmd + 4, &text));
			int i = t ? index_of(t, text) : -1;
			if (i >= 0)
				gtk_notebook_set_current_page(GTK_NOTEBOOK(t->w), i);
			else
				fprintf(stderr, "gui script: no tab '%s'\n", text);
		} else if (strncmp(cmd, "slide ", 6) == 0) {
			Item *s = nth(K_SLIDER, number_and_text(cmd + 6, &text));
			if (s)
				gtk_range_set_value(GTK_RANGE(s->w), atof(text));
		} else if (strncmp(cmd, "press ", 6) == 0 || strncmp(cmd, "point ", 6) == 0) {
			/* press <n> <x> <y>: a click on the n-th canvas; point: the mouse moves there */
			int n = 0;
			double x = 0, y = 0;
			Item *c = sscanf(cmd + 6, "%d %lf %lf", &n, &x, &y) == 3 ? nth(K_CANVAS, n) : NULL;
			if (c)
				canvas_event((int64_t)(c - items) + 1, cmd[1] == 'r' ? 0 : 1, x, y);
			else
				fprintf(stderr, "gui script: no canvas for '%s'\n", cmd);
		} else if (strncmp(cmd, "key ", 4) == 0) {
			/* key <name>: to the first canvas that takes keys */
			int found = 0;
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_CANVAS && window_alive(items[i].win) && items[i].keyed.fn) {
					canvas_key(i + 1, strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
					found = 1;
				}
			for (int i = 0; i < nitems && !found; i++)
				if (items[i].kind == K_WINDOW && window_alive(i + 1) && items[i].keyed.fn) {
					/* the window's own keys */
					snprintf(items[i].key, sizeof items[i].key, "%s", strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
					invoke(&items[i].keyed);
					found = 1;
				}
			if (!found)
				fprintf(stderr, "gui script: nothing takes keys\n");
		} else if (strncmp(cmd, "menu ", 5) == 0) {
			/* menu <Menu> > <Item>: chooses that item */
			const char *sep = strstr(cmd + 5, " > ");
			int found = 0;
			if (sep) {
				size_t tl = (size_t)(sep - (cmd + 5));
				for (int i = 0; i < nitems && !found; i++) {
					Item *it = &items[i];
					if (it->kind != K_MENU_ITEM || it->in_menu < 1 || !window_alive(it->win))
						continue;
					Item *m = &items[it->in_menu - 1];
					if (strlen(m->title) == tl && strncmp(m->title, cmd + 5, tl) == 0 && strcmp(it->label, sep + 3) == 0) {
						found = 1;
						if (it->grayed)
							fprintf(stderr, "gui script: '%s' is grayed out\n", sep + 3);
						activate(i + 1);
					}
				}
			}
			if (!found)
				fprintf(stderr, "gui script: no menu item '%s'\n", cmd + 5);
		} else if (strncmp(cmd, "shortcut ", 9) == 0) {
			/* shortcut <keys>: as if they were pressed */
			char norm[64], gtk[96];
			int found = 0;
			if (parse_keys(cmd + 9, norm, sizeof norm, gtk, sizeof gtk))
				for (int i = 0; i < nitems && !found; i++)
					if ((items[i].kind == K_MENU_ITEM || items[i].kind == K_SHORTCUT) && items[i].win == win &&
					    items[i].keys_name && strcmp(items[i].keys_name, norm) == 0) {
						found = 1;
						activate(i + 1);
					}
			if (!found)
				fprintf(stderr, "gui script: no shortcut '%s'\n", cmd + 9);
		} else if (strncmp(cmd, "wait ", 5) == 0) {
			wait_ms(atoi(cmd + 5));

		} else if (strcmp(cmd, "processes") == 0) {
			settle_processes();
		} else if (strncmp(cmd, "answer ", 7) == 0 || strncmp(cmd, "pick ", 5) == 0 || strcmp(cmd, "cancel") == 0) {
			int kind = pending.kind;
			Handler h = pending.h;
			pending.kind = D_NONE;
			if (kind == D_NONE) {
				fprintf(stderr, "gui script: '%s' but no dialog is open\n", cmd);
			} else if (cmd[0] == 'a') {
				if (kind == D_ASK && strcmp(cmd + 7, "yes") == 0)
					invoke(&h);
			} else if (cmd[0] == 'p' && kind != D_ASK) {
				set_chosen(h.win, cmd + 5);
				invoke(&h);
			}
		} else if (strncmp(cmd, "shot ", 5) == 0) {
			/* let GTK lay out and draw a frame first (it draws on its frame
			 * clock, so give it a little time) */
			int ok = 0;
			for (int tries = 0; tries < 50 && !ok; tries++) {
				for (int i = 0; i < 5; i++) {
					usleep(10000);
					pump();
				}
				ok = screenshot(items[win - 1].w, cmd + 5);
			}
			if (!ok)
				fprintf(stderr, "gui script: could not save %s\n", cmd + 5);
		} else if (strcmp(cmd, "close") == 0) {
			gtk_window_close(GTK_WINDOW(items[win - 1].w));
			pump();
			break;
		} else if (*cmd) {
			fprintf(stderr, "gui script: unknown command '%s'\n", cmd);
		}
		pump();
	}
}

/* ------------------------------------------------------------- processes */

/* Cabby's event loop (in its runtime): while a window is open, its timers
 * and what it watches (processes' output) are looked at every few
 * milliseconds, and their handlers called. */
extern int cby_events_fds(int *fds, int max) __attribute__((weak));
extern void cby_events_step(int wait_ms) __attribute__((weak));

static int (*ev_fds)(int *, int);
static void (*ev_step)(int);

static void find_events(void)
{
	static int looked;
	if (looked)
		return;
	looked = 1;
	ev_fds = cby_events_fds;
	ev_step = cby_events_step;
	if (!ev_fds || !ev_step) {
		/* in the interpreter: the cabby program itself has them */
		ev_fds = (int (*)(int *, int))dlsym(RTLD_DEFAULT, "cby_events_fds");
		ev_step = (void (*)(int))dlsym(RTLD_DEFAULT, "cby_events_step");
		if (!ev_fds || !ev_step)
			ev_fds = NULL, ev_step = NULL;
	}
}

static gboolean poll_events(gpointer data)
{
	(void)data;
	ev_step(0);
	return G_SOURCE_CONTINUE;
}

/* script command `processes`: lets watched processes finish */
static void settle_processes(void)
{
	find_events();
	int fds[64];
	while (ev_fds && ev_fds(fds, 64) > 0) {
		ev_step(50);
		pump();
	}
}

/* ------------------------------------------------------------- running */

void cby_gtk_window_run(int64_t win)
{
	Item *it = item(win, K_WINDOW);
	if (it->destroyed)
		return;
	it->open = 1;
	gtk_window_present(GTK_WINDOW(it->w));
	if (scripted() && script_left) {
		for (int i = 0; i < 200 && !gtk_widget_get_mapped(items[win - 1].w); i++)
			g_main_context_iteration(NULL, TRUE);
		run_script(win);
	}
	/* sleep until something happens; return once the window is closed */
	find_events();
	guint watch = ev_step ? g_timeout_add_full(G_PRIORITY_DEFAULT, 10, poll_events, NULL, NULL) : 0;
	while (window_alive(win) && items[win - 1].open)
		g_main_context_iteration(NULL, TRUE);
	if (watch)
		g_source_remove(watch);
	if (window_alive(win))
		stop_timers(&items[win - 1]);
	settle();
	fflush(stdout);
}

/* videos: in the gui library (drawn with SDL), not yet here */
int64_t cby_gtk_add_video(int64_t box, int64_t source)
{
	(void)box;
	(void)source;
	fail("%s", "videos aren't in the gtk library yet: 'use gui' shows them");
}

void cby_gtk_video_controls(int64_t id, int show) { (void)id; (void)show; }
void cby_gtk_video_on_end(int64_t id, void *obj, void *fn) { (void)id; (void)obj; (void)fn; }
