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
#include "pcm_reader.h"

#define RIFF_FOURCC(a,b,c,d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))

typedef struct wav_reader_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_sample_description_t sample_format;
    int64_t length;
    int64_t position;
    int32_t data_offset;
    int ignore_length;
    pcm_io_context_t io;
} wav_reader_t;

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
uint32_t riff_next_chunk(wav_reader_t *reader, uint32_t *chunk_size)
{
    uint32_t fcc;
    return (pcm_scanl(&reader->io, "LL", &fcc, chunk_size) == 2) ? fcc : 0;
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
        if ((rc = pcm_read(&reader->io, buffer, nbytes)) < 0)
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
    ENSURE(fcc == RIFF_FOURCC('d','s','6','4') && chunk_size >= 28);
    TRY_IO(pcm_scanl(&reader->io, "QQQL",
                     &riff_size, length, &sample_count, &table_size) != 4);
    TRY_IO(pcm_skip(&reader->io, (chunk_size - 27) & ~1));
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

    ENSURE(size >= 16);
    TRY_IO(pcm_scanl(&reader->io, "SSLLSS", &wFormatTag, &nChannels,
                     &nSamplesPerSec, &nAvgBytesPerSec, &nBlockAlign,
                     &wBitsPerSample) != 6);
    wValidBitsPerSample = wBitsPerSample;

    ENSURE(wFormatTag == 1 || wFormatTag == 3 || wFormatTag == 0xfffe);
    ENSURE(nChannels && nSamplesPerSec && nAvgBytesPerSec &&
           nBlockAlign && wBitsPerSample && !(wBitsPerSample & 7) &&
           nBlockAlign == nChannels * wBitsPerSample / 8);

    if (wFormatTag == 3)
        is_float = 1;

    if (wFormatTag != 0xfffe)
        TRY_IO(pcm_skip(&reader->io, (size - 15) & ~1));
    else {
        ENSURE(size >= 40);
        TRY_IO(pcm_scanl(&reader->io, "SSL",
                         &cbSize, &wValidBitsPerSample, &dwChannelMask) != 3);
        TRY_IO(pcm_read(&reader->io, guid, 16) != 16);

        if (memcmp(guid, WAV_GUID_FLOAT, 16) == 0)
            is_float = 1;
        else if (memcmp(guid, WAV_GUID_PCM, 16) != 0)
            goto FAIL;
        ENSURE(wValidBitsPerSample && wValidBitsPerSample <= wBitsPerSample);
        TRY_IO(pcm_skip(&reader->io, (size - 39) & ~1));
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
    ENSURE(container == RIFF_FOURCC('R','I','F','F') ||
           container == RIFF_FOURCC('R','F','6','4'));
    TRY_IO(pcm_read32le(&reader->io, &fcc));
    ENSURE(fcc == RIFF_FOURCC('W','A','V','E'));

    if (container == RIFF_FOURCC('R','F','6','4'))
        riff_ds64(reader, data_length);
    while ((fcc = riff_next_chunk(reader, &chunk_size)) != 0) {
        if (fcc == RIFF_FOURCC('f','m','t',' ')) {
            if (wav_fmt(reader, chunk_size) < 0)
                goto FAIL;
        } else if (fcc == RIFF_FOURCC('d','a','t','a')) {
            if (container == RIFF_FOURCC('R','I','F','F'))
                *data_length = chunk_size;
            reader->data_offset = pcm_tell(&reader->io);
            break;
        } else {
            TRY_IO(pcm_skip(&reader->io, (chunk_size + 1) & ~1));
        }
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

pcm_reader_t *wav_open(pcm_io_context_t *io, int ignore_length)
{
    wav_reader_t *reader = 0;
    int64_t data_length;
    unsigned bpf;

    if ((reader = calloc(1, sizeof(wav_reader_t))) == 0)
        return 0;
    memcpy(&reader->io, io, sizeof(pcm_io_context_t));
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

    if (reader->length == INT64_MAX) {
        if (pcm_seek(&reader->io, 0, SEEK_END) >= 0) {
            int64_t size = pcm_tell(&reader->io);
            if (size > 0)
                reader->length = (size - reader->data_offset) / bpf;
            pcm_seek(&reader->io, reader->data_offset, SEEK_SET);
        }
    }
    reader->vtbl = &wav_vtable;
    return (pcm_reader_t *)reader;
}

pcm_reader_t *raw_open(pcm_io_context_t *io,
                       const pcm_sample_description_t *desc)
{
    wav_reader_t *reader = 0;

    if ((reader = calloc(1, sizeof(wav_reader_t))) == 0)
        return 0;
    memcpy(&reader->io, io, sizeof(pcm_io_context_t));
    memcpy(&reader->sample_format, desc, sizeof(pcm_sample_description_t));
    if (pcm_seek(&reader->io, 0, SEEK_END) >= 0) {
        int64_t size = pcm_tell(&reader->io);
        if (size > 0)
            reader->length = size / reader->sample_format.bytes_per_frame;
        pcm_seek(&reader->io, 0, SEEK_SET);
    } else
        reader->length = INT64_MAX;
    reader->vtbl = &wav_vtable;
    return (pcm_reader_t *)reader;
}
