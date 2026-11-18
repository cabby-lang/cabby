/* video_source.h — a video, as something to show: what the videos library
 * (runtime/videos.c, on FFmpeg) hands a window (the gui library's video
 * widget, runtime/gui.c), so that neither needs the other to be linked.
 * The window asks it for the picture to show now and for the sound to
 * play next, and tells it what to play, pause and seek; the videos
 * library decodes.
 *
 * Its address, as a whole number, is a video's `source` in Cabby:
 * `Window.add_video Film.source`. */
#ifndef CABBY_VIDEO_SOURCE_H
#define CABBY_VIDEO_SOURCE_H

#include <stdint.h>

#define CBY_VIDEO_MAGIC 0x43425956u   /* "CBYV" */
#define CBY_VIDEO_RATE 48000          /* the sound: so many a second, two channels, floats */

typedef struct CbyVideoSource {
	uint32_t magic;
	void *self;
	/* the clock it plays by, in seconds: the window's (whose clock, under
	 * a test script, is the script's) */
	void (*attach)(void *self, double (*clock)(void));
	/* the picture for now, RGBA (w * h * 4): NULL before there's one.
	 * serial changes when the picture does. Decodes as much as is needed
	 * for it. */
	const uint8_t *(*picture)(void *self, int *w, int *h, int64_t *serial);
	/* the sound to play next, into out (frames * 2 floats): ahead is how
	 * long until it's heard (what's waiting to be played already). Called
	 * from the sound's own thread. */
	void (*sound)(void *self, float *out, int frames, double ahead);
	void (*play)(void *self);
	void (*pause)(void *self);
	void (*seek)(void *self, double seconds);
	double (*position)(void *self);
	double (*duration)(void *self);
	int (*playing)(void *self);
	int (*has_sound)(void *self);
	int (*size)(void *self, int *w, int *h);
	/* how many times it has come to its end (for on_end) */
	int64_t (*ends)(void *self);
	/* how loud, 0 to 1 */
	float (*volume)(void *self);
} CbyVideoSource;

#endif
