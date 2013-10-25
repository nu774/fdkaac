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
#include <math.h>
#include "lpcm.h"
#include "m4af_endian.h"

#if defined(_MSC_VER) && _MSC_VER < 1700
#  ifdef _M_IX86
inline int lrint(double x)
{
    int n;
    _asm {
        fld x
        fistp n
    }
    return n;
}
#  else
#    include <emmintrin.h>
inline int lrint(double x)
{
    return _mm_cvtsd_si32(_mm_load_sd(&x));
}
#  endif
#endif

static
inline double pcm_clip(double n, double min_value, double max_value)
{
    if (n < min_value)
        return min_value;
    else if (n > max_value)
        return max_value;
    return n;
}
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
inline int16_t pcm_quantize_s32(int32_t n)
{
    n = ((n >> 15) + 1) >> 1;
    return (n == 0x8000) ? 0x7fff : n;
}
static
inline int16_t pcm_quantize_f64(double v)
{
    return (int16_t)lrint(pcm_clip(v * 32768.0, -32768.0, 32767.0));
}
static
inline int16_t pcm_s8_to_s16(int8_t n)
{
    return n << 8;
}
static
inline int16_t pcm_u8_to_s16(uint8_t n)
{
    return (n << 8) ^ 0x8000;
}
static
inline int16_t pcm_s16le_to_s16(int16_t n)
{
    return m4af_ltoh16(n);
}
static
inline int16_t pcm_s16be_to_s16(int16_t n)
{
    return m4af_btoh16(n);
}
static
inline int16_t pcm_u16le_to_s16(uint16_t n)
{
    return m4af_ltoh16(n) ^ 0x8000;
}
static
inline int16_t pcm_u16be_to_s16(uint16_t n)
{
    return m4af_btoh16(n) ^ 0x8000;
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
inline int16_t pcm_s24le_to_s16(uint8_t *p)
{
    return pcm_quantize_s32(pcm_s24le_to_s32(p));
}
static
inline int16_t pcm_s24be_to_s16(uint8_t *p)
{
    return pcm_quantize_s32(pcm_s24be_to_s32(p));
}
static
inline int16_t pcm_u24le_to_s16(uint8_t *p)
{
    return pcm_quantize_s32(pcm_u24le_to_s32(p));
}
static
inline int16_t pcm_u24be_to_s16(uint8_t *p)
{
    return pcm_quantize_s32(pcm_u24be_to_s32(p));
}
static
inline int16_t pcm_s32le_to_s16(int32_t n)
{
    return pcm_quantize_s32(m4af_ltoh32(n));
}
static
inline int16_t pcm_s32be_to_s16(int32_t n)
{
    return pcm_quantize_s32(m4af_btoh32(n));
}
static
inline int16_t pcm_u32le_to_s16(int32_t n)
{
    return pcm_quantize_s32(m4af_ltoh32(n) ^ 0x80000000);
}
static
inline int16_t pcm_u32be_to_s16(int32_t n)
{
    return pcm_quantize_s32(m4af_btoh32(n) ^ 0x80000000);
}
static
inline int16_t pcm_f32le_to_s16(int32_t n)
{
    return pcm_quantize_f64(pcm_i2f(m4af_ltoh32(n)));
}
static
inline int16_t pcm_f32be_to_s16(int32_t n)
{
    return pcm_quantize_f64(pcm_i2f(m4af_btoh32(n)));
}
static
inline int16_t pcm_f64le_to_s16(int64_t n)
{
    return pcm_quantize_f64(pcm_i2d(m4af_ltoh64(n)));
}
static
inline int16_t pcm_f64be_to_s16(int64_t n)
{
    return pcm_quantize_f64(pcm_i2d(m4af_btoh64(n)));
}

int pcm_convert_to_native_sint16(const pcm_sample_description_t *format,
                                 const void *input, uint32_t nframes,
                                 int16_t *result)
{
#define CONVERT(type, conv) \
    do { \
        unsigned i; \
        type *ip = (type *)input; \
        for (i = 0; i < count; ++i) { \
            result[i] = conv(ip[i]); \
        } \
    } while(0)

#define CONVERT_BYTES(conv) \
    do { \
        unsigned i, bytes_per_channel; \
        uint8_t *ip = (uint8_t *)input; \
        bytes_per_channel = PCM_BYTES_PER_CHANNEL(format); \
        for (i = 0; i < count; ++i) { \
            result[i] = conv(ip); \
            ip += bytes_per_channel; \
        } \
    } while(0)

    uint32_t count = nframes * format->channels_per_frame;
    if (!count)
        return 0;
    switch (PCM_BYTES_PER_CHANNEL(format) | format->sample_type<<4) {
    case 1 | PCM_TYPE_SINT<<4:
        CONVERT(int8_t, pcm_s8_to_s16); break;
    case 1 | PCM_TYPE_UINT<<4:
        CONVERT(uint8_t, pcm_u8_to_s16); break;
    case 2 | PCM_TYPE_SINT<<4:
        CONVERT(int16_t, pcm_s16le_to_s16); break;
    case 2 | PCM_TYPE_UINT<<4:
        CONVERT(uint16_t, pcm_u16le_to_s16); break;
    case 2 | PCM_TYPE_SINT_BE<<4:
        CONVERT(int16_t, pcm_s16be_to_s16); break;
    case 2 | PCM_TYPE_UINT_BE<<4:
        CONVERT(int16_t, pcm_u16be_to_s16); break;
    case 3 | PCM_TYPE_SINT<<4:
        CONVERT_BYTES(pcm_s24le_to_s16); break;
    case 3 | PCM_TYPE_UINT<<4:
        CONVERT_BYTES(pcm_u24le_to_s16); break;
    case 3 | PCM_TYPE_SINT_BE<<4:
        CONVERT_BYTES(pcm_s24be_to_s16); break;
    case 3 | PCM_TYPE_UINT_BE<<4:
        CONVERT_BYTES(pcm_u24be_to_s16); break;
    case 4 | PCM_TYPE_SINT<<4:
        CONVERT(int32_t, pcm_s32le_to_s16); break;
    case 4 | PCM_TYPE_UINT<<4:
        CONVERT(uint32_t, pcm_u32le_to_s16); break;
    case 4 | PCM_TYPE_FLOAT<<4:
        CONVERT(int32_t, pcm_f32le_to_s16); break;
    case 4 | PCM_TYPE_SINT_BE<<4:
        CONVERT(int32_t, pcm_s32be_to_s16); break;
    case 4 | PCM_TYPE_UINT_BE<<4:
        CONVERT(uint32_t, pcm_u32be_to_s16); break;
    case 4 | PCM_TYPE_FLOAT_BE<<4:
        CONVERT(int32_t, pcm_f32be_to_s16); break;
    case 8 | PCM_TYPE_FLOAT<<4:
        CONVERT(int64_t, pcm_f64le_to_s16); break;
    case 8 | PCM_TYPE_FLOAT_BE<<4:
        CONVERT(int64_t, pcm_f64be_to_s16); break;
    default:
        return -1;
    }
    return 0;
}
