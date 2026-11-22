/* world_models.h — models made in Blender (or any program that writes
 * glTF 2.0: `.glb`, or `.gltf` with its `.bin` and pictures), included by
 * world.c. Read with cgltf: each mesh's triangles, its materials' colors
 * and pictures, the nodes they hang from, the skeleton a character's skin
 * bends with, and the animations that move them.
 *
 * A model is read once, and each shape made from it has its own pose: the
 * animation it's playing, and how far through. Each frame, the nodes are
 * moved as the animation says, and a skinned mesh (a character) is bent to
 * them here, on the processor, the same for both kinds of drawing. A glTF
 * model faces +z; the world's shapes face -z, so it's turned round to
 * face the way a shape does. */

#define CGLTF_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "third_party/cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* every mesh made, for OpenGL to let go of (built-in shapes' and models') */
static Mesh **all_meshes;
static int nall_meshes;

static Mesh *mesh_new(void)
{
	Mesh *m = calloc(1, sizeof *m);
	all_meshes = realloc(all_meshes, sizeof *all_meshes * (size_t)(nall_meshes + 1));
	all_meshes[nall_meshes++] = m;
	return m;
}

typedef struct Prim {
	int node;             /* the node it hangs from */
	Mesh *mesh;           /* its triangles, at rest */
	float color[4];       /* its material's color */
	int picture;          /* its material's picture (-1: none) */
	int two_sided;
	/* bent by a skin: each vertex's joints and how much each pulls */
	int skin;             /* (-1: none) */
	uint16_t *joints;
	float *weights;
} Prim;

typedef struct Channel {
	int node, path;       /* path: 0 move (translation), 1 turn (rotation), 2 size (scale) */
	int step;             /* jumps, not smooth */
	int cubic;            /* (cubic spline: its values come in threes, the middle the value) */
	int n;                /* keyframes */
	float *times, *values;
} Channel;

typedef struct Anim {
	char *name;
	float length;         /* seconds */
	Channel *ch;
	int nch;
} Anim;

typedef struct Skin {
	int *joints;          /* nodes */
	float *inverse;       /* 16 floats each */
	int n;
} Skin;

typedef struct Model {
	char *path;
	int nnodes;
	int *parent;          /* -1: a root */
	float (*t)[3], (*r)[4], (*s)[3];   /* each node at rest: moved, turned (a quaternion), sized */
	float (*matrix)[16];  /* a node given as a matrix instead (has_matrix) */
	int *has_matrix;
	int *in_scene;
	Prim *prims;
	int nprims;
	Skin *skins;
	int nskins;
	Anim *anims;
	int nanims;
	V3 lo, hi;            /* the box round it at rest, turned to face -z */
} Model;

static Model *models;
static int nmodels;

/* a shape made from a model: its own pose, and its own bent meshes */
typedef struct Pose {
	int anim;             /* -1: at rest */
	double time, speed;
	int loop, done;
	float (*t)[3], (*r)[4], (*s)[3];
	float (*world)[16];   /* each node's matrix in the model, this frame */
	Mesh **bent;          /* by prim: its skinned mesh (NULL: not skinned) */
	int posed;            /* the frame it was posed for (-1: needs posing) */
} Pose;

/* ---- quaternions and matrices for nodes */

static void quat_slerp(const float *a, const float *b0, float t, float *out)
{
	float b[4] = { b0[0], b0[1], b0[2], b0[3] };
	float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	if (d < 0) {
		for (int k = 0; k < 4; k++)
			b[k] = -b[k];
		d = -d;
	}
	float wa, wb;
	if (d > 0.9995f) {
		wa = 1 - t;
		wb = t;
	} else {
		float th = acosf(d), s = sinf(th);
		wa = sinf((1 - t) * th) / s;
		wb = sinf(t * th) / s;
	}
	float len = 0;
	for (int k = 0; k < 4; k++) {
		out[k] = a[k] * wa + b[k] * wb;
		len += out[k] * out[k];
	}
	len = sqrtf(len);
	if (len > 0)
		for (int k = 0; k < 4; k++)
			out[k] /= len;
}

static M4 m_trs(const float *t, const float *q, const float *s)
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	M4 m = m_identity();
	m.m[0] = (1 - 2 * (y * y + z * z)) * s[0];
	m.m[1] = (2 * (x * y + z * w)) * s[0];
	m.m[2] = (2 * (x * z - y * w)) * s[0];
	m.m[4] = (2 * (x * y - z * w)) * s[1];
	m.m[5] = (1 - 2 * (x * x + z * z)) * s[1];
	m.m[6] = (2 * (y * z + x * w)) * s[1];
	m.m[8] = (2 * (x * z + y * w)) * s[2];
	m.m[9] = (2 * (y * z - x * w)) * s[2];
	m.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
	m.m[12] = t[0];
	m.m[13] = t[1];
	m.m[14] = t[2];
	return m;
}

/* ---- reading */

static int picture_from_rgba(const char *key, int w, int h, unsigned char *rgba)
{
	for (int i = 0; i < npictures; i++)
		if (strcmp(pictures[i].path, key) == 0) {
			stbi_image_free(rgba);
			return i;
		}
	Picture p = { strdup(key), w, h, rgba, NULL, NULL, 0, 0, w, h };
	pictures = realloc(pictures, sizeof *pictures * (size_t)(npictures + 1));
	pictures[npictures] = p;
	return npictures++;
}

/* pictures to be drawn sharp, not smoothed (the model's sampler says
 * "nearest": pixel art) */
static unsigned char *picture_sharp;
static int npicture_sharp;

static void set_sharp(int pic)
{
	if (pic >= npicture_sharp) {
		picture_sharp = realloc(picture_sharp, (size_t)(pic + 1));
		memset(picture_sharp + npicture_sharp, 0, (size_t)(pic + 1 - npicture_sharp));
		npicture_sharp = pic + 1;
	}
	picture_sharp[pic] = 1;
}

static int is_sharp(int pic) { return pic >= 0 && pic < npicture_sharp && picture_sharp[pic]; }

/* a material's picture: in the file (a .glb's), or a file beside it */
static int model_picture(const char *path, cgltf_data *d, cgltf_texture *t)
{
	if (!t || !t->image)
		return -1;
	cgltf_image *im = t->image;
	char key[1200];
	snprintf(key, sizeof key, "%s#%d", path, (int)(im - d->images));
	for (int i = 0; i < npictures; i++)
		if (strcmp(pictures[i].path, key) == 0)
			return i;
	int w = 0, h = 0, n = 0;
	unsigned char *rgba = NULL;
	if (im->buffer_view) {
		const unsigned char *bytes = (const unsigned char *)im->buffer_view->buffer->data + im->buffer_view->offset;
		rgba = stbi_load_from_memory(bytes, (int)im->buffer_view->size, &w, &h, &n, 4);
	} else if (im->uri && strncmp(im->uri, "data:", 5) != 0) {
		char file[1200];
		const char *slash = strrchr(path, '/');
		snprintf(file, sizeof file, "%.*s%s", slash ? (int)(slash - path + 1) : 0, path, im->uri);
		cgltf_decode_uri(file + (slash ? (int)(slash - path + 1) : 0));
		rgba = stbi_load(file, &w, &h, &n, 4);
	} else if (im->uri) {
		/* data:image/png;base64,... */
		const char *comma = strchr(im->uri, ',');
		if (comma) {
			size_t len = strlen(comma + 1) * 3 / 4;
			void *raw = NULL;
			cgltf_options o = { 0 };
			if (cgltf_load_buffer_base64(&o, len, comma + 1, &raw) == cgltf_result_success) {
				rgba = stbi_load_from_memory(raw, (int)len, &w, &h, &n, 4);
				free(raw);
			}
		}
	}
	if (!rgba) {
		/* (a picture stb_image can't read: the color alone, and said once) */
		static char warned[1200];
		if (strcmp(warned, path) != 0) {
			snprintf(warned, sizeof warned, "%s", path);
			fprintf(stderr, "world: the model '%s' has a picture Cabby can't read%s%s%s: its colors are shown instead "
				"(export its pictures as PNG or JPEG)\n", path, im->mime_type ? " (" : "",
				im->mime_type ? im->mime_type : "", im->mime_type ? ")" : "");
		}
		return -1;
	}
	int pic = picture_from_rgba(key, w, h, rgba);
	if (t->sampler && t->sampler->mag_filter == 9728)   /* (GL_NEAREST) */
		set_sharp(pic);
	return pic;
}

static int node_index(cgltf_data *d, cgltf_node *n) { return n ? (int)(n - d->nodes) : -1; }

static void mark_scene(cgltf_data *d, Model *m, cgltf_node *n)
{
	m->in_scene[node_index(d, n)] = 1;
	for (size_t i = 0; i < n->children_count; i++)
		mark_scene(d, m, n->children[i]);
}

/* the nodes' matrices in the model, from their moves, turns and sizes */
static void model_world(const Model *m, float (*t)[3], float (*r)[4], float (*s)[3], float (*out)[16])
{
	/* (parents come before children in no particular order: done until all are) */
	char *done = calloc((size_t)m->nnodes, 1);
	int left = m->nnodes;
	while (left > 0) {
		int progress = 0;
		for (int i = 0; i < m->nnodes; i++) {
			if (done[i] || (m->parent[i] >= 0 && !done[m->parent[i]]))
				continue;
			M4 local;
			if (m->has_matrix[i])
				memcpy(local.m, m->matrix[i], sizeof local.m);
			else
				local = m_trs(t[i], r[i], s[i]);
			M4 w = local;
			if (m->parent[i] >= 0) {
				M4 p;
				memcpy(p.m, out[m->parent[i]], sizeof p.m);
				w = m_mul(p, local);
			}
			memcpy(out[i], w.m, sizeof w.m);
			done[i] = 1;
			left--;
			progress = 1;
		}
		if (!progress)
			break;   /* (a loop in the file: left as they are) */
	}
	free(done);
}

/* the model turned to face -z, as the world's shapes do */
static M4 model_facing(void) { return m_rotate_y((float)M_PI); }

static void skin_mesh(const Model *m, const Prim *p, float (*world)[16], Mesh *out);

static int model_load(const char *path)
{
	for (int i = 0; i < nmodels; i++)
		if (strcmp(models[i].path, path) == 0)
			return i;
	cgltf_options opt = { 0 };
	cgltf_data *d = NULL;
	cgltf_result r = cgltf_parse_file(&opt, path, &d);
	if (r == cgltf_result_file_not_found || r == cgltf_result_io_error)
		fail("can't read the model '%s': it isn't there", path);
	if (r != cgltf_result_success)
		fail("can't read the model '%s': it isn't a glTF model (a .glb or .gltf file, as Blender exports)", path);
	/* made in a way that can't be read (squeezed small, or with pictures
	 * stb_image doesn't know): say so, and how to make it again */
	for (cgltf_size i = 0; i < d->extensions_required_count; i++) {
		const char *e = d->extensions_required[i];
		const char *what = strcmp(e, "KHR_draco_mesh_compression") == 0 ? "Draco compression"
				   : strncmp(e, "EXT_meshopt", 11) == 0 || strncmp(e, "KHR_meshopt", 11) == 0 ? "meshopt compression"
				   : strcmp(e, "KHR_texture_basisu") == 0 ? "KTX2 (Basis) pictures"
				   : strcmp(e, "EXT_texture_webp") == 0 ? "WebP pictures"
				   : NULL;
		if (what) {
			char why[1600];
			snprintf(why, sizeof why,
				 "can't read the model '%s': it's made with %s, which Cabby doesn't read. Export it again without "
				 "(in Blender: File > Export > glTF 2.0, compression off, and pictures as PNG or JPEG)",
				 path, what);
			cgltf_free(d);
			fail("%s", why);
		}
	}
	if (cgltf_load_buffers(&opt, d, path) != cgltf_result_success) {
		cgltf_free(d);
		fail("can't read the model '%s': its .bin file (beside it) is missing", path);
	}
	Model m = { .path = strdup(path) };
	m.nnodes = (int)d->nodes_count;
	int nn = m.nnodes > 0 ? m.nnodes : 1;
	m.parent = malloc(sizeof *m.parent * (size_t)nn);
	m.t = malloc(sizeof *m.t * (size_t)nn);
	m.r = malloc(sizeof *m.r * (size_t)nn);
	m.s = malloc(sizeof *m.s * (size_t)nn);
	m.matrix = malloc(sizeof *m.matrix * (size_t)nn);
	m.has_matrix = calloc((size_t)nn, sizeof *m.has_matrix);
	m.in_scene = calloc((size_t)nn, sizeof *m.in_scene);
	for (int i = 0; i < m.nnodes; i++) {
		cgltf_node *n = &d->nodes[i];
		m.parent[i] = node_index(d, n->parent);
		float t0[3] = { 0, 0, 0 }, r0[4] = { 0, 0, 0, 1 }, s0[3] = { 1, 1, 1 };
		memcpy(m.t[i], n->has_translation ? n->translation : t0, sizeof m.t[i]);
		memcpy(m.r[i], n->has_rotation ? n->rotation : r0, sizeof m.r[i]);
		memcpy(m.s[i], n->has_scale ? n->scale : s0, sizeof m.s[i]);
		m.has_matrix[i] = n->has_matrix;
		memcpy(m.matrix[i], n->matrix, sizeof m.matrix[i]);
	}
	cgltf_scene *scene = d->scene ? d->scene : d->scenes_count ? &d->scenes[0] : NULL;
	if (scene) {
		for (size_t i = 0; i < scene->nodes_count; i++)
			mark_scene(d, &m, scene->nodes[i]);
	} else {
		for (int i = 0; i < m.nnodes; i++)
			m.in_scene[i] = 1;
	}
	/* skins */
	m.nskins = (int)d->skins_count;
	m.skins = calloc((size_t)(m.nskins ? m.nskins : 1), sizeof *m.skins);
	for (int i = 0; i < m.nskins; i++) {
		cgltf_skin *s = &d->skins[i];
		Skin *k = &m.skins[i];
		k->n = (int)s->joints_count;
		k->joints = malloc(sizeof *k->joints * (size_t)(k->n ? k->n : 1));
		k->inverse = malloc(sizeof *k->inverse * 16 * (size_t)(k->n ? k->n : 1));
		for (int j = 0; j < k->n; j++) {
			k->joints[j] = node_index(d, s->joints[j]);
			M4 id = m_identity();
			if (s->inverse_bind_matrices)
				cgltf_accessor_read_float(s->inverse_bind_matrices, (cgltf_size)j, k->inverse + 16 * j, 16);
			else
				memcpy(k->inverse + 16 * j, id.m, sizeof id.m);
		}
	}
	/* the meshes, as each node that has one uses it */
	for (int ni = 0; ni < m.nnodes; ni++) {
		cgltf_node *node = &d->nodes[ni];
		if (!node->mesh || !m.in_scene[ni])
			continue;
		for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
			cgltf_primitive *pr = &node->mesh->primitives[pi];
			if (pr->type != cgltf_primitive_type_triangles)
				continue;   /* (lines and points: not drawn) */
			cgltf_accessor *pos = NULL, *nor = NULL, *uv = NULL, *col = NULL, *jo = NULL, *we = NULL;
			for (size_t a = 0; a < pr->attributes_count; a++) {
				cgltf_attribute *at = &pr->attributes[a];
				if (at->type == cgltf_attribute_type_position)
					pos = at->data;
				else if (at->type == cgltf_attribute_type_normal)
					nor = at->data;
				else if (at->type == cgltf_attribute_type_texcoord && at->index == 0)
					uv = at->data;
				else if (at->type == cgltf_attribute_type_color && at->index == 0)
					col = at->data;
				else if (at->type == cgltf_attribute_type_joints && at->index == 0)
					jo = at->data;
				else if (at->type == cgltf_attribute_type_weights && at->index == 0)
					we = at->data;
			}
			if (!pos || !pos->count)
				continue;
			Prim p = { .node = ni, .mesh = mesh_new(), .color = { 1, 1, 1, 1 }, .picture = -1, .skin = -1 };
			Mesh *me = p.mesh;
			int nv = (int)pos->count;
			me->v = calloc((size_t)nv * VERT, sizeof *me->v);
			me->nv = nv;
			for (int v = 0; v < nv; v++) {
				float *o = me->v + VERT * v;
				cgltf_accessor_read_float(pos, (cgltf_size)v, o, 3);
				if (nor)
					cgltf_accessor_read_float(nor, (cgltf_size)v, o + 3, 3);
				if (uv)
					cgltf_accessor_read_float(uv, (cgltf_size)v, o + 6, 2);
			}
			if (pr->indices) {
				me->ni = (int)pr->indices->count;
				me->ix = malloc(sizeof *me->ix * (size_t)(me->ni ? me->ni : 1));
				for (int k = 0; k < me->ni; k++)
					me->ix[k] = (uint32_t)cgltf_accessor_read_index(pr->indices, (cgltf_size)k);
			} else {
				me->ni = nv - nv % 3;
				me->ix = malloc(sizeof *me->ix * (size_t)(me->ni ? me->ni : 1));
				for (int k = 0; k < me->ni; k++)
					me->ix[k] = (uint32_t)k;
			}
			for (int k = 0; k < me->ni; k++)
				if (me->ix[k] >= (uint32_t)nv)
					me->ix[k] = 0;   /* (a broken file: not past the end) */
			if (!nor) {
				/* no normals: each vertex's from the triangles round it */
				for (int k = 0; k + 2 < me->ni; k += 3) {
					float *a = me->v + VERT * me->ix[k], *b = me->v + VERT * me->ix[k + 1], *c = me->v + VERT * me->ix[k + 2];
					V3 n = v_cross(v_sub(v3(b[0], b[1], b[2]), v3(a[0], a[1], a[2])), v_sub(v3(c[0], c[1], c[2]), v3(a[0], a[1], a[2])));
					for (int q = 0; q < 3; q++) {
						float *o = me->v + VERT * me->ix[k + q];
						o[3] += n.x, o[4] += n.y, o[5] += n.z;
					}
				}
				for (int v = 0; v < nv; v++) {
					float *o = me->v + VERT * v;
					V3 n = v_norm(v3(o[3], o[4], o[5]));
					o[3] = n.x, o[4] = n.y, o[5] = n.z;
				}
			}
			/* vertex colors (Blender's color attribute): kept by taking their
			 * average into the color (a mesh painted one color, as most are) */
			if (col) {
				double sum[3] = { 0, 0, 0 };
				for (int v = 0; v < nv; v++) {
					float c4[4] = { 1, 1, 1, 1 };
					cgltf_accessor_read_float(col, (cgltf_size)v, c4, col->type == cgltf_type_vec3 ? 3 : 4);
					for (int k = 0; k < 3; k++)
						sum[k] += c4[k];
				}
				for (int k = 0; k < 3; k++)
					p.color[k] = (float)(sum[k] / nv);
			}
			cgltf_material *mat = pr->material;
			if (mat) {
				if (mat->has_pbr_metallic_roughness) {
					for (int k = 0; k < 4; k++)
						p.color[k] *= mat->pbr_metallic_roughness.base_color_factor[k];
					p.picture = model_picture(path, d, mat->pbr_metallic_roughness.base_color_texture.texture);
				}
				p.two_sided = mat->double_sided;
			}
			if (node->skin && jo && we) {
				p.skin = (int)(node->skin - d->skins);
				p.joints = calloc((size_t)nv * 4, sizeof *p.joints);
				p.weights = calloc((size_t)nv * 4, sizeof *p.weights);
				for (int v = 0; v < nv; v++) {
					cgltf_uint j4[4] = { 0, 0, 0, 0 };
					cgltf_accessor_read_uint(jo, (cgltf_size)v, j4, 4);
					cgltf_accessor_read_float(we, (cgltf_size)v, p.weights + 4 * v, 4);
					for (int k = 0; k < 4; k++)
						p.joints[4 * v + k] = (uint16_t)(j4[k] < (cgltf_uint)m.skins[p.skin].n ? j4[k] : 0);
				}
			}
			m.prims = realloc(m.prims, sizeof *m.prims * (size_t)(m.nprims + 1));
			m.prims[m.nprims++] = p;
		}
	}
	/* animations */
	m.nanims = (int)d->animations_count;
	m.anims = calloc((size_t)(m.nanims ? m.nanims : 1), sizeof *m.anims);
	for (int i = 0; i < m.nanims; i++) {
		cgltf_animation *a = &d->animations[i];
		Anim *an = &m.anims[i];
		char name[32];
		snprintf(name, sizeof name, "animation %d", i + 1);
		an->name = strdup(a->name && *a->name ? a->name : name);
		for (size_t c = 0; c < a->channels_count; c++) {
			cgltf_animation_channel *ch = &a->channels[c];
			int path_kind = ch->target_path == cgltf_animation_path_type_translation ? 0
				      : ch->target_path == cgltf_animation_path_type_rotation   ? 1
				      : ch->target_path == cgltf_animation_path_type_scale      ? 2 : -1;
			if (path_kind < 0 || !ch->target_node || !ch->sampler)
				continue;   /* (morph weights: not yet) */
			cgltf_animation_sampler *sa = ch->sampler;
			Channel k = { node_index(d, ch->target_node), path_kind, sa->interpolation == cgltf_interpolation_type_step,
				      sa->interpolation == cgltf_interpolation_type_cubic_spline, (int)sa->input->count, NULL, NULL };
			int comps = path_kind == 1 ? 4 : 3;
			k.times = malloc(sizeof *k.times * (size_t)(k.n ? k.n : 1));
			k.values = malloc(sizeof *k.values * (size_t)comps * (size_t)(k.n ? k.n : 1));
			for (int f = 0; f < k.n; f++) {
				cgltf_accessor_read_float(sa->input, (cgltf_size)f, &k.times[f], 1);
				/* (a cubic spline's: in, value, out; the value is the middle) */
				cgltf_size at = k.cubic ? (cgltf_size)(3 * f + 1) : (cgltf_size)f;
				cgltf_accessor_read_float(sa->output, at, &k.values[comps * f], (cgltf_size)comps);
				if (k.times[f] > an->length)
					an->length = k.times[f];
			}
			an->ch = realloc(an->ch, sizeof *an->ch * (size_t)(an->nch + 1));
			an->ch[an->nch++] = k;
		}
	}
	cgltf_free(d);
	/* the box round it, at rest */
	float (*world)[16] = malloc(sizeof *world * (size_t)nn);
	model_world(&m, m.t, m.r, m.s, world);
	M4 face = model_facing();
	m.lo = v3(1e30f, 1e30f, 1e30f);
	m.hi = v3(-1e30f, -1e30f, -1e30f);
	for (int i = 0; i < m.nprims; i++) {
		Prim *p = &m.prims[i];
		Mesh tmp = { 0 };
		const Mesh *src = p->mesh;
		M4 place = face;
		if (p->skin >= 0) {
			skin_mesh(&m, p, world, &tmp);
			src = &tmp;
		} else {
			M4 nw;
			memcpy(nw.m, world[p->node], sizeof nw.m);
			place = m_mul(face, nw);
		}
		for (int v = 0; v < src->nv; v++) {
			const float *o = src->v + VERT * v;
			V3 q = m_point(&place, v3(o[0], o[1], o[2]));
			m.lo = v3(fminf(m.lo.x, q.x), fminf(m.lo.y, q.y), fminf(m.lo.z, q.z));
			m.hi = v3(fmaxf(m.hi.x, q.x), fmaxf(m.hi.y, q.y), fmaxf(m.hi.z, q.z));
		}
		free(tmp.v);
		mesh_bounds(p->mesh);
	}
	free(world);
	if (m.lo.x > m.hi.x)
		m.lo = m.hi = v3(0, 0, 0);   /* (nothing to draw in it) */
	models = realloc(models, sizeof *models * (size_t)(nmodels + 1));
	models[nmodels] = m;
	return nmodels++;
}

/* a skinned mesh bent to the joints as they are now: its vertices, moved
 * as the joints that pull them are, in proportion (into out, which gets
 * the same vertices' places and normals, the rest copied) */
static void skin_mesh(const Model *m, const Prim *p, float (*world)[16], Mesh *out)
{
	const Skin *k = &m->skins[p->skin];
	const Mesh *src = p->mesh;
	if (!out->v || out->nv != src->nv) {
		free(out->v);
		out->v = malloc(sizeof *out->v * VERT * (size_t)src->nv);
		out->nv = src->nv;
	}
	M4 *jm = malloc(sizeof *jm * (size_t)(k->n ? k->n : 1));
	for (int j = 0; j < k->n; j++) {
		M4 w, inv;
		memcpy(w.m, world[k->joints[j] >= 0 ? k->joints[j] : 0], sizeof w.m);
		memcpy(inv.m, k->inverse + 16 * j, sizeof inv.m);
		jm[j] = m_mul(w, inv);
	}
	for (int v = 0; v < src->nv; v++) {
		const float *i = src->v + VERT * v;
		float *o = out->v + VERT * v;
		M4 b = { { 0 } };
		float total = 0;
		for (int q = 0; q < 4; q++) {
			float wt = p->weights[4 * v + q];
			if (wt == 0)
				continue;
			const M4 *a = &jm[p->joints[4 * v + q]];
			for (int e = 0; e < 16; e++)
				b.m[e] += a->m[e] * wt;
			total += wt;
		}
		if (total == 0)
			b = m_identity();
		V3 pt = m_point(&b, v3(i[0], i[1], i[2]));
		/* (normals turned by the same matrix: right enough unless a joint is
		 * stretched unevenly) */
		V3 n = v_norm(v3(b.m[0] * i[3] + b.m[4] * i[4] + b.m[8] * i[5], b.m[1] * i[3] + b.m[5] * i[4] + b.m[9] * i[5],
				 b.m[2] * i[3] + b.m[6] * i[4] + b.m[10] * i[5]));
		o[0] = pt.x, o[1] = pt.y, o[2] = pt.z;
		o[3] = n.x, o[4] = n.y, o[5] = n.z;
		o[6] = i[6], o[7] = i[7];
	}
	free(jm);
}

/* ---- a shape's pose */

static Pose *pose_new(const Model *m)
{
	Pose *p = calloc(1, sizeof *p);
	int nn = m->nnodes > 0 ? m->nnodes : 1;
	p->anim = -1;
	p->speed = 1;
	p->t = malloc(sizeof *p->t * (size_t)nn);
	p->r = malloc(sizeof *p->r * (size_t)nn);
	p->s = malloc(sizeof *p->s * (size_t)nn);
	p->world = malloc(sizeof *p->world * (size_t)nn);
	p->bent = calloc((size_t)(m->nprims ? m->nprims : 1), sizeof *p->bent);
	p->posed = -1;
	return p;
}

/* where an animation's channel is at time t */
static void channel_at(const Channel *c, float t, float *out)
{
	int comps = c->path == 1 ? 4 : 3;
	if (c->n == 0)
		return;
	if (t <= c->times[0] || c->n == 1) {
		memcpy(out, c->values, sizeof *out * (size_t)comps);
		return;
	}
	if (t >= c->times[c->n - 1]) {
		memcpy(out, c->values + comps * (c->n - 1), sizeof *out * (size_t)comps);
		return;
	}
	int lo = 0, hi = c->n - 1;
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		if (c->times[mid] <= t)
			lo = mid;
		else
			hi = mid;
	}
	const float *a = c->values + comps * lo, *b = c->values + comps * hi;
	float span = c->times[hi] - c->times[lo];
	float u = span > 0 ? (t - c->times[lo]) / span : 0;
	if (c->step) {
		memcpy(out, a, sizeof *out * (size_t)comps);
	} else if (c->path == 1) {
		quat_slerp(a, b, u, out);
	} else {
		for (int k = 0; k < comps; k++)
			out[k] = a[k] + (b[k] - a[k]) * u;
	}
}

/* the nodes, moved as the animation says now; skinned meshes bent */
static void pose_update(const Model *m, Pose *p)
{
	memcpy(p->t, m->t, sizeof *p->t * (size_t)m->nnodes);
	memcpy(p->r, m->r, sizeof *p->r * (size_t)m->nnodes);
	memcpy(p->s, m->s, sizeof *p->s * (size_t)m->nnodes);
	if (p->anim >= 0 && p->anim < m->nanims) {
		const Anim *a = &m->anims[p->anim];
		float t = (float)p->time;
		for (int i = 0; i < a->nch; i++) {
			const Channel *c = &a->ch[i];
			if (c->node < 0 || c->node >= m->nnodes)
				continue;
			channel_at(c, t, c->path == 0 ? p->t[c->node] : c->path == 1 ? p->r[c->node] : p->s[c->node]);
		}
	}
	model_world(m, p->t, p->r, p->s, p->world);
	for (int i = 0; i < m->nprims; i++) {
		const Prim *pr = &m->prims[i];
		if (pr->skin < 0)
			continue;
		if (!p->bent[i]) {
			p->bent[i] = mesh_new();
			p->bent[i]->ix = pr->mesh->ix;   /* (the same triangles: shared) */
			p->bent[i]->ni = pr->mesh->ni;
			p->bent[i]->shared_ix = 1;
			p->bent[i]->two_sided = pr->mesh->two_sided;
		}
		skin_mesh(m, pr, p->world, p->bent[i]);
		p->bent[i]->changed = 1;
	}
}

/* time going on, a frame's worth: looped, or to its end and stopped there */
static void pose_step(const Model *m, Pose *p)
{
	if (p->anim < 0 || p->anim >= m->nanims || p->done)
		return;
	double len = m->anims[p->anim].length;
	p->time += p->speed / WORLD_FPS;
	if (len <= 0) {
		p->time = 0;
		return;
	}
	if (p->loop) {
		p->time = fmod(p->time, len);
		if (p->time < 0)
			p->time += len;
	} else if (p->time >= len || p->time < 0) {
		p->time = p->time >= len ? len : 0;
		p->done = 1;
	}
	p->posed = -1;
}
