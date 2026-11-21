/* world_gl.h — the world drawn with OpenGL 3.3 (included by world.c). Its
 * functions are looked up from the driver when the window opens (with
 * SDL_GL_GetProcAddress), so there's nothing to link and no headers to
 * install. Each shape is drawn with one pair of shaders, which light it as
 * world_soft.h does: the sun, the light all round (ambient), and the
 * nearest lamps, worked out at every pixel. */

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef unsigned int GLbitfield;

#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_LESS 0x0201
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_MULTISAMPLE 0x809D
#define GL_FALSE 0
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_NEAREST 0x2600
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_LEQUAL 0x0203
#define GL_TEXTURE1 0x84C1
#define GL_NONE 0

#define GL_FUNCTIONS(X) \
	X(void, glEnable, (GLenum)) \
	X(void, glDisable, (GLenum)) \
	X(void, glCullFace, (GLenum)) \
	X(void, glFrontFace, (GLenum)) \
	X(void, glDepthFunc, (GLenum)) \
	X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
	X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, glClear, (GLbitfield)) \
	X(GLuint, glCreateShader, (GLenum)) \
	X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
	X(void, glCompileShader, (GLuint)) \
	X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
	X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, glDeleteShader, (GLuint)) \
	X(GLuint, glCreateProgram, (void)) \
	X(void, glAttachShader, (GLuint, GLuint)) \
	X(void, glLinkProgram, (GLuint)) \
	X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
	X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, glDeleteProgram, (GLuint)) \
	X(void, glUseProgram, (GLuint)) \
	X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
	X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *)) \
	X(void, glUniform3fv, (GLint, GLsizei, const GLfloat *)) \
	X(void, glUniform1fv, (GLint, GLsizei, const GLfloat *)) \
	X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
	X(void, glUniform1i, (GLint, GLint)) \
	X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
	X(void, glDeleteVertexArrays, (GLsizei, const GLuint *)) \
	X(void, glBindVertexArray, (GLuint)) \
	X(void, glGenBuffers, (GLsizei, GLuint *)) \
	X(void, glDeleteBuffers, (GLsizei, const GLuint *)) \
	X(void, glBindBuffer, (GLenum, GLuint)) \
	X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
	X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
	X(void, glEnableVertexAttribArray, (GLuint)) \
	X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *)) \
	X(void, glGenTextures, (GLsizei, GLuint *)) \
	X(void, glDeleteTextures, (GLsizei, const GLuint *)) \
	X(void, glBindTexture, (GLenum, GLuint)) \
	X(void, glActiveTexture, (GLenum)) \
	X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
	X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
	X(void, glGenerateMipmap, (GLenum)) \
	X(void, glPixelStorei, (GLenum, GLint)) \
	X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
	X(void, glFinish, (void)) \
	X(void, glBlendFunc, (GLenum, GLenum)) \
	X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
	X(void, glGenFramebuffers, (GLsizei, GLuint *)) \
	X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
	X(void, glBindFramebuffer, (GLenum, GLuint)) \
	X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
	X(GLenum, glCheckFramebufferStatus, (GLenum)) \
	X(void, glDrawBuffer, (GLenum)) \
	X(void, glReadBuffer, (GLenum))

#define GL_DECLARE(ret, name, args) static ret(*name) args;
GL_FUNCTIONS(GL_DECLARE)

static const char *gl_vertex_shader =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec3 aNormal;\n"
	"layout(location = 2) in vec2 aUV;\n"
	"uniform mat4 uMVP;\n"
	"uniform mat4 uModel;\n"
	"uniform mat3 uNormals;\n"
	"out vec3 vPos;\n"
	"out vec3 vNormal;\n"
	"out vec2 vUV;\n"
	"void main() {\n"
	"	vPos = (uModel * vec4(aPos, 1.0)).xyz;\n"
	"	vNormal = uNormals * aNormal;\n"
	"	vUV = aUV;\n"
	"	gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"}\n";

static const char *gl_fragment_shader =
	"#version 330 core\n"
	"in vec3 vPos;\n"
	"in vec3 vNormal;\n"
	"in vec2 vUV;\n"
	"uniform vec4 uColor;\n"
	"uniform sampler2D uPicture;\n"
	"uniform int uHasPicture;\n"
	"uniform vec2 uTile;\n"
	"uniform vec3 uSun;\n"
	"uniform vec3 uSunColor;\n"
	"uniform vec3 uAmbient;\n"
	"uniform int uLamps;\n"
	"uniform vec3 uLampPos[8];\n"
	"uniform vec3 uLampColor[8];\n"
	"uniform float uLampReach[8];\n"
	"uniform int uShadows;\n"
	"uniform mat4 uShadowVP;\n"
	"uniform sampler2DShadow uShadowMap;\n"
	"uniform float uShadowSize;\n"
	"uniform float uShadowTexel;\n"
	"uniform float uShadowBias;\n"
	"out vec4 outColor;\n"
	/* (as world.c's sun_reaches: nudged off the surface, nine texels round it) */
	"float sunReaches(vec3 n) {\n"
	"	if (uShadows == 0) return 1.0;\n"
	"	vec4 q = uShadowVP * vec4(vPos + n * uShadowTexel * 1.5, 1.0);\n"
	"	vec3 c = q.xyz * 0.5 + 0.5;\n"
	"	if (c.x < 0.0 || c.y < 0.0 || c.x >= 1.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;\n"
	"	vec2 at = (floor(c.xy * uShadowSize) + 0.5) / uShadowSize;\n"
	"	float seen = 0.0;\n"
	"	for (int dy = -1; dy <= 1; dy++)\n"
	"		for (int dx = -1; dx <= 1; dx++)\n"
	"			seen += texture(uShadowMap, vec3(at + vec2(dx, dy) / uShadowSize, c.z - uShadowBias));\n"
	"	return seen / 9.0;\n"
	"}\n"
	"void main() {\n"
	"	vec3 base = uColor.rgb;\n"
	"	if (uHasPicture != 0) {\n"
	"		vec4 t = texture(uPicture, vUV * uTile);\n"
	"		if (t.a < 0.5) discard;\n"
	"		base *= t.rgb;\n"
	"	}\n"
	"	vec3 n = normalize(vNormal);\n"
	"	if (!gl_FrontFacing) n = -n;\n"
	"	float sd = max(dot(n, uSun), 0.0);\n"
	"	if (sd > 0.0) sd *= sunReaches(n);\n"
	"	vec3 light = uAmbient + uSunColor * sd;\n"
	"	for (int i = 0; i < uLamps; i++) {\n"
	"		vec3 to = uLampPos[i] - vPos;\n"
	"		float d = length(to);\n"
	"		float fall = 1.0 - d / uLampReach[i];\n"
	"		if (fall <= 0.0) continue;\n"
	"		float f = d > 1e-6 ? dot(n, to / d) : 1.0;\n"
	"		if (f <= 0.0) continue;\n"
	"		light += uLampColor[i] * fall * fall * f;\n"
	"	}\n"
	"	outColor = vec4(clamp(base * light, 0.0, 1.0), 1.0);\n"
	"}\n";

/* the world as the sun sees it, for the shadows: only how far each texel
 * is (a picture's see-through places cast none) */
static const char *gl_shadow_vertex =
	"#version 330 core\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 2) in vec2 aUV;\n"
	"uniform mat4 uMVP;\n"
	"out vec2 vUV;\n"
	"void main() { vUV = aUV; gl_Position = uMVP * vec4(aPos, 1.0); }\n";

static const char *gl_shadow_fragment =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"uniform sampler2D uPicture;\n"
	"uniform int uHasPicture;\n"
	"uniform vec2 uTile;\n"
	"void main() { if (uHasPicture != 0 && texture(uPicture, vUV * uTile).a < 0.5) discard; }\n";

#define GL_SHADOW 2048
static GLuint gl_shadow_program, gl_shadow_fbo, gl_shadow_texture;
static GLint us_mvp, us_picture, us_has_picture, us_tile;

/* the texts over the world: the overlay (world_text.h), as a texture over
 * the whole window */
static const char *gl_overlay_vertex =
	"#version 330 core\n"
	"out vec2 vUV;\n"
	"void main() {\n"
	"	vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
	"	vUV = vec2(p.x, 1.0 - p.y);\n"
	"	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *gl_overlay_fragment =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"uniform sampler2D uOverlay;\n"
	"out vec4 outColor;\n"
	"void main() { outColor = texture(uOverlay, vUV); }\n";

static GLuint gl_overlay_program, gl_overlay_texture, gl_empty_vao;
static GLint u_overlay;

static GLuint gl_program;
static GLint u_mvp, u_model, u_normals, u_color, u_picture, u_has_picture, u_tile, u_sun, u_sun_color, u_ambient, u_lamps,
	u_lamp_pos, u_lamp_color, u_lamp_reach, u_shadows, u_shadow_vp, u_shadow_map, u_shadow_size, u_shadow_texel, u_shadow_bias;
static GLuint *gl_textures;   /* by picture (0: not made yet) */
static int ngl_textures;

static GLuint gl_compile(GLenum kind, const char *src)
{
	GLuint s = glCreateShader(kind);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof log, NULL, log);
		fprintf(stderr, "world: OpenGL wouldn't take a shader (so Cabby draws instead): %s\n", log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static GLuint gl_link(GLuint vs, GLuint fs)
{
	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		glDeleteProgram(p);
		return 0;
	}
	return p;
}

/* the functions, and the shaders; 0 if this OpenGL won't do */
static int gl_start(void)
{
	int missing = 0;
#define GL_LOAD(ret, name, args) \
	name = (ret(*) args)SDL_GL_GetProcAddress(#name); \
	if (!name) \
		missing = 1;
	GL_FUNCTIONS(GL_LOAD)
	if (missing)
		return 0;
	GLuint vs = gl_compile(GL_VERTEX_SHADER, gl_vertex_shader), fs = vs ? gl_compile(GL_FRAGMENT_SHADER, gl_fragment_shader) : 0;
	if (!vs || !fs) {
		if (vs)
			glDeleteShader(vs);
		return 0;
	}
	gl_program = gl_link(vs, fs);
	if (!gl_program)
		return 0;
	vs = gl_compile(GL_VERTEX_SHADER, gl_overlay_vertex);
	fs = vs ? gl_compile(GL_FRAGMENT_SHADER, gl_overlay_fragment) : 0;
	gl_overlay_program = vs && fs ? gl_link(vs, fs) : 0;
	if (!gl_overlay_program) {
		glDeleteProgram(gl_program);
		gl_program = 0;
		return 0;
	}
	u_overlay = glGetUniformLocation(gl_overlay_program, "uOverlay");
	glGenVertexArrays(1, &gl_empty_vao);
	glGenTextures(1, &gl_overlay_texture);
	glBindTexture(GL_TEXTURE_2D, gl_overlay_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	overlay_dirty = 1;   /* (a new texture: the texts go into it again) */
	u_mvp = glGetUniformLocation(gl_program, "uMVP");
	u_model = glGetUniformLocation(gl_program, "uModel");
	u_normals = glGetUniformLocation(gl_program, "uNormals");
	u_color = glGetUniformLocation(gl_program, "uColor");
	u_picture = glGetUniformLocation(gl_program, "uPicture");
	u_has_picture = glGetUniformLocation(gl_program, "uHasPicture");
	u_tile = glGetUniformLocation(gl_program, "uTile");
	u_sun = glGetUniformLocation(gl_program, "uSun");
	u_sun_color = glGetUniformLocation(gl_program, "uSunColor");
	u_ambient = glGetUniformLocation(gl_program, "uAmbient");
	u_lamps = glGetUniformLocation(gl_program, "uLamps");
	u_lamp_pos = glGetUniformLocation(gl_program, "uLampPos");
	u_lamp_color = glGetUniformLocation(gl_program, "uLampColor");
	u_lamp_reach = glGetUniformLocation(gl_program, "uLampReach");
	u_shadows = glGetUniformLocation(gl_program, "uShadows");
	u_shadow_vp = glGetUniformLocation(gl_program, "uShadowVP");
	u_shadow_map = glGetUniformLocation(gl_program, "uShadowMap");
	u_shadow_size = glGetUniformLocation(gl_program, "uShadowSize");
	u_shadow_texel = glGetUniformLocation(gl_program, "uShadowTexel");
	u_shadow_bias = glGetUniformLocation(gl_program, "uShadowBias");
	/* the shadows' own: their shaders, and a depth texture drawn into
	 * (without them, no shadows: the world is still drawn) */
	vs = gl_compile(GL_VERTEX_SHADER, gl_shadow_vertex);
	fs = vs ? gl_compile(GL_FRAGMENT_SHADER, gl_shadow_fragment) : 0;
	gl_shadow_program = vs && fs ? gl_link(vs, fs) : 0;
	if (gl_shadow_program) {
		us_mvp = glGetUniformLocation(gl_shadow_program, "uMVP");
		us_picture = glGetUniformLocation(gl_shadow_program, "uPicture");
		us_has_picture = glGetUniformLocation(gl_shadow_program, "uHasPicture");
		us_tile = glGetUniformLocation(gl_shadow_program, "uTile");
		glGenTextures(1, &gl_shadow_texture);
		glBindTexture(GL_TEXTURE_2D, gl_shadow_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, GL_SHADOW, GL_SHADOW, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		glGenFramebuffers(1, &gl_shadow_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, gl_shadow_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl_shadow_texture, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			glDeleteFramebuffers(1, &gl_shadow_fbo);
			glDeleteTextures(1, &gl_shadow_texture);
			glDeleteProgram(gl_shadow_program);
			gl_shadow_fbo = gl_shadow_texture = gl_shadow_program = 0;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	return 1;
}

/* everything made for this window's OpenGL, let go with it */
static void gl_stop(void)
{
	for (int i = 0; i < nmeshes + nall_meshes; i++) {
		Mesh *m = i < nmeshes ? &meshes[i] : all_meshes[i - nmeshes];
		if (m->gl_vao) {
			glDeleteVertexArrays(1, &m->gl_vao);
			glDeleteBuffers(1, &m->gl_vbo);
			glDeleteBuffers(1, &m->gl_ebo);
			m->gl_vao = m->gl_vbo = m->gl_ebo = 0;
		}
	}
	for (int i = 0; i < ngl_textures; i++)
		if (gl_textures[i])
			glDeleteTextures(1, &gl_textures[i]);
	free(gl_textures);
	gl_textures = NULL;
	ngl_textures = 0;
	glDeleteProgram(gl_program);
	gl_program = 0;
	glDeleteProgram(gl_overlay_program);
	gl_overlay_program = 0;
	if (gl_shadow_program) {
		glDeleteProgram(gl_shadow_program);
		glDeleteFramebuffers(1, &gl_shadow_fbo);
		glDeleteTextures(1, &gl_shadow_texture);
		gl_shadow_program = gl_shadow_fbo = gl_shadow_texture = 0;
	}
	glDeleteTextures(1, &gl_overlay_texture);
	glDeleteVertexArrays(1, &gl_empty_vao);
	gl_overlay_texture = gl_empty_vao = 0;
}

static void gl_mesh(Mesh *m)
{
	if (m->gl_vao && m->changed) {   /* (a skin bent: its vertices again) */
		glBindBuffer(GL_ARRAY_BUFFER, m->gl_vbo);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(float) * VERT * (size_t)m->nv), m->v, GL_DYNAMIC_DRAW);
		m->changed = 0;
	}
	if (m->gl_vao)
		return;
	m->changed = 0;
	glGenVertexArrays(1, &m->gl_vao);
	glBindVertexArray(m->gl_vao);
	glGenBuffers(1, &m->gl_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, m->gl_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(float) * VERT * (size_t)m->nv), m->v, GL_STATIC_DRAW);
	glGenBuffers(1, &m->gl_ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->gl_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * (size_t)m->ni), m->ix, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VERT * sizeof(float), (void *)0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, VERT * sizeof(float), (void *)(3 * sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, VERT * sizeof(float), (void *)(6 * sizeof(float)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
}

static GLuint gl_texture(int pic)
{
	if (pic >= ngl_textures) {
		gl_textures = realloc(gl_textures, sizeof *gl_textures * (size_t)(pic + 1));
		memset(gl_textures + ngl_textures, 0, sizeof *gl_textures * (size_t)(pic + 1 - ngl_textures));
		ngl_textures = pic + 1;
	}
	if (!gl_textures[pic]) {
		Picture *p = &pictures[pic];
		glGenTextures(1, &gl_textures[pic]);
		glBindTexture(GL_TEXTURE_2D, gl_textures[pic]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p->w, p->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p->rgba);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, is_sharp(pic) ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, is_sharp(pic) ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	return gl_textures[pic];
}

static void gl_overlay(int64_t id, int w, int h, float k)
{
	if (overlay_ready(id, w, h, k)) {
		glBindTexture(GL_TEXTURE_2D, gl_overlay_texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, overlay);
	}
	if (!overlay_any)
		return;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(gl_overlay_program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl_overlay_texture);
	glUniform1i(u_overlay, 0);
	glBindVertexArray(gl_empty_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisable(GL_BLEND);
}

/* the sun's map, drawn into the shadow texture: into lit, if there is one */
static void gl_shadows(int64_t id, Lit *lit)
{
	World *wo = world(id);
	M4 vp;
	float texel;
	if (!gl_shadow_program || !shadow_view(wo, GL_SHADOW, &vp, &texel))
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, gl_shadow_fbo);
	glViewport(0, 0, GL_SHADOW, GL_SHADOW);
	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);   /* (both sides cast, as world_soft.h's) */
	glUseProgram(gl_shadow_program);
	glUniform1i(us_picture, 0);
	glActiveTexture(GL_TEXTURE0);
	for (int i = 0; i < nshapes; i++) {
		Shape *sh = &shapes[i];
		if (sh->world != id || !sh->alive || !sh->visible || sh->scale <= 0)
			continue;
		int np = shape_parts(sh, wo->frames);
		for (int pi = 0; pi < np; pi++) {
			Part *pt = &parts[pi];
			Mesh *me = pt->mesh;
			if (!me || !me->nv || !me->ni)
				continue;
			gl_mesh(me);
			M4 mvp = m_mul(vp, pt->model);
			glUniformMatrix4fv(us_mvp, 1, GL_FALSE, mvp.m);
			if (pt->picture >= 0) {
				glBindTexture(GL_TEXTURE_2D, gl_texture(pt->picture));
				glUniform1i(us_has_picture, 1);
				glUniform2f(us_tile, pt->us, pt->vs);
			} else {
				glUniform1i(us_has_picture, 0);
			}
			glBindVertexArray(me->gl_vao);
			glDrawElements(GL_TRIANGLES, me->ni, GL_UNSIGNED_INT, (void *)0);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	lit->shadows = 1;
	lit->shadow_vp = vp;
	lit->shadow_size = GL_SHADOW;
	lit->shadow_texel = texel;
	lit->shadow_bias = texel / SHADOW_DEPTH;
}

static void gl_draw(int64_t id, int w, int h, float k)
{
	World *wo = world(id);
	Lit lit;
	lighting(id, &lit);
	gl_shadows(id, &lit);
	glViewport(0, 0, w, h);
	glClearColor(wo->background.r, wo->background.g, wo->background.b, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);
	glEnable(GL_MULTISAMPLE);
	glUseProgram(gl_program);
	glUniform1i(u_shadows, lit.shadows);
	glUniform1i(u_shadow_map, 1);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gl_shadow_texture);
	if (lit.shadows) {
		glUniformMatrix4fv(u_shadow_vp, 1, GL_FALSE, lit.shadow_vp.m);
		glUniform1fv(u_shadow_size, 1, (float[]){ (float)lit.shadow_size });
		glUniform1fv(u_shadow_texel, 1, &lit.shadow_texel);
		glUniform1fv(u_shadow_bias, 1, &lit.shadow_bias);
	}
	glUniform3fv(u_sun, 1, &lit.sun.x);
	glUniform3fv(u_sun_color, 1, lit.sun_c);
	glUniform3fv(u_ambient, 1, lit.ambient);
	glUniform1i(u_lamps, lit.nlamps);
	if (lit.nlamps) {
		glUniform3fv(u_lamp_pos, lit.nlamps, &lit.lamp_pos[0][0]);
		glUniform3fv(u_lamp_color, lit.nlamps, &lit.lamp_c[0][0]);
		glUniform1fv(u_lamp_reach, lit.nlamps, lit.lamp_reach);
	}
	glUniform1i(u_picture, 0);
	glActiveTexture(GL_TEXTURE0);
	M4 vp = m_mul(lens_of(wo, (float)w / (float)h), view_of(wo));
	for (int i = 0; i < nshapes; i++) {
		Shape *sh = &shapes[i];
		if (sh->world != id || !sh->alive || !sh->visible || sh->scale <= 0)
			continue;
		int np = shape_parts(sh, wo->frames);
		for (int pi = 0; pi < np; pi++) {
			Part *pt = &parts[pi];
			Mesh *me = pt->mesh;
			if (!me || !me->nv || !me->ni)
				continue;
			gl_mesh(me);
			M4 model = pt->model;
			M4 mvp = m_mul(vp, model);
			float nm[9];
			m_normals(&model, nm);
			glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);
			glUniformMatrix4fv(u_model, 1, GL_FALSE, model.m);
			glUniformMatrix3fv(u_normals, 1, GL_FALSE, nm);
			glUniform4fv(u_color, 1, pt->color);
			if (pt->picture >= 0) {
				glBindTexture(GL_TEXTURE_2D, gl_texture(pt->picture));
				glUniform1i(u_has_picture, 1);
				glUniform2f(u_tile, pt->us, pt->vs);
			} else {
				glUniform1i(u_has_picture, 0);
			}
			if (pt->two_sided)
				glDisable(GL_CULL_FACE);
			else
				glEnable(GL_CULL_FACE);
			glBindVertexArray(me->gl_vao);
			glDrawElements(GL_TRIANGLES, me->ni, GL_UNSIGNED_INT, (void *)0);
		}
	}
	gl_overlay(id, w, h, k);
}

/* what's drawn, read back (for a test script's shot and pixel): rows from
 * the top, as a picture's are */
static void gl_read(Frame *f, int w, int h)
{
	if (f->w != w || f->h != h || !f->rgba) {
		free(f->rgba);
		free(f->depth);
		f->rgba = malloc((size_t)w * h * 4);
		f->depth = malloc(sizeof *f->depth * (size_t)w * h);
		f->w = w;
		f->h = h;
	}
	glFinish();
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, f->rgba);
	size_t row = (size_t)w * 4;
	unsigned char *tmp = malloc(row);
	for (int y = 0; y < h / 2; y++) {
		memcpy(tmp, f->rgba + row * y, row);
		memcpy(f->rgba + row * y, f->rgba + row * (h - 1 - y), row);
		memcpy(f->rgba + row * (h - 1 - y), tmp, row);
	}
	free(tmp);
}
