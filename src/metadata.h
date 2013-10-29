/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef METADATA_H
#define METADATA_H

#include "m4af.h"

typedef struct aacenc_tag_entry_t {
    uint32_t tag;
    char    *name;
    char    *data;
    uint32_t data_size;
} aacenc_tag_entry_t;

typedef struct aacenc_tag_store_t {
    aacenc_tag_entry_t *tag_table;
    unsigned tag_count;
    unsigned tag_table_capacity;
} aacenc_tag_store_t;

typedef struct aacenc_translate_generic_text_tag_ctx_t {
    unsigned track, track_total, disc, disc_total;
    void   (*add)(void *, const aacenc_tag_entry_t *);
    void    *add_ctx;
} aacenc_translate_generic_text_tag_ctx_t;

typedef void (*aacenc_tag_callback_t)(void *ctx, const char *key,
                                      const char *value, uint32_t size);

void aacenc_translate_generic_text_tag(void *ctx, const char *key,
                                       const char *val, uint32_t size);


void aacenc_add_tag_to_store(aacenc_tag_store_t *store, uint32_t tag,
                             const char *key, const char *value,
                             uint32_t size, int is_file_name);

void aacenc_add_tag_entry_to_store(void *store, const aacenc_tag_entry_t *tag);

void aacenc_free_tag_store(aacenc_tag_store_t *store);

void aacenc_write_tags_from_json(m4af_ctx_t *m4af, const char *json_filename);

void aacenc_write_tag_entry(void *m4af, const aacenc_tag_entry_t *tag);

#endif
