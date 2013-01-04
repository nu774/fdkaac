/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef LPCM_H
#define LPCM_H

enum pcm_type {
    PCM_TYPE_UNKNOWN = 0,
    PCM_TYPE_SINT = 1,
    PCM_TYPE_UINT = 2,
    PCM_TYPE_FLOAT = 4,
    PCM_TYPE_SINT_BE = (8|1),
    PCM_TYPE_UINT_BE = (8|2),
    PCM_TYPE_FLOAT_BE = (8|4),
};

typedef struct pcm_sample_description_t {
    enum pcm_type sample_type;
    uint32_t sample_rate;
    uint8_t bits_per_channel;
    uint8_t bytes_per_frame;
    uint8_t channels_per_frame;
    uint32_t channel_mask;
} pcm_sample_description_t;

#define PCM_IS_SINT(desc) ((desc)->sample_type & 1)
#define PCM_IS_UINT(desc) ((desc)->sample_type & 2)
#define PCM_IS_FLOAT(desc) ((desc)->sample_type & 4)
#define PCM_IS_BIG_ENDIAN(desc) ((desc)->sample_type & 8)
#define PCM_BYTES_PER_CHANNEL(desc) \
    ((desc)->bytes_per_frame / (desc)->channels_per_frame)

int pcm_convert_to_native_sint16(const pcm_sample_description_t *format,
                                 const void *input, uint32_t nframes,
                                 int16_t **result, uint32_t *osize);
#endif
