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
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include "compat.h"

int64_t aacenc_timer(void)
{
    struct timeval tv = { 0 };
    gettimeofday(&tv, 0);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

FILE *aacenc_fopen(const char *name, const char *mode)
{
    FILE *fp;
    if (strcmp(name, "-") == 0)
        fp = (mode[0] == 'r') ? stdin : stdout;
    else
        fp = fopen(name, mode);
    return fp;
}

int aacenc_seekable(FILE *fp)
{
    return fseek(fp, 0, SEEK_CUR) == 0;
}

/*
 * Different from POSIX basename() when path ends with /.
 * Since we use this only for a regular file, the difference doesn't matter.
 */
const char *aacenc_basename(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1: path;
}

#ifndef HAVE_ICONV
char *aacenc_to_utf8(const char *s)
{
    return strdup(s);
}
#else /* HAVE_ICONV */

#include <sys/types.h>
#include <stddef.h>
#include <errno.h>
#include <iconv.h>

#if HAVE_LIBCHARSET_H
#include <libcharset.h>
#elif HAVE_LANGINFO_H
#include <langinfo.h>
static const char *locale_charset(void)
{
    return nl_langinfo(CODESET);
}
#else
static const char *locale_charset(void)
{
    return 0;
}
#endif

static
int utf8_from_charset(const char *charset, const char *from, char **to)
{
    iconv_t cd;
    size_t fromlen, obsize, ibleft, obleft;
    char *ip, *op;

    cd = iconv_open("UTF-8", charset);
    if (cd == (iconv_t)-1)
        return -1;

    fromlen = strlen(from);
    ibleft  = fromlen;
    obsize  = 2;
    obleft  = obsize - 1;
    *to     = malloc(obsize);
    ip      = (char *)from;
    op      = *to;

    while (ibleft > 0) {
        if (iconv(cd, &ip, &ibleft, &op, &obleft) != (size_t)-1)
            break;
        else {
            if (errno == E2BIG || obleft == 0) {
                ptrdiff_t offset = op - *to;
                obsize *= 2;
                *to = realloc(*to, obsize);
                op = *to + offset;
                obleft = obsize - offset - 1;
            }
            if (errno == EILSEQ) {
                ++ip;
                --ibleft;
                *op++ = '?';
                --obleft;
            }
            if (errno != E2BIG && errno != EILSEQ)
                break;
        }
    }
    iconv_close(cd);
    *op = 0;
    if (fromlen > 0 && op == *to) {
        free(op);
        return -1;
    }
    return 0;
}

char *aacenc_to_utf8(const char *s)
{
    char *result;
    const char *charset;
    
    if ((charset = locale_charset()) == 0)
        charset = "US-ASCII";
    if (utf8_from_charset(charset, s, &result) < 0)
        result = strdup(s);
    return result;
}
#endif /* HAVE_ICONV */
