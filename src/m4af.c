/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
#  include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#elif defined _MSC_VER
#  define PRId64 "I64d"
#endif
#include "m4af.h"
#include "m4af_endian.h"

#define m4af_realloc(memory,size) realloc(memory, size)
#define m4af_free(memory) free(memory)
#define m4af_max(a,b) ((a)<(b)?(b):(a))

#define M4AF_ATOM_WILD  0xffffffff

typedef struct m4af_sample_entry_t {
    uint32_t size;
    uint32_t delta;
} m4af_sample_entry_t;

typedef struct m4af_chunk_entry_t {
    int64_t offset;
    uint32_t size;
    uint32_t samples_per_chunk;
    uint32_t duration;
} m4af_chunk_entry_t;

typedef struct m4af_itmf_entry_t {
    uint32_t fcc;
    char *name;
    uint32_t type_code;
    char *data;
    uint32_t data_size;
} m4af_itmf_entry_t;

typedef struct m4af_track_t {
    uint32_t codec;
    uint32_t timescale;
    uint16_t num_channels;
    int64_t creation_time;
    int64_t modification_time;
    int64_t duration;
    uint32_t frame_duration;
    uint32_t encoder_delay;
    uint32_t padding;
    uint8_t *decSpecificInfo;
    uint32_t decSpecificInfoSize;
    uint32_t bufferSizeDB;
    uint32_t maxBitrate;
    uint32_t avgBitrate;
    int is_vbr;

    m4af_sample_entry_t *sample_table;
    uint32_t num_samples;
    uint32_t sample_table_capacity;

    m4af_chunk_entry_t *chunk_table;
    uint32_t num_chunks;
    uint32_t chunk_table_capacity;

    uint8_t *chunk_buffer;
    uint32_t chunk_size;
    uint32_t chunk_capacity;

    /* temporary, to help parsing */
    uint64_t stsc_pos;
    uint64_t stsc_size;

    uint64_t stts_pos;
    uint64_t stts_size;
} m4af_track_t;

struct m4af_ctx_t {
    uint32_t timescale;
    int64_t creation_time;
    int64_t modification_time;
    int64_t mdat_pos;
    int64_t mdat_size;
    int priming_mode;
    int last_error;

    m4af_itmf_entry_t *itmf_table;
    uint32_t num_tags;
    uint32_t itmf_table_capacity;

    m4af_io_callbacks_t io;
    void *io_cookie;

    uint16_t num_tracks;
    m4af_track_t track[2];

    m4af_itmf_entry_t current_tag;
};

typedef struct m4af_box_parser_t {
    uint32_t name;
    int (*handler)(m4af_ctx_t *ctx, uint32_t name, uint64_t size);
} m4af_box_parser_t;

static
int m4af_write_null_cb(void *cookie, const void *data, uint32_t size)
{
    int64_t *pos = cookie;
    *pos += size;
    return 0;
}
static
int m4af_seek_null_cb(void *cookie, int64_t off, int whence)
{
    int64_t *pos = cookie;
    *pos = off; /* XXX: we use only SEEK_SET */
    return 0;
}
static
int64_t m4af_tell_null_cb(void *cookie)
{
    return *((int64_t*)cookie);
}

static m4af_io_callbacks_t m4af_null_io_callbacks = {
    0, m4af_write_null_cb, m4af_seek_null_cb, m4af_tell_null_cb
};

static
int64_t m4af_timestamp(void)
{
    return (int64_t)(time(0)) + (((1970 - 1904) * 365) + 17) * 24 * 60 * 60;
}


/*
 * http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2 
 */
static
uint32_t m4af_roundup(uint32_t n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

static
int64_t m4af_tell(m4af_ctx_t *ctx)
{
    int64_t pos = -1;
    if ((pos = ctx->io.tell(ctx->io_cookie)) < 0)
        ctx->last_error = M4AF_IO_ERROR;
    return pos;
}

static
int m4af_set_pos(m4af_ctx_t *ctx, int64_t pos)
{
    int rc = -1;
    if ((rc = ctx->io.seek(ctx->io_cookie, pos, SEEK_SET)) < 0)
        ctx->last_error = M4AF_IO_ERROR;
    return rc;
}

static
int m4af_write(m4af_ctx_t *ctx, const void *data, uint32_t size)
{
    int rc = -1;
    if ((rc = ctx->io.write(ctx->io_cookie, data, size)) < 0)
        ctx->last_error = M4AF_IO_ERROR;
    return rc;
}

static
int m4af_write16(m4af_ctx_t *ctx, uint32_t data)
{
    data = m4af_htob16(data);
    return m4af_write(ctx, &data, 2);
}

static
int m4af_write32(m4af_ctx_t *ctx, uint32_t data)
{
    data = m4af_htob32(data);
    return m4af_write(ctx, &data, 4);
}

static
int m4af_write64(m4af_ctx_t *ctx, uint64_t data)
{
    data = m4af_htob64(data);
    return m4af_write(ctx, &data, 8);
}

static
int m4af_write24(m4af_ctx_t *ctx, uint32_t data)
{
    data = m4af_htob32(data << 8);
    return m4af_write(ctx, &data, 3);
}

static
void m4af_write32_at(m4af_ctx_t *ctx, int64_t pos, uint32_t value)
{
    int64_t current_pos = m4af_tell(ctx);
    m4af_set_pos(ctx, pos);
    m4af_write32(ctx, value);
    m4af_set_pos(ctx, current_pos);
}

m4af_ctx_t *m4af_create(uint32_t codec, uint32_t timescale,
                        m4af_io_callbacks_t *io, void *io_cookie, int no_timestamp)
{
    m4af_ctx_t *ctx;
    int64_t timestamp;

    if (codec != M4AF_FOURCC('m','p','4','a') &&
        codec != M4AF_FOURCC('a','l','a','c'))
        return 0;
    if ((ctx = m4af_realloc(0, sizeof(m4af_ctx_t))) == 0)
        return 0;
    memset(ctx, 0, sizeof(m4af_ctx_t));
    memcpy(&ctx->io, io, sizeof(m4af_io_callbacks_t));
    ctx->io_cookie = io_cookie;
    ctx->timescale = timescale;
    timestamp = no_timestamp ? 0 : m4af_timestamp();
    ctx->creation_time = timestamp;
    ctx->modification_time = timestamp;
    ctx->num_tracks = 1;
    ctx->track[0].codec = codec;
    ctx->track[0].timescale = timescale;
    ctx->track[0].creation_time = timestamp;
    ctx->track[0].modification_time = timestamp;
    ctx->track[0].num_channels = 2;
    return ctx;
}

static
void m4af_free_itmf_table(m4af_ctx_t *ctx)
{
    uint32_t i;
    m4af_itmf_entry_t *entry = ctx->itmf_table;
    for (i = 0; i < ctx->num_tags; ++i, ++entry) {
        if (entry->fcc == M4AF_FOURCC('-','-','-','-'))
            m4af_free(entry->name);
        m4af_free(entry->data);
    }
    m4af_free(ctx->itmf_table);
}

static
void m4af_clear_track(m4af_ctx_t *ctx, int track_idx)
{
    m4af_track_t *track = ctx->track + track_idx;
    if (track->decSpecificInfo)
        m4af_free(track->decSpecificInfo);
    if (track->sample_table)
        m4af_free(track->sample_table);
    if (track->chunk_table)
        m4af_free(track->chunk_table);
    if (track->chunk_buffer)
        m4af_free(track->chunk_buffer);
    memset(track, 0, sizeof(m4af_track_t));
}

void m4af_teardown(m4af_ctx_t **ctxp)
{
    unsigned i;
    m4af_ctx_t *ctx = *ctxp;
    for (i = 0; i < ctx->num_tracks; ++i)
        m4af_clear_track(ctx, i);
    if (ctx->itmf_table)
        m4af_free_itmf_table(ctx);
    m4af_free(ctx);
    *ctxp = 0;
}

void m4af_set_num_channels(m4af_ctx_t *ctx, uint32_t track_idx,
                           uint16_t channels)
{
    ctx->track[track_idx].num_channels = channels;
}

void m4af_set_fixed_frame_duration(m4af_ctx_t *ctx, uint32_t track_idx,
                                   uint32_t length)
{
    ctx->track[track_idx].frame_duration = length;
}

int m4af_set_decoder_specific_info(m4af_ctx_t *ctx, uint32_t track_idx,
                                   uint8_t *data, uint32_t size)
{
    m4af_track_t *track = &ctx->track[track_idx];
    if (size > track->decSpecificInfoSize) {
        uint8_t *memory = m4af_realloc(track->decSpecificInfo, size);
        if (memory == 0) {
            ctx->last_error = M4AF_NO_MEMORY;
            goto DONE;
        }
        track->decSpecificInfo = memory;
    }
    if (size > 0)
        memcpy(track->decSpecificInfo, data, size);
    track->decSpecificInfoSize = size;
DONE:
    return ctx->last_error;
}

void m4af_set_vbr_mode(m4af_ctx_t *ctx, uint32_t track_idx, int is_vbr)
{
    m4af_track_t *track = &ctx->track[track_idx];
    track->is_vbr = is_vbr;
}

void m4af_set_priming(m4af_ctx_t *ctx, uint32_t track_idx,
                      uint32_t encoder_delay, uint32_t padding)
{
    m4af_track_t *track = &ctx->track[track_idx];
    track->encoder_delay = encoder_delay;
    track->padding = padding;
}

void m4af_set_priming_mode(m4af_ctx_t *ctx, int mode)
{
    ctx->priming_mode = mode;
}

static
int m4af_add_sample_entry(m4af_ctx_t *ctx, uint32_t track_idx,
                          uint32_t size, uint32_t delta)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_sample_entry_t *entry;

    if (ctx->last_error)
        return -1;
    if (track->num_samples == track->sample_table_capacity) {
        uint32_t new_size = track->sample_table_capacity;
        new_size = new_size ? new_size * 2 : 1;
        entry = m4af_realloc(track->sample_table, new_size * sizeof(*entry));
        if (entry == 0) {
            ctx->last_error = M4AF_NO_MEMORY;
            return -1;
        }
        track->sample_table = entry;
        track->sample_table_capacity = new_size;
    }
    entry = track->sample_table + track->num_samples;
    entry->size = size;
    entry->delta = delta;
    ++track->num_samples;
    return 0;
}

static
int m4af_flush_chunk(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_chunk_entry_t *entry;
    if (!track->num_chunks || !track->chunk_size)
        return 0;
    entry = &track->chunk_table[track->num_chunks - 1];
    entry->offset = m4af_tell(ctx);
    m4af_write(ctx, track->chunk_buffer, track->chunk_size);
    ctx->mdat_size += track->chunk_size;
    track->chunk_size = 0;
    return ctx->last_error ? -1 : 0;
}

static
int m4af_add_chunk_entry(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_chunk_entry_t *entry;
    if (track->num_chunks == track->chunk_table_capacity) {
        uint32_t new_size = track->chunk_table_capacity;
        new_size = new_size ? new_size * 2 : 1;
        entry = m4af_realloc(track->chunk_table, new_size * sizeof(*entry));
        if (entry == 0) {
            ctx->last_error = M4AF_NO_MEMORY;
            return -1;
        }
        track->chunk_table = entry;
        track->chunk_table_capacity = new_size;
    }
    memset(&track->chunk_table[track->num_chunks++], 0,
           sizeof(m4af_chunk_entry_t));
    return 0;
}

static
int m4af_update_chunk_table(m4af_ctx_t *ctx, uint32_t track_idx,
                            uint32_t size, uint32_t delta)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_chunk_entry_t *entry;
    int add_new_chunk = 0;

    if (ctx->last_error)
        return -1;
    if (track->num_chunks == 0)
        add_new_chunk = 1;
    else {
        entry = &track->chunk_table[track->num_chunks - 1];
        if (entry->duration + delta > track->timescale / 2)
            add_new_chunk = 1;
    }
    if (add_new_chunk) {
        m4af_flush_chunk(ctx, track_idx);
        if (m4af_add_chunk_entry(ctx, track_idx) < 0)
            return -1;
    }
    entry = &track->chunk_table[track->num_chunks - 1];
    entry->size += size;
    ++entry->samples_per_chunk;
    entry->duration += delta;
    return 0;
}

static
void m4af_update_max_bitrate(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    uint32_t duration = 0, size = 0, bitrate;
    m4af_sample_entry_t *ent = track->sample_table + track->num_samples - 1;

    for (; ent >= track->sample_table && duration < track->timescale; --ent) {
        duration += ent->delta;
        size += ent->size;
    }
    bitrate = (uint32_t)(size * 8.0 * track->timescale / duration + .5);
    if (bitrate > track->maxBitrate)
        track->maxBitrate = bitrate;
}

static
int m4af_append_sample_to_chunk(m4af_ctx_t *ctx, uint32_t track_idx,
                                const void *data, uint32_t size)
{
    m4af_track_t *track = &ctx->track[track_idx];
    uint32_t newsize = track->chunk_size + size;

    if (ctx->last_error)
        return -1;
    if (track->chunk_capacity < newsize) {
        uint32_t capacity = m4af_roundup(newsize);
        uint8_t *memory = realloc(track->chunk_buffer, capacity);
        if (!memory) {
            ctx->last_error = M4AF_NO_MEMORY;
            return -1;
        }
        track->chunk_buffer = memory;
        track->chunk_capacity = capacity;
    }
    memcpy(track->chunk_buffer + track->chunk_size, data, size);
    track->chunk_size = newsize;
    return 0;
}

int m4af_write_sample(m4af_ctx_t *ctx, uint32_t track_idx, const void *data,
                      uint32_t size, uint32_t duration)
{
    m4af_track_t *track = &ctx->track[track_idx];
    if (track->frame_duration)
        duration = track->frame_duration;
    if (size > track->bufferSizeDB)
        track->bufferSizeDB = size;
    track->duration += duration;
    m4af_add_sample_entry(ctx, track_idx, size, duration);
    m4af_update_chunk_table(ctx, track_idx, size, duration);
    m4af_update_max_bitrate(ctx, track_idx);
    m4af_append_sample_to_chunk(ctx, track_idx, data, size);
    return ctx->last_error;
}

static
m4af_itmf_entry_t *m4af_find_itmf_slot(m4af_ctx_t *ctx, uint32_t fcc,
                                       const char *name)
{
    m4af_itmf_entry_t *entry = ctx->itmf_table;

    if (name)
        fcc = M4AF_FOURCC('-','-','-','-');

    if (fcc != M4AF_TAG_ARTWORK)
        for (; entry != ctx->itmf_table + ctx->num_tags; ++entry)
            if (fcc == entry->fcc && (!name || !strcmp(name, entry->name)))
                return entry;

    if (ctx->num_tags == ctx->itmf_table_capacity) {
        uint32_t new_size = ctx->itmf_table_capacity;
        new_size = new_size ? new_size * 2 : 1;
        entry = m4af_realloc(ctx->itmf_table, new_size * sizeof(*entry));
        if (entry == 0) {
            ctx->last_error = M4AF_NO_MEMORY;
            return 0;
        }
        ctx->itmf_table = entry;
        ctx->itmf_table_capacity = new_size;
    }
    entry = &ctx->itmf_table[ctx->num_tags++];
    memset(entry, 0, sizeof(m4af_itmf_entry_t));
    entry->fcc = fcc;
    if (name) {
        char *name_copy = m4af_realloc(0, strlen(name) + 1);
        if (!name_copy) {
            ctx->last_error = M4AF_NO_MEMORY;
            --ctx->num_tags;
            return 0;
        }
        strcpy(name_copy, name);
        entry->name = name_copy;
    }
    return entry;
}

int m4af_add_itmf_long_tag(m4af_ctx_t *ctx, const char *name,
                           const char *data)
{
    m4af_itmf_entry_t *entry;
    char *data_copy = 0;
    size_t name_len = strlen(name);
    size_t data_len = strlen(data);
    if (!name_len || !data_len)
        return 0;

    if ((entry = m4af_find_itmf_slot(ctx, 0, name)) == 0)
        goto FAIL;
    entry->type_code = M4AF_UTF8;
    if ((data_copy = m4af_realloc(entry->data, data_len)) == 0) {
        ctx->last_error = M4AF_NO_MEMORY;
        goto FAIL;
    }
    memcpy(data_copy, data, data_len);
    entry->data = data_copy;
    entry->data_size = data_len;
    return 0;
FAIL:
    return ctx->last_error;
}

int m4af_add_itmf_short_tag(m4af_ctx_t *ctx, uint32_t fcc,
                            uint32_t type_code, const void *data,
                            uint32_t data_size)
{
    m4af_itmf_entry_t *entry;
    char *data_copy = 0;
    
    if (!data_size)
        return 0;
    if ((entry = m4af_find_itmf_slot(ctx, fcc, 0)) == 0)
        goto FAIL;
    entry->type_code = type_code;
    if ((data_copy = m4af_realloc(entry->data, data_size)) == 0) {
        ctx->last_error = M4AF_NO_MEMORY;
        goto FAIL;
    }
    memcpy(data_copy, data, data_size);
    entry->data = data_copy;
    entry->data_size = data_size;
    return 0;
FAIL:
    return ctx->last_error;
}

int m4af_add_itmf_string_tag(m4af_ctx_t *ctx, uint32_t fcc, const char *data)
{
    return m4af_add_itmf_short_tag(ctx, fcc, M4AF_UTF8, data, strlen(data));
}

int m4af_add_itmf_int8_tag(m4af_ctx_t *ctx, uint32_t fcc, int value)
{
    uint8_t data = value;
    return m4af_add_itmf_short_tag(ctx, fcc, M4AF_INTEGER, &data, 1);
}

int m4af_add_itmf_int16_tag(m4af_ctx_t *ctx, uint32_t fcc, int value)
{
    uint16_t data = m4af_htob16(value);
    return m4af_add_itmf_short_tag(ctx, fcc, M4AF_INTEGER, &data, 2);
}

int m4af_add_itmf_int32_tag(m4af_ctx_t *ctx, uint32_t fcc, uint32_t value)
{
    uint32_t data = m4af_htob32(value);
    return m4af_add_itmf_short_tag(ctx, fcc, M4AF_INTEGER, &data, 4);
}

int m4af_add_itmf_int64_tag(m4af_ctx_t *ctx, uint32_t fcc, uint64_t value)
{
    uint64_t data = m4af_htob64(value);
    return m4af_add_itmf_short_tag(ctx, fcc, M4AF_INTEGER, &data, 8);
}

int m4af_add_itmf_track_tag(m4af_ctx_t *ctx, int track, int total)
{
    uint16_t data[4] = { 0 };
    data[1] = m4af_htob16(track);
    data[2] = m4af_htob16(total);
    return m4af_add_itmf_short_tag(ctx, M4AF_FOURCC('t','r','k','n'),
                                   M4AF_IMPLICIT, &data, 8);
}

int m4af_add_itmf_disk_tag(m4af_ctx_t *ctx, int disk, int total)
{
    uint16_t data[3] = { 0 };
    data[1] = m4af_htob16(disk);
    data[2] = m4af_htob16(total);
    return m4af_add_itmf_short_tag(ctx, M4AF_FOURCC('d','i','s','k'),
                                   M4AF_IMPLICIT, &data, 6);
}

int m4af_add_itmf_genre_tag(m4af_ctx_t *ctx, int genre)
{
    uint16_t data = m4af_htob16(genre);
    return m4af_add_itmf_short_tag(ctx, M4AF_FOURCC('g','n','r','e'),
                                   M4AF_IMPLICIT, &data, 2);
}

static
int m4af_set_iTunSMPB(m4af_ctx_t *ctx)
{
    const char *fmt = " 00000000 %08X %08X %08X%08X 00000000 00000000 "
        "00000000 00000000 00000000 00000000 00000000 00000000";
    m4af_track_t *track = &ctx->track[0];
    char buf[256];
    uint64_t length = track->duration - track->encoder_delay - track->padding;
    sprintf(buf, fmt, track->encoder_delay, track->padding,
            (uint32_t)(length >> 32), (uint32_t)length);
    return m4af_add_itmf_long_tag(ctx, "iTunSMPB", buf);
}

static
uint32_t m4af_update_box_size(m4af_ctx_t *ctx, int64_t pos)
{
    int64_t current_pos = m4af_tell(ctx);
    m4af_set_pos(ctx, pos);
    m4af_write32(ctx, current_pos - pos);
    m4af_set_pos(ctx, current_pos);
    return current_pos - pos;
}

static
void m4af_write_descriptor(m4af_ctx_t *ctx, uint32_t tag, uint32_t size)
{
    uint8_t buf[5];
    buf[0] = tag;
    buf[1] = ((size >> 21) | 0x80);
    buf[2] = ((size >> 14) | 0x80);
    buf[3] = ((size >>  7) | 0x80);
    buf[4] = (size & 0x7f);
    m4af_write(ctx, buf, 5);
}

static
void m4af_write_ftyp_box(m4af_ctx_t *ctx)
{
    m4af_write(ctx, "\0\0\0\040""ftypM4A \0\0\0\0M4A mp42isom\0\0\0\0", 32);
}

static
void m4af_write_free_box(m4af_ctx_t *ctx, uint32_t size)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write32(ctx, size + 8);
    m4af_write(ctx, "free", 4);
    if (size > 0)
        m4af_set_pos(ctx, pos + size + 8);
}

int m4af_begin_write(m4af_ctx_t *ctx)
{
    m4af_write_ftyp_box(ctx);
    m4af_write_free_box(ctx, 0);
    m4af_write(ctx, "\0\0\0\0mdat", 8);
    ctx->mdat_pos = m4af_tell(ctx);
    return ctx->last_error;
}

static
void m4af_write_stco_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    uint32_t i;
    m4af_chunk_entry_t *index = track->chunk_table;
    int is_co64 = index[track->num_chunks - 1].offset > 0xffffffff;
    int64_t pos = m4af_tell(ctx);

    m4af_write32(ctx, 0); /* size */
    m4af_write(ctx, is_co64 ? "co64" : "stco", 4);
    m4af_write32(ctx, 0); /* version and flags */
    m4af_write32(ctx, track->num_chunks);
    for (i = 0; i < track->num_chunks; ++i, ++index) {
        if (is_co64)
            m4af_write64(ctx, index->offset);
        else
            m4af_write32(ctx, index->offset);
    }
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_stsz_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_sample_entry_t *index = track->sample_table;
    uint32_t i;
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx,
               "\0\0\0\0"  /* size                     */
               "stsz"      /* type                     */
               "\0"        /* version                  */
               "\0\0\0"    /* flags                    */
               "\0\0\0\0"  /* sample_size: 0(variable) */
               , 16);
    m4af_write32(ctx, track->num_samples);
    for (i = 0; i < track->num_samples; ++i, ++index)
        m4af_write32(ctx, index->size);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_stsc_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_chunk_entry_t *index = track->chunk_table;
    uint32_t i, prev_samples_per_chunk = 0, entry_count = 0;
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx,
               "\0\0\0\0"  /* size        */
               "stsc"      /* type        */
               "\0"        /* version     */
               "\0\0\0"    /* flags       */
               "\0\0\0\0"  /* entry_count */
               , 16);

    for (i = 0; i < track->num_chunks; ++i, ++index) {
        if (index->samples_per_chunk != prev_samples_per_chunk) {
            ++entry_count;
            m4af_write32(ctx, i + 1);
            m4af_write32(ctx, index->samples_per_chunk);
            m4af_write32(ctx, 1); /* sample_description_index */
            prev_samples_per_chunk = index->samples_per_chunk;
        }
    }
    m4af_write32_at(ctx, pos + 12, entry_count);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_stts_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_sample_entry_t *index = track->sample_table;
    uint32_t i, prev_delta = 0, entry_count = 0, sample_count = 0;
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx,
               "\0\0\0\0"  /* size        */
               "stts"      /* type        */
               "\0"        /* version     */
               "\0\0\0"    /* flags       */
               "\0\0\0\0"  /* entry_count */
               , 16);

    for (i = 0; i < track->num_samples; ++i, ++index) {
        if (index->delta == prev_delta)
            ++sample_count;
        else {
            ++entry_count;
            if (sample_count) {
                m4af_write32(ctx, sample_count);
                m4af_write32(ctx, prev_delta);
            }
            prev_delta = index->delta;
            sample_count = 1;
        } 
    }
    if (sample_count) {
        m4af_write32(ctx, sample_count);
        m4af_write32(ctx, prev_delta);
    }
    m4af_write32_at(ctx, pos + 12, entry_count);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_esds_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0esds", 8);
    m4af_write32(ctx, 0); /* version + flags */

    /* ES_Descriptor */
    m4af_write_descriptor(ctx, 3, 32 + track->decSpecificInfoSize);
    m4af_write(ctx, "\0\0\0", 3);
    /* DecoderConfigDescriptor */
    m4af_write_descriptor(ctx, 4, 18 + track->decSpecificInfoSize);
    m4af_write(ctx,
               "\x40"   /* objectTypeIndication: 0x40(Audio ISO/IEC 14496-3)*/
               "\x15"   /* streamType(6): 0x05(AudioStream)                 
                         * upStream(1)  : 0
                         * reserved(1)  : 1
                         */
               , 2);
    m4af_write24(ctx, track->bufferSizeDB);
    m4af_write32(ctx, track->maxBitrate);
    m4af_write32(ctx, track->is_vbr ? 0: track->avgBitrate);
    /* DecoderSpecificInfo */
    m4af_write_descriptor(ctx, 5, track->decSpecificInfoSize);
    m4af_write(ctx, track->decSpecificInfo, track->decSpecificInfoSize);
    /* SLConfigDescriptor */
    m4af_write_descriptor(ctx, 6, 1);
    m4af_write(ctx, "\002", 1); /* predefined */

    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_alac_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx,
               "\0\0\0\0"  /* size        */
               "alac"      /* type        */
               "\0"        /* version     */
               "\0\0\0"    /* flags       */
               , 12);
    m4af_write(ctx, track->decSpecificInfo, track->decSpecificInfoSize);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_mp4a_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    m4af_write32(ctx, 0);   /* size         */
    m4af_write32(ctx, track->codec); /* mp4a or alac */
    m4af_write(ctx,
               "\0\0\0\0\0\0" /* reserved                */
               "\0\001"       /* data_reference_index: 1 */
               "\0\0\0\0"     /* reserved[0]             */
               "\0\0\0\0"     /* reserved[1]             */
               ,16);
    m4af_write16(ctx, track->num_channels);
    m4af_write(ctx,
               "\0\020"       /* samplesize: 16          */
               "\0\0"         /* pre_defined             */
               "\0\0"         /* reserved                */
               ,6);
    if (track->codec == M4AF_FOURCC('m','p','4','a')) {
        m4af_write32(ctx, track->timescale << 16);
        m4af_write_esds_box(ctx, track_idx);
    } else {
        m4af_write32(ctx, 44100 << 16);
        m4af_write_alac_box(ctx, track_idx);
    }
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_stsd_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0stsd", 8);
    m4af_write(ctx,
               "\0"          /* version        */
               "\0\0\0"      /* flags          */
               "\0\0\0\001"  /* entry_count: 1 */
               , 8);
    m4af_write_mp4a_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_sbgp_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    m4af_write(ctx,
               "\0\0\0\034"  /* size: 28                */
               "sbgp"        /* type                    */
               "\0"          /* version                 */
               "\0\0\0"      /* flags                   */
               "roll"        /* grouping_type           */
               "\0\0\0\001"  /* entry_count: 1          */
               , 20);
    m4af_write32(ctx, track->num_samples);
    m4af_write32(ctx, 1);    /* group_description_index */
}

static
void m4af_write_sgpd_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_write(ctx,
               "\0\0\0\026"  /* size: 22           */
               "sgpd"        /* type               */
               "\0"          /* version            */
               "\0\0\0"      /* flags              */
               "roll"        /* grouping_type      */
               "\0\0\0\001"  /* entry_count: 1     */
               "\377\377"    /* payload_data: -1   */
               , 22);
}

static
void m4af_write_stbl_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0stbl", 8);
    m4af_write_stsd_box(ctx, track_idx);
    if ((ctx->priming_mode & M4AF_PRIMING_MODE_EDTS) &&
        (track->encoder_delay || track->padding)) {
        m4af_write_sbgp_box(ctx, track_idx);
        m4af_write_sgpd_box(ctx, track_idx);
    }
    m4af_write_stts_box(ctx, track_idx);
    m4af_write_stsc_box(ctx, track_idx);
    m4af_write_stsz_box(ctx, track_idx);
    m4af_write_stco_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_url_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_write(ctx,
               "\0\0\0\014"  /* size                       */
               "url "        /* type                       */
               "\0"          /* version                    */
               "\0\0\001"    /* flags: 1(in the same file) */
               , 12);
}

static
void m4af_write_dref_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0dref", 8);
    m4af_write(ctx,
               "\0"          /* version        */
               "\0\0\0"      /* flags          */
               "\0\0\0\001"  /* entry_count: 1 */
               ,8);
    m4af_write_url_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_dinf_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0dinf", 8);
    m4af_write_dref_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_smhd_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_write(ctx,
               "\0\0\0\020"  /* size          */
               "smhd"        /* type          */
               "\0"          /* version       */
               "\0\0\0"      /* flags         */
               "\0\0"        /* balance       */
               "\0\0"        /* reserved      */
               , 16);
}

static
void m4af_write_minf_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0minf", 8);
    /* TODO: add TEXT support */
    if (track->codec != M4AF_CODEC_TEXT)
        m4af_write_smhd_box(ctx, track_idx);
    m4af_write_dinf_box(ctx, track_idx);
    m4af_write_stbl_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_mdhd_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    uint8_t version = (track->creation_time > UINT32_MAX ||
                       track->modification_time > UINT32_MAX ||
                       track->duration > UINT32_MAX);

    m4af_write(ctx, "\0\0\0\0mdhd", 8);
    m4af_write(ctx, &version, 1);
    m4af_write(ctx, "\0\0\0", 3); /* flags */
    if (version) {
        m4af_write64(ctx, track->creation_time);
        m4af_write64(ctx, track->modification_time);
        m4af_write32(ctx, track->timescale);
        m4af_write64(ctx, track->duration);
    } else {
        m4af_write32(ctx, track->creation_time);
        m4af_write32(ctx, track->modification_time);
        m4af_write32(ctx, track->timescale);
        m4af_write32(ctx, track->duration);
    }
    m4af_write(ctx,
               "\x55\xc4"  /* language: und */
               "\0\0"      /* pre_defined   */
               , 4);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_hdlr_box(m4af_ctx_t *ctx, uint32_t track_idx, const char *type)
{
    int64_t pos = m4af_tell(ctx);
    static const char reserved_and_name[10] = { 0 };

    m4af_write(ctx,
               "\0\0\0\0"  /* size          */
               "hdlr"      /* type          */
               "\0"        /* version       */
               "\0\0\0"    /* flags         */
               "\0\0\0\0"  /* pre_defined   */
               , 16);
    m4af_write(ctx, type, 4); /* handler_type */
    /* reserved[0] */
    m4af_write(ctx, !strcmp(type, "mdir") ? "appl" : "\0\0\0\0", 4);
    /* reserved[1], reserved[2], name */
    m4af_write(ctx, reserved_and_name, 9);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_mdia_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    const char *hdlr =
        (track->codec == M4AF_CODEC_TEXT) ? "text" : "soun";
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0mdia", 8);
    m4af_write_mdhd_box(ctx, track_idx);
    m4af_write_hdlr_box(ctx, track_idx, hdlr);
    m4af_write_minf_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_elst_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    uint8_t version;
    int64_t duration = track->duration - track->encoder_delay - track->padding;
    int64_t pos = m4af_tell(ctx);
    duration = (double)duration / track->timescale * ctx->timescale + .5;
    version  = (duration > UINT32_MAX);

    m4af_write(ctx, "\0\0\0\0elst", 8);
    m4af_write(ctx, &version, 1);
    m4af_write(ctx, "\0\0\0", 3);  /* flags          */
    m4af_write32(ctx, 1);          /* entry_count: 1 */
    if (version) {
        m4af_write64(ctx, duration);
        m4af_write64(ctx, track->encoder_delay);
    } else {
        m4af_write32(ctx, duration);
        m4af_write32(ctx, track->encoder_delay);
    }
    m4af_write16(ctx, 1);    /* media_rate_integer  */
    m4af_write16(ctx, 0);    /* media_rate_fraction */
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_edts_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0edts", 8);
    m4af_write_elst_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_tkhd_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    int64_t duration =
        (double)track->duration / track->timescale * ctx->timescale + .5;
    uint8_t version = (track->creation_time > UINT32_MAX ||
                       track->modification_time > UINT32_MAX ||
                       duration > UINT32_MAX);
    m4af_write(ctx, "\0\0\0\0tkhd", 8);
    m4af_write(ctx, &version, 1);
    m4af_write(ctx, "\0\0\007", 3);  /* flags  */
    if (version) {
        m4af_write64(ctx, track->creation_time);
        m4af_write64(ctx, track->modification_time);
        m4af_write32(ctx, track_idx + 1);
        m4af_write(ctx, "\0\0\0\0"   /* reserved    */
                        , 4);
        m4af_write64(ctx, duration);
    } else {
        m4af_write32(ctx, track->creation_time);
        m4af_write32(ctx, track->modification_time);
        m4af_write32(ctx, track_idx + 1);
        m4af_write(ctx, "\0\0\0\0"   /* reserved    */
                        , 4);
        m4af_write32(ctx, duration);
    }
    m4af_write(ctx,
               "\0\0\0\0"   /* reserved[0]      */
               "\0\0\0\0"   /* reserved[1]      */
               "\0\0"       /* layer            */
               "\0\0"       /* alternate_group  */
               "\001\0"     /* volume: 1.0      */
               "\0\0"       /* reserved         */
               "\0\001\0\0" /* matrix[0]        */
               "\0\0\0\0"   /* matrix[1]        */
               "\0\0\0\0"   /* matrix[2]        */
               "\0\0\0\0"   /* matrix[3]        */
               "\0\001\0\0" /* matrix[4]        */
               "\0\0\0\0"   /* matrix[5]        */
               "\0\0\0\0"   /* matrix[6]        */
               "\0\0\0\0"   /* matrix[7]        */
               "\100\0\0\0" /* matrix[8]        */
               "\0\0\0\0"   /* width            */
               "\0\0\0\0"   /* height           */
               , 60);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_trak_box(m4af_ctx_t *ctx, uint32_t track_idx)
{
    m4af_track_t *track = &ctx->track[track_idx];
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0trak", 8);
    m4af_write_tkhd_box(ctx, track_idx);
    if ((ctx->priming_mode & M4AF_PRIMING_MODE_EDTS) &&
        (track->encoder_delay || track->padding))
        m4af_write_edts_box(ctx, track_idx);
    m4af_write_mdia_box(ctx, track_idx);
    m4af_update_box_size(ctx, pos);
}

static
int64_t m4af_movie_duration(m4af_ctx_t *ctx)
{
    int64_t movie_duration = 0;
    unsigned i;
    for (i = 0; i < ctx->num_tracks; ++i) {
        double x = ctx->track[i].duration;
        int64_t duration = x / ctx->track[i].timescale * ctx->timescale + .5;
        if (duration > movie_duration)
            movie_duration = duration;
    }
    return movie_duration;
}

static
void m4af_write_mvhd_box(m4af_ctx_t *ctx)
{
    int64_t pos = m4af_tell(ctx);
    int64_t movie_duration = m4af_movie_duration(ctx);
    uint8_t version = (ctx->creation_time > UINT32_MAX ||
                       ctx->modification_time > UINT32_MAX ||
                       movie_duration > UINT32_MAX);

    m4af_write(ctx, "\0\0\0\0mvhd", 8);
    m4af_write(ctx, &version, 1);
    m4af_write(ctx, "\0\0\0", 3); /* flags */
    if (version) {
        m4af_write64(ctx, ctx->creation_time);
        m4af_write64(ctx, ctx->modification_time);
        m4af_write32(ctx, ctx->timescale);
        m4af_write64(ctx, movie_duration);
    } else {
        m4af_write32(ctx, ctx->creation_time);
        m4af_write32(ctx, ctx->modification_time);
        m4af_write32(ctx, ctx->timescale);
        m4af_write32(ctx, movie_duration);
    }
    m4af_write(ctx,
               "\0\001\0\0" /* rate: 1.0        */
               "\001\0"     /* volume: 1.0      */
               "\0\0"       /* reserved         */
               "\0\0\0\0"   /* reserved[0]      */
               "\0\0\0\0"   /* reserved[1]      */
               "\0\001\0\0" /* matrix[0]        */
               "\0\0\0\0"   /* matrix[1]        */
               "\0\0\0\0"   /* matrix[2]        */
               "\0\0\0\0"   /* matrix[3]        */
               "\0\001\0\0" /* matrix[4]        */
               "\0\0\0\0"   /* matrix[5]        */
               "\0\0\0\0"   /* matrix[6]        */
               "\0\0\0\0"   /* matrix[7]        */
               "\100\0\0\0" /* matrix[8]        */
               "\0\0\0\0"   /* pre_defined[0]   */
               "\0\0\0\0"   /* pre_defined[1]   */
               "\0\0\0\0"   /* pre_defined[2]   */
               "\0\0\0\0"   /* pre_defined[3]   */
               "\0\0\0\0"   /* pre_defined[4]   */
               "\0\0\0\0"   /* pre_defined[5]   */
               , 76);
    m4af_write32(ctx, ctx->num_tracks + 1);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_mean_box(m4af_ctx_t *ctx)
{
    m4af_write(ctx,
               "\0\0\0\034"       /* size           */
               "mean"
               "\0"               /* version        */
               "\0\0\0"           /* flags          */
               "com.apple.iTunes" /* meaning-string */
               , 28);
}

static
void m4af_write_name_box(m4af_ctx_t *ctx, const char *name)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx,
               "\0\0\0\0"  /* size    */
               "name"      /* type    */
               "\0"        /* version */
               "\0\0\0"    /* flags   */
               , 12);
    m4af_write(ctx, name, strlen(name));
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_data_box(m4af_ctx_t *ctx, uint32_t type_code,
                         const char *data, uint32_t data_size)
{
    int64_t pos = m4af_tell(ctx);
    uint8_t code = type_code;
    m4af_write(ctx,
               "\0\0\0\0"  /* size              */
               "data"      /* type              */
               "\0\0"      /* reserved          */
               "\0"        /* type_set_indifier */
               ,11);
    m4af_write(ctx, &code, 1);
    m4af_write(ctx, "\0\0\0\0", 4);   /* locale */
    m4af_write(ctx, data, data_size);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_metadata(m4af_ctx_t *ctx, m4af_itmf_entry_t *entry)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0", 4);
    m4af_write32(ctx, entry->fcc);
    if (entry->fcc != M4AF_FOURCC('-','-','-','-'))
        m4af_write_data_box(ctx, entry->type_code,
                            entry->data, entry->data_size);
    else {
        m4af_write_mean_box(ctx);
        m4af_write_name_box(ctx, entry->name);
        m4af_write_data_box(ctx, 1, entry->data, entry->data_size);
    }
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_ilst_box(m4af_ctx_t *ctx)
{
    uint32_t i;
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0ilst", 8);
    for (i = 0; i < ctx->num_tags; ++i)
        m4af_write_metadata(ctx, &ctx->itmf_table[i]);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_meta_box(m4af_ctx_t *ctx)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx,
               "\0\0\0\0"  /* size                     */
               "meta"      /* type                     */
               "\0"        /* version                  */
               "\0\0\0"    /* flags                    */
               , 12);
    m4af_write_hdlr_box(ctx, 0, "mdir");
    m4af_write_ilst_box(ctx);
    m4af_update_box_size(ctx, pos);
}

static
void m4af_write_udta_box(m4af_ctx_t *ctx)
{
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0udta", 8);
    m4af_write_meta_box(ctx);
    m4af_update_box_size(ctx, pos);
}

static
uint32_t m4af_write_moov_box(m4af_ctx_t *ctx)
{
    unsigned i;
    int64_t pos = m4af_tell(ctx);
    m4af_write(ctx, "\0\0\0\0moov", 8);
    m4af_write_mvhd_box(ctx);
    for (i = 0; i < ctx->num_tracks; ++i)
        m4af_write_trak_box(ctx, i);
    if (ctx->num_tags)
        m4af_write_udta_box(ctx);
    return m4af_update_box_size(ctx, pos);
}

static
void m4af_finalize_mdat(m4af_ctx_t *ctx)
{
    if (ctx->mdat_size + 8 > UINT32_MAX) {
        m4af_set_pos(ctx, ctx->mdat_pos - 16);
        m4af_write32(ctx, 1);
        m4af_write(ctx, "mdat", 4);
        m4af_write64(ctx, ctx->mdat_size + 16);
    } else {
        m4af_set_pos(ctx, ctx->mdat_pos - 8);
        m4af_write32(ctx, ctx->mdat_size + 8);
    }
    m4af_set_pos(ctx, ctx->mdat_pos + ctx->mdat_size);
}

static
uint64_t m4af_patch_moov(m4af_ctx_t *ctx, uint32_t moov_size, uint32_t offset)
{
    int64_t pos = 0;
    uint32_t moov_size2;
    int i, j;
    m4af_io_callbacks_t io_reserve = ctx->io;
    void *io_cookie_reserve = ctx->io_cookie;

    for (i = 0; i < ctx->num_tracks; ++i)
        for (j = 0; j < ctx->track[i].num_chunks; ++j)
            ctx->track[i].chunk_table[j].offset += offset;

    ctx->io = m4af_null_io_callbacks;
    ctx->io_cookie = &pos;
    moov_size2 = m4af_write_moov_box(ctx);

    if (moov_size2 != moov_size) {
        /* stco -> co64 switching */
        for (i = 0; i < ctx->num_tracks; ++i)
            for (j = 0; j < ctx->track[i].num_chunks; ++j)
                ctx->track[i].chunk_table[j].offset += moov_size2 - moov_size;
        moov_size2 = m4af_write_moov_box(ctx);
    }
    ctx->io = io_reserve;
    ctx->io_cookie = io_cookie_reserve;
    return moov_size2;
}

static
void m4af_shift_mdat_pos(m4af_ctx_t *ctx, uint32_t offset)
{
    int64_t begin, end;
    char *buf;
    
    buf = malloc(1024*1024*2);

    end = ctx->mdat_pos + ctx->mdat_size;
    for (; (begin = m4af_max(ctx->mdat_pos, end - 1024*1024*2)) < end;
            end = begin) {
        m4af_set_pos(ctx, begin);
        ctx->io.read(ctx->io_cookie, buf, end - begin);
        m4af_set_pos(ctx, begin + offset);
        m4af_write(ctx, buf, end - begin);
    }
    ctx->mdat_pos += offset;
    m4af_set_pos(ctx, ctx->mdat_pos - 16);
    m4af_write_free_box(ctx, 0);
    m4af_write(ctx, "\0\0\0\0mdat", 8);
    m4af_finalize_mdat(ctx);

    free(buf);
}

int m4af_finalize(m4af_ctx_t *ctx, int optimize)
{
    unsigned i;
    m4af_track_t *track;
    uint32_t moov_size;

    for (i = 0; i < ctx->num_tracks; ++i) {
        track = ctx->track + i;
        if (track->duration) {
            int64_t track_size = 0;
            unsigned j;
            for (j = 0; j < track->num_chunks; ++j)
                track_size += track->chunk_table[j].size;
            track->avgBitrate =
                8.0 * track_size * track->timescale / track->duration + .5;
        }
        m4af_flush_chunk(ctx, i);
    }
    track = ctx->track;
    if ((ctx->priming_mode & M4AF_PRIMING_MODE_ITUNSMPB) &&
        (track->encoder_delay || track->padding))
        m4af_set_iTunSMPB(ctx);
    m4af_finalize_mdat(ctx);
    moov_size = m4af_write_moov_box(ctx);
    if (optimize) {
        int64_t pos;
        uint32_t moov_size2 = m4af_patch_moov(ctx, moov_size, moov_size + 1024);
        m4af_shift_mdat_pos(ctx, moov_size2 + 1024);
        m4af_set_pos(ctx, 32);
        m4af_write_moov_box(ctx);
        pos = m4af_tell(ctx);
        m4af_write_free_box(ctx, ctx->mdat_pos - pos - 24);
    }
    return ctx->last_error;
}
