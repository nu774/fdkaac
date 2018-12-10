/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef M4AF_H
#define M4AF_H

#define M4AF_FOURCC(a,b,c,d) (((a)<<24)|((b)<<16)|((c)<<8)|(d))

enum m4af_error_code {
    M4AF_IO_ERROR      = -1,
    M4AF_NO_MEMORY     = -2,
    M4AF_FORMAT_ERROR  = -3,
    M4AF_NOT_SUPPORTED = -4,
};

enum m4af_itmf_tag {
    M4AF_TAG_TITLE            = M4AF_FOURCC('\xa9','n','a','m'),
    M4AF_TAG_ARTIST           = M4AF_FOURCC('\xa9','A','R','T'),
    M4AF_TAG_ALBUM            = M4AF_FOURCC('\xa9','a','l','b'),
    M4AF_TAG_GENRE            = M4AF_FOURCC('\xa9','g','e','n'),
    M4AF_TAG_DATE             = M4AF_FOURCC('\xa9','d','a','y'),
    M4AF_TAG_COMPOSER         = M4AF_FOURCC('\xa9','w','r','t'),
    M4AF_TAG_GROUPING         = M4AF_FOURCC('\xa9','g','r','p'),
    M4AF_TAG_COMMENT          = M4AF_FOURCC('\xa9','c','m','t'),
    M4AF_TAG_LYRICS           = M4AF_FOURCC('\xa9','l','y','r'),
    M4AF_TAG_TOOL             = M4AF_FOURCC('\xa9','t','o','o'),
    M4AF_TAG_ALBUM_ARTIST     = M4AF_FOURCC('a','A','R','T'),
    M4AF_TAG_TRACK            = M4AF_FOURCC('t','r','k','n'),
    M4AF_TAG_DISK             = M4AF_FOURCC('d','i','s','k'),
    M4AF_TAG_GENRE_ID3        = M4AF_FOURCC('g','n','r','e'),
    M4AF_TAG_TEMPO            = M4AF_FOURCC('t','m','p','o'),
    M4AF_TAG_DESCRIPTION      = M4AF_FOURCC('d','e','s','c'),
    M4AF_TAG_LONG_DESCRIPTION = M4AF_FOURCC('l','d','e','s'),
    M4AF_TAG_COPYRIGHT        = M4AF_FOURCC('c','p','r','t'),
    M4AF_TAG_COMPILATION      = M4AF_FOURCC('c','p','i','l'),
    M4AF_TAG_ARTWORK          = M4AF_FOURCC('c','o','v','r'),
};

enum m4af_itmf_type_code {
    M4AF_IMPLICIT = 0,
    M4AF_UTF8 = 1,
    M4AF_GIF = 12,
    M4AF_JPEG = 13,
    M4AF_PNG = 14,
    M4AF_INTEGER = 21,
};

enum m4af_codec_type {
    M4AF_CODEC_MP4A = M4AF_FOURCC('m','p','4','a'),
    M4AF_CODEC_ALAC = M4AF_FOURCC('a','l','a','c'),
    M4AF_CODEC_TEXT = M4AF_FOURCC('t','e','x','t'),
};

enum m4af_priming_mode {
    M4AF_PRIMING_MODE_ITUNSMPB = 1,
    M4AF_PRIMING_MODE_EDTS = 2,
    M4AF_PRIMING_MODE_BOTH = 3
};

typedef int (*m4af_read_callback)(void *cookie, void *buffer, uint32_t size);
typedef int (*m4af_write_callback)(void *cookie, const void *data,
                                   uint32_t size);
typedef int (*m4af_seek_callback)(void *cookie, int64_t off, int whence);
typedef int64_t (*m4af_tell_callback)(void *cookie);

typedef struct m4af_io_callbacks_t {
    m4af_read_callback read;
    m4af_write_callback write;
    m4af_seek_callback seek;
    m4af_tell_callback tell;
} m4af_io_callbacks_t;

typedef struct m4af_ctx_t m4af_ctx_t;


m4af_ctx_t *m4af_create(uint32_t codec, uint32_t timescale,
                        m4af_io_callbacks_t *io, void *io_cookie, int no_timestamp);

int m4af_begin_write(m4af_ctx_t *ctx);

int m4af_finalize(m4af_ctx_t *ctx, int optimize);

void m4af_teardown(m4af_ctx_t **ctx);

int m4af_write_sample(m4af_ctx_t *ctx, uint32_t track_idx, const void *data,
                      uint32_t size, uint32_t duration);

int m4af_set_decoder_specific_info(m4af_ctx_t *ctx, uint32_t track_idx,
                                   uint8_t *data, uint32_t size);

void m4af_set_vbr_mode(m4af_ctx_t *ctx, uint32_t track_idx, int is_vbr);

void m4af_set_priming(m4af_ctx_t *ctx, uint32_t track_idx,
                      uint32_t encoder_delay, uint32_t padding);

void m4af_set_priming_mode(m4af_ctx_t *ctx, int mode);

void m4af_set_num_channels(m4af_ctx_t *ctx, uint32_t track_idx,
                           uint16_t channels);

void m4af_set_fixed_frame_duration(m4af_ctx_t *ctx, uint32_t track_idx,
                                   uint32_t length);

int m4af_add_itmf_long_tag(m4af_ctx_t *ctx, const char *name,
                           const char *data);

int m4af_add_itmf_short_tag(m4af_ctx_t *ctx, uint32_t fcc, uint32_t type_code,
                            const void *data, uint32_t data_size);

int m4af_add_itmf_string_tag(m4af_ctx_t *ctx, uint32_t fcc, const char *data);

int m4af_add_itmf_int8_tag(m4af_ctx_t *ctx, uint32_t fcc, int value);

int m4af_add_itmf_int16_tag(m4af_ctx_t *ctx, uint32_t fcc, int value);

int m4af_add_itmf_int32_tag(m4af_ctx_t *ctx, uint32_t fcc, uint32_t value);

int m4af_add_itmf_int64_tag(m4af_ctx_t *ctx, uint32_t fcc, uint64_t value);

int m4af_add_itmf_track_tag(m4af_ctx_t *ctx, int track, int total);

int m4af_add_itmf_disk_tag(m4af_ctx_t *ctx, int disk, int total);

int m4af_add_itmf_genre_tag(m4af_ctx_t *ctx, int genre);
#endif
