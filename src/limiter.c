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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "pcm_reader.h"
#include "lpc.h"

typedef struct buffer_t {
    void *data;
    unsigned count;
    unsigned capacity;
    unsigned head;
} buffer_t;

typedef struct limiter_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_reader_t *src;
    pcm_sample_description_t format;
    int64_t position;
    buffer_t buffers[1];
} limiter_t;

static inline pcm_reader_t *get_source(pcm_reader_t *reader)
{
    return ((limiter_t *)reader)->src;
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
    return ((limiter_t *)reader)->position;
}

static int reserve_buffer(buffer_t *bp, size_t required, unsigned unit)
{
    if (bp->capacity < required) {
        unsigned newsize = 1;
        void *p;
        while (newsize < required)
            newsize <<= 1;
        p = realloc(bp->data, newsize * unit);
        if (!p) return -1;
        bp->data = p;
        bp->capacity = newsize;
    }
    return 0;
}

static int read_frames(pcm_reader_t *reader, void *buffer, unsigned nframes)
{
    limiter_t *self = (limiter_t *)reader;
    unsigned i, n, res, nch = self->format.channels_per_frame;
    size_t bytes = nframes * pcm_get_format(self->src)->bytes_per_frame;
    buffer_t *ibp = &self->buffers[nch];
    float *obp = buffer;

    do {
        if (reserve_buffer(ibp, bytes, 1) < 0)
           return -1;
        res = pcm_read_frames(self->src, ibp->data, nframes);
        for (n = 0; n < nch; ++n) {
            float *ip = (float *)ibp->data, *x;
            buffer_t *bp = &self->buffers[n];
            unsigned end, limit;
            if (reserve_buffer(bp, bp->count + res, sizeof(float)) < 0)
                return -1;
            x = bp->data;
            for (i = 0; i < res; ++i)
                x[bp->count++] = pcm_clip(ip[i * nch + n], -3.0, 3.0);
            limit = bp->count;
            if (limit > 0 && res > 0) {
                float last = x[limit - 1];
                for (; limit > 0 && x[limit-1] * last > 0; --limit)
                    ;
            }
            end = bp->head;
            while (end < limit) {
                unsigned start, peak_pos;
                float peak;
                for (peak_pos = end; peak_pos < limit; ++peak_pos)
                    if (x[peak_pos] > 1.0f || x[peak_pos] < -1.0f)
                        break;
                if (peak_pos == limit)
                    break;
                start = peak_pos;
                peak = fabs(x[peak_pos]);
                while (start > bp->head && x[peak_pos] * x[start] >= 0.0f)
                    --start;
                ++start;
                for (end = peak_pos + 1; end < limit; ++end) {
                    float y;
                    if (x[peak_pos] * x[end] < 0.0f)
                        break;
                    y = fabs(x[end]);
                    if (y > peak) {
                        peak = y;
                        peak_pos = end;
                    }
                }
                if (peak < 2.0f) {
                    float a = (peak - 1.0f) / (peak * peak);
                    if (x[peak_pos] > 0.0f) a = -a;
                    for (i = start; i < end; ++i)
                        x[i] = x[i] + a * x[i] * x[i];
                } else {
                    float u = peak, v = 1.0f;
                    float a = (u - 2.0f * v) / (u * u * u);
                    float b = (3.0f * v - 2.0f * u) / (u * u);
                    if (x[peak_pos] < 0.0f) b = -b;
                    for (i = start; i < end; ++i)
                        x[i] = x[i] + b * x[i] * x[i] + a * x[i] * x[i] * x[i];
                }
            }
            bp->head = limit;
        }
        res = nframes;
        for (n = 0; n < nch; ++n)
            if (self->buffers[n].head < res)
                res = self->buffers[n].head;
        for (i = 0; i < res; ++i)
            for (n = 0; n < nch; ++n)
                *obp++ = ((float *)self->buffers[n].data)[i];
        if (res) {
            for (n = 0; n < nch; ++n) {
                buffer_t *bp = &self->buffers[n];
                float *p = bp->data;
                memmove(p, p + res, (bp->count - res) * sizeof(float));
                bp->count -= res;
                bp->head  -= res;
            }
        }
    } while (res == 0 && self->buffers[0].count);
    self->position += res;
    return res;
}

static void teardown(pcm_reader_t **reader)
{
    int i;
    limiter_t *self = (limiter_t *)*reader;
    pcm_teardown(&self->src);
    for (i = 0; i < self->format.channels_per_frame + 1; ++i)
        free(self->buffers[i].data);
    free(self);
    *reader = 0;
}

static pcm_reader_vtbl_t my_vtable = {
    get_format, get_length, get_position, read_frames, teardown
};

pcm_reader_t *limiter_open(pcm_reader_t *reader)
{
    limiter_t *self;
    int n = pcm_get_format(reader)->channels_per_frame;
    size_t size = sizeof(limiter_t) + offsetof(limiter_t, buffers[n + 1]);

    if ((self = calloc(1, size)) == 0)
        return 0;
    self->src = reader;
    self->vtbl = &my_vtable;
    self->format = *pcm_get_format(reader);
    self->format.bits_per_channel = 32;
    return (pcm_reader_t *)self;
}
