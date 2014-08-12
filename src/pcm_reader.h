/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef PCM_READER_H
#define PCM_READER_H

#include "lpcm.h"
#include "metadata.h"

typedef struct pcm_reader_t pcm_reader_t;

typedef struct pcm_reader_vtbl_t {
    const pcm_sample_description_t *(*get_format)(pcm_reader_t *);
    int64_t (*get_length)(pcm_reader_t *);
    int64_t (*get_position)(pcm_reader_t *);
    int (*read_frames)(pcm_reader_t *, void *, unsigned);
    void (*teardown)(pcm_reader_t **);
} pcm_reader_vtbl_t;

struct pcm_reader_t {
    pcm_reader_vtbl_t *vtbl;
};

typedef int (*pcm_read_callback)(void *cookie, void *data, uint32_t count);
typedef int (*pcm_seek_callback)(void *cookie, int64_t off, int whence);
typedef int64_t (*pcm_tell_callback)(void *cookie);

typedef struct pcm_io_vtbl_t {
    pcm_read_callback read;
    pcm_seek_callback seek;
    pcm_tell_callback tell;
} pcm_io_vtbl_t;

typedef struct pcm_io_context_t {
    pcm_io_vtbl_t *vtbl;
    void *cookie;
} pcm_io_context_t;

static inline
const pcm_sample_description_t *pcm_get_format(pcm_reader_t *r)
{
    return r->vtbl->get_format(r);
}

static inline
int64_t pcm_get_length(pcm_reader_t *r)
{
    return r->vtbl->get_length(r);
}

static inline
int64_t pcm_get_position(pcm_reader_t *r)
{
    return r->vtbl->get_position(r);
}

int pcm_read_frames(pcm_reader_t *r, void *data, unsigned nframes);

static inline
void pcm_teardown(pcm_reader_t **r)
{
    (*r)->vtbl->teardown(r);
}

static inline
uint32_t bitcount(uint32_t bits)
{
    bits = (bits & 0x55555555) + (bits >> 1 & 0x55555555);
    bits = (bits & 0x33333333) + (bits >> 2 & 0x33333333);
    bits = (bits & 0x0f0f0f0f) + (bits >> 4 & 0x0f0f0f0f);
    bits = (bits & 0x00ff00ff) + (bits >> 8 & 0x00ff00ff);
    return (bits & 0x0000ffff) + (bits >>16 & 0x0000ffff);
}

#define TRY_IO(expr) \
    do { \
        if ((expr)) goto FAIL; \
    } while (0)

#define ENSURE(expr) \
    do { \
        if (!(expr)) goto FAIL;\
    } while (0)

int pcm_read(pcm_io_context_t *io, void *buffer, uint32_t size);
int pcm_skip(pcm_io_context_t *io, int64_t count);

static inline int pcm_seek(pcm_io_context_t *io, int64_t off, int whence)
{
    return io->vtbl->seek ? io->vtbl->seek(io->cookie, off, whence) : -1;
}

static inline int64_t pcm_tell(pcm_io_context_t *io)
{
    return io->vtbl->tell ? io->vtbl->tell(io->cookie) : -1;
}

int pcm_read16le(pcm_io_context_t *io, uint16_t *value);
int pcm_read16be(pcm_io_context_t *io, uint16_t *value);
int pcm_read32le(pcm_io_context_t *io, uint32_t *value);
int pcm_read32be(pcm_io_context_t *io, uint32_t *value);
int pcm_read64le(pcm_io_context_t *io, uint64_t *value);
int pcm_read64be(pcm_io_context_t *io, uint64_t *value);
int pcm_scanl(pcm_io_context_t *io, const char *fmt, ...);
int pcm_scanb(pcm_io_context_t *io, const char *fmt, ...);

int apple_chan_chunk(pcm_io_context_t *io, uint32_t chunk_size,
                     pcm_sample_description_t *fmt, uint8_t *mapping);

pcm_reader_t *wav_open(pcm_io_context_t *io, int ignore_length);
pcm_reader_t *raw_open(pcm_io_context_t *io,
                       const pcm_sample_description_t *desc);
pcm_reader_t *caf_open(pcm_io_context_t *io,
                       aacenc_tag_callback_t tag_callback, void *tag_ctx);

pcm_reader_t *pcm_open_native_converter(pcm_reader_t *reader);
pcm_reader_t *pcm_open_float_converter(pcm_reader_t *reader);
pcm_reader_t *pcm_open_sint16_converter(pcm_reader_t *reader);

pcm_reader_t *extrapolater_open(pcm_reader_t *reader);
pcm_reader_t *limiter_open(pcm_reader_t *reader);

#endif
