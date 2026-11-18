/* world.c — the world library (`use world`): 3D. A window looking into a
 * world of shapes (boxes, balls, planes, cylinders, cones), colored or with
 * pictures on them, lit by the sun and by lamps, seen by a camera; and, as
 * in the game library, frames, keys, timers and test scripts.
 *
 * Distances are in metres, as a person would think of them: y is up, and
 * the camera starts at 0, 1.6, 6 (standing, a little back), looking along
 * -z. A shape's position is its middle (a plane's: its middle, flat).
 * Turning is in degrees: a shape's angle turns it round y, as a person
 * turns, counterclockwise seen from above; at 0 it faces -z.
 *
 * Drawn with OpenGL (3.3, a Mac's 4.1) where there is one: SDL 3 opens it,
 * and its functions are looked up when the window opens, so nothing more
 * is linked. Under a test script, and where there's no OpenGL (over SSH,
 * say), the world is drawn by Cabby itself (world_soft.h): slower, but the
 * same picture on every computer, to the pixel, which is what the tests
 * check. The two draw alike: the same shapes, lit the same way. */

#define SDL_LIB_NAME "world"
#include "sdl_common.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>

#define WORLD_FPS 60

/* ------------------------------------------------------------- vectors and matrices
 *
 * Column-major 4x4 matrices of floats, as OpenGL has them: m[col * 4 + row]. */

typedef struct V3 {
	float x, y, z;
} V3;

static V3 v3(float x, float y, float z) { return (V3){ x, y, z }; }
static V3 v_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 v_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 v_mul(V3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float v_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 v_cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float v_len(V3 a) { return sqrtf(v_dot(a, a)); }
static V3 v_norm(V3 a)
{
	float l = v_len(a);
	return l > 1e-12f ? v_mul(a, 1.0f / l) : v3(0, 0, 0);
}

typedef struct M4 {
	float m[16];
} M4;

static M4 m_identity(void)
{
	M4 r = { { 0 } };
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
	return r;
}

static M4 m_mul(M4 a, M4 b)
{
	M4 r;
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++) {
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += a.m[k * 4 + row] * b.m[c * 4 + k];
			r.m[c * 4 + row] = s;
		}
	return r;
}

static M4 m_translate(float x, float y, float z)
{
	M4 r = m_identity();
	r.m[12] = x;
	r.m[13] = y;
	r.m[14] = z;
	return r;
}

static M4 m_scale(float x, float y, float z)
{
	M4 r = m_identity();
	r.m[0] = x;
	r.m[5] = y;
	r.m[10] = z;
	return r;
}

static float radians(double degrees) { return (float)(degrees * M_PI / 180.0); }

static M4 m_rotate_y(float a)
{
	M4 r = m_identity();
	float c = cosf(a), s = sinf(a);
	r.m[0] = c;
	r.m[2] = -s;
	r.m[8] = s;
	r.m[10] = c;
	return r;
}

static M4 m_rotate_x(float a)
{
	M4 r = m_identity();
	float c = cosf(a), s = sinf(a);
	r.m[5] = c;
	r.m[6] = s;
	r.m[9] = -s;
	r.m[10] = c;
	return r;
}

static M4 m_rotate_z(float a)
{
	M4 r = m_identity();
	float c = cosf(a), s = sinf(a);
	r.m[0] = c;
	r.m[1] = s;
	r.m[4] = -s;
	r.m[5] = c;
	return r;
}

static M4 m_perspective(float fov_y, float aspect, float near, float far)
{
	M4 r = { { 0 } };
	float f = 1.0f / tanf(fov_y / 2);
	r.m[0] = f / aspect;
	r.m[5] = f;
	r.m[10] = (far + near) / (near - far);
	r.m[11] = -1;
	r.m[14] = 2 * far * near / (near - far);
	return r;
}

/* the camera at eye, looking along forward */
static M4 m_look(V3 eye, V3 forward, V3 up)
{
	V3 f = v_norm(forward);
	V3 s = v_norm(v_cross(f, up));
	if (v_len(s) < 1e-6f)
		s = v3(1, 0, 0);   /* (straight up or down: any side will do) */
	V3 u = v_cross(s, f);
	M4 r = m_identity();
	r.m[0] = s.x;
	r.m[4] = s.y;
	r.m[8] = s.z;
	r.m[1] = u.x;
	r.m[5] = u.y;
	r.m[9] = u.z;
	r.m[2] = -f.x;
	r.m[6] = -f.y;
	r.m[10] = -f.z;
	r.m[12] = -v_dot(s, eye);
	r.m[13] = -v_dot(u, eye);
	r.m[14] = v_dot(f, eye);
	return r;
}

static V3 m_point(const M4 *a, V3 p)
{
	return v3(a->m[0] * p.x + a->m[4] * p.y + a->m[8] * p.z + a->m[12],
		  a->m[1] * p.x + a->m[5] * p.y + a->m[9] * p.z + a->m[13],
		  a->m[2] * p.x + a->m[6] * p.y + a->m[10] * p.z + a->m[14]);
}

/* the 3x3 that turns normals: the inverse of the model's turning and
 * stretching, transposed (so a stretched box's sides still face out) */
static void m_normals(const M4 *a, float n[9])
{
	float a00 = a->m[0], a01 = a->m[4], a02 = a->m[8];
	float a10 = a->m[1], a11 = a->m[5], a12 = a->m[9];
	float a20 = a->m[2], a21 = a->m[6], a22 = a->m[10];
	float c00 = a11 * a22 - a12 * a21, c01 = a12 * a20 - a10 * a22, c02 = a10 * a21 - a11 * a20;
	float det = a00 * c00 + a01 * c01 + a02 * c02;
	float k = fabsf(det) > 1e-20f ? 1.0f / det : 0;
	/* the transposed inverse is the cofactors over the determinant;
	 * column-major, as OpenGL's mat3 */
	n[0] = c00 * k;
	n[1] = c01 * k;
	n[2] = c02 * k;
	n[3] = (a02 * a21 - a01 * a22) * k;
	n[4] = (a00 * a22 - a02 * a20) * k;
	n[5] = (a01 * a20 - a00 * a21) * k;
	n[6] = (a01 * a12 - a02 * a11) * k;
	n[7] = (a02 * a10 - a00 * a12) * k;
	n[8] = (a00 * a11 - a01 * a10) * k;
}

/* ------------------------------------------------------------- meshes
 *
 * A mesh is triangles: each vertex its position, its normal (which way the
 * surface faces there) and where on a picture it is (u, v: 0 to 1, v down
 * from the picture's top). The built-in shapes are made once, a metre
 * across, and stretched to size by the shape's matrix. */

#define VERT 8   /* floats a vertex: x y z, nx ny nz, u v */

typedef struct Mesh {
	float *v;
	int nv;
	uint32_t *ix;
	int ni;
	int two_sided;        /* (a plane: seen from below too) */
	V3 lo, hi;            /* the box round it */
	unsigned gl_vao, gl_vbo, gl_ebo;
	int changed;          /* its vertices moved (a skin bent): OpenGL's copy is out of date */
	int shared_ix;        /* (its triangles are another mesh's) */
} Mesh;

enum { MESH_BOX, MESH_BALL, MESH_PLANE, MESH_CYLINDER, MESH_CONE, NMESH_BUILT_IN };

static Mesh *meshes;
static int nmeshes;

static void mesh_vertex(Mesh *m, float x, float y, float z, float nx, float ny, float nz, float u, float v)
{
	m->v = realloc(m->v, sizeof *m->v * VERT * (size_t)(m->nv + 1));
	float *p = m->v + VERT * m->nv++;
	p[0] = x, p[1] = y, p[2] = z, p[3] = nx, p[4] = ny, p[5] = nz, p[6] = u, p[7] = v;
}

static void mesh_triangle(Mesh *m, uint32_t a, uint32_t b, uint32_t c)
{
	m->ix = realloc(m->ix, sizeof *m->ix * (size_t)(m->ni + 3));
	m->ix[m->ni++] = a;
	m->ix[m->ni++] = b;
	m->ix[m->ni++] = c;
}

static void mesh_bounds(Mesh *m)
{
	m->lo = v3(1e30f, 1e30f, 1e30f);
	m->hi = v3(-1e30f, -1e30f, -1e30f);
	for (int i = 0; i < m->nv; i++) {
		float *p = m->v + VERT * i;
		m->lo = v3(fminf(m->lo.x, p[0]), fminf(m->lo.y, p[1]), fminf(m->lo.z, p[2]));
		m->hi = v3(fmaxf(m->hi.x, p[0]), fmaxf(m->hi.y, p[1]), fmaxf(m->hi.z, p[2]));
	}
}

/* a box's side: its middle c, facing n, across it u and up it w (half
 * sizes); counterclockwise seen from outside */
static void box_side(Mesh *m, V3 c, V3 n, V3 u, V3 w)
{
	uint32_t b = (uint32_t)m->nv;
	V3 p0 = v_sub(v_sub(c, u), w), p1 = v_sub(v_add(c, u), w), p2 = v_add(v_add(c, u), w), p3 = v_add(v_sub(c, u), w);
	mesh_vertex(m, p0.x, p0.y, p0.z, n.x, n.y, n.z, 0, 1);
	mesh_vertex(m, p1.x, p1.y, p1.z, n.x, n.y, n.z, 1, 1);
	mesh_vertex(m, p2.x, p2.y, p2.z, n.x, n.y, n.z, 1, 0);
	mesh_vertex(m, p3.x, p3.y, p3.z, n.x, n.y, n.z, 0, 0);
	mesh_triangle(m, b, b + 1, b + 2);
	mesh_triangle(m, b, b + 2, b + 3);
}

static void make_box(Mesh *m)
{
	float h = 0.5f;
	box_side(m, v3(0, 0, h), v3(0, 0, 1), v3(h, 0, 0), v3(0, h, 0));     /* front (+z) */
	box_side(m, v3(0, 0, -h), v3(0, 0, -1), v3(-h, 0, 0), v3(0, h, 0));  /* back */
	box_side(m, v3(h, 0, 0), v3(1, 0, 0), v3(0, 0, -h), v3(0, h, 0));    /* right */
	box_side(m, v3(-h, 0, 0), v3(-1, 0, 0), v3(0, 0, h), v3(0, h, 0));   /* left */
	box_side(m, v3(0, h, 0), v3(0, 1, 0), v3(h, 0, 0), v3(0, 0, -h));    /* top */
	box_side(m, v3(0, -h, 0), v3(0, -1, 0), v3(h, 0, 0), v3(0, 0, h));   /* bottom */
}

static void make_plane(Mesh *m)
{
	box_side(m, v3(0, 0, 0), v3(0, 1, 0), v3(0.5f, 0, 0), v3(0, 0, -0.5f));
	m->two_sided = 1;
}

/* a ball: rings from the top down, each of segments round */
static void make_ball(Mesh *m)
{
	const int rings = 16, segments = 32;
	for (int r = 0; r <= rings; r++) {
		double a = M_PI * r / rings;
		for (int s = 0; s <= segments; s++) {
			double b = 2 * M_PI * s / segments;
			float nx = (float)(sin(a) * sin(b)), ny = (float)cos(a), nz = (float)(sin(a) * cos(b));
			mesh_vertex(m, nx * 0.5f, ny * 0.5f, nz * 0.5f, nx, ny, nz, (float)s / segments, (float)r / rings);
		}
	}
	for (int r = 0; r < rings; r++)
		for (int s = 0; s < segments; s++) {
			uint32_t a = (uint32_t)(r * (segments + 1) + s), b = a + (uint32_t)segments + 1;
			if (r > 0)
				mesh_triangle(m, a, b, a + 1);
			if (r < rings - 1)
				mesh_triangle(m, a + 1, b, b + 1);
		}
}

/* a cylinder (top 0.5 across) or a cone (top a point), a metre tall */
static void make_round(Mesh *m, int cone)
{
	const int segments = 32;
	float top = cone ? 0 : 0.5f;
	/* the side: its normal leans up for a cone */
	float lean = cone ? 0.5f : 0;   /* (radius / height, for a cone) */
	for (int s = 0; s <= segments; s++) {
		double b = 2 * M_PI * s / segments;
		float x = (float)sin(b), z = (float)cos(b);
		V3 n = v_norm(v3(x, lean, z));
		mesh_vertex(m, x * 0.5f, -0.5f, z * 0.5f, n.x, n.y, n.z, (float)s / segments, 1);
		mesh_vertex(m, x * top, 0.5f, z * top, n.x, n.y, n.z, (float)s / segments, 0);
	}
	for (int s = 0; s < segments; s++) {
		uint32_t a = (uint32_t)(2 * s);
		mesh_triangle(m, a, a + 2, a + 1);
		if (!cone)
			mesh_triangle(m, a + 1, a + 2, a + 3);
		else
			mesh_triangle(m, a + 1, a + 2, a + 3);   /* (thin: the top is a point) */
	}
	/* the ends: the bottom, and a cylinder's top */
	for (int end = 0; end < (cone ? 1 : 2); end++) {
		float y = end ? 0.5f : -0.5f, ny = end ? 1.0f : -1.0f;
		uint32_t c = (uint32_t)m->nv;
		mesh_vertex(m, 0, y, 0, 0, ny, 0, 0.5f, 0.5f);
		for (int s = 0; s <= segments; s++) {
			double b = 2 * M_PI * s / segments;
			float x = (float)sin(b), z = (float)cos(b);
			mesh_vertex(m, x * 0.5f, y, z * 0.5f, 0, ny, 0, 0.5f + x * 0.5f, 0.5f - z * 0.5f);
		}
		for (int s = 0; s < segments; s++) {
			if (end)
				mesh_triangle(m, c, c + 1 + (uint32_t)s, c + 2 + (uint32_t)s);
			else
				mesh_triangle(m, c, c + 2 + (uint32_t)s, c + 1 + (uint32_t)s);
		}
	}
}

static void make_meshes(void)
{
	if (nmeshes)
		return;
	nmeshes = NMESH_BUILT_IN;
	meshes = calloc(NMESH_BUILT_IN, sizeof *meshes);
	make_box(&meshes[MESH_BOX]);
	make_ball(&meshes[MESH_BALL]);
	make_plane(&meshes[MESH_PLANE]);
	make_round(&meshes[MESH_CYLINDER], 0);
	make_round(&meshes[MESH_CONE], 1);
	for (int i = 0; i < nmeshes; i++)
		mesh_bounds(&meshes[i]);
}

/* ------------------------------------------------------------- worlds, shapes and lamps */

typedef struct KeyState {
	char name[16];
	int down, went_down, came_up, pressed, released;
	long since, held;
} KeyState;

typedef struct Timer {
	long every, next;
	Handler h;
	int alive;
} Timer;

typedef struct Shortcut {
	char keys[48];
	Handler h;
} Shortcut;

struct Pose;

typedef struct Shape {
	int world;            /* from 1 */
	int alive, visible, mesh;
	int model;            /* a model's (index into models; -1: a built-in shape) */
	struct Pose *pose;
	double x, y, z;
	double yaw, pitch, roll;   /* degrees: round y, then x, then z */
	double w, h, d;       /* its size, in metres */
	double scale;
	Color color;
	int picture;          /* (index into pictures; -1: none) */
	double tile;          /* metres a copy of the picture covers (0: once, stretched) */
	char *group;
} Shape;

static Shape *shapes;
static int nshapes;

typedef struct Lamp {
	int world, alive;
	double x, y, z, reach;
	Color color;
} Lamp;

static Lamp *lamps;
static int nlamps;

#define MAX_LAMPS 8   /* (lit at once: the nearest) */

typedef struct World {
	char *title;
	int width, height;    /* the window's size to begin with */
	Color background;
	Handler frame, clicked, moved, keyed;
	long frames;
	int paused;
	KeyState *keys;
	int nkeys;
	Timer *timers;
	int ntimers;
	Shortcut *shortcuts;
	int nshortcuts;
	/* the camera: where it is, which way it faces */
	double cx, cy, cz, cyaw, cpitch, fov;
	int64_t follow;
	double follow_distance, follow_height;
	/* the light */
	V3 sun;               /* the way the sunlight goes (down, and a little sideways) */
	Color sun_color, ambient;
	/* the mouse */
	int mouse_x, mouse_y;
	int locked;           /* lock_mouse: hidden, and moved_x/moved_y say how far it went */
	double moved_x, moved_y, moving_x, moving_y;
	char key[16];
	int open, closing;
} World;

static World *worlds;
static int nworlds;

static World *world(int64_t id)
{
	if (id < 1 || id > nworlds)
		fail("%s", "this world isn't open yet");
	return &worlds[id - 1];
}

static Shape *shape(int64_t id)
{
	if (id < 1 || id > nshapes)
		fail("%s", "this shape hasn't been added to a world yet");
	return &shapes[id - 1];
}

static Lamp *lamp(int64_t id)
{
	if (id < 1 || id > nlamps)
		fail("%s", "this lamp hasn't been added to a world yet");
	return &lamps[id - 1];
}

int64_t cby_world_window_new(void)
{
	worlds = realloc(worlds, sizeof *worlds * (size_t)(nworlds + 1));
	worlds[nworlds] = (World){
		.title = strdup("Cabby"), .width = 800, .height = 600, .background = color("skyblue"),
		.cx = 0, .cy = 1.6, .cz = 6, .fov = 60,
		.sun = v_norm(v3(-0.4f, -1, -0.6f)), .sun_color = { 0.85f, 0.85f, 0.8f, 1 }, .ambient = { 0.35f, 0.35f, 0.4f, 1 },
		.follow_distance = 5, .follow_height = 2,
	};
	return ++nworlds;
}

static void world_retitle(int64_t id);

void cby_world_set_title(int64_t id, const char *title)
{
	World *w = world(id);
	free(w->title);
	w->title = strdup(title);
	world_retitle(id);
}

void cby_world_set_size(int64_t id, int width, int height)
{
	World *w = world(id);
	if (width < 1 || height < 1)
		fail("%s", "a window must be at least 1 by 1 pixel");
	w->width = width;
	w->height = height;
}

int cby_world_width(int64_t id) { return world(id)->width; }
int cby_world_height(int64_t id) { return world(id)->height; }
void cby_world_set_background(int64_t id, const char *c) { world(id)->background = color(c); }
void cby_world_on_frame(int64_t id, void *obj, void *fn) { world(id)->frame = (Handler){ obj, fn }; }
void cby_world_on_click(int64_t id, void *obj, void *fn) { world(id)->clicked = (Handler){ obj, fn }; }
void cby_world_on_mouse_move(int64_t id, void *obj, void *fn) { world(id)->moved = (Handler){ obj, fn }; }
void cby_world_on_key(int64_t id, void *obj, void *fn) { world(id)->keyed = (Handler){ obj, fn }; }
int64_t cby_world_frame(int64_t id) { return id < 1 ? 0 : world(id)->frames; }
void cby_world_set_paused(int64_t id, int on) { world(id)->paused = on != 0; }
int cby_world_paused(int64_t id) { return id < 1 ? 0 : world(id)->paused; }
int cby_world_mouse_x(int64_t id) { return world(id)->mouse_x; }
int cby_world_mouse_y(int64_t id) { return world(id)->mouse_y; }
double cby_world_mouse_moved_x(int64_t id) { return world(id)->moved_x; }
double cby_world_mouse_moved_y(int64_t id) { return world(id)->moved_y; }

static char *handed;   /* text being handed to Cabby, a byte at a time */
static int64_t hand(const char *s)
{
	free(handed);
	handed = strdup(s ? s : "");
	return (int64_t)strlen(handed);
}
char cby_world_text_char(int64_t i) { return handed && i >= 0 && i < (int64_t)strlen(handed) ? handed[i] : 0; }
int64_t cby_world_key_length(int64_t id) { return hand(world(id)->key); }

void cby_world_every(int64_t id, int ms, int repeat, void *obj, void *fn)
{
	World *w = world(id);
	long frames = (long)ms * WORLD_FPS / 1000;
	if (frames < 1)
		frames = 1;
	w->timers = realloc(w->timers, sizeof *w->timers * (size_t)(w->ntimers + 1));
	w->timers[w->ntimers++] = (Timer){ repeat ? frames : 0, w->frames + frames, { obj, fn }, 1 };
}

void cby_world_stop_timers(int64_t id)
{
	World *w = world(id);
	for (int i = 0; i < w->ntimers; i++)
		w->timers[i].alive = 0;
}

/* ---- the light */

void cby_world_set_sun(int64_t id, double x, double y, double z)
{
	V3 d = v_norm(v3((float)x, (float)y, (float)z));
	if (v_len(d) == 0)
		fail("%s", "the sun needs a way to shine: set_sun 0, -1, 0 is straight down");
	world(id)->sun = d;
}

void cby_world_set_sun_color(int64_t id, const char *c) { world(id)->sun_color = color(c); }
void cby_world_set_ambient(int64_t id, const char *c) { world(id)->ambient = color(c); }

int64_t cby_world_lamp_new(int64_t id, double x, double y, double z, const char *c, double reach)
{
	world(id);
	if (reach <= 0)
		fail("%s", "a lamp's light must reach some way (in metres)");
	lamps = realloc(lamps, sizeof *lamps * (size_t)(nlamps + 1));
	lamps[nlamps] = (Lamp){ (int)id, 1, x, y, z, reach, color(c) };
	return ++nlamps;
}

void cby_world_lamp_move_to(int64_t id, double x, double y, double z)
{
	Lamp *l = lamp(id);
	l->x = x, l->y = y, l->z = z;
}

void cby_world_lamp_set_color(int64_t id, const char *c) { lamp(id)->color = color(c); }
void cby_world_lamp_set_reach(int64_t id, double r) { lamp(id)->reach = r > 0 ? r : 0.001; }
void cby_world_lamp_remove(int64_t id) { lamp(id)->alive = 0; }
double cby_world_lamp_x(int64_t id) { return lamp(id)->x; }
double cby_world_lamp_y(int64_t id) { return lamp(id)->y; }
double cby_world_lamp_z(int64_t id) { return lamp(id)->z; }

/* ---- shapes */

static int64_t new_shape(int64_t id, int mesh, double w, double h, double d, const char *c, double x, double y, double z)
{
	world(id);
	if (w <= 0 || h <= 0 || d <= 0)
		fail("%s", "a shape's size must be more than 0");
	make_meshes();
	shapes = realloc(shapes, sizeof *shapes * (size_t)(nshapes + 1));
	shapes[nshapes] = (Shape){ .world = (int)id, .alive = 1, .visible = 1, .mesh = mesh, .x = x, .y = y, .z = z,
				   .w = w, .h = h, .d = d, .scale = 1, .color = color(c), .picture = -1, .group = strdup(""),
				   .model = -1 };
	return ++nshapes;
}

int64_t cby_world_add_box(int64_t id, double w, double h, double d, const char *c, double x, double y, double z)
{
	return new_shape(id, MESH_BOX, w, h, d, c, x, y, z);
}

int64_t cby_world_add_ball(int64_t id, double r, const char *c, double x, double y, double z)
{
	return new_shape(id, MESH_BALL, 2 * r, 2 * r, 2 * r, c, x, y, z);
}

int64_t cby_world_add_plane(int64_t id, double w, double d, const char *c, double x, double y, double z)
{
	/* (flat: its height is only for its box, to touch things on it) */
	int64_t s = new_shape(id, MESH_PLANE, w, 1, d, c, x, y, z);
	shapes[s - 1].h = 0.001;
	return s;
}

int64_t cby_world_add_cylinder(int64_t id, double r, double h, const char *c, double x, double y, double z)
{
	return new_shape(id, MESH_CYLINDER, 2 * r, h, 2 * r, c, x, y, z);
}

int64_t cby_world_add_cone(int64_t id, double r, double h, const char *c, double x, double y, double z)
{
	return new_shape(id, MESH_CONE, 2 * r, h, 2 * r, c, x, y, z);
}

int64_t cby_world_shape_world(int64_t id) { return shape(id)->world; }
double cby_world_shape_x(int64_t id) { return shape(id)->x; }
double cby_world_shape_y(int64_t id) { return shape(id)->y; }
double cby_world_shape_z(int64_t id) { return shape(id)->z; }
double cby_world_shape_angle(int64_t id) { return shape(id)->yaw; }
double cby_world_shape_tilt(int64_t id) { return shape(id)->pitch; }
double cby_world_shape_roll(int64_t id) { return shape(id)->roll; }
double cby_world_shape_scale(int64_t id) { return shape(id)->scale; }
int cby_world_shape_visible(int64_t id) { return shape(id)->visible; }
int cby_world_shape_removed(int64_t id) { return !shape(id)->alive; }

void cby_world_shape_move_to(int64_t id, double x, double y, double z)
{
	Shape *s = shape(id);
	s->x = x, s->y = y, s->z = z;
}

void cby_world_shape_move(int64_t id, double dx, double dy, double dz)
{
	Shape *s = shape(id);
	s->x += dx, s->y += dy, s->z += dz;
}

/* the way it faces, flat: at angle 0, -z */
static void facing(double yaw, double *fx, double *fz)
{
	double a = yaw * M_PI / 180.0;
	*fx = -sin(a);
	*fz = -cos(a);
}

void cby_world_shape_move_forward(int64_t id, double distance)
{
	Shape *s = shape(id);
	double fx, fz;
	facing(s->yaw, &fx, &fz);
	s->x += fx * distance;
	s->z += fz * distance;
}

/* sideways: to its right */
void cby_world_shape_move_sideways(int64_t id, double distance)
{
	Shape *s = shape(id);
	double fx, fz;
	facing(s->yaw, &fx, &fz);
	s->x += -fz * distance;
	s->z += fx * distance;
}

static double wrap_degrees(double a)
{
	a = fmod(a, 360);
	/* (+ 0.0: no -0, which would print as "-0.0") */
	return a < 0 ? a + 360 : a + 0.0;
}

void cby_world_shape_set_angle(int64_t id, double degrees) { shape(id)->yaw = wrap_degrees(degrees); }
void cby_world_shape_turn(int64_t id, double degrees) { Shape *s = shape(id); s->yaw = wrap_degrees(s->yaw + degrees); }
void cby_world_shape_set_tilt(int64_t id, double degrees) { shape(id)->pitch = wrap_degrees(degrees); }
void cby_world_shape_tilt_by(int64_t id, double degrees) { Shape *s = shape(id); s->pitch = wrap_degrees(s->pitch + degrees); }
void cby_world_shape_set_roll(int64_t id, double degrees) { shape(id)->roll = wrap_degrees(degrees); }
void cby_world_shape_roll_by(int64_t id, double degrees) { Shape *s = shape(id); s->roll = wrap_degrees(s->roll + degrees); }
void cby_world_shape_set_scale(int64_t id, double k) { shape(id)->scale = k > 0 ? k : 0; }
void cby_world_shape_set_visible(int64_t id, int on) { shape(id)->visible = on != 0; }
void cby_world_shape_remove(int64_t id) { shape(id)->alive = 0; }
void cby_world_shape_set_color(int64_t id, const char *c) { shape(id)->color = color(c); }

void cby_world_shape_set_size(int64_t id, double w, double h, double d)
{
	Shape *s = shape(id);
	if (w <= 0 || h <= 0 || d <= 0)
		fail("%s", "a shape's size must be more than 0");
	if (s->model >= 0)
		fail("%s", "a model is the size it was made: make it bigger or smaller with set_scale");
	s->w = w, s->d = d;
	s->h = s->mesh == MESH_PLANE ? 0.001 : h;
}

void cby_world_shape_set_picture(int64_t id, const char *path)
{
	Shape *s = shape(id);
	s->picture = *path ? picture(path) : -1;
}

void cby_world_shape_set_picture_size(int64_t id, double metres) { shape(id)->tile = metres > 0 ? metres : 0; }

void cby_world_shape_set_group(int64_t id, const char *g)
{
	Shape *s = shape(id);
	free(s->group);
	s->group = strdup(g);
}

int64_t cby_world_shape_group_length(int64_t id) { return hand(shape(id)->group); }

#include "world_models.h"

/* a model, its origin (where Blender's was: usually its feet) at X, Y, Z */
int64_t cby_world_add_model(int64_t id, const char *path, double x, double y, double z)
{
	int m = model_load(path);
	int64_t sid = new_shape(id, MESH_BOX, 1, 1, 1, "white", x, y, z);
	Shape *s = &shapes[sid - 1];
	s->model = m;
	s->pose = pose_new(&models[m]);
	return sid;
}

static Shape *model_shape(int64_t id)
{
	Shape *s = shape(id);
	if (s->model < 0)
		fail("%s", "only a model has animations (one from add_model)");
	return s;
}

static int anim_named(const Model *m, const char *name)
{
	for (int i = 0; i < m->nanims; i++)
		if (strcmp(m->anims[i].name, name) == 0)
			return i;
	return -1;
}

/* plays an animation by its name (as Blender's action is called): looped,
 * or once, to its end */
void cby_world_shape_play(int64_t id, const char *name, int loop)
{
	Shape *s = model_shape(id);
	const Model *m = &models[s->model];
	int a = anim_named(m, name);
	if (a < 0) {
		char known[600] = "";
		for (int i = 0; i < m->nanims && strlen(known) < 500; i++)
			snprintf(known + strlen(known), sizeof known - strlen(known), "%s\"%s\"", i ? ", " : "", m->anims[i].name);
		if (!m->nanims)
			fail("the model '%s' has no animations", m->path);
		char msg[800];
		snprintf(msg, sizeof msg, "the model has no animation '%s': it has %s", name, known);
		fail("%s", msg);
	}
	Pose *p = s->pose;
	if (p->anim == a && p->loop == loop && !p->done)
		return;   /* (already playing: it goes on, not from the start) */
	p->anim = a;
	p->loop = loop;
	p->done = 0;
	p->time = p->speed < 0 ? m->anims[a].length : 0;
	p->posed = -1;
}

void cby_world_shape_stop_animation(int64_t id)
{
	Shape *s = model_shape(id);
	s->pose->anim = -1;
	s->pose->posed = -1;
}

void cby_world_shape_set_animation_speed(int64_t id, double speed) { model_shape(id)->pose->speed = speed; }

int cby_world_shape_animation_count(int64_t id)
{
	Shape *s = shape(id);
	return s->model < 0 ? 0 : models[s->model].nanims;
}

int64_t cby_world_shape_animation_name(int64_t id, int i)
{
	Shape *s = shape(id);
	if (s->model < 0 || i < 0 || i >= models[s->model].nanims)
		return hand("");
	return hand(models[s->model].anims[i].name);
}

int64_t cby_world_shape_playing(int64_t id)
{
	Shape *s = shape(id);
	if (s->model < 0 || s->pose->anim < 0 || s->pose->done)
		return hand("");
	return hand(models[s->model].anims[s->pose->anim].name);
}

double cby_world_shape_animation_time(int64_t id)
{
	Shape *s = shape(id);
	return s->model < 0 || s->pose->anim < 0 ? 0 : s->pose->time;
}

/* where a shape is drawn: its matrix */
static M4 shape_matrix(const Shape *s)
{
	M4 m = m_translate((float)s->x, (float)s->y, (float)s->z);
	m = m_mul(m, m_rotate_y(radians(s->yaw)));
	m = m_mul(m, m_rotate_x(radians(s->pitch)));
	m = m_mul(m, m_rotate_z(radians(s->roll)));
	float k = (float)s->scale;
	if (s->model >= 0)
		return m_mul(m, m_scale(k, k, k));   /* (a model is its own size) */
	float h = s->mesh == MESH_PLANE ? 1.0f : (float)s->h;
	return m_mul(m, m_scale((float)s->w * k, h * k, (float)s->d * k));
}

/* the box round a shape, as it stands in the world (turned, its corners) */
static void shape_box(const Shape *s, V3 *lo, V3 *hi)
{
	M4 m = shape_matrix(s);
	V3 mlo = s->model >= 0 ? models[s->model].lo : meshes[s->mesh].lo;
	V3 mhi = s->model >= 0 ? models[s->model].hi : meshes[s->mesh].hi;
	*lo = v3(1e30f, 1e30f, 1e30f);
	*hi = v3(-1e30f, -1e30f, -1e30f);
	for (int i = 0; i < 8; i++) {
		V3 c = v3(i & 1 ? mhi.x : mlo.x, i & 2 ? mhi.y : mlo.y, i & 4 ? mhi.z : mlo.z);
		V3 p = m_point(&m, c);
		*lo = v3(fminf(lo->x, p.x), fminf(lo->y, p.y), fminf(lo->z, p.z));
		*hi = v3(fmaxf(hi->x, p.x), fmaxf(hi->y, p.y), fmaxf(hi->z, p.z));
	}
	if (s->model < 0 && s->mesh == MESH_PLANE) {   /* (a plane is touched from just above it) */
		lo->y -= 0.0005f;
		hi->y += 0.0005f;
	}
}

/* touching: their boxes overlap (a ball's box is a little bigger than it,
 * so balls touch a little early at the corners) */
static int touching(const Shape *a, const Shape *b)
{
	if (!a->alive || !b->alive || a == b)
		return 0;
	V3 al, ah, bl, bh;
	shape_box(a, &al, &ah);
	shape_box(b, &bl, &bh);
	return al.x < bh.x && bl.x < ah.x && al.y < bh.y && bl.y < ah.y && al.z < bh.z && bl.z < ah.z;
}

int cby_world_shape_touches(int64_t a, int64_t b) { return touching(shape(a), shape(b)); }

double cby_world_shape_distance_to(int64_t a, int64_t b)
{
	Shape *s = shape(a), *t = shape(b);
	return sqrt((s->x - t->x) * (s->x - t->x) + (s->y - t->y) * (s->y - t->y) + (s->z - t->z) * (s->z - t->z));
}

/* the top of the shape, and its bottom: where to stand something on it */
double cby_world_shape_top(int64_t id) { V3 lo, hi; shape_box(shape(id), &lo, &hi); return hi.y; }
double cby_world_shape_bottom(int64_t id) { V3 lo, hi; shape_box(shape(id), &lo, &hi); return lo.y; }

/* ---- what's drawn of a shape: its parts (a built-in shape is one; a
 * model, one for each mesh in it), each with its matrix and look */

typedef struct Part {
	Mesh *mesh;
	M4 model;
	float color[4];
	int picture;
	float us, vs;
	int two_sided;
} Part;

static Part *parts;
static int cap_parts;

static void tiling(const Shape *s, float *us, float *vs);

static int shape_parts(Shape *s, long frame)
{
	M4 place = shape_matrix(s);
	if (s->model < 0) {
		if (cap_parts < 1)
			parts = realloc(parts, sizeof *parts * (size_t)(cap_parts = 8));
		Part *p = &parts[0];
		p->mesh = &meshes[s->mesh];
		p->model = place;
		p->color[0] = s->color.r, p->color[1] = s->color.g, p->color[2] = s->color.b, p->color[3] = s->color.a;
		p->picture = s->picture;
		tiling(s, &p->us, &p->vs);
		p->two_sided = p->mesh->two_sided;
		return 1;
	}
	Model *m = &models[s->model];
	Pose *pose = s->pose;
	if (pose->posed != frame) {
		pose_update(m, pose);
		pose->posed = frame;
	}
	if (cap_parts < m->nprims)
		parts = realloc(parts, sizeof *parts * (size_t)(cap_parts = m->nprims));
	M4 face = m_mul(place, model_facing());
	for (int i = 0; i < m->nprims; i++) {
		Prim *pr = &m->prims[i];
		Part *p = &parts[i];
		if (pr->skin >= 0) {
			p->mesh = pose->bent[i];
			p->model = face;   /* (a skin's vertices are where its joints put them, in the model) */
		} else {
			M4 nw;
			memcpy(nw.m, pose->world[pr->node], sizeof nw.m);
			p->mesh = pr->mesh;
			p->model = m_mul(face, nw);
		}
		float tint[4] = { s->color.r, s->color.g, s->color.b, s->color.a };
		for (int k = 0; k < 4; k++)
			p->color[k] = pr->color[k] * tint[k];
		p->picture = s->picture >= 0 ? s->picture : pr->picture;
		p->us = p->vs = 1;
		p->two_sided = pr->two_sided;
	}
	return m->nprims;
}

/* ---- finding shapes: those in a group (all, or those touching one) */

static int64_t *found;
static int nfound;

int cby_world_find(int64_t id, int64_t touching_id, const char *group)
{
	nfound = 0;
	Shape *t = touching_id ? shape(touching_id) : NULL;
	for (int i = 0; i < nshapes; i++) {
		Shape *s = &shapes[i];
		if (s->world != id || !s->alive || (*group && strcmp(s->group, group) != 0))
			continue;
		if (t && !touching(t, s))
			continue;
		found = realloc(found, sizeof *found * (size_t)(nfound + 1));
		found[nfound++] = i + 1;
	}
	return nfound;
}

int64_t cby_world_found(int i) { return i >= 0 && i < nfound ? found[i] : 0; }

/* ---- the camera */

void cby_world_move_camera_to(int64_t id, double x, double y, double z)
{
	World *w = world(id);
	w->cx = x, w->cy = y, w->cz = z;
	w->follow = 0;
}

void cby_world_move_camera(int64_t id, double dx, double dy, double dz)
{
	World *w = world(id);
	w->cx += dx, w->cy += dy, w->cz += dz;
	w->follow = 0;
}

/* forward the way it faces (flat, as a person walks), and sideways */
void cby_world_camera_forward(int64_t id, double distance)
{
	World *w = world(id);
	double fx, fz;
	facing(w->cyaw, &fx, &fz);
	w->cx += fx * distance;
	w->cz += fz * distance;
	w->follow = 0;
}

void cby_world_camera_sideways(int64_t id, double distance)
{
	World *w = world(id);
	double fx, fz;
	facing(w->cyaw, &fx, &fz);
	w->cx += -fz * distance;
	w->cz += fx * distance;
	w->follow = 0;
}

static void clamp_pitch(World *w)
{
	if (w->cpitch > 89)
		w->cpitch = 89;
	if (w->cpitch < -89)
		w->cpitch = -89;
}

void cby_world_point_camera_at(int64_t id, double x, double y, double z)
{
	World *w = world(id);
	double dx = x - w->cx, dy = y - w->cy, dz = z - w->cz;
	double flat = sqrt(dx * dx + dz * dz);
	if (flat < 1e-9 && fabs(dy) < 1e-9)
		return;
	w->cyaw = wrap_degrees(atan2(-dx, -dz) * 180 / M_PI);
	w->cpitch = atan2(dy, flat) * 180 / M_PI;
	clamp_pitch(w);
	w->follow = 0;
}

void cby_world_turn_camera(int64_t id, double yaw, double pitch)
{
	World *w = world(id);
	w->cyaw = wrap_degrees(w->cyaw + yaw);
	w->cpitch += pitch;
	clamp_pitch(w);
}

void cby_world_set_camera_angle(int64_t id, double yaw, double pitch)
{
	World *w = world(id);
	w->cyaw = wrap_degrees(yaw);
	w->cpitch = pitch;
	clamp_pitch(w);
}

void cby_world_set_field_of_view(int64_t id, double degrees)
{
	if (degrees < 5 || degrees > 150)
		fail("%s", "the field of view is in degrees, from 5 to 150 (60 to begin with)");
	world(id)->fov = degrees;
}

static void camera_follows(World *w);

void cby_world_follow(int64_t id, int64_t sid, double distance, double height)
{
	World *w = world(id);
	shape(sid);
	w->follow = sid;
	w->follow_distance = distance;
	w->follow_height = height;
	camera_follows(w);
}

double cby_world_camera_x(int64_t id) { return world(id)->cx; }
double cby_world_camera_y(int64_t id) { return world(id)->cy; }
double cby_world_camera_z(int64_t id) { return world(id)->cz; }
double cby_world_camera_angle(int64_t id) { return world(id)->cyaw; }
double cby_world_camera_tilt(int64_t id) { return world(id)->cpitch; }

/* following: behind the shape (the way it faces), above it, looking at it */
static void camera_follows(World *w)
{
	if (!w->follow || w->follow > nshapes || !shapes[w->follow - 1].alive)
		return;
	Shape *s = &shapes[w->follow - 1];
	double fx, fz;
	facing(s->yaw, &fx, &fz);
	w->cx = s->x - fx * w->follow_distance;
	w->cz = s->z - fz * w->follow_distance;
	w->cy = s->y + w->follow_height;
	int64_t keep = w->follow;
	cby_world_point_camera_at((int64_t)(w - worlds) + 1, s->x, s->y + s->h * s->scale * 0.25, s->z);
	w->follow = keep;
}

/* the camera's view, and its lens (for a picture aspect wide) */
static V3 camera_forward(const World *w)
{
	float y = radians(w->cyaw), p = radians(w->cpitch);
	return v3(-sinf(y) * cosf(p), sinf(p), -cosf(y) * cosf(p));
}

static M4 view_of(const World *w)
{
	return m_look(v3((float)w->cx, (float)w->cy, (float)w->cz), camera_forward(w), v3(0, 1, 0));
}

#define NEAR 0.05f
#define FAR 1000.0f

static M4 lens_of(const World *w, float aspect) { return m_perspective(radians(w->fov), aspect, NEAR, FAR); }

/* ---- what's under the mouse: the nearest shape whose box the ray from
 * the camera through that pixel goes into */

static int64_t pick(int64_t id, double px, double py, int width, int height)
{
	World *w = world(id);
	float aspect = (float)width / (float)height;
	float t = tanf(radians(w->fov) / 2);
	float nx = (float)(2 * (px + 0.5) / width - 1), ny = (float)(1 - 2 * (py + 0.5) / height);
	V3 f = v_norm(camera_forward(w));
	V3 s = v_norm(v_cross(f, v3(0, 1, 0)));
	if (v_len(s) < 1e-6f)
		s = v3(1, 0, 0);
	V3 u = v_cross(s, f);
	V3 dir = v_norm(v_add(f, v_add(v_mul(s, nx * t * aspect), v_mul(u, ny * t))));
	V3 eye = v3((float)w->cx, (float)w->cy, (float)w->cz);
	int64_t best = 0;
	float best_t = 1e30f;
	for (int i = 0; i < nshapes; i++) {
		Shape *sh = &shapes[i];
		if (sh->world != id || !sh->alive || !sh->visible)
			continue;
		V3 lo, hi;
		shape_box(sh, &lo, &hi);
		float t0 = 0, t1 = 1e30f;
		float o[3] = { eye.x, eye.y, eye.z }, d[3] = { dir.x, dir.y, dir.z };
		float l[3] = { lo.x, lo.y, lo.z }, h[3] = { hi.x, hi.y, hi.z };
		int miss = 0;
		for (int k = 0; k < 3 && !miss; k++) {
			if (fabsf(d[k]) < 1e-12f) {
				if (o[k] < l[k] || o[k] > h[k])
					miss = 1;
				continue;
			}
			float a = (l[k] - o[k]) / d[k], b = (h[k] - o[k]) / d[k];
			if (a > b) {
				float x = a;
				a = b;
				b = x;
			}
			t0 = fmaxf(t0, a);
			t1 = fminf(t1, b);
			if (t0 > t1)
				miss = 1;
		}
		if (!miss && t0 < best_t) {
			best_t = t0;
			best = i + 1;
		}
	}
	return best;
}

/* ------------------------------------------------------------- keys */

static int key_slot(World *w, const char *name)
{
	for (int i = 0; i < w->nkeys; i++)
		if (strcmp(w->keys[i].name, name) == 0)
			return i;
	w->keys = realloc(w->keys, sizeof *w->keys * (size_t)(w->nkeys + 1));
	w->keys[w->nkeys] = (KeyState){ 0 };
	snprintf(w->keys[w->nkeys].name, sizeof w->keys[w->nkeys].name, "%s", name);
	return w->nkeys++;
}

static void key_goes(int64_t id, const char *name, int down)
{
	World *w = world(id);
	char lower[16];
	key_name_of(name, lower, sizeof lower);
	int slot = key_slot(w, lower);   /* (first: it may move the keys) */
	KeyState *k = &w->keys[slot];
	if (down && !k->down) {
		k->down = 1;
		k->went_down = 1;
	} else if (!down && k->down) {
		k->down = 0;
		k->came_up = 1;
	}
}

static KeyState *key_now(int64_t id, const char *name)
{
	World *w = world(id);
	char lower[16];
	key_name_of(name, lower, sizeof lower);
	for (int i = 0; i < w->nkeys; i++)
		if (strcmp(w->keys[i].name, lower) == 0)
			return &w->keys[i];
	return NULL;
}

int cby_world_key_press(int64_t id, const char *name) { KeyState *k = key_now(id, name); return k && k->pressed; }
int cby_world_key_hold(int64_t id, const char *name) { KeyState *k = key_now(id, name); return k && (k->down || k->pressed); }
int cby_world_key_release(int64_t id, const char *name) { KeyState *k = key_now(id, name); return k && k->released; }

int cby_world_key_held_for(int64_t id, const char *name)
{
	KeyState *k = key_now(id, name);
	World *w = world(id);
	if (!k)
		return 0;
	long frames = k->released ? k->held : k->down ? w->frames - k->since : 0;
	return (int)(frames * 1000 / WORLD_FPS);
}

void cby_world_add_shortcut(int64_t id, const char *keys, void *obj, void *fn)
{
	World *w = world(id);
	char norm[48];
	if (!parse_keys(keys, norm, sizeof norm))
		fail("'%s' isn't a shortcut: write it like \"Ctrl+S\", \"P\" or \"F5\"", keys);
	w->shortcuts = realloc(w->shortcuts, sizeof *w->shortcuts * (size_t)(w->nshortcuts + 1));
	Shortcut *s = &w->shortcuts[w->nshortcuts++];
	snprintf(s->keys, sizeof s->keys, "%s", norm);
	s->h = (Handler){ obj, fn };
}

static int shortcut(int64_t id, const char *norm)
{
	World *w = world(id);
	for (int i = 0; i < w->nshortcuts; i++)
		if (strcmp(w->shortcuts[i].keys, norm) == 0) {
			invoke(w->shortcuts[i].h);
			return 1;
		}
	return 0;
}

/* ---- random numbers: its own generator, the same everywhere under a script */

static int scripted(void) { return getenv("CABBY_GUI_SCRIPT") != NULL; }
static uint64_t rng_state;

int cby_world_random(int from, int to)
{
	if (!rng_state) {
		rng_state = scripted() ? 0x853c49e6748fea9bull : ((uint64_t)SDL_GetTicksNS() ^ (uint64_t)(uintptr_t)&rng_state) | 1;
	}
	if (to < from) {
		int t = from;
		from = to;
		to = t;
	}
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	uint64_t span = (uint64_t)((int64_t)to - from) + 1;
	return (int)(from + (int64_t)(rng_state % span));
}

/* ------------------------------------------------------------- drawing: what each
 * renderer needs, worked out once a frame */

typedef struct Lit {
	V3 sun;               /* towards the sun */
	float sun_c[3], ambient[3];
	int nlamps;
	float lamp_pos[MAX_LAMPS][3], lamp_c[MAX_LAMPS][3], lamp_reach[MAX_LAMPS];
} Lit;

static void lighting(int64_t id, Lit *l)
{
	World *w = world(id);
	l->sun = v_mul(w->sun, -1);
	l->sun_c[0] = w->sun_color.r, l->sun_c[1] = w->sun_color.g, l->sun_c[2] = w->sun_color.b;
	l->ambient[0] = w->ambient.r, l->ambient[1] = w->ambient.g, l->ambient[2] = w->ambient.b;
	/* the lamps nearest the camera, if there are more than can be lit */
	int order[64], n = 0;
	for (int i = 0; i < nlamps && n < 64; i++)
		if (lamps[i].world == id && lamps[i].alive)
			order[n++] = i;
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0; j--) {
			Lamp *a = &lamps[order[j - 1]], *b = &lamps[order[j]];
			double da = (a->x - w->cx) * (a->x - w->cx) + (a->y - w->cy) * (a->y - w->cy) + (a->z - w->cz) * (a->z - w->cz);
			double db = (b->x - w->cx) * (b->x - w->cx) + (b->y - w->cy) * (b->y - w->cy) + (b->z - w->cz) * (b->z - w->cz);
			if (db >= da)
				break;
			int t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	l->nlamps = n < MAX_LAMPS ? n : MAX_LAMPS;
	for (int i = 0; i < l->nlamps; i++) {
		Lamp *a = &lamps[order[i]];
		l->lamp_pos[i][0] = (float)a->x, l->lamp_pos[i][1] = (float)a->y, l->lamp_pos[i][2] = (float)a->z;
		l->lamp_c[i][0] = a->color.r, l->lamp_c[i][1] = a->color.g, l->lamp_c[i][2] = a->color.b;
		l->lamp_reach[i] = (float)a->reach;
	}
}

/* how a picture repeats on a shape: u and v stretched (1, 1: once) */
static void tiling(const Shape *s, float *us, float *vs)
{
	*us = *vs = 1;
	if (s->tile <= 0)
		return;
	double k = s->scale;
	*us = (float)(s->w * k / s->tile);
	*vs = (float)((s->mesh == MESH_PLANE ? s->d : s->h) * k / s->tile);
}

/* the light falling on a point: what the shaders work out too, the same way */
static void light_at(const Lit *l, V3 p, V3 n, float out[3])
{
	float d = v_dot(n, l->sun);
	if (d < 0)
		d = 0;
	for (int k = 0; k < 3; k++)
		out[k] = l->ambient[k] + l->sun_c[k] * d;
	for (int i = 0; i < l->nlamps; i++) {
		V3 to = v_sub(v3(l->lamp_pos[i][0], l->lamp_pos[i][1], l->lamp_pos[i][2]), p);
		float dist = v_len(to);
		float fall = 1 - dist / l->lamp_reach[i];
		if (fall <= 0)
			continue;
		float facing_it = dist > 1e-6f ? v_dot(n, v_mul(to, 1 / dist)) : 1;
		if (facing_it <= 0)
			continue;
		float k2 = fall * fall * facing_it;
		for (int k = 0; k < 3; k++)
			out[k] += l->lamp_c[i][k] * k2;
	}
}

#include "world_soft.h"
#include "world_text.h"
#include "world_gl.h"

/* ------------------------------------------------------------- the window */

static SDL_Window *window;
static int64_t window_world;
static int use_gl;               /* drawn with OpenGL (else by world_soft.h) */
static SDL_GLContext gl_context;
static SDL_Texture *soft_texture;
static Frame soft;               /* (what world_soft.h draws into) */

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

static void world_retitle(int64_t id)
{
	if (window && window_world == id)
		SDL_SetWindowTitle(window, worlds[id - 1].title);
}

static void start_sdl(void)
{
	static int started;
	if (started)
		return;
	if (scripted())
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");   /* drawn, but not shown */
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "world: cannot open a window: %s\n", SDL_GetError());
		exit(1);
	}
	started = 1;
	set_idle_hook();
}

/* the window, with OpenGL if it can have it: 3.3 (a Mac's: 4.1, which has
 * 3.3's), four samples a pixel to smooth the edges if the card can */
static int open_gl_window(World *w)
{
	const char *want = getenv("CABBY_WORLD_DRAWING");
	if (want && strcmp(want, "cabby") == 0)
		return 0;
	for (int samples = 4; samples >= 0; samples -= 4) {
		SDL_GL_ResetAttributes();
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
		window = SDL_CreateWindow(w->title, w->width, w->height,
					  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
		if (!window)
			continue;
		gl_context = SDL_GL_CreateContext(window);
		if (gl_context && gl_start()) {
			SDL_GL_SetSwapInterval(1);
			return 1;
		}
		if (gl_context)
			SDL_GL_DestroyContext(gl_context);
		gl_context = NULL;
		SDL_DestroyWindow(window);
		window = NULL;
	}
	return 0;
}

/* the size drawn at, in pixels */
static void drawing_size(int *pw, int *ph)
{
	World *w = &worlds[window_world - 1];
	if (scripted()) {
		*pw = w->width;
		*ph = w->height;
		return;
	}
	if (use_gl)
		SDL_GetWindowSizeInPixels(window, pw, ph);
	else
		SDL_GetWindowSize(window, pw, ph);   /* (drawn by Cabby: at the window's size, not every pixel) */
	if (*pw < 1)
		*pw = 1;
	if (*ph < 1)
		*ph = 1;
}

static void draw_frame(int64_t id)
{
	World *w = world(id);
	camera_follows(w);
	int pw, ph;
	drawing_size(&pw, &ph);
	int pts_w = pw;
	if (!scripted())
		SDL_GetWindowSize(window, &pts_w, NULL);
	float k = pts_w > 0 ? (float)pw / (float)pts_w : 1;   /* (pixels for each of the window's) */
	if (use_gl) {
		gl_draw(id, pw, ph, k);
		SDL_GL_SwapWindow(window);
		return;
	}
	soft_draw(id, &soft, pw, ph);
	overlay_ready(id, pw, ph, k);
	overlay_onto(&soft);
	if (!renderer)
		return;
	if (!soft_texture || soft_texture->w != pw || soft_texture->h != ph) {
		if (soft_texture)
			SDL_DestroyTexture(soft_texture);
		soft_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, pw, ph);
	}
	if (soft_texture) {
		SDL_UpdateTexture(soft_texture, NULL, soft.rgba, pw * 4);
		SDL_RenderTexture(renderer, soft_texture, NULL, NULL);
	}
	SDL_RenderPresent(renderer);
}

/* Cabby's event loop, stepped once a frame (workers' on_done, processes) */
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

static void step(int64_t id)
{
	World *w = world(id);
	w->frames++;
	find_events();
	if (ev_step)
		ev_step(0);
	for (int i = 0; i < w->nkeys; i++) {
		KeyState *k = &w->keys[i];
		k->pressed = k->went_down;
		k->released = k->came_up;
		if (k->went_down)
			k->since = w->frames;
		if (k->came_up)
			k->held = w->frames - k->since;
		k->went_down = k->came_up = 0;
	}
	w->moved_x = w->moving_x;
	w->moved_y = w->moving_y;
	w->moving_x = w->moving_y = 0;
	for (int i = 0; i < nshapes; i++)
		if (shapes[i].world == id && shapes[i].alive && shapes[i].model >= 0)
			pose_step(&models[shapes[i].model], shapes[i].pose);
	invoke(w->frame);
	for (int i = 0; i < worlds[id - 1].ntimers && !worlds[id - 1].closing; i++) {
		Timer *t = &worlds[id - 1].timers[i];
		if (!t->alive || worlds[id - 1].frames < t->next)
			continue;
		Handler h = t->h;
		if (t->every)
			t->next += t->every;
		else
			t->alive = 0;
		invoke(h);
	}
	w = &worlds[id - 1];
	for (int i = 0; i < w->nkeys; i++)
		w->keys[i].pressed = w->keys[i].released = 0;
	camera_follows(w);   /* (where it is is known at once, not only when drawn) */
}

void cby_world_close(int64_t id)
{
	if (id >= 1 && id <= nworlds)
		worlds[id - 1].closing = 1;
}

static void key_named(int64_t id, const char *name, int ctrl, int shift, int alt, int down, int repeat)
{
	if (!name[0])
		return;
	if (!repeat)
		key_goes(id, name, down);
	if (!down || repeat)
		return;
	char norm[48];
	snprintf(norm, sizeof norm, "%s%s%s%s", ctrl ? "ctrl+" : "", shift ? "shift+" : "", alt ? "alt+" : "", name);
	if (!shortcut(id, norm) && shift) {
		snprintf(norm, sizeof norm, "%s%s%s", ctrl ? "ctrl+" : "", alt ? "alt+" : "", name);
		shortcut(id, norm);
	}
	World *w = world(id);
	if (w->keyed.fn) {
		snprintf(w->key, sizeof w->key, "%s", strcmp(name, "space") == 0 ? " " : name);
		invoke(w->keyed);
	}
}

static void key_event(int64_t id, SDL_Keycode code, SDL_Keymod mods, int down, int repeat)
{
	char name[16];
	sdl_key_name(code, name, sizeof name);
	key_named(id, name, (mods & SDL_KMOD_CTRL) != 0, (mods & SDL_KMOD_SHIFT) != 0, (mods & SDL_KMOD_ALT) != 0, down, repeat);
}

/* the mouse, in the window's points (as set_size counts them) */
static void mouse_event(int64_t id, float x, float y, int click)
{
	World *w = world(id);
	w->mouse_x = (int)floor(x);
	w->mouse_y = (int)floor(y);
	invoke(click ? w->clicked : w->moved);
}

/* the shape under the mouse (0: none) */
int64_t cby_world_shape_at_mouse(int64_t id)
{
	World *w = world(id);
	int ww = w->width, wh = w->height;
	if (window && !scripted())
		SDL_GetWindowSize(window, &ww, &wh);
	return pick(id, w->mouse_x, w->mouse_y, ww, wh);
}

void cby_world_lock_mouse(int64_t id, int on)
{
	World *w = world(id);
	w->locked = on != 0;
	if (window && !scripted())
		SDL_SetWindowRelativeMouseMode(window, w->locked);
}

int cby_world_mouse_locked(int64_t id) { return world(id)->locked; }

void cby_world_show_message(int64_t id, const char *text)
{
	(void)world(id);
	if (scripted())
		return;   /* (a test has no one to read it) */
	if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, worlds[id - 1].title, text, window))
		printf("%s\n", text);
}

static void run_script(int64_t id);

void cby_world_run(int64_t id)
{
	World *w = world(id);
	if (window)
		fail("%s", "a world is already showing: one world window at a time");
	start_sdl();
	window_world = id;
	use_gl = 0;
	const char *drawing = getenv("CABBY_WORLD_DRAWING");
	if (!scripted() || (drawing && strcmp(drawing, "opengl") == 0))
		use_gl = open_gl_window(w);   /* (a script asking for OpenGL: to compare the two) */
	if (!use_gl) {
		SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
		if (!make_window(w->title, w->width, w->height, flags, scripted(), &window, &renderer)) {
			fprintf(stderr, "world: cannot open a window: %s\n", SDL_GetError());
			exit(1);
		}
		if (!scripted())
			SDL_SetRenderVSync(renderer, 1);
	}
	w = &worlds[id - 1];
	if (w->locked && !scripted())
		SDL_SetWindowRelativeMouseMode(window, true);
	w->open = 1;
	w->closing = 0;
	if (scripted()) {
		run_script(id);
	} else {
		const Uint64 each = 1000000000ull / WORLD_FPS;
		Uint64 last = SDL_GetTicksNS(), owed = 0;
		while (!worlds[id - 1].closing) {
			SDL_Event e;
			while (SDL_PollEvent(&e)) {
				w = &worlds[id - 1];
				if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
					w->closing = 1;
				else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
					if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && w->locked) {
						w->locked = 0;   /* (Escape always lets the mouse go) */
						SDL_SetWindowRelativeMouseMode(window, false);
					}
					key_event(id, e.key.key, e.key.mod, e.type == SDL_EVENT_KEY_DOWN, e.key.repeat);
				} else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
					mouse_event(id, e.button.x, e.button.y, 1);
				else if (e.type == SDL_EVENT_MOUSE_MOTION) {
					w->moving_x += e.motion.xrel;
					w->moving_y += e.motion.yrel;
					if (w->moved.fn)
						mouse_event(id, e.motion.x, e.motion.y, 0);
					else
						w->mouse_x = (int)e.motion.x, w->mouse_y = (int)e.motion.y;
				}
			}
			Uint64 now = SDL_GetTicksNS();
			owed += now - last;
			last = now;
			if (owed > 250000000ull)
				owed = 250000000ull;
			while (owed >= each && !worlds[id - 1].closing) {
				owed -= each;
				if (!worlds[id - 1].paused)
					step(id);
			}
			if (worlds[id - 1].closing)
				break;
			draw_frame(id);
			Uint64 spent = SDL_GetTicksNS() - now;
			if (spent < each / 2)
				SDL_DelayNS(each / 2 - spent);
		}
	}
	worlds[id - 1].open = 0;
	if (use_gl) {
		gl_stop();
		SDL_GL_DestroyContext(gl_context);
		gl_context = NULL;
	} else {
		if (soft_texture)
			SDL_DestroyTexture(soft_texture);
		soft_texture = NULL;
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}
	SDL_DestroyWindow(window);
	window = NULL;
	window_world = 0;
	for (int i = 0; i < 10; i++) {
		SDL_PumpEvents();
		SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
		SDL_Delay(5);
	}
	fflush(stdout);
}

/* ------------------------------------------------------------- test scripts
 *
 * CABBY_GUI_SCRIPT, as for game: `;` between commands; frames run only when
 * the script says. The world is drawn by Cabby (world_soft.h), the same on
 * every computer. */

static char *script_left;
static char *script_all;

static void run_script_steps(int64_t id);

static void run_script(int64_t id)
{
	static int loaded;
	if (!loaded) {
		loaded = 1;
		script_left = script_all = strdup(getenv("CABBY_GUI_SCRIPT"));
	}
	run_script_steps(id);
	if (!script_left || !*script_left) {
		free(script_all);
		script_all = script_left = NULL;
	}
}

/* the frame as it is now, drawn the way the window draws it */
static void frame_now(int64_t id)
{
	World *w = world(id);
	camera_follows(w);
	if (use_gl) {
		gl_draw(id, w->width, w->height, 1);
		gl_read(&soft, w->width, w->height);
	} else {
		soft_draw(id, &soft, w->width, w->height);
		overlay_ready(id, w->width, w->height, 1);
		overlay_onto(&soft);
	}
}

static int save_frame(int64_t id, const char *path)
{
	World *w = world(id);
	frame_now(id);
	return stbi_write_png(path, w->width, w->height, 4, soft.rgba, w->width * 4);
}

static void run_script_steps(int64_t id)
{
	while (script_left && *script_left && !worlds[id - 1].closing) {
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
		World *w = world(id);
		if (strcmp(cmd, "processes") == 0) {
			find_events();
			int fds[64];
			while (ev_fds && ev_fds(fds, 64) > 0)
				ev_step(50);
		} else if (strncmp(cmd, "frames ", 7) == 0) {
			frames = atoi(cmd + 7);
		} else if (strncmp(cmd, "wait ", 5) == 0) {
			frames = (int)((long)atoi(cmd + 5) * WORLD_FPS / 1000);
		} else if (strncmp(cmd, "hold ", 5) == 0) {
			key_goes(id, cmd + 5, 1);
		} else if (strncmp(cmd, "release ", 8) == 0) {
			key_goes(id, cmd + 8, 0);
		} else if (strncmp(cmd, "tap ", 4) == 0) {
			key_goes(id, cmd + 4, 1);
			if (!w->paused)
				step(id);
			key_goes(id, cmd + 4, 0);
			frames = 1;
		} else if (strncmp(cmd, "key ", 4) == 0) {
			if (w->keyed.fn) {
				snprintf(w->key, sizeof w->key, "%s", strcmp(cmd + 4, "space") == 0 ? " " : cmd + 4);
				invoke(w->keyed);
			}
		} else if (strncmp(cmd, "shortcut ", 9) == 0) {
			char norm[48];
			if (!parse_keys(cmd + 9, norm, sizeof norm) || !shortcut(id, norm))
				fprintf(stderr, "world script: no shortcut '%s'\n", cmd + 9);
		} else if (strncmp(cmd, "press ", 6) == 0 || strncmp(cmd, "point ", 6) == 0) {
			double x = 0, y = 0;
			if (sscanf(cmd + 6, "%lf %lf", &x, &y) == 2)
				mouse_event(id, (float)x, (float)y, cmd[1] == 'r');
			else
				fprintf(stderr, "world script: '%s' needs a place in the window: press 120 80\n", cmd);
		} else if (strncmp(cmd, "look ", 5) == 0) {
			/* look <dx> <dy>: the mouse moved that far (as a locked mouse is read) */
			double x = 0, y = 0;
			if (sscanf(cmd + 5, "%lf %lf", &x, &y) == 2) {
				w->moving_x += x;
				w->moving_y += y;
			}
		} else if (strncmp(cmd, "shot ", 5) == 0) {
			if (!save_frame(id, cmd + 5))
				fprintf(stderr, "world script: could not save %s\n", cmd + 5);
		} else if (strncmp(cmd, "pixel ", 6) == 0) {
			int x = 0, y = 0;
			if (sscanf(cmd + 6, "%d %d", &x, &y) == 2 && x >= 0 && y >= 0 && x < w->width && y < w->height) {
				frame_now(id);
				const unsigned char *p = soft.rgba + ((size_t)y * w->width + x) * 4;
				printf("pixel %d %d: #%02x%02x%02x\n", x, y, p[0], p[1], p[2]);
				fflush(stdout);
			} else {
				fprintf(stderr, "world script: '%s' needs a place in the window\n", cmd);
			}
		} else if (strcmp(cmd, "close") == 0) {
			w->closing = 1;
		} else if (*cmd) {
			fprintf(stderr, "world script: unknown command '%s'\n", cmd);
		}
		for (int i = 0; i < frames && !worlds[id - 1].closing; i++)
			if (!worlds[id - 1].paused)
				step(id);
	}
}
