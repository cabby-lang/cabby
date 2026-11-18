/* videos.c — the videos library's C side: video files opened and decoded
 * with FFmpeg's libraries (libavformat reads the file, libavcodec decodes
 * its picture and sound, libswscale turns each picture into RGBA and
 * libswresample the sound into 48000 floats a second, in two channels).
 *
 * A video plays by a clock: its position is where it was started from,
 * plus the time since. The window that shows it (gui's video widget, by
 * video_source.h) gives its own clock, asks for the picture for now as it
 * draws, and plays the sound as the sound's own thread asks for it; the
 * sound is kept to the clock (moved on, or back, if it drifts by more than
 * a few hundredths of a second). Decoding happens as it's asked for, on
 * the window's thread, as far ahead as is needed: the next picture, and
 * half a second of sound. FFmpeg's decoders use the computer's
 * processors themselves (threads of their own).
 *
 * Under a test script, the window's clock is the script's, so what's
 * shown at each moment is the same every time. */
#include "rt.h"
#include "video_source.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* (images.c's, in the runtime) */
int64_t cby_image_from_rgba(int64_t w, int64_t h, const uint8_t *px);

#define QN 8              /* pictures decoded ahead, at most */
#define EPS 1e-6          /* (seconds: for comparing times) */
#define RATE CBY_VIDEO_RATE

typedef struct {
	uint8_t *rgba;
	double pts;
	int64_t serial;
} Frame;

typedef struct Video {
	CbyVideoSource src;
	pthread_mutex_t lock;
	char *path;
	AVFormatContext *fmt;
	int vi, ai;                    /* the picture's and the sound's streams (-1: none) */
	AVCodecContext *vdec, *adec;
	struct SwsContext *sws;
	SwrContext *swr;
	AVPacket *pkt;
	AVFrame *frm;
	double t0;                     /* the file's own start time, taken from every time in it */
	double duration, fps;
	int w, h;
	int eof;                       /* the file's been read to its end (and the decoders emptied) */
	/* pictures: decoded, waiting to be shown (the first is what's shown) */
	Frame q[QN];
	int qlen;
	uint8_t *spare[QN];
	int nspare;
	int64_t serials;
	double last_pts;               /* the last picture's time */
	/* sound: so many frames (two floats each) from sample a_first on */
	float *ring;
	int64_t a_first, a_len, a_cap;
	int a_started;
	int64_t a_cursor;              /* the next sample the sound's thread gives */
	double a_end;                  /* the time of the last sound decoded */
	/* playing */
	int playing, looping, at_end;
	double base_pos, base_clock;
	double (*clock)(void);
	int64_t ends;
	float vol;
	int attached;                  /* shown in a window */
} Video;

static Video **videos;
static int64_t nvideos;
static pthread_mutex_t table = PTHREAD_MUTEX_INITIALIZER;

static Video *vid(int64_t id)
{
	pthread_mutex_lock(&table);
	Video *v = id >= 1 && id <= nvideos ? videos[id - 1] : NULL;
	pthread_mutex_unlock(&table);
	if (!v)
		rt_panic(NULL, 0, 0, "%s", "that video has been closed");
	return v;
}

/* text handed to Cabby a byte at a time: its length, then each byte */
static _Thread_local char *handed;
static void handed_ends(void)
{
	free(handed);
	handed = NULL;
}

static int64_t hand(const char *s)
{
	static _Thread_local int hooked;
	if (!hooked) {
		hooked = 1;
		rt_on_thread_end(handed_ends);
	}
	free(handed);
	handed = strdup(s ? s : "");
	return (int64_t)strlen(handed);
}
char cby_videos_char(int64_t i) { return handed && i >= 0 && (size_t)i < strlen(handed) ? handed[i] : 0; }

static double real_clock(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ------------------------------------------------------------ opening */

static AVCodecContext *open_decoder(AVStream *st)
{
	const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
	if (!c)
		return NULL;
	AVCodecContext *d = avcodec_alloc_context3(c);
	if (!d || avcodec_parameters_to_context(d, st->codecpar) < 0) {
		avcodec_free_context(&d);
		return NULL;
	}
	d->pkt_timebase = st->time_base;
	d->thread_count = 0;   /* (as many as the computer has) */
	if (avcodec_open2(d, c, NULL) < 0) {
		avcodec_free_context(&d);
		return NULL;
	}
	return d;
}

static void quiet(void)
{
	static int done;
	if (!done) {
		done = 1;
		av_log_set_level(getenv("CABBY_VIDEOS_LOG") ? AV_LOG_INFO : AV_LOG_QUIET);
	}
}

static void free_video(Video *v)
{
	for (int i = 0; i < v->qlen; i++)
		av_free(v->q[i].rgba);
	for (int i = 0; i < v->nspare; i++)
		av_free(v->spare[i]);
	sws_freeContext(v->sws);
	swr_free(&v->swr);
	avcodec_free_context(&v->vdec);
	avcodec_free_context(&v->adec);
	av_packet_free(&v->pkt);
	av_frame_free(&v->frm);
	avformat_close_input(&v->fmt);
	free(v->ring);
	free(v->path);
	pthread_mutex_destroy(&v->lock);
	free(v);
}

/* why the last open didn't work */
static _Thread_local char why[256];

static Video *open_video(const char *path)
{
	quiet();
	Video *v = calloc(1, sizeof *v);
	pthread_mutex_init(&v->lock, NULL);
	v->vi = v->ai = -1;
	v->vol = 1;
	v->clock = real_clock;
	v->path = strdup(path);
	int err = avformat_open_input(&v->fmt, path, NULL, NULL);
	if (err < 0) {
		char e[128];
		av_strerror(err, e, sizeof e);
		snprintf(why, sizeof why, "%s", e);
		v->fmt = NULL;
		free_video(v);
		return NULL;
	}
	if (avformat_find_stream_info(v->fmt, NULL) < 0) {
		snprintf(why, sizeof why, "%s", "what's in it couldn't be made out");
		free_video(v);
		return NULL;
	}
	int vi = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (vi >= 0 && !(v->fmt->streams[vi]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
		v->vdec = open_decoder(v->fmt->streams[vi]);
		if (v->vdec) {
			v->vi = vi;
			v->w = v->vdec->width;
			v->h = v->vdec->height;
			AVRational r = av_guess_frame_rate(v->fmt, v->fmt->streams[vi], NULL);
			v->fps = r.num > 0 && r.den > 0 ? av_q2d(r) : 0;
		}
	}
	int ai = av_find_best_stream(v->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ai >= 0) {
		v->adec = open_decoder(v->fmt->streams[ai]);
		if (v->adec) {
			AVChannelLayout in = { 0 }, out = AV_CHANNEL_LAYOUT_STEREO;
			if (v->adec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || v->adec->ch_layout.nb_channels < 1)
				av_channel_layout_default(&in, v->adec->ch_layout.nb_channels > 0 ? v->adec->ch_layout.nb_channels : 2);
			else
				av_channel_layout_copy(&in, &v->adec->ch_layout);
			if (swr_alloc_set_opts2(&v->swr, &out, AV_SAMPLE_FMT_FLT, RATE, &in, v->adec->sample_fmt,
						v->adec->sample_rate, 0, NULL) >= 0 &&
			    swr_init(v->swr) >= 0)
				v->ai = ai;
			else
				avcodec_free_context(&v->adec);
			av_channel_layout_uninit(&in);
		}
	}
	if (v->vi < 0 && v->ai < 0) {
		snprintf(why, sizeof why, "%s", "there's no picture or sound in it that can be decoded");
		free_video(v);
		return NULL;
	}
	v->t0 = v->fmt->start_time != AV_NOPTS_VALUE ? (double)v->fmt->start_time / AV_TIME_BASE : 0;
	v->duration = v->fmt->duration != AV_NOPTS_VALUE ? (double)v->fmt->duration / AV_TIME_BASE : 0;
	v->pkt = av_packet_alloc();
	v->frm = av_frame_alloc();
	v->a_cap = RATE * 3;
	v->ring = malloc(sizeof(float) * 2 * (size_t)v->a_cap);
	v->last_pts = -1;
	return v;
}

/* ------------------------------------------------------------ decoding */

static double frame_time(Video *v, AVStream *st, AVFrame *f)
{
	int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
	if (ts == AV_NOPTS_VALUE)
		return -1;
	return (double)ts * av_q2d(st->time_base) - v->t0;
}

static void got_picture(Video *v, AVFrame *f)
{
	double pts = frame_time(v, v->fmt->streams[v->vi], f);
	if (pts < 0)
		pts = v->last_pts < 0 ? 0 : v->last_pts + (v->fps > 0 ? 1 / v->fps : 0.04);
	if (f->width != v->w || f->height != v->h) {
		/* (its size changed: the pictures waiting go) */
		for (int i = 0; i < v->qlen; i++)
			av_free(v->q[i].rgba);
		for (int i = 0; i < v->nspare; i++)
			av_free(v->spare[i]);
		v->qlen = v->nspare = 0;
		v->w = f->width;
		v->h = f->height;
	}
	int flags = SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT;
	v->sws = sws_getCachedContext(v->sws, f->width, f->height, f->format, v->w, v->h, AV_PIX_FMT_RGBA, flags, NULL,
				      NULL, NULL);
	if (!v->sws)
		return;
	if (v->qlen == QN) {
		/* (full: the oldest goes) */
		v->spare[v->nspare++] = v->q[0].rgba;
		memmove(v->q, v->q + 1, sizeof *v->q * (QN - 1));
		v->qlen--;
	}
	uint8_t *rgba = v->nspare ? v->spare[--v->nspare] : av_malloc((size_t)v->w * (size_t)v->h * 4);
	uint8_t *dst[4] = { rgba };
	int stride[4] = { v->w * 4 };
	sws_scale(v->sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, stride);
	v->q[v->qlen++] = (Frame){ rgba, pts, ++v->serials };
	if (pts > v->last_pts)
		v->last_pts = pts;
}

static void got_sound(Video *v, AVFrame *f)
{
	int most = swr_get_out_samples(v->swr, f->nb_samples);
	if (most <= 0)
		return;
	if (!v->a_started) {
		double pts = frame_time(v, v->fmt->streams[v->ai], f);
		v->a_first = (int64_t)llround((pts < 0 ? 0 : pts) * RATE);
		v->a_len = 0;
		v->a_started = 1;
	}
	if (v->a_len + most > v->a_cap) {
		/* (full: the oldest goes) */
		int64_t drop = v->a_len + most - v->a_cap;
		if (drop > v->a_len)
			drop = v->a_len;
		memmove(v->ring, v->ring + 2 * drop, sizeof(float) * 2 * (size_t)(v->a_len - drop));
		v->a_len -= drop;
		v->a_first += drop;
	}
	uint8_t *out = (uint8_t *)(v->ring + 2 * v->a_len);
	int n = swr_convert(v->swr, &out, most, (const uint8_t **)f->extended_data, f->nb_samples);
	if (n > 0) {
		v->a_len += n;
		v->a_end = (double)(v->a_first + v->a_len) / RATE;
	}
}

static void receive(Video *v, AVCodecContext *d, int picture)
{
	while (avcodec_receive_frame(d, v->frm) == 0) {
		if (picture)
			got_picture(v, v->frm);
		else
			got_sound(v, v->frm);
		av_frame_unref(v->frm);
	}
}

/* reads one packet from the file, and decodes it: 0 at the end */
static int step(Video *v)
{
	if (v->eof)
		return 0;
	if (av_read_frame(v->fmt, v->pkt) < 0) {
		/* the end: what the decoders still have */
		if (v->vdec) {
			avcodec_send_packet(v->vdec, NULL);
			receive(v, v->vdec, 1);
		}
		if (v->adec) {
			avcodec_send_packet(v->adec, NULL);
			receive(v, v->adec, 0);
		}
		v->eof = 1;
		return 0;
	}
	if (v->pkt->stream_index == v->vi) {
		if (avcodec_send_packet(v->vdec, v->pkt) == 0)
			receive(v, v->vdec, 1);
	} else if (v->pkt->stream_index == v->ai) {
		if (avcodec_send_packet(v->adec, v->pkt) == 0)
			receive(v, v->adec, 0);
	}
	av_packet_unref(v->pkt);
	return 1;
}

/* ------------------------------------------------------------ the clock */

static double length_of(Video *v)
{
	if (v->duration > 0)
		return v->duration;
	double a = v->a_end, p = v->last_pts >= 0 ? v->last_pts + (v->fps > 0 ? 1 / v->fps : 0) : 0;
	return a > p ? a : p;
}

static double pos_now(Video *v)
{
	double p = v->playing ? v->base_pos + (v->clock() - v->base_clock) : v->base_pos;
	if (p < 0)
		p = 0;
	if (v->duration > 0 && p > v->duration)
		p = v->duration;
	return p;
}

static void do_seek(Video *v, double t)
{
	if (t < 0)
		t = 0;
	if (v->duration > 0 && t > v->duration)
		t = v->duration;
	int64_t ts = (int64_t)((t + v->t0) * AV_TIME_BASE);
	av_seek_frame(v->fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
	if (v->vdec)
		avcodec_flush_buffers(v->vdec);
	if (v->adec) {
		avcodec_flush_buffers(v->adec);
		swr_close(v->swr);
		swr_init(v->swr);
	}
	for (int i = 0; i < v->qlen; i++)
		v->spare[v->nspare++] = v->q[i].rgba;
	v->qlen = 0;
	v->last_pts = -1;
	v->a_len = 0;
	v->a_started = 0;
	v->a_end = 0;
	v->a_cursor = (int64_t)llround(t * RATE);
	v->eof = 0;
	v->at_end = 0;
	v->base_pos = t;
	v->base_clock = v->clock();
}

/* decodes as far as is needed for Pos: the picture shown then (and the one
 * after it, to know it's the right one), and half a second of sound */
static void fill(Video *v, double pos)
{
	for (int n = 0; n < 2000; n++) {
		while (v->qlen >= 2 && v->q[1].pts <= pos + EPS) {
			v->spare[v->nspare++] = v->q[0].rgba;
			memmove(v->q, v->q + 1, sizeof *v->q * (size_t)(v->qlen - 1));
			v->qlen--;
		}
		/* (sound long gone, played or not) */
		int64_t keep = (int64_t)((pos - 0.25) * RATE);
		if (v->a_cursor < keep)
			keep = v->a_cursor;
		if (keep > v->a_first && v->a_len > 0) {
			int64_t drop = keep - v->a_first;
			if (drop > v->a_len)
				drop = v->a_len;
			memmove(v->ring, v->ring + 2 * drop, sizeof(float) * 2 * (size_t)(v->a_len - drop));
			v->a_len -= drop;
			v->a_first += drop;
		}
		int need_picture = v->vi >= 0 && (v->qlen == 0 || v->q[v->qlen - 1].pts <= pos + EPS);
		int need_sound = v->ai >= 0 && (!v->a_started || v->a_end < pos + 0.5);
		if (v->eof || !(need_picture || need_sound))
			return;
		if (!need_picture && v->qlen == QN)
			return;
		step(v);
	}
}

/* at the end: played again from the start (looping), or stopped there */
static void check_end(Video *v)
{
	if (!v->playing)
		return;
	double len = length_of(v);
	double p = v->base_pos + (v->clock() - v->base_clock);
	int done = v->duration > 0 ? p >= v->duration - EPS : (v->eof && p >= len - EPS);
	if (!done)
		return;
	if (v->looping && len > 0) {
		/* (again from the start, as far on as it's gone past the end: the
		 * same however seldom it's looked at) */
		double over = p - len;
		int64_t times = 1 + (int64_t)floor(over / len);
		v->ends += times;
		do_seek(v, over - (double)(times - 1) * len);
	} else {
		v->ends++;
		v->playing = 0;
		v->base_pos = len;
		v->at_end = 1;
	}
}

/* ------------------------------------------------------------ as a source */

static void s_attach(void *self, double (*clock)(void))
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	double p = pos_now(v);
	v->clock = clock ? clock : real_clock;
	v->attached = 1;
	v->base_pos = p;
	v->base_clock = v->clock();
	pthread_mutex_unlock(&v->lock);
}

static const uint8_t *s_picture(void *self, int *w, int *h, int64_t *serial)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	check_end(v);
	fill(v, pos_now(v));
	const uint8_t *px = NULL;
	if (v->qlen > 0) {
		px = v->q[0].rgba;
		*w = v->w;
		*h = v->h;
		*serial = v->q[0].serial;
	}
	pthread_mutex_unlock(&v->lock);
	return px;
}

static void s_sound(void *self, float *out, int frames, double ahead)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	if (!v->playing || v->ai < 0) {
		memset(out, 0, sizeof(float) * 2 * (size_t)frames);
		pthread_mutex_unlock(&v->lock);
		return;
	}
	int64_t want = (int64_t)llround((pos_now(v) + ahead) * RATE);
	if (llabs(v->a_cursor - want) > RATE * 8 / 100)
		v->a_cursor = want;
	for (int i = 0; i < frames; i++) {
		int64_t at = v->a_cursor + i - v->a_first;
		if (at >= 0 && at < v->a_len) {
			out[2 * i] = v->ring[2 * at] * v->vol;
			out[2 * i + 1] = v->ring[2 * at + 1] * v->vol;
		} else {
			out[2 * i] = out[2 * i + 1] = 0;
		}
	}
	v->a_cursor += frames;
	pthread_mutex_unlock(&v->lock);
}

static void s_play(void *self)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	if (v->at_end)
		do_seek(v, 0);
	if (!v->playing) {
		v->base_clock = v->clock();
		v->playing = 1;
	}
	pthread_mutex_unlock(&v->lock);
}

static void s_pause(void *self)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	check_end(v);
	if (v->playing) {
		v->base_pos = pos_now(v);
		v->playing = 0;
	}
	pthread_mutex_unlock(&v->lock);
}

static void s_seek(void *self, double t)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	do_seek(v, t);
	pthread_mutex_unlock(&v->lock);
}

static double s_position(void *self)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	check_end(v);
	double p = pos_now(v);
	pthread_mutex_unlock(&v->lock);
	return p;
}

static double s_duration(void *self)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	double d = length_of(v);
	pthread_mutex_unlock(&v->lock);
	return d;
}

static int s_playing(void *self)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	check_end(v);
	int p = v->playing;
	pthread_mutex_unlock(&v->lock);
	return p;
}

static int s_has_sound(void *self) { return ((Video *)self)->ai >= 0; }

static int s_size(void *self, int *w, int *h)
{
	Video *v = self;
	*w = v->w;
	*h = v->h;
	return v->vi >= 0;
}

static int64_t s_ends(void *self)
{
	Video *v = self;
	pthread_mutex_lock(&v->lock);
	check_end(v);
	int64_t e = v->ends;
	pthread_mutex_unlock(&v->lock);
	return e;
}

static float s_volume(void *self) { return ((Video *)self)->vol; }

/* ------------------------------------------------------------ for Cabby */

/* a video file opened: its number, or 0 (and error says why); a panic
 * instead, if panics */
int64_t cby_videos_open(const char *path, int panics)
{
	Video *v = open_video(path);
	if (!v && panics)
		rt_panic(NULL, 0, 0, "can't open the video '%s': %s", path, why);
	if (!v)
		return 0;
	v->src = (CbyVideoSource){ CBY_VIDEO_MAGIC, v, s_attach, s_picture, s_sound, s_play, s_pause, s_seek,
				   s_position, s_duration, s_playing, s_has_sound, s_size, s_ends, s_volume };
	pthread_mutex_lock(&table);
	int64_t i = 0;
	while (i < nvideos && videos[i])
		i++;
	if (i == nvideos) {
		videos = realloc(videos, sizeof *videos * (size_t)(nvideos + 1));
		nvideos++;
	}
	videos[i] = v;
	pthread_mutex_unlock(&table);
	return i + 1;
}

void cby_videos_close(int64_t id)
{
	if (id < 1 || id > nvideos)
		return;
	pthread_mutex_lock(&table);
	Video *v = videos[id - 1];
	videos[id - 1] = NULL;
	pthread_mutex_unlock(&table);
	if (!v)
		return;
	if (!v->attached) {
		free_video(v);
		return;
	}
	/* (a window may still be showing it: so it's kept, stopped, until
	 * the program ends) */
	pthread_mutex_lock(&v->lock);
	v->playing = 0;
	pthread_mutex_unlock(&v->lock);
	static Video **kept;
	static int nkept;
	kept = realloc(kept, sizeof *kept * (size_t)(nkept + 1));
	kept[nkept++] = v;
}

/* the source a window shows: its address */
int64_t cby_videos_source(int64_t id) { return (int64_t)(intptr_t)&vid(id)->src; }

/* 0 width, 1 height, 2 has a picture, 3 has sound */
int64_t cby_videos_number(int64_t id, int64_t what)
{
	Video *v = vid(id);
	switch (what) {
	case 0: return v->vi >= 0 ? v->w : 0;
	case 1: return v->vi >= 0 ? v->h : 0;
	case 2: return v->vi >= 0;
	case 3: return v->ai >= 0;
	}
	return 0;
}

/* 0 duration, 1 frame rate, 2 position */
double cby_videos_seconds(int64_t id, int64_t what)
{
	Video *v = vid(id);
	switch (what) {
	case 0: return s_duration(v);
	case 1: return v->vi >= 0 ? v->fps : 0;
	case 2: return s_position(v);
	}
	return 0;
}

/* 0 the picture's codec, 1 the sound's, 2 its title, 3 the file's kind,
 * 4 the file's path */
int64_t cby_videos_text(int64_t id, int64_t what)
{
	Video *v = vid(id);
	switch (what) {
	case 0: return hand(v->vi >= 0 ? avcodec_get_name(v->vdec->codec_id) : "");
	case 1: return hand(v->ai >= 0 ? avcodec_get_name(v->adec->codec_id) : "");
	case 2: {
		AVDictionaryEntry *e = av_dict_get(v->fmt->metadata, "title", NULL, 0);
		return hand(e ? e->value : "");
	}
	case 3: return hand(v->fmt->iformat ? v->fmt->iformat->name : "");
	case 4: return hand(v->path);
	}
	return hand("");
}

void cby_videos_play(int64_t id) { s_play(vid(id)); }
void cby_videos_pause(int64_t id) { s_pause(vid(id)); }
int cby_videos_playing(int64_t id) { return s_playing(vid(id)); }
void cby_videos_seek(int64_t id, double t) { s_seek(vid(id), t); }
int64_t cby_videos_ends(int64_t id) { return s_ends(vid(id)); }

void cby_videos_set_looping(int64_t id, int yes)
{
	Video *v = vid(id);
	pthread_mutex_lock(&v->lock);
	v->looping = yes;
	pthread_mutex_unlock(&v->lock);
}

int cby_videos_looping(int64_t id) { return vid(id)->looping; }

void cby_videos_set_volume(int64_t id, double level)
{
	Video *v = vid(id);
	pthread_mutex_lock(&v->lock);
	v->vol = (float)(level < 0 ? 0 : level > 1 ? 1 : level);
	pthread_mutex_unlock(&v->lock);
}

double cby_videos_volume(int64_t id) { return vid(id)->vol; }

/* the picture at that time, as an images.picture (its number); the video
 * is left there, paused or playing as it was */
int64_t cby_videos_picture_at(int64_t id, double t)
{
	Video *v = vid(id);
	if (v->vi < 0)
		rt_panic(NULL, 0, 0, "%s", "this video has no picture: it's only sound");
	pthread_mutex_lock(&v->lock);
	do_seek(v, t);
	fill(v, v->base_pos);
	int64_t pic = 0;
	if (v->qlen > 0)
		pic = cby_image_from_rgba(v->w, v->h, v->q[0].rgba);
	pthread_mutex_unlock(&v->lock);
	if (!pic)
		rt_panic(NULL, 0, 0, "%s", "there's no picture in this video at that time");
	return pic;
}

/* its sound, all of it, saved as a WAV file (48000 a second, two
 * channels, 16 bits): false if it has none, or it couldn't be written */
static void put32(FILE *f, uint32_t n)
{
	uint8_t b[4] = { (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) };
	fwrite(b, 1, 4, f);
}

static void put16(FILE *f, uint16_t n)
{
	uint8_t b[2] = { (uint8_t)n, (uint8_t)(n >> 8) };
	fwrite(b, 1, 2, f);
}

int cby_videos_save_sound(int64_t id, const char *path)
{
	Video *orig = vid(id);
	if (orig->ai < 0)
		return 0;
	/* (read again, apart, so the video's left as it was) */
	Video *v = open_video(orig->path);
	if (!v || v->ai < 0) {
		if (v)
			free_video(v);
		return 0;
	}
	FILE *f = fopen(path, "wb");
	if (!f) {
		free_video(v);
		return 0;
	}
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32(f, 16);
	put16(f, 1);
	put16(f, 2);
	put32(f, RATE);
	put32(f, RATE * 4);
	put16(f, 4);
	put16(f, 16);
	fwrite("data\0\0\0\0", 1, 8, f);
	uint32_t bytes = 0;
	v->vi = -1;   /* (only the sound) */
	avcodec_free_context(&v->vdec);
	for (;;) {
		int more = step(v);
		for (int64_t i = 0; i < v->a_len * 2; i++) {
			float s = v->ring[i] * 32768.0f;
			int16_t n = s >= 32767 ? 32767 : s <= -32768 ? -32768 : (int16_t)lrintf(s);
			put16(f, (uint16_t)n);
		}
		bytes += (uint32_t)(v->a_len * 4);
		v->a_first += v->a_len;
		v->a_len = 0;
		if (!more)
			break;
	}
	fseek(f, 4, SEEK_SET);
	put32(f, 36 + bytes);
	fseek(f, 40, SEEK_SET);
	put32(f, bytes);
	int ok = fclose(f) == 0;
	free_video(v);
	return ok;
}

/* the FFmpeg it's built on: "7.1" */
int64_t cby_videos_ffmpeg(void) { return hand(av_version_info()); }
