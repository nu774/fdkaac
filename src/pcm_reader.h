#ifndef PCM_READER_H
#define PCM_READER_H

typedef struct pcm_reader_t pcm_reader_t;

typedef struct pcm_reader_vtbl_t {
    const pcm_sample_description_t *(*get_format)(pcm_reader_t *);
    int64_t (*get_length)(pcm_reader_t *);
    int64_t (*get_position)(pcm_reader_t *);
    int (*read_frames)(pcm_reader_t *, void *, unsigned);
    void (*teardown)(pcm_reader_t **);
} pcm_reader_vtbl_t;

struct pcm_reader_t {
    pcm_reader_vtbl_t *vtbl;
};

static inline
const pcm_sample_description_t *pcm_get_format(pcm_reader_t *r)
{
    return r->vtbl->get_format(r);
}

static inline
int64_t pcm_get_length(pcm_reader_t *r)
{
    return r->vtbl->get_length(r);
}

static inline
int64_t pcm_get_position(pcm_reader_t *r)
{
    return r->vtbl->get_position(r);
}

static inline
int64_t pcm_read_frames(pcm_reader_t *r, void *data, unsigned nframes)
{
    return r->vtbl->read_frames(r, data, nframes);
}

static inline
void pcm_teardown(pcm_reader_t **r)
{
    (*r)->vtbl->teardown(r);
}

#endif
