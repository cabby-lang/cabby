/* midi.c — MIDI keyboards and synthesizers, for the sound library.
 *
 * A keyboard (anything that sends MIDI: a keyboard, drum pads, a
 * controller with knobs) is read on a thread of its own, so nothing is
 * missed while the program is busy. What's played is told to the program's
 * handlers from the event loop (events.run, a window's run), through a
 * pipe the loop watches; a keyboard played through an instrument is heard
 * at once, from the keyboard's thread, straight into the mix.
 *
 * A synthesizer (anything that takes MIDI: a hardware synthesizer, a
 * keyboard's own sounds, FluidSynth) is sent messages as the program asks.
 *
 * On Linux it's the ALSA sequencer (libasound, loaded when it's first
 * needed: every desktop has it, and PipeWire's MIDI goes through it). On a
 * Mac it's Core MIDI, loaded the same way. Neither is needed to build.
 *
 * For the tests: CABBY_MIDI_IN=song.mid is the one keyboard there is, and
 * plays the file's notes, as fast as they can be read; CABBY_MIDI_OUT=out.mid
 * is the one synthesizer, and what it's sent is saved to that MIDI file
 * when it's closed (or the program ends). */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "third_party/tml.h"

#define RATE 48000

/* ------------------------------------------------------------- the rest of Cabby */

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

static void call_directly(void *obj, void *fn) { ((void (*)(void *))fn)(obj); }

/* The interpreter replaces this when it loads the library. (Weak, so a
 * program can link this with the other libraries.) */
__attribute__((weak)) void (*cby_invoke_hook)(void *obj, void *fn) = call_directly;

extern int cby_events_watch(int fd, void (*fn)(void *data), void *data);
extern void cby_events_unwatch(int id);

/* sound.c's */
extern int sound_live_channel(const char *instrument);
extern int64_t sound_live_now(void);
extern void sound_live_message(int channel, int status, int a, int b, int64_t frame);
extern void sound_live_end(int channel);
extern int sound_program_of(const char *name);

static double seconds_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + t.tv_nsec / 1e9;
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

typedef struct {
	uint8_t status, a, b;
} Msg;

/* a list of names, found as it's asked for */
typedef struct {
	char **names;
	int n;
	/* where each is: the ALSA client and port, or the Core MIDI endpoint */
	int *client, *port;
	uint32_t *endpoint;
} Found;

static void found_add(Found *f, const char *name, int client, int port, uint32_t endpoint)
{
	f->names = realloc(f->names, sizeof *f->names * (size_t)(f->n + 1));
	f->client = realloc(f->client, sizeof *f->client * (size_t)(f->n + 1));
	f->port = realloc(f->port, sizeof *f->port * (size_t)(f->n + 1));
	f->endpoint = realloc(f->endpoint, sizeof *f->endpoint * (size_t)(f->n + 1));
	f->names[f->n] = strdup(name);
	f->client[f->n] = client;
	f->port[f->n] = port;
	f->endpoint[f->n] = endpoint;
	f->n++;
}

static void found_free(Found *f)
{
	for (int i = 0; i < f->n; i++)
		free(f->names[i]);
	free(f->names);
	free(f->client);
	free(f->port);
	free(f->endpoint);
	memset(f, 0, sizeof *f);
}

/* ============================================================= Linux: the ALSA sequencer */

#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)

/* (from alsa/seq_event.h and seq.h: their shapes, which haven't changed in
 * twenty years, so nothing of ALSA's is needed to build) */
typedef struct {
	unsigned char client, port;
} SeqAddr;

typedef struct {
	unsigned char type, flags, tag, queue;
	unsigned int time[2];
	SeqAddr source, dest;
	union {
		struct {
			unsigned char channel, note, velocity, off_velocity;
			unsigned int duration;
		} note;
		struct {
			unsigned char channel, unused[3];
			unsigned int param;
			int value;
		} control;
		unsigned char raw[12];
	} data;
} SeqEvent;

enum {
	SEQ_NOTEON = 6, SEQ_NOTEOFF = 7, SEQ_KEYPRESS = 8, SEQ_CONTROLLER = 10, SEQ_PGMCHANGE = 11,
	SEQ_CHANPRESS = 12, SEQ_PITCHBEND = 13
};
#define SEQ_OPEN_OUTPUT 1
#define SEQ_OPEN_INPUT 2
#define SEQ_OPEN_DUPLEX 3
#define SEQ_CAP_READ (1 << 0)
#define SEQ_CAP_WRITE (1 << 1)
#define SEQ_CAP_SUBS_READ (1 << 5)
#define SEQ_CAP_SUBS_WRITE (1 << 6)
#define SEQ_CAP_NO_EXPORT (1 << 7)
#define SEQ_TYPE_MIDI_GENERIC (1 << 1)
#define SEQ_TYPE_APPLICATION (1 << 20)
#define SEQ_ADDRESS_SUBSCRIBERS 254
#define SEQ_ADDRESS_UNKNOWN 253
#define SEQ_QUEUE_DIRECT 253

static struct {
	int tried, ready;
	int (*open)(void **h, const char *name, int streams, int mode);
	int (*close)(void *h);
	int (*set_client_name)(void *h, const char *name);
	int (*client_id)(void *h);
	int (*create_simple_port)(void *h, const char *name, unsigned int caps, unsigned int type);
	int (*connect_from)(void *h, int my_port, int client, int port);
	int (*connect_to)(void *h, int my_port, int client, int port);
	int (*nonblock)(void *h, int on);
	int (*event_input)(void *h, SeqEvent **ev);
	int (*event_output_direct)(void *h, SeqEvent *ev);
	int (*poll_descriptors_count)(void *h, short events);
	int (*poll_descriptors)(void *h, struct pollfd *p, unsigned int space, short events);
	int (*client_info_malloc)(void **info);
	void (*client_info_free)(void *info);
	void (*client_info_set_client)(void *info, int client);
	int (*client_info_get_client)(const void *info);
	const char *(*client_info_get_name)(void *info);
	int (*query_next_client)(void *h, void *info);
	int (*port_info_malloc)(void **info);
	void (*port_info_free)(void *info);
	void (*port_info_set_client)(void *info, int client);
	void (*port_info_set_port)(void *info, int port);
	int (*port_info_get_port)(const void *info);
	const char *(*port_info_get_name)(const void *info);
	unsigned int (*port_info_get_capability)(const void *info);
	int (*query_next_port)(void *h, void *info);
	int (*set_error_handler)(void *handler);
	int no_sequencer;   /* it didn't open: /dev/snd/seq isn't there */
} alsa;

/* (ALSA's own messages, while Cabby looks: a computer without a sequencer
 * is told why by the panic, not by ALSA's lines) */
static void alsa_quiet(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
	(void)file;
	(void)line;
	(void)function;
	(void)err;
	(void)fmt;
}

static int seq_open(void **h, int streams, int mode)
{
	alsa.set_error_handler((void *)alsa_quiet);
	int r = alsa.open(h, "default", streams, mode);
	alsa.set_error_handler(NULL);
	if (r < 0)
		alsa.no_sequencer = 1;
	return r;
}

static int alsa_load(void)
{
	if (alsa.tried)
		return alsa.ready;
	alsa.tried = 1;
	void *lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
	if (!lib)
		lib = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
	if (!lib)
		return 0;
#define GET(field, symbol) \
	if (!(*(void **)&alsa.field = dlsym(lib, symbol))) \
		return 0;
	GET(open, "snd_seq_open")
	GET(close, "snd_seq_close")
	GET(set_client_name, "snd_seq_set_client_name")
	GET(client_id, "snd_seq_client_id")
	GET(create_simple_port, "snd_seq_create_simple_port")
	GET(connect_from, "snd_seq_connect_from")
	GET(connect_to, "snd_seq_connect_to")
	GET(nonblock, "snd_seq_nonblock")
	GET(event_input, "snd_seq_event_input")
	GET(event_output_direct, "snd_seq_event_output_direct")
	GET(poll_descriptors_count, "snd_seq_poll_descriptors_count")
	GET(poll_descriptors, "snd_seq_poll_descriptors")
	GET(client_info_malloc, "snd_seq_client_info_malloc")
	GET(client_info_free, "snd_seq_client_info_free")
	GET(client_info_set_client, "snd_seq_client_info_set_client")
	GET(client_info_get_client, "snd_seq_client_info_get_client")
	GET(client_info_get_name, "snd_seq_client_info_get_name")
	GET(query_next_client, "snd_seq_query_next_client")
	GET(port_info_malloc, "snd_seq_port_info_malloc")
	GET(port_info_free, "snd_seq_port_info_free")
	GET(port_info_set_client, "snd_seq_port_info_set_client")
	GET(port_info_set_port, "snd_seq_port_info_set_port")
	GET(port_info_get_port, "snd_seq_port_info_get_port")
	GET(port_info_get_name, "snd_seq_port_info_get_name")
	GET(port_info_get_capability, "snd_seq_port_info_get_capability")
	GET(query_next_port, "snd_seq_query_next_port")
	GET(set_error_handler, "snd_lib_error_set_handler")
#undef GET
	alsa.ready = 1;
	return 1;
}

/* the sequencer's ports that can be read from (keyboards) or written to
 * (synthesizers): not ALSA's own (the system's, Midi Through), nor ours */
static void alsa_find(Found *f, int outputs)
{
	if (!alsa_load())
		return;
	void *h;
	if (seq_open(&h, SEQ_OPEN_DUPLEX, 0) < 0)
		return;
	int me = alsa.client_id(h);
	void *ci, *pi;
	alsa.client_info_malloc(&ci);
	alsa.port_info_malloc(&pi);
	alsa.client_info_set_client(ci, -1);
	unsigned want = outputs ? SEQ_CAP_WRITE | SEQ_CAP_SUBS_WRITE : SEQ_CAP_READ | SEQ_CAP_SUBS_READ;
	while (alsa.query_next_client(h, ci) >= 0) {
		int client = alsa.client_info_get_client(ci);
		const char *cname = alsa.client_info_get_name(ci);
		if (client == 0 || client == me || strcmp(cname, "Midi Through") == 0)
			continue;
		alsa.port_info_set_client(pi, client);
		alsa.port_info_set_port(pi, -1);
		while (alsa.query_next_port(h, pi) >= 0) {
			unsigned caps = alsa.port_info_get_capability(pi);
			if ((caps & want) != want || (caps & SEQ_CAP_NO_EXPORT))
				continue;
			const char *pname = alsa.port_info_get_name(pi);
			char name[300];
			/* "Keystation 49 MK3 MIDI 1": the port's name, with its
			 * device's if it doesn't say */
			if (contains_text(pname, cname))
				snprintf(name, sizeof name, "%s", pname);
			else
				snprintf(name, sizeof name, "%s: %s", cname, pname);
			found_add(f, name, client, alsa.port_info_get_port(pi), 0);
		}
	}
	alsa.port_info_free(pi);
	alsa.client_info_free(ci);
	alsa.close(h);
}

#endif

/* ============================================================= macOS: Core MIDI */

#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)

/* (Core MIDI's and Core Foundation's shapes, from their headers, so
 * neither is needed to build: they're loaded when MIDI is first used) */
typedef uint32_t MIDIRef;
typedef const void *CFStr;
#pragma pack(push, 4)
typedef struct {
	uint64_t timeStamp;
	uint16_t length;
	uint8_t data[256];
} MacPacket;
typedef struct {
	uint32_t numPackets;
	MacPacket packet[1];
} MacPacketList;
#pragma pack(pop)
typedef void (*MacReadProc)(const MacPacketList *list, void *ref, void *source_ref);
#define CF_UTF8 0x08000100u

static struct {
	int tried, ready;
	int32_t (*client_create)(CFStr name, void *notify, void *ref, MIDIRef *out);
	int32_t (*input_port_create)(MIDIRef client, CFStr name, MacReadProc proc, void *ref, MIDIRef *out);
	int32_t (*output_port_create)(MIDIRef client, CFStr name, MIDIRef *out);
	int32_t (*port_connect_source)(MIDIRef port, MIDIRef source, void *ref);
	int32_t (*port_disconnect_source)(MIDIRef port, MIDIRef source);
	int32_t (*port_dispose)(MIDIRef port);
	unsigned long (*number_of_sources)(void);
	MIDIRef (*get_source)(unsigned long i);
	unsigned long (*number_of_destinations)(void);
	MIDIRef (*get_destination)(unsigned long i);
	int32_t (*get_string_property)(MIDIRef obj, CFStr property, CFStr *out);
	int32_t (*send)(MIDIRef port, MIDIRef dest, const MacPacketList *list);
	MacPacket *(*packet_list_init)(MacPacketList *list);
	MacPacket *(*packet_list_add)(MacPacketList *list, unsigned long size, MacPacket *cur, uint64_t time,
				      unsigned long n, const uint8_t *data);
	CFStr *display_name;   /* kMIDIPropertyDisplayName */
	CFStr (*string_create)(const void *alloc, const char *text, uint32_t encoding);
	unsigned char (*string_get)(CFStr s, char *buf, long size, uint32_t encoding);
	void (*release)(const void *obj);
	MIDIRef client;
} mac;

static int mac_load(void)
{
	if (mac.tried)
		return mac.ready;
	mac.tried = 1;
	void *midi = dlopen("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI", RTLD_NOW | RTLD_LOCAL);
	void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_NOW | RTLD_LOCAL);
	if (!midi || !cf)
		return 0;
#define GET(lib, field, symbol) \
	if (!(*(void **)&mac.field = dlsym(lib, symbol))) \
		return 0;
	GET(midi, client_create, "MIDIClientCreate")
	GET(midi, input_port_create, "MIDIInputPortCreate")
	GET(midi, output_port_create, "MIDIOutputPortCreate")
	GET(midi, port_connect_source, "MIDIPortConnectSource")
	GET(midi, port_disconnect_source, "MIDIPortDisconnectSource")
	GET(midi, port_dispose, "MIDIPortDispose")
	GET(midi, number_of_sources, "MIDIGetNumberOfSources")
	GET(midi, get_source, "MIDIGetSource")
	GET(midi, number_of_destinations, "MIDIGetNumberOfDestinations")
	GET(midi, get_destination, "MIDIGetDestination")
	GET(midi, get_string_property, "MIDIObjectGetStringProperty")
	GET(midi, send, "MIDISend")
	GET(midi, packet_list_init, "MIDIPacketListInit")
	GET(midi, packet_list_add, "MIDIPacketListAdd")
	GET(midi, display_name, "kMIDIPropertyDisplayName")
	GET(cf, string_create, "CFStringCreateWithCString")
	GET(cf, string_get, "CFStringGetCString")
	GET(cf, release, "CFRelease")
#undef GET
	CFStr name = mac.string_create(NULL, "Cabby", CF_UTF8);
	int32_t r = mac.client_create(name, NULL, NULL, &mac.client);
	mac.release(name);
	if (r != 0)
		return 0;
	mac.ready = 1;
	return 1;
}

static void mac_find(Found *f, int outputs)
{
	if (!mac_load())
		return;
	unsigned long n = outputs ? mac.number_of_destinations() : mac.number_of_sources();
	for (unsigned long i = 0; i < n; i++) {
		MIDIRef e = outputs ? mac.get_destination(i) : mac.get_source(i);
		CFStr s = NULL;
		char name[300] = "";
		if (mac.get_string_property(e, *mac.display_name, &s) == 0 && s) {
			mac.string_get(s, name, sizeof name, CF_UTF8);
			mac.release(s);
		}
		if (!name[0])
			snprintf(name, sizeof name, "MIDI %lu", i + 1);
		found_add(f, name, 0, 0, e);
	}
}

#endif

/* ============================================================= what's there */

static const char *test_in(void)
{
	const char *f = getenv("CABBY_MIDI_IN");
	return f && *f ? f : NULL;
}

static const char *test_out(void)
{
	const char *f = getenv("CABBY_MIDI_OUT");
	return f && *f ? f : NULL;
}

static void find(Found *f, int outputs)
{
	const char *t = outputs ? test_out() : test_in();
	if (t) {
		char name[600];
		snprintf(name, sizeof name, "%s (%s)", t, outputs ? "CABBY_MIDI_OUT" : "CABBY_MIDI_IN");
		found_add(f, name, -1, -1, 0);
		return;
	}
#if defined(__APPLE__)
	mac_find(f, outputs);
#else
	alsa_find(f, outputs);
#endif
}

/* the names, one to a line, for sound.midi_inputs and midi_outputs */
static char *given;
static size_t given_len;

int64_t cby_midi_names(int outputs)
{
	Found f = { 0 };
	find(&f, outputs);
	size_t n = 0;
	for (int i = 0; i < f.n; i++)
		n += strlen(f.names[i]) + 1;
	free(given);
	given = malloc(n + 1);
	given[0] = 0;
	given_len = 0;
	for (int i = 0; i < f.n; i++) {
		size_t k = strlen(f.names[i]);
		memcpy(given + given_len, f.names[i], k);
		given_len += k;
		given[given_len++] = '\n';
	}
	if (given_len)
		given_len--;   /* (a line end between each, not after the last) */
	given[given_len] = 0;
	found_free(&f);
	return (int64_t)given_len;
}

char cby_midi_given_char(int64_t i)
{
	return given && i >= 0 && (size_t)i < given_len ? given[i] : 0;
}

static int64_t give(const char *text)
{
	free(given);
	given = strdup(text);
	given_len = strlen(given);
	return (int64_t)given_len;
}

/* which of what's there: the first (name ""), or the first whose name has
 * the name in it */
static int choose(Found *f, const char *name, int outputs)
{
	for (int i = 0; i < f->n; i++)
		if (contains_text(f->names[i], name))
			return i;
	const char *what = outputs ? "synthesizer" : "keyboard";
	char text[400];
	if (!*name) {
#if defined(__APPLE__)
		snprintf(text, sizeof text, "there's no MIDI %s connected (sound.midi_%s lists what is)", what,
			 outputs ? "outputs" : "inputs");
#else
		snprintf(text, sizeof text, "there's no MIDI %s connected%s (sound.midi_%s lists what is)", what,
			 !alsa_load() ? ", and no ALSA library (libasound) to find one with"
			 : alsa.no_sequencer ? ", and no ALSA sequencer (/dev/snd/seq) to find one with" : "",
			 outputs ? "outputs" : "inputs");
#endif
	} else {
		snprintf(text, sizeof text, "there's no MIDI %s called '%%s' (sound.midi_%s lists what's connected)", what,
			 outputs ? "outputs" : "inputs");
		found_free(f);
		fail(text, name);
	}
	found_free(f);
	fail("%s", text);
}

/* ============================================================= synthesizers (out) */

typedef struct {
	double at;     /* seconds, from when it was opened */
	Msg m;
} Sent;

typedef struct {
	int used;
	char *name;
	pthread_mutex_t lock;
	int kind;      /* 1 ALSA, 2 Core MIDI, 3 the test's file */
#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)
	void *seq;
	int seq_port;
#endif
#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)
	MIDIRef mac_port, mac_dest;
#endif
	char *file;    /* the test's */
	Sent *sent;
	int nsent, capsent;
	double opened;
	/* notes to let go later (play_note): kept in order */
	struct {
		double at;
		Msg m;
	} *later;
	int nlater, caplater;
} Output;

static Output **outs;
static int nouts;

/* (the keyboards, below: one sent on to a synthesizer that's closed stops) */
typedef struct Input Input;
static Input **ins;
static int nins;
/* (the table of keyboards: a worker may open one as the program does) */
static pthread_mutex_t ins_lock = PTHREAD_MUTEX_INITIALIZER;
static void forget_forwards(int64_t out);
static pthread_mutex_t outs_lock = PTHREAD_MUTEX_INITIALIZER;   /* (the timer thread looks at outs) */
static pthread_t timer_thread;
static int timer_running;
static pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;

static Output *output(int64_t id)
{
	pthread_mutex_lock(&outs_lock);
	Output *o = id >= 1 && id <= nouts ? outs[id - 1] : NULL;
	pthread_mutex_unlock(&outs_lock);
	if (!o || !o->used)
		fail("that synthesizer has been closed%s", "");
	return o;
}

/* a message to the synthesizer (with its lock held) */
static void send_now(Output *o, Msg m)
{
	if (o->kind == 3) {
		if (o->nsent == o->capsent) {
			o->capsent = o->capsent ? o->capsent * 2 : 256;
			o->sent = realloc(o->sent, sizeof *o->sent * (size_t)o->capsent);
		}
		o->sent[o->nsent++] = (Sent){ seconds_now() - o->opened, m };
		return;
	}
#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)
	if (o->kind == 1) {
		SeqEvent e;
		memset(&e, 0, sizeof e);
		int ch = m.status & 0x0F;
		switch (m.status & 0xF0) {
		case 0x90:
			e.type = SEQ_NOTEON;
			e.data.note.channel = (unsigned char)ch;
			e.data.note.note = m.a;
			e.data.note.velocity = m.b;
			break;
		case 0x80:
			e.type = SEQ_NOTEOFF;
			e.data.note.channel = (unsigned char)ch;
			e.data.note.note = m.a;
			break;
		case 0xB0:
			e.type = SEQ_CONTROLLER;
			e.data.control.channel = (unsigned char)ch;
			e.data.control.param = m.a;
			e.data.control.value = m.b;
			break;
		case 0xC0:
			e.type = SEQ_PGMCHANGE;
			e.data.control.channel = (unsigned char)ch;
			e.data.control.value = m.a;
			break;
		case 0xE0:
			e.type = SEQ_PITCHBEND;
			e.data.control.channel = (unsigned char)ch;
			e.data.control.value = (m.a | (m.b << 7)) - 8192;
			break;
		default:
			return;
		}
		e.queue = SEQ_QUEUE_DIRECT;
		e.source.port = (unsigned char)o->seq_port;
		e.dest.client = SEQ_ADDRESS_SUBSCRIBERS;
		e.dest.port = SEQ_ADDRESS_UNKNOWN;
		alsa.event_output_direct(o->seq, &e);
		return;
	}
#endif
#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)
	if (o->kind == 2) {
		uint8_t buf[512];
		MacPacketList *list = (MacPacketList *)buf;
		MacPacket *p = mac.packet_list_init(list);
		uint8_t data[3] = { m.status, m.a, m.b };
		int n = (m.status & 0xF0) == 0xC0 || (m.status & 0xF0) == 0xD0 ? 2 : 3;
		mac.packet_list_add(list, sizeof buf, p, 0, (unsigned long)n, data);
		mac.send(o->mac_port, o->mac_dest, list);
	}
#endif
}

/* notes let go when their time comes (play_note): a thread of its own,
 * so the program needn't wait */
static void *timer_work(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&outs_lock);
	for (;;) {
		double next = -1, now = seconds_now();
		for (int i = 0; i < nouts; i++) {
			Output *o = outs[i];
			if (!o || !o->used)
				continue;
			pthread_mutex_lock(&o->lock);
			int k = 0;
			while (k < o->nlater && o->opened + o->later[k].at <= now)
				send_now(o, o->later[k++].m);
			if (k) {
				memmove(o->later, o->later + k, sizeof *o->later * (size_t)(o->nlater - k));
				o->nlater -= k;
			}
			if (o->nlater && (next < 0 || o->opened + o->later[0].at < next))
				next = o->opened + o->later[0].at;
			pthread_mutex_unlock(&o->lock);
		}
		if (!timer_running)
			break;
		struct timespec until;
		clock_gettime(CLOCK_REALTIME, &until);
		double wait = next < 0 ? 3600 : next - now;
		if (wait < 0)
			wait = 0;
		until.tv_sec += (time_t)wait;
		until.tv_nsec += (long)((wait - (double)(time_t)wait) * 1e9);
		if (until.tv_nsec >= 1000000000) {
			until.tv_sec++;
			until.tv_nsec -= 1000000000;
		}
		pthread_cond_timedwait(&timer_cond, &outs_lock, &until);
		if (!timer_running)
			break;
	}
	pthread_mutex_unlock(&outs_lock);
	return NULL;
}

int64_t cby_midi_open_output(const char *name)
{
	Found f = { 0 };
	find(&f, 1);
	int i = choose(&f, name, 1);
	Output *o = calloc(1, sizeof *o);
	o->used = 1;
	o->name = strdup(f.names[i]);
	pthread_mutex_init(&o->lock, NULL);
	o->opened = seconds_now();
	if (test_out()) {
		o->kind = 3;
		o->file = strdup(test_out());
	} else {
#if defined(__APPLE__)
		o->kind = 2;
		o->mac_dest = f.endpoint[i];
		CFStr pname = mac.string_create(NULL, "Cabby out", CF_UTF8);
		int32_t r = mac.output_port_create(mac.client, pname, &o->mac_port);
		mac.release(pname);
		if (r != 0) {
			found_free(&f);
			free(o->name);
			free(o);
			fail("can't send to the synthesizer '%s' (Core MIDI said no)", name);
		}
#else
		o->kind = 1;
		if (seq_open(&o->seq, SEQ_OPEN_OUTPUT, 0) < 0) {
			found_free(&f);
			free(o->name);
			free(o);
			fail("can't send to the synthesizer '%s': the ALSA sequencer didn't open", name);
		}
		alsa.set_client_name(o->seq, "Cabby");
		o->seq_port = alsa.create_simple_port(o->seq, "Cabby out", SEQ_CAP_READ | SEQ_CAP_SUBS_READ,
						      SEQ_TYPE_MIDI_GENERIC | SEQ_TYPE_APPLICATION);
		if (o->seq_port < 0 || alsa.connect_to(o->seq, o->seq_port, f.client[i], f.port[i]) < 0) {
			alsa.close(o->seq);
			char what[400];
			snprintf(what, sizeof what, "%s", f.names[i]);
			found_free(&f);
			free(o->name);
			free(o);
			fail("can't send to the synthesizer '%s': it didn't let Cabby connect", what);
		}
#endif
	}
	found_free(&f);
	pthread_mutex_lock(&outs_lock);
	int k = 0;
	while (k < nouts && outs[k])
		k++;
	if (k == nouts) {
		outs = realloc(outs, sizeof *outs * (size_t)(nouts + 1));
		nouts++;
	}
	outs[k] = o;
	if (!timer_running) {
		timer_running = 1;
		pthread_create(&timer_thread, NULL, timer_work, NULL);
	}
	pthread_mutex_unlock(&outs_lock);
	return k + 1;
}

int64_t cby_midi_output_name(int64_t id) { return give(output(id)->name); }

void cby_midi_send(int64_t id, int64_t status, int64_t a, int64_t b)
{
	Output *o = output(id);
	Msg m = { (uint8_t)status, (uint8_t)(a & 0x7F), (uint8_t)(b & 0x7F) };
	pthread_mutex_lock(&o->lock);
	send_now(o, m);
	pthread_mutex_unlock(&o->lock);
}

/* a message a while from now (a note let go, after play_note's time) */
void cby_midi_send_later(int64_t id, int64_t ms, int64_t status, int64_t a, int64_t b)
{
	Output *o = output(id);
	Msg m = { (uint8_t)status, (uint8_t)(a & 0x7F), (uint8_t)(b & 0x7F) };
	double at = seconds_now() - o->opened + (double)ms / 1000;
	pthread_mutex_lock(&outs_lock);
	pthread_mutex_lock(&o->lock);
	if (o->nlater == o->caplater) {
		o->caplater = o->caplater ? o->caplater * 2 : 64;
		o->later = realloc(o->later, sizeof *o->later * (size_t)o->caplater);
	}
	int i = o->nlater++;
	while (i > 0 && o->later[i - 1].at > at) {
		o->later[i] = o->later[i - 1];
		i--;
	}
	o->later[i].at = at;
	o->later[i].m = m;
	pthread_mutex_unlock(&o->lock);
	pthread_cond_signal(&timer_cond);
	pthread_mutex_unlock(&outs_lock);
}

/* (sound.c's song writer has the same: written again here, small) */
static void put_be(FILE *f, uint32_t v, int size)
{
	for (int i = size - 1; i >= 0; i--)
		fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static void put_varlen_to(uint8_t *buf, size_t *n, uint32_t v)
{
	uint8_t tmp[5];
	int k = 0;
	tmp[k++] = v & 0x7F;
	while (v >>= 7)
		tmp[k++] = (uint8_t)(0x80 | (v & 0x7F));
	while (k)
		buf[(*n)++] = tmp[--k];
}

/* the test's synthesizer: what it was sent, as a MIDI file, a millisecond
 * to a tick (500 ticks a beat at 120 beats a minute) */
static void save_sent(Output *o)
{
	FILE *f = fopen(o->file, "wb");
	if (!f)
		return;
	size_t cap = 64 + (size_t)o->nsent * 9, n = 0;
	uint8_t *t = malloc(cap);
	/* the tempo: 500000 microseconds a beat */
	const uint8_t tempo[] = { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 };
	memcpy(t, tempo, sizeof tempo);
	n = sizeof tempo;
	uint32_t last = 0;
	for (int i = 0; i < o->nsent; i++) {
		uint32_t ms = (uint32_t)(o->sent[i].at * 1000 + 0.5);
		if (ms < last)
			ms = last;
		put_varlen_to(t, &n, ms - last);
		last = ms;
		Msg m = o->sent[i].m;
		t[n++] = m.status;
		t[n++] = m.a;
		if ((m.status & 0xF0) != 0xC0 && (m.status & 0xF0) != 0xD0)
			t[n++] = m.b;
	}
	const uint8_t end[] = { 0x00, 0xFF, 0x2F, 0x00 };
	memcpy(t + n, end, sizeof end);
	n += sizeof end;
	fwrite("MThd", 1, 4, f);
	put_be(f, 6, 4);
	put_be(f, 0, 2);
	put_be(f, 1, 2);
	put_be(f, 500, 2);
	fwrite("MTrk", 1, 4, f);
	put_be(f, (uint32_t)n, 4);
	fwrite(t, 1, n, f);
	free(t);
	fclose(f);
}

void cby_midi_output_close(int64_t id)
{
	pthread_mutex_lock(&outs_lock);
	Output *o = id >= 1 && id <= nouts ? outs[id - 1] : NULL;
	if (o)
		outs[id - 1] = NULL;
	pthread_mutex_unlock(&outs_lock);
	if (!o)
		return;
	forget_forwards(id);   /* (a keyboard sending to it stops: the number may be another's next) */
	pthread_mutex_lock(&o->lock);
	/* notes waiting to be let go are let go now, so none is left sounding */
	for (int i = 0; i < o->nlater; i++)
		send_now(o, o->later[i].m);
	o->nlater = 0;
	pthread_mutex_unlock(&o->lock);
	if (o->kind == 3)
		save_sent(o);
#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)
	if (o->kind == 1)
		alsa.close(o->seq);
#endif
#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)
	if (o->kind == 2)
		mac.port_dispose(o->mac_port);
#endif
	pthread_mutex_destroy(&o->lock);
	free(o->name);
	free(o->file);
	free(o->sent);
	free(o->later);
	free(o);
}

/* ============================================================= keyboards (in) */

struct Input {
	int used;
	int64_t id;
	char *name;
	int kind;                   /* 1 ALSA, 2 Core MIDI, 3 the test's file */
	pthread_t thread;
	int thread_running;
	int stop[2];                /* (ALSA's and the file's thread: told to stop) */
#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)
	void *seq;
#endif
#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)
	MIDIRef mac_port, mac_source;
	uint8_t running, have, need, bytes[2];   /* reading the bytes: running status */
	int in_sysex;
#endif
	char *file;
	/* what's come, for the handlers */
	pthread_mutex_t qlock;
	Msg *q;
	int qn, qcap;
	int wake[2];                /* a byte for each lot: the event loop watches it */
	int watch;
	int telling;                /* handlers are set: what comes is kept for them */
	volatile int through;       /* the instrument's channel, plus 1 (0: not played through) */
	volatile int64_t forward;   /* a synthesizer it's sent on to (0: none) */
	/* what the handlers are told of */
	Msg cur;
	void *note_obj, *note_fn, *control_obj, *control_fn, *bend_obj, *bend_fn;
};

static void forget_forwards(int64_t out)
{
	pthread_mutex_lock(&ins_lock);
	for (int i = 0; i < nins; i++)
		if (ins[i] && ins[i]->forward == out)
			ins[i]->forward = 0;
	pthread_mutex_unlock(&ins_lock);
}

static Input *input(int64_t id)
{
	pthread_mutex_lock(&ins_lock);
	Input *in = id >= 1 && id <= nins ? ins[id - 1] : NULL;
	pthread_mutex_unlock(&ins_lock);
	if (!in)
		fail("that keyboard has been closed%s", "");
	return in;
}

/* the keyboard, or NULL if it's been closed */
static Input *input_or_none(int64_t id)
{
	pthread_mutex_lock(&ins_lock);
	Input *in = id >= 1 && id <= nins ? ins[id - 1] : NULL;
	pthread_mutex_unlock(&ins_lock);
	return in;
}

/* a message from the keyboard (its own thread): played at once if it's
 * played through, sent on if it's sent on, and kept for the handlers */
static void got(Input *in, Msg m, int64_t frame)
{
	int type = m.status & 0xF0;
	if (type != 0x80 && type != 0x90 && type != 0xB0 && type != 0xE0)
		return;   /* (key pressure, program changes, the clock...: not told) */
	int through = in->through;
	if (through)
		sound_live_message(through - 1, m.status, m.a, m.b, frame);
	int64_t fwd = in->forward;
	if (fwd) {
		pthread_mutex_lock(&outs_lock);
		Output *o = fwd >= 1 && fwd <= nouts ? outs[fwd - 1] : NULL;
		if (o) {
			pthread_mutex_lock(&o->lock);
			send_now(o, m);
			pthread_mutex_unlock(&o->lock);
		}
		pthread_mutex_unlock(&outs_lock);
	}
	/* A keyboard's notes from before a handler was given aren't told. The
	 * test's file is read as fast as it can be, maybe before the program's
	 * next line gives on_note (play_through starts it): its notes are kept
	 * for the handler, as a person couldn't play them that soon. */
	if (!in->telling && in->kind != 3)
		return;
	pthread_mutex_lock(&in->qlock);
	if (in->qn == in->qcap) {
		in->qcap = in->qcap ? in->qcap * 2 : 64;
		in->q = realloc(in->q, sizeof *in->q * (size_t)in->qcap);
	}
	in->q[in->qn++] = m;
	pthread_mutex_unlock(&in->qlock);
	char one = 1;
	if (write(in->wake[1], &one, 1) < 0) {
		/* (full: the loop has plenty to wake it already) */
	}
}

/* the event loop: each message that's come, to its handler */
static void tell(void *data)
{
	int64_t id = (int64_t)(intptr_t)data;
	Input *in = input_or_none(id);
	if (!in)
		return;
	char buf[256];
	while (read(in->wake[0], buf, sizeof buf) > 0) {
	}
	for (;;) {
		in = input_or_none(id);
		if (!in)
			return;   /* (a handler closed it) */
		pthread_mutex_lock(&in->qlock);
		if (!in->qn) {
			pthread_mutex_unlock(&in->qlock);
			return;
		}
		Msg m = in->q[0];
		memmove(in->q, in->q + 1, sizeof *in->q * (size_t)(in->qn - 1));
		in->qn--;
		pthread_mutex_unlock(&in->qlock);
		in->cur = m;
		int type = m.status & 0xF0;
		void *obj = NULL, *fn = NULL;
		if (type == 0x80 || type == 0x90) {
			obj = in->note_obj;
			fn = in->note_fn;
		} else if (type == 0xB0) {
			obj = in->control_obj;
			fn = in->control_fn;
		} else if (type == 0xE0) {
			obj = in->bend_obj;
			fn = in->bend_fn;
		}
		if (fn)
			cby_invoke_hook(obj, fn);
	}
}

#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)
static void *alsa_work(void *arg)
{
	Input *in = arg;
	int n = alsa.poll_descriptors_count(in->seq, POLLIN);
	struct pollfd *p = calloc((size_t)n + 1, sizeof *p);
	alsa.poll_descriptors(in->seq, p, (unsigned)n, POLLIN);
	p[n].fd = in->stop[0];
	p[n].events = POLLIN;
	for (;;) {
		if (poll(p, (nfds_t)n + 1, -1) < 0 && errno != EINTR)
			break;
		if (p[n].revents)
			break;
		SeqEvent *e;
		while (alsa.event_input(in->seq, &e) >= 0 && e) {
			Msg m = { 0, 0, 0 };
			int ch = e->data.note.channel & 0x0F;
			switch (e->type) {
			case SEQ_NOTEON:
				m = (Msg){ (uint8_t)(0x90 | ch), e->data.note.note, e->data.note.velocity };
				break;
			case SEQ_NOTEOFF:
				m = (Msg){ (uint8_t)(0x80 | ch), e->data.note.note, 0 };
				break;
			case SEQ_CONTROLLER:
				m = (Msg){ (uint8_t)(0xB0 | (e->data.control.channel & 0x0F)), (uint8_t)(e->data.control.param & 0x7F),
					   (uint8_t)(e->data.control.value & 0x7F) };
				break;
			case SEQ_PITCHBEND: {
				int v = e->data.control.value + 8192;
				v = v < 0 ? 0 : v > 16383 ? 16383 : v;
				m = (Msg){ (uint8_t)(0xE0 | (e->data.control.channel & 0x0F)), (uint8_t)(v & 0x7F), (uint8_t)(v >> 7) };
				break;
			}
			default:
				continue;
			}
			got(in, m, -1);
		}
	}
	free(p);
	return NULL;
}
#endif

#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)
/* Core MIDI's thread: its packets' bytes, read as MIDI messages (a
 * keyboard may leave the status out when it's the same: running status) */
static void mac_read(const MacPacketList *list, void *ref, void *source_ref)
{
	(void)source_ref;
	Input *in = ref;
	const MacPacket *p = &list->packet[0];
	for (uint32_t k = 0; k < list->numPackets; k++) {
		for (uint16_t i = 0; i < p->length; i++) {
			uint8_t c = p->data[i];
			if (c >= 0xF8)
				continue;   /* (the clock and the like: on their own, anywhere) */
			if (c == 0xF0) {
				in->in_sysex = 1;
				continue;
			}
			if (in->in_sysex) {
				if (c == 0xF7 || c & 0x80)
					in->in_sysex = 0;
				if (!(c & 0x80) || c == 0xF7)
					continue;
			}
			if (c & 0x80) {
				in->running = c >= 0xF0 ? 0 : c;
				in->have = 0;
				int t = c & 0xF0;
				in->need = t == 0xC0 || t == 0xD0 ? 1 : 2;
				continue;
			}
			if (!in->running)
				continue;
			in->bytes[in->have++] = c;
			if (in->have == in->need) {
				Msg m = { in->running, in->bytes[0], in->need == 2 ? in->bytes[1] : 0 };
				in->have = 0;
				got(in, m, -1);
			}
		}
#if defined(__aarch64__) || defined(__arm__)
		p = (const MacPacket *)(((uintptr_t)&p->data[p->length] + 3) & ~(uintptr_t)3);
#else
		p = (const MacPacket *)&p->data[p->length];
#endif
	}
}
#endif

/* the test's keyboard: the file's notes, played as fast as they can be
 * read, and (played through) put at their own times in the mix */
static void *file_work(void *arg)
{
	Input *in = arg;
	tml_message *song = tml_load_filename(in->file);
	int64_t start = sound_live_now();
	for (tml_message *m = song; m; m = m->next) {
		Msg msg = { (uint8_t)(m->type | (m->channel & 0x0F)), 0, 0 };
		if (m->type == 0xE0) {
			msg.a = (uint8_t)(m->pitch_bend & 0x7F);
			msg.b = (uint8_t)(m->pitch_bend >> 7);
		} else {
			msg.a = (uint8_t)m->key;
			msg.b = (uint8_t)m->velocity;
		}
		if (m->type == 0x80 || m->type == 0x90 || m->type == 0xB0 || m->type == 0xE0)
			got(in, msg, start + (int64_t)m->time * RATE / 1000);
	}
	tml_free(song);
	return NULL;
}

int64_t cby_midi_open_input(const char *name)
{
	Found f = { 0 };
	find(&f, 0);
	int i = choose(&f, name, 0);
	Input *in = calloc(1, sizeof *in);
	in->used = 1;
	in->name = strdup(f.names[i]);
	in->stop[0] = in->stop[1] = -1;
	pthread_mutex_init(&in->qlock, NULL);
	if (pipe(in->wake) != 0) {
		found_free(&f);
		fail("can't listen to the keyboard '%s': no pipe", in->name);
	}
	fcntl(in->wake[0], F_SETFL, O_NONBLOCK);
	fcntl(in->wake[1], F_SETFL, O_NONBLOCK);
	fcntl(in->wake[0], F_SETFD, FD_CLOEXEC);
	fcntl(in->wake[1], F_SETFD, FD_CLOEXEC);
	pthread_mutex_lock(&ins_lock);
	int k = 0;
	while (k < nins && ins[k])
		k++;
	if (k == nins) {
		ins = realloc(ins, sizeof *ins * (size_t)(nins + 1));
		nins++;
	}
	ins[k] = in;
	in->id = k + 1;
	pthread_mutex_unlock(&ins_lock);
	if (test_in()) {
		in->kind = 3;
		in->file = strdup(test_in());
		FILE *t = fopen(in->file, "rb");
		if (!t) {
			found_free(&f);
			fail("can't play CABBY_MIDI_IN's '%s' as a keyboard: it isn't there", in->file);
		}
		fclose(t);
		/* (the thread starts when there's something to tell: a handler,
		 * or playing through) */
	} else {
#if defined(__APPLE__)
		in->kind = 2;
		in->mac_source = f.endpoint[i];
		CFStr pname = mac.string_create(NULL, "Cabby in", CF_UTF8);
		int32_t r = mac.input_port_create(mac.client, pname, mac_read, in, &in->mac_port);
		mac.release(pname);
		if (r != 0 || mac.port_connect_source(in->mac_port, in->mac_source, NULL) != 0) {
			found_free(&f);
			fail("can't listen to the keyboard '%s' (Core MIDI said no)", in->name);
		}
#else
		in->kind = 1;
		if (seq_open(&in->seq, SEQ_OPEN_INPUT, 1 /* (not waiting) */) < 0) {
			found_free(&f);
			fail("can't listen to the keyboard '%s': the ALSA sequencer didn't open", in->name);
		}
		alsa.set_client_name(in->seq, "Cabby");
		int port = alsa.create_simple_port(in->seq, "Cabby in", SEQ_CAP_WRITE | SEQ_CAP_SUBS_WRITE,
						   SEQ_TYPE_MIDI_GENERIC | SEQ_TYPE_APPLICATION);
		if (port < 0 || alsa.connect_from(in->seq, port, f.client[i], f.port[i]) < 0) {
			found_free(&f);
			fail("can't listen to the keyboard '%s': it didn't let Cabby connect", in->name);
		}
		if (pipe(in->stop) == 0) {
			in->thread_running = 1;
			pthread_create(&in->thread, NULL, alsa_work, in);
		}
#endif
	}
	found_free(&f);
	return in->id;
}

/* (the test's file starts playing as soon as it's listened to) */
static void start_file(Input *in)
{
	if (in->kind == 3 && !in->thread_running) {
		in->thread_running = 1;
		pthread_create(&in->thread, NULL, file_work, in);
	}
}

int64_t cby_midi_input_name(int64_t id) { return give(input(id)->name); }

/* 0 on_note, 1 on_control, 2 on_bend */
void cby_midi_on(int64_t id, int64_t which, void *obj, void *fn)
{
	Input *in = input(id);
	if (which == 0) {
		in->note_obj = obj;
		in->note_fn = fn;
	} else if (which == 1) {
		in->control_obj = obj;
		in->control_fn = fn;
	} else {
		in->bend_obj = obj;
		in->bend_fn = fn;
	}
	if (!in->watch)
		in->watch = cby_events_watch(in->wake[0], tell, (void *)(intptr_t)id);
	in->telling = 1;
	start_file(in);
}

/* what the handler's told of: 0 pressed (1) or let go (0), 1 the key, 2
 * how hard (velocity), 3 the channel (1 to 16), 4 the control's number, 5
 * its value, 6 the bend (-8192 to 8191) */
int64_t cby_midi_current(int64_t id, int64_t what)
{
	Msg m = input(id)->cur;
	int type = m.status & 0xF0;
	switch (what) {
	case 0:
		return type == 0x90 && m.b > 0;
	case 1:
		return type == 0x80 || type == 0x90 ? m.a : 0;
	case 2:
		return type == 0x90 ? m.b : 0;
	case 3:
		return (m.status & 0x0F) + 1;
	case 4:
		return type == 0xB0 ? m.a : 0;
	case 5:
		return type == 0xB0 ? m.b : 0;
	case 6:
		return type == 0xE0 ? (m.a | (m.b << 7)) - 8192 : 0;
	}
	return 0;
}

/* heard at once, through one of sound's instruments ("" stops it) */
void cby_midi_play_through(int64_t id, const char *instrument)
{
	Input *in = input(id);
	int was = in->through;
	if (!*instrument) {
		in->through = 0;
		if (was)
			sound_live_end(was - 1);
		return;
	}
	int ch = sound_live_channel(instrument);
	in->through = ch + 1;
	if (was && was != ch + 1)
		sound_live_end(was - 1);
	start_file(in);
}

/* sent straight on to a synthesizer, from the keyboard's thread (0: not) */
void cby_midi_send_to(int64_t id, int64_t out)
{
	Input *in = input(id);
	if (out)
		output(out);
	in->forward = out;
	start_file(in);
}

int64_t cby_midi_program_of(const char *instrument) { return sound_program_of(instrument); }

void cby_midi_input_close(int64_t id)
{
	pthread_mutex_lock(&ins_lock);
	Input *in = id >= 1 && id <= nins ? ins[id - 1] : NULL;
	if (in)
		ins[id - 1] = NULL;
	pthread_mutex_unlock(&ins_lock);
	if (!in)
		return;
	if (in->watch)
		cby_events_unwatch(in->watch);
	in->telling = 0;
#if defined(__APPLE__) || defined(CABBY_MIDI_TRY_MAC)
	if (in->kind == 2) {
		mac.port_disconnect_source(in->mac_port, in->mac_source);
		mac.port_dispose(in->mac_port);   /* (no more calls after this) */
	}
#endif
	if (in->thread_running) {
		if (in->stop[1] >= 0) {
			char one = 1;
			if (write(in->stop[1], &one, 1) < 0) {
			}
		}
		pthread_join(in->thread, NULL);
	}
#if !defined(__APPLE__) || defined(CABBY_MIDI_TRY_ALSA)
	if (in->kind == 1)
		alsa.close(in->seq);
#endif
	if (in->through)
		sound_live_end(in->through - 1);
	if (in->stop[0] >= 0) {
		close(in->stop[0]);
		close(in->stop[1]);
	}
	close(in->wake[0]);
	close(in->wake[1]);
	pthread_mutex_destroy(&in->qlock);
	free(in->q);
	free(in->name);
	free(in->file);
	free(in);
}

/* ============================================================= the end */

__attribute__((destructor)) static void midi_end(void)
{
	for (int i = 0; i < nins; i++)
		cby_midi_input_close(i + 1);
	free(ins);
	ins = NULL;
	nins = 0;
	/* the synthesizers: notes let go, and the test's file saved */
	for (int i = 0; i < nouts; i++)
		cby_midi_output_close(i + 1);
	if (timer_running) {
		pthread_mutex_lock(&outs_lock);
		timer_running = 0;
		pthread_cond_signal(&timer_cond);
		pthread_mutex_unlock(&outs_lock);
		pthread_join(timer_thread, NULL);
	}
	free(outs);
	outs = NULL;
	nouts = 0;
	free(given);
	given = NULL;
}
