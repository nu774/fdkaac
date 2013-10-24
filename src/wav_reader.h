/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef WAV_READER_H
#define WAV_READER_H

#include "lpcm.h"
#include "pcm_reader.h"

pcm_reader_t *wav_open(pcm_io_context_t *io, int ignore_length);
pcm_reader_t *raw_open(pcm_io_context_t *io,
                       const pcm_sample_description_t *desc);

#endif
