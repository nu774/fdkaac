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
#include "m4af.h"

typedef struct caf_reader_t {
    pcm_reader_vtbl_t *vtbl;
    pcm_sample_description_t sample_format;
    int64_t length;
    int64_t position;
    int64_t data_offset;
    pcm_io_context_t io;
    aacenc_tag_callback_t tag_callback;
    void *tag_ctx;
    uint8_t chanmap[8];
} caf_reader_t;

static const pcm_sample_description_t *caf_get_format(pcm_reader_t *reader)
{
    return &((caf_reader_t *)reader)->sample_format;
}

static int64_t caf_get_length(pcm_reader_t *reader)
{
    return ((caf_reader_t *)reader)->length;
}

static int64_t caf_get_position(pcm_reader_t *reader)
{
    return ((caf_reader_t *)reader)->position;
}

static void caf_teardown(pcm_reader_t **reader)
{
    free(*reader);
    *reader = 0;
}

static
uint32_t caf_next_chunk(caf_reader_t *reader, int64_t *chunk_size)
{
    uint32_t fcc;
    if (pcm_scanb(&reader->io, "LQ", &fcc, chunk_size) == 2)
        return fcc;
    return 0;
}

static
int caf_desc(caf_reader_t *reader, int64_t chunk_size)
{
    double mSampleRate;
    uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket,
             mChannelsPerFrame, mBitsPerChannel;
    pcm_sample_description_t *desc = &reader->sample_format;

    ENSURE(chunk_size >= 32);
    TRY_IO(pcm_scanb(&reader->io, "QLLLLLL", &mSampleRate, &mFormatID,
                     &mFormatFlags, &mBytesPerPacket, &mFramesPerPacket,
                     &mChannelsPerFrame, &mBitsPerChannel) != 7);

    ENSURE(mFormatID == M4AF_FOURCC('l','p','c','m'));
    ENSURE(mSampleRate && mBytesPerPacket &&
           mChannelsPerFrame >= 1 && mChannelsPerFrame <= 8 &&
           mBitsPerChannel && mFramesPerPacket == 1 &&
           mBytesPerPacket % mChannelsPerFrame == 0 &&
           mBytesPerPacket >= mChannelsPerFrame * ((mBitsPerChannel + 7) / 8));

    desc->sample_rate        = mSampleRate;
    desc->bits_per_channel   = mBitsPerChannel;
    desc->bytes_per_frame    = mBytesPerPacket;
    desc->channels_per_frame = mChannelsPerFrame;

    switch (mFormatFlags) {
    case 0: desc->sample_type = PCM_TYPE_SINT_BE;  break;
    case 1: desc->sample_type = PCM_TYPE_FLOAT_BE; break;
    case 2: desc->sample_type = PCM_TYPE_SINT;     break;
    case 3: desc->sample_type = PCM_TYPE_FLOAT;    break;
    default: goto FAIL;
    }

    TRY_IO(pcm_skip(&reader->io, chunk_size - 32));
    return 0;
FAIL:
    return -1;
}

static
int caf_info(caf_reader_t *reader, int64_t chunk_size)
{
    char *buf, *key, *val, *end;
    size_t len;

    if (chunk_size < 4 || (buf = malloc(chunk_size)) == 0)
        return -1;
    pcm_read(&reader->io, buf, chunk_size);
    key = buf + 4;
    end = buf + chunk_size;
    do {
        if ((val = key + strlen(key) + 1) < end) {
            len = strlen(val);
            if (reader->tag_callback)
                reader->tag_callback(reader->tag_ctx, key, val, len);
            key = val + len + 1;
        }
    } while (key < end && val < end);

    if (reader->tag_callback)
        reader->tag_callback(reader->tag_ctx, 0, 0, 0);
    free(buf);
    return 0;
}

static
int caf_read_frames(pcm_reader_t *preader, void *buffer, unsigned nframes)
{
    int rc;
    unsigned i, j, nbytes;
    caf_reader_t *reader = (caf_reader_t *)preader;
    unsigned bpf = reader->sample_format.bytes_per_frame;
    unsigned nchannels = reader->sample_format.channels_per_frame;
    unsigned bpc = bpf / nchannels;
    uint8_t tmp[64]; /* enough room for maximum bpf: 8ch float64 */
    uint8_t *bp;
    uint8_t *chanmap = reader->chanmap;

    if (nframes > reader->length - reader->position)
        nframes = reader->length - reader->position;
    nbytes = nframes * bpf;
    if (nbytes) {
        if ((rc = pcm_read(&reader->io, buffer, nbytes)) < 0)
            return -1;
        nframes = rc / bpf;
        for (bp = buffer, i = 0; i < nframes; ++i, bp += bpf) {
            memcpy(tmp, bp, bpf);
            for (j = 0; j < nchannels; ++j)
                memcpy(bp + bpc * j, tmp + bpc * chanmap[j], bpc);
        }
        reader->position += nframes;
    }
    if (nframes == 0) {
        /* fetch info after data chunk */
        uint32_t fcc;
        int64_t chunk_size;
        while ((fcc = caf_next_chunk(reader, &chunk_size)) != 0) {
            if (fcc == M4AF_FOURCC('i','n','f','o'))
                TRY_IO(caf_info(reader, chunk_size));
            else
                TRY_IO(pcm_skip(&reader->io, chunk_size));
        }
    }
    return nframes;
FAIL:
    return 0;
}

static
int caf_parse(caf_reader_t *reader, int64_t *data_length)
{
    uint32_t fcc;
    int64_t chunk_size;

    *data_length = 0;

    /* CAFFileHeader */
    TRY_IO(pcm_read32be(&reader->io, &fcc));
    ENSURE(fcc == M4AF_FOURCC('c','a','f','f'));
    TRY_IO(pcm_skip(&reader->io, 4)); /* mFileVersion, mFileFlags */

    while ((fcc = caf_next_chunk(reader, &chunk_size)) != 0) {
        if (fcc == M4AF_FOURCC('d','e','s','c'))
            TRY_IO(caf_desc(reader, chunk_size));
        else if (fcc == M4AF_FOURCC('i','n','f','o'))
            TRY_IO(caf_info(reader, chunk_size));
        else if (fcc == M4AF_FOURCC('c','h','a','n')) {
            ENSURE(reader->sample_format.channels_per_frame);
            if (apple_chan_chunk(&reader->io, chunk_size,
                                 &reader->sample_format, reader->chanmap) < 0)
                goto FAIL;
        } else if (fcc == M4AF_FOURCC('d','a','t','a')) {
            TRY_IO(pcm_skip(&reader->io, 4)); /* mEditCount */
            *data_length = (chunk_size == ~0ULL) ? chunk_size : chunk_size - 4;
            reader->data_offset = pcm_tell(&reader->io);
            break;
        } else
            TRY_IO(pcm_skip(&reader->io, chunk_size));
    }
    ENSURE(reader->sample_format.channels_per_frame);
    ENSURE(fcc == M4AF_FOURCC('d','a','t','a'));
    return 0;
FAIL:
    return -1;
}

static pcm_reader_vtbl_t caf_vtable = {
    caf_get_format,
    caf_get_length,
    caf_get_position,
    caf_read_frames,
    caf_teardown
};

pcm_reader_t *caf_open(pcm_io_context_t *io,
                       aacenc_tag_callback_t tag_callback, void *tag_ctx)
{
    caf_reader_t *reader = 0;
    int64_t data_length;
    unsigned bpf;

    if ((reader = calloc(1, sizeof(caf_reader_t))) == 0)
        return 0;
    memcpy(&reader->io, io, sizeof(pcm_io_context_t));
    reader->tag_callback = tag_callback;
    reader->tag_ctx = tag_ctx;
    memcpy(reader->chanmap, "\000\001\002\003\004\005\006\007", 8);

    if (caf_parse(reader, &data_length) < 0) {
        free(reader);
        return 0;
    }
    bpf = reader->sample_format.bytes_per_frame;

    /* CAF uses -1 to indicate "unknown size" */
    if (data_length < 0 || data_length % bpf)
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
    reader->vtbl = &caf_vtable;
    return (pcm_reader_t *)reader;
}
