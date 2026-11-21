/* accessibility.h — gui's windows, for screen readers (included by gui.c).
 *
 * On Linux, a screen reader (Orca) learns what's in a program's windows
 * through AT-SPI: the program puts a tree of objects on the accessibility
 * bus (a D-Bus bus of its own), each a window, a button, a text box...,
 * with its role, its name, its states (focused, checked, enabled) and
 * where it is; the reader asks about them, and the program tells it when
 * something changes (the focus moved, text was typed). GTK does the same.
 *
 * Here, the tree is gui's own: the application, its open windows, and in
 * each, its menus and widgets (rows and columns left out: they only lay
 * things out). It's put on the bus only when it's wanted, when a screen
 * reader is running (org.a11y.Status says so), or CABBY_ACCESSIBILITY=1
 * asks; CABBY_ACCESSIBILITY=0 never. So a gui program costs no more
 * without one.
 *
 * The bus is reached with Cabby's own D-Bus (runtime/dbus.c): nothing
 * needs installing beyond what a Linux desktop has. (A Mac's VoiceOver
 * reads a program another way, through its own accessibility: still to
 * come.) */

/* Cabby's D-Bus and JSON (runtime/dbus.c, json.c), from the runtime */
#ifdef __wasi__
/* (in a browser there's no D-Bus: no bus is found, and none is served) */
static int64_t cby_dbus_try_session(void) { return 0; }
static int64_t cby_dbus_try_address(const char *address) { (void)address; return 0; }
static void cby_dbus_serve_tree(int64_t id, const char *prefix,
				void (*fn)(void *ctx, int64_t bus, const char *path, const char *iface, const char *member,
					   int64_t args),
				void *ctx) { (void)id; (void)prefix; (void)fn; (void)ctx; }
extern void cby_dbus_close(int64_t id);
#else
extern int64_t cby_dbus_try_session(void);
extern int64_t cby_dbus_try_address(const char *address);
extern void cby_dbus_close(int64_t id);
extern void cby_dbus_serve_tree(int64_t id, const char *prefix,
				void (*fn)(void *ctx, int64_t bus, const char *path, const char *iface, const char *member,
					   int64_t args),
				void *ctx);
#endif
extern int64_t cby_dbus_message_new(const char *dest, const char *path, const char *iface, const char *member);
extern int64_t cby_dbus_signal_new(const char *path, const char *iface, const char *member);
extern int64_t cby_dbus_answer_new(void);
extern void cby_dbus_message_free(int64_t id);
extern void cby_dbus_add_str(int64_t id, const char *s);
extern void cby_dbus_add_path(int64_t id, const char *s);
extern void cby_dbus_add_bool(int64_t id, int b);
extern void cby_dbus_add_int(int64_t id, int64_t v);
extern void cby_dbus_add_uint(int64_t id, int64_t v);
extern void cby_dbus_add_double(int64_t id, double v);
extern void cby_dbus_begin_list(int64_t id, const char *t);
extern void cby_dbus_end_list(int64_t id);
extern void cby_dbus_begin_lookup(int64_t id, const char *k, const char *v);
extern void cby_dbus_end_lookup(int64_t id);
extern void cby_dbus_begin_variant(int64_t id, const char *t);
extern void cby_dbus_end_variant(int64_t id);
extern void cby_dbus_begin_struct(int64_t id);
extern void cby_dbus_end_struct(int64_t id);
extern int64_t cby_dbus_try_call(int64_t id, int64_t msg);
extern void cby_dbus_reply(int64_t id, int64_t msg);
extern void cby_dbus_fail(int64_t id, const char *name, const char *text);
extern void cby_dbus_send(int64_t id, int64_t msg);
extern int64_t cby_dbus_text(int64_t id, int64_t what);
extern void cby_dbus_take_text(char *dst);
extern int64_t cby_json_at(int64_t h, int64_t i);
extern int64_t cby_json_long(int64_t h);
extern int64_t cby_json_length(int64_t h);
extern int cby_json_bool(int64_t h);
extern double cby_json_double(int64_t h);
extern int64_t cby_json_kind(int64_t h);
extern void cby_json_release(int64_t h);
extern const char *cby_json_str_of(int64_t h);

#include "../version.h"

#define A11Y_ROOT "/org/a11y/atspi/accessible/root"
#define A11Y_PREFIX "/org/a11y/atspi/accessible"

static int64_t a11y_bus;          /* the accessibility bus (0: not on it) */
static int a11y_tried;
static char a11y_me[128];         /* our name on it: ":1.42" */
static char a11y_parent_bus[128] = "";   /* the registry's desktop, our root's parent */
static char a11y_parent_path[256] = "/org/a11y/atspi/null";

/* AT-SPI's roles (atspi-constants.h) */
enum {
	R_CANVAS = 6, R_CHECK_BOX = 7, R_COMBO_BOX = 11, R_DRAWING_AREA = 18, R_FRAME = 23, R_IMAGE = 27, R_LABEL = 29,
	R_LIST = 31, R_LIST_ITEM = 32, R_MENU = 33, R_MENU_BAR = 34, R_MENU_ITEM = 35, R_PAGE_TAB = 37, R_PAGE_TAB_LIST = 38,
	R_PANEL = 39, R_PROGRESS_BAR = 42, R_PUSH_BUTTON = 43, R_SEPARATOR = 50, R_SLIDER = 51, R_SPIN_BUTTON = 52,
	R_TABLE = 55, R_TEXT = 61, R_APPLICATION = 75, R_ENTRY = 79, R_COLOR_CHOOSER = 9, R_FONT_CHOOSER = 22,
	R_ALERT = 2, R_DIALOG = 16
};

/* and states, as bits */
enum {
	S_ACTIVE = 1, S_CHECKED = 4, S_EDITABLE = 7, S_ENABLED = 8, S_FOCUSABLE = 11, S_FOCUSED = 12, S_MULTI_LINE = 17,
	S_SELECTABLE = 22, S_SELECTED = 23, S_SENSITIVE = 24, S_SHOWING = 25, S_SINGLE_LINE = 26, S_VISIBLE = 30,
	S_CHECKABLE = 41
};

/* ------------------------------------------------------------- the tree */

/* Objects by number: 0 the application (the root); above 0, an item; below
 * 0, what a window has that isn't an item: -(window * 4 + part), where the
 * part is its menu bar (0), the message or question showing over it (1),
 * or that's buttons (2: No; 3: Yes, or a message's OK). */
enum { P_BAR, P_DIALOG, P_NO, P_YES };
#define A11Y_NULL INT64_MIN       /* (no object) */
#define A11Y_DESKTOP (INT64_MIN + 1)   /* (the root's parent: the registry's) */
#define A11Y_GONE (INT64_MIN + 2)

static int64_t a11y_part_id(int64_t win, int part) { return -(win * 4 + part); }
static int64_t a11y_win_of(int64_t id) { return id >= 0 ? 0 : (-id) / 4; }
static int a11y_part(int64_t id) { return (int)((-id) % 4); }

/* and below that, a drop-down's, list's or tabs' options (each a list item,
 * or a tab): -(A11Y_OPTIONS + item * 2^20 + which) */
#define A11Y_OPTIONS ((int64_t)1 << 40)
static int a11y_is_option(int64_t id) { return id < 0 && id > A11Y_GONE && -id >= A11Y_OPTIONS; }
static int64_t a11y_option_id(int64_t item, int which) { return -(A11Y_OPTIONS + (item << 20) + which); }
static int64_t a11y_option_owner(int64_t id) { return (-id - A11Y_OPTIONS) >> 20; }
static int a11y_option_which(int64_t id) { return (int)((-id - A11Y_OPTIONS) & 0xFFFFF); }
static int a11y_has_options(const Item *it) { return it->kind == K_CHOICE || it->kind == K_LIST || it->kind == K_TABS; }

static void a11y_path(int64_t id, char *out, size_t size)
{
	if (id == 0)
		snprintf(out, size, A11Y_ROOT);
	else if (a11y_is_option(id))
		snprintf(out, size, A11Y_PREFIX "/o%lld_%d", (long long)a11y_option_owner(id), a11y_option_which(id));
	else if (id < 0)
		snprintf(out, size, A11Y_PREFIX "/s%lld", (long long)-id);
	else
		snprintf(out, size, A11Y_PREFIX "/%lld", (long long)id);
}

static int a11y_alive(int64_t id);

static int64_t a11y_id_of(const char *path)
{
	if (strcmp(path, A11Y_ROOT) == 0 || strcmp(path, A11Y_PREFIX) == 0)
		return 0;
	const char *last = strrchr(path, '/');
	if (!last)
		return A11Y_GONE;
	if (last[1] == 'o') {
		long long owner = atoll(last + 2);
		const char *u = strchr(last, '_');
		int which = u ? atoi(u + 1) : -1;
		if (owner < 1 || owner > nitems || which < 0 || which >= (1 << 20))
			return A11Y_GONE;
		int64_t id = a11y_option_id(owner, which);
		return a11y_alive(id) ? id : A11Y_GONE;
	}
	int special = last[1] == 's';
	long long n = atoll(last + 1 + special);
	if (n < 1 || (!special && n > nitems))
		return A11Y_GONE;
	int64_t id = special ? -n : n;
	return a11y_alive(id) ? id : A11Y_GONE;
}

/* the message or question showing over a window (0: none) */
static int a11y_dialog_of(int64_t win)
{
	return win >= 1 && win <= nitems && items[win - 1].kind == K_WINDOW ? items[win - 1].dialog : 0;
}

/* there to be read: in an open window */
static int a11y_alive(int64_t id)
{
	if (id == 0)
		return 1;
	if (a11y_is_option(id)) {
		int64_t o = a11y_option_owner(id);
		return o >= 1 && o <= nitems && a11y_has_options(&items[o - 1]) && a11y_option_which(id) < items[o - 1].nopts &&
		       a11y_alive(o);
	}
	if (id < 0) {
		int64_t w = a11y_win_of(id);
		if (w < 1 || w > nitems || items[w - 1].kind != K_WINDOW || !a11y_alive(w))
			return 0;
		switch (a11y_part(id)) {
		case P_BAR: return items[w - 1].nmenus > 0;
		case P_DIALOG: case P_YES: return a11y_dialog_of(w) != 0;
		case P_NO: return a11y_dialog_of(w) == D_ASK;
		}
		return 0;
	}
	if (id > nitems)
		return 0;
	Item *it = &items[id - 1];
	if (it->kind == K_WINDOW)
		return it->open && !it->destroyed;
	return window_alive(it->win) && items[it->win - 1].open;
}

/* boxes (rows, columns, the window's own) are left out: their insides
 * are their parent's */
static int a11y_transparent(const Item *it) { return it->kind == K_BOX || it->kind == K_SHORTCUT; }

static void a11y_push(int64_t **out, int *n, int64_t id)
{
	*out = realloc(*out, sizeof **out * (size_t)(*n + 1));
	(*out)[(*n)++] = id;
}

static void a11y_kids_of_box(int64_t box, int64_t **out, int *n)
{
	Item *b = &items[box - 1];
	for (int i = 0; i < b->nkids; i++) {
		int64_t k = b->kids[i];
		Item *it = &items[k - 1];
		if (it->hidden)
			continue;
		if (a11y_transparent(it)) {
			a11y_kids_of_box(k, out, n);
			continue;
		}
		a11y_push(out, n, k);
	}
}

/* what's in an object, in order (freed by the caller) */
static int a11y_children(int64_t id, int64_t **out)
{
	int n = 0;
	*out = NULL;
	if (id == 0) {
		for (int64_t i = 1; i <= nitems; i++)
			if (items[i - 1].kind == K_WINDOW && a11y_alive(i))
				a11y_push(out, &n, i);
		return n;
	}
	if (a11y_is_option(id)) {   /* (a tab: what's on it, when it's showing) */
		Item *o = &items[a11y_option_owner(id) - 1];
		int which = a11y_option_which(id);
		if (o->kind == K_TABS && which == o->selected && which < o->nkids)
			a11y_kids_of_box(o->kids[which], out, &n);
		return n;
	}
	if (id < 0) {
		int64_t w = a11y_win_of(id);
		if (a11y_part(id) == P_BAR)
			for (int i = 0; i < items[w - 1].nmenus; i++)
				a11y_push(out, &n, items[w - 1].menus[i]);
		else if (a11y_part(id) == P_DIALOG) {
			if (a11y_dialog_of(w) == D_ASK)
				a11y_push(out, &n, a11y_part_id(w, P_NO));
			a11y_push(out, &n, a11y_part_id(w, P_YES));
		}
		return n;
	}
	Item *it = &items[id - 1];
	if (it->kind == K_WINDOW) {
		/* the message or question showing over it, its menu bar, and
		 * what's in it */
		if (it->dialog)
			a11y_push(out, &n, a11y_part_id(id, P_DIALOG));
		if (it->nmenus)
			a11y_push(out, &n, a11y_part_id(id, P_BAR));
		a11y_kids_of_box(it->box, out, &n);
		return n;
	}
	if (a11y_has_options(it)) {
		for (int i = 0; i < it->nopts; i++)
			a11y_push(out, &n, a11y_option_id(id, i));
		return n;
	}
	if (it->kind == K_MENU) {
		a11y_kids_of_box(id, out, &n);
		return n;
	}
	return 0;
}

static int64_t a11y_parent(int64_t id)
{
	if (id == 0)
		return A11Y_DESKTOP;
	if (a11y_is_option(id))
		return a11y_option_owner(id);
	if (id < 0) {
		int p = a11y_part(id);
		return p == P_BAR || p == P_DIALOG ? a11y_win_of(id) : a11y_part_id(a11y_win_of(id), P_DIALOG);
	}
	Item *it = &items[id - 1];
	if (it->kind == K_WINDOW)
		return 0;
	if (it->kind == K_MENU)
		return a11y_part_id(it->win, P_BAR);
	for (int64_t p = it->parent, below = id; p; below = p, p = items[p - 1].parent) {
		if (items[p - 1].kind == K_TABS) {   /* (on a tab: the tab it's on) */
			for (int i = 0; i < items[p - 1].nkids; i++)
				if (items[p - 1].kids[i] == below)
					return a11y_option_id(p, i);
			return p;
		}
		if (!a11y_transparent(&items[p - 1]))
			return p;
	}
	return it->win;
}

static int a11y_role(int64_t id)
{
	if (id == 0)
		return R_APPLICATION;
	if (a11y_is_option(id))
		return items[a11y_option_owner(id) - 1].kind == K_TABS ? R_PAGE_TAB : R_LIST_ITEM;
	if (id < 0)
		switch (a11y_part(id)) {
		case P_BAR: return R_MENU_BAR;
		case P_DIALOG: return a11y_dialog_of(a11y_win_of(id)) == D_ASK ? R_DIALOG : R_ALERT;
		default: return R_PUSH_BUTTON;
		}
	switch (items[id - 1].kind) {
	case K_WINDOW: return R_FRAME;
	case K_LABEL: return R_LABEL;
	case K_BUTTON: return R_PUSH_BUTTON;
	case K_ENTRY: return R_ENTRY;
	case K_CHECK: return R_CHECK_BOX;
	case K_CHOICE: return R_COMBO_BOX;
	case K_TEXT_AREA: return R_TEXT;
	case K_LIST: return R_LIST;
	case K_SLIDER: return R_SLIDER;
	case K_PROGRESS: return R_PROGRESS_BAR;
	case K_IMAGE: return R_IMAGE;
	case K_VIDEO: return R_IMAGE;
	case K_SEPARATOR: return R_SEPARATOR;
	case K_CANVAS: return R_DRAWING_AREA;
	case K_MENU: return R_MENU;
	case K_MENU_ITEM: return items[id - 1].separator ? R_SEPARATOR : R_MENU_ITEM;
	case K_TABS: return R_PAGE_TAB_LIST;
	case K_TABLE: return R_TABLE;
	case K_NUMBER: return R_SPIN_BUTTON;
	case K_COLOR: return R_COLOR_CHOOSER;
	case K_FONT: return R_FONT_CHOOSER;
	}
	return R_PANEL;
}

static const char *a11y_role_name(int role)
{
	switch (role) {
	case R_APPLICATION: return "application";
	case R_FRAME: return "frame";
	case R_LABEL: return "label";
	case R_PUSH_BUTTON: return "push button";
	case R_ENTRY: return "entry";
	case R_CHECK_BOX: return "check box";
	case R_COMBO_BOX: return "combo box";
	case R_TEXT: return "text";
	case R_LIST: return "list";
	case R_SLIDER: return "slider";
	case R_PROGRESS_BAR: return "progress bar";
	case R_IMAGE: return "image";
	case R_SEPARATOR: return "separator";
	case R_DRAWING_AREA: return "drawing area";
	case R_MENU: return "menu";
	case R_MENU_BAR: return "menu bar";
	case R_MENU_ITEM: return "menu item";
	case R_PAGE_TAB_LIST: return "page tab list";
	case R_TABLE: return "table";
	case R_SPIN_BUTTON: return "spin button";
	case R_COLOR_CHOOSER: return "color chooser";
	case R_FONT_CHOOSER: return "font chooser";
	case R_ALERT: return "alert";
	case R_LIST_ITEM: return "list item";
	case R_PAGE_TAB: return "page tab";
	case R_DIALOG: return "dialog";
	}
	return "panel";
}

/* a text box's name: what it's said to be (set_description), or else the
 * label just before it ("Name:", then the box), as a reader would take it */
static const char *a11y_label_before(int64_t id)
{
	Item *it = &items[id - 1];
	if (!it->parent)
		return "";
	Item *b = &items[it->parent - 1];
	for (int i = 0; i < b->nkids; i++)
		if (b->kids[i] == id && i > 0 && items[b->kids[i - 1] - 1].kind == K_LABEL)
			return items[b->kids[i - 1] - 1].text ? items[b->kids[i - 1] - 1].text : "";
	return "";
}

static char a11y_app_name[256];
static const char *a11y_own_name(int64_t id);

static const char *a11y_name(int64_t id)
{
	if (id == 0)
		return a11y_app_name;
	if (a11y_is_option(id))
		return items[a11y_option_owner(id) - 1].opts[a11y_option_which(id)];
	if (id < 0) {
		int64_t w = a11y_win_of(id);
		switch (a11y_part(id)) {
		case P_DIALOG: return items[w - 1].dialog_text ? items[w - 1].dialog_text : "";
		case P_NO: return "No";
		case P_YES: return a11y_dialog_of(w) == D_ASK ? "Yes" : "OK";
		}
		return "";
	}
	const char *n = a11y_own_name(id);
	return *n ? n : items[id - 1].described ? items[id - 1].described : "";
}

static const char *a11y_own_name(int64_t id)
{
	Item *it = &items[id - 1];
	switch (it->kind) {
	case K_WINDOW: return it->title ? it->title : "";
	case K_LABEL:
	case K_BUTTON:
	case K_CHECK:
	case K_MENU:
	case K_MENU_ITEM: return it->text ? it->text : "";
	case K_ENTRY:
	case K_TEXT_AREA:
	case K_CHOICE:
	case K_LIST:
	case K_SLIDER:
	case K_NUMBER:
	case K_PROGRESS: return a11y_label_before(id);
	}
	return "";
}

static int a11y_focusable(const Item *it)
{
	switch (it->kind) {
	case K_BUTTON: case K_ENTRY: case K_CHECK: case K_CHOICE: case K_TEXT_AREA: case K_LIST: case K_SLIDER:
	case K_NUMBER: case K_TABLE: case K_COLOR: case K_FONT: case K_TABS: return 1;
	case K_CANVAS: return it->keyed.fn != NULL || it->clicked.fn != NULL;
	}
	return 0;
}

/* what has the keys in a window: a dialog's button while one is showing */
static int64_t a11y_focus_of(int64_t win)
{
	Item *w = &items[win - 1];
	if (w->dialog)
		return a11y_part_id(win, w->dialog == D_ASK && w->dialog_button == 0 ? P_NO : P_YES);
	if (w->popup >= 1 && w->popup <= nitems) {   /* (a menu or drop-down open: what's lit up in it) */
		Item *p = &items[w->popup - 1];
		if (p->kind == K_MENU && popup_hover >= 0 && popup_hover < p->nkids)
			return p->kids[popup_hover];
		if (p->kind == K_CHOICE && popup_hover >= 0 && popup_hover < p->nopts)
			return a11y_option_id(w->popup, popup_hover);
		return w->popup;
	}
	return w->focus;
}

static void a11y_states(int64_t id, uint32_t st[2])
{
	st[0] = st[1] = 0;
#define SET(bit) (st[(bit) / 32] |= 1u << ((bit) % 32))
	if (id <= 0) {
		if (a11y_alive(id)) {
			SET(S_ENABLED);
			SET(S_SENSITIVE);
			SET(S_VISIBLE);
			SET(S_SHOWING);
			if (a11y_is_option(id)) {
				Item *o = &items[a11y_option_owner(id) - 1];
				SET(S_SELECTABLE);
				if (o->selected == a11y_option_which(id))
					SET(S_SELECTED);
				if (a11y_focus_of(o->win) == id)
					SET(S_FOCUSED);
			} else if (id < 0 && a11y_part(id) >= P_NO) {
				SET(S_FOCUSABLE);
				if (a11y_focus_of(a11y_win_of(id)) == id)
					SET(S_FOCUSED);
			}
		}
		return;
	}
	Item *it = &items[id - 1];
	if (!a11y_alive(id))
		return;
	Item *w = &items[(it->kind == K_WINDOW ? id : it->win) - 1];
	if (!it->disabled) {
		SET(S_ENABLED);
		SET(S_SENSITIVE);
	}
	if (!it->hidden) {
		SET(S_VISIBLE);
		SET(S_SHOWING);
	}
	if (it->kind == K_WINDOW) {
		if (w->sw && (SDL_GetWindowFlags(w->sw) & SDL_WINDOW_INPUT_FOCUS))
			SET(S_ACTIVE);
		return;
	}
	if (a11y_focusable(it))
		SET(S_FOCUSABLE);
	if (a11y_focus_of(it->win) == id)
		SET(S_FOCUSED);
	if (it->kind == K_CHECK) {
		SET(S_CHECKABLE);
		if (it->checked)
			SET(S_CHECKED);
	}
	if ((it->kind == K_ENTRY || it->kind == K_TEXT_AREA || it->kind == K_NUMBER) && !it->read_only)
		SET(S_EDITABLE);
	if (it->kind == K_ENTRY || it->kind == K_NUMBER)
		SET(S_SINGLE_LINE);
	if (it->kind == K_TEXT_AREA)
		SET(S_MULTI_LINE);
#undef SET
}

/* ------------------------------------------------------------- doing */

static int a11y_actionable(int64_t id)
{
	if (a11y_is_option(id))
		return 1;
	if (id < 0)
		return a11y_part(id) >= P_NO;
	if (id == 0)
		return 0;
	Item *it = &items[id - 1];
	return it->kind == K_BUTTON || it->kind == K_CHECK || (it->kind == K_MENU_ITEM && !it->separator);
}

/* what a click does, as the script's "click" and "check" do it */
static void a11y_do(int64_t id)
{
	if (a11y_is_option(id)) {   /* an option: chosen */
		int64_t o = a11y_option_owner(id);
		if (usable(o)) {
			cby_gui_select(o, a11y_option_which(id));
			if (items[o - 1].kind != K_TABS)
				set_focus(items[o - 1].win, o);
		}
		return;
	}
	if (id < 0) {   /* a dialog's button */
		int64_t w = a11y_win_of(id);
		if (a11y_dialog_of(w))
			answer_dialog(w, a11y_dialog_of(w) == D_ASK ? a11y_part(id) == P_YES : 0);
		return;
	}
	Item *it = &items[id - 1];
	if (it->kind == K_MENU_ITEM) {
		if (!it->disabled)
			call(it->on_click, it->win);
	} else if (usable(id)) {
		if (it->kind == K_CHECK)
			cby_gui_check_set(id, !it->checked);
		else
			call(it->on_click, it->win);
	}
}

/* ------------------------------------------------------------- text, counted in characters */

static int64_t a11y_char_at(const unsigned char *p)
{
	if (p[0] < 0x80)
		return p[0];
	if ((p[0] & 0xE0) == 0xC0 && p[1])
		return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
	if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2])
		return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
	if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3])
		return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
	return 0xFFFD;
}

static const char *a11y_text(int64_t id)
{
	if (id <= 0)
		return "";
	Item *it = &items[id - 1];
	if (it->kind == K_CHOICE)
		return it->selected >= 0 && it->selected < it->nopts ? it->opts[it->selected] : "";
	return it->text ? it->text : "";
}

static int a11y_has_text(int64_t id)
{
	if (id <= 0)
		return 0;
	int k = items[id - 1].kind;
	return k == K_ENTRY || k == K_TEXT_AREA || k == K_LABEL || k == K_CHOICE;
}

/* text typed into, with a caret */
static int a11y_typed(int64_t id) { return id > 0 && (items[id - 1].kind == K_ENTRY || items[id - 1].kind == K_TEXT_AREA); }

static int a11y_chars(const char *s, int bytes)
{
	int n = 0;
	for (int i = 0; s[i] && (bytes < 0 || i < bytes); i++)
		if ((s[i] & 0xC0) != 0x80)
			n++;
	return n;
}

static int a11y_byte_of(const char *s, int chars)
{
	int i = 0, n = 0;
	for (; s[i]; i++)
		if ((s[i] & 0xC0) != 0x80) {
			if (n == chars)
				return i;
			n++;
		}
	return i;
}

/* ------------------------------------------------------------- answering */

/* an object's reference: (bus name, path) */
static void a11y_add_ref(int64_t msg, int64_t id)
{
	char path[128];
	cby_dbus_begin_struct(msg);
	if (id == A11Y_DESKTOP && a11y_parent_bus[0]) {
		cby_dbus_add_str(msg, a11y_parent_bus);
		cby_dbus_add_path(msg, a11y_parent_path);
	} else if (id == A11Y_NULL || id == A11Y_DESKTOP || id == A11Y_GONE) {
		cby_dbus_add_str(msg, "");
		cby_dbus_add_path(msg, "/org/a11y/atspi/null");
	} else {
		cby_dbus_add_str(msg, a11y_me);
		a11y_path(id, path, sizeof path);
		cby_dbus_add_path(msg, path);
	}
	cby_dbus_end_struct(msg);
}

static int64_t a11y_reply_begin(void) { return cby_dbus_answer_new(); }

static void a11y_reply(int64_t bus, int64_t msg)
{
	cby_dbus_reply(bus, msg);
	cby_dbus_message_free(msg);
}

static void a11y_kids(int64_t id, int64_t **out, int *n)
{
	*n = a11y_children(id, out);
}

static void a11y_screen_box(int64_t id, int *x, int *y, int *w, int *h, int on_screen)
{
	*x = *y = *w = *h = 0;
	if (id == 0)
		return;
	if (a11y_is_option(id))
		id = a11y_option_owner(id);   /* (where the drop-down, list or tabs are) */
	int64_t win = id < 0 ? a11y_win_of(id) : items[id - 1].kind == K_WINDOW ? id : items[id - 1].win;
	Item *wi = &items[win - 1];
	int wx = 0, wy = 0;
	if (on_screen && wi->sw)
		SDL_GetWindowPosition(wi->sw, &wx, &wy);
	if (id < 0) {   /* (a menu bar, or a dialog: across the window) */
		*x = wx;
		*y = wy;
		if (wi->sw)
			SDL_GetWindowSize(wi->sw, w, h);
		if (a11y_part(id) == P_BAR)
			*h = (int)bar_h(win);
		return;
	}
	Item *it = &items[id - 1];
	if (it->kind == K_WINDOW) {
		*x = wx;
		*y = wy;
		if (wi->sw)
			SDL_GetWindowSize(wi->sw, w, h);
		return;
	}
	*x = wx + it->x;
	*y = wy + it->y;
	*w = it->w;
	*h = it->h;
}

static void a11y_add_variant_property(int64_t msg, int64_t id, const char *iface, const char *name, int *found)
{
	*found = 1;
	if (strcmp(iface, "org.a11y.atspi.Accessible") == 0) {
		if (strcmp(name, "Name") == 0) {
			cby_dbus_begin_variant(msg, "s");
			cby_dbus_add_str(msg, a11y_name(id));
		} else if (strcmp(name, "Description") == 0) {
			cby_dbus_begin_variant(msg, "s");
			cby_dbus_add_str(msg, id > 0 && items[id - 1].described ? items[id - 1].described : "");
		} else if (strcmp(name, "Parent") == 0) {
			cby_dbus_begin_variant(msg, "(so)");
			int64_t p = a11y_parent(id);
			a11y_add_ref(msg, p);
		} else if (strcmp(name, "ChildCount") == 0) {
			int64_t *k;
			int n;
			a11y_kids(id, &k, &n);
			free(k);
			cby_dbus_begin_variant(msg, "i");
			cby_dbus_add_int(msg, n);
		} else if (strcmp(name, "Locale") == 0) {
			cby_dbus_begin_variant(msg, "s");
			const char *l = getenv("LANG");
			cby_dbus_add_str(msg, l && *l ? l : "C");
		} else if (strcmp(name, "AccessibleId") == 0 || strcmp(name, "HelpText") == 0) {
			cby_dbus_begin_variant(msg, "s");
			cby_dbus_add_str(msg, "");
		} else {
			*found = 0;
			return;
		}
	} else if (strcmp(iface, "org.a11y.atspi.Application") == 0) {
		if (strcmp(name, "ToolkitName") == 0) {
			cby_dbus_begin_variant(msg, "s");
			cby_dbus_add_str(msg, "Cabby");
		} else if (strcmp(name, "Version") == 0) {
			cby_dbus_begin_variant(msg, "s");
			cby_dbus_add_str(msg, CABBY_VERSION);
		} else if (strcmp(name, "AtspiVersion") == 0) {
			cby_dbus_begin_variant(msg, "s");
			cby_dbus_add_str(msg, "2.1");
		} else if (strcmp(name, "Id") == 0) {
			cby_dbus_begin_variant(msg, "i");
			cby_dbus_add_int(msg, 0);
		} else {
			*found = 0;
			return;
		}
	} else if (strcmp(iface, "org.a11y.atspi.Text") == 0 && a11y_has_text(id)) {
		const char *t = a11y_text(id);
		if (strcmp(name, "CharacterCount") == 0) {
			cby_dbus_begin_variant(msg, "i");
			cby_dbus_add_int(msg, a11y_chars(t, -1));
		} else if (strcmp(name, "CaretOffset") == 0) {
			cby_dbus_begin_variant(msg, "i");
			cby_dbus_add_int(msg, !a11y_typed(id) ? 0 : a11y_chars(t, items[id - 1].cursor));
		} else {
			*found = 0;
			return;
		}
	} else if (strcmp(iface, "org.a11y.atspi.Value") == 0 && id > 0) {
		Item *it = &items[id - 1];
		double lo = it->min, hi = it->max, v = it->value;
		if (it->kind == K_PROGRESS) {
			lo = 0;
			hi = 1;
			v = it->fraction;
		}
		cby_dbus_begin_variant(msg, strcmp(name, "Text") == 0 ? "s" : "d");
		if (strcmp(name, "MinimumValue") == 0)
			cby_dbus_add_double(msg, lo);
		else if (strcmp(name, "MaximumValue") == 0)
			cby_dbus_add_double(msg, hi);
		else if (strcmp(name, "MinimumIncrement") == 0)
			cby_dbus_add_double(msg, it->kind == K_PROGRESS ? 0.01 : 1);
		else if (strcmp(name, "CurrentValue") == 0)
			cby_dbus_add_double(msg, v);
		else if (strcmp(name, "Text") == 0)
			cby_dbus_add_str(msg, "");
		else {
			cby_dbus_add_double(msg, 0);
			*found = 0;
		}
	} else if (strcmp(iface, "org.a11y.atspi.Action") == 0 && strcmp(name, "NActions") == 0) {
		cby_dbus_begin_variant(msg, "i");
		cby_dbus_add_int(msg, a11y_actionable(id) ? 1 : 0);
	} else {
		*found = 0;
		return;
	}
	cby_dbus_end_variant(msg);
}

/* which interfaces an object has */
static void a11y_interfaces(int64_t id, const char **out, int *n)
{
	*n = 0;
	out[(*n)++] = "org.a11y.atspi.Accessible";
	if (id == 0) {
		out[(*n)++] = "org.a11y.atspi.Application";
		return;
	}
	out[(*n)++] = "org.a11y.atspi.Component";
	if (a11y_actionable(id))
		out[(*n)++] = "org.a11y.atspi.Action";
	if (a11y_has_text(id)) {
		out[(*n)++] = "org.a11y.atspi.Text";
	}
	if (id > 0 && (items[id - 1].kind == K_SLIDER || items[id - 1].kind == K_PROGRESS || items[id - 1].kind == K_NUMBER))
		out[(*n)++] = "org.a11y.atspi.Value";
}

static int a11y_index_in_parent(int64_t id)
{
	if (id == 0)
		return -1;
	int64_t p = a11y_parent(id);
	int64_t *k;
	int n, at = -1;
	a11y_kids(p, &k, &n);
	for (int i = 0; i < n; i++)
		if (k[i] == id)
			at = i;
	free(k);
	return at;
}

/* a word, line or the like around a character: [*start, *end) in characters */
static void a11y_around(const char *t, int offset, int granularity, int *start, int *end)
{
	int n = a11y_chars(t, -1);
	if (offset < 0)
		offset = 0;
	if (offset > n)
		offset = n;
	if (granularity == 0) {   /* a character */
		*start = offset;
		*end = offset < n ? offset + 1 : n;
		return;
	}
	if (granularity == 1) {   /* a word: the letters around it */
		int a = offset, b = offset;
		const char *p = t + a11y_byte_of(t, a);
		while (a > 0) {
			const char *q = t + a11y_byte_of(t, a - 1);
			if (*q == ' ' || *q == '\n' || *q == '\t')
				break;
			a--;
		}
		while (b < n) {
			p = t + a11y_byte_of(t, b);
			if (*p == ' ' || *p == '\n' || *p == '\t')
				break;
			b++;
		}
		*start = a;
		*end = b;
		return;
	}
	/* a sentence, line or paragraph: the line it's on */
	int a = offset, b = offset;
	while (a > 0 && t[a11y_byte_of(t, a - 1)] != '\n')
		a--;
	while (b < n && t[a11y_byte_of(t, b)] != '\n')
		b++;
	*start = a;
	*end = b;
}

static void a11y_add_text_range(int64_t msg, const char *t, int a, int b)
{
	int ba = a11y_byte_of(t, a), bb = a11y_byte_of(t, b);
	char *part = strndup(t + ba, (size_t)(bb > ba ? bb - ba : 0));
	cby_dbus_add_str(msg, part);
	free(part);
}

static void a11y_call(void *ctx, int64_t bus, const char *path, const char *iface, const char *member, int64_t args)
{
	(void)ctx;
	int64_t id = a11y_id_of(path);
	if (id == A11Y_GONE) {
		cby_dbus_fail(bus, "org.freedesktop.DBus.Error.UnknownObject", "that's gone");
		return;
	}
	int64_t m = 0;
	int64_t a0 = cby_json_length(args) > 0 ? cby_json_at(args, 0) : 0;
	int64_t a1 = cby_json_length(args) > 1 ? cby_json_at(args, 1) : 0;
	int64_t a2 = cby_json_length(args) > 2 ? cby_json_at(args, 2) : 0;
	if (strcmp(iface, "org.freedesktop.DBus.Properties") == 0) {
		if (strcmp(member, "Get") == 0 && a0 && a1) {
			int found;
			m = a11y_reply_begin();
			a11y_add_variant_property(m, id, cby_json_str_of(a0), cby_json_str_of(a1), &found);
			if (found)
				a11y_reply(bus, m);
			else {
				cby_dbus_message_free(m);
				m = 0;
				cby_dbus_fail(bus, "org.freedesktop.DBus.Error.UnknownProperty", "there's no such property");
			}
		} else if (strcmp(member, "GetAll") == 0 && a0) {
			static const char *const names[] = { "Name", "Description", "Parent", "ChildCount", "Locale", "ToolkitName",
							     "Version", "AtspiVersion", "CharacterCount", "CaretOffset",
							     "MinimumValue", "MaximumValue", "CurrentValue", "NActions", NULL };
			m = a11y_reply_begin();
			cby_dbus_begin_lookup(m, "s", "v");
			for (int i = 0; names[i]; i++) {
				/* (each the interface asked has: tried into a message of its own first) */
				int64_t trial = a11y_reply_begin();
				int found;
				a11y_add_variant_property(trial, id, cby_json_str_of(a0), names[i], &found);
				cby_dbus_message_free(trial);
				if (!found)
					continue;
				cby_dbus_add_str(m, names[i]);
				a11y_add_variant_property(m, id, cby_json_str_of(a0), names[i], &found);
			}
			cby_dbus_end_lookup(m);
			a11y_reply(bus, m);
		}
	} else if (strcmp(iface, "org.a11y.atspi.Accessible") == 0) {
		m = a11y_reply_begin();
		if (strcmp(member, "GetRole") == 0)
			cby_dbus_add_uint(m, a11y_role(id));
		else if (strcmp(member, "GetRoleName") == 0 || strcmp(member, "GetLocalizedRoleName") == 0)
			cby_dbus_add_str(m, a11y_role_name(a11y_role(id)));
		else if (strcmp(member, "GetState") == 0) {
			uint32_t st[2];
			a11y_states(id, st);
			cby_dbus_begin_list(m, "u");
			cby_dbus_add_uint(m, st[0]);
			cby_dbus_add_uint(m, st[1]);
			cby_dbus_end_list(m);
		} else if (strcmp(member, "GetChildren") == 0 || strcmp(member, "GetChildAtIndex") == 0) {
			int64_t *k;
			int n;
			a11y_kids(id, &k, &n);
			if (member[3] == 'C' && member[8] == 'r') {   /* GetChildren */
				cby_dbus_begin_list(m, "(so)");
				for (int i = 0; i < n; i++)
					a11y_add_ref(m, k[i]);
				cby_dbus_end_list(m);
			} else {
				int64_t i = a0 ? cby_json_long(a0) : -1;
				a11y_add_ref(m, i >= 0 && i < n ? k[i] : A11Y_NULL);
			}
			free(k);
		} else if (strcmp(member, "GetIndexInParent") == 0)
			cby_dbus_add_int(m, a11y_index_in_parent(id));
		else if (strcmp(member, "GetRelationSet") == 0) {
			cby_dbus_begin_list(m, "(ua(so))");
			cby_dbus_end_list(m);
		} else if (strcmp(member, "GetAttributes") == 0) {
			cby_dbus_begin_lookup(m, "s", "s");
			cby_dbus_add_str(m, "toolkit");
			cby_dbus_add_str(m, "Cabby");
			cby_dbus_end_lookup(m);
		} else if (strcmp(member, "GetApplication") == 0)
			a11y_add_ref(m, 0);
		else if (strcmp(member, "GetInterfaces") == 0) {
			const char *names[8];
			int n;
			a11y_interfaces(id, names, &n);
			cby_dbus_begin_list(m, "s");
			for (int i = 0; i < n; i++)
				cby_dbus_add_str(m, names[i]);
			cby_dbus_end_list(m);
		} else {
			cby_dbus_message_free(m);
			m = 0;
		}
		if (m)
			a11y_reply(bus, m);
	} else if (strcmp(iface, "org.a11y.atspi.Component") == 0) {
		int x, y, w, h;
		int on_screen = a0 && cby_json_kind(a0) != 0 ? cby_json_long(a0) == 0 : 1;
		m = a11y_reply_begin();
		if (strcmp(member, "GetExtents") == 0) {
			a11y_screen_box(id, &x, &y, &w, &h, on_screen);
			cby_dbus_begin_struct(m);
			cby_dbus_add_int(m, x);
			cby_dbus_add_int(m, y);
			cby_dbus_add_int(m, w);
			cby_dbus_add_int(m, h);
			cby_dbus_end_struct(m);
		} else if (strcmp(member, "GetPosition") == 0 || strcmp(member, "GetSize") == 0) {
			a11y_screen_box(id, &x, &y, &w, &h, on_screen);
			cby_dbus_add_int(m, member[3] == 'P' ? x : w);
			cby_dbus_add_int(m, member[3] == 'P' ? y : h);
		} else if (strcmp(member, "GetLayer") == 0)
			cby_dbus_add_uint(m, id > 0 && items[id - 1].kind == K_WINDOW ? 7 : 3);   /* (window, widget) */
		else if (strcmp(member, "GetMDIZOrder") == 0)
			cby_dbus_add_int(m, 0);
		else if (strcmp(member, "GetAlpha") == 0)
			cby_dbus_add_double(m, 1);
		else if (strcmp(member, "GrabFocus") == 0) {
			int ok = id > 0 && a11y_focusable(&items[id - 1]) && usable(id);
			if (ok)
				set_focus(items[id - 1].win, id);
			cby_dbus_add_bool(m, ok);
		} else if (strcmp(member, "Contains") == 0 || strcmp(member, "GetAccessibleAtPoint") == 0) {
			int px = a0 ? (int)cby_json_long(a0) : 0, py = a1 ? (int)cby_json_long(a1) : 0;
			int screen = a2 ? cby_json_long(a2) == 0 : 1;
			if (member[0] == 'C') {
				a11y_screen_box(id, &x, &y, &w, &h, screen);
				cby_dbus_add_bool(m, px >= x && py >= y && px < x + w && py < y + h);
			} else {
				int64_t *k;
				int n;
				int64_t found = A11Y_NULL;
				a11y_kids(id, &k, &n);
				for (int i = 0; i < n; i++) {
					a11y_screen_box(k[i], &x, &y, &w, &h, screen);
					if (px >= x && py >= y && px < x + w && py < y + h)
						found = k[i];
				}
				free(k);
				a11y_add_ref(m, found);
			}
		} else {
			cby_dbus_message_free(m);
			m = 0;
		}
		if (m)
			a11y_reply(bus, m);
	} else if (strcmp(iface, "org.a11y.atspi.Action") == 0 && a11y_actionable(id)) {
		const char *verb = id > 0 && items[id - 1].kind == K_CHECK ? "toggle" : "click";
		const char *keys = id > 0 && items[id - 1].keys_label ? items[id - 1].keys_label : "";
		m = a11y_reply_begin();
		if (strcmp(member, "GetActions") == 0) {
			cby_dbus_begin_list(m, "(sss)");
			cby_dbus_begin_struct(m);
			cby_dbus_add_str(m, verb);
			cby_dbus_add_str(m, "");
			cby_dbus_add_str(m, keys);
			cby_dbus_end_struct(m);
			cby_dbus_end_list(m);
		} else if (strcmp(member, "GetName") == 0 || strcmp(member, "GetLocalizedName") == 0)
			cby_dbus_add_str(m, verb);
		else if (strcmp(member, "GetDescription") == 0)
			cby_dbus_add_str(m, "");
		else if (strcmp(member, "GetKeyBinding") == 0)
			cby_dbus_add_str(m, keys);
		else if (strcmp(member, "DoAction") == 0) {
			/* answered first: what it does may open a dialog, or ask the reader things */
			cby_dbus_add_bool(m, a11y_is_option(id) ? usable(a11y_option_owner(id)) : id < 0 || usable(id));
			a11y_reply(bus, m);
			m = 0;
			a11y_do(id);
		} else {
			cby_dbus_message_free(m);
			m = -1;
		}
		if (m > 0)
			a11y_reply(bus, m);
	} else if (strcmp(iface, "org.a11y.atspi.Text") == 0 && a11y_has_text(id)) {
		const char *t = a11y_text(id);
		Item *it = &items[id - 1];
		m = a11y_reply_begin();
		if (strcmp(member, "GetText") == 0) {
			int n = a11y_chars(t, -1);
			int a = a0 ? (int)cby_json_long(a0) : 0, b = a1 ? (int)cby_json_long(a1) : -1;
			if (b < 0 || b > n)
				b = n;
			if (a < 0)
				a = 0;
			a11y_add_text_range(m, t, a, b < a ? a : b);
		} else if (strcmp(member, "GetCaretOffset") == 0)
			cby_dbus_add_int(m, !a11y_typed(id) ? 0 : a11y_chars(t, it->cursor));
		else if (strcmp(member, "SetCaretOffset") == 0) {
			if (a11y_typed(id) && a0) {
				it->cursor = it->anchor = a11y_byte_of(t, (int)cby_json_long(a0));
				items[it->win - 1].dirty = 1;
			}
			cby_dbus_add_bool(m, a11y_typed(id));
		} else if (strcmp(member, "GetCharacterAtOffset") == 0) {
			int at = a0 ? (int)cby_json_long(a0) : 0;
			const unsigned char *p = (const unsigned char *)t + a11y_byte_of(t, at);
			cby_dbus_add_int(m, *p ? a11y_char_at(p) : 0);
		} else if (strcmp(member, "GetStringAtOffset") == 0 || strcmp(member, "GetTextAtOffset") == 0 ||
			   strcmp(member, "GetTextBeforeOffset") == 0 || strcmp(member, "GetTextAfterOffset") == 0) {
			int at = a0 ? (int)cby_json_long(a0) : 0, kind = a1 ? (int)cby_json_long(a1) : 0;
			/* (GetTextAtOffset's kinds: 0 a character, 1-2 a word, 3-4 a sentence, 5-6 a line; GetStringAtOffset's:
			 * 0 a character, 1 a word, 2 a sentence, 3 a line, 4 a paragraph) */
			int granularity = member[3] == 'S' ? kind : kind == 0 ? 0 : kind <= 2 ? 1 : 3;
			int a, b;
			a11y_around(t, at, granularity, &a, &b);
			if (member[7] == 'B' && a > 0)
				a11y_around(t, a - 1, granularity, &a, &b);
			else if (member[7] == 'A')
				a11y_around(t, b + 1, granularity, &a, &b);
			a11y_add_text_range(m, t, a, b);
			cby_dbus_add_int(m, a);
			cby_dbus_add_int(m, b);
		} else if (strcmp(member, "GetNSelections") == 0)
			cby_dbus_add_int(m, a11y_typed(id) && it->anchor != it->cursor ? 1 : 0);
		else if (strcmp(member, "GetSelection") == 0) {
			int a = a11y_chars(t, it->anchor < it->cursor ? it->anchor : it->cursor);
			int b = a11y_chars(t, it->anchor < it->cursor ? it->cursor : it->anchor);
			cby_dbus_add_int(m, !a11y_typed(id) ? 0 : a);
			cby_dbus_add_int(m, !a11y_typed(id) ? 0 : b);
		} else if (strcmp(member, "GetAttributeRun") == 0 || strcmp(member, "GetAttributes") == 0) {
			int n = a11y_chars(t, -1);
			cby_dbus_begin_lookup(m, "s", "s");
			cby_dbus_end_lookup(m);
			cby_dbus_add_int(m, 0);
			cby_dbus_add_int(m, n);
		} else if (strcmp(member, "GetDefaultAttributes") == 0 || strcmp(member, "GetDefaultAttributeSet") == 0) {
			cby_dbus_begin_lookup(m, "s", "s");
			cby_dbus_end_lookup(m);
		} else if (strcmp(member, "GetCharacterExtents") == 0 || strcmp(member, "GetRangeExtents") == 0) {
			int x, y, w, h;
			a11y_screen_box(id, &x, &y, &w, &h, 1);
			cby_dbus_add_int(m, x);
			cby_dbus_add_int(m, y);
			cby_dbus_add_int(m, member[3] == 'C' ? 8 : w);
			cby_dbus_add_int(m, h);
		} else {
			cby_dbus_message_free(m);
			m = 0;
		}
		if (m)
			a11y_reply(bus, m);
	}
	/* (the Value interface is its properties alone) */
	if (a0)
		cby_json_release(a0);
	if (a1)
		cby_json_release(a1);
	if (a2)
		cby_json_release(a2);
}

/* ------------------------------------------------------------- telling the reader */

static void a11y_signal(int64_t id, const char *iface, const char *member, const char *detail, int d1, int d2,
			const char *vsig, int64_t vref, const char *vtext)
{
	if (!a11y_bus)
		return;
	char path[128];
	a11y_path(id, path, sizeof path);
	int64_t m = cby_dbus_signal_new(path, iface, member);
	cby_dbus_add_str(m, detail);
	cby_dbus_add_int(m, d1);
	cby_dbus_add_int(m, d2);
	cby_dbus_begin_variant(m, vsig);
	if (vsig[0] == 's')
		cby_dbus_add_str(m, vtext ? vtext : "");
	else if (vsig[0] == '(')
		a11y_add_ref(m, vref);
	else
		cby_dbus_add_int(m, 0);
	cby_dbus_end_variant(m);
	cby_dbus_begin_lookup(m, "s", "v");
	cby_dbus_end_lookup(m);
	cby_dbus_send(a11y_bus, m);
	cby_dbus_message_free(m);
}

/* what was last told, to tell the reader what changes */
typedef struct {
	int64_t focus;          /* each window's */
	int64_t win_active;
	char *text;             /* the focused item's text, and caret */
	int caret;
	int checked, selected, value;
	int open;
} A11yWas;

static A11yWas *a11y_was;
static int na11y_was;
static int64_t a11y_active_win;

static void a11y_tick(void)
{
	if (!a11y_bus)
		return;
	if (na11y_was < nitems) {
		a11y_was = realloc(a11y_was, sizeof *a11y_was * (size_t)nitems);
		memset(a11y_was + na11y_was, 0, sizeof *a11y_was * (size_t)(nitems - na11y_was));
		na11y_was = nitems;
	}
	for (int64_t w = 1; w <= nitems; w++) {
		Item *wi = &items[w - 1];
		if (wi->kind != K_WINDOW)
			continue;
		A11yWas *was = &a11y_was[w - 1];
		int open = a11y_alive(w);
		if (open != was->open) {
			was->open = open;
			a11y_signal(0, "org.a11y.atspi.Event.Object", "ChildrenChanged", open ? "add" : "remove", 0, 0, "(so)",
				    w, NULL);
			a11y_signal(w, "org.a11y.atspi.Event.Window", open ? "Create" : "Destroy", "", 0, 0, "i", 0, NULL);
		}
		if (!open)
			continue;
		int active = wi->sw && (SDL_GetWindowFlags(wi->sw) & SDL_WINDOW_INPUT_FOCUS);
		if (active && a11y_active_win != w) {
			a11y_active_win = w;
			a11y_signal(w, "org.a11y.atspi.Event.Window", "Activate", "", 0, 0, "i", 0, NULL);
			a11y_signal(w, "org.a11y.atspi.Event.Object", "StateChanged", "active", 1, 0, "i", 0, NULL);
		}
		int64_t f = a11y_focus_of(w);
		if (f != was->focus) {
			if (was->focus && a11y_alive(was->focus))
				a11y_signal(was->focus, "org.a11y.atspi.Event.Object", "StateChanged", "focused", 0, 0, "i", 0, NULL);
			was->focus = f;
			free(was->text);
			was->text = NULL;
			if (f) {
				if (a11y_is_option(f)) {
					a11y_signal(f, "org.a11y.atspi.Event.Object", "StateChanged", "focused", 1, 0, "i", 0, NULL);
					a11y_signal(f, "org.a11y.atspi.Event.Focus", "Focus", "", 0, 0, "i", 0, NULL);
					continue;
				}
				if (f < 0) {   /* (a dialog's button: the dialog's come up) */
					a11y_signal(w, "org.a11y.atspi.Event.Object", "ChildrenChanged", "add", 0, 0, "(so)",
						    a11y_part_id(w, P_DIALOG), NULL);
					a11y_signal(f, "org.a11y.atspi.Event.Object", "StateChanged", "focused", 1, 0, "i", 0, NULL);
					a11y_signal(f, "org.a11y.atspi.Event.Focus", "Focus", "", 0, 0, "i", 0, NULL);
					continue;
				}
				Item *it = &items[f - 1];
				a11y_signal(f, "org.a11y.atspi.Event.Object", "StateChanged", "focused", 1, 0, "i", 0, NULL);
				a11y_signal(f, "org.a11y.atspi.Event.Focus", "Focus", "", 0, 0, "i", 0, NULL);
				was->text = strdup(a11y_text(f));
				was->caret = it->cursor;
				was->checked = it->checked;
				was->selected = it->selected;
				was->value = it->value;
			}
			continue;
		}
		if (f <= 0)
			continue;
		/* the focused item: what changed in it */
		Item *it = &items[f - 1];
		const char *now = a11y_text(f);
		if (was->text && strcmp(was->text, now) != 0) {
			/* the part that's different: where it starts, and what went and came */
			const char *old = was->text;
			int s = 0;
			while (old[s] && old[s] == now[s])
				s++;
			while (s > 0 && (old[s] & 0xC0) == 0x80)
				s--;
			int lo = (int)strlen(old), ln = (int)strlen(now);
			int e = 0;
			while (e < lo - s && e < ln - s && old[lo - 1 - e] == now[ln - 1 - e])
				e++;
			while (e > 0 && (now[ln - e] & 0xC0) == 0x80)
				e--;
			int start = a11y_chars(old, s);
			if (lo - s - e > 0) {
				char *gone = strndup(old + s, (size_t)(lo - s - e));
				a11y_signal(f, "org.a11y.atspi.Event.Object", "TextChanged", "delete", start, a11y_chars(gone, -1),
					    "s", 0, gone);
				free(gone);
			}
			if (ln - s - e > 0) {
				char *came = strndup(now + s, (size_t)(ln - s - e));
				a11y_signal(f, "org.a11y.atspi.Event.Object", "TextChanged", "insert", start, a11y_chars(came, -1),
					    "s", 0, came);
				free(came);
			}
			free(was->text);
			was->text = strdup(now);
		}
		if (it->cursor != was->caret && (it->kind == K_ENTRY || it->kind == K_TEXT_AREA)) {
			was->caret = it->cursor;
			a11y_signal(f, "org.a11y.atspi.Event.Object", "TextCaretMoved", "", a11y_chars(now, it->cursor), 0, "i", 0,
				    NULL);
		}
		if (it->selected != was->selected && a11y_has_options(it)) {
			was->selected = it->selected;
			a11y_signal(f, "org.a11y.atspi.Event.Object", "SelectionChanged", "", 0, 0, "i", 0, NULL);
			if (it->selected >= 0 && it->selected < it->nopts)
				a11y_signal(a11y_option_id(f, it->selected), "org.a11y.atspi.Event.Object", "StateChanged", "selected", 1,
					    0, "i", 0, NULL);
		}
		if (it->checked != was->checked) {
			was->checked = it->checked;
			a11y_signal(f, "org.a11y.atspi.Event.Object", "StateChanged", "checked", it->checked != 0, 0, "i", 0, NULL);
		}
		if (it->value != was->value && (it->kind == K_SLIDER || it->kind == K_NUMBER)) {
			was->value = it->value;
			a11y_signal(f, "org.a11y.atspi.Event.Object", "PropertyChange", "accessible-value", 0, 0, "i", 0, NULL);
		}
	}
}

/* ------------------------------------------------------------- joining the bus */

/* whether a screen reader wants us: org.a11y.Status's IsEnabled or
 * ScreenReaderEnabled, on the session bus */
static int a11y_wanted(int64_t session)
{
	int on = 0;
	const char *names[] = { "IsEnabled", "ScreenReaderEnabled" };
	for (int i = 0; i < 2 && !on; i++) {
		int64_t m = cby_dbus_message_new("org.a11y.Bus", "/org/a11y/bus", "org.freedesktop.DBus.Properties", "Get");
		cby_dbus_add_str(m, "org.a11y.Status");
		cby_dbus_add_str(m, names[i]);
		int64_t got = cby_dbus_try_call(session, m);
		cby_dbus_message_free(m);
		if (got) {
			int64_t v = cby_json_at(got, 0);
			on = cby_json_kind(v) == 3 /* (J_BOOL) */ && cby_json_bool(v);
			cby_json_release(v);
			cby_json_release(got);
		}
	}
	return on;
}

static void a11y_start(void)
{
	if (a11y_tried)
		return;
	a11y_tried = 1;
#ifdef __APPLE__
	return;   /* (a Mac's VoiceOver: still to come) */
#endif
	const char *want = getenv("CABBY_ACCESSIBILITY");
	if (want && strcmp(want, "0") == 0)
		return;
	int forced = want && strcmp(want, "1") == 0;
	if (!forced && scripted())
		return;   /* (a test's window: not the desktop's business, unless it's asked) */
	const char *address = getenv("AT_SPI_BUS_ADDRESS");
	char *asked = NULL;
	int64_t session = 0;
	if (!forced || !address || !*address) {
		session = cby_dbus_try_session();
		if (!session)
			return;
		if (!forced && !a11y_wanted(session)) {
			cby_dbus_close(session);
			return;
		}
		if (!address || !*address) {
			int64_t m = cby_dbus_message_new("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
			int64_t got = cby_dbus_try_call(session, m);
			cby_dbus_message_free(m);
			if (got) {
				int64_t v = cby_json_at(got, 0);
				asked = strdup(cby_json_str_of(v));
				cby_json_release(v);
				cby_json_release(got);
			}
			address = asked;
		}
	}
	if (session)
		cby_dbus_close(session);
	if (!address || !*address) {
		free(asked);
		return;
	}
	a11y_bus = cby_dbus_try_address(address);
	free(asked);
	if (!a11y_bus)
		return;
	int64_t n = cby_dbus_text(a11y_bus, 0);
	if (n > 0 && n < (int64_t)sizeof a11y_me)
		cby_dbus_take_text(a11y_me);
	a11y_me[n > 0 && n < (int64_t)sizeof a11y_me ? n : 0] = 0;
	/* the program's name: its file's, without the folder or .cby (as
	 * `cabby greeter.cby` or ./greeter was run) */
	snprintf(a11y_app_name, sizeof a11y_app_name, "cabby");
	FILE *cmd = fopen("/proc/self/cmdline", "rb");
	if (cmd) {
		char buf[4096];
		size_t got = fread(buf, 1, sizeof buf - 1, cmd);
		fclose(cmd);
		buf[got] = 0;
		const char *pick = buf;
		for (size_t i = 0; i < got; i += strlen(buf + i) + 1) {
			size_t l = strlen(buf + i);
			if (l > 4 && strcmp(buf + i + l - 4, ".cby") == 0) {
				pick = buf + i;
				break;
			}
		}
		const char *base = strrchr(pick, '/');
		base = base ? base + 1 : pick;
		if (*base) {
			snprintf(a11y_app_name, sizeof a11y_app_name, "%.250s", base);
			size_t l = strlen(a11y_app_name);
			if (l > 4 && strcmp(a11y_app_name + l - 4, ".cby") == 0)
				a11y_app_name[l - 4] = 0;
		}
	}
	cby_dbus_serve_tree(a11y_bus, A11Y_PREFIX, a11y_call, NULL);
	/* and the registry told we're here: our root's parent is its desktop */
	int64_t m = cby_dbus_message_new("org.a11y.atspi.Registry", A11Y_ROOT, "org.a11y.atspi.Socket", "Embed");
	cby_dbus_begin_struct(m);
	cby_dbus_add_str(m, a11y_me);
	cby_dbus_add_path(m, A11Y_ROOT);
	cby_dbus_end_struct(m);
	int64_t got = cby_dbus_try_call(a11y_bus, m);
	cby_dbus_message_free(m);
	if (got) {
		int64_t ref = cby_json_at(got, 0);
		int64_t b = cby_json_at(ref, 0), p = cby_json_at(ref, 1);
		snprintf(a11y_parent_bus, sizeof a11y_parent_bus, "%s", cby_json_str_of(b));
		snprintf(a11y_parent_path, sizeof a11y_parent_path, "%s", cby_json_str_of(p));
		cby_json_release(b);
		cby_json_release(p);
		cby_json_release(ref);
		cby_json_release(got);
	}
}
