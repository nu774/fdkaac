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
#include "aacenc.h"

int aacenc_is_sbr_active(const aacenc_param_t *params)
{
    switch (params->profile) {
    case AOT_SBR: case AOT_PS: case AOT_MP2_SBR: case AOT_MP2_PS:
    case AOT_DABPLUS_SBR: case AOT_DABPLUS_PS:
    case AOT_DRM_SBR: case AOT_DRM_MPEG_PS:
        return 1;
    }
    if (params->profile == AOT_ER_AAC_ELD && params->lowdelay_sbr)
        return 1;
    return 0;
}

static
int aacenc_channel_mode(const pcm_sample_description_t *format)
{
    uint32_t chanmask = format->channel_mask;

    if (format->channels_per_frame > 6)
        return 0;
    if (!chanmask) {
        static uint32_t defaults[] = { 0x4, 0x3, 0x7, 0, 0x37, 0x3f };
        chanmask = defaults[format->channels_per_frame - 1];
    }
    switch (chanmask) {
    case 0x3:   return MODE_2;
    case 0x4:   return MODE_1;
    case 0x7:   return MODE_1_2;
    case 0x37:  return MODE_1_2_2;
    case 0x3f:  return MODE_1_2_2_1;
    case 0x107: return MODE_1_2_1;
    case 0x607: return MODE_1_2_2;
    case 0x60f: return MODE_1_2_2_1;
    }
    return 0;
}

int aacenc_init(HANDLE_AACENCODER *encoder, const aacenc_param_t *params,
                const pcm_sample_description_t *format,
                AACENC_InfoStruct *info)
{
    int channel_mode;
    int aot;

    *encoder = 0;
    if ((channel_mode = aacenc_channel_mode(format)) == 0) {
        fprintf(stderr, "ERROR: unsupported channel layout\n");
        goto FAIL;
    }
    if (aacEncOpen(encoder, 0, 0) != AACENC_OK) {
        fprintf(stderr, "ERROR: aacEncOpen() failed\n");
        goto FAIL;
    }
    aot = (params->profile ? params->profile : AOT_AAC_LC);
    if (aacEncoder_SetParam(*encoder, AACENC_AOT, aot) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported profile\n");
        goto FAIL;
    }
    if (params->bitrate_mode == 0)
        aacEncoder_SetParam(*encoder, AACENC_BITRATE, params->bitrate);
    else if (aacEncoder_SetParam(*encoder, AACENC_BITRATEMODE,
                                 params->bitrate_mode) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported bitrate mode\n");
        goto FAIL;
    }
    if (aacEncoder_SetParam(*encoder, AACENC_SAMPLERATE,
                            format->sample_rate) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported sample rate\n");
        goto FAIL;
    }
    aacEncoder_SetParam(*encoder, AACENC_CHANNELMODE, channel_mode);
    aacEncoder_SetParam(*encoder, AACENC_BANDWIDTH, params->bandwidth);
    aacEncoder_SetParam(*encoder, AACENC_CHANNELORDER, 1);
    aacEncoder_SetParam(*encoder, AACENC_AFTERBURNER, !!params->afterburner);

    if (aot == AOT_ER_AAC_ELD && params->lowdelay_sbr)
        aacEncoder_SetParam(*encoder, AACENC_SBR_MODE, 1);

    if (aacEncoder_SetParam(*encoder, AACENC_TRANSMUX,
                            params->transport_format) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported transport format\n");
        goto FAIL;
    }
    if (aacEncoder_SetParam(*encoder, AACENC_SIGNALING_MODE,
                            params->sbr_signaling) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported transport format\n");
        goto FAIL;
    }
    if (params->adts_crc_check)
        aacEncoder_SetParam(*encoder, AACENC_PROTECTION, 1);
    if (params->header_period)
        aacEncoder_SetParam(*encoder, AACENC_HEADER_PERIOD,
                            params->header_period);

    if (aacEncEncode(*encoder, 0, 0, 0, 0) != AACENC_OK) {
        fprintf(stderr, "ERROR: encoder initialization failed\n");
        goto FAIL;
    }
    if (aacEncInfo(*encoder, info) != AACENC_OK) {
        fprintf(stderr, "ERROR: cannot retrieve encoder info\n");
        goto FAIL;
    }
    return 0;
FAIL:
    if (encoder)
        aacEncClose(encoder);
    return -1;
}

int aac_encode_frame(HANDLE_AACENCODER encoder,
                     const pcm_sample_description_t *format,
                     const int16_t *input, unsigned iframes,
                     uint8_t **output, uint32_t *olen, uint32_t *osize)
{
    uint32_t ilen = iframes * format->channels_per_frame;
    AACENC_BufDesc ibdesc = { 0 }, obdesc = { 0 };
    AACENC_InArgs iargs = { 0 };
    AACENC_OutArgs oargs = { 0 };
    void *ibufs[] = { (void*)input };
    void *obufs[1];
    INT ibuf_ids[] = { IN_AUDIO_DATA };
    INT obuf_ids[] = { OUT_BITSTREAM_DATA };
    INT ibuf_sizes[] = { ilen * sizeof(int16_t) };
    INT obuf_sizes[1];
    INT ibuf_el_sizes[] = { sizeof(int16_t) };
    INT obuf_el_sizes[] = { 1 };
    AACENC_ERROR err;
    unsigned channel_mode, obytes;

    channel_mode = aacEncoder_GetParam(encoder, AACENC_CHANNELMODE);
    obytes = 6144 / 8 * channel_mode;
    if (!*output || *osize < obytes) {
        *osize = obytes;
        *output = realloc(*output, obytes);
    }
    obufs[0] = *output;
    obuf_sizes[0] = obytes;

    iargs.numInSamples = ilen ? ilen : -1; /* -1 for signaling EOF */
    ibdesc.numBufs = 1;
    ibdesc.bufs = ibufs;
    ibdesc.bufferIdentifiers = ibuf_ids;
    ibdesc.bufSizes = ibuf_sizes;
    ibdesc.bufElSizes = ibuf_el_sizes;
    obdesc.numBufs = 1;
    obdesc.bufs = obufs;
    obdesc.bufferIdentifiers = obuf_ids;
    obdesc.bufSizes = obuf_sizes;
    obdesc.bufElSizes = obuf_el_sizes;

    err = aacEncEncode(encoder, &ibdesc, &obdesc, &iargs, &oargs);
    if (err != AACENC_ENCODE_EOF && err != AACENC_OK) {
        fprintf(stderr, "ERROR: aacEncEncode() failed\n");
        return -1;
    }
    *olen = oargs.numOutBytes;
    return oargs.numInSamples;
}
