/* world_text.h — writing over the world (included by world.c): a score, a
 * message, what's being aimed at. Each text stays where it was put, in the
 * window's pixels from its top left corner, until it's changed or removed.
 * They're drawn into a picture of their own (the overlay), made again only
 * when one changes, then laid over each frame: by Cabby's drawing, or as a
 * texture by OpenGL. Fonts as gui's are (sdl_common.h); with none on the
 * computer, Cabby's own 8 by 16 one. */

typedef struct Text {
	int world, alive, visible;
	char *text;
	double x, y, size;
	Color color;
} Text;

static Text *texts;
static int ntexts;

static int overlay_dirty = 1;
static unsigned char *overlay;   /* RGBA, ow by oh, not premultiplied */
static int ow, oh, overlay_any;

static Text *text_item(int64_t id)
{
	if (id < 1 || id > ntexts)
		fail("%s", "this text hasn't been added to a world yet");
	return &texts[id - 1];
}

int64_t cby_world_text_new(int64_t id, const char *s, double x, double y)
{
	(void)world(id);
	texts = realloc(texts, sizeof *texts * (size_t)(ntexts + 1));
	texts[ntexts] = (Text){ (int)id, 1, 1, strdup(s), x, y, 20, color("white") };
	overlay_dirty = 1;
	return ++ntexts;
}

void cby_world_text_set(int64_t id, const char *s)
{
	Text *t = text_item(id);
	if (strcmp(t->text, s) == 0)
		return;
	free(t->text);
	t->text = strdup(s);
	overlay_dirty = 1;
}

int64_t cby_world_text_length(int64_t id) { return hand(text_item(id)->text); }

void cby_world_text_move_to(int64_t id, double x, double y)
{
	Text *t = text_item(id);
	t->x = x, t->y = y;
	overlay_dirty = 1;
}

void cby_world_text_set_color(int64_t id, const char *c)
{
	text_item(id)->color = color(c);
	overlay_dirty = 1;
}

void cby_world_text_set_size(int64_t id, double size)
{
	if (size <= 0)
		fail("%s", "a text's size is its height in pixels: more than 0");
	text_item(id)->size = size;
	overlay_dirty = 1;
}

void cby_world_text_set_visible(int64_t id, int on)
{
	text_item(id)->visible = on != 0;
	overlay_dirty = 1;
}

void cby_world_text_remove(int64_t id)
{
	text_item(id)->alive = 0;
	overlay_dirty = 1;
}

/* a glyph's coverage laid onto the overlay, in the text's color */
static void overlay_put(int x, int y, unsigned cover, Color c)
{
	if (x < 0 || y < 0 || x >= ow || y >= oh || !cover)
		return;
	unsigned char *o = overlay + ((size_t)y * ow + x) * 4;
	float a = cover / 255.0f * c.a, was = o[3] / 255.0f;
	float out = a + was * (1 - a);
	if (out <= 0)
		return;
	float rgb[3] = { c.r, c.g, c.b };
	for (int k = 0; k < 3; k++) {
		float v = (rgb[k] * a + o[k] / 255.0f * was * (1 - a)) / out;
		o[k] = (unsigned char)(v * 255 + 0.5f);
	}
	o[3] = (unsigned char)(out * 255 + 0.5f);
	overlay_any = 1;
}

/* one text, at the overlay's scale (its pixels for each of the window's) */
static void overlay_text(const Text *t, float k)
{
	int font = font_for("Sans", 1, 0);
	Font *f = &fonts[font];
	float px = (float)(t->size * k);
	float pen = (float)(t->x * k), top = (float)(t->y * k);
	const unsigned char *p = (const unsigned char *)t->text;
	if (!f->data) {
		/* Cabby's own: 8 by 16, made bigger in whole pixels */
		int scale = (int)(px / 16 + 0.5f);
		if (scale < 1)
			scale = 1;
		while (*p) {
			uint32_t code = next_char(&p);
			if (code >= 32 && code < 127)
				for (int r = 0; r < 16; r++)
					for (int b = 0; b < 8; b++)
						if (game_font[code - 32][r] & (0x80 >> b))
							for (int dy = 0; dy < scale; dy++)
								for (int dx = 0; dx < scale; dx++)
									overlay_put((int)pen + b * scale + dx, (int)top + r * scale + dy, 255, t->color);
			pen += 8 * scale;
		}
		return;
	}
	float scale = stbtt_ScaleForPixelHeight(&f->info, px);
	int ascent, descent, gap;
	stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
	float base = top + ascent * scale;
	uint32_t prev = 0;
	while (*p) {
		uint32_t code = next_char(&p);
		if (prev)
			pen += stbtt_GetCodepointKernAdvance(&f->info, (int)prev, (int)code) * scale;
		int adv, lsb, w, h, xo, yo;
		stbtt_GetCodepointHMetrics(&f->info, (int)code, &adv, &lsb);
		unsigned char *bits = stbtt_GetCodepointBitmap(&f->info, scale, scale, (int)code, &w, &h, &xo, &yo);
		if (bits) {
			for (int r = 0; r < h; r++)
				for (int c = 0; c < w; c++)
					overlay_put((int)(pen + 0.5f) + xo + c, (int)(base + 0.5f) + yo + r, bits[r * w + c], t->color);
			stbtt_FreeBitmap(bits, NULL);
		}
		pen += adv * scale;
		prev = code;
	}
}

/* the overlay for this world, w by h pixels, the window's points times k;
 * 1 if it changed */
static int overlay_ready(int64_t id, int w, int h, float k)
{
	if (!overlay_dirty && w == ow && h == oh && overlay)
		return 0;
	if (w != ow || h != oh || !overlay) {
		free(overlay);
		overlay = malloc((size_t)w * h * 4);
		ow = w;
		oh = h;
	}
	memset(overlay, 0, (size_t)w * h * 4);
	overlay_any = 0;
	for (int i = 0; i < ntexts; i++)
		if (texts[i].world == id && texts[i].alive && texts[i].visible && texts[i].text[0])
			overlay_text(&texts[i], k);
	overlay_dirty = 0;
	return 1;
}

/* laid over a frame Cabby drew */
static void overlay_onto(Frame *f)
{
	if (!overlay_any || f->w != ow || f->h != oh)
		return;
	for (size_t i = 0; i < (size_t)ow * oh; i++) {
		unsigned a = overlay[i * 4 + 3];
		if (!a)
			continue;
		unsigned char *d = f->rgba + i * 4;
		for (int k = 0; k < 3; k++)
			d[k] = (unsigned char)((overlay[i * 4 + k] * a + d[k] * (255 - a) + 127) / 255);
	}
}
