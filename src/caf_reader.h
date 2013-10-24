/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef CAF_READER_H
#define CAF_READER_H

#include "lpcm.h"
#include "pcm_reader.h"
#include "metadata.h"

pcm_reader_t *caf_open(pcm_io_context_t *io,
                       aacenc_tag_callback_t tag_callback, void *tag_ctx);

#endif
