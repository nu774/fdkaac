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

typedef int16_t sample_t;

typedef struct buffer_t {
    sample_t *data;
    unsigned count;    /* count in frames */
    unsigned capacity; /* size in bytes   */
} buffer_t;

typedef struct extrapolater_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_reader_t *src;
    pcm_sample_description_t format;
    buffer_t buffer[2];
    unsigned nbuffer;
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

static int realloc_buffer(buffer_t *bp, size_t size)
{
    if (bp->capacity < size) {
        void *p = realloc(bp->data, size);
        if (!p) return -1;
        bp->data = p;
        bp->capacity = size;
    }
    return 0;
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

static int fetch(extrapolater_t *self, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    buffer_t *bp = &self->buffer[self->nbuffer];
    int rc = 0;

    if (realloc_buffer(bp, nframes * sfmt->bytes_per_frame) == 0) {
        rc = pcm_read_frames(self->src, bp->data, nframes);
        if (rc > 0) bp->count = rc;
    }
    if (rc > 0)
        self->nbuffer ^= 1;
    return rc <= 0 ? 0 : bp->count;
}

static int extrapolate(extrapolater_t *self, const buffer_t *bp,
                        void *dst, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned i, n = sfmt->channels_per_frame;
    float lpc[LPC_ORDER];

    for (i = 0; i < n; ++i) {
        vorbis_lpc_from_data(bp->data + i, lpc, bp->count, LPC_ORDER, n);
        vorbis_lpc_predict(lpc, &bp->data[i + n * (bp->count - LPC_ORDER)],
                           LPC_ORDER, (sample_t*)dst + i, nframes, n);
    }
    return nframes;
}

static int process1(extrapolater_t *self, void *buffer, unsigned nframes);
static int process2(extrapolater_t *self, void *buffer, unsigned nframes);
static int process3(extrapolater_t *self, void *buffer, unsigned nframes);

static int process0(extrapolater_t *self, void *buffer, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned nchannels = sfmt->channels_per_frame;
    buffer_t *bp = &self->buffer[self->nbuffer];

    if (fetch(self, nframes) < 2 * LPC_ORDER)
        memset(buffer, 0, nframes * sfmt->bytes_per_frame);
    else {
        reverse_buffer(bp->data, bp->count, nchannels);
        extrapolate(self, bp, buffer, nframes);
        reverse_buffer(buffer, nframes, nchannels);
        reverse_buffer(bp->data, bp->count, nchannels);
    }
    if (bp->count)
        self->process = process1;
    else {
        memset(bp->data, 0, nframes * sfmt->bytes_per_frame);
        bp->count = nframes;
        self->process = process2;
    }
    return nframes;
}

static int process1(extrapolater_t *self, void *buffer, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    buffer_t *bp = &self->buffer[self->nbuffer ^ 1];

    assert(bp->count <= nframes);
    memcpy(buffer, bp->data, bp->count * sfmt->bytes_per_frame); 
    if (!fetch(self, nframes)) {
        buffer_t *bbp = &self->buffer[self->nbuffer];
        if (bp->count < 2 * LPC_ORDER) {
            size_t total = bp->count + bbp->count;
            if (bbp->count &&
                realloc_buffer(bbp, total * sfmt->bytes_per_frame) == 0 &&
                realloc_buffer(bp, total * sfmt->bytes_per_frame) == 0)
            {
                memcpy(bbp->data + bbp->count * sfmt->channels_per_frame,
                       bp->data, bp->count * sfmt->bytes_per_frame);
                memcpy(bp->data, bbp->data, total * sfmt->bytes_per_frame);
                bp->count = total;
            }
        }
        if (bp->count >= 2 * LPC_ORDER)
            extrapolate(self, bp, bbp->data, nframes);
        else
            memset(bbp->data, 0, nframes * sfmt->bytes_per_frame);
        bbp->count = nframes;
        self->process = process2;
    }
    return bp->count;
}

static int process2(extrapolater_t *self, void *buffer, unsigned nframes)
{
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    buffer_t *bp = &self->buffer[self->nbuffer];
    if (bp->count < nframes)
        nframes = bp->count;
    memcpy(buffer, bp->data, nframes * sfmt->bytes_per_frame); 
    if (bp->count > nframes)
        memmove(bp->data, bp->data + nframes * sfmt->channels_per_frame,
                (bp->count - nframes) * sfmt->bytes_per_frame);
    bp->count -= nframes;
    if (bp->count == 0)
        self->process = process3;
    return nframes;
}

static int process3(extrapolater_t *self, void *buffer, unsigned nframes)
{
    return 0;
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
    free(self->buffer[0].data);
    free(self->buffer[1].data);
    free(self);
    *reader = 0;
}

static pcm_reader_vtbl_t my_vtable = {
    get_format, get_length, get_position, read_frames, teardown
};

pcm_reader_t *extrapolater_open(pcm_reader_t *reader)
{
    extrapolater_t *self = 0;

    if ((self = calloc(1, sizeof(extrapolater_t))) == 0)
        return 0;
    self->src = reader;
    self->vtbl = &my_vtable;
    self->process = process0;
    return (pcm_reader_t *)self;
}
