/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include "pcm_reader.h"
#include "m4af_endian.h"

int pcm_read(pcm_io_context_t *io, void *buffer, uint32_t size)
{
    int rc;
    uint32_t count = 0;

    do {
        rc = io->vtbl->read(io->cookie, buffer, size - count);
        if (rc > 0)
            count += rc;
    } while (rc > 0 && count < size);
    return count > 0 ? count : rc;
}

int pcm_skip(pcm_io_context_t *io, int64_t count)
{
    char buff[8192];
    int rc;
    pcm_io_vtbl_t *vp = io->vtbl;

    if (count == 0 || pcm_seek(io, count, SEEK_CUR) >= 0)
        return 0;
    do {
        if ((rc = vp->read(io->cookie, buff, count > 8192 ? 8192 : count)) > 0)
            count -= rc;
    } while (rc > 0 && count > 0);

    return count == 0 ? 0 : -1;
}

int pcm_read16le(pcm_io_context_t *io, uint16_t *value)
{
    if (pcm_read(io, value, 2) == 2) {
        *value = m4af_ltoh16(*value);
        return 0;
    }
    return -1;
}

int pcm_read16be(pcm_io_context_t *io, uint16_t *value)
{
    if (pcm_read(io, value, 2) == 2) {
        *value = m4af_btoh16(*value);
        return 0;
    }
    return -1;
}

int pcm_read32le(pcm_io_context_t *io, uint32_t *value)
{
    if (pcm_read(io, value, 4) == 4) {
        *value = m4af_ltoh32(*value);
        return 0;
    }
    return -1;
}

int pcm_read32be(pcm_io_context_t *io, uint32_t *value)
{
    if (pcm_read(io, value, 4) == 4) {
        *value = m4af_btoh32(*value);
        return 0;
    }
    return -1;
}

int pcm_read64le(pcm_io_context_t *io, uint64_t *value)
{
    if (pcm_read(io, value, 8) == 8) {
        *value = m4af_ltoh64(*value);
        return 0;
    }
    return -1;
}

int pcm_read64be(pcm_io_context_t *io, uint64_t *value)
{
    if (pcm_read(io, value, 8) == 8) {
        *value = m4af_btoh64(*value);
        return 0;
    }
    return -1;
}

int pcm_scanl(pcm_io_context_t *io, const char *fmt, ...)
{
    int c, count = 0;
    va_list ap;

    va_start(ap, fmt);
    while ((c = *fmt++)) {
        switch (c) {
        case 'S':
            TRY_IO(pcm_read16le(io, va_arg(ap, uint16_t*)));
            ++count;
            break;
        case 'L':
            TRY_IO(pcm_read32le(io, va_arg(ap, uint32_t*)));
            ++count;
            break;
        case 'Q':
            TRY_IO(pcm_read64le(io, va_arg(ap, uint64_t*)));
            ++count;
            break;
        }
    }
FAIL:
    va_end(ap);
    return count;
}

int pcm_scanb(pcm_io_context_t *io, const char *fmt, ...)
{
    int c, count = 0;
    va_list ap;

    va_start(ap, fmt);
    while ((c = *fmt++)) {
        switch (c) {
        case 'S':
            TRY_IO(pcm_read16be(io, va_arg(ap, uint16_t*)));
            ++count;
            break;
        case 'L':
            TRY_IO(pcm_read32be(io, va_arg(ap, uint32_t*)));
            ++count;
            break;
        case 'Q':
            TRY_IO(pcm_read64be(io, va_arg(ap, uint64_t*)));
            ++count;
            break;
        }
    }
FAIL:
    va_end(ap);
    return count;
}
