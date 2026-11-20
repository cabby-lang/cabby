/* game.c — the game library, on SDL 3: a window with a stage, sprites on
 * it, and frames.
 *
 * SDL makes the window, reads the keys and mouse, and draws with the
 * graphics card (SDL's renderer). Pictures, text and PNG files come from
 * sdl_common.h, shared with the SDL gui library. So the game library needs
 * SDL 3 and nothing else.
 *
 * Everything moves in frames, 60 a second. Each frame: the keys are looked
 * at (what went down, what's held, what came up since the last frame),
 * on_frame runs, then the timers that are due; then the stage is drawn: the
 * background, the sprites, and what the stage's own on_draw draws over
 * them.
 *
 * Under a test script (CABBY_GUI_SCRIPT), the window is drawn off screen,
 * and frames only run when the script says (`frames 30`), so a test sees
 * the same frames every time.
 *
 * Built with TERMINAL_GAME, this is the tgame library: the same game, shown
 * in a terminal (see tgame_terminal.h). */

#ifdef TERMINAL_GAME
#define SDL_LIB_NAME "tgame"
#else
#define SDL_LIB_NAME "game"
#endif
#include "sdl_common.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#ifdef TERMINAL_GAME
#include "tgame_terminal.h"
#endif

#define GAME_FPS 60
#define GAME_DEFAULT_W 640
#define GAME_DEFAULT_H 480
/* ------------------------------------------------------------- games and sprites */

typedef struct KeyState {
	char name[16];
	int down, went_down, came_up, pressed, released;
	long since, held;
} KeyState;

typedef struct GameTimer {
	long every, next;
	Handler h;
	int alive;
} GameTimer;

typedef struct Shortcut {
	char keys[48];        /* "ctrl+s" */
	Handler h;
} Shortcut;

typedef struct Game {
	char *title;
	int width, height;
	Color background;
	char *background_picture;
	Handler frame, draw, clicked, moved, keyed;
	long frames;
	int paused;
	KeyState *keys;
	int nkeys;
	GameTimer *timers;
	int ntimers;
	Shortcut *shortcuts;
	int nshortcuts;
	int world_x, world_y;        /* the world's top left corner (0, 0 unless set_world_area) */
	int world_w, world_h;
	double camera_x, camera_y;
	int64_t follow;
	int mouse_x, mouse_y;
	char key[16];         /* the last key, for the stage's on_key */
	int open;             /* running, and not closed */
	int closing;
	int *by_depth;        /* the layers drawn lower in front (sort_by_depth) */
	int nby_depth;
} Game;

static Game *games;
static int ngames;

enum { SP_PICTURE, SP_RECTANGLE, SP_CIRCLE };

typedef struct Sprite {
	int game;             /* from 1 */
	int alive, kind;
	double x, y, angle, scale;
	int visible;
	double w, h;          /* shapes: their size (a circle: w is the diameter) */
	Color color;
	int *pictures;        /* (indexes) one, or several to show in turn */
	int npictures;
	long every, started, order;
	char *group;
	int layer;            /* drawn in layers, the lowest first (0 to begin with) */
} Sprite;

static Sprite *sprites;
static int nsprites;
static long front_order, back_order;

static Game *game(int64_t win)
{
	if (win < 1 || win > ngames)
		fail("%s", "this game window hasn't been made yet");
	return &games[win - 1];
}

static Sprite *sprite(int64_t id)
{
	if (id < 1 || id > nsprites)
		fail("%s", "this sprite hasn't been added to a game yet");
	return &sprites[id - 1];
}

int64_t cby_game_window_new(void)
{
	games = realloc(games, sizeof *games * (size_t)(ngames + 1));
	games[ngames] = (Game){ .title = strdup("Cabby"), .width = GAME_DEFAULT_W, .height = GAME_DEFAULT_H,
				.background = { 1, 1, 1, 1 } };
	return ++ngames;
}

void cby_game_set_title(int64_t win, const char *title)
{
	Game *g = game(win);
	free(g->title);
	g->title = strdup(title);
}

void cby_game_set_size(int64_t win, int width, int height)
{
	Game *g = game(win);
	g->width = width < 1 ? 1 : width;
	g->height = height < 1 ? 1 : height;
}

int cby_game_width(int64_t win) { return win < 1 ? GAME_DEFAULT_W : game(win)->width; }
int cby_game_height(int64_t win) { return win < 1 ? GAME_DEFAULT_H : game(win)->height; }

void cby_game_set_background(int64_t win, const char *spec)
{
	Game *g = game(win);
	free(g->background_picture);
	g->background_picture = NULL;
	if (strchr(spec, '.') && !strchr(spec, '(')) {   /* a file: "sky.png" */
		picture(spec);
		g->background_picture = strdup(spec);
	} else {
		g->background = color(spec);
	}
}

void cby_game_on_frame(int64_t win, void *obj, void *fn) { game(win)->frame = (Handler){ obj, fn }; }
int64_t cby_game_frame(int64_t win) { return win < 1 ? 0 : game(win)->frames; }
void cby_game_set_paused(int64_t win, int on) { game(win)->paused = on != 0; }
int cby_game_paused(int64_t win) { return win < 1 ? 0 : game(win)->paused; }

void cby_game_every(int64_t win, int ms, int repeat, void *obj, void *fn)
{
	Game *g = game(win);
	long frames = (long)((double)ms * GAME_FPS / 1000 + 0.5);
	if (frames < 1)
		frames = 1;
	g->timers = realloc(g->timers, sizeof *g->timers * (size_t)(g->ntimers + 1));
	g->timers[g->ntimers++] = (GameTimer){ repeat ? frames : 0, g->frames + frames, { obj, fn }, 1 };
}

void cby_game_stop_timers(int64_t win)
{
	Game *g = game(win);
	for (int i = 0; i < g->ntimers; i++)
		g->timers[i].alive = 0;
}

/* ---- keys */



static int key_slot(Game *g, const char *name)
{
	for (int i = 0; i < g->nkeys; i++)
		if (strcmp(g->keys[i].name, name) == 0)
			return i;
	return -1;
}

static void key_goes(int64_t win, const char *name, int down)
{
	char n[16];
	key_name_of(name, n, sizeof n);
	if (!n[0])
		return;
	Game *g = game(win);
	int i = key_slot(g, n);
	if (i < 0) {
		g->keys = realloc(g->keys, sizeof *g->keys * (size_t)(g->nkeys + 1));
		i = g->nkeys++;
		g->keys[i] = (KeyState){ 0 };
		snprintf(g->keys[i].name, sizeof g->keys[i].name, "%s", n);
	}
	KeyState *k = &g->keys[i];
	if (down && !k->down) {
		k->down = 1;
		k->went_down = 1;
	} else if (!down && k->down) {
		k->down = 0;
		k->came_up = 1;
	}
}

static KeyState *key_now(int64_t win, const char *name)
{
	if (win < 1)
		return NULL;
	char n[16];
	key_name_of(name, n, sizeof n);
	Game *g = game(win);
	int i = key_slot(g, n);
	return i < 0 ? NULL : &g->keys[i];
}

int cby_game_key_press(int64_t win, const char *name) { KeyState *k = key_now(win, name); return k && k->pressed; }
int cby_game_key_hold(int64_t win, const char *name) { KeyState *k = key_now(win, name); return k && (k->down || k->pressed); }
int cby_game_key_release(int64_t win, const char *name) { KeyState *k = key_now(win, name); return k && k->released; }

int cby_game_key_held_for(int64_t win, const char *name)
{
	KeyState *k = key_now(win, name);
	if (!k)
		return 0;
	long frames = k->down ? game(win)->frames - k->since : k->released ? k->held : 0;
	return (int)(frames * 1000 / GAME_FPS);
}

/* ---- shortcuts: "Ctrl+S", "F5", "P" */


void cby_game_add_shortcut(int64_t win, const char *keys, void *obj, void *fn)
{
	Game *g = game(win);
	char norm[48];
	if (!parse_keys(keys, norm, sizeof norm))
		fail("'%s' isn't a shortcut: write it like \"Ctrl+S\", \"Ctrl+Shift+Z\", \"Alt+X\", \"F5\" or \"P\"", keys);
	g->shortcuts = realloc(g->shortcuts, sizeof *g->shortcuts * (size_t)(g->nshortcuts + 1));
	Shortcut *s = &g->shortcuts[g->nshortcuts++];
	snprintf(s->keys, sizeof s->keys, "%s", norm);
	s->h = (Handler){ obj, fn };
}

/* runs the shortcut for these keys, if there is one */
static int shortcut(int64_t win, const char *norm)
{
	Game *g = game(win);
	for (int i = 0; i < g->nshortcuts; i++)
		if (strcmp(g->shortcuts[i].keys, norm) == 0) {
			invoke(g->shortcuts[i].h);
			return 1;
		}
	return 0;
}

/* ---- the stage's own handlers (as a canvas's) */

void cby_game_stage_on_draw(int64_t win, void *obj, void *fn) { game(win)->draw = (Handler){ obj, fn }; }
void cby_game_stage_on_click(int64_t win, void *obj, void *fn) { game(win)->clicked = (Handler){ obj, fn }; }
void cby_game_stage_on_mouse_move(int64_t win, void *obj, void *fn) { game(win)->moved = (Handler){ obj, fn }; }
void cby_game_stage_on_key(int64_t win, void *obj, void *fn) { game(win)->keyed = (Handler){ obj, fn }; }
int cby_game_stage_mouse_x(int64_t win) { return game(win)->mouse_x; }
int cby_game_stage_mouse_y(int64_t win) { return game(win)->mouse_y; }
void cby_game_stage_redraw(int64_t win) { (void)game(win); /* (it's drawn every frame) */ }

static char *handed;   /* the last text handed out, read a character at a time */

int64_t cby_game_stage_key_length(int64_t win)
{
	free(handed);
	handed = strdup(game(win)->key);
	return (int64_t)strlen(handed);
}

char cby_game_text_char(int64_t i) { return handed && i >= 0 && i < (int64_t)strlen(handed) ? handed[i] : 0; }

/* ---- random numbers: its own generator, the same everywhere under a script */

static int scripted(void);
static uint64_t seed;

int cby_game_random(int from, int to)
{
	if (!seed) {
		seed = scripted() ? 0x9E3779B97F4A7C15ull : (uint64_t)SDL_GetTicksNS() * 2654435761ull + 1;
		if (!seed)
			seed = 1;
	}
	if (to < from) {
		int t = from;
		from = to;
		to = t;
	}
	seed ^= seed << 13;
	seed ^= seed >> 7;
	seed ^= seed << 17;
	uint64_t span = (uint64_t)((int64_t)to - from) + 1;
	return (int)(from + (int64_t)(seed % span));
}

/* ---- sprites */

static int64_t new_sprite(int64_t win, int kind, double x, double y)
{
	game(win);
	sprites = realloc(sprites, sizeof *sprites * (size_t)(nsprites + 1));
	sprites[nsprites] = (Sprite){ .game = (int)win, .alive = 1, .kind = kind, .x = x, .y = y, .scale = 1,
				      .visible = 1, .order = ++front_order, .color = { 0, 0, 0, 1 } };
	return ++nsprites;
}

static void set_one_picture(Sprite *s, int pic)
{
	s->pictures = realloc(s->pictures, sizeof *s->pictures);
	s->pictures[0] = pic;
	s->npictures = 1;
}

int64_t cby_game_sprite_picture(int64_t win, const char *path, double x, double y)
{
	int pic = picture(path);   /* a panic now, at the line that added it, if it can't be shown */
	int64_t id = new_sprite(win, SP_PICTURE, x, y);
	set_one_picture(&sprites[id - 1], pic);
	return id;
}

static int64_t shape(int64_t win, int kind, double w, double h, const char *c, double x, double y)
{
	Color col = color(c);
	int64_t id = new_sprite(win, kind, x, y);
	sprites[id - 1].w = w;
	sprites[id - 1].h = h;
	sprites[id - 1].color = col;
	return id;
}

int64_t cby_game_sprite_rectangle(int64_t win, double w, double h, const char *c, double x, double y)
{
	return shape(win, SP_RECTANGLE, w, h, c, x, y);
}

int64_t cby_game_sprite_circle(int64_t win, double radius, const char *c, double x, double y)
{
	return shape(win, SP_CIRCLE, radius * 2, radius * 2, c, x, y);
}

int64_t cby_game_sprite_window(int64_t id) { return sprite(id)->game; }
double cby_game_sprite_x(int64_t id) { return sprite(id)->x; }
double cby_game_sprite_y(int64_t id) { return sprite(id)->y; }
double cby_game_sprite_angle(int64_t id) { return sprite(id)->angle; }
double cby_game_sprite_scale(int64_t id) { return sprite(id)->scale; }
int cby_game_sprite_visible(int64_t id) { return sprite(id)->visible; }
int cby_game_sprite_removed(int64_t id) { return !sprite(id)->alive; }

static int showing(Sprite *s)   /* which of its pictures shows now */
{
	if (s->npictures > 1 && s->every > 0)
		return s->pictures[((games[s->game - 1].frames - s->started) / s->every) % s->npictures];
	return s->npictures ? s->pictures[0] : -1;
}

/* the box round what shows of it, about its middle, before turning */
static void sprite_extent(Sprite *s, double *x0, double *y0, double *x1, double *y1)
{
	if (s->kind == SP_PICTURE) {
		Picture *p = &pictures[showing(s)];
		*x0 = p->x0 - p->w / 2.0;
		*y0 = p->y0 - p->h / 2.0;
		*x1 = p->x1 - p->w / 2.0;
		*y1 = p->y1 - p->h / 2.0;
	} else {
		*x0 = -s->w / 2;
		*y0 = -s->h / 2;
		*x1 = s->w / 2;
		*y1 = s->h / 2;
	}
	*x0 *= s->scale; *y0 *= s->scale; *x1 *= s->scale; *y1 *= s->scale;
}

double cby_game_sprite_width(int64_t id)
{
	Sprite *s = sprite(id);
	return s->kind == SP_PICTURE ? pictures[s->pictures[0]].w * s->scale : s->w * s->scale;
}

double cby_game_sprite_height(int64_t id)
{
	Sprite *s = sprite(id);
	return s->kind == SP_PICTURE ? pictures[s->pictures[0]].h * s->scale : s->h * s->scale;
}

void cby_game_sprite_move_to(int64_t id, double x, double y) { Sprite *s = sprite(id); s->x = x; s->y = y; }
void cby_game_sprite_move(int64_t id, double dx, double dy) { Sprite *s = sprite(id); s->x += dx; s->y += dy; }

/* the way it's facing: 0 is up the stage (as drawn), 90 to the right */
void cby_game_sprite_move_forward(int64_t id, double distance)
{
	Sprite *s = sprite(id);
	double a = s->angle * M_PI / 180;
	s->x += distance * sin(a);
	s->y -= distance * cos(a);
}

void cby_game_sprite_set_angle(int64_t id, double degrees)
{
	Sprite *s = sprite(id);
	s->angle = fmod(degrees, 360);
	if (s->angle < 0)
		s->angle += 360;
}

void cby_game_sprite_turn(int64_t id, double degrees) { cby_game_sprite_set_angle(id, sprite(id)->angle + degrees); }
void cby_game_sprite_set_scale(int64_t id, double scale) { sprite(id)->scale = scale > 0 ? scale : 0; }
void cby_game_sprite_set_visible(int64_t id, int on) { sprite(id)->visible = on != 0; }
void cby_game_sprite_remove(int64_t id) { sprite(id)->alive = 0; }

void cby_game_sprite_set_picture(int64_t id, const char *path)
{
	Sprite *s = sprite(id);
	if (s->kind != SP_PICTURE)
		fail("%s", "only a sprite made from a picture can change its picture (a shape changes its color)");
	set_one_picture(s, picture(path));
}

void cby_game_sprite_set_color(int64_t id, const char *c)
{
	Sprite *s = sprite(id);
	if (s->kind == SP_PICTURE)
		fail("%s", "a sprite made from a picture can't change its color (it can change its picture)");
	s->color = color(c);
}

void cby_game_sprite_animate_start(int64_t id)
{
	Sprite *s = sprite(id);
	if (s->kind != SP_PICTURE)
		fail("%s", "only a sprite made from a picture can be animated");
	s->npictures = 0;
}

void cby_game_sprite_animate_picture(int64_t id, const char *path)
{
	Sprite *s = sprite(id);
	int pic = picture(path);
	s->pictures = realloc(s->pictures, sizeof *s->pictures * (size_t)(s->npictures + 1));
	s->pictures[s->npictures++] = pic;
}

void cby_game_sprite_animate_end(int64_t id, int ms)
{
	Sprite *s = sprite(id);
	if (s->npictures == 0)
		fail("%s", "animate needs at least one picture");
	s->every = (long)((double)ms * GAME_FPS / 1000 + 0.5);
	if (s->every < 1)
		s->every = 1;
	s->started = games[s->game - 1].frames;
}

void cby_game_sprite_to_front(int64_t id) { sprite(id)->order = ++front_order; }

/* ---- layers: the ground under everything, then what stands on it, then
 * what's above it all; within a layer that's sorted by depth, what's lower
 * on the stage (its bottom edge) is drawn in front, as a hero south of a
 * tree stands in front of it and one north of it behind its leaves */
void cby_game_sprite_set_layer(int64_t id, int layer) { sprite(id)->layer = layer; }
int cby_game_sprite_layer(int64_t id) { return sprite(id)->layer; }

void cby_game_sort_by_depth(int64_t win, int layer)
{
	Game *g = game(win);
	for (int i = 0; i < g->nby_depth; i++)
		if (g->by_depth[i] == layer)
			return;
	g->by_depth = realloc(g->by_depth, sizeof *g->by_depth * (size_t)(g->nby_depth + 1));
	g->by_depth[g->nby_depth++] = layer;
}
void cby_game_sprite_to_back(int64_t id) { sprite(id)->order = --back_order; }

void cby_game_sprite_set_group(int64_t id, const char *group)
{
	Sprite *s = sprite(id);
	free(s->group);
	s->group = *group ? strdup(group) : NULL;
}

int64_t cby_game_sprite_group_length(int64_t id)
{
	Sprite *s = sprite(id);
	free(handed);
	handed = strdup(s->group ? s->group : "");
	return (int64_t)strlen(handed);
}

/* ---- touching: a circle is a circle; a rectangle its box; a picture the
 * box round its pixels that show; a turned box, the box round it as turned */

static void sprite_box(Sprite *s, double *l, double *t, double *r, double *b)
{
	double x0, y0, x1, y1;
	sprite_extent(s, &x0, &y0, &x1, &y1);
	if (s->angle != 0 && s->kind != SP_CIRCLE) {
		double a = s->angle * M_PI / 180, c = cos(a), sn = sin(a);
		double xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
		double nx0 = 1e300, ny0 = 1e300, nx1 = -1e300, ny1 = -1e300;
		for (int i = 0; i < 4; i++) {
			double rx = xs[i] * c - ys[i] * sn, ry = xs[i] * sn + ys[i] * c;
			if (rx < nx0) nx0 = rx;
			if (rx > nx1) nx1 = rx;
			if (ry < ny0) ny0 = ry;
			if (ry > ny1) ny1 = ry;
		}
		x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
	}
	*l = s->x + x0;
	*t = s->y + y0;
	*r = s->x + x1;
	*b = s->y + y1;
}

static int touching(Sprite *a, Sprite *b)
{
	if (a == b || !a->alive || !b->alive || !a->visible || !b->visible || a->game != b->game)
		return 0;
	double al, at, ar, ab, bl, bt, br, bb;
	sprite_box(a, &al, &at, &ar, &ab);
	sprite_box(b, &bl, &bt, &br, &bb);
	if (ar <= al || ab <= at || br <= bl || bb <= bt)
		return 0;
	if (a->kind == SP_CIRCLE && b->kind == SP_CIRCLE) {
		double dx = a->x - b->x, dy = a->y - b->y, r = (ar - al) / 2 + (br - bl) / 2;
		return dx * dx + dy * dy < r * r;
	}
	if (a->kind == SP_CIRCLE || b->kind == SP_CIRCLE) {
		Sprite *c = a->kind == SP_CIRCLE ? a : b;
		double l = c == a ? bl : al, t = c == a ? bt : at, r = c == a ? br : ar, bo = c == a ? bb : ab;
		double radius = c == a ? (ar - al) / 2 : (br - bl) / 2;
		double nx = c->x < l ? l : c->x > r ? r : c->x;
		double ny = c->y < t ? t : c->y > bo ? bo : c->y;
		double dx = c->x - nx, dy = c->y - ny;
		return dx * dx + dy * dy < radius * radius;
	}
	return al < br && bl < ar && at < bb && bt < ab;
}

int cby_game_sprite_touches(int64_t a, int64_t b) { return touching(sprite(a), sprite(b)); }
double cby_game_sprite_left(int64_t id) { double l, t, r, b; sprite_box(sprite(id), &l, &t, &r, &b); return l; }
double cby_game_sprite_top(int64_t id) { double l, t, r, b; sprite_box(sprite(id), &l, &t, &r, &b); return t; }
double cby_game_sprite_right(int64_t id) { double l, t, r, b; sprite_box(sprite(id), &l, &t, &r, &b); return r; }
double cby_game_sprite_bottom(int64_t id) { double l, t, r, b; sprite_box(sprite(id), &l, &t, &r, &b); return b; }

/* ---- the camera */

static void camera(Game *g, double *x, double *y)
{
	double ww = g->world_w ? g->world_w : g->width, wh = g->world_h ? g->world_h : g->height;
	double cx = g->camera_x, cy = g->camera_y;
	if (g->follow && g->follow <= nsprites && sprites[g->follow - 1].alive) {
		cx = sprites[g->follow - 1].x - g->width / 2.0;
		cy = sprites[g->follow - 1].y - g->height / 2.0;
	}
	if (cx > g->world_x + ww - g->width) cx = g->world_x + ww - g->width;
	if (cy > g->world_y + wh - g->height) cy = g->world_y + wh - g->height;
	if (cx < g->world_x) cx = g->world_x;
	if (cy < g->world_y) cy = g->world_y;
	*x = floor(cx + 0.5);   /* whole pixels, so pictures stay sharp */
	*y = floor(cy + 0.5);
}

int cby_game_sprite_on_stage(int64_t id)
{
	Sprite *s = sprite(id);
	if (!s->alive)
		return 0;
	Game *g = &games[s->game - 1];
	double l, t, r, b, cx, cy;
	sprite_box(s, &l, &t, &r, &b);
	camera(g, &cx, &cy);
	return r > cx && l < cx + g->width && b > cy && t < cy + g->height;
}

void cby_game_set_world_area(int64_t win, int left, int top, int width, int height)
{
	Game *g = game(win);
	g->world_x = left;
	g->world_y = top;
	g->world_w = width > 0 ? width : 0;
	g->world_h = height > 0 ? height : 0;
}

void cby_game_set_world_size(int64_t win, int width, int height)
{
	cby_game_set_world_area(win, 0, 0, width, height);
}

int cby_game_world_left(int64_t win) { return game(win)->world_x; }
int cby_game_world_top(int64_t win) { return game(win)->world_y; }

int cby_game_world_width(int64_t win) { Game *g = game(win); return g->world_w ? g->world_w : g->width; }
int cby_game_world_height(int64_t win) { Game *g = game(win); return g->world_h ? g->world_h : g->height; }

void cby_game_move_camera_to(int64_t win, double x, double y)
{
	Game *g = game(win);
	g->follow = 0;
	g->camera_x = x;
	g->camera_y = y;
}

void cby_game_follow(int64_t win, int64_t id)
{
	if (id)
		sprite(id);
	game(win)->follow = id;
}

double cby_game_camera_x(int64_t win) { double x, y; camera(game(win), &x, &y); return x; }
double cby_game_camera_y(int64_t win) { double x, y; camera(game(win), &x, &y); return y; }

/* ---- finding sprites: those in a group (all, or those touching a sprite) */

static int64_t *found;
static int nfound;

int cby_game_find(int64_t win, int64_t touching_id, const char *group)
{
	game(win);
	Sprite *t = touching_id ? sprite(touching_id) : NULL;
	nfound = 0;
	found = realloc(found, sizeof *found * (size_t)(nsprites ? nsprites : 1));
	for (int i = 0; i < nsprites; i++) {
		Sprite *s = &sprites[i];
		if (s->game != win || !s->alive)
			continue;
		if (*group && (!s->group || strcmp(s->group, group) != 0))
			continue;
		if (t && !touching(t, s))
			continue;
		found[nfound++] = i + 1;
	}
	return nfound;
}

int64_t cby_game_found(int i) { return i >= 0 && i < nfound ? found[i] : 0; }

/* ------------------------------------------------------------- drawing
 *
 * With SDL's renderer: rectangles as they are, and everything else made of
 * triangles. What the stage's on_draw draws is drawn the same way, with a
 * pen: its color, line width and font. */

static int64_t drawing;        /* the game whose stage is being drawn (0: none) */
static SDL_FColor pen;
static float pen_width = 1, font_size = 14;
static int pen_font;           /* (index into fonts) */


/* which is drawn first: the lower layer; then, in a layer sorted by depth,
 * the one whose bottom is higher up; then the one added (or sent to the
 * back) first */
typedef struct DrawKey {
	int i, layer;
	double depth;
} DrawKey;

static int draws_before(const DrawKey *a, const DrawKey *b)
{
	if (a->layer != b->layer)
		return a->layer < b->layer;
	if (a->depth != b->depth)
		return a->depth < b->depth;
	return sprites[a->i].order < sprites[b->i].order;
}

static void draw_sprites(int64_t win, double cx, double cy)
{
	Game *g = game(win);
	int n = 0;
	DrawKey *keys = malloc(sizeof *keys * (size_t)(nsprites ? nsprites : 1));
	for (int i = 0; i < nsprites; i++) {
		Sprite *s = &sprites[i];
		if (s->game != win || !s->alive || !s->visible)
			continue;
		DrawKey k = { i, s->layer, 0 };
		for (int j = 0; j < g->nby_depth; j++)
			if (g->by_depth[j] == s->layer) {
				double l, t, r, b;
				sprite_box(s, &l, &t, &r, &b);
				k.depth = b;
			}
		keys[n++] = k;
	}
	for (int i = 1; i < n; i++) {   /* (insertion sort: nearly always in order already) */
		DrawKey v = keys[i];
		int j = i - 1;
		while (j >= 0 && draws_before(&v, &keys[j])) {
			keys[j + 1] = keys[j];
			j--;
		}
		keys[j + 1] = v;
	}
	int *order = malloc(sizeof *order * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++)
		order[i] = keys[i].i;
	free(keys);
	for (int k = 0; k < n; k++) {
		Sprite *s = &sprites[order[k]];
		double x = s->x - cx, y = s->y - cy;
		if (s->kind == SP_PICTURE) {
			int pic = showing(s);
			SDL_Texture *t = picture_texture(pic);
			if (!t)
				continue;
			double w = pictures[pic].w * s->scale, h = pictures[pic].h * s->scale;
			SDL_FRect r = { (float)(x - w / 2), (float)(y - h / 2), (float)w, (float)h };
			SDL_RenderTextureRotated(renderer, t, NULL, &r, s->angle, NULL, SDL_FLIP_NONE);
		} else if (s->kind == SP_CIRCLE) {
			fill_ellipse(x, y, s->w / 2 * s->scale, s->h / 2 * s->scale, fcolor(s->color));
		} else {
			double hw = s->w / 2 * s->scale, hh = s->h / 2 * s->scale, a = s->angle * M_PI / 180;
			double c = cos(a), sn = sin(a);
			double xs[4] = { -hw, hw, hw, -hw }, ys[4] = { -hh, -hh, hh, hh };
			float xy[8];
			for (int i = 0; i < 4; i++) {
				xy[2 * i] = (float)(x + xs[i] * c - ys[i] * sn);
				xy[2 * i + 1] = (float)(y + xs[i] * sn + ys[i] * c);
			}
			int ix[6] = { 0, 1, 2, 0, 2, 3 };
			triangles(xy, 4, ix, 6, fcolor(s->color));
		}
	}
	free(order);
}

/* The whole stage: the background, the sprites, then the stage's on_draw */
static void draw_stage(int64_t win)
{
	Game *g = game(win);
	double cx, cy;
	camera(g, &cx, &cy);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColorFloat(renderer, g->background.r, g->background.g, g->background.b, 1);
	SDL_RenderClear(renderer);
	if (g->background_picture) {
		SDL_Texture *t = picture_texture(picture(g->background_picture));
		double ww = g->world_w ? g->world_w : g->width, wh = g->world_h ? g->world_h : g->height;
		SDL_FRect r = { (float)(g->world_x - cx), (float)(g->world_y - cy), (float)ww, (float)wh };
		if (t)
			SDL_RenderTexture(renderer, t, NULL, &r);
	}
	draw_sprites(win, cx, cy);
	pen = (SDL_FColor){ 0, 0, 0, 1 };
	pen_width = 1;
	font_size = 14;
	pen_font = font_for("Sans", 0, 0);
	drawing = win;
	invoke(game(win)->draw);
	drawing = 0;
}

/* ---- the stage's pen: only while its on_draw runs */

static void need_pen(int64_t win)
{
	game(win);
	if (drawing != win || !renderer)
		fail("%s", "the stage can only be drawn on in its on_draw handler (it's drawn again every frame)");
}

void cby_game_stage_set_color(int64_t win, const char *c)
{
	need_pen(win);
	pen = fcolor(color(c));
}

void cby_game_stage_fill_with(int64_t win, const char *c)
{
	need_pen(win);
	Color col = color(c);
	SDL_SetRenderDrawColorFloat(renderer, col.r, col.g, col.b, col.a);
	SDL_FRect r = { 0, 0, (float)game(win)->width, (float)game(win)->height };
	SDL_RenderFillRect(renderer, &r);
}

void cby_game_stage_set_line_width(int64_t win, double w)
{
	need_pen(win);
	pen_width = (float)(w > 0 ? w : 1);
}

void cby_game_stage_set_font_size(int64_t win, double size)
{
	need_pen(win);
	font_size = (float)(size > 0 ? size : 14);
}

/* "Serif Bold 16": the family, bold or italic, and the size in pixels */
void cby_game_stage_set_font(int64_t win, const char *spec)
{
	need_pen(win);
	double size;
	pen_font = font_named(spec, &size);
	if (size > 0)
		font_size = (float)size;
}

void cby_game_stage_line(int64_t win, double x1, double y1, double x2, double y2)
{
	need_pen(win);
	thick_line(x1, y1, x2, y2, pen_width, pen);
}

void cby_game_stage_rectangle(int64_t win, double x, double y, double w, double h, int filled)
{
	need_pen(win);
	if (filled) {
		SDL_SetRenderDrawColorFloat(renderer, pen.r, pen.g, pen.b, pen.a);
		SDL_FRect r = { (float)x, (float)y, (float)w, (float)h };
		SDL_RenderFillRect(renderer, &r);
		return;
	}
	double pw = pen_width / 2;   /* the outline centered on the edges, square at the corners */
	SDL_SetRenderDrawColorFloat(renderer, pen.r, pen.g, pen.b, pen.a);
	SDL_FRect sides[4] = { { (float)(x - pw), (float)(y - pw), (float)(w + 2 * pw), pen_width },
			       { (float)(x - pw), (float)(y + h - pw), (float)(w + 2 * pw), pen_width },
			       { (float)(x - pw), (float)(y + pw), pen_width, (float)(h - 2 * pw) },
			       { (float)(x + w - pw), (float)(y + pw), pen_width, (float)(h - 2 * pw) } };
	SDL_RenderFillRects(renderer, sides, 4);
}

void cby_game_stage_ellipse(int64_t win, double x, double y, double rx, double ry, int filled)
{
	need_pen(win);
	if (rx <= 0 || ry <= 0)
		return;
	if (filled)
		fill_ellipse(x, y, rx, ry, pen);
	else
		ring(x, y, rx, ry, pen_width, 0, 2 * M_PI, pen);
}

void cby_game_stage_arc(int64_t win, double x, double y, double r, double from, double to)
{
	need_pen(win);
	if (r <= 0)
		return;
	while (to < from)
		to += 360;
	ring(x, y, r, r, pen_width, (from - 90) * M_PI / 180, (to - 90) * M_PI / 180, pen);
}

static double *corners;
static int ncorners, capcorners;

void cby_game_stage_corner(int64_t win, double x, double y)
{
	(void)win;
	if (ncorners == capcorners) {
		capcorners = capcorners ? capcorners * 2 : 16;
		corners = realloc(corners, sizeof *corners * (size_t)capcorners * 2);
	}
	corners[2 * ncorners] = x;
	corners[2 * ncorners + 1] = y;
	ncorners++;
}

void cby_game_stage_polygon(int64_t win, int filled, int64_t numbers)
{
	int n = ncorners;
	ncorners = 0;
	need_pen(win);
	if (numbers % 2 != 0 || numbers < 6)
		fail("%s", numbers % 2 != 0 ? "a polygon's corners come in pairs: {x, y, x, y, ...}"
					    : "a polygon needs at least 3 corners: {x, y, x, y, x, y}");
	if (filled) {
		fill_polygon(corners, n, pen);
	} else {
		for (int i = 0; i < n; i++) {
			int j = (i + 1) % n;
			thick_line(corners[2 * i], corners[2 * i + 1], corners[2 * j], corners[2 * j + 1], pen_width, pen);
		}
	}
}


/* text, its top left corner at X, Y */
void cby_game_stage_text(int64_t win, double x, double y, const char *text)
{
	need_pen(win);
#ifdef TERMINAL_GAME
	if (text_as_cells) {   /* (in the terminal: written as characters, over the picture) */
		keep_text(x, y, font_size, pen, text);
		return;
	}
#endif
	draw_text(pen_font, font_size, x, y, text, pen);
}

/* ---- measuring and wrapping text, in the pen's font and size (Sans 14
 * outside on_draw, as it starts each time) */

static void pen_font_ready(void)
{
	if (!nfonts)
		pen_font = font_for("Sans", 0, 0);   /* (before the stage is first drawn) */
}

double cby_game_stage_text_width(int64_t win, const char *text)
{
	game(win);
	pen_font_ready();
	return text_width(pen_font, font_size, text);
}

double cby_game_stage_line_height(int64_t win)
{
	game(win);
	pen_font_ready();
	return line_height(pen_font, font_size);
}

/* The text's lines, each no wider than width, broken at spaces (and at a
 * new line in it): calls line for each, with its start and length. A word
 * wider than width on its own has a line to itself. */
static int wrap_text(const char *text, double width, void (*line)(const char *s, int n, int i, void *data), void *data)
{
	int count = 0;
	const char *p = text;
	while (*p) {
		const char *start = p, *last_space = NULL, *q = p;
		while (*q && *q != '\n') {
			const char *next = q;
			if (*next == ' ')
				last_space = q;
			next++;
			while ((*next & 0xC0) == 0x80)
				next++;
			if (text_width_n(pen_font, font_size, start, (int)(next - start)) > width && q > start) {
				if (last_space && last_space > start)
					q = last_space;
				break;
			}
			q = next;
		}
		int n = (int)(q - start);
		while (n > 0 && start[n - 1] == ' ')
			n--;
		if (line)
			line(start, n, count, data);
		count++;
		p = q;
		while (*p == ' ')
			p++;
		if (*p == '\n')
			p++;
	}
	return count ? count : 1;
}

int cby_game_stage_lines_for(int64_t win, double width, const char *text)
{
	game(win);
	pen_font_ready();
	return wrap_text(text, width, NULL, NULL);
}

typedef struct WrapAt {
	int64_t win;
	double x, y;
} WrapAt;

static void write_line(const char *s, int n, int i, void *data)
{
	WrapAt *w = data;
	char *one = malloc((size_t)n + 1);
	memcpy(one, s, (size_t)n);
	one[n] = '\0';
	cby_game_stage_text(w->win, w->x, w->y + i * line_height(pen_font, font_size), one);
	free(one);
}

/* the text, wrapped to fit width, its top left corner at x, y: how many
 * lines it took */
int cby_game_stage_write_wrapped(int64_t win, double x, double y, double width, const char *text)
{
	need_pen(win);
	WrapAt w = { win, x, y };
	return wrap_text(text, width, write_line, &w);
}

void cby_game_stage_image(int64_t win, double x, double y, const char *path)
{
	need_pen(win);
	int pic = picture(path);
	SDL_Texture *t = picture_texture(pic);
	SDL_FRect r = { (float)x, (float)y, (float)pictures[pic].w, (float)pictures[pic].h };
	if (t)
		SDL_RenderTexture(renderer, t, NULL, &r);
}

/* ------------------------------------------------------------- the window */

#ifndef TERMINAL_GAME
static SDL_Window *window;
#endif
static int64_t window_game;    /* the game in the window */

static int scripted(void) { return getenv("CABBY_GUI_SCRIPT") != NULL; }


#ifndef TERMINAL_GAME
/* While the interactive session waits for typing, the window system is
 * still answered (see rt_idle_hook in the runtime). (A game in a terminal
 * has no window system to answer.) */
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
#endif

static void start_sdl(void)
{
	static int started;
	if (started)
		return;
#ifdef TERMINAL_GAME
	/* no window, no screen: SDL draws into a picture in memory (see
	 * cby_game_run), which works anywhere, over SSH too */
	SDL_Init(0);
	started = 1;
#else
	if (scripted()) {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");   /* drawn, but not shown */
		/* and by SDL's own drawing, which needs no graphics library (a Mac
		 * has no OpenGL ES for the offscreen driver to find) and draws the
		 * same on every computer */
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	}
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "game: can't open a window: %s\n", SDL_GetError());
		ssh_instead("game", "tgame");
		exit(1);
	}
	started = 1;
	set_idle_hook();
#endif
}

/* the stage as it is now, as a picture (NULL if it can't be drawn) */
static SDL_Surface *stage_surface(int64_t win)
{
	Game *g = game(win);
	if (!renderer)
		return NULL;
	SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, g->width, g->height);
	if (!target)
		return NULL;
	SDL_Texture *was = SDL_GetRenderTarget(renderer);
	SDL_SetRenderTarget(renderer, target);
	SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
	draw_stage(win);
	SDL_Surface *s = SDL_RenderReadPixels(renderer, NULL);
	SDL_SetRenderTarget(renderer, was);
	SDL_SetRenderLogicalPresentation(renderer, g->width, g->height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_DestroyTexture(target);
	if (s && s->format != SDL_PIXELFORMAT_RGBA32) {
		SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(s);
		s = c;
	}
	return s;
}

/* the stage as it is now, saved as a PNG file */
static int save_stage(int64_t win, const char *path)
{
	SDL_Surface *s = stage_surface(win);
	if (!s)
		return 0;
	int ok = save_png(s, path);
	SDL_DestroySurface(s);
	return ok;
}

void cby_game_stage_save(int64_t win, const char *path)
{
	if (drawing)
		fail("%s", "the stage can't be saved while it's being drawn (save it from another handler)");
	if (!renderer || window_game != win)
		fail("%s", "the stage can only be saved while its game is running");
	if (!save_stage(win, path))
		fail("can't save the stage as '%s'", path);
}

/* Runs one frame. (By its number, not a pointer: handlers may add games
 * and sprites, which can move the tables.) */
/* Cabby's event loop (its runtime's: in the interpreter, or linked in),
 * stepped once a frame, so a worker's on_done (use threads), a process's
 * lines and events' timers are handled in a game too */
static void (*ev_step)(int);
static int (*ev_fds)(int *, int);

static void find_events(void)
{
	static int looked;
	if (!looked) {
		looked = 1;
		ev_step = (void (*)(int))dlsym(RTLD_DEFAULT, "cby_events_step");
		ev_fds = (int (*)(int *, int))dlsym(RTLD_DEFAULT, "cby_events_fds");
		if (!ev_step || !ev_fds)
			ev_step = NULL, ev_fds = NULL;
	}
}

static void step(int64_t win)
{
	Game *g = game(win);
	g->frames++;
	find_events();
	if (ev_step)
		ev_step(0);
	for (int i = 0; i < g->nkeys; i++) {
		KeyState *k = &g->keys[i];
		k->pressed = k->went_down;
		k->released = k->came_up;
		if (k->went_down)
			k->since = g->frames;
		if (k->came_up)
			k->held = g->frames - k->since;
		k->went_down = k->came_up = 0;
	}
	invoke(g->frame);
	for (int i = 0; i < games[win - 1].ntimers && !games[win - 1].closing; i++) {
		GameTimer *t = &games[win - 1].timers[i];
		if (!t->alive || games[win - 1].frames < t->next)
			continue;
		Handler h = t->h;
		if (t->every)
			t->next += t->every;
		else
			t->alive = 0;
		invoke(h);
	}
	g = &games[win - 1];
	for (int i = 0; i < g->nkeys; i++)
		g->keys[i].pressed = g->keys[i].released = 0;
}

static void present(int64_t win)
{
	draw_stage(win);
	SDL_RenderPresent(renderer);
}

void cby_game_close(int64_t win)
{
	if (win >= 1 && win <= ngames)
		games[win - 1].closing = 1;
}

/* a key went down (or up), by its name: the game's keys, the shortcuts,
 * the stage's on_key */
static void key_named(int64_t win, const char *name, int ctrl, int shift, int alt, int down, int repeat)
{
	if (!name[0])
		return;
	if (!repeat)
		key_goes(win, name, down);
	if (!down || repeat)
		return;
	char norm[48];
	snprintf(norm, sizeof norm, "%s%s%s%s", ctrl ? "ctrl+" : "", shift ? "shift+" : "", alt ? "alt+" : "", name);
	if (!shortcut(win, norm) && shift) {
		snprintf(norm, sizeof norm, "%s%s%s", ctrl ? "ctrl+" : "", alt ? "alt+" : "", name);
		shortcut(win, norm);   /* (Shift+P is P too, unless Shift+P is a shortcut of its own) */
	}
	Game *g = game(win);
	if (g->keyed.fn) {
		snprintf(g->key, sizeof g->key, "%s", strcmp(name, "space") == 0 ? " " : name);
		invoke(g->keyed);
	}
}

#ifndef TERMINAL_GAME
/* SDL's key */
static void key_event(int64_t win, SDL_Keycode code, SDL_Keymod mods, int down, int repeat)
{
	char name[16];
	sdl_key_name(code, name, sizeof name);
	key_named(win, name, (mods & SDL_KMOD_CTRL) != 0, (mods & SDL_KMOD_SHIFT) != 0, (mods & SDL_KMOD_ALT) != 0, down, repeat);
}
#endif

static void mouse_event(int64_t win, float x, float y, int click)
{
	Game *g = game(win);
	g->mouse_x = (int)floor(x);
	g->mouse_y = (int)floor(y);
	invoke(click ? g->clicked : g->moved);
}

static void run_script(int64_t win);

#ifdef TERMINAL_GAME
/* ------------------------------------------------------------- in a terminal */

static int64_t term_game;
static int message_done;

/* Keys, where the terminal doesn't say when they're let go: a key is held
 * until its repeats stop (a moment longer at first, before the terminal
 * starts repeating it). */
typedef struct Guess {
	char name[16];
	double last;
	int repeats;
} Guess;

static Guess guesses[16];
static int nguesses;

static void guesses_expire(int64_t win)
{
	double now = now_ms();
	for (int i = 0; i < nguesses; i++)
		if (now - guesses[i].last > (guesses[i].repeats ? 150 : 200)) {
			key_goes(win, guesses[i].name, 0);
			guesses[i--] = guesses[--nguesses];
		}
}

static void term_key(const char *name, int ctrl, int shift, int alt, int kind)
{
	int64_t win = term_game;
	if (ctrl && (!strcmp(name, "c") || !strcmp(name, "q"))) {
		if (kind == 1)
			games[win - 1].closing = 1;   /* Ctrl+C or Ctrl+Q: the game ends */
		return;
	}
	if (message_box) {
		if (kind == 1 && (!strcmp(name, "enter") || !strcmp(name, "space") || !strcmp(name, "escape")))
			message_done = 1;
		return;
	}
	if (kitty_keys) {
		key_named(win, name, ctrl, shift, alt, kind != 3, kind == 2);
		return;
	}
	for (int i = 0; i < nguesses; i++)
		if (!strcmp(guesses[i].name, name)) {
			guesses[i].last = now_ms();   /* the terminal repeating it: still held */
			guesses[i].repeats++;
			key_named(win, name, ctrl, shift, alt, 1, 1);
			return;
		}
	if (nguesses < 16) {
		Guess *g = &guesses[nguesses++];
		snprintf(g->name, sizeof g->name, "%s", name);
		g->last = now_ms();
		g->repeats = 0;
	}
	key_named(win, name, ctrl, shift, alt, 1, 0);
}

static void term_mouse(int col, int row, int click)
{
	int64_t win = term_game;
	Game *g = game(win);
	if (message_box || term_scale <= 0)
		return;
	double x = (col - 1 - pic_col + 0.5) / term_scale, y = ((row - 1 - pic_row) * 2 + 1) / term_scale;
	if (x < 0 || y < 0 || x >= g->width || y >= g->height)
		return;
	if (click || g->moved.fn)
		mouse_event(win, (float)x, (float)y, click);
}

/* the stage, drawn and shown in the terminal (only what changed) */
static void term_present(int64_t win)
{
	Game *g = game(win);
	clear_texts();
	text_as_cells = 1;
	SDL_Surface *s = stage_surface(win);
	text_as_cells = 0;
	if (!s)
		return;
	int resized = frame_to_cells(s, g->width, g->height);
	SDL_DestroySurface(s);
	if (resized)
		puts_("\x1b[0m\x1b[2J");
	cells_out(term_fd, resized);
}

/* The terminal as the script's `terminal` command saves it: a picture of
 * the stage in characters, CABBY_TERMINAL_SIZE big (80x24 to begin with),
 * as the codes a terminal would be sent. */
static int term_snapshot(int64_t win, const char *path)
{
	const char *size = getenv("CABBY_TERMINAL_SIZE");
	int c = 80, r = 24;
	if (size)
		sscanf(size, "%dx%d", &c, &r);
	term_cols = c > 0 ? c : 80;
	term_rows = r > 0 ? r : 24;
	term_resized = 0;
	truecolor = 1;
	Game *g = game(win);
	clear_texts();
	text_as_cells = 1;
	SDL_Surface *s = stage_surface(win);
	text_as_cells = 0;
	if (!s)
		return 0;
	frame_to_cells(s, g->width, g->height);
	SDL_DestroySurface(s);
	FILE *f = fopen(path, "w");
	if (!f)
		return 0;
	cells_out(fileno(f), 1);
	fclose(f);
	return 1;
}

static void term_run(int64_t win)
{
	term_game = win;
	nguesses = 0;
	term_start();
	if (games[win - 1].moved.fn)
		term_mouse_moves(1);
	free(shown);
	shown = NULL;
	term_resized = 1;
	const double each = 1000.0 / GAME_FPS;
	double last = now_ms(), owed = 0;
	long shown_frame = -1;
	while (!games[win - 1].closing) {
		read_input(term_key, term_mouse);
		if (!kitty_keys)
			guesses_expire(win);
		double now = now_ms();
		owed += now - last;
		last = now;
		if (owed > 250)
			owed = 250;   /* after a stall, catch up a little, not all at once */
		while (owed >= each && !games[win - 1].closing) {
			owed -= each;
			if (!games[win - 1].paused)
				step(win);
		}
		if (games[win - 1].closing)
			break;
		/* shown 30 times a second (the game still runs at 60) */
		long f = games[win - 1].frames;
		if (term_resized || shown_frame < 0 || f - shown_frame >= 2 || games[win - 1].paused) {
			term_present(win);
			shown_frame = f;
		}
		double spent = now_ms() - now, wait = each - spent;
		if (wait > 1) {
			struct pollfd p = { 0, POLLIN, 0 };
			poll(&p, 1, (int)wait);   /* (a key wakes it sooner) */
		}
	}
	term_stop();
}
#endif

void cby_game_run(int64_t win)
{
	Game *g = game(win);
	if (renderer)
		fail("%s", "a game is already running: one game window at a time");
	start_sdl();
#ifdef TERMINAL_GAME
	/* drawn into a picture of the stage's size, by SDL's own drawing code */
	SDL_Surface *canvas = SDL_CreateSurface(g->width, g->height, SDL_PIXELFORMAT_RGBA32);
	renderer = canvas ? SDL_CreateSoftwareRenderer(canvas) : NULL;
	if (!renderer) {
		fprintf(stderr, "tgame: can't draw the game: %s\n", SDL_GetError());
		exit(1);
	}
#else
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (!make_window(g->title, g->width, g->height, flags, scripted(), &window, &renderer)) {
		fprintf(stderr, "game: can't open a window: %s\n", SDL_GetError());
		ssh_instead("game", "tgame");
		exit(1);
	}
	ssh_note("game", "tgame");
#endif
	window_game = win;
	g->open = 1;
	g->closing = 0;
	SDL_SetRenderLogicalPresentation(renderer, g->width, g->height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	if (!scripted())
		SDL_SetRenderVSync(renderer, 1);   /* (not for a test's: SDL would wait a 60th of a second a frame) */
	if (scripted()) {
		run_script(win);
	} else {
#ifdef TERMINAL_GAME
		term_run(win);
#else
		const Uint64 each = 1000000000ull / GAME_FPS;
		Uint64 last = SDL_GetTicksNS(), owed = 0;
		while (!games[win - 1].closing) {
			SDL_Event e;
			while (SDL_PollEvent(&e)) {
				SDL_ConvertEventToRenderCoordinates(renderer, &e);
				if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
					games[win - 1].closing = 1;
				else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
					key_event(win, e.key.key, e.key.mod, e.type == SDL_EVENT_KEY_DOWN, e.key.repeat);
				else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
					mouse_event(win, e.button.x, e.button.y, 1);
				else if (e.type == SDL_EVENT_MOUSE_MOTION && games[win - 1].moved.fn)
					mouse_event(win, e.motion.x, e.motion.y, 0);
			}
			Uint64 now = SDL_GetTicksNS();
			owed += now - last;
			last = now;
			if (owed > 250000000ull)
				owed = 250000000ull;   /* after a stall, catch up a little, not all at once */
			while (owed >= each && !games[win - 1].closing) {
				owed -= each;
				if (!games[win - 1].paused)
					step(win);
			}
			if (games[win - 1].closing)
				break;
			present(win);
			Uint64 spent = SDL_GetTicksNS() - now;
			if (spent < each / 2)
				SDL_DelayNS(each / 2 - spent);   /* (if the screen doesn't keep time for us) */
		}
#endif
	}
	games[win - 1].open = 0;
	forget_textures(renderer);
	SDL_DestroyRenderer(renderer);
	renderer = NULL;
	window_game = 0;
#ifdef TERMINAL_GAME
	SDL_DestroySurface(canvas);
#else
	SDL_DestroyWindow(window);
	window = NULL;
	/* (gone from the screen now, not when events are next looked at) */
	for (int i = 0; i < 10; i++) {
		SDL_PumpEvents();
		SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
		SDL_Delay(5);
	}
#endif
	fflush(stdout);
}

#ifndef TERMINAL_GAME
typedef struct BoxLine {
	double x, y, step;
} BoxLine;

static void box_line(const char *s, int n, int i, void *data)
{
	BoxLine *b = data;
	draw_text_n(pen_font, font_size, b->x, b->y + i * b->step, s, n, (SDL_FColor){ 0.17f, 0.17f, 0.22f, 1 });
}

/* show_message's box, over the stage: the game a little darker, a white box
 * in the middle with the text wrapped in it, and an OK button (where, in ok) */
static void message_box_drawn(Game *g, const char *text, SDL_FRect *ok)
{
	pen_font = font_for("Sans", 0, 0);
	font_size = 16;
	double lh = line_height(pen_font, font_size);
	double w = g->width - 48 < 440 ? g->width - 48 : 440;
	int lines = wrap_text(text, w - 48, NULL, NULL);
	double h = lines * lh + 24 + 16 + 34 + 20;
	double x = (g->width - w) / 2, y = (g->height - h) / 2;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColorFloat(renderer, 0, 0, 0, 0.35f);
	SDL_RenderFillRect(renderer, &(SDL_FRect){ 0, 0, (float)g->width, (float)g->height });
	SDL_SetRenderDrawColorFloat(renderer, 0.17f, 0.17f, 0.22f, 1);
	SDL_RenderFillRect(renderer, &(SDL_FRect){ (float)x - 3, (float)y - 3, (float)w + 6, (float)h + 6 });
	SDL_SetRenderDrawColorFloat(renderer, 1, 1, 1, 1);
	SDL_RenderFillRect(renderer, &(SDL_FRect){ (float)x, (float)y, (float)w, (float)h });
	BoxLine b = { x + 24, y + 24, lh };
	wrap_text(text, w - 48, box_line, &b);
	double tw = text_width(pen_font, font_size, "OK");
	*ok = (SDL_FRect){ (float)(x + w - 24 - (tw + 48)), (float)(y + h - 20 - 34), (float)(tw + 48), 34 };
	SDL_SetRenderDrawColorFloat(renderer, 0.17f, 0.17f, 0.22f, 1);
	SDL_RenderFillRect(renderer, ok);
	draw_text(pen_font, font_size, ok->x + 24, ok->y + (34 - lh) / 2, "OK", (SDL_FColor){ 1, 1, 1, 1 });
}
#endif

void cby_game_show_message(int64_t win, const char *text)
{
	Game *g = game(win);
	if (scripted())
		return;   /* (a test has no one to read it) */
#ifdef TERMINAL_GAME
	if (!term_on || term_game != win) {
		printf("%s\n", text);   /* (the game isn't running yet) */
		return;
	}
	/* a box over the game, which waits (no frames) until it's closed */
	message_box = strdup(text);
	message_done = 0;
	term_present(win);
	while (!message_done && !games[win - 1].closing) {
		struct pollfd p = { 0, POLLIN, 0 };
		poll(&p, 1, 50);
		read_input(term_key, term_mouse);
		if (term_resized)
			term_present(win);
	}
	free(message_box);
	message_box = NULL;
	term_present(win);
	(void)g;
#else
	if (!window || window_game != win || !g->open) {
		printf("%s\n", text);   /* (the game isn't running yet) */
		return;
	}
	/* a box over the game, drawn by the game itself (not the desktop's own
	 * message box, which on Linux is another program, slow to appear): it
	 * waits, with no frames, until it's closed */
	int done = 0;
	SDL_FRect ok;
	while (!done && !g->closing) {
		draw_stage(win);
		message_box_drawn(g, text, &ok);
		SDL_RenderPresent(renderer);
		SDL_Event e;
		if (!SDL_WaitEventTimeout(&e, 100))
			continue;
		do {
			SDL_ConvertEventToRenderCoordinates(renderer, &e);
			if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				g->closing = 1;
			else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
				 (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE ||
				  e.key.key == SDLK_ESCAPE))
				done = 1;
			else if (e.type == SDL_EVENT_KEY_UP)
				key_event(win, e.key.key, e.key.mod, 0, 0);   /* (a key let go is let go, for the game) */
			else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.x >= ok.x && e.button.x < ok.x + ok.w &&
				 e.button.y >= ok.y && e.button.y < ok.y + ok.h)
				done = 1;
		} while (!done && SDL_PollEvent(&e));
	}
	/* (the key that closed it, and anything else pressed meanwhile, isn't
	 * the game's) */
	SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN);
#endif
}

/* ------------------------------------------------------------- test scripts
 *
 * CABBY_GUI_SCRIPT, as for gui: `;` between commands. Frames run only when
 * the script says. */

static char *script_left;
static char *script_all;      /* (kept, so it's freed at the script's end, not lost) */

static void run_script_steps(int64_t win);

/* the script's commands, until it ends or the window closes; at its end,
 * the copy of it is freed */
static void run_script(int64_t win)
{
	static int loaded;
	if (!loaded) {
		loaded = 1;
		script_left = script_all = strdup(getenv("CABBY_GUI_SCRIPT"));
	}
	run_script_steps(win);
	if (!script_left || !*script_left) {
		free(script_all);
		script_all = script_left = NULL;
	}
}

static void run_script_steps(int64_t win)
{
	present(win);
	while (script_left && *script_left && !games[win - 1].closing) {
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
		int frames = 0;
		if (strcmp(cmd, "processes") == 0) {
			/* until what the event loop watches (workers, processes) is done */
			find_events();
			int fds[64];
			while (ev_fds && ev_fds(fds, 64) > 0)
				ev_step(50);
		} else if (strncmp(cmd, "frames ", 7) == 0) {
			frames = atoi(cmd + 7);
		} else if (strncmp(cmd, "wait ", 5) == 0) {
			frames = (int)((long)atoi(cmd + 5) * GAME_FPS / 1000);
		} else if (strncmp(cmd, "hold ", 5) == 0) {
			key_goes(win, cmd + 5, 1);
		} else if (strncmp(cmd, "release ", 8) == 0) {
			key_goes(win, cmd + 8, 0);
		} else if (strncmp(cmd, "tap ", 4) == 0) {
			key_goes(win, cmd + 4, 1);
			if (!games[win - 1].paused)
				step(win);
			key_goes(win, cmd + 4, 0);
			frames = 1;   /* and the frame that hears it come up */
		} else if (strncmp(cmd, "key ", 4) == 0) {
			Game *g = game(win);
			if (g->keyed.fn) {
				snprintf(g->key, sizeof g->key, "%s", strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
				invoke(g->keyed);
			}
		} else if (strncmp(cmd, "shortcut ", 9) == 0) {
			char norm[48];
			if (!parse_keys(cmd + 9, norm, sizeof norm) || !shortcut(win, norm))
				fprintf(stderr, "game script: no shortcut '%s'\n", cmd + 9);
		} else if (strncmp(cmd, "press ", 6) == 0 || strncmp(cmd, "point ", 6) == 0) {
			int n = 0;
			double x = 0, y = 0;
			if (sscanf(cmd + 6, "%d %lf %lf", &n, &x, &y) == 3)
				mouse_event(win, (float)x, (float)y, cmd[1] == 'r');
			else
				fprintf(stderr, "game script: '%s' needs a stage and a place: press 1 120 80\n", cmd);
		} else if (strncmp(cmd, "shot ", 5) == 0) {
			if (!save_stage(win, cmd + 5))
				fprintf(stderr, "game script: could not save %s\n", cmd + 5);
#ifdef TERMINAL_GAME
		} else if (strncmp(cmd, "terminal ", 9) == 0) {
			/* terminal <file>: the stage as the terminal would show it */
			if (!term_snapshot(win, cmd + 9))
				fprintf(stderr, "game script: could not save %s\n", cmd + 9);
#endif
		} else if (strcmp(cmd, "close") == 0) {
			games[win - 1].closing = 1;
		} else if (*cmd) {
			fprintf(stderr, "game script: unknown command '%s'\n", cmd);
		}
		for (int i = 0; i < frames && !games[win - 1].closing; i++) {
			if (!games[win - 1].paused)
				step(win);
			present(win);
		}
	}
}
