/* gtk_min.h — the GTK 4 / GLib declarations gui.c uses.
 *
 * Only for building where GTK's development headers are not installed but
 * the GTK 4 library is (gui.c includes <gtk/gtk.h> whenever it exists).
 * These match GTK 4's real C interface (4.10 or newer). Every type is a
 * distinct struct, and the cast macros mirror GTK's, so code that passes
 * the wrong kind of pointer fails here just as it would against the real
 * headers. */
#ifndef CABBY_GTK_MIN_H
#define CABBY_GTK_MIN_H

#include <stddef.h>
#include <stdint.h>

typedef int gboolean;
typedef unsigned int guint;
typedef int64_t gint64;
typedef void *gpointer;
typedef const void *gconstpointer;
typedef unsigned char guchar;
typedef size_t gsize;
typedef uint32_t guint32;

typedef struct _GObject GObject;
typedef struct _GParamSpec GParamSpec;
typedef struct _GListModel GListModel;
typedef struct _GAsyncResult GAsyncResult;
typedef struct _GCancellable GCancellable;
typedef struct _GFile GFile;
typedef struct _GError GError;
typedef struct _GMainContext GMainContext;
typedef struct _GdkDisplay GdkDisplay;

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkBox GtkBox;
typedef struct _GtkLabel GtkLabel;
typedef struct _GtkButton GtkButton;
typedef struct _GtkEditable GtkEditable;
typedef struct _GtkCheckButton GtkCheckButton;
typedef struct _GtkDropDown GtkDropDown;
typedef struct _GtkStringList GtkStringList;
typedef struct _GtkTextView GtkTextView;
typedef struct _GtkTextBuffer GtkTextBuffer;
typedef struct _GtkScrolledWindow GtkScrolledWindow;
typedef struct _GtkListBox GtkListBox;
typedef struct _GtkListBoxRow GtkListBoxRow;
typedef struct _GtkScale GtkScale;
typedef struct _GtkRange GtkRange;
typedef struct _GtkProgressBar GtkProgressBar;
typedef struct _GtkPicture GtkPicture;
typedef struct _GtkAlertDialog GtkAlertDialog;
typedef struct _GtkFileDialog GtkFileDialog;
typedef struct _GtkNative GtkNative;
typedef struct _GdkSnapshot GdkSnapshot;
typedef GdkSnapshot GtkSnapshot;
typedef struct _GdkPaintable GdkPaintable;
typedef struct _GdkTexture GdkTexture;
typedef struct _GskRenderNode GskRenderNode;
typedef struct _GskRenderer GskRenderer;
typedef struct _GtkDrawingArea GtkDrawingArea;
typedef struct _GtkNotebook GtkNotebook;
typedef struct _GtkSpinButton GtkSpinButton;
typedef struct _GtkColorDialog GtkColorDialog;
typedef struct _GtkColorDialogButton GtkColorDialogButton;
typedef struct _GtkFontDialog GtkFontDialog;
typedef struct _GtkFontDialogButton GtkFontDialogButton;
typedef struct _PangoFontDescription PangoFontDescription;
typedef struct _GtkColumnView GtkColumnView;
typedef struct _GtkColumnViewColumn GtkColumnViewColumn;
typedef struct _GtkListItemFactory GtkListItemFactory;
typedef struct _GtkListItem GtkListItem;
typedef struct _GtkStringObject GtkStringObject;
typedef struct _GtkSortListModel GtkSortListModel;
typedef struct _GtkSingleSelection GtkSingleSelection;
typedef struct _GtkSelectionModel GtkSelectionModel;
typedef struct _GtkSorter GtkSorter;
typedef struct _GtkCustomSorter GtkCustomSorter;
typedef int (*GCompareDataFunc)(gconstpointer a, gconstpointer b, gpointer data);
typedef enum { GTK_SORT_ASCENDING, GTK_SORT_DESCENDING } GtkSortType;
typedef struct _GMenu GMenu;
typedef struct _GMenuModel GMenuModel;
typedef struct _GMenuItem GMenuItem;
typedef struct _GAction GAction;
typedef struct _GActionMap GActionMap;
typedef struct _GActionGroup GActionGroup;
typedef struct _GSimpleAction GSimpleAction;
typedef struct _GSimpleActionGroup GSimpleActionGroup;
typedef struct _GVariant GVariant;
typedef struct _GVariantType GVariantType;
typedef struct _GtkShortcutController GtkShortcutController;
typedef struct _GtkShortcut GtkShortcut;
typedef struct _GtkShortcutTrigger GtkShortcutTrigger;
typedef struct _GtkShortcutAction GtkShortcutAction;
typedef struct _GtkEventController GtkEventController;
typedef struct _GtkCssProvider GtkCssProvider;
typedef struct _GtkStyleProvider GtkStyleProvider;
typedef struct _GtkGesture GtkGesture;
typedef struct _GtkGestureSingle GtkGestureSingle;
typedef struct _GdkRGBA {
	float red, green, blue, alpha;
} GdkRGBA;
typedef enum {
	GDK_NO_MODIFIER_MASK = 0, GDK_SHIFT_MASK = 1 << 0, GDK_CONTROL_MASK = 1 << 2, GDK_ALT_MASK = 1 << 3,
	GDK_SUPER_MASK = 1 << 26, GDK_META_MASK = 1 << 28
} GdkModifierType;
typedef struct _GdkFrameClock GdkFrameClock;
typedef enum { GTK_PHASE_NONE, GTK_PHASE_CAPTURE, GTK_PHASE_BUBBLE, GTK_PHASE_TARGET } GtkPropagationPhase;

/* cairo, which GTK draws with */
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
typedef enum { CAIRO_STATUS_SUCCESS = 0 } cairo_status_t;
typedef enum { CAIRO_FORMAT_ARGB32 = 0 } cairo_format_t;
typedef enum { CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_SLANT_OBLIQUE } cairo_font_slant_t;
typedef enum { CAIRO_FONT_WEIGHT_NORMAL, CAIRO_FONT_WEIGHT_BOLD } cairo_font_weight_t;
typedef enum { CAIRO_LINE_CAP_BUTT, CAIRO_LINE_CAP_ROUND, CAIRO_LINE_CAP_SQUARE } cairo_line_cap_t;
typedef enum { CAIRO_LINE_JOIN_MITER, CAIRO_LINE_JOIN_ROUND, CAIRO_LINE_JOIN_BEVEL } cairo_line_join_t;
typedef struct {
	double ascent, descent, height, max_x_advance, max_y_advance;
} cairo_font_extents_t;

typedef void (*GCallback)(void);
typedef void (*GClosureNotify)(gpointer data, void *closure);
typedef gboolean (*GSourceFunc)(gpointer data);
typedef void (*GDestroyNotify)(gpointer data);
typedef void (*GAsyncReadyCallback)(GObject *source, GAsyncResult *res, gpointer data);
typedef gboolean (*GtkShortcutFunc)(GtkWidget *widget, GVariant *args, gpointer data);
typedef void (*GtkDrawingAreaDrawFunc)(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data);

#define G_CALLBACK(f) ((GCallback)(f))
#define G_OBJECT(o) ((GObject *)(o))
#define G_LIST_MODEL(o) ((GListModel *)(o))
#define GTK_WIDGET(o) ((GtkWidget *)(o))
typedef struct _GtkAccessible GtkAccessible;
#define GTK_ACCESSIBLE(o) ((GtkAccessible *)(o))
#define GTK_ACCESSIBLE_PROPERTY_DESCRIPTION 1
void gtk_accessible_update_property(GtkAccessible *self, int first_property, ...);
#define GTK_STYLE_PROVIDER(o) ((GtkStyleProvider *)(o))
#define GTK_WINDOW(o) ((GtkWindow *)(o))
#define GTK_BOX(o) ((GtkBox *)(o))
#define GTK_LABEL(o) ((GtkLabel *)(o))
#define GTK_BUTTON(o) ((GtkButton *)(o))
#define GTK_EDITABLE(o) ((GtkEditable *)(o))
#define GTK_CHECK_BUTTON(o) ((GtkCheckButton *)(o))
#define GTK_DROP_DOWN(o) ((GtkDropDown *)(o))
#define GTK_STRING_LIST(o) ((GtkStringList *)(o))
#define GTK_TEXT_VIEW(o) ((GtkTextView *)(o))
#define GTK_TEXT_BUFFER(o) ((GtkTextBuffer *)(o))
#define GTK_SCROLLED_WINDOW(o) ((GtkScrolledWindow *)(o))
#define GTK_LIST_BOX(o) ((GtkListBox *)(o))
#define GTK_LIST_BOX_ROW(o) ((GtkListBoxRow *)(o))
#define GTK_SCALE(o) ((GtkScale *)(o))
#define GTK_RANGE(o) ((GtkRange *)(o))
#define GTK_PROGRESS_BAR(o) ((GtkProgressBar *)(o))
#define GTK_PICTURE(o) ((GtkPicture *)(o))
#define GTK_ALERT_DIALOG(o) ((GtkAlertDialog *)(o))
#define GTK_FILE_DIALOG(o) ((GtkFileDialog *)(o))
#define GTK_NATIVE(o) ((GtkNative *)(o))
#define GDK_PAINTABLE(o) ((GdkPaintable *)(o))
#define GTK_DRAWING_AREA(o) ((GtkDrawingArea *)(o))
#define GTK_EVENT_CONTROLLER(o) ((GtkEventController *)(o))
#define GTK_GESTURE_SINGLE(o) ((GtkGestureSingle *)(o))
#define G_MENU_MODEL(o) ((GMenuModel *)(o))
#define GTK_NOTEBOOK(o) ((GtkNotebook *)(o))
#define GTK_SPIN_BUTTON(o) ((GtkSpinButton *)(o))
#define GTK_COLOR_DIALOG_BUTTON(o) ((GtkColorDialogButton *)(o))
#define GTK_FONT_DIALOG_BUTTON(o) ((GtkFontDialogButton *)(o))
#define GTK_COLUMN_VIEW(o) ((GtkColumnView *)(o))
#define GTK_LIST_ITEM(o) ((GtkListItem *)(o))
#define GTK_STRING_OBJECT(o) ((GtkStringObject *)(o))
#define GTK_SELECTION_MODEL(o) ((GtkSelectionModel *)(o))
#define GTK_SORTER(o) ((GtkSorter *)(o))
#define G_ACTION(o) ((GAction *)(o))
#define G_ACTION_MAP(o) ((GActionMap *)(o))
#define G_ACTION_GROUP(o) ((GActionGroup *)(o))
#define GTK_SHORTCUT_CONTROLLER(o) ((GtkShortcutController *)(o))
#define TRUE 1
#define FALSE 0
#define G_PRIORITY_DEFAULT 0
#define G_SOURCE_REMOVE FALSE
#define G_SOURCE_CONTINUE TRUE
#define GTK_INVALID_LIST_POSITION ((guint)0xffffffff)

typedef enum { GTK_ORIENTATION_HORIZONTAL = 0, GTK_ORIENTATION_VERTICAL = 1 } GtkOrientation;
typedef enum { GTK_WRAP_NONE, GTK_WRAP_CHAR, GTK_WRAP_WORD, GTK_WRAP_WORD_CHAR } GtkWrapMode;
typedef enum { GTK_ALIGN_FILL, GTK_ALIGN_START, GTK_ALIGN_END, GTK_ALIGN_CENTER } GtkAlign;

/* GLib / GObject */
unsigned long g_signal_connect_data(gpointer instance, const char *signal, GCallback handler, gpointer data,
				    GClosureNotify destroy, int flags);
void g_signal_emit_by_name(gpointer instance, const char *signal, ...);
gpointer g_object_ref(gpointer o);
void g_object_unref(gpointer o);
void g_object_get(gpointer object, const char *first_property_name, ...);
void g_free(gpointer p);
gboolean g_main_context_iteration(GMainContext *c, gboolean may_block);
gboolean g_main_context_pending(GMainContext *c);
guint g_timeout_add_full(int priority, guint interval, GSourceFunc function, gpointer data, GDestroyNotify notify);
gboolean g_source_remove(guint tag);
gint64 g_get_monotonic_time(void);
char *g_file_get_path(GFile *file);
void g_error_free(GError *error);

/* GTK: start-up, windows, boxes */
gboolean gtk_init_check(void);
GtkWidget *gtk_window_new(void);
void gtk_window_set_title(GtkWindow *w, const char *title);
void gtk_window_set_default_size(GtkWindow *w, int width, int height);
void gtk_window_set_child(GtkWindow *w, GtkWidget *child);
void gtk_window_present(GtkWindow *w);
void gtk_window_close(GtkWindow *w);
void gtk_window_destroy(GtkWindow *w);
GtkWidget *gtk_box_new(GtkOrientation orientation, int spacing);
void gtk_box_append(GtkBox *box, GtkWidget *child);

/* widgets in general */
void gtk_widget_set_margin_top(GtkWidget *w, int m);
void gtk_widget_set_margin_bottom(GtkWidget *w, int m);
void gtk_widget_set_margin_start(GtkWidget *w, int m);
void gtk_widget_set_margin_end(GtkWidget *w, int m);
void gtk_widget_set_hexpand(GtkWidget *w, gboolean e);
void gtk_widget_set_vexpand(GtkWidget *w, gboolean e);
void gtk_widget_set_halign(GtkWidget *w, GtkAlign align);
void gtk_widget_set_valign(GtkWidget *w, GtkAlign align);
void gtk_widget_set_sensitive(GtkWidget *w, gboolean sensitive);
void gtk_widget_set_visible(GtkWidget *w, gboolean visible);
void gtk_widget_set_size_request(GtkWidget *w, int width, int height);
gboolean gtk_widget_get_mapped(GtkWidget *w);
GdkDisplay *gdk_display_get_default(void);
void gdk_display_flush(GdkDisplay *display);
int gtk_widget_get_width(GtkWidget *w);
int gtk_widget_get_height(GtkWidget *w);
GtkWidget *gtk_widget_get_root(GtkWidget *w);

/* labels, buttons, text boxes */
GtkWidget *gtk_label_new(const char *text);
void gtk_label_set_text(GtkLabel *l, const char *text);
const char *gtk_label_get_text(GtkLabel *l);
void gtk_label_set_xalign(GtkLabel *l, float x);
void gtk_label_set_wrap(GtkLabel *l, gboolean wrap);
GtkWidget *gtk_button_new_with_label(const char *label);
void gtk_button_set_label(GtkButton *b, const char *label);
const char *gtk_button_get_label(GtkButton *b);
GtkWidget *gtk_entry_new(void);
const char *gtk_editable_get_text(GtkEditable *e);
void gtk_editable_set_text(GtkEditable *e, const char *text);
void gtk_editable_insert_text(GtkEditable *e, const char *text, int length, int *position);

/* check boxes */
GtkWidget *gtk_check_button_new_with_label(const char *label);
gboolean gtk_check_button_get_active(GtkCheckButton *b);
void gtk_check_button_set_active(GtkCheckButton *b, gboolean active);
const char *gtk_check_button_get_label(GtkCheckButton *b);
void gtk_check_button_set_label(GtkCheckButton *b, const char *label);

/* drop-down lists */
GtkWidget *gtk_drop_down_new_from_strings(const char *const *strings);
GListModel *gtk_drop_down_get_model(GtkDropDown *d);
guint gtk_drop_down_get_selected(GtkDropDown *d);
void gtk_drop_down_set_selected(GtkDropDown *d, guint position);
void gtk_string_list_append(GtkStringList *list, const char *string);

/* multi-line text */
GtkWidget *gtk_text_view_new(void);
GtkTextBuffer *gtk_text_view_get_buffer(GtkTextView *v);
void gtk_text_view_set_editable(GtkTextView *v, gboolean editable);
void gtk_text_view_set_wrap_mode(GtkTextView *v, GtkWrapMode mode);
void gtk_text_buffer_set_text(GtkTextBuffer *b, const char *text, int len);
GtkWidget *gtk_scrolled_window_new(void);
void gtk_scrolled_window_set_child(GtkScrolledWindow *s, GtkWidget *child);
void gtk_scrolled_window_set_min_content_height(GtkScrolledWindow *s, int height);
void gtk_scrolled_window_set_has_frame(GtkScrolledWindow *s, gboolean has_frame);
void gtk_text_view_set_left_margin(GtkTextView *v, int margin);
void gtk_text_view_set_right_margin(GtkTextView *v, int margin);
void gtk_text_view_set_top_margin(GtkTextView *v, int margin);
void gtk_text_view_set_bottom_margin(GtkTextView *v, int margin);

/* lists */
GtkWidget *gtk_list_box_new(void);
void gtk_list_box_append(GtkListBox *b, GtkWidget *child);
void gtk_list_box_remove_all(GtkListBox *b);
GtkListBoxRow *gtk_list_box_get_selected_row(GtkListBox *b);
GtkListBoxRow *gtk_list_box_get_row_at_index(GtkListBox *b, int index);
void gtk_list_box_select_row(GtkListBox *b, GtkListBoxRow *row);
void gtk_list_box_unselect_all(GtkListBox *b);
int gtk_list_box_row_get_index(GtkListBoxRow *row);

/* sliders, progress bars, pictures, separators */
GtkWidget *gtk_scale_new_with_range(GtkOrientation o, double min, double max, double step);
void gtk_scale_set_digits(GtkScale *s, int digits);
void gtk_scale_set_draw_value(GtkScale *s, gboolean draw);
double gtk_range_get_value(GtkRange *r);
void gtk_range_set_value(GtkRange *r, double value);
GtkWidget *gtk_progress_bar_new(void);
void gtk_progress_bar_set_fraction(GtkProgressBar *p, double fraction);
GtkWidget *gtk_picture_new_for_filename(const char *filename);
void gtk_picture_set_filename(GtkPicture *p, const char *filename);
GtkWidget *gtk_separator_new(GtkOrientation o);

/* dialogs (GTK 4.10) */
GtkAlertDialog *gtk_alert_dialog_new(const char *format, ...);
void gtk_alert_dialog_set_buttons(GtkAlertDialog *d, const char *const *labels);
void gtk_alert_dialog_set_cancel_button(GtkAlertDialog *d, int button);
void gtk_alert_dialog_set_default_button(GtkAlertDialog *d, int button);
void gtk_alert_dialog_show(GtkAlertDialog *d, GtkWindow *parent);
void gtk_alert_dialog_choose(GtkAlertDialog *d, GtkWindow *parent, GCancellable *c,
			     GAsyncReadyCallback callback, gpointer data);
int gtk_alert_dialog_choose_finish(GtkAlertDialog *d, GAsyncResult *result, GError **error);
GtkFileDialog *gtk_file_dialog_new(void);
void gtk_file_dialog_set_title(GtkFileDialog *d, const char *title);
void gtk_file_dialog_set_initial_name(GtkFileDialog *d, const char *name);
void gtk_file_dialog_open(GtkFileDialog *d, GtkWindow *parent, GCancellable *c,
			  GAsyncReadyCallback callback, gpointer data);
GFile *gtk_file_dialog_open_finish(GtkFileDialog *d, GAsyncResult *result, GError **error);
void gtk_file_dialog_save(GtkFileDialog *d, GtkWindow *parent, GCancellable *c,
			  GAsyncReadyCallback callback, gpointer data);
GFile *gtk_file_dialog_save_finish(GtkFileDialog *d, GAsyncResult *result, GError **error);

/* screenshots for test scripts */
GdkPaintable *gtk_widget_paintable_new(GtkWidget *w);
GtkSnapshot *gtk_snapshot_new(void);
void gdk_paintable_snapshot(GdkPaintable *p, GdkSnapshot *s, double w, double h);
GskRenderNode *gtk_snapshot_free_to_node(GtkSnapshot *s);
GskRenderer *gtk_native_get_renderer(GtkNative *n);
GdkTexture *gsk_renderer_render_texture(GskRenderer *r, GskRenderNode *node, const void *viewport);
gboolean gdk_texture_save_to_png(GdkTexture *t, const char *filename);
void gsk_render_node_unref(GskRenderNode *node);

/* drawing areas (canvases) */
GtkWidget *gtk_drawing_area_new(void);
void gtk_drawing_area_set_content_width(GtkDrawingArea *a, int width);
void gtk_drawing_area_set_content_height(GtkDrawingArea *a, int height);
void gtk_drawing_area_set_draw_func(GtkDrawingArea *a, GtkDrawingAreaDrawFunc draw, gpointer data,
				    GDestroyNotify destroy);
void gtk_widget_queue_draw(GtkWidget *w);
typedef gboolean (*GtkTickCallback)(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data);
guint gtk_widget_add_tick_callback(GtkWidget *w, GtkTickCallback callback, gpointer user_data, GDestroyNotify notify);
void gtk_widget_remove_tick_callback(GtkWidget *w, guint id);
gint64 gdk_frame_clock_get_frame_time(GdkFrameClock *frame_clock);
void gtk_event_controller_set_propagation_phase(GtkEventController *controller, GtkPropagationPhase phase);
guint gdk_keyval_to_lower(guint keyval);
void gtk_window_set_resizable(GtkWindow *w, gboolean resizable);
GtkWidget *gtk_widget_get_first_child(GtkWidget *w);
void gtk_widget_set_focusable(GtkWidget *w, gboolean focusable);
gboolean gtk_widget_grab_focus(GtkWidget *w);
void gtk_widget_add_controller(GtkWidget *w, GtkEventController *controller);
GtkGesture *gtk_gesture_click_new(void);
void gtk_gesture_single_set_button(GtkGestureSingle *g, guint button);
GtkEventController *gtk_event_controller_motion_new(void);
GtkEventController *gtk_event_controller_key_new(void);
const char *gdk_keyval_name(guint keyval);
guint32 gdk_keyval_to_unicode(guint keyval);
gboolean gdk_rgba_parse(GdkRGBA *rgba, const char *spec);
GdkTexture *gdk_texture_new_from_filename(const char *path, GError **error);
int gdk_texture_get_width(GdkTexture *t);
int gdk_texture_get_height(GdkTexture *t);
void gdk_texture_download(GdkTexture *t, guchar *data, gsize stride);

/* tabs */
GtkWidget *gtk_notebook_new(void);
int gtk_notebook_append_page(GtkNotebook *n, GtkWidget *child, GtkWidget *tab_label);
int gtk_notebook_get_current_page(GtkNotebook *n);
void gtk_notebook_set_current_page(GtkNotebook *n, int page_num);
int gtk_notebook_get_n_pages(GtkNotebook *n);
void gtk_scrolled_window_set_min_content_width(GtkScrolledWindow *s, int width);

/* number boxes, color and font buttons */
GtkWidget *gtk_spin_button_new_with_range(double min, double max, double step);
double gtk_spin_button_get_value(GtkSpinButton *s);
void gtk_spin_button_set_value(GtkSpinButton *s, double value);
GtkColorDialog *gtk_color_dialog_new(void);
GtkWidget *gtk_color_dialog_button_new(GtkColorDialog *dialog);
const GdkRGBA *gtk_color_dialog_button_get_rgba(GtkColorDialogButton *b);
void gtk_color_dialog_button_set_rgba(GtkColorDialogButton *b, const GdkRGBA *color);
GtkFontDialog *gtk_font_dialog_new(void);
GtkWidget *gtk_font_dialog_button_new(GtkFontDialog *dialog);
PangoFontDescription *gtk_font_dialog_button_get_font_desc(GtkFontDialogButton *b);
void gtk_font_dialog_button_set_font_desc(GtkFontDialogButton *b, const PangoFontDescription *desc);
PangoFontDescription *pango_font_description_from_string(const char *str);
char *pango_font_description_to_string(const PangoFontDescription *desc);
void pango_font_description_free(PangoFontDescription *desc);
typedef enum { PANGO_STYLE_NORMAL, PANGO_STYLE_OBLIQUE, PANGO_STYLE_ITALIC } PangoStyle;
typedef enum { PANGO_WEIGHT_NORMAL = 400, PANGO_WEIGHT_SEMIBOLD = 600, PANGO_WEIGHT_BOLD = 700 } PangoWeight;
#define PANGO_SCALE 1024
const char *pango_font_description_get_family(const PangoFontDescription *desc);
PangoStyle pango_font_description_get_style(const PangoFontDescription *desc);
PangoWeight pango_font_description_get_weight(const PangoFontDescription *desc);
int pango_font_description_get_size(const PangoFontDescription *desc);

/* tables */
GtkWidget *gtk_column_view_new(GtkSelectionModel *model);
void gtk_column_view_set_model(GtkColumnView *v, GtkSelectionModel *model);
void gtk_column_view_append_column(GtkColumnView *v, GtkColumnViewColumn *column);
GtkSorter *gtk_column_view_get_sorter(GtkColumnView *v);
void gtk_column_view_sort_by_column(GtkColumnView *v, GtkColumnViewColumn *column, GtkSortType direction);
GtkColumnViewColumn *gtk_column_view_column_new(const char *title, GtkListItemFactory *factory);
void gtk_column_view_column_set_sorter(GtkColumnViewColumn *c, GtkSorter *sorter);
void gtk_column_view_column_set_expand(GtkColumnViewColumn *c, gboolean expand);
GtkListItemFactory *gtk_signal_list_item_factory_new(void);
void gtk_list_item_set_child(GtkListItem *i, GtkWidget *child);
GtkWidget *gtk_list_item_get_child(GtkListItem *i);
gpointer gtk_list_item_get_item(GtkListItem *i);
GtkStringList *gtk_string_list_new(const char *const *strings);
void gtk_string_list_splice(GtkStringList *list, guint position, guint n_removals, const char *const *additions);
const char *gtk_string_object_get_string(GtkStringObject *s);
GtkSortListModel *gtk_sort_list_model_new(GListModel *model, GtkSorter *sorter);
GtkSingleSelection *gtk_single_selection_new(GListModel *model);
guint gtk_single_selection_get_selected(GtkSingleSelection *s);
void gtk_single_selection_set_selected(GtkSingleSelection *s, guint position);
void gtk_single_selection_set_autoselect(GtkSingleSelection *s, gboolean autoselect);
void gtk_single_selection_set_can_unselect(GtkSingleSelection *s, gboolean can_unselect);
guint g_list_model_get_n_items(GListModel *list);
gpointer g_list_model_get_item(GListModel *list, guint position);
GtkCustomSorter *gtk_custom_sorter_new(GCompareDataFunc sort, gpointer data, GDestroyNotify destroy);

/* menus and shortcuts */
GMenu *g_menu_new(void);
void g_menu_append_submenu(GMenu *menu, const char *label, GMenuModel *submenu);
void g_menu_append_section(GMenu *menu, const char *label, GMenuModel *section);
void g_menu_append_item(GMenu *menu, GMenuItem *item);
GMenuItem *g_menu_item_new(const char *label, const char *detailed_action);
void g_menu_item_set_attribute(GMenuItem *item, const char *attribute, const char *format_string, ...);
GSimpleAction *g_simple_action_new(const char *name, const GVariantType *parameter_type);
void g_simple_action_set_enabled(GSimpleAction *action, gboolean enabled);
GSimpleActionGroup *g_simple_action_group_new(void);
void g_action_map_add_action(GActionMap *map, GAction *action);
void gtk_widget_insert_action_group(GtkWidget *w, const char *name, GActionGroup *group);
GtkWidget *gtk_popover_menu_bar_new_from_model(GMenuModel *model);
void gtk_box_prepend(GtkBox *box, GtkWidget *child);
GtkEventController *gtk_shortcut_controller_new(void);
void gtk_shortcut_controller_add_shortcut(GtkShortcutController *c, GtkShortcut *shortcut);
GtkShortcut *gtk_shortcut_new(GtkShortcutTrigger *trigger, GtkShortcutAction *action);
GtkShortcutTrigger *gtk_shortcut_trigger_parse_string(const char *string);
GtkShortcutAction *gtk_callback_action_new(GtkShortcutFunc callback, gpointer data, GDestroyNotify destroy);

/* cairo */
cairo_t *cairo_create(cairo_surface_t *target);
void cairo_destroy(cairo_t *cr);
void cairo_save(cairo_t *cr);
void cairo_restore(cairo_t *cr);
void cairo_set_source_rgba(cairo_t *cr, double red, double green, double blue, double alpha);
void cairo_set_source_surface(cairo_t *cr, cairo_surface_t *surface, double x, double y);
void cairo_set_line_width(cairo_t *cr, double width);
void cairo_set_line_cap(cairo_t *cr, cairo_line_cap_t cap);
void cairo_set_line_join(cairo_t *cr, cairo_line_join_t join);
void cairo_new_path(cairo_t *cr);
void cairo_close_path(cairo_t *cr);
void cairo_move_to(cairo_t *cr, double x, double y);
void cairo_line_to(cairo_t *cr, double x, double y);
void cairo_rectangle(cairo_t *cr, double x, double y, double width, double height);
void cairo_arc(cairo_t *cr, double xc, double yc, double radius, double angle1, double angle2);
void cairo_translate(cairo_t *cr, double tx, double ty);
void cairo_scale(cairo_t *cr, double sx, double sy);
void cairo_stroke(cairo_t *cr);
void cairo_fill(cairo_t *cr);
void cairo_paint(cairo_t *cr);
void cairo_rotate(cairo_t *cr, double angle);
int cairo_image_surface_get_width(cairo_surface_t *surface);
int cairo_image_surface_get_height(cairo_surface_t *surface);
void cairo_select_font_face(cairo_t *cr, const char *family, cairo_font_slant_t slant, cairo_font_weight_t weight);
void cairo_set_font_size(cairo_t *cr, double size);
void cairo_font_extents(cairo_t *cr, cairo_font_extents_t *extents);
void cairo_show_text(cairo_t *cr, const char *utf8);
cairo_surface_t *cairo_image_surface_create(cairo_format_t format, int width, int height);
unsigned char *cairo_image_surface_get_data(cairo_surface_t *surface);
int cairo_image_surface_get_stride(cairo_surface_t *surface);
void cairo_surface_flush(cairo_surface_t *surface);
void cairo_surface_mark_dirty(cairo_surface_t *surface);
cairo_surface_t *cairo_image_surface_create_for_data(unsigned char *data, cairo_format_t format, int width, int height,
						     int stride);
int cairo_format_stride_for_width(cairo_format_t format, int width);
void cairo_surface_destroy(cairo_surface_t *surface);
cairo_status_t cairo_surface_write_to_png(cairo_surface_t *surface, const char *filename);

/* CSS of the program's own (set_text_color, set_background) */
#define GTK_CHECK_VERSION(major, minor, micro) (4 > (major) || (4 == (major) && 10 > (minor)) || (4 == (major) && 10 == (minor) && 0 >= (micro)))
#define GTK_STYLE_PROVIDER_PRIORITY_APPLICATION 600
GtkCssProvider *gtk_css_provider_new(void);
void gtk_css_provider_load_from_data(GtkCssProvider *css_provider, const char *data, long length);
void gtk_css_provider_load_from_string(GtkCssProvider *css_provider, const char *string);
void gtk_style_context_add_provider_for_display(GdkDisplay *display, GtkStyleProvider *provider, guint priority);
void gtk_widget_add_css_class(GtkWidget *widget, const char *css_class);
char *gdk_rgba_to_string(const GdkRGBA *rgba);

#endif
