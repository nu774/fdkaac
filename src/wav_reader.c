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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "wav_reader.h"
#include "m4af_endian.h"

#define RIFF_FOURCC(a,b,c,d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))

#define TRY_IO(expr) \
    do { \
        if (expr) \
            goto FAIL; \
    } while (0)

#define ASSERT_FORMAT(ctx, expr) \
    do { \
        if (!expr) { \
            if (!ctx->last_error) \
                ctx->last_error = WAV_INVALID_FORMAT; \
            goto FAIL;\
        } \
    } while (0)

struct wav_reader_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_sample_description_t sample_format;
    int64_t length;
    int64_t position;
    int32_t data_offset;
    int ignore_length;
    int last_error;
    wav_io_context_t io;
    void *io_cookie;
};

static const uint8_t WAV_GUID_PCM[] = {
    1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71
};
static const uint8_t WAV_GUID_FLOAT[] = {
    3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71
};

static const pcm_sample_description_t *wav_get_format(pcm_reader_t *reader)
{
    return &((wav_reader_t *)reader)->sample_format;
}

static int64_t wav_get_length(pcm_reader_t *reader)
{
    return ((wav_reader_t *)reader)->length;
}

static int64_t wav_get_position(pcm_reader_t *reader)
{
    return ((wav_reader_t *)reader)->position;
}

static void wav_teardown(pcm_reader_t **reader)
{
    free(*reader);
    *reader = 0;
}

static
int riff_read(wav_reader_t *reader, void *buffer, uint32_t size)
{
    int rc;
    uint32_t count = 0;

    if (reader->last_error)
        return -1;
    do {
        rc = reader->io.read(reader->io_cookie, buffer, size - count);
        if (rc > 0)
            count += rc;
        else if (rc < 0)
            reader->last_error = WAV_IO_ERROR;
    } while (rc > 0 && count < size);
    return count > 0 ? count : rc;
}

static
int riff_skip(wav_reader_t *reader, int64_t count)
{
    char buff[8192];
    int rc;

    if (reader->last_error)
        return -1;
    if (count == 0)
        return 0;
    if (reader->io.seek &&
        reader->io.seek(reader->io_cookie, count, SEEK_CUR) >= 0)
        return 0;

    do {
        if ((rc = riff_read(reader, buff, count > 8192 ? 8192 : count)) > 0)
            count -= rc;
    } while (rc > 0 && count > 0);

    if (count > 0)
        reader->last_error = WAV_IO_ERROR;
    return reader->last_error ? -1 : 0;
}

static
int riff_seek(wav_reader_t *reader, int64_t off, int whence)
{
    int rc;
    if (reader->last_error)
        return -1;
    if (!reader->io.seek)
        goto FAIL;
    if ((rc = reader->io.seek(reader->io_cookie, off, whence)) < 0)
        goto FAIL;
    return 0;
FAIL:
    reader->last_error = WAV_IO_ERROR;
    return -1;
}

static
int64_t riff_tell(wav_reader_t *reader)
{
    int64_t off;

    if (reader->last_error || !reader->io.tell)
        return -1;
    off = reader->io.tell(reader->io_cookie);
    if (off < 0) {
        reader->last_error = WAV_IO_ERROR;
        return -1;
    }
    return off;
}

static
int riff_read16(wav_reader_t *reader, uint16_t *value)
{
    TRY_IO(riff_read(reader, value, 2) != 2);
    *value = m4af_ltoh16(*value);
    return 0;
FAIL:
    return -1;
}

static
int riff_read32(wav_reader_t *reader, uint32_t *value)
{
    TRY_IO(riff_read(reader, value, 4) != 4);
    *value = m4af_ltoh32(*value);
    return 0;
FAIL:
    return -1;
}

static
int riff_read64(wav_reader_t *reader, uint64_t *value)
{
    TRY_IO(riff_read(reader, value, 8) != 8);
    *value = m4af_ltoh64(*value);
    return 0;
FAIL:
    return -1;
}

static
int riff_scan(wav_reader_t *reader, const char *fmt, ...)
{
    int c, count = 0;
    va_list ap;

    va_start(ap, fmt);
    while ((c = *fmt++)) {
        switch (c) {
        case 'S':
            TRY_IO(riff_read16(reader, va_arg(ap, uint16_t*)));
            ++count;
            break;
        case 'L':
            TRY_IO(riff_read32(reader, va_arg(ap, uint32_t*)));
            ++count;
            break;
        case 'Q':
            TRY_IO(riff_read64(reader, va_arg(ap, uint64_t*)));
            ++count;
            break;
        }
    }
FAIL:
    va_end(ap);
    return count;
}

static
uint32_t riff_next_chunk(wav_reader_t *reader, uint32_t *chunk_size)
{
    uint32_t fcc;
    if (riff_scan(reader, "LL", &fcc, chunk_size) == 2)
        return fcc;
    return 0;
}

static
int wav_read_frames(pcm_reader_t *preader, void *buffer, unsigned nframes)
{
    int rc;
    unsigned nbytes;
    wav_reader_t *reader = (wav_reader_t *)preader;

    if (!reader->ignore_length && nframes > reader->length - reader->position)
        nframes = reader->length - reader->position;
    nbytes = nframes * reader->sample_format.bytes_per_frame;
    if (nbytes) {
        if ((rc = riff_read(reader, buffer, nbytes)) < 0)
            return -1;
        nframes = rc / reader->sample_format.bytes_per_frame;
        reader->position += nframes;
    }
    return nframes;
}

static
int riff_ds64(wav_reader_t *reader, int64_t *length)
{
    uint32_t fcc, chunk_size, table_size;
    uint64_t riff_size, sample_count;

    fcc = riff_next_chunk(reader, &chunk_size);
    ASSERT_FORMAT(reader,
                  fcc == RIFF_FOURCC('d','s','6','4') && chunk_size >= 28);
    TRY_IO(riff_scan(reader, "QQQL",
                     &riff_size, length, &sample_count, &table_size) != 4);
    TRY_IO(riff_skip(reader, (chunk_size - 27) & ~1));
    reader->data_offset += (chunk_size + 9) & ~1;
FAIL:
    return -1;
}

static
int wav_fmt(wav_reader_t *reader, uint32_t size)
{
    uint16_t wFormatTag, nChannels, nBlockAlign, wBitsPerSample, cbSize;
    uint32_t nSamplesPerSec, nAvgBytesPerSec, dwChannelMask = 0;
    uint16_t wValidBitsPerSample;
    uint8_t guid[16];
    int is_float = 0;

    ASSERT_FORMAT(reader, size >= 16);
    TRY_IO(riff_scan(reader, "SSLLSS", &wFormatTag, &nChannels,
                     &nSamplesPerSec, &nAvgBytesPerSec, &nBlockAlign,
                     &wBitsPerSample) != 6);
    wValidBitsPerSample = wBitsPerSample;

    if (wFormatTag != 1 && wFormatTag != 3 && wFormatTag != 0xfffe) {
        reader->last_error = WAV_UNSUPPORTED_FORMAT;
        goto FAIL;
    }
    ASSERT_FORMAT(reader,
                  nChannels && nSamplesPerSec && nAvgBytesPerSec &&
                  nBlockAlign && wBitsPerSample && !(wBitsPerSample & 7) &&
                  nBlockAlign == nChannels * wBitsPerSample / 8);
    if (wFormatTag == 3)
        is_float = 1;

    if (wFormatTag != 0xfffe)
        TRY_IO(riff_skip(reader, (size - 15) & ~1));
    else {
        ASSERT_FORMAT(reader, size >= 40);
        TRY_IO(riff_scan(reader, "SSL",
                         &cbSize, &wValidBitsPerSample, &dwChannelMask) != 3);
        TRY_IO(riff_read(reader, guid, 16) != 16);

        if (memcmp(guid, WAV_GUID_FLOAT, 16) == 0)
            is_float = 1;
        else if (memcmp(guid, WAV_GUID_PCM, 16) != 0) {
            reader->last_error = WAV_UNSUPPORTED_FORMAT;
            goto FAIL;
        }
        ASSERT_FORMAT(reader,
                      wValidBitsPerSample &&
                      wValidBitsPerSample <= wBitsPerSample);
        TRY_IO(riff_skip(reader, (size - 39) & ~1));
    }
    reader->sample_format.sample_rate = nSamplesPerSec;
    reader->sample_format.bits_per_channel = wValidBitsPerSample;
    reader->sample_format.bytes_per_frame = nBlockAlign;
    reader->sample_format.channels_per_frame = nChannels;
    reader->sample_format.channel_mask = dwChannelMask;
    if (is_float)
        reader->sample_format.sample_type = PCM_TYPE_FLOAT;
    else if (wBitsPerSample == 8)
        reader->sample_format.sample_type = PCM_TYPE_UINT;
    else
        reader->sample_format.sample_type = PCM_TYPE_SINT;
    return 0;
FAIL:
    return -1;
}

static
int wav_parse(wav_reader_t *reader, int64_t *data_length)
{
    uint32_t container, fcc, chunk_size;

    *data_length = 0;
    container = riff_next_chunk(reader, &chunk_size);
    if (container != RIFF_FOURCC('R','I','F','F') &&
        container != RIFF_FOURCC('R','F','6','4'))
        goto FAIL;
    TRY_IO(riff_read32(reader, &fcc));
    if (fcc != RIFF_FOURCC('W','A','V','E'))
        goto FAIL;
    reader->data_offset = 12;

    if (container == RIFF_FOURCC('R','F','6','4'))
        riff_ds64(reader, data_length);
    while ((fcc = riff_next_chunk(reader, &chunk_size)) != 0) {
        if (fcc == RIFF_FOURCC('f','m','t',' ')) {
            if (wav_fmt(reader, chunk_size) < 0)
                goto FAIL;
        } else if (fcc == RIFF_FOURCC('d','a','t','a')) {
            if (container == RIFF_FOURCC('R','I','F','F'))
                *data_length = chunk_size;
            reader->data_offset += 8;
            break;
        } else {
            TRY_IO(riff_skip(reader, (chunk_size + 1) & ~1));
        }
        reader->data_offset += (chunk_size + 9) & ~1;
    }
    if (fcc == RIFF_FOURCC('d','a','t','a'))
        return 0;
FAIL:
    return -1;
}

static pcm_reader_vtbl_t wav_vtable = {
    wav_get_format,
    wav_get_length,
    wav_get_position,
    wav_read_frames,
    wav_teardown
};

pcm_reader_t *wav_open(wav_io_context_t *io_ctx, void *io_cookie,
                       int ignore_length)
{
    wav_reader_t *reader = 0;
    int64_t data_length;
    unsigned bpf;

    if ((reader = calloc(1, sizeof(wav_reader_t))) == 0)
        return 0;
    memcpy(&reader->io, io_ctx, sizeof(wav_io_context_t));
    reader->io_cookie = io_cookie;
    reader->ignore_length = ignore_length;
    if (wav_parse(reader, &data_length) < 0) {
        free(reader);
        return 0;
    }
    bpf = reader->sample_format.bytes_per_frame;
    if (ignore_length || !data_length || data_length % bpf)
        reader->length = INT64_MAX;
    else
        reader->length = data_length / bpf;

    if (reader->length == INT64_MAX && reader->io.seek && reader->io.tell) {
        if (reader->io.seek(reader->io_cookie, 0, SEEK_END) >= 0) {
            int64_t size = reader->io.tell(reader->io_cookie);
            if (size > 0)
                reader->length = (size - reader->data_offset) / bpf;
            reader->io.seek(reader->io_cookie, reader->data_offset, SEEK_SET);
        }
    }
    reader->vtbl = &wav_vtable;
    return (pcm_reader_t *)reader;
}

pcm_reader_t *raw_open(wav_io_context_t *io_ctx, void *io_cookie,
                       const pcm_sample_description_t *desc)
{
    wav_reader_t *reader = 0;

    if ((reader = calloc(1, sizeof(wav_reader_t))) == 0)
        return 0;
    memcpy(&reader->io, io_ctx, sizeof(wav_io_context_t));
    memcpy(&reader->sample_format, desc, sizeof(pcm_sample_description_t));
    reader->io_cookie = io_cookie;
    if (io_ctx->seek && io_ctx->tell) {
        if (reader->io.seek(reader->io_cookie, 0, SEEK_END) >= 0) {
            int64_t size = reader->io.tell(reader->io_cookie);
            if (size > 0)
                reader->length = size / desc->bytes_per_frame;
            reader->io.seek(reader->io_cookie, reader->data_offset, SEEK_SET);
        }
    } else
        reader->length = INT64_MAX;
    reader->vtbl = &wav_vtable;
    return (pcm_reader_t *)reader;
}


