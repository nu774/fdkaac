/*
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "pcm_reader.h"
#include "lpc.h"

/* The extrapolater sits directly after pcm_sint16_converter and works on
 * the same sample representation libfdk-aac expects, whatever width the
 * library was built with. */
typedef INT_PCM sample_t;

/* A plain frame buffer, sized once at open() time. */
typedef struct pcmbuf_t {
    sample_t *data;
    unsigned count;    /* valid frames currently stored */
    unsigned capacity; /* allocated capacity, in frames  */
} pcmbuf_t;

typedef struct extrapolater_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_reader_t *src;
    unsigned pad_frames; /* frames extrapolated at each end of the stream,
                          * fixed at open() time; unrelated to whatever
                          * chunk size read_frames() happens to be called
                          * with -- this stage doesn't know or care        */

    pcmbuf_t ctx;  /* trailing window of up-to-pad_frames real samples,
                    * used as LPC context for both ends of the stream    */
    unsigned last_read; /* frame count of the single most recent read from
                         * src (the initial lead read, or the latest body
                         * pass-through read) -- see generate_pad()       */
    pcmbuf_t lead; /* real audio read ahead at open() time to compute the
                    * pre-roll pad, awaiting hand-off to the caller      */
    unsigned lead_pos;
    pcmbuf_t pad;  /* generated extrapolated (or silent) pad frames;
                    * reused for both the pre-roll and post-roll, since
                    * the two are never pending at the same time        */
    unsigned pad_pos;

    int (*process)(struct extrapolater_t *, void *, unsigned);
} extrapolater_t;

#define LPC_ORDER 32

static inline pcm_reader_t *get_source(pcm_reader_t *reader)
{
    return ((extrapolater_t *)reader)->src;
}

static const
pcm_sample_description_t *get_format(pcm_reader_t *reader)
{
    return pcm_get_format(get_source(reader));
}

static int64_t get_length(pcm_reader_t *reader)
{
    return pcm_get_length(get_source(reader));
}

static int64_t get_position(pcm_reader_t *reader)
{
    return pcm_get_position(get_source(reader));
}

static int pcmbuf_alloc(pcmbuf_t *b, unsigned nframes, unsigned bytes_per_frame)
{
    if ((b->data = malloc((size_t)nframes * bytes_per_frame)) == 0)
        return -1;
    b->capacity = nframes;
    b->count = 0;
    return 0;
}

/*
 * Append nframes new frames to the tail of the fifo, evicting whichever
 * oldest frames no longer fit in "capacity". Handles any nframes,
 * including a single push bigger than the whole window, so callers don't
 * need to match their chunk size to the window size.
 */
static void fifo_push(pcmbuf_t *fifo, const sample_t *data, unsigned nframes,
                       unsigned nchannels)
{
    if (nframes >= fifo->capacity) {
        memcpy(fifo->data,
               data + (size_t)(nframes - fifo->capacity) * nchannels,
               (size_t)fifo->capacity * nchannels * sizeof(sample_t));
        fifo->count = fifo->capacity;
        return;
    }
    if (fifo->count + nframes > fifo->capacity) {
        unsigned drop = fifo->count + nframes - fifo->capacity;
        memmove(fifo->data, fifo->data + (size_t)drop * nchannels,
                (size_t)(fifo->count - drop) * nchannels * sizeof(sample_t));
        fifo->count -= drop;
    }
    memcpy(fifo->data + (size_t)fifo->count * nchannels, data,
           (size_t)nframes * nchannels * sizeof(sample_t));
    fifo->count += nframes;
}

/* Copy out up to nframes frames starting at *pos, advancing *pos. */
static unsigned emit_from(pcmbuf_t *buf, unsigned *pos, void *dst,
                          unsigned nframes, unsigned nchannels)
{
    unsigned n = buf->count - *pos;
    if (n > nframes) n = nframes;
    memcpy(dst, buf->data + (size_t)(*pos) * nchannels,
           (size_t)n * nchannels * sizeof(sample_t));
    *pos += n;
    return n;
}

static void reverse_buffer(sample_t *data, unsigned nframes, unsigned nchannels)
{
    unsigned i = 0, j = nchannels * (nframes - 1), n;

    for (; i < j; i += nchannels, j -= nchannels) {
        for (n = 0; n < nchannels; ++n) {
            sample_t tmp = data[i + n];
            data[i + n] = data[j + n];
            data[j + n] = tmp;
        }
    }
}

/*
 * Predict self->pad_frames frames that would follow the last ctx_frames
 * frames of self->ctx (i.e. ctx's own trailing window, not necessarily
 * the whole of it -- see generate_pad()).
 */
static void extrapolate(extrapolater_t *self, void *dst, unsigned ctx_frames)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned i, n = sfmt->channels_per_frame;
    sample_t *tail = self->ctx.data + (size_t)(self->ctx.count - ctx_frames) * n;
    float lpc[LPC_ORDER];

    for (i = 0; i < n; ++i) {
        vorbis_lpc_from_data(tail + i, lpc, ctx_frames, LPC_ORDER, n);
        vorbis_lpc_predict(lpc, &tail[i + n * (ctx_frames - LPC_ORDER)],
                           LPC_ORDER, (sample_t*)dst + i, self->pad_frames, n);
    }
}

/*
 * Predict self->pad_frames frames that would precede self->ctx (the whole
 * of it): run the same forward predictor over the time-reversed context,
 * then reverse the result back. self->ctx is restored to its original
 * order afterward, since it's a persistent sliding window still needed
 * later.
 */
static void extrapolate_backward(extrapolater_t *self, void *dst)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned nchannels = sfmt->channels_per_frame;
    pcmbuf_t *ctx = &self->ctx;

    reverse_buffer(ctx->data, ctx->count, nchannels);
    extrapolate(self, dst, ctx->count);
    reverse_buffer(dst, self->pad_frames, nchannels);
    reverse_buffer(ctx->data, ctx->count, nchannels);
}

/*
 * Fill self->pad with pad_frames frames of extrapolated (or, if there
 * isn't enough context to fit an LPC_ORDER model, silent) audio.
 *
 * For the post-roll, the context is normally just the single most recent
 * chunk read from src (self->last_read frames) -- not the full trailing
 * window -- so that a healthily-sized final chunk is extrapolated from
 * itself alone. Only when that last chunk is too short on its own does
 * this fall back to the full window, which reaches back into the
 * previous chunk for enough context.
 */
static void generate_pad(extrapolater_t *self, int backward)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned ctx_frames = self->ctx.count;

    if (!backward && self->last_read >= 2 * LPC_ORDER
        && self->last_read < ctx_frames)
        ctx_frames = self->last_read;

    self->pad.count = self->pad_frames;
    self->pad_pos = 0;
    if (ctx_frames < 2 * LPC_ORDER)
        memset(self->pad.data, 0, self->pad_frames * sfmt->bytes_per_frame);
    else if (backward)
        extrapolate_backward(self, self->pad.data);
    else
        extrapolate(self, self->pad.data, ctx_frames);
}

static int state_body(extrapolater_t *self, void *buffer, unsigned nframes);
static int state_postroll(extrapolater_t *self, void *buffer, unsigned nframes);

static int state_eof(extrapolater_t *self, void *buffer, unsigned nframes)
{
    (void)self;
    (void)buffer;
    (void)nframes;
    return 0;
}

/* Hand out the pre-roll pad generated at open() time, however many calls
 * it takes -- nframes need not have any particular relationship to
 * pad_frames. */
static int state_preroll(extrapolater_t *self, void *buffer, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned n = emit_from(&self->pad, &self->pad_pos, buffer, nframes,
                           sfmt->channels_per_frame);
    if (self->pad_pos == self->pad.count)
        self->process = state_body;
    return n;
}

/*
 * Hand out real audio: first whatever was read ahead at open() time to
 * compute the pre-roll, then a direct pass-through of self->src, keeping
 * self->ctx topped up with trailing context for the post-roll. Once src
 * is exhausted, generate the post-roll pad and fall through to serving
 * it immediately, so this call never returns 0 while there's still more
 * data to come.
 */
static int state_body(extrapolater_t *self, void *buffer, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    int n;

    if (self->lead_pos < self->lead.count)
        return emit_from(&self->lead, &self->lead_pos, buffer, nframes,
                         sfmt->channels_per_frame);

    n = pcm_read_frames(self->src, buffer, nframes);
    if (n > 0) {
        fifo_push(&self->ctx, buffer, n, sfmt->channels_per_frame);
        self->last_read = n;
        return n;
    }

    generate_pad(self, /* backward = */ 0);
    self->process = state_postroll;
    return state_postroll(self, buffer, nframes);
}

/* Hand out the post-roll pad generated at end-of-stream. */
static int state_postroll(extrapolater_t *self, void *buffer, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned n = emit_from(&self->pad, &self->pad_pos, buffer, nframes,
                           sfmt->channels_per_frame);
    if (self->pad_pos == self->pad.count)
        self->process = state_eof;
    return n;
}

static int read_frames(pcm_reader_t *reader, void *buffer, unsigned nframes)
{
    extrapolater_t *self = (extrapolater_t *)reader;
    return self->process(self, buffer, nframes);
}

static void teardown(pcm_reader_t **reader)
{
    extrapolater_t *self = (extrapolater_t *)*reader;
    pcm_teardown(&self->src);
    free(self->ctx.data);
    free(self->lead.data);
    free(self->pad.data);
    free(self);
    *reader = 0;
}

static pcm_reader_vtbl_t my_vtable = {
    get_format, get_length, get_position, read_frames, teardown
};

/*
 * Wrap reader so that pad_frames of LPC-extrapolated (or silent, if
 * there isn't enough audio to fit an LPC_ORDER model) samples are
 * inserted before and after the real audio, giving the downstream
 * encoder extra context at both ends instead of a hard edge.
 *
 * pad_frames is a plain PCM-domain sample count, chosen by the caller;
 * this stage has no notion of "AAC frames" and doesn't require
 * read_frames() to be called with any particular, let alone consistent,
 * nframes.
 */
pcm_reader_t *extrapolater_open(pcm_reader_t *reader, unsigned pad_frames)
{
    extrapolater_t *self = 0;
    const pcm_sample_description_t *sfmt = pcm_get_format(reader);
    unsigned bpf = sfmt->bytes_per_frame;

    assert(bpf == sizeof(sample_t) * sfmt->channels_per_frame);

    if ((self = calloc(1, sizeof(extrapolater_t))) == 0)
        return 0;
    self->src = reader;
    self->vtbl = &my_vtable;
    self->pad_frames = pad_frames;

    if (pcmbuf_alloc(&self->ctx, pad_frames, bpf) < 0 ||
        pcmbuf_alloc(&self->lead, pad_frames, bpf) < 0 ||
        pcmbuf_alloc(&self->pad, pad_frames, bpf) < 0)
    {
        free(self->ctx.data);
        free(self->lead.data);
        free(self->pad.data);
        free(self);
        return 0;
    }

    self->lead.count = pcm_read_frames(reader, self->lead.data, pad_frames);
    self->last_read = self->lead.count;
    if (self->lead.count)
        fifo_push(&self->ctx, self->lead.data, self->lead.count,
                 sfmt->channels_per_frame);

    generate_pad(self, /* backward = */ 1);
    self->process = state_preroll;
    return (pcm_reader_t *)self;
}
