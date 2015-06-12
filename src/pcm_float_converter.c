/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
#  include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#include "pcm_reader.h"

typedef struct pcm_float_converter_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_reader_t *src;
    pcm_sample_description_t format;
} pcm_float_converter_t;

static inline pcm_reader_t *get_source(pcm_reader_t *reader)
{
    return ((pcm_float_converter_t *)reader)->src;
}

static const
pcm_sample_description_t *get_format(pcm_reader_t *reader)
{
    return &((pcm_float_converter_t *)reader)->format;
}

static int64_t get_length(pcm_reader_t *reader)
{
    return pcm_get_length(get_source(reader));
}

static int64_t get_position(pcm_reader_t *reader)
{
    return pcm_get_position(get_source(reader));
}

static int read_frames(pcm_reader_t *reader, void *buffer, unsigned nframes)
{
    pcm_float_converter_t *self = (pcm_float_converter_t *)reader;
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    nframes = pcm_read_frames(self->src, buffer, nframes);
    if (!(sfmt->sample_type & PCM_TYPE_FLOAT)) {
        int32_t *ip = buffer;
        float   *op = buffer;
        unsigned i, count = nframes * sfmt->channels_per_frame;
        for (i = 0; i < count; ++i)
            op[i] = ip[i] / 2147483648.0f;
    }
    return nframes;
}

static void teardown(pcm_reader_t **reader)
{
    pcm_float_converter_t *self = (pcm_float_converter_t *)*reader;
    pcm_teardown(&self->src);
    free(self);
    *reader = 0;
}

static pcm_reader_vtbl_t my_vtable = {
    get_format, get_length, get_position, read_frames, teardown
};

pcm_reader_t *pcm_open_float_converter(pcm_reader_t *reader)
{
    pcm_float_converter_t *self = 0;
    pcm_sample_description_t *fmt;

    if ((self = calloc(1, sizeof(pcm_float_converter_t))) == 0)
        return 0;
    self->src = reader;
    self->vtbl = &my_vtable;
    memcpy(&self->format, pcm_get_format(reader), sizeof(self->format));
    fmt = &self->format;
    fmt->bits_per_channel = 32;
    fmt->sample_type = PCM_TYPE_FLOAT;
    fmt->bytes_per_frame = 4 * fmt->channels_per_frame;
    return (pcm_reader_t *)self;
}
