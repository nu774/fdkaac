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
#include "m4af_endian.h"
#include "catypes.h"

int pcm_read_frames(pcm_reader_t *r, void *data, unsigned nframes)
{
    int n;
    unsigned count = 0;
    uint8_t *bp = data;
    unsigned bpf = pcm_get_format(r)->bytes_per_frame;

    do {
        n = r->vtbl->read_frames(r, bp, nframes - count);
        if (n > 0) {
            count += n;
            bp += n * bpf;
        }
    } while (n > 0 && count < nframes);
    return count;
}

int pcm_read(pcm_io_context_t *io, void *buffer, uint32_t size)
{
    int rc;
    uint32_t count = 0;
    uint8_t *bp = buffer;

    do {
        rc = io->vtbl->read(io->cookie, bp, size - count);
        if (rc > 0) {
            count += rc;
            bp += rc;
        }
    } while (rc > 0 && count < size);
    return count > 0 ? count : rc;
}

int pcm_skip(pcm_io_context_t *io, int64_t count)
{
    char buff[8192];
    int rc;
    pcm_io_vtbl_t *vp = io->vtbl;

    if (count == 0 || pcm_seek(io, count, SEEK_CUR) >= 0)
        return 0;
    do {
        if ((rc = vp->read(io->cookie, buff, count > 8192 ? 8192 : count)) > 0)
            count -= rc;
    } while (rc > 0 && count > 0);

    return count == 0 ? 0 : -1;
}

int pcm_read16le(pcm_io_context_t *io, uint16_t *value)
{
    if (pcm_read(io, value, 2) == 2) {
        *value = m4af_ltoh16(*value);
        return 0;
    }
    return -1;
}

int pcm_read16be(pcm_io_context_t *io, uint16_t *value)
{
    if (pcm_read(io, value, 2) == 2) {
        *value = m4af_btoh16(*value);
        return 0;
    }
    return -1;
}

int pcm_read32le(pcm_io_context_t *io, uint32_t *value)
{
    if (pcm_read(io, value, 4) == 4) {
        *value = m4af_ltoh32(*value);
        return 0;
    }
    return -1;
}

int pcm_read32be(pcm_io_context_t *io, uint32_t *value)
{
    if (pcm_read(io, value, 4) == 4) {
        *value = m4af_btoh32(*value);
        return 0;
    }
    return -1;
}

int pcm_read64le(pcm_io_context_t *io, uint64_t *value)
{
    if (pcm_read(io, value, 8) == 8) {
        *value = m4af_ltoh64(*value);
        return 0;
    }
    return -1;
}

int pcm_read64be(pcm_io_context_t *io, uint64_t *value)
{
    if (pcm_read(io, value, 8) == 8) {
        *value = m4af_btoh64(*value);
        return 0;
    }
    return -1;
}

int pcm_scanl(pcm_io_context_t *io, const char *fmt, ...)
{
    int c, count = 0;
    va_list ap;

    va_start(ap, fmt);
    while ((c = *fmt++)) {
        switch (c) {
        case 'S':
            TRY_IO(pcm_read16le(io, va_arg(ap, uint16_t*)));
            ++count;
            break;
        case 'L':
            TRY_IO(pcm_read32le(io, va_arg(ap, uint32_t*)));
            ++count;
            break;
        case 'Q':
            TRY_IO(pcm_read64le(io, va_arg(ap, uint64_t*)));
            ++count;
            break;
        }
    }
FAIL:
    va_end(ap);
    return count;
}

int pcm_scanb(pcm_io_context_t *io, const char *fmt, ...)
{
    int c, count = 0;
    va_list ap;

    va_start(ap, fmt);
    while ((c = *fmt++)) {
        switch (c) {
        case 'S':
            TRY_IO(pcm_read16be(io, va_arg(ap, uint16_t*)));
            ++count;
            break;
        case 'L':
            TRY_IO(pcm_read32be(io, va_arg(ap, uint32_t*)));
            ++count;
            break;
        case 'Q':
            TRY_IO(pcm_read64be(io, va_arg(ap, uint64_t*)));
            ++count;
            break;
        }
    }
FAIL:
    va_end(ap);
    return count;
}

static
int channel_compare(const void *a, const void *b)
{
    return (*(const uint8_t **)a)[0] - (*(const uint8_t **)b)[0];
}

void apple_translate_channel_labels(uint8_t *channels, unsigned n)
{
    unsigned i;
    char *has_side = strpbrk((char*)channels, "\x0A\x0B");

    for (i = 0; i < n; ++i) {
        switch (channels[i]) {
        case kAudioChannelLabel_LeftSurround:
        case kAudioChannelLabel_RightSurround:
            if (!has_side) channels[i] += 5; // map to SL/SR
            break;
        case kAudioChannelLabel_RearSurroundLeft:
        case kAudioChannelLabel_RearSurroundRight:
            if (!has_side) channels[i] -= 28; // map to BL/BR
            break;
        case kAudioChannelLabel_Mono:
            channels[i] = kAudioChannelLabel_Center;
            break;
        }
    }
}

int apple_chan_chunk(pcm_io_context_t *io, uint32_t chunk_size,
                     pcm_sample_description_t *fmt, uint8_t *mapping)
{
    /*
     * Although FDK encoder supports upto 5.1ch, we handle upto
     * 8 channels here.
     */
    uint32_t i, mChannelLayoutTag, mChannelBitmap, mNumberChannelDescriptions;
    uint32_t mask = 0;
    const uint32_t nchannels = fmt->channels_per_frame;
    uint8_t channels[9] = { 0 };
    uint8_t *index[8] = { 0 };
    const char *layout = 0;

    ENSURE(chunk_size >= 12);
    TRY_IO(pcm_scanb(io, "LLL", &mChannelLayoutTag, &mChannelBitmap,
                     &mNumberChannelDescriptions) != 3);

    switch (mChannelLayoutTag) {
    case kAudioChannelLayoutTag_UseChannelBitmap:
        ENSURE(bitcount(mChannelBitmap) == nchannels);
        TRY_IO(pcm_skip(io, chunk_size - 12));
        fmt->channel_mask = mChannelBitmap;
        for (i = 0; i < nchannels; ++i)
            mapping[i] = i;
        return 0;
    case kAudioChannelLayoutTag_UseChannelDescriptions:
        ENSURE(mNumberChannelDescriptions == nchannels);
        ENSURE(chunk_size >= 12 + nchannels * 20);
        for (i = 0; i < mNumberChannelDescriptions; ++i) {
            uint32_t mChannelLabel;
            TRY_IO(pcm_read32be(io, &mChannelLabel));
            ENSURE(mChannelLabel && mChannelLabel <= 0xff);
            channels[i] = mChannelLabel;
            TRY_IO(pcm_skip(io, 16));
        }
        TRY_IO(pcm_skip(io, chunk_size - 12 - nchannels * 20));
        apple_translate_channel_labels(channels, nchannels);
        for (i = 0; i < nchannels; ++i)
            if (channels[i] > kAudioChannelLabel_TopBackLeft)
                goto FAIL;
        break;
    default:
        ENSURE((mChannelLayoutTag & 0xffff) == nchannels);
        TRY_IO(pcm_skip(io, chunk_size - 12));

        switch (mChannelLayoutTag) {
        /* 1ch */
        case kAudioChannelLayoutTag_Mono:
            layout = "\x03"; break;
        /* 1.1ch */
        case kAudioChannelLayoutTag_AC3_1_0_1:
            layout = "\x03\x04"; break;
        /* 2ch */
        case kAudioChannelLayoutTag_Stereo:
        case kAudioChannelLayoutTag_MatrixStereo:
        case kAudioChannelLayoutTag_Binaural:
            layout = "\x01\x02"; break;
        /* 2.1ch */
        case kAudioChannelLayoutTag_DVD_4:
            layout = "\x01\x02\x04"; break;
        /* 3ch */
        case kAudioChannelLayoutTag_MPEG_3_0_A:
            layout = "\x01\x02\x03"; break;
        case kAudioChannelLayoutTag_AC3_3_0:
            layout = "\x01\x03\x02"; break;
        case kAudioChannelLayoutTag_MPEG_3_0_B:
            layout = "\x03\x01\x02"; break;
        case kAudioChannelLayoutTag_ITU_2_1:
            layout = "\x01\x02\x09"; break;
        /* 3.1ch */
        case kAudioChannelLayoutTag_DVD_10:
            layout = "\x01\x02\x03\x04"; break;
        case kAudioChannelLayoutTag_AC3_3_0_1:
            layout = "\x01\x03\x02\x04"; break;
        case kAudioChannelLayoutTag_DVD_5:
            layout = "\x01\x02\x04\x09"; break;
        case kAudioChannelLayoutTag_AC3_2_1_1:
            layout = "\x01\x02\x09\x04"; break;
        /* 4ch */
        case kAudioChannelLayoutTag_Quadraphonic:
        case kAudioChannelLayoutTag_ITU_2_2:
            layout = "\x01\x02\x0A\x0B"; break;
        case kAudioChannelLayoutTag_MPEG_4_0_A:
            layout = "\x01\x02\x03\x09"; break;
        case kAudioChannelLayoutTag_MPEG_4_0_B:
            layout = "\x03\x01\x02\x09"; break;
        case kAudioChannelLayoutTag_AC3_3_1:
            layout = "\x01\x03\x02\x09"; break;
        /* 4.1ch */
        case kAudioChannelLayoutTag_DVD_6:
            layout = "\x01\x02\x04\x0A\x0B"; break;
        case kAudioChannelLayoutTag_DVD_18:
            layout = "\x01\x02\x0A\x0B\x04"; break;
        case kAudioChannelLayoutTag_DVD_11:
            layout = "\x01\x02\x03\x04\x09"; break;
        case kAudioChannelLayoutTag_AC3_3_1_1:
            layout = "\x01\x03\x02\x09\x04"; break;
        /* 5ch */
        case kAudioChannelLayoutTag_MPEG_5_0_A:
            layout = "\x01\x02\x03\x0A\x0B"; break;
        case kAudioChannelLayoutTag_Pentagonal:
        case kAudioChannelLayoutTag_MPEG_5_0_B:
            layout = "\x01\x02\x0A\x0B\x03"; break;
        case kAudioChannelLayoutTag_MPEG_5_0_C:
            layout = "\x01\x03\x02\x0A\x0B"; break;
        case kAudioChannelLayoutTag_MPEG_5_0_D:
            layout = "\x03\x01\x02\x0A\x0B"; break;
        /* 5.1ch */
        case kAudioChannelLayoutTag_MPEG_5_1_A:
            layout = "\x01\x02\x03\x04\x0A\x0B"; break;
        case kAudioChannelLayoutTag_MPEG_5_1_B:
            layout = "\x01\x02\x0A\x0B\x03\x04"; break;
        case kAudioChannelLayoutTag_MPEG_5_1_C:
            layout = "\x01\x03\x02\x0A\x0B\x04"; break;
        case kAudioChannelLayoutTag_MPEG_5_1_D:
            layout = "\x03\x01\x02\x0A\x0B\x04"; break;
        /* 6ch */
        case kAudioChannelLayoutTag_Hexagonal:
        case kAudioChannelLayoutTag_AudioUnit_6_0:
            layout = "\x01\x02\x0A\x0B\x03\x09"; break;
        case kAudioChannelLayoutTag_AAC_6_0:
            layout = "\x03\x01\x02\x0A\x0B\x09"; break;
        /* 6.1ch */
        case kAudioChannelLayoutTag_MPEG_6_1_A:
            layout = "\x01\x02\x03\x04\x0A\x0B\x09"; break;
        case kAudioChannelLayoutTag_AAC_6_1:
            layout = "\x03\x01\x02\x0A\x0B\x09\x04"; break;
        /* 7ch */
        case kAudioChannelLayoutTag_AudioUnit_7_0:
            layout = "\x01\x02\x0A\x0B\x03\x05\x06"; break;
        case kAudioChannelLayoutTag_AudioUnit_7_0_Front:
            layout = "\x01\x02\x0A\x0B\x03\x07\x08"; break;
        case kAudioChannelLayoutTag_AAC_7_0:
            layout = "\x03\x01\x02\x0A\x0B\x05\x06"; break;
        /* 7.1ch */
        case kAudioChannelLayoutTag_MPEG_7_1_A:
            layout = "\x01\x02\x03\x04\x0A\x0B\x07\x08"; break;
        case kAudioChannelLayoutTag_MPEG_7_1_B:
            layout = "\x03\x07\x08\x01\x02\x05\x06\x04"; break;
        case kAudioChannelLayoutTag_MPEG_7_1_C:
            layout = "\x01\x02\x03\x04\x0A\x0B\x05\x06"; break;
        case kAudioChannelLayoutTag_Emagic_Default_7_1:
            layout = "\x01\x02\x0A\x0B\x03\x04\x07\x08"; break;
        /* 8ch */
        case kAudioChannelLayoutTag_Octagonal:
            layout = "\x01\x02\x05\x06\x03\x09\x0A\x0B"; break;
        case kAudioChannelLayoutTag_AAC_Octagonal:
            layout = "\x03\x01\x02\x0A\x0B\x05\x06\x09"; break;
        default:
            goto FAIL;
        }
        strcpy((char*)channels, layout);
    }

    for (i = 0; i < nchannels; ++i)
        mask |= 1 << (channels[i] - 1);
    fmt->channel_mask = mask;
    ENSURE(bitcount(mask) == nchannels);

    for (i = 0; i < nchannels; ++i) 
        index[i] = channels + i;
    qsort(index, nchannels, sizeof(char*), channel_compare);
    for (i = 0; i < nchannels; ++i) 
        mapping[i] = index[i] - channels;

    return 0;
FAIL:
    return -1;
}
