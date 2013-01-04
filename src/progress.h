/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef PROGRESS_H
#define PROGRESS_H

typedef struct aacenc_progress_t {
    double start;
    double timescale;
    int64_t total;
    int64_t processed;
} aacenc_progress_t;

void aacenc_progress_init(aacenc_progress_t *progress, int64_t total,
                          int32_t timescale);
void aacenc_progress_update(aacenc_progress_t *progress, int64_t current,
                            int period);
void aacenc_progress_finish(aacenc_progress_t *progress, int64_t current);

#endif
