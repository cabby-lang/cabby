/* sound.c — the sound library: sound files, notes and MIDI files.
 *
 * Everything that plays is mixed here, into one stream: 48000 frames a
 * second, two channels, as floats. miniaudio sends the stream to the
 * system's sound (PipeWire, PulseAudio, ALSA, Core Audio) and reads sound
 * files; TinySoundFont plays notes with a SoundFont's instruments, and
 * TinyMidiLoader reads MIDI files. Without a SoundFont, notes are played by
 * a small synthesizer of our own instead.
 *
 * What plays:
 *   - clips: sound.play "boom.wav", read whole into memory once, and played
 *     from there as often as asked, several at a time;
 *   - tracks: sound.load gives one, read from its file as it plays, which can
 *     be paused, looped and so on (music is a track that loops);
 *   - events: notes going on and off, and a MIDI file's messages, each at
 *     the frame it's due.
 *
 * With CABBY_SOUND=none, nothing is sent to a sound device: the stream is
 * mixed only when sound.wait asks, as fast as it can, so tests run at once.
 * With CABBY_SOUND_OUT=file.wav as well (or on its own), what was mixed is
 * saved to that file when the program ends, to listen to. */

#include <math.h>
#include <pthread.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "third_party/miniaudio.h"
#define TSF_IMPLEMENTATION
#include "third_party/tsf.h"
#define TML_IMPLEMENTATION
#include "third_party/tml.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define RATE 48000
#define CHANNELS 2

/* ------------------------------------------------------------- panics */

extern _Noreturn void rt_panic(const char *file, int line, int col, const char *fmt, ...) __attribute__((weak));

static _Noreturn void fail(const char *fmt, const char *a)
{
	if (rt_panic)
		rt_panic(NULL, 0, 0, fmt, a);
	fprintf(stderr, "sound: ");
	fprintf(stderr, fmt, a);
	fputc('\n', stderr);
	exit(101);
}

/* ------------------------------------------------------------- what plays */

typedef struct Clip {
	char *path;
	float *pcm;          /* frames * CHANNELS */
	uint64_t frames;
} Clip;

typedef struct Voice {   /* a clip playing */
	int clip;            /* (index) */
	uint64_t pos;
	int alive;
} Voice;

typedef struct Track {
	int used;
	char *path;
	ma_decoder dec;
	int playing, looping;
	float volume;
	uint64_t length;     /* frames (0: not known) */
	uint64_t pos;
} Track;

enum { EV_ON, EV_OFF, EV_PROGRAM, EV_CONTROL, EV_PITCH, EV_END };

typedef struct Event {
	uint64_t at;         /* the frame it's due */
	int type, channel, a, b;
	int midi;            /* from a MIDI file (stopping it takes these out): 1 play_midi, 2 the music */
} Event;

typedef struct Synth {   /* a note of our own synthesizer's */
	int channel, key;
	double freq, phase;
	float velocity;
	uint64_t start, released;   /* frames; released 0: still held */
	int alive;
	int pedal;                  /* let go while the sustain pedal was down: held until it's up */
} Synth;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static Clip *clips;
static int nclips;
static Voice *voices;
static int nvoices;
static Track **tracks;          /* (each in a place of its own: a decoder mustn't move) */
static int ntracks;
static Event *events;
static int nevents, capevents;
static Synth synths[64];
static uint64_t now;           /* frames mixed so far */
static float master = 1;
static int music = -1;         /* the track play_music plays */

/* Music can be a MIDI file too: its messages are kept, queued as events
 * (marked 2), and queued again from the start each time it ends. */
static tml_message *music_midi;
static int64_t music_loop_at = -1;   /* the frame it ends, to start again (-1: not due) */
static float music_volume = 1;       /* set_music_volume (fades start from it) */
static float effects_volume = 1;     /* set_effects_volume: sound.play's sounds */
static float music_fade = 1;         /* the fade's part: 1 to 0 */
static uint64_t fade_start, fade_frames;   /* fade_frames 0: not fading */
static char *music_path;             /* what the music is ("": none, or faded out) */

/* change_music: the music that starts when the fading one has gone */
static tml_message *next_midi;
static int next_track = -1;
static char *next_path;

/* crossfade_music: the new music getting louder (music_rise 0 to 1) while
 * the old gets quieter. A MIDI file's music plays on channels 0 to 15, or
 * 48 to 63 (music_bank 1), so the old one's notes and instruments are
 * still its own while it fades: its events are marked 3. */
static int music_bank;
static float music_rise = 1;
static uint64_t rise_start, rise_frames;
static int old_music = -1;           /* the old music's track (-1: none) */
static int old_midi, old_bank;       /* the old music is a MIDI file's, on that bank */
static float old_fade = 1;
static uint64_t old_start, old_frames;

/* tracks the music has finished with, closed by the program's thread */
static int spent[16];
static int nspent;

static tsf *font;              /* the SoundFont (NULL: our own synthesizer) */
static int font_looked;
static char *font_path;

/* instruments: each one used gets a channel (not 9, MIDI's drums, which
 * "drums" uses; 0 to 15 are left for MIDI files) */
static struct { char *name; int channel; } instruments[32];
static int ninstruments;
static char *instrument;       /* what play_note plays with */
static float note_volume = 0.8f;

/* ------------------------------------------------------------- the device */

static ma_device device;
static int started;            /* the device is running */
static int quiet;              /* no device: mixed only when wait asks */
static int setup_done;

static float *kept;        /* CABBY_SOUND_OUT: everything mixed, to save */
static uint64_t nkept, capkept;

static void mix(float *out, uint32_t frames);

static void on_device(ma_device *d, void *out, const void *in, ma_uint32 frames)
{
	(void)d;
	(void)in;
	pthread_mutex_lock(&lock);
	mix((float *)out, frames);
	pthread_mutex_unlock(&lock);
}

static void save_kept(void)
{
	const char *path = getenv("CABBY_SOUND_OUT");
	if (!path || !*path || !kept)
		return;
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	uint32_t data = (uint32_t)(nkept * CHANNELS * 2);
	uint32_t riff = 36 + data, fmt = 16, rate = RATE, bytes = RATE * CHANNELS * 2;
	uint16_t pcm = 1, ch = CHANNELS, align = CHANNELS * 2, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
	fwrite(&bytes, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
	for (uint64_t i = 0; i < nkept * CHANNELS; i++) {
		float v = kept[i];
		int16_t s = (int16_t)(v >= 1 ? 32767 : v <= -1 ? -32767 : v * 32767);
		fwrite(&s, 2, 1, f);
	}
	fclose(f);
}

void cby_sound_wait(void);

static void at_end(void)
{
	if (quiet && getenv("CABBY_SOUND_OUT"))
		cby_sound_wait();   /* what's left, into the file */
	if (started) {
		ma_device_uninit(&device);
		started = 0;
	}
	save_kept();
}

/* the system's sound, for playing and recording: the usual sound systems,
 * in order (not JACK: it's for studios, and PipeWire answers as PulseAudio
 * anyway) */
static ma_context context;
static int context_state;      /* 0 not tried, 1 ready, -1 none */

static int get_context(void)
{
	if (!context_state) {
		ma_backend backends[] = { ma_backend_coreaudio, ma_backend_pulseaudio, ma_backend_alsa, ma_backend_sndio,
					  ma_backend_audio4, ma_backend_oss };
		ma_uint32 n = sizeof backends / sizeof *backends;
		/* CABBY_SOUND_SYSTEM=alsa (or pulseaudio, coreaudio) uses that one
		 * only: to find out whether a problem is the sound system's */
		const char *want = getenv("CABBY_SOUND_SYSTEM");
		if (want && *want) {
			ma_backend only = strcmp(want, "alsa") == 0 ? ma_backend_alsa
					: strcmp(want, "pulseaudio") == 0 || strcmp(want, "pipewire") == 0 ? ma_backend_pulseaudio
					: strcmp(want, "coreaudio") == 0 ? ma_backend_coreaudio : ma_backend_null;
			if (only == ma_backend_null)
				fprintf(stderr, "sound: CABBY_SOUND_SYSTEM is alsa, pulseaudio or coreaudio, not '%s'\n", want);
			else {
				backends[0] = only;
				n = 1;
			}
		}
		context_state = ma_context_init(backends, n, NULL, &context) == MA_SUCCESS ? 1 : -1;
	}
	return context_state == 1;
}

static void setup_once(void);
static pthread_once_t setup_flag = PTHREAD_ONCE_INIT;

/* the device, opened the first time anything plays (from any thread: a
 * worker may be first) */
static void setup(void)
{
	pthread_once(&setup_flag, setup_once);
}

static void setup_once(void)
{
	setup_done = 1;
	atexit(at_end);
	const char *mode = getenv("CABBY_SOUND");
	const char *out = getenv("CABBY_SOUND_OUT");
	if ((mode && strcmp(mode, "none") == 0) || (out && *out)) {
		quiet = 1;
		return;
	}
	ma_device_config c = ma_device_config_init(ma_device_type_playback);
	c.playback.format = ma_format_f32;
	c.playback.channels = CHANNELS;
	c.sampleRate = RATE;
	c.dataCallback = on_device;
	if (!get_context() || ma_device_init(&context, &c, &device) != MA_SUCCESS || ma_device_start(&device) != MA_SUCCESS) {
		fprintf(stderr, "sound: no sound device could be opened, so nothing will be heard\n");
		quiet = 1;
		return;
	}
	started = 1;
	if (device.pContext && device.pContext->backend == ma_backend_null) {
		/* (miniaudio's last resort: a device that plays to nowhere) */
		fprintf(stderr, "sound: no sound device could be opened, so nothing will be heard\n");
		ma_device_uninit(&device);
		started = 0;
		quiet = 1;
	}
}

/* ------------------------------------------------------------- the SoundFont */

static void find_font(void)
{
	if (font_looked)
		return;
	font_looked = 1;
	const char *env = getenv("CABBY_SOUNDFONT");
	static const char *const places[] = {
		"/usr/share/soundfonts/default.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2",
		"/usr/share/sounds/sf2/default-GM.sf2", "/usr/share/sounds/sf2/FluidR3_GM.sf2",
		"/usr/share/sounds/sf2/TimGM6mb.sf2", "/usr/local/share/soundfonts/default.sf2",
		"/opt/homebrew/share/soundfonts/default.sf2", NULL };
	if (env && strcmp(env, "none") == 0)
		return;   /* (our own synthesizer, wherever there's a SoundFont) */
	if (env && *env) {
		font = tsf_load_filename(env);
		if (font)
			font_path = strdup(env);
	}
	for (int i = 0; !font && places[i]; i++) {
		font = tsf_load_filename(places[i]);
		if (font)
			font_path = strdup(places[i]);
	}
	if (font) {
		tsf_set_output(font, TSF_STEREO_INTERLEAVED, RATE, 0);
		tsf_set_max_voices(font, 128);
	}
}

/* ------------------------------------------------------------- our own synthesizer
 *
 * A note is a sine with two quieter overtones (so it sounds like an
 * instrument, not a test tone), rising quickly, settling, and fading out
 * when let go. */

static void synth_on(int channel, int key, float velocity)
{
	int free = -1;
	for (int i = 0; i < 64; i++)
		if (!synths[i].alive) {
			free = i;
			break;
		}
	if (free < 0) {   /* all in use: the oldest makes way */
		free = 0;
		for (int i = 1; i < 64; i++)
			if (synths[i].start < synths[free].start)
				free = i;
	}
	synths[free] = (Synth){ channel, key, 440.0 * pow(2, (key - 69) / 12.0), 0, velocity, now, 0, 1, 0 };
}

static int synth_sustain[64];   /* each channel's sustain pedal is down */

static void synth_off(int channel, int key)
{
	for (int i = 0; i < 64; i++)
		if (synths[i].alive && !synths[i].released && synths[i].channel == channel && synths[i].key == key) {
			if (channel >= 0 && channel < 64 && synth_sustain[channel])
				synths[i].pedal = 1;
			else
				synths[i].released = now ? now : 1;
		}
}

/* the sustain pedal (MIDI's control 64): notes let go while it was down
 * are let go as it comes up */
static void synth_pedal(int channel, int down)
{
	if (channel < 0 || channel >= 64)
		return;
	synth_sustain[channel] = down;
	if (!down)
		for (int i = 0; i < 64; i++)
			if (synths[i].alive && synths[i].pedal && synths[i].channel == channel) {
				synths[i].pedal = 0;
				synths[i].released = now ? now : 1;
			}
}

static void synth_render(float *out, uint32_t frames)
{
	const double attack = RATE * 0.005, decay = RATE * 0.15, release = RATE * 0.12;
	for (int i = 0; i < 64; i++) {
		Synth *s = &synths[i];
		if (!s->alive)
			continue;
		double step = 2 * M_PI * s->freq / RATE;
		for (uint32_t f = 0; f < frames; f++) {
			uint64_t t = now + f;
			double age = (double)(t - s->start), env;
			if (age < attack)
				env = age / attack;
			else if (age < attack + decay)
				env = 1 - 0.4 * (age - attack) / decay;
			else
				env = 0.6;
			if (s->released) {
				double gone = (double)(t - s->released);
				if (gone >= release) {
					s->alive = 0;
					break;
				}
				env *= 1 - gone / release;
			}
			double v = sin(s->phase) + 0.3 * sin(2 * s->phase) + 0.1 * sin(3 * s->phase);
			s->phase += step;
			if (s->phase > 2 * M_PI)
				s->phase -= 2 * M_PI;
			float x = (float)(v * env * s->velocity * 0.18);
			out[f * 2] += x;
			out[f * 2 + 1] += x;
		}
	}
}

static int synth_busy(void)
{
	for (int i = 0; i < 64; i++)
		if (synths[i].alive)
			return 1;
	return 0;
}

/* ------------------------------------------------------------- events */

static void add_event(uint64_t at, int type, int channel, int a, int b, int midi)
{
	if (nevents == capevents) {
		capevents = capevents ? capevents * 2 : 256;
		events = realloc(events, sizeof *events * (size_t)capevents);
	}
	int i = nevents++;
	while (i > 0 && events[i - 1].at > at) {   /* kept in order: most are added in order already */
		events[i] = events[i - 1];
		i--;
	}
	events[i] = (Event){ at, type, channel, a, b, midi };
}

static void run_event(const Event *e)
{
	switch (e->type) {
	case EV_ON: {
		float v = e->b / 127.0f;
		if (e->midi == 2)
			v *= music_volume * music_fade * music_rise;   /* (the music's volume: for each note as it starts) */
		else if (e->midi == 3)
			v *= music_volume * old_fade;
		if (v <= 0)
			break;
		if (font)
			tsf_channel_note_on(font, e->channel, e->a, v);
		else
			synth_on(e->channel, e->a, v);
		break;
	}
	case EV_END:
		if (e->midi == 2)
			music_loop_at = (int64_t)e->at;   /* the music: again, from the start */
		break;
	case EV_OFF:
		if (font)
			tsf_channel_note_off(font, e->channel, e->a);
		else
			synth_off(e->channel, e->a);
		break;
	case EV_PROGRAM:
		if (font)
			tsf_channel_set_presetnumber(font, e->channel, e->a, e->channel % 48 == 9);
		break;
	case EV_CONTROL:
		if (font)
			tsf_channel_midi_control(font, e->channel, e->a, e->b);
		else if (e->a == 64)
			synth_pedal(e->channel, e->b >= 64);
		break;
	case EV_PITCH:
		if (font)
			tsf_channel_set_pitchwheel(font, e->channel, e->a);
		break;
	}
}

static void queue_midi(tml_message *m, uint64_t start, int midi, int bank);
static void notes_off(int midi, int bank);

/* ------------------------------------------------------------- monitoring
 *
 * Hearing the input as it's recorded (sound.set_monitoring): what comes in
 * is converted as it comes (the input's callback), into a small buffer that
 * the mix takes from, a moment later. Little is kept waiting, so it's
 * heard as soon as it can be: if more piles up (the input's clock a little
 * fast against the output's), the oldest goes. */

#define MONITOR_CAP (RATE / 2)            /* frames the buffer holds */
static pthread_mutex_t mon_lock = PTHREAD_MUTEX_INITIALIZER;
static int monitoring;                    /* sound.set_monitoring */
static float monitor_volume = 1;
static float mon_buf[MONITOR_CAP * CHANNELS];
static uint32_t mon_read, mon_count;      /* (frames) */
static ma_data_converter mon_conv;
static int mon_conv_ready;

/* frames from the input, converted, onto the buffer (the input's thread) */
static void monitor_in(const float *in, uint64_t n)
{
	if (!mon_conv_ready)
		return;
	float out[1024 * CHANNELS];
	uint64_t done = 0;
	while (done < n) {
		ma_uint64 in_frames = n - done, out_frames = 1024;
		if (ma_data_converter_process_pcm_frames(&mon_conv, in + done * mon_conv.channelsIn, &in_frames, out,
							 &out_frames) != MA_SUCCESS || (!in_frames && !out_frames))
			break;
		done += in_frames;
		pthread_mutex_lock(&mon_lock);
		for (ma_uint64 f = 0; f < out_frames; f++) {
			if (mon_count == MONITOR_CAP) {
				mon_read = (mon_read + 1) % MONITOR_CAP;
				mon_count--;
			}
			uint32_t at = (mon_read + mon_count) % MONITOR_CAP;
			for (int c = 0; c < CHANNELS; c++)
				mon_buf[at * CHANNELS + c] = out[f * CHANNELS + c];
			mon_count++;
		}
		pthread_mutex_unlock(&mon_lock);
	}
}

/* the input, added to what's mixed (the output's thread, in mix) */
static void monitor_out(float *out, uint32_t frames)
{
	pthread_mutex_lock(&mon_lock);
	/* no more waiting than a few of the output's pieces: what's late is
	 * dropped rather than heard later and later */
	uint32_t most = frames * 3 + RATE / 100;
	while (mon_count > most) {
		mon_read = (mon_read + 1) % MONITOR_CAP;
		mon_count--;
	}
	uint32_t n = mon_count < frames ? mon_count : frames;
	float v = monitoring ? monitor_volume : 0;
	for (uint32_t f = 0; f < n; f++) {
		for (int c = 0; c < CHANNELS; c++)
			out[f * CHANNELS + c] += mon_buf[mon_read * CHANNELS + c] * v;
		mon_read = (mon_read + 1) % MONITOR_CAP;
	}
	mon_count -= n;
	pthread_mutex_unlock(&mon_lock);
}

/* ------------------------------------------------------------- mixing */

static float *scratch;
static uint32_t capscratch;

static void mix_part(float *out, uint32_t frames)
{
	/* clips */
	for (int i = 0; i < nvoices; i++) {
		Voice *v = &voices[i];
		if (!v->alive)
			continue;
		Clip *c = &clips[v->clip];
		uint64_t left = c->frames - v->pos, n = left < frames ? left : frames;
		for (uint64_t f = 0; f < n * CHANNELS; f++)
			out[f] += c->pcm[v->pos * CHANNELS + f] * effects_volume;
		v->pos += n;
		if (v->pos >= c->frames)
			v->alive = 0;
	}
	/* tracks */
	if (capscratch < frames) {
		capscratch = frames;
		scratch = realloc(scratch, sizeof *scratch * frames * CHANNELS);
	}
	for (int i = 0; i < ntracks; i++) {
		Track *t = tracks[i];
		if (!t || !t->playing)
			continue;
		uint32_t done = 0;
		int empty = 0;   /* reads that gave nothing in a row (an empty file mustn't loop for ever) */
		while (done < frames && empty < 2) {
			ma_uint64 want = frames - done, got = 0;
			ma_decoder_read_pcm_frames(&t->dec, scratch, want, &got);
			float vol = t->volume * (i == music ? music_volume * music_fade * music_rise
							    : i == old_music ? music_volume * old_fade : 1);
			for (uint64_t f = 0; f < got * CHANNELS; f++)
				out[done * CHANNELS + f] += scratch[f] * vol;
			done += (uint32_t)got;
			t->pos += got;
			empty = got ? 0 : empty + 1;
			if (got < want) {   /* the end */
				ma_decoder_seek_to_pcm_frame(&t->dec, 0);
				t->pos = 0;
				if (!t->looping) {
					t->playing = 0;
					break;
				}
			}
		}
	}
	/* notes */
	if (font)
		tsf_render_float(font, out, (int)frames, 1);
	synth_render(out, frames);
}

/* a track the music has finished with, to close later (with the lock) */
static void spend(int track)
{
	if (track >= 0 && nspent < 16)
		spent[nspent++] = track;
}

void cby_sound_track_close(int64_t id);

/* (the program's thread, without the lock) */
static void close_spent(void)
{
	for (;;) {
		pthread_mutex_lock(&lock);
		int t = nspent ? spent[--nspent] : -1;
		pthread_mutex_unlock(&lock);
		if (t < 0)
			break;
		cby_sound_track_close(t + 1);
	}
}

/* change_music's music, once the old has faded (with the lock) */
static void start_next(void)
{
	if (next_midi) {
		music_midi = next_midi;
		next_midi = NULL;
		queue_midi(music_midi, now, 2, music_bank);
	} else if (next_track >= 0 && tracks[next_track]) {
		music = next_track;
		tracks[music]->looping = 1;
		tracks[music]->playing = 1;
	}
	next_track = -1;
	free(music_path);
	music_path = next_path;
	next_path = NULL;
}

/* the old music of a crossfade, stopped (with the lock) */
static void old_off(void)
{
	old_frames = 0;
	old_fade = 1;
	if (old_midi)
		notes_off(3, old_bank);
	old_midi = 0;
	if (old_music >= 0 && tracks[old_music]) {
		tracks[old_music]->playing = 0;
		spend(old_music);
	}
	old_music = -1;
}

/* Mixes the next `frames` frames into out: everything playing, with each
 * event run at its own frame. */
static void mix(float *out, uint32_t frames)
{
	memset(out, 0, sizeof *out * frames * CHANNELS);
	if (fade_frames) {
		/* the music fading out: quieter each time, and stopped at the end */
		uint64_t gone = now - fade_start;
		music_fade = gone >= fade_frames ? 0 : 1 - (float)gone / (float)fade_frames;
		if (gone >= fade_frames) {
			fade_frames = 0;
			if (music >= 0 && tracks[music]) {
				tracks[music]->playing = 0;
				spend(music);
			}
			music = -1;
			if (music_midi) {
				tml_free(music_midi);
				music_midi = NULL;
				notes_off(2, music_bank);
			}
			music_loop_at = -1;
			music_fade = 1;
			free(music_path);
			music_path = NULL;
			start_next();   /* change_music's, if it's waiting */
		}
	}
	if (rise_frames) {
		uint64_t gone = now - rise_start;
		music_rise = gone >= rise_frames ? 1 : (float)gone / (float)rise_frames;
		if (gone >= rise_frames)
			rise_frames = 0;
	}
	if (old_frames) {
		uint64_t gone = now - old_start;
		old_fade = gone >= old_frames ? 0 : 1 - (float)gone / (float)old_frames;
		if (gone >= old_frames)
			old_off();
	}
	uint32_t done = 0;
	while (done < frames) {
		/* events due now */
		int k = 0;
		while (k < nevents && events[k].at <= now)
			run_event(&events[k++]);
		if (k) {
			memmove(events, events + k, sizeof *events * (size_t)(nevents - k));
			nevents -= k;
		}
		if (music_loop_at >= 0) {
			uint64_t at = (uint64_t)music_loop_at;
			music_loop_at = -1;
			if (music_midi)
				queue_midi(music_midi, at > now ? at : now, 2, music_bank);
			continue;   /* (its first notes may be due already) */
		}
		uint32_t part = frames - done;
		if (nevents && events[0].at - now < part)
			part = (uint32_t)(events[0].at - now);
		mix_part(out + done * CHANNELS, part);
		done += part;
		now += part;
	}
	if (mon_count)
		monitor_out(out, frames);
	for (uint32_t i = 0; i < frames * CHANNELS; i++) {
		float v = out[i] * master;
		out[i] = v > 1 ? 1 : v < -1 ? -1 : v;
	}
	/* clips that ended can go */
	int k = 0;
	for (int i = 0; i < nvoices; i++)
		if (voices[i].alive)
			voices[k++] = voices[i];
	nvoices = k;
	if (getenv("CABBY_SOUND_OUT")) {
		if (nkept + frames > capkept) {
			capkept = (nkept + frames) * 2;
			kept = realloc(kept, sizeof *kept * capkept * CHANNELS);
		}
		memcpy(kept + nkept * CHANNELS, out, sizeof *out * frames * CHANNELS);
		nkept += frames;
	}
}

/* still playing something that will end (not looping music) */
static int busy(void)
{
	for (int i = 0; i < nvoices; i++)
		if (voices[i].alive)
			return 1;
	for (int i = 0; i < ntracks; i++)
		if (tracks[i] && tracks[i]->playing && !tracks[i]->looping)
			return 1;
	for (int i = 0; i < nevents; i++)
		if (events[i].midi != 2)
			return 1;   /* (music from a MIDI file loops: it doesn't count) */
	if (music_midi)
		return 0;   /* (and the notes sounding are its, or nearly over) */
	if (font && tsf_active_voice_count(font) > 0)
		return 1;
	return synth_busy();
}

/* ------------------------------------------------------------- clips */

/* Clips are added by the program's thread, or a worker's: clip_lock keeps
 * two from adding at once (the mix reads them with `lock`). */
static pthread_mutex_t clip_lock = PTHREAD_MUTEX_INITIALIZER;

/* a clip of its own, for sound that isn't a file (a recording): its number */
static int add_clip(const char *name, float *pcm, uint64_t frames)
{
	pthread_mutex_lock(&clip_lock);
	pthread_mutex_lock(&lock);
	clips = realloc(clips, sizeof *clips * (size_t)(nclips + 1));
	clips[nclips] = (Clip){ strdup(name), pcm, frames };
	int n = nclips++;
	pthread_mutex_unlock(&lock);
	pthread_mutex_unlock(&clip_lock);
	return n;
}

static int clip(const char *path)
{
	pthread_mutex_lock(&clip_lock);
	for (int i = 0; i < nclips; i++)
		if (strcmp(clips[i].path, path) == 0) {
			pthread_mutex_unlock(&clip_lock);
			return i;
		}
	ma_decoder_config c = ma_decoder_config_init(ma_format_f32, CHANNELS, RATE);
	ma_uint64 frames = 0;
	void *pcm = NULL;
	if (ma_decode_file(path, &c, &frames, &pcm) != MA_SUCCESS) {
		pthread_mutex_unlock(&clip_lock);
		fail("can't play '%s': it isn't there, or isn't a sound file (WAV, MP3 or FLAC)", path);
	}
	pthread_mutex_lock(&lock);
	clips = realloc(clips, sizeof *clips * (size_t)(nclips + 1));
	clips[nclips] = (Clip){ strdup(path), pcm, frames };
	int n = nclips++;
	pthread_mutex_unlock(&lock);
	pthread_mutex_unlock(&clip_lock);
	return n;
}

void cby_sound_play(const char *path)
{
	setup();
	int c = clip(path);
	pthread_mutex_lock(&lock);
	voices = realloc(voices, sizeof *voices * (size_t)(nvoices + 1));
	voices[nvoices++] = (Voice){ c, 0, 1 };
	pthread_mutex_unlock(&lock);
}

/* ------------------------------------------------------------- tracks */

int64_t cby_sound_load(const char *path)
{
	setup();
	Track *t = calloc(1, sizeof *t);
	t->used = 1;
	t->volume = 1;
	ma_decoder_config c = ma_decoder_config_init(ma_format_f32, CHANNELS, RATE);
	if (ma_decoder_init_file(path, &c, &t->dec) != MA_SUCCESS) {
		free(t);
		fail("can't load '%s': it isn't there, or isn't a sound file (WAV, MP3 or FLAC)", path);
	}
	ma_uint64 len = 0;
	if (ma_decoder_get_length_in_pcm_frames(&t->dec, &len) == MA_SUCCESS)
		t->length = len;
	t->path = strdup(path);
	pthread_mutex_lock(&lock);
	int i = 0;
	while (i < ntracks && tracks[i])
		i++;
	if (i == ntracks) {
		tracks = realloc(tracks, sizeof *tracks * (size_t)(ntracks + 1));
		ntracks++;
	}
	tracks[i] = t;
	pthread_mutex_unlock(&lock);
	return i + 1;
}

static Track *track(int64_t id)
{
	pthread_mutex_lock(&lock);
	Track *t = id >= 1 && id <= ntracks ? tracks[id - 1] : NULL;
	pthread_mutex_unlock(&lock);
	if (!t)
		fail("%s", "this sound hasn't been loaded (sound.load gives one)");
	return t;
}

void cby_sound_track_play(int64_t id)
{
	pthread_mutex_lock(&lock);
	track(id)->playing = 1;
	pthread_mutex_unlock(&lock);
}

void cby_sound_track_pause(int64_t id)
{
	pthread_mutex_lock(&lock);
	track(id)->playing = 0;
	pthread_mutex_unlock(&lock);
}

void cby_sound_track_stop(int64_t id)
{
	pthread_mutex_lock(&lock);
	Track *t = track(id);
	t->playing = 0;
	ma_decoder_seek_to_pcm_frame(&t->dec, 0);
	t->pos = 0;
	pthread_mutex_unlock(&lock);
}

void cby_sound_track_set_looping(int64_t id, int on)
{
	pthread_mutex_lock(&lock);
	track(id)->looping = on != 0;
	pthread_mutex_unlock(&lock);
}

void cby_sound_track_set_volume(int64_t id, double v)
{
	pthread_mutex_lock(&lock);
	track(id)->volume = (float)(v < 0 ? 0 : v);
	pthread_mutex_unlock(&lock);
}

int cby_sound_track_playing(int64_t id)
{
	pthread_mutex_lock(&lock);
	int p = track(id)->playing;
	pthread_mutex_unlock(&lock);
	return p;
}

double cby_sound_track_seconds(int64_t id) { return (double)track(id)->length / RATE; }

double cby_sound_track_position(int64_t id)
{
	pthread_mutex_lock(&lock);
	double p = (double)track(id)->pos / RATE;
	pthread_mutex_unlock(&lock);
	return p;
}

void cby_sound_track_move_to(int64_t id, double seconds)
{
	pthread_mutex_lock(&lock);
	Track *t = track(id);
	uint64_t f = seconds <= 0 ? 0 : (uint64_t)(seconds * RATE);
	if (t->length && f > t->length)
		f = t->length;
	ma_decoder_seek_to_pcm_frame(&t->dec, f);
	t->pos = f;
	pthread_mutex_unlock(&lock);
}

void cby_sound_track_close(int64_t id)
{
	pthread_mutex_lock(&lock);
	if (id >= 1 && id <= ntracks && tracks[id - 1]) {
		Track *t = tracks[id - 1];
		ma_decoder_uninit(&t->dec);
		free(t->path);
		free(t);
		tracks[id - 1] = NULL;
		if (music == id - 1)
			music = -1;
	}
	pthread_mutex_unlock(&lock);
}

static void ready_for_notes(void);
static uint64_t soon(void);

static int is_midi_file(const char *path)
{
	size_t n = strlen(path);
	return (n > 4 && !strcasecmp(path + n - 4, ".mid")) || (n > 5 && !strcasecmp(path + n - 5, ".midi"));
}

/* the music that's playing, stopped, and any waiting or fading out (with
 * the lock; the tracks are closed afterwards, by close_spent) */
static void music_off(void)
{
	spend(music);
	music = -1;
	if (music_midi) {
		tml_free(music_midi);
		music_midi = NULL;
		notes_off(2, music_bank);
	}
	music_loop_at = -1;
	fade_frames = 0;
	music_fade = 1;
	rise_frames = 0;
	music_rise = 1;
	old_off();
	if (next_midi)
		tml_free(next_midi);
	next_midi = NULL;
	spend(next_track);
	next_track = -1;
	free(next_path);
	next_path = NULL;
	free(music_path);
	music_path = NULL;
}

/* music: one track (or MIDI file), looping, replaced by the next */
void cby_sound_play_music(const char *path)
{
	if (is_midi_file(path)) {
		setup();
		tml_message *m = tml_load_filename(path);
		if (!m)
			fail("can't play '%s': it isn't there, or isn't a MIDI file", path);
		pthread_mutex_lock(&lock);
		music_off();
		ready_for_notes();
		music_midi = m;
		queue_midi(m, soon(), 2, music_bank);
		music_path = strdup(path);
		pthread_mutex_unlock(&lock);
		close_spent();
		return;
	}
	int64_t id = cby_sound_load(path);
	pthread_mutex_lock(&lock);
	music_off();
	music = (int)id - 1;
	tracks[music]->looping = 1;
	tracks[music]->playing = 1;
	music_path = strdup(path);
	pthread_mutex_unlock(&lock);
	close_spent();
}

/* whether music is playing (and not on its way out), with the lock */
static int music_on(void)
{
	return (music_midi || (music >= 0 && tracks[music] && tracks[music]->playing)) && !fade_frames;
}

/* new music, loaded and ready, as change_music and crossfade_music need it:
 * a MIDI file's messages, or a track (outside the lock) */
static void load_music(const char *path, tml_message **m, int *track)
{
	setup();
	*m = NULL;
	*track = -1;
	if (is_midi_file(path)) {
		*m = tml_load_filename(path);
		if (!*m)
			fail("can't play '%s': it isn't there, or isn't a MIDI file", path);
	} else {
		*track = (int)cby_sound_load(path) - 1;
	}
}

/* the old music fades out over ms milliseconds, then the new starts
 * (the same music already playing goes on) */
void cby_sound_change_music(const char *path, int64_t ms)
{
	if (ms < 0)
		fail("%s", "a fade takes 0 milliseconds or more");
	pthread_mutex_lock(&lock);
	int same = (music_path && strcmp(music_path, path) == 0 && !fade_frames) ||
		   (next_path && strcmp(next_path, path) == 0);
	int playing = music_on() || fade_frames;
	pthread_mutex_unlock(&lock);
	if (same)
		return;
	if (!playing || ms == 0) {
		cby_sound_play_music(path);
		return;
	}
	tml_message *m;
	int track;
	load_music(path, &m, &track);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	if (next_midi)
		tml_free(next_midi);
	spend(next_track);
	free(next_path);
	next_midi = m;
	next_track = track;
	next_path = strdup(path);
	if (!fade_frames) {
		fade_start = now;
		fade_frames = (uint64_t)ms * RATE / 1000;
	}
	pthread_mutex_unlock(&lock);
	close_spent();
}

/* the new music gets louder over ms milliseconds as the old gets quieter */
void cby_sound_crossfade_music(const char *path, int64_t ms)
{
	if (ms < 0)
		fail("%s", "a fade takes 0 milliseconds or more");
	pthread_mutex_lock(&lock);
	int same = music_path && strcmp(music_path, path) == 0 && !fade_frames;
	int playing = music_on();
	pthread_mutex_unlock(&lock);
	if (same)
		return;
	if (!playing || ms == 0) {
		cby_sound_play_music(path);
		return;
	}
	tml_message *m;
	int track;
	load_music(path, &m, &track);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	old_off();                    /* (a crossfade still going: its old music goes now) */
	if (next_midi)
		tml_free(next_midi);
	next_midi = NULL;
	spend(next_track);
	next_track = -1;
	free(next_path);
	next_path = NULL;
	/* what's playing becomes the old music */
	old_music = music;
	old_midi = music_midi != NULL;
	old_bank = music_bank;
	for (int i = 0; i < nevents; i++)
		if (events[i].midi == 2)
			events[i].midi = 3;
	if (music_midi)
		tml_free(music_midi);
	music_midi = NULL;
	music = -1;
	music_loop_at = -1;
	old_start = now;
	old_frames = (uint64_t)ms * RATE / 1000;
	old_fade = music_fade * music_rise;
	fade_frames = 0;
	music_fade = 1;
	/* and the new one comes in */
	if (m) {
		if (old_midi)
			music_bank = 1 - music_bank;
		music_midi = m;
		queue_midi(m, soon(), 2, music_bank);
	} else {
		music = track;
		tracks[music]->looping = 1;
		tracks[music]->playing = 1;
	}
	music_rise = 0;
	rise_start = now;
	rise_frames = old_frames;
	free(music_path);
	music_path = strdup(path);
	pthread_mutex_unlock(&lock);
	close_spent();
}

/* the music's volume, from 0 to 1 (the effects and notes aren't changed) */
void cby_sound_set_music_volume(double v)
{
	if (v < 0 || v > 1) {
		char b[32];
		snprintf(b, sizeof b, "%g", v);
		fail("a volume is from 0 to 1, not %s", b);
	}
	pthread_mutex_lock(&lock);
	music_volume = (float)v;
	pthread_mutex_unlock(&lock);
}

double cby_sound_music_volume(void) { return music_volume; }

/* how loud sound.play's sounds are, from 0 to 1 (music and notes aren't changed) */
void cby_sound_set_effects_volume(double v)
{
	if (v < 0 || v > 1) {
		char b[32];
		snprintf(b, sizeof b, "%g", v);
		fail("a volume is from 0 to 1, not %s", b);
	}
	pthread_mutex_lock(&lock);
	effects_volume = (float)v;
	pthread_mutex_unlock(&lock);
}

double cby_sound_effects_volume(void) { return effects_volume; }

/* the music gets quieter over ms milliseconds, and stops */
void cby_sound_fade_out_music(int64_t ms)
{
	if (ms < 0)
		fail("%s", "a fade takes 0 milliseconds or more");
	pthread_mutex_lock(&lock);
	if (music >= 0 || music_midi) {
		fade_start = now;
		fade_frames = (uint64_t)ms * RATE / 1000;
		if (!fade_frames) {
			music_off();
			pthread_mutex_unlock(&lock);
			close_spent();
			return;
		}
	}
	pthread_mutex_unlock(&lock);
}

void cby_sound_stop_music(void)
{
	pthread_mutex_lock(&lock);
	music_off();
	pthread_mutex_unlock(&lock);
	close_spent();
}

/* ------------------------------------------------------------- notes */

static const struct { const char *name; int program; } programs[] = {
	{ "piano", 0 }, { "bright piano", 1 }, { "electric piano", 4 }, { "harpsichord", 6 },
	{ "glockenspiel", 9 }, { "music box", 10 }, { "vibraphone", 11 }, { "marimba", 12 },
	{ "xylophone", 13 }, { "organ", 19 }, { "accordion", 21 }, { "harmonica", 22 },
	{ "guitar", 24 }, { "steel guitar", 25 }, { "jazz guitar", 26 }, { "electric guitar", 27 },
	{ "overdriven guitar", 29 }, { "distortion guitar", 30 }, { "bass", 32 }, { "electric bass", 33 },
	{ "violin", 40 }, { "viola", 41 }, { "cello", 42 }, { "double bass", 43 }, { "harp", 46 },
	{ "timpani", 47 }, { "strings", 48 }, { "choir", 52 }, { "trumpet", 56 }, { "trombone", 57 },
	{ "tuba", 58 }, { "french horn", 60 }, { "brass", 61 }, { "saxophone", 65 }, { "oboe", 68 },
	{ "bassoon", 70 }, { "clarinet", 71 }, { "piccolo", 72 }, { "flute", 73 }, { "recorder", 74 },
	{ "pan flute", 75 }, { "sitar", 104 }, { "banjo", 105 }, { "steel drums", 114 },
};

/* an instrument's General MIDI program (0 to 127), -2 for drums, or a panic
 * (outside the lock: a panic ends the program, and the device must still
 * be able to stop) */
static int program_of(const char *name)
{
	if (strcmp(name, "drums") == 0)
		return -2;
	for (size_t i = 0; i < sizeof programs / sizeof *programs; i++)
		if (strcmp(programs[i].name, name) == 0)
			return programs[i].program;
	char *end;
	long n = strtol(name, &end, 10);
	if (*name && !*end && n >= 1 && n <= 128)
		return (int)n - 1;   /* General MIDI's numbers, 1 to 128 */
	fail("'%s' isn't an instrument sound knows: try \"piano\", \"guitar\", \"violin\", \"flute\", "
	     "\"drums\" (sound.md has them all), or a General MIDI number from 1 to 128", name);
}

/* the channel an instrument plays on (made the first time it's used; the
 * name has been checked by program_of) */
static int channel_for(const char *name)
{
	for (int i = 0; i < ninstruments; i++)
		if (strcmp(instruments[i].name, name) == 0)
			return instruments[i].channel;
	int program = program_of(name), drums = program == -2;
	if (ninstruments == 32)
		return 15;   /* (more than anyone needs: the last ones share) */
	int ch = drums ? 9 : 16 + ninstruments;
	instruments[ninstruments].name = strdup(name);
	instruments[ninstruments++].channel = ch;
	if (font)
		tsf_channel_set_presetnumber(font, ch, drums ? 0 : program, drums);
	return ch;
}

/* "C4" is middle C (60), "A4" 440 Hz (69); sharps with #, flats with b;
 * a plain number is a MIDI note number */
static int note_number(const char *s)
{
	const char *p = s;
	while (*p == ' ')
		p++;
	char *end;
	long n = strtol(p, &end, 10);
	if (end != p && !*end && n >= 0 && n <= 127)
		return (int)n;
	static const int steps[] = { 9, 11, 0, 2, 4, 5, 7 };   /* A to G */
	char l = (char)(*p >= 'a' && *p <= 'z' ? *p - 'a' + 'A' : *p);
	if (l < 'A' || l > 'G')
		fail("'%s' isn't a note: write it as a letter and an octave, like \"C4\" (middle C), \"F#3\" or \"Bb5\"",
		     s);
	int key = steps[l - 'A'];
	p++;
	while (*p == '#' || *p == 'b') {
		key += *p == '#' ? 1 : -1;
		p++;
	}
	n = strtol(p, &end, 10);
	if (end == p || *end)
		fail("'%s' isn't a note: it needs an octave, like \"C4\" (middle C) or \"A3\"", s);
	key += 12 * ((int)n + 1);
	if (key < 0 || key > 127)
		fail("'%s' is too high or too low to play", s);
	return key;
}

static void ready_for_notes(void)
{
	find_font();
	if (!instrument)
		instrument = strdup("piano");
}

void cby_sound_set_instrument(const char *name)
{
	setup();
	program_of(name);   /* (a panic now, if it isn't one) */
	pthread_mutex_lock(&lock);
	ready_for_notes();
	channel_for(name);
	free(instrument);
	instrument = strdup(name);
	pthread_mutex_unlock(&lock);
}

void cby_sound_set_note_volume(double v)
{
	note_volume = (float)(v < 0 ? 0 : v > 1 ? 1 : v);
}

/* notes start a moment from now, so live ones aren't late for the device */
static uint64_t soon(void)
{
	return now + (quiet ? 0 : RATE / 50);
}

static uint64_t note_end;      /* where play_notes carries on from */

static void note(uint64_t at, int key, int ms)
{
	int ch = channel_for(instrument);
	add_event(at, EV_ON, ch, key, (int)(note_volume * 127), 0);
	add_event(at + (uint64_t)ms * RATE / 1000, EV_OFF, ch, key, 0, 0);
}

void cby_sound_play_note(const char *name, int ms)
{
	setup();
	int key = note_number(name);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	note(soon(), key, ms < 1 ? 1 : ms);
	pthread_mutex_unlock(&lock);
}

/* start_note and stop_note: a note held as long as the program likes (a
 * key held down, say) */
static int held[128];          /* the channel each held note started on, plus 1 */

void cby_sound_start_note(const char *name)
{
	setup();
	int key = note_number(name);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	int ch = channel_for(instrument);
	held[key] = ch + 1;
	add_event(soon(), EV_ON, ch, key, (int)(note_volume * 127), 0);
	pthread_mutex_unlock(&lock);
}

void cby_sound_stop_note(const char *name)
{
	setup();
	int key = note_number(name);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	if (held[key])
		add_event(soon(), EV_OFF, held[key] - 1, key, 0, 0);
	held[key] = 0;
	pthread_mutex_unlock(&lock);
}

/* ---- a keyboard played through an instrument (midi.c): its notes go
 * straight into the mix from the keyboard's own thread, so they're heard
 * at once, whatever the program is doing */

/* (the program's thread) the instrument's channel, ready to play: a panic
 * here if it isn't one */
int sound_live_channel(const char *name)
{
	setup();
	program_of(name);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	int ch = channel_for(name);
	pthread_mutex_unlock(&lock);
	return ch;
}

/* where the mix is now, in frames (for a keyboard that's a test's MIDI
 * file, whose notes are put at their own times from here) */
int64_t sound_live_now(void)
{
	pthread_mutex_lock(&lock);
	int64_t n = (int64_t)now;
	pthread_mutex_unlock(&lock);
	return n;
}

/* a MIDI message, played on the channel: at a frame, or (-1) at once */
void sound_live_message(int channel, int status, int a, int b, int64_t frame)
{
	pthread_mutex_lock(&lock);
	uint64_t at = frame < 0 ? soon() : (uint64_t)frame;
	switch (status & 0xF0) {
	case 0x90:
		if (b > 0) {
			add_event(at, EV_ON, channel, a, (int)(b * note_volume / 0.8f > 127 ? 127 : b * note_volume / 0.8f), 0);
			break;
		}
		/* (a note on with no velocity is a note off) */
		/* fall through */
	case 0x80:
		add_event(at, EV_OFF, channel, a, 0, 0);
		break;
	case 0xB0:
		add_event(at, EV_CONTROL, channel, a, b, 0);
		break;
	case 0xE0:
		add_event(at, EV_PITCH, channel, a | (b << 7), 0, 0);
		break;
	}
	pthread_mutex_unlock(&lock);
}

/* every note of the channel let go, and the pedal up (a keyboard closed) */
void sound_live_end(int channel)
{
	pthread_mutex_lock(&lock);
	uint64_t at = soon();
	add_event(at, EV_CONTROL, channel, 64, 0, 0);
	for (int key = 0; key < 128; key++)
		add_event(at, EV_OFF, channel, key, 0, 0);
	pthread_mutex_unlock(&lock);
}

/* play_chord: the notes, one at a time from Cabby, all at the same moment */
static uint64_t chord_at;
static int chord_started;

void cby_sound_chord_note(const char *name, int ms)
{
	setup();
	int key = note_number(name);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	if (!chord_started) {
		chord_started = 1;
		chord_at = soon();
	}
	note(chord_at, key, ms < 1 ? 1 : ms);
	pthread_mutex_unlock(&lock);
}

void cby_sound_chord_end(void) { chord_started = 0; }

/* play_notes "C4 E4 G4 - C5": one after another, each Ms long; - is a rest.
 * A tune played straight after another follows on from it. */
void cby_sound_play_notes(const char *tune, int ms)
{
	setup();
	/* every note checked first */
	for (const char *q = tune;;) {
		char w[16];
		while (*q == ' ' || *q == ',')
			q++;
		if (!*q)
			break;
		size_t n = 0;
		while (q[n] && q[n] != ' ' && q[n] != ',')
			n++;
		if (n >= sizeof w)
			fail("'%s' has something in it that isn't a note", tune);
		memcpy(w, q, n);
		w[n] = '\0';
		q += n;
		if (strcmp(w, "-") != 0)
			note_number(w);
	}
	pthread_mutex_lock(&lock);
	ready_for_notes();
	if (ms < 1)
		ms = 1;
	uint64_t at = note_end > soon() ? note_end : soon();
	uint64_t each = (uint64_t)ms * RATE / 1000;
	char word[16];
	const char *p = tune;
	for (;;) {
		while (*p == ' ' || *p == ',')
			p++;
		if (!*p)
			break;
		size_t n = 0;
		while (p[n] && p[n] != ' ' && p[n] != ',')
			n++;
		if (n >= sizeof word)
			n = sizeof word - 1;
		memcpy(word, p, n);
		word[n] = '\0';
		p += n;
		if (strcmp(word, "-") != 0) {
			int key = note_number(word);
			int ch = channel_for(instrument);
			add_event(at, EV_ON, ch, key, (int)(note_volume * 127), 0);
			add_event(at + each - each / 10, EV_OFF, ch, key, 0, 0);   /* a little gap before the next */
		}
		at += each;
	}
	note_end = at;
	pthread_mutex_unlock(&lock);
}

/* ------------------------------------------------------------- MIDI files */

/* a MIDI file's messages as events, from the frame start (with the lock) */
static void queue_midi(tml_message *m, uint64_t start, int midi, int bank)
{
	unsigned int last = 0;
	for (tml_message *e = m; e; e = e->next) {
		uint64_t at = start + (uint64_t)e->time * RATE / 1000;
		int ch = e->channel + 48 * bank;
		last = e->time;
		switch (e->type) {
		case TML_NOTE_ON:
			add_event(at, e->velocity ? EV_ON : EV_OFF, ch, e->key, e->velocity, midi);
			break;
		case TML_NOTE_OFF:
			add_event(at, EV_OFF, ch, e->key, 0, midi);
			break;
		case TML_PROGRAM_CHANGE:
			add_event(at, EV_PROGRAM, ch, e->program, 0, midi);
			break;
		case TML_CONTROL_CHANGE:
			add_event(at, EV_CONTROL, ch, e->control, e->control_value, midi);
			break;
		case TML_PITCH_BEND:
			add_event(at, EV_PITCH, ch, e->pitch_bend, 0, midi);
			break;
		}
	}
	/* (a moment after the last message, so a looping tune doesn't run its
	 * end into its start) */
	add_event(start + (uint64_t)last * RATE / 1000 + RATE / 20, EV_END, 0, 0, 0, midi);
	if (font)
		tsf_channel_set_presetnumber(font, 9 + 48 * bank, 0, 1);   /* channel 10 is drums */
}

/* a MIDI file's events out of the queue, and its notes ended (with the lock) */
static void notes_off(int midi, int bank)
{
	int k = 0;
	for (int i = 0; i < nevents; i++)
		if (events[i].midi != midi)
			events[k++] = events[i];
	nevents = k;
	for (int ch = 48 * bank; ch < 48 * bank + 16; ch++) {
		if (font)
			tsf_channel_note_off_all(font, ch);
		for (int i = 0; i < 64; i++)
			if (synths[i].alive && synths[i].channel == ch && !synths[i].released)
				synths[i].released = now ? now : 1;
	}
}

void cby_sound_play_midi(const char *path)
{
	setup();
	tml_message *m = tml_load_filename(path);
	if (!m)
		fail("can't play '%s': it isn't there, or isn't a MIDI file", path);
	pthread_mutex_lock(&lock);
	ready_for_notes();
	queue_midi(m, soon(), 1, 0);
	pthread_mutex_unlock(&lock);
	tml_free(m);
}

void cby_sound_stop_midi(void)
{
	pthread_mutex_lock(&lock);
	notes_off(1, 0);
	pthread_mutex_unlock(&lock);
}

/* the SoundFont to play notes and MIDI files with */
void cby_sound_set_soundfont(const char *path)
{
	tsf *f = tsf_load_filename(path);
	if (!f)
		fail("can't use '%s': it isn't there, or isn't a SoundFont (.sf2)", path);
	tsf_set_output(f, TSF_STEREO_INTERLEAVED, RATE, 0);
	tsf_set_max_voices(f, 128);
	pthread_mutex_lock(&lock);
	setup();
	font_looked = 1;
	tsf *old = font;
	font = f;
	free(font_path);
	font_path = strdup(path);
	for (int i = 0; i < ninstruments; i++) {   /* the instruments again, from the new font */
		int drums = instruments[i].channel == 9, program = 0;
		for (size_t k = 0; k < sizeof programs / sizeof *programs; k++)
			if (strcmp(programs[k].name, instruments[i].name) == 0)
				program = programs[k].program;
		if (!drums && atoi(instruments[i].name) >= 1)
			program = atoi(instruments[i].name) - 1;
		tsf_channel_set_presetnumber(font, instruments[i].channel, program, drums);
	}
	pthread_mutex_unlock(&lock);
	if (old)
		tsf_close(old);
}

int64_t cby_sound_soundfont_length(void)
{
	pthread_mutex_lock(&lock);
	find_font();
	pthread_mutex_unlock(&lock);
	return font_path ? (int64_t)strlen(font_path) : 0;
}

char cby_sound_soundfont_char(int64_t i)
{
	return font_path && i >= 0 && i < (int64_t)strlen(font_path) ? font_path[i] : 0;
}

/* ------------------------------------------------------------- all of it */

void cby_sound_set_volume(double v)
{
	pthread_mutex_lock(&lock);
	master = (float)(v < 0 ? 0 : v);
	pthread_mutex_unlock(&lock);
}

void cby_sound_stop(void)
{
	pthread_mutex_lock(&lock);
	nvoices = 0;
	nevents = 0;
	note_end = 0;
	for (int i = 0; i < ntracks; i++)
		if (tracks[i] && tracks[i]->playing) {
			tracks[i]->playing = 0;
			ma_decoder_seek_to_pcm_frame(&tracks[i]->dec, 0);
			tracks[i]->pos = 0;
		}
	if (font)
		tsf_note_off_all(font);
	for (int i = 0; i < 64; i++)
		synths[i].alive = 0;
	pthread_mutex_unlock(&lock);
}

int cby_sound_busy(void)
{
	pthread_mutex_lock(&lock);
	int b = busy();
	pthread_mutex_unlock(&lock);
	return b;
}

/* Waits until everything that will end has (music that loops goes on).
 * Without a device, it mixes the rest now, as fast as it can. */
void cby_sound_wait(void)
{
	setup();
	if (quiet) {
		float buf[1024 * CHANNELS];
		pthread_mutex_lock(&lock);
		for (uint64_t limit = (uint64_t)RATE * 3600; busy() && limit > 1024; limit -= 1024)
			mix(buf, 1024);
		pthread_mutex_unlock(&lock);
		return;
	}
	for (;;) {
		pthread_mutex_lock(&lock);
		int b = busy();
		pthread_mutex_unlock(&lock);
		if (!b)
			break;
		ma_sleep(10);
	}
	ma_sleep(60);   /* the device's last moment, played out */
}

/* how long it has been mixing, in seconds */
double cby_sound_time(void)
{
	pthread_mutex_lock(&lock);
	double t = (double)now / RATE;
	pthread_mutex_unlock(&lock);
	return t;
}

/* ------------------------------------------------------------- songs */

/* A song, as sound.song (in sound.hby) sends it, a part at a time, made
 * into a MIDI file's bytes: format 1, a track for the tempo and the time
 * signature, then a track for each part. Saved, or played as play_midi
 * plays a file. */

#define TICKS 480              /* to a beat */

typedef struct {
	int64_t tick;
	int seq;                   /* (the order they came in, to keep it) */
	int n;
	uint8_t msg[3];
} SongEvent;

typedef struct {
	char *name;
	int channel;
	SongEvent *ev;
	int nev, cap;
} SongPart;

/* (each thread's own: a worker can write a song while the program reads one) */
static _Thread_local SongPart *sparts;
static _Thread_local int nsparts;
static _Thread_local double song_tempo = 120;
static _Thread_local int song_top = 4, song_bottom = 4;
static _Thread_local int64_t song_end;

static void song_clear(void)
{
	for (int i = 0; i < nsparts; i++) {
		free(sparts[i].name);
		free(sparts[i].ev);
	}
	free(sparts);
	sparts = NULL;
	nsparts = 0;
}

static void songs_kept(void);

void cby_sound_song_start(double tempo, int64_t top, int64_t bottom, double beats)
{
	songs_kept();
	song_clear();
	if (tempo <= 0)
		tempo = 120;
	song_tempo = tempo;
	song_top = top > 0 ? (int)top : 4;
	song_bottom = bottom > 0 ? (int)bottom : 4;
	song_end = beats > 0 ? (int64_t)llround(beats * TICKS) : 0;
}

static void song_event(SongPart *p, int64_t tick, int n, int a, int b, int c)
{
	if (p->nev == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 64;
		p->ev = realloc(p->ev, sizeof *p->ev * (size_t)p->cap);
	}
	SongEvent *e = &p->ev[p->nev];
	e->tick = tick < 0 ? 0 : tick;
	e->seq = p->nev++;
	e->n = n;
	e->msg[0] = (uint8_t)a;
	e->msg[1] = (uint8_t)b;
	e->msg[2] = (uint8_t)c;
	if (e->tick > song_end)
		song_end = e->tick;
}

void cby_sound_song_part(const char *instrument)
{
	int program = program_of(instrument), channel = 9;
	if (program != -2) {
		channel = 0;
		for (int i = 0; i < nsparts; i++)
			if (sparts[i].channel != 9)
				channel++;
		if (channel >= 9)
			channel++;   /* (10, MIDI's channel 9, is the drums') */
		if (channel > 15)
			fail("a song can have 15 parts%s, and drums: MIDI has no more channels than that", "");
	}
	sparts = realloc(sparts, sizeof *sparts * (size_t)(nsparts + 1));
	SongPart *p = &sparts[nsparts++];
	memset(p, 0, sizeof *p);
	p->name = strdup(instrument);
	p->channel = channel;
	if (program >= 0)
		song_event(p, 0, 2, 0xC0 | channel, program, 0);
}

void cby_sound_song_note(int64_t key, double beat, double beats, double volume)
{
	if (!nsparts)
		return;
	SongPart *p = &sparts[nsparts - 1];
	int64_t on = (int64_t)llround(beat * TICKS), off = (int64_t)llround((beat + beats) * TICKS);
	if (off <= on)
		off = on + 1;
	int velocity = (int)lround(volume * 127);
	velocity = velocity < 1 ? 1 : velocity > 127 ? 127 : velocity;
	song_event(p, on, 3, 0x90 | p->channel, (int)key, velocity);
	song_event(p, off, 3, 0x80 | p->channel, (int)key, 0);
}

/* at the same moment, a note's end before the next note's start */
static int song_order(const void *x, const void *y)
{
	const SongEvent *a = x, *b = y;
	if (a->tick != b->tick)
		return a->tick < b->tick ? -1 : 1;
	int ka = (a->msg[0] & 0xF0) == 0x90, kb = (b->msg[0] & 0xF0) == 0x90;
	if (ka != kb)
		return ka - kb;
	return a->seq - b->seq;
}

typedef struct {
	unsigned char *b;
	size_t n, cap;
} Bytes;

static void put(Bytes *o, const void *data, size_t n)
{
	if (o->n + n > o->cap) {
		o->cap = (o->n + n) * 2 + 256;
		o->b = realloc(o->b, o->cap);
	}
	memcpy(o->b + o->n, data, n);
	o->n += n;
}

static void put_byte(Bytes *o, int c)
{
	unsigned char b = (unsigned char)c;
	put(o, &b, 1);
}

static void put_number(Bytes *o, uint32_t v, int size)
{
	for (int i = size - 1; i >= 0; i--)
		put_byte(o, (int)(v >> (8 * i)) & 0xFF);
}

static void put_varlen(Bytes *o, uint64_t v)
{
	unsigned char tmp[10];
	int n = 0;
	tmp[n++] = v & 0x7F;
	while (v >>= 7)
		tmp[n++] = 0x80 | (v & 0x7F);
	while (n)
		put_byte(o, tmp[--n]);
}

static void put_track(Bytes *o, const Bytes *t)
{
	put(o, "MTrk", 4);
	put_number(o, (uint32_t)t->n, 4);
	put(o, t->b, t->n);
}

static Bytes song_bytes(void)
{
	Bytes o = { 0 }, t = { 0 };
	put(&o, "MThd", 4);
	put_number(&o, 6, 4);
	put_number(&o, 1, 2);
	put_number(&o, (uint32_t)(nsparts + 1), 2);
	put_number(&o, TICKS, 2);

	/* the tempo, and the time signature */
	put_varlen(&t, 0);
	put(&t, "\xFF\x51\x03", 3);
	put_number(&t, (uint32_t)llround(60000000.0 / song_tempo), 3);
	int power = 0;
	while ((1 << power) < song_bottom && power < 6)
		power++;
	put_varlen(&t, 0);
	put(&t, "\xFF\x58\x04", 3);
	put_byte(&t, song_top);
	put_byte(&t, power);
	put_byte(&t, 24);
	put_byte(&t, 8);
	put_varlen(&t, (uint64_t)song_end);
	put(&t, "\xFF\x2F\x00", 3);
	put_track(&o, &t);

	for (int i = 0; i < nsparts; i++) {
		SongPart *p = &sparts[i];
		t.n = 0;
		size_t len = strlen(p->name);   /* the part's name: its instrument */
		put_varlen(&t, 0);
		put(&t, "\xFF\x03", 2);
		put_varlen(&t, len);
		put(&t, p->name, len);
		qsort(p->ev, (size_t)p->nev, sizeof *p->ev, song_order);
		int64_t last = 0;
		for (int k = 0; k < p->nev; k++) {
			put_varlen(&t, (uint64_t)(p->ev[k].tick - last));
			put(&t, p->ev[k].msg, (size_t)p->ev[k].n);
			last = p->ev[k].tick;
		}
		put_varlen(&t, (uint64_t)(song_end - last));
		put(&t, "\xFF\x2F\x00", 3);
		put_track(&o, &t);
	}
	free(t.b);
	return o;
}

void cby_sound_song_save(const char *path)
{
	Bytes o = song_bytes();
	FILE *f = fopen(path, "wb");
	if (!f) {
		free(o.b);
		fail("can't save the song as '%s': its folder isn't there, or can't be written to", path);
	}
	fwrite(o.b, 1, o.n, f);
	fclose(f);
	free(o.b);
}

void cby_sound_song_play(void)
{
	setup();
	Bytes o = song_bytes();
	tml_message *m = tml_load_memory(o.b, (int)o.n);
	free(o.b);
	if (!m)
		return;
	pthread_mutex_lock(&lock);
	ready_for_notes();
	queue_midi(m, soon(), 1, 0);
	pthread_mutex_unlock(&lock);
	tml_free(m);
}

/* ---- reading a MIDI file into a song: its notes, by channel, in beats */

typedef struct {
	int key, velocity;
	int64_t on, off;
} ReadNote;

typedef struct {
	int channel, program;
	ReadNote *notes;
	int nnotes, cap;
} ReadPart;

static _Thread_local ReadPart *rparts;
static _Thread_local int nrparts;
static _Thread_local double read_tempo, read_beats;
static _Thread_local int read_top, read_bottom, read_division;
static _Thread_local char read_text[32];

static void read_clear(void)
{
	for (int i = 0; i < nrparts; i++)
		free(rparts[i].notes);
	free(rparts);
	rparts = NULL;
	nrparts = 0;
}

static ReadPart *read_part(int channel)
{
	for (int i = 0; i < nrparts; i++)
		if (rparts[i].channel == channel)
			return &rparts[i];
	rparts = realloc(rparts, sizeof *rparts * (size_t)(nrparts + 1));
	ReadPart *p = &rparts[nrparts++];
	memset(p, 0, sizeof *p);
	p->channel = channel;
	p->program = -1;
	return p;
}

static uint64_t get_varlen(const unsigned char **p, const unsigned char *end)
{
	uint64_t v = 0;
	for (int i = 0; i < 5 && *p < end; i++) {
		unsigned char c = *(*p)++;
		v = (v << 7) | (c & 0x7F);
		if (!(c & 0x80))
			break;
	}
	return v;
}

static int note_by_start(const void *x, const void *y)
{
	const ReadNote *a = x, *b = y;
	if (a->on != b->on)
		return a->on < b->on ? -1 : 1;
	return a->key - b->key;
}

/* reads the file; the number of parts (a part for each channel with notes) */
/* a thread's songs, written and read, let go as it ends (a worker's) */
static pthread_key_t song_key;
static pthread_once_t song_key_once = PTHREAD_ONCE_INIT;

static void songs_end(void *p)
{
	(void)p;
	song_clear();
	read_clear();
}

static void song_key_make(void) { pthread_key_create(&song_key, songs_end); }

static void songs_kept(void)
{
	pthread_once(&song_key_once, song_key_make);
	pthread_setspecific(song_key, (void *)1);
}

int64_t cby_sound_song_read(const char *path)
{
	songs_kept();
	read_clear();
	FILE *f = fopen(path, "rb");
	if (!f)
		fail("can't read the song '%s': it isn't there", path);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *data = malloc(size > 0 ? (size_t)size : 1);
	size_t got = fread(data, 1, size > 0 ? (size_t)size : 0, f);
	fclose(f);
	if (got < 14 || memcmp(data, "MThd", 4) != 0) {
		free(data);
		fail("can't read the song '%s': it isn't a MIDI file", path);
	}
	read_division = (data[12] << 8) | data[13];
	if (read_division & 0x8000 || read_division == 0) {
		free(data);
		fail("can't read the song '%s': its times are in frames of film, not beats", path);
	}
	read_tempo = 0;
	read_top = read_bottom = 0;
	int64_t end_tick = 0;
	int open[16][128];             /* each note started and not yet ended (+1) */
	const unsigned char *p = data + 8 + ((data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]);
	const unsigned char *all_end = data + got;
	while (p + 8 <= all_end) {
		uint32_t len = ((uint32_t)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
		const unsigned char *q = p + 8, *end = q + len;
		if (end > all_end)
			end = all_end;
		if (memcmp(p, "MTrk", 4) == 0) {
			memset(open, 0, sizeof open);
			int64_t tick = 0;
			int status = 0;
			while (q < end) {
				tick += (int64_t)get_varlen(&q, end);
				if (q >= end)
					break;
				if (*q & 0x80)
					status = *q++;
				if (status == 0xFF) {
					if (q >= end)
						break;
					int type = *q++;
					uint64_t n = get_varlen(&q, end);
					if (q + n > end)
						break;
					if (type == 0x51 && n == 3 && !read_tempo) {
						uint32_t us = ((uint32_t)q[0] << 16) | (q[1] << 8) | q[2];
						if (us)
							read_tempo = round(60000000.0 / us * 100) / 100;
					} else if (type == 0x58 && n >= 2 && !read_top) {
						read_top = q[0];
						read_bottom = 1 << (q[1] < 7 ? q[1] : 2);
					} else if (type == 0x2F && tick > end_tick) {
						end_tick = tick;
					}
					q += n;
					status = 0;
					continue;
				}
				if (status == 0xF0 || status == 0xF7) {
					uint64_t n = get_varlen(&q, end);
					q += n;
					status = 0;
					continue;
				}
				if (!status)
					break;     /* (a broken file: stop reading this track) */
				int kind = status & 0xF0, ch = status & 0x0F;
				int a = q < end ? *q++ : 0, b = 0;
				if (kind != 0xC0 && kind != 0xD0)
					b = q < end ? *q++ : 0;
				if (kind == 0xC0) {
					ReadPart *rp = read_part(ch);
					if (rp->program < 0)
						rp->program = a;
				} else if (kind == 0x90 && b > 0) {
					ReadPart *rp = read_part(ch);
					if (rp->nnotes == rp->cap) {
						rp->cap = rp->cap ? rp->cap * 2 : 64;
						rp->notes = realloc(rp->notes, sizeof *rp->notes * (size_t)rp->cap);
					}
					rp->notes[rp->nnotes] = (ReadNote){ a & 127, b, tick, -1 };
					open[ch][a & 127] = ++rp->nnotes;
				} else if (kind == 0x80 || kind == 0x90) {
					int k = open[ch][a & 127];
					if (k) {
						read_part(ch)->notes[k - 1].off = tick;
						open[ch][a & 127] = 0;
					}
				}
				if (tick > end_tick)
					end_tick = tick;
			}
		}
		p = end;
	}
	free(data);
	/* a part only for each channel with notes; notes never ended end with the song */
	int k = 0;
	for (int i = 0; i < nrparts; i++) {
		if (!rparts[i].nnotes) {
			free(rparts[i].notes);
			continue;
		}
		for (int n = 0; n < rparts[i].nnotes; n++)
			if (rparts[i].notes[n].off < 0)
				rparts[i].notes[n].off = end_tick;
		qsort(rparts[i].notes, (size_t)rparts[i].nnotes, sizeof *rparts[i].notes, note_by_start);
		rparts[k++] = rparts[i];
	}
	nrparts = k;
	if (!read_tempo)
		read_tempo = 120;
	if (!read_top)
		read_top = read_bottom = 4;
	read_beats = (double)end_tick / read_division;
	return nrparts;
}

double cby_sound_read_tempo(void) { return read_tempo; }
int64_t cby_sound_read_top(void) { return read_top; }
int64_t cby_sound_read_bottom(void) { return read_bottom; }
double cby_sound_read_beats(void) { return read_beats; }

/* a part's instrument, by name where sound has one, or its number */
int64_t cby_sound_read_instrument(int64_t part)
{
	ReadPart *p = &rparts[part];
	int program = p->program < 0 ? 0 : p->program;
	if (p->channel == 9) {
		snprintf(read_text, sizeof read_text, "drums");
	} else {
		snprintf(read_text, sizeof read_text, "%d", program + 1);
		for (size_t i = 0; i < sizeof programs / sizeof *programs; i++)
			if (programs[i].program == program) {
				snprintf(read_text, sizeof read_text, "%s", programs[i].name);
				break;
			}
	}
	return (int64_t)strlen(read_text);
}

char cby_sound_read_text_char(int64_t i)
{
	return i >= 0 && i < (int64_t)strlen(read_text) ? read_text[i] : 0;
}

int64_t cby_sound_read_notes(int64_t part) { return rparts[part].nnotes; }
int64_t cby_sound_read_key(int64_t part, int64_t i) { return rparts[part].notes[i].key; }

double cby_sound_read_beat(int64_t part, int64_t i)
{
	return (double)rparts[part].notes[i].on / read_division;
}

double cby_sound_read_length(int64_t part, int64_t i)
{
	ReadNote *n = &rparts[part].notes[i];
	return (double)(n->off - n->on) / read_division;
}

double cby_sound_read_volume(int64_t part, int64_t i)
{
	return rparts[part].notes[i].velocity / 127.0;
}

/* a note's MIDI number, from its name ("C4": 60) */
int64_t cby_sound_note_key(const char *name)
{
	return note_number(name);
}

/* (midi.c's) an instrument's General MIDI program, from 0 (-2: drums); a
 * panic if it isn't one */
int sound_program_of(const char *name)
{
	return program_of(name);
}

/* a panic if it isn't an instrument sound knows */
void cby_sound_check_instrument(const char *name)
{
	program_of(name);
}

/* everything kept for songs, given back at the end (for valgrind) */
__attribute__((destructor)) static void song_end_of_program(void)
{
	song_clear();
	read_clear();
}

/* a panic from sound.hby's Cabby code, said as sound's own are */
void cby_sound_panic(const char *message)
{
	fail("%s", message);
}

/* ------------------------------------------------------------- recording */

/* A recording: what comes in from a microphone or audio interface, kept in
 * memory as floats (48000 frames a second, one or two channels), to save
 * as a WAV file or play. One input records at a time: a second record
 * stops the first.
 *
 * The input is recorded as it comes, at its own rate and channels (a
 * webcam's microphone may be 16000 frames a second, in stereo), into a
 * small buffer the program's thread empties: the input's callback only
 * copies, and never waits long, or the input runs late and clicks. The
 * program's thread then converts it, as it asks how long the recording is,
 * or how loud, or saves it (settle).
 *
 * With CABBY_SOUND_IN=file.wav, that file is the input instead, and comes
 * in all at once (its level, for a meter, as if it were being played in);
 * with CABBY_SOUND=none and no CABBY_SOUND_IN, a recording is silent. */

typedef struct {
	float *pcm;                /* the recording: RATE frames a second, `channels` each */
	uint64_t frames, cap;
	int channels;
	int going, paused;
	uint64_t skip;             /* input frames still to drop: the delay before it's in time */
	float *raw, *spare;        /* what's come in, not yet converted (the callback's), and */
	uint64_t raw_frames, raw_cap, spare_cap;   /* the other buffer, to swap with it */
	int in_channels, in_rate;  /* the input's own */
	uint64_t received, calls;  /* frames the input has sent, and how many times */
	double opened, first_call, last_call, longest_gap;   /* (seconds: CABBY_SOUND_DEBUG says, and the guard) */
	uint64_t first_frames;     /* the first piece's */
	int runaway;               /* it sent far more than it could have: stopped */
	int runaway_said;
	ma_data_converter conv;
	int converting;            /* conv is ready: the recording is from an input */
	int from_file;
	double started;            /* (from a file: when, for the level) */
	int clip;                  /* played as this clip, while it hasn't changed (-1: none) */
	uint64_t clip_frames;
} Recording;

static pthread_mutex_t rec_lock = PTHREAD_MUTEX_INITIALIZER;
static Recording **recs;
static int nrecs;
static Recording *capturing;   /* the one the input goes into */
static ma_device input;
static int input_open;
static int rec_channels = 1, rec_bits = 24;
static int rec_delay = -1;     /* ms; -1: worked out from the devices */
static int input_warned;

static double seconds_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static void keep_frames(Recording *r, const float *f, uint64_t n)
{
	if (r->frames + n > r->cap) {
		r->cap = (r->frames + n) * 2 + RATE;
		r->pcm = realloc(r->pcm, sizeof *r->pcm * r->cap * (size_t)r->channels);
	}
	memcpy(r->pcm + r->frames * (size_t)r->channels, f, sizeof *f * n * (size_t)r->channels);
	r->frames += n;
}

/* the input's callback: what came in, copied into raw, as it is */
static void on_input(ma_device *d, void *out, const void *in, ma_uint32 frames)
{
	(void)d;
	(void)out;
	pthread_mutex_lock(&rec_lock);
	Recording *r = capturing;
	if (r && in) {
		double t = seconds_now();
		if (r->calls && t - r->last_call > r->longest_gap)
			r->longest_gap = t - r->last_call;
		if (!r->calls) {
			r->first_call = t;
			r->first_frames = frames;
		}
		r->last_call = t;
		r->calls++;
		r->received += frames;
		/* far more than it could have sent by now: something's wrong */
		if (r->received > (uint64_t)((t - r->opened + 2) * r->in_rate * 2))
			r->runaway = 1;
	}
	if (r && in && !r->paused && !r->runaway) {
		size_t ch = (size_t)r->in_channels;
		const float *f = in;
		uint64_t n = frames;
		if (r->skip) {
			uint64_t s = r->skip < n ? r->skip : n;
			f += s * ch;
			n -= s;
			r->skip -= s;
		}
		if (n && r->raw_frames + n > r->raw_cap) {
			/* (rarely: the program's thread empties it often) */
			r->raw_cap = (r->raw_frames + n) * 2 + (uint64_t)r->in_rate / 2;
			r->raw = realloc(r->raw, sizeof *r->raw * r->raw_cap * ch);
		}
		if (n) {
			memcpy(r->raw + r->raw_frames * ch, f, sizeof *f * n * ch);
			r->raw_frames += n;
		}
	}
	pthread_mutex_unlock(&rec_lock);
	if (in && monitoring)
		monitor_in(in, frames);   /* (heard paused too, and before it's in time) */
}

/* what has come in, converted onto the end of the recording (the program's
 * thread: the input's buffer is swapped for the spare one, and converted
 * without the lock) */
static void close_input(void);

static void settle(Recording *r)
{
	if (!r->converting)
		return;
	pthread_mutex_lock(&rec_lock);
	/* an input that sends far more than it could have (some ALSA plugins
	 * can, with miniaudio, as fast as they're asked): stopped, before it
	 * fills the computer's memory */
	double secs = seconds_now() - r->opened;
	int runaway = r->runaway && !r->runaway_said;
	if (runaway)
		r->runaway_said = 1;
	int mine = capturing == r;
	pthread_mutex_unlock(&rec_lock);
	if (runaway) {
		fprintf(stderr, "sound: the input sent %.0f seconds of sound in %.1f seconds, so the recording has stopped "
				"(try another input, or without CABBY_SOUND_SYSTEM)\n", (double)r->received / r->in_rate, secs);
		if (mine)
			close_input();
	}
	pthread_mutex_lock(&rec_lock);
	float *got = r->raw;
	uint64_t n = r->raw_frames, cap = r->raw_cap;
	r->raw = r->spare;
	r->raw_cap = r->spare_cap;
	r->raw_frames = 0;
	r->spare = NULL;
	r->spare_cap = 0;
	pthread_mutex_unlock(&rec_lock);
	float out[4096];
	uint64_t done = 0;
	while (done < n) {
		ma_uint64 in_frames = n - done, out_frames = 4096 / (uint64_t)r->channels;
		if (ma_data_converter_process_pcm_frames(&r->conv, got + done * (size_t)r->in_channels, &in_frames, out,
							 &out_frames) != MA_SUCCESS || (!in_frames && !out_frames))
			break;
		keep_frames(r, out, out_frames);
		done += in_frames;
	}
	pthread_mutex_lock(&rec_lock);
	if (!r->spare) {
		r->spare = got;
		r->spare_cap = cap;
	} else {
		free(got);
	}
	pthread_mutex_unlock(&rec_lock);
}

static void free_recording(Recording *r)
{
	if (r->converting)
		ma_data_converter_uninit(&r->conv, NULL);
	free(r->raw);
	free(r->spare);
	free(r->pcm);
	free(r);
}

/* (looked at with the lock: a worker may be adding one; and let go
 * before a panic, so it isn't left held) */
static Recording *recording(int64_t id)
{
	pthread_mutex_lock(&rec_lock);
	Recording *r = id >= 1 && id <= nrecs ? recs[id - 1] : NULL;
	pthread_mutex_unlock(&rec_lock);
	if (!r)
		fail("that recording has been dropped%s", "");
	return r;
}

/* the input stops (outside rec_lock: closing waits for its last call) */
static void close_input(void)
{
	pthread_mutex_lock(&rec_lock);
	if (capturing)
		capturing->going = 0;
	capturing = NULL;
	pthread_mutex_unlock(&rec_lock);
	if (input_open) {
		ma_device_uninit(&input);
		input_open = 0;
	}
	pthread_mutex_lock(&mon_lock);
	if (mon_conv_ready) {
		ma_data_converter_uninit(&mon_conv, NULL);
		mon_conv_ready = 0;
	}
	mon_read = mon_count = 0;
	pthread_mutex_unlock(&mon_lock);
}

/* frames a device holds before they're heard, or after they're played in */
static uint64_t device_delay(ma_device *d, int playback)
{
	ma_uint32 period = playback ? d->playback.internalPeriodSizeInFrames : d->capture.internalPeriodSizeInFrames;
	ma_uint32 periods = playback ? d->playback.internalPeriods : d->capture.internalPeriods;
	ma_uint32 rate = d->sampleRate ? d->sampleRate : RATE;
	return (uint64_t)period * periods * RATE / rate;
}

/* whether Name is in Text, big or small letters alike */
static int contains_text(const char *text, const char *name)
{
	size_t n = strlen(name);
	for (; *text; text++)
		if (strncasecmp(text, name, n) == 0)
			return 1;
	return n == 0;
}

static void start_monitoring_file(int64_t id);

static int64_t start_recording(const char *name)
{
	close_input();
	Recording *r = calloc(1, sizeof *r);
	r->channels = rec_channels;
	r->going = 1;
	r->clip = -1;
	const char *file = getenv("CABBY_SOUND_IN");
	const char *mode = getenv("CABBY_SOUND");
	if (file && *file) {
		ma_decoder_config c = ma_decoder_config_init(ma_format_f32, (ma_uint32)r->channels, RATE);
		ma_uint64 frames = 0;
		void *pcm = NULL;
		if (ma_decode_file(file, &c, &frames, &pcm) != MA_SUCCESS) {
			free(r);
			fail("can't record from CABBY_SOUND_IN's '%s': it isn't there, or isn't a sound file", file);
		}
		keep_frames(r, pcm, frames);
		ma_free(pcm, NULL);
		r->from_file = 1;
		r->started = seconds_now();
	} else if (!(mode && strcmp(mode, "none") == 0)) {
		ma_device_config c = ma_device_config_init(ma_device_type_capture);
		c.capture.format = ma_format_f32;
		c.capture.channels = 0;     /* (its own, and its own rate: converted afterwards) */
		c.sampleRate = 0;
		c.dataCallback = on_input;
		/* room for a moment's sound in the system's buffers (not the
		 * smallest it can do): a USB microphone that's a little late then
		 * doesn't lose any, which is heard as a click. Recording isn't
		 * heard as it goes, so the wait doesn't matter. */
		c.performanceProfile = ma_performance_profile_conservative;
		c.periodSizeInMilliseconds = 25;
		c.periods = 4;
		if (monitoring) {
			/* heard as it comes: smaller pieces, sooner (a slow USB
			 * microphone may click; recording without monitoring doesn't) */
			setup();
			c.performanceProfile = ma_performance_profile_low_latency;
			c.periodSizeInMilliseconds = 5;
			c.periods = 3;
		}
		ma_device_id id;
		int chosen = 0;
		if (name && get_context()) {
			ma_device_info *infos;
			ma_uint32 n;
			if (ma_context_get_devices(&context, NULL, NULL, &infos, &n) == MA_SUCCESS)
				for (ma_uint32 i = 0; i < n && !chosen; i++)
					if (contains_text(infos[i].name, name)) {
						id = infos[i].id;
						chosen = 1;
					}
			if (!chosen) {
				free(r);
				fail("there's no input called '%s' to record from: sound.inputs lists them", name);
			}
			c.capture.pDeviceID = &id;
		}
		if (get_context() && ma_device_init(&context, &c, &input) == MA_SUCCESS) {
			r->in_channels = (int)input.capture.channels;
			r->in_rate = (int)input.sampleRate;
			ma_data_converter_config cc = ma_data_converter_config_init(
				ma_format_f32, ma_format_f32, (ma_uint32)r->in_channels, (ma_uint32)r->channels,
				(ma_uint32)r->in_rate, RATE);
			if (r->in_channels > 0 && r->in_rate > 0 && ma_data_converter_init(&cc, NULL, &r->conv) == MA_SUCCESS) {
				r->converting = 1;
				ma_data_converter_config mc = ma_data_converter_config_init(
					ma_format_f32, ma_format_f32, (ma_uint32)r->in_channels, CHANNELS, (ma_uint32)r->in_rate, RATE);
				pthread_mutex_lock(&mon_lock);
				mon_conv_ready = ma_data_converter_init(&mc, NULL, &mon_conv) == MA_SUCCESS;
				mon_read = mon_count = 0;
				pthread_mutex_unlock(&mon_lock);
				r->opened = seconds_now();
				input_open = 1;
				if (getenv("CABBY_SOUND_DEBUG"))
					fprintf(stderr, "sound: recording from '%s' through %s: %d frames a second, %d channel%s, "
							"in pieces of %u frames, %u of them\n",
						input.capture.name, ma_get_backend_name(context.backend), r->in_rate,
						r->in_channels, r->in_channels == 1 ? "" : "s",
						input.capture.internalPeriodSizeInFrames, input.capture.internalPeriods);
				/* in time with what's playing: what's heard now was mixed
				 * a moment ago, and what's played along with it comes in
				 * a moment later */
				uint64_t delay = device_delay(&input, 0) + (started ? device_delay(&device, 1) : 0);
				if (rec_delay >= 0)
					delay = (uint64_t)rec_delay * RATE / 1000;
				r->skip = delay * (uint64_t)r->in_rate / RATE;
			} else {
				ma_device_uninit(&input);
			}
		}
	}
	pthread_mutex_lock(&rec_lock);
	int i = 0;
	while (i < nrecs && recs[i])
		i++;
	if (i == nrecs) {
		recs = realloc(recs, sizeof *recs * (size_t)(nrecs + 1));
		nrecs++;
	}
	recs[i] = r;
	if (input_open)
		capturing = r;
	pthread_mutex_unlock(&rec_lock);
	if (input_open && ma_device_start(&input) != MA_SUCCESS) {
		pthread_mutex_lock(&rec_lock);
		capturing = NULL;
		pthread_mutex_unlock(&rec_lock);
		ma_device_uninit(&input);
		input_open = 0;
	}
	if (!input_open && !r->from_file && !(mode && strcmp(mode, "none") == 0) && !input_warned) {
		input_warned = 1;
		fprintf(stderr, "sound: no input to record from could be opened, so the recording is silent\n");
	}
	if (r->from_file && monitoring)
		start_monitoring_file(i + 1);   /* (CABBY_SOUND_IN's file, heard as it's "recorded") */
	return i + 1;
}

void cby_sound_set_monitoring(int on)
{
	monitoring = on != 0;
	if (monitoring)
		setup();
}

int cby_sound_monitoring(void) { return monitoring; }

void cby_sound_set_monitor_volume(double v)
{
	monitor_volume = (float)(v < 0 ? 0 : v > 1 ? 1 : v);
}

double cby_sound_monitor_volume(void) { return monitor_volume; }

int64_t cby_sound_record(void) { return start_recording(NULL); }
int64_t cby_sound_record_from(const char *name) { return start_recording(name); }

void cby_sound_recording_stop(int64_t id)
{
	Recording *r = recording(id);
	pthread_mutex_lock(&rec_lock);
	int mine = capturing == r;
	r->going = 0;
	pthread_mutex_unlock(&rec_lock);
	if (mine)
		close_input();
	settle(r);
	if (getenv("CABBY_SOUND_DEBUG") && r->converting) {
		/* (from the first piece's arrival to the last's: the first piece's
		 * frames came before the clock started) */
		double secs = r->last_call - r->first_call;
		uint64_t after = r->received - r->first_frames;
		fprintf(stderr, "sound: %llu frames came in, in %llu pieces (%.0f a second, for %d), %.2f seconds after "
				"it opened; the longest wait between pieces was %.0f ms\n",
			(unsigned long long)r->received, (unsigned long long)r->calls, secs > 0 ? after / secs : 0,
			r->in_rate, r->first_call - r->opened, r->longest_gap * 1000);
	}
}

void cby_sound_recording_pause(int64_t id)
{
	Recording *r = recording(id);
	pthread_mutex_lock(&rec_lock);
	r->paused = 1;
	pthread_mutex_unlock(&rec_lock);
}

void cby_sound_recording_resume(int64_t id)
{
	Recording *r = recording(id);
	pthread_mutex_lock(&rec_lock);
	r->paused = 0;
	pthread_mutex_unlock(&rec_lock);
}

int cby_sound_recording_going(int64_t id)
{
	Recording *r = recording(id);
	pthread_mutex_lock(&rec_lock);
	int g = r->going && !r->paused;
	pthread_mutex_unlock(&rec_lock);
	return g;
}

double cby_sound_recording_seconds(int64_t id)
{
	Recording *r = recording(id);
	settle(r);
	return (double)r->frames / RATE;
}

/* how loud it's coming in: the loudest of the last tenth of a second, 0 to 1 */
double cby_sound_recording_level(int64_t id)
{
	Recording *r = recording(id);
	settle(r);
	pthread_mutex_lock(&rec_lock);
	int on = r->going && !r->paused;
	pthread_mutex_unlock(&rec_lock);
	double level = 0;
	if (on) {
		uint64_t end = r->frames;
		if (r->from_file) {
			uint64_t at = (uint64_t)((seconds_now() - r->started) * RATE);
			end = at < r->frames ? at : r->frames;
		}
		uint64_t start = end > RATE / 10 ? end - RATE / 10 : 0;
		for (uint64_t i = start * (size_t)r->channels; i < end * (size_t)r->channels; i++) {
			double v = fabs(r->pcm[i]);
			if (v > level)
				level = v;
		}
	}
	return level > 1 ? 1 : level;
}

static void put_little(FILE *f, uint32_t v, int size)
{
	for (int i = 0; i < size; i++)
		fputc((int)(v >> (8 * i)) & 0xFF, f);
}

void cby_sound_recording_save(int64_t id, const char *path)
{
	Recording *r = recording(id);
	settle(r);
	FILE *f = fopen(path, "wb");
	if (!f) {
		fail("can't save the recording as '%s': its folder isn't there, or can't be written to", path);
	}
	int bytes = rec_bits / 8, ch = r->channels;
	uint32_t data = (uint32_t)(r->frames * (uint64_t)ch * (uint64_t)bytes);
	fwrite("RIFF", 1, 4, f);
	put_little(f, 36 + data, 4);
	fwrite("WAVEfmt ", 1, 8, f);
	put_little(f, 16, 4);
	put_little(f, 1, 2);                          /* PCM */
	put_little(f, (uint32_t)ch, 2);
	put_little(f, RATE, 4);
	put_little(f, (uint32_t)(RATE * ch * bytes), 4);
	put_little(f, (uint32_t)(ch * bytes), 2);
	put_little(f, (uint32_t)rec_bits, 2);
	fwrite("data", 1, 4, f);
	put_little(f, data, 4);
	double top = bytes == 3 ? 8388607.0 : 32767.0;
	for (uint64_t i = 0; i < r->frames * (uint64_t)ch; i++) {
		double v = r->pcm[i];
		v = v > 1 ? 1 : v < -1 ? -1 : v;
		put_little(f, (uint32_t)(int32_t)lrint(v * top), bytes);
	}
	fclose(f);
}

/* plays it, as sound.play plays a file: it's made a clip of its own (again,
 * if it has changed since) */
void cby_sound_recording_play(int64_t id)
{
	setup();
	Recording *r = recording(id);
	settle(r);
	if (r->clip < 0 || r->clip_frames != r->frames) {
		float *pcm = ma_malloc(sizeof *pcm * (r->frames ? r->frames : 1) * CHANNELS, NULL);
		for (uint64_t i = 0; i < r->frames; i++)
			for (int c = 0; c < CHANNELS; c++)
				pcm[i * CHANNELS + c] = r->pcm[i * (size_t)r->channels + (r->channels == 1 ? 0 : c)];
		r->clip = add_clip("\001recording", pcm, r->frames);
		r->clip_frames = r->frames;
	}
	int c = r->clip;
	pthread_mutex_lock(&lock);
	voices = realloc(voices, sizeof *voices * (size_t)(nvoices + 1));
	voices[nvoices++] = (Voice){ c, 0, 1 };
	pthread_mutex_unlock(&lock);
}

/* CABBY_SOUND_IN's file, recorded all at once, heard as it would be
 * monitored: played, at the monitor's volume */
static void start_monitoring_file(int64_t id)
{
	setup();
	Recording *r = recording(id);
	float *pcm = ma_malloc(sizeof *pcm * (r->frames ? r->frames : 1) * CHANNELS, NULL);
	for (uint64_t i = 0; i < r->frames; i++)
		for (int c = 0; c < CHANNELS; c++)
			pcm[i * CHANNELS + c] = r->pcm[i * (size_t)r->channels + (r->channels == 1 ? 0 : c)] * monitor_volume;
	int c = add_clip("\001monitored", pcm, r->frames);
	pthread_mutex_lock(&lock);
	voices = realloc(voices, sizeof *voices * (size_t)(nvoices + 1));
	voices[nvoices++] = (Voice){ c, 0, 1 };
	pthread_mutex_unlock(&lock);
}

/* how loud a hundredth of a second is: its average (root mean square) */
static double window_loudness(const Recording *r, uint64_t start, uint64_t frames)
{
	double sum = 0;
	uint64_t n = 0;
	for (uint64_t i = start * (size_t)r->channels; i < (start + frames) * (size_t)r->channels && i < r->frames * (size_t)r->channels; i++, n++)
		sum += (double)r->pcm[i] * r->pcm[i];
	return n ? sqrt(sum / (double)n) : 0;
}

static int by_size(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* The quiet at the start and the end cut off (a moment of it kept). Quiet
 * is by the hundredth of a second, on average, against the loudest
 * hundredth: a click is too short to count, and an input's hiss is far
 * below what's played into it. */
void cby_sound_recording_trim(int64_t id)
{
	Recording *r = recording(id);
	pthread_mutex_lock(&rec_lock);
	int going = r->going && !r->from_file;
	pthread_mutex_unlock(&rec_lock);
	if (going)
		fail("stop a recording before trimming it%s", "");
	settle(r);
	uint64_t w = RATE / 100, windows = (r->frames + w - 1) / w;
	if (!windows)
		return;
	double *loud = malloc(sizeof *loud * windows), *sorted = malloc(sizeof *sorted * windows);
	for (uint64_t k = 0; k < windows; k++)
		sorted[k] = loud[k] = window_loudness(r, k * w, w);
	qsort(sorted, windows, sizeof *sorted, by_size);
	double loudest = sorted[windows - 1], floor_ = sorted[windows / 10];
	/* quiet: 30 dB below the loudest, or 10 dB above the input's own
	 * noise (the quietest tenth of the take), whichever is louder; but
	 * always well below the loudest */
	double quiet = loudest / 30;
	if (floor_ * 3 > quiet)
		quiet = floor_ * 3;
	if (quiet > loudest / 2)
		quiet = loudest / 2;
	if (quiet < 0.002)
		quiet = 0.002;
	free(sorted);
	uint64_t first = windows, last = 0;
	for (uint64_t k = 0; k < windows; k++)
		if (loud[k] > quiet) {
			if (first == windows)
				first = k;
			last = k + 1;
		}
	free(loud);
	if (first == windows) {
		r->frames = 0;
		return;
	}
	uint64_t from = first * w, to = last * w;
	from = from > RATE / 20 ? from - RATE / 20 : 0;
	to = to + RATE / 10 < r->frames ? to + RATE / 10 : r->frames;
	int ch = r->channels;
	memmove(r->pcm, r->pcm + from * (size_t)ch, sizeof *r->pcm * (to - from) * (size_t)ch);
	r->frames = to - from;
}

void cby_sound_recording_close(int64_t id)
{
	if (id < 1 || id > nrecs || !recs[id - 1])
		return;
	pthread_mutex_lock(&rec_lock);
	Recording *r = recs[id - 1];
	int mine = capturing == r;
	pthread_mutex_unlock(&rec_lock);
	if (mine)
		close_input();
	pthread_mutex_lock(&rec_lock);
	recs[id - 1] = NULL;
	pthread_mutex_unlock(&rec_lock);
	free_recording(r);
}

void cby_sound_set_recording_channels(int64_t n)
{
	if (n != 1 && n != 2)
		fail("a recording has 1 channel (mono) or 2 (stereo)%s", "");
	rec_channels = (int)n;
}

void cby_sound_set_recording_bits(int64_t n)
{
	if (n != 16 && n != 24)
		fail("recordings are saved with 16 bits (as a CD is) or 24 (as an audio interface records)%s", "");
	rec_bits = (int)n;
}

void cby_sound_set_recording_delay(int64_t ms)
{
	rec_delay = ms < 0 ? -1 : (int)ms;
}

/* the inputs that can be recorded from, by name, a line each */
static char *inputs_text;

int64_t cby_sound_inputs(void)
{
	free(inputs_text);
	inputs_text = strdup("");
	const char *file = getenv("CABBY_SOUND_IN"), *mode = getenv("CABBY_SOUND");
	size_t n = 0;
	if (file && *file) {
		free(inputs_text);
		inputs_text = strdup(file);
	} else if (!(mode && strcmp(mode, "none") == 0) && get_context()) {
		ma_device_info *infos;
		ma_uint32 count;
		if (ma_context_get_devices(&context, NULL, NULL, &infos, &count) == MA_SUCCESS)
			for (ma_uint32 i = 0; i < count; i++) {
				size_t len = strlen(infos[i].name);
				inputs_text = realloc(inputs_text, n + len + 2);
				if (n)
					inputs_text[n++] = '\n';
				memcpy(inputs_text + n, infos[i].name, len + 1);
				n += len;
			}
	}
	return (int64_t)strlen(inputs_text);
}

char cby_sound_inputs_char(int64_t i)
{
	return inputs_text && i >= 0 && i < (int64_t)strlen(inputs_text) ? inputs_text[i] : 0;
}

__attribute__((destructor)) static void recordings_end_of_program(void)
{
	close_input();
	for (int i = 0; i < nrecs; i++)
		if (recs[i])
			free_recording(recs[i]);
	free(recs);
	recs = NULL;
	nrecs = 0;
	free(inputs_text);
	inputs_text = NULL;
}

/* A sound file (WAV, MP3, FLAC, Ogg Vorbis) saved again as a WAV file of
 * 16-bit samples, at a rate and with so many channels (resampled, and
 * mixed down or spread, as it needs): 16000 a second in one channel is
 * what Whisper hears (speech.hear). */
void cby_sound_to_wav(const char *from, const char *to, int64_t rate, int64_t channels)
{
	if (rate < 1000 || rate > 384000 || channels < 1 || channels > 8)
		fail("%s", "a WAV file is 1000 to 384000 samples a second, in 1 to 8 channels");
	ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, (ma_uint32)channels, (ma_uint32)rate);
	ma_decoder dec;
	if (ma_decoder_init_file(from, &cfg, &dec) != MA_SUCCESS)
		fail("can't read the sound '%s': it isn't there, or isn't WAV, MP3, FLAC or Ogg Vorbis", from);
	FILE *f = fopen(to, "wb");
	if (!f) {
		ma_decoder_uninit(&dec);
		fail("can't save '%s': its folder isn't there, or can't be written to", to);
	}
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put_little(f, 16, 4);
	put_little(f, 1, 2);
	put_little(f, (uint32_t)channels, 2);
	put_little(f, (uint32_t)rate, 4);
	put_little(f, (uint32_t)(rate * channels * 2), 4);
	put_little(f, (uint32_t)(channels * 2), 2);
	put_little(f, 16, 2);
	fwrite("data\0\0\0\0", 1, 8, f);
	int16_t buf[4096 * 8];
	uint64_t total = 0;
	for (;;) {
		ma_uint64 got = 0;
		if (ma_decoder_read_pcm_frames(&dec, buf, 4096, &got) != MA_SUCCESS && got == 0)
			break;
		if (got == 0)
			break;
		for (ma_uint64 i = 0; i < got * (ma_uint64)channels; i++)
			put_little(f, (uint32_t)(uint16_t)buf[i], 2);
		total += got;
	}
	ma_decoder_uninit(&dec);
	uint32_t data = (uint32_t)(total * (uint64_t)channels * 2);
	fseek(f, 4, SEEK_SET);
	put_little(f, 36 + data, 4);
	fseek(f, 40, SEEK_SET);
	put_little(f, data, 4);
	fclose(f);
}
