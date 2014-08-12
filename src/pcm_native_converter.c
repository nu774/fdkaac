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
#include "m4af_endian.h"
#include "pcm_reader.h"

static
inline float pcm_i2f(int32_t n)
{
    union {
        int32_t ivalue;
        float fvalue;
    } u;
    u.ivalue = n;
    return u.fvalue;
}
static
inline double pcm_i2d(int64_t n)
{
    union {
        int64_t ivalue;
        double fvalue;
    } u;
    u.ivalue = n;
    return u.fvalue;
}
static
inline int32_t pcm_s8_to_s32(int8_t n)
{
    return n << 24;
}
static
inline int32_t pcm_u8_to_s32(uint8_t n)
{
    return (n << 24) ^ 0x80000000;
}
static
inline int32_t pcm_s16le_to_s32(int16_t n)
{
    return m4af_ltoh16(n) << 16;
}
static
inline int32_t pcm_s16be_to_s32(int16_t n)
{
    return m4af_btoh16(n) << 16;
}
static
inline int32_t pcm_u16le_to_s32(uint16_t n)
{
    return (m4af_ltoh16(n) << 16) ^ 0x80000000;
}
static
inline int32_t pcm_u16be_to_s32(uint16_t n)
{
    return (m4af_btoh16(n) << 16) ^ 0x80000000;
}
static
inline int32_t pcm_s24le_to_s32(uint8_t *p)
{
    return p[0]<<8 | p[1]<<16 | p[2]<<24;
}
static
inline int32_t pcm_s24be_to_s32(uint8_t *p)
{
    return p[0]<<24 | p[1]<<16 | p[2]<<8;
}
static
inline int32_t pcm_u24le_to_s32(uint8_t *p)
{
    return pcm_s24le_to_s32(p) ^ 0x80000000;
}
static
inline int32_t pcm_u24be_to_s32(uint8_t *p)
{
    return pcm_s24be_to_s32(p) ^ 0x80000000;
}
static
inline int32_t pcm_s32le_to_s32(int32_t n)
{
    return m4af_ltoh32(n);
}
static
inline int32_t pcm_s32be_to_s32(int32_t n)
{
    return m4af_btoh32(n);
}
static
inline int32_t pcm_u32le_to_s32(int32_t n)
{
    return m4af_ltoh32(n) ^ 0x80000000;
}
static
inline int32_t pcm_u32be_to_s32(int32_t n)
{
    return m4af_btoh32(n) ^ 0x80000000;
}
static
inline float pcm_f32le_to_f32(int32_t n)
{
    return pcm_i2f(m4af_ltoh32(n));
}
static
inline float pcm_f32be_to_f32(int32_t n)
{
    return pcm_i2f(m4af_btoh32(n));
}
static
inline float pcm_f64le_to_f32(int64_t n)
{
    return pcm_i2d(m4af_ltoh64(n));
}
static
inline float pcm_f64be_to_f32(int64_t n)
{
    return pcm_i2d(m4af_btoh64(n));
}

static
int pcm_convert_to_native(const pcm_sample_description_t *format,
                          const void *input, uint32_t nframes,
                          void *result)
{
#define CONVERT(type, rtype, conv) \
    do { \
        unsigned i; \
        type *ip = (type *)input; \
        for (i = 0; i < count; ++i) { \
            ((rtype *)result)[i] = conv(ip[i]); \
        } \
    } while(0)

#define CONVERT_BYTES(rtype, conv) \
    do { \
        unsigned i, bytes_per_channel; \
        uint8_t *ip = (uint8_t *)input; \
        bytes_per_channel = PCM_BYTES_PER_CHANNEL(format); \
        for (i = 0; i < count; ++i) { \
            ((rtype *)result)[i] = conv(ip); \
            ip += bytes_per_channel; \
        } \
    } while(0)

    uint32_t count = nframes * format->channels_per_frame;
    if (!count)
        return 0;
    switch (PCM_BYTES_PER_CHANNEL(format) | format->sample_type<<4) {
    case 1 | PCM_TYPE_SINT<<4:
        CONVERT(int8_t, int32_t, pcm_s8_to_s32); break;
    case 1 | PCM_TYPE_UINT<<4:
        CONVERT(uint8_t, int32_t, pcm_u8_to_s32); break;
    case 2 | PCM_TYPE_SINT<<4:
        CONVERT(int16_t, int32_t, pcm_s16le_to_s32); break;
    case 2 | PCM_TYPE_UINT<<4:
        CONVERT(uint16_t, int32_t, pcm_u16le_to_s32); break;
    case 2 | PCM_TYPE_SINT_BE<<4:
        CONVERT(int16_t, int32_t, pcm_s16be_to_s32); break;
    case 2 | PCM_TYPE_UINT_BE<<4:
        CONVERT(int16_t, int32_t, pcm_u16be_to_s32); break;
    case 3 | PCM_TYPE_SINT<<4:
        CONVERT_BYTES(int32_t, pcm_s24le_to_s32); break;
    case 3 | PCM_TYPE_UINT<<4:
        CONVERT_BYTES(int32_t, pcm_u24le_to_s32); break;
    case 3 | PCM_TYPE_SINT_BE<<4:
        CONVERT_BYTES(int32_t, pcm_s24be_to_s32); break;
    case 3 | PCM_TYPE_UINT_BE<<4:
        CONVERT_BYTES(int32_t, pcm_u24be_to_s32); break;
    case 4 | PCM_TYPE_SINT<<4:
        CONVERT(int32_t, int32_t, pcm_s32le_to_s32); break;
    case 4 | PCM_TYPE_UINT<<4:
        CONVERT(uint32_t, int32_t, pcm_u32le_to_s32); break;
    case 4 | PCM_TYPE_FLOAT<<4:
        CONVERT(int32_t, float, pcm_f32le_to_f32); break;
    case 4 | PCM_TYPE_SINT_BE<<4:
        CONVERT(int32_t, int32_t, pcm_s32be_to_s32); break;
    case 4 | PCM_TYPE_UINT_BE<<4:
        CONVERT(uint32_t, int32_t, pcm_u32be_to_s32); break;
    case 4 | PCM_TYPE_FLOAT_BE<<4:
        CONVERT(int32_t, float, pcm_f32be_to_f32); break;
    case 8 | PCM_TYPE_FLOAT<<4:
        CONVERT(int64_t, float, pcm_f64le_to_f32); break;
    case 8 | PCM_TYPE_FLOAT_BE<<4:
        CONVERT(int64_t, float, pcm_f64be_to_f32); break;
    default:
        return -1;
    }
    return 0;
}

typedef struct pcm_native_converter_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_reader_t *src;
    pcm_sample_description_t format;
    void *pivot;
    unsigned capacity;
} pcm_native_converter_t;

static inline pcm_reader_t *get_source(pcm_reader_t *reader)
{
    return ((pcm_native_converter_t *)reader)->src;
}

static const
pcm_sample_description_t *get_format(pcm_reader_t *reader)
{
    return &((pcm_native_converter_t *)reader)->format;
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
    pcm_native_converter_t *self = (pcm_native_converter_t *)reader;
    const pcm_sample_description_t *sfmt = pcm_get_format(self->src);
    unsigned bytes = nframes * sfmt->bytes_per_frame;

    if (self->capacity < bytes) {
        void *p = realloc(self->pivot, bytes);
        if (!p) return -1;
        self->pivot = p;
        self->capacity = bytes;
    }
    nframes = pcm_read_frames(self->src, self->pivot, nframes);
    if (pcm_convert_to_native(sfmt, self->pivot, nframes, buffer) < 0)
        return -1;
    return nframes;
}

static void teardown(pcm_reader_t **reader)
{
    pcm_native_converter_t *self = (pcm_native_converter_t *)*reader;
    pcm_teardown(&self->src);
    free(self->pivot);
    free(self);
    *reader = 0;
}

static pcm_reader_vtbl_t my_vtable = {
    get_format, get_length, get_position, read_frames, teardown
};

pcm_reader_t *pcm_open_native_converter(pcm_reader_t *reader)
{
    pcm_native_converter_t *self = 0;
    pcm_sample_description_t *fmt;

    if ((self = calloc(1, sizeof(pcm_native_converter_t))) == 0)
        return 0;
    self->src = reader;
    self->vtbl = &my_vtable;
    memcpy(&self->format, pcm_get_format(reader), sizeof(self->format));
    fmt = &self->format;
    fmt->sample_type = PCM_IS_FLOAT(fmt) ?  PCM_TYPE_FLOAT : PCM_TYPE_SINT;
    fmt->bytes_per_frame = 4 * fmt->channels_per_frame;
    return (pcm_reader_t *)self;
}
