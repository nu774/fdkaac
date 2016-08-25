/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef AACENC_H
#define AACENC_H

#include <fdk-aac/aacenc_lib.h>
#include "lpcm.h"

#define AACENC_PARAMS \
    unsigned profile; \
    unsigned bitrate; \
    unsigned bitrate_mode; \
    unsigned bandwidth; \
    unsigned afterburner; \
    unsigned lowdelay_sbr; \
    unsigned sbr_ratio; \
    unsigned sbr_signaling; \
    unsigned transport_format; \
    unsigned adts_crc_check; \
    unsigned header_period;

typedef struct aacenc_param_t {
    AACENC_PARAMS
} aacenc_param_t;

typedef struct aacenc_frame_t {
    uint8_t *data;
    uint32_t size, capacity;
} aacenc_frame_t;

int aacenc_is_explicit_bw_compatible_sbr_signaling_available();

int aacenc_is_sbr_ratio_available();

int aacenc_is_sbr_active(const aacenc_param_t *params);

int aacenc_is_dual_rate_sbr(const aacenc_param_t *params);

void aacenc_get_lib_info(LIB_INFO *info);

int aacenc_mp4asc(const aacenc_param_t *params,
                  const uint8_t *asc, uint32_t ascsize,
                  uint8_t *outasc, uint32_t *outsize);

int aacenc_init(HANDLE_AACENCODER *encoder, const aacenc_param_t *params,
                const pcm_sample_description_t *format,
                AACENC_InfoStruct *info);

int aac_encode_frame(HANDLE_AACENCODER encoder,
                     const pcm_sample_description_t *format,
                     const int16_t *input, unsigned iframes,
                     aacenc_frame_t *output);

#endif
