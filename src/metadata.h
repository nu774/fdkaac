#ifndef METADATA_H
#define METADATA_H

typedef struct aacenc_tag_entry_t {
    uint32_t    tag;
    const char *name;
    const char *data;
    uint32_t    data_size;
    int         is_file_name;
} aacenc_tag_entry_t;

typedef struct aacenc_tag_param_t {
    aacenc_tag_entry_t *tag_table;
    unsigned tag_count;
    unsigned tag_table_capacity;
} aacenc_tag_param_t;

char *aacenc_load_tag_from_file(const char *path, uint32_t *data_size);

void aacenc_param_add_itmf_entry(aacenc_tag_param_t *params, uint32_t tag,
                                 const char *key, const char *value,
                                 uint32_t size, int is_file_name);

void aacenc_put_tags_from_json(m4af_ctx_t *m4af, const char *json_filename);

void aacenc_put_tag_entry(m4af_ctx_t *m4af, const aacenc_tag_entry_t *tag);

#endif
