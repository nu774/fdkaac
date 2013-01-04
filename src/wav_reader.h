/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef WAV_READER_H
#define WAV_READER_H

#include "lpcm.h"

enum wav_error_code {
    WAV_IO_ERROR = 1,
    WAV_NO_MEMORY,
    WAV_INVALID_FORMAT,
    WAV_UNSUPPORTED_FORMAT
};

typedef int (*wav_read_callback)(void *cookie, void *data, uint32_t size);
typedef int (*wav_seek_callback)(void *cookie, int64_t off, int whence);

typedef struct wav_io_context_t {
    wav_read_callback read;
    wav_seek_callback seek;
} wav_io_context_t;

typedef struct wav_reader_t wav_reader_t;

wav_reader_t *wav_open(wav_io_context_t *io_ctx, void *io_cookie,
                       int ignore_length);
const pcm_sample_description_t *wav_get_format(wav_reader_t *reader);
int wav_read_frames(wav_reader_t *reader, void *buffer, unsigned nframes);
int64_t wav_get_length(wav_reader_t *reader);
int64_t wav_get_position(wav_reader_t *reader);
void wav_teardown(wav_reader_t **reader);

#endif
