/* world_soft.h — the world drawn by Cabby itself (included by world.c):
 * for test scripts, and where there's no OpenGL. Triangles are cut where
 * they cross in front of the camera, then filled a pixel at a time, the
 * nearest kept (a depth for every pixel), each pixel lit as the shaders
 * light it (world_gl.h), and shaded where the sun's map (below) says
 * something nearer the sun is in the way. Floats, worked in one order, with no fused
 * multiply-adds (world.c is built with -ffp-contract=off), so the picture
 * is the same to the pixel on every computer. */

typedef struct Frame {
	unsigned char *rgba;
	float *depth;
	int w, h;
} Frame;

/* a vertex on its way to the screen: where it is to the lens (clip
 * space), where it is in the world, its normal, and its place on the picture */
typedef struct SoftV {
	float c[4];
	V3 p, n;
	float u, v;
} SoftV;

static SoftV soft_mix(const SoftV *a, const SoftV *b, float t)
{
	SoftV r;
	for (int k = 0; k < 4; k++)
		r.c[k] = a->c[k] + (b->c[k] - a->c[k]) * t;
	r.p = v_add(a->p, v_mul(v_sub(b->p, a->p), t));
	r.n = v_add(a->n, v_mul(v_sub(b->n, a->n), t));
	r.u = a->u + (b->u - a->u) * t;
	r.v = a->v + (b->v - a->v) * t;
	return r;
}

typedef struct SoftShape {
	float base[4];
	const Picture *pic;
	float us, vs;
	int two_sided;
	const Lit *lit;
	int glow;             /* (its own color: no light on it) */
} SoftShape;

static void soft_pixel(Frame *f, int x, int y, float z, const SoftShape *s, V3 p, V3 n, float u, float v)
{
	float col[4] = { s->base[0], s->base[1], s->base[2], 1 };
	if (s->pic) {
		float tu = u * s->us, tv = v * s->vs;
		tu -= floorf(tu);
		tv -= floorf(tv);
		int px = (int)(tu * (float)s->pic->w), py = (int)(tv * (float)s->pic->h);
		if (px >= s->pic->w)
			px = s->pic->w - 1;
		if (py >= s->pic->h)
			py = s->pic->h - 1;
		const unsigned char *t = s->pic->rgba + ((size_t)py * s->pic->w + px) * 4;
		if (t[3] < 128)
			return;   /* (see-through there: as if nothing were) */
		for (int k = 0; k < 3; k++)
			col[k] *= t[k] / 255.0f;
	}
	float light[3] = { 1, 1, 1 };
	if (!s->glow)
		light_at(s->lit, p, v_norm(n), light);
	unsigned char *o = f->rgba + ((size_t)y * f->w + x) * 4;
	for (int k = 0; k < 3; k++) {
		float c = col[k] * light[k];
		c = c < 0 ? 0 : c > 1 ? 1 : c;
		o[k] = (unsigned char)(c * 255.0f + 0.5f);
	}
	o[3] = 255;
	f->depth[(size_t)y * f->w + x] = z;
}

static void soft_triangle(Frame *f, const SoftShape *s, const SoftV *a, const SoftV *b, const SoftV *c)
{
	const SoftV *t[3] = { a, b, c };
	float sx[3], sy[3], sz[3], iw[3];
	for (int i = 0; i < 3; i++) {
		iw[i] = 1.0f / t[i]->c[3];
		sx[i] = (t[i]->c[0] * iw[i] * 0.5f + 0.5f) * (float)f->w;
		sy[i] = (0.5f - t[i]->c[1] * iw[i] * 0.5f) * (float)f->h;
		sz[i] = t[i]->c[2] * iw[i];
	}
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
	if (area == 0)
		return;
	/* counterclockwise as the camera sees it is the front: on the screen,
	 * y counting down, that's an area below 0 */
	int back = area > 0;
	if (back && !s->two_sided)
		return;
	float minx = fminf(sx[0], fminf(sx[1], sx[2])), maxx = fmaxf(sx[0], fmaxf(sx[1], sx[2]));
	float miny = fminf(sy[0], fminf(sy[1], sy[2])), maxy = fmaxf(sy[0], fmaxf(sy[1], sy[2]));
	int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > f->w)
		x1 = f->w;
	if (y1 > f->h)
		y1 = f->h;
	float inv = 1.0f / area;
	for (int y = y0; y < y1; y++) {
		float py = (float)y + 0.5f;
		for (int x = x0; x < x1; x++) {
			float px = (float)x + 0.5f;
			float w0 = ((sx[2] - sx[1]) * (py - sy[1]) - (sy[2] - sy[1]) * (px - sx[1])) * inv;
			float w1 = ((sx[0] - sx[2]) * (py - sy[2]) - (sy[0] - sy[2]) * (px - sx[2])) * inv;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0)
				continue;
			float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
			if (z < -1 || z > 1 || z >= f->depth[(size_t)y * f->w + x])
				continue;
			/* in proportion to 1/w: as far away things shrink */
			float q0 = w0 * iw[0], q1 = w1 * iw[1], q2 = w2 * iw[2];
			float k = 1.0f / (q0 + q1 + q2);
			q0 *= k, q1 *= k, q2 *= k;
			V3 p = v_add(v_add(v_mul(a->p, q0), v_mul(b->p, q1)), v_mul(c->p, q2));
			V3 n = v_add(v_add(v_mul(a->n, q0), v_mul(b->n, q1)), v_mul(c->n, q2));
			if (back)
				n = v_mul(n, -1);
			float u = a->u * q0 + b->u * q1 + c->u * q2, v = a->v * q0 + b->v * q1 + c->v * q2;
			soft_pixel(f, x, y, z, s, p, n, u, v);
		}
	}
}

/* a triangle, cut where it goes behind the near plane (z = -w): what's left
 * is a triangle or four-sided, drawn as one or two */
static void soft_clip(Frame *f, const SoftShape *s, const SoftV *a, const SoftV *b, const SoftV *c)
{
	const SoftV *in[3] = { a, b, c };
	SoftV out[4];
	int n = 0;
	for (int i = 0; i < 3; i++) {
		const SoftV *p = in[i], *q = in[(i + 1) % 3];
		float dp = p->c[2] + p->c[3], dq = q->c[2] + q->c[3];
		if (dp >= 0)
			out[n++] = *p;
		if ((dp >= 0) != (dq >= 0))
			out[n++] = soft_mix(p, q, dp / (dp - dq));
	}
	if (n < 3)
		return;
	soft_triangle(f, s, &out[0], &out[1], &out[2]);
	if (n == 4)
		soft_triangle(f, s, &out[0], &out[2], &out[3]);
}

/* ---- shadows: the world as the sun sees it, its nearest depth at each of
 * the map's texels (world.c's shadow_view says where the sun looks from) */

#define SOFT_SHADOW 1024
static float *soft_shadow_map;

static void soft_shadow_triangle(const float (*q)[3], const float *uv, const Picture *pic, float us, float vs)
{
	float sx[3], sy[3];
	for (int i = 0; i < 3; i++) {
		sx[i] = (q[i][0] * 0.5f + 0.5f) * (float)SOFT_SHADOW;
		sy[i] = (q[i][1] * 0.5f + 0.5f) * (float)SOFT_SHADOW;
	}
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
	if (area == 0)
		return;
	float minx = fminf(sx[0], fminf(sx[1], sx[2])), maxx = fmaxf(sx[0], fmaxf(sx[1], sx[2]));
	float miny = fminf(sy[0], fminf(sy[1], sy[2])), maxy = fmaxf(sy[0], fmaxf(sy[1], sy[2]));
	int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > SOFT_SHADOW)
		x1 = SOFT_SHADOW;
	if (y1 > SOFT_SHADOW)
		y1 = SOFT_SHADOW;
	float inv = 1.0f / area;
	for (int y = y0; y < y1; y++) {
		float py = (float)y + 0.5f;
		for (int x = x0; x < x1; x++) {
			float px = (float)x + 0.5f;
			float w0 = ((sx[2] - sx[1]) * (py - sy[1]) - (sy[2] - sy[1]) * (px - sx[1])) * inv;
			float w1 = ((sx[0] - sx[2]) * (py - sy[2]) - (sy[0] - sy[2]) * (px - sx[2])) * inv;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0)
				continue;
			float d = (w0 * q[0][2] + w1 * q[1][2] + w2 * q[2][2]) * 0.5f + 0.5f;
			float *at = &soft_shadow_map[(size_t)y * SOFT_SHADOW + x];
			if (d < 0 || d >= *at)
				continue;
			if (pic) {
				/* (where the picture is see-through, so is its shadow: leaves) */
				float tu = (w0 * uv[0] + w1 * uv[2] + w2 * uv[4]) * us, tv = (w0 * uv[1] + w1 * uv[3] + w2 * uv[5]) * vs;
				tu -= floorf(tu);
				tv -= floorf(tv);
				int ux = (int)(tu * (float)pic->w), vy = (int)(tv * (float)pic->h);
				if (ux >= pic->w)
					ux = pic->w - 1;
				if (vy >= pic->h)
					vy = pic->h - 1;
				if (pic->rgba[((size_t)vy * pic->w + ux) * 4 + 3] < 128)
					continue;
			}
			*at = d;
		}
	}
}

/* the sun's map, for this frame, into lit (nothing if there are no shadows) */
static void soft_shadows(int64_t id, Lit *lit)
{
	World *wo = world(id);
	M4 vp;
	float texel;
	if (!shadow_view(wo, SOFT_SHADOW, &vp, &texel))
		return;
	if (!soft_shadow_map)
		soft_shadow_map = malloc(sizeof *soft_shadow_map * SOFT_SHADOW * SOFT_SHADOW);
	if (!soft_shadow_map)
		return;
	for (size_t i = 0; i < (size_t)SOFT_SHADOW * SOFT_SHADOW; i++)
		soft_shadow_map[i] = 1;
	float (*q)[3] = NULL;
	int cap = 0;
	for (int i = 0; i < nshapes; i++) {
		Shape *sh = &shapes[i];
		if (sh->world != id || !shown(sh) || sh->glow)
			continue;   /* (a glowing shape casts no shadow) */
		int np = shape_parts(sh, wo->frames);
		for (int pi = 0; pi < np; pi++) {
			Part *pt = &parts[pi];
			const Mesh *me = pt->mesh;
			if (!me || !me->nv)
				continue;
			M4 m = m_mul(vp, pt->model);
			if (me->nv > cap) {
				cap = me->nv;
				q = realloc(q, sizeof *q * (size_t)cap);
			}
			for (int k = 0; k < me->nv; k++) {
				const float *src = me->v + VERT * k;
				for (int r = 0; r < 3; r++)
					q[k][r] = m.m[r] * src[0] + m.m[4 + r] * src[1] + m.m[8 + r] * src[2] + m.m[12 + r];
			}
			const Picture *pic = pt->picture >= 0 ? &pictures[pt->picture] : NULL;
			for (int k = 0; k + 2 < me->ni; k += 3) {
				float tri[3][3], uv[6];
				for (int c = 0; c < 3; c++) {
					uint32_t vi = me->ix[k + c];
					memcpy(tri[c], q[vi], sizeof tri[c]);
					uv[c * 2] = me->v[VERT * vi + 6];
					uv[c * 2 + 1] = me->v[VERT * vi + 7];
				}
				soft_shadow_triangle(tri, uv, pic, pt->us, pt->vs);
			}
		}
	}
	free(q);
	lit->shadows = 1;
	lit->shadow_vp = vp;
	lit->shadow_map = soft_shadow_map;
	lit->shadow_size = SOFT_SHADOW;
	lit->shadow_texel = texel;
	lit->shadow_bias = texel / SHADOW_DEPTH;
}

static void soft_draw(int64_t id, Frame *f, int w, int h)
{
	World *wo = world(id);
	if (f->w != w || f->h != h || !f->rgba) {
		free(f->rgba);
		free(f->depth);
		f->rgba = malloc((size_t)w * h * 4);
		f->depth = malloc(sizeof *f->depth * (size_t)w * h);
		f->w = w;
		f->h = h;
	}
	unsigned char bg[4] = { (unsigned char)(wo->background.r * 255 + 0.5f), (unsigned char)(wo->background.g * 255 + 0.5f),
				(unsigned char)(wo->background.b * 255 + 0.5f), 255 };
	for (size_t i = 0; i < (size_t)w * h; i++) {
		memcpy(f->rgba + i * 4, bg, 4);
		f->depth[i] = 1;
	}
	Lit lit;
	lighting(id, &lit);
	soft_shadows(id, &lit);
	M4 vp = m_mul(lens_of(wo, (float)w / (float)h), view_of(wo));
	SoftV *vs = NULL;
	int cap = 0;
	for (int i = 0; i < nshapes; i++) {
		Shape *sh = &shapes[i];
		if (sh->world != id || !shown(sh))
			continue;
		int np = shape_parts(sh, wo->frames);
		for (int pi = 0; pi < np; pi++) {
			Part *pt = &parts[pi];
			const Mesh *me = pt->mesh;
			if (!me || !me->nv)
				continue;
			M4 model = pt->model;
			M4 mvp = m_mul(vp, model);
			float nm[9];
			m_normals(&model, nm);
			if (me->nv > cap) {
				cap = me->nv;
				vs = realloc(vs, sizeof *vs * (size_t)cap);
			}
			for (int k = 0; k < me->nv; k++) {
				const float *src = me->v + VERT * k;
				SoftV *d = &vs[k];
				for (int r = 0; r < 4; r++)
					d->c[r] = mvp.m[r] * src[0] + mvp.m[4 + r] * src[1] + mvp.m[8 + r] * src[2] + mvp.m[12 + r];
				d->p = m_point(&model, v3(src[0], src[1], src[2]));
				d->n = v3(nm[0] * src[3] + nm[3] * src[4] + nm[6] * src[5], nm[1] * src[3] + nm[4] * src[4] + nm[7] * src[5],
					  nm[2] * src[3] + nm[5] * src[4] + nm[8] * src[5]);
				d->u = src[6];
				d->v = src[7];
			}
			SoftShape ss = { { pt->color[0], pt->color[1], pt->color[2], pt->color[3] },
					 pt->picture >= 0 ? &pictures[pt->picture] : NULL, pt->us, pt->vs, pt->two_sided, &lit,
					 sh->glow };
			for (int k = 0; k + 2 < me->ni; k += 3)
				soft_clip(f, &ss, &vs[me->ix[k]], &vs[me->ix[k + 1]], &vs[me->ix[k + 2]]);
		}
	}
	free(vs);
}
