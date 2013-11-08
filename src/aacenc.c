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
#include "aacenc.h"

int aacenc_is_sbr_ratio_available()
{
#if AACENCODER_LIB_VL0 < 3 || (AACENCODER_LIB_VL0==3 && AACENCODER_LIB_VL1<4)
    return 0;
#else
    LIB_INFO lib_info;
    aacenc_get_lib_info(&lib_info);
    return lib_info.version > 0x03040000;
#endif
}

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

int aacenc_is_dual_rate_sbr(const aacenc_param_t *params)
{
    if (params->profile == AOT_PS || params->profile == AOT_MP2_PS)
        return 1;
    else if (params->profile == AOT_SBR || params->profile == AOT_MP2_SBR)
        return params->sbr_ratio == 0 || params->sbr_ratio == 2;
    else if (params->profile == AOT_ER_AAC_ELD && params->lowdelay_sbr)
        return params->sbr_ratio == 2;
    return 0;
}

void aacenc_get_lib_info(LIB_INFO *info)
{
    LIB_INFO *lib_info = 0;
    lib_info = calloc(FDK_MODULE_LAST, sizeof(LIB_INFO));
    if (aacEncGetLibInfo(lib_info) == AACENC_OK) {
        int i;
        for (i = 0; i < FDK_MODULE_LAST; ++i) {
            if (lib_info[i].module_id == FDK_AACENC) {
                memcpy(info, &lib_info[i], sizeof(LIB_INFO));
                break;
            }
        }
    }
    free(lib_info);
}

static
int aacenc_channel_mode(const pcm_sample_description_t *format)
{
    uint32_t chanmask = format->channel_mask;

    if (format->channels_per_frame > 8)
        return 0;
    if (!chanmask) {
        static uint32_t defaults[] = { 0x4, 0x3, 0x7, 0, 0x37, 0x3f, 0, 0x63f };
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
#if AACENCODER_LIB_VL0 > 3 || (AACENCODER_LIB_VL0==3 && AACENCODER_LIB_VL1>=4)
    case 0xff:  return MODE_1_2_2_2_1;
    case 0x63f: return MODE_7_1_REAR_SURROUND;
#endif
    }
    return 0;
}

int aacenc_init(HANDLE_AACENCODER *encoder, const aacenc_param_t *params,
                const pcm_sample_description_t *format,
                AACENC_InfoStruct *info)
{
    int channel_mode;
    int aot;
    LIB_INFO lib_info;

    *encoder = 0;
    aacenc_get_lib_info(&lib_info);

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
    if (aacEncoder_SetParam(*encoder, AACENC_CHANNELMODE,
                            channel_mode) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported channel mode\n");
        goto FAIL;
    }
    aacEncoder_SetParam(*encoder, AACENC_BANDWIDTH, params->bandwidth);
    aacEncoder_SetParam(*encoder, AACENC_CHANNELORDER, 1);
    aacEncoder_SetParam(*encoder, AACENC_AFTERBURNER, !!params->afterburner);

    aacEncoder_SetParam(*encoder, AACENC_SBR_MODE, params->lowdelay_sbr);

#if AACENCODER_LIB_VL0 > 3 || (AACENCODER_LIB_VL0==3 && AACENCODER_LIB_VL1>=4)
    if (lib_info.version > 0x03040000)
        aacEncoder_SetParam(*encoder, AACENC_SBR_RATIO, params->sbr_ratio);
#endif

    if (aacEncoder_SetParam(*encoder, AACENC_TRANSMUX,
                            params->transport_format) != AACENC_OK) {
        fprintf(stderr, "ERROR: unsupported transport format\n");
        goto FAIL;
    }
    if (aacEncoder_SetParam(*encoder, AACENC_SIGNALING_MODE,
                            params->sbr_signaling) != AACENC_OK) {
        fprintf(stderr, "ERROR: failed to set SBR signaling mode\n");
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
                     aacenc_frame_t *output)
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
    if (!output->data || output->capacity < obytes) {
        uint8_t *p = realloc(output->data, obytes);
        if (!p) return -1;
        output->capacity = obytes;
        output->data = p;
    }
    obufs[0] = output->data;
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
    output->size = oargs.numOutBytes;
    return oargs.numInSamples / format->channels_per_frame;
}
