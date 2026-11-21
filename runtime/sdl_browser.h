/* sdl_browser.h — a window in a web page, for the libraries that draw
 * through SDL (game), built for WebAssembly.
 *
 * There SDL draws in software, off screen, as it does under a test script,
 * and the page shows each frame: when SDL presents one, its pixels go to
 * the page through cabby.js's `cabby` imports (wasm/cabby.js), which draw
 * them on a canvas. What the person does there (keys, the mouse, closing)
 * comes back the same way, and is put into SDL's own events just before
 * the library looks at them, so the library reads them as it does a
 * window's. Waiting (SDL_Delay, a frame's rest) lets the page run
 * meanwhile: cabby.js suspends the program there (JSPI).
 *
 * Included after sdl_common.h and before the library's own code: from here
 * on, SDL_PollEvent, SDL_WaitEventTimeout, SDL_RenderPresent and
 * SDL_DestroyWindow are the ones below. */
#ifndef CABBY_SDL_BROWSER_H
#define CABBY_SDL_BROWSER_H
#ifndef __wasi__
#define IN_BROWSER 0
#else
#define IN_BROWSER 1

/* what the page has: a frame to show (RGBA, w by h), a window opened or
 * closed, and what the person did since it was last asked */
#define BROWSER_IMPORT(name) __attribute__((import_module("cabby"), import_name(name)))
BROWSER_IMPORT("show") void browser_show(const void *rgba, int w, int h);
BROWSER_IMPORT("window") void browser_window(const char *title, int w, int h);
BROWSER_IMPORT("closed") void browser_closed(void);
BROWSER_IMPORT("input") int browser_input(void *events, int max);

/* one thing the person did, as cabby.js writes it: 32 bytes */
typedef struct BrowserEvent {
	int32_t kind;        /* 1 a key down, 2 a key up, 3 a click (the button down), 4 the mouse moved,
				5 the window closed, 6 text typed, 7 the button let go, 8 the wheel turned */
	int32_t mods;        /* 1 Ctrl, 2 Shift, 4 Alt */
	int32_t repeat;      /* (a key held, repeating) */
	float x, y;          /* where, in the window's pixels (the wheel: how far, across and down) */
	char key[12];        /* the key, as SDL names it: "A", "Left", "Space"; or the text, in UTF-8 */
} BrowserEvent;

static SDL_Window *browser_shown;   /* the window the page is showing */
static bool browser_held;           /* (the mouse button, down) */

/* typed text, kept until it's read (an event holds only a pointer to it) */
static const char *browser_text(const char *s)
{
	static char ring[64][12];
	static int at;
	char *t = ring[at++ % 64];
	snprintf(t, sizeof ring[0], "%s", s);
	return t;
}

/* what the person did, into SDL's events */
static void browser_pump(void)
{
	BrowserEvent ev[32];
	int n = browser_input(ev, 32);
	SDL_WindowID id = browser_shown ? SDL_GetWindowID(browser_shown) : 0;
	for (int i = 0; i < n; i++) {
		SDL_Event e;
		SDL_zero(e);
		Uint64 now = SDL_GetTicksNS();
		SDL_Keymod mod = (SDL_Keymod)(((ev[i].mods & 1) ? SDL_KMOD_LCTRL : 0) | ((ev[i].mods & 2) ? SDL_KMOD_LSHIFT : 0) |
					      ((ev[i].mods & 4) ? SDL_KMOD_LALT : 0));
		switch (ev[i].kind) {
		case 1: case 2:
			ev[i].key[sizeof ev[i].key - 1] = '\0';
			e.type = ev[i].kind == 1 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			e.key.timestamp = now;
			e.key.windowID = id;
			e.key.key = SDL_GetKeyFromName(ev[i].key);
			e.key.mod = mod;
			e.key.down = ev[i].kind == 1;
			e.key.repeat = ev[i].repeat != 0;
			if (e.key.key == SDLK_UNKNOWN)
				continue;
			break;
		case 3: case 7:
			browser_held = ev[i].kind == 3;
			e.type = ev[i].kind == 3 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
			e.button.timestamp = now;
			e.button.windowID = id;
			e.button.button = SDL_BUTTON_LEFT;
			e.button.down = ev[i].kind == 3;
			e.button.clicks = 1;
			e.button.x = ev[i].x;
			e.button.y = ev[i].y;
			break;
		case 4:
			e.type = SDL_EVENT_MOUSE_MOTION;
			e.motion.timestamp = now;
			e.motion.windowID = id;
			e.motion.state = browser_held ? SDL_BUTTON_LMASK : 0;
			e.motion.x = ev[i].x;
			e.motion.y = ev[i].y;
			break;
		case 6:
			ev[i].key[sizeof ev[i].key - 1] = '\0';
			e.type = SDL_EVENT_TEXT_INPUT;
			e.text.timestamp = now;
			e.text.windowID = id;
			e.text.text = browser_text(ev[i].key);
			break;
		case 8:
			e.type = SDL_EVENT_MOUSE_WHEEL;
			e.wheel.timestamp = now;
			e.wheel.windowID = id;
			e.wheel.x = ev[i].x;
			e.wheel.y = ev[i].y;
			e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
			break;
		case 5:
			e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
			e.window.timestamp = now;
			e.window.windowID = id;
			break;
		default:
			continue;
		}
		SDL_PushEvent(&e);
	}
}

static bool browser_poll_event(SDL_Event *e)
{
	browser_pump();
	return SDL_PollEvent(e);
}

/* (waiting a little at a time, so the page is listened to meanwhile) */
static bool browser_wait_event(SDL_Event *e, Sint32 ms)
{
	Uint64 until = SDL_GetTicks() + (Uint64)(ms < 0 ? 0 : ms);
	for (;;) {
		if (browser_poll_event(e))
			return true;
		if (ms >= 0 && SDL_GetTicks() >= until)
			return false;
		SDL_Delay(10);
	}
}

/* a frame, presented: then shown on the page, as RGBA */
static bool browser_present(SDL_Renderer *r)
{
	bool ok = SDL_RenderPresent(r);
	SDL_Window *w = SDL_GetRenderWindow(r);
	SDL_Surface *s = w ? SDL_GetWindowSurface(w) : NULL;
	if (!s)
		return ok;
	static void *rgba;
	static size_t rgba_size;
	size_t need = (size_t)s->w * (size_t)s->h * 4;
	if (need > rgba_size) {
		free(rgba);
		rgba = malloc(need);
		rgba_size = rgba ? need : 0;
	}
	if (!rgba)
		return ok;
	if (w != browser_shown) {
		browser_shown = w;
		browser_window(SDL_GetWindowTitle(w), s->w, s->h);
	}
	SDL_ConvertPixels(s->w, s->h, s->format, s->pixels, s->pitch, SDL_PIXELFORMAT_RGBA32, rgba, s->w * 4);
	browser_show(rgba, s->w, s->h);
	return ok;
}

static void browser_destroy_window(SDL_Window *w)
{
	if (w && w == browser_shown) {
		browser_shown = NULL;
		browser_closed();
	}
	SDL_DestroyWindow(w);
}

#define SDL_PollEvent(e) browser_poll_event(e)
#define SDL_WaitEventTimeout(e, ms) browser_wait_event(e, ms)
#define SDL_RenderPresent(r) browser_present(r)
#define SDL_DestroyWindow(w) browser_destroy_window(w)

#endif
#endif
