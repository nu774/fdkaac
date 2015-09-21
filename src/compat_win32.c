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
#include <assert.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/timeb.h>
#include "compat.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

int64_t aacenc_timer(void)
{
#if HAVE_STRUCT___TIMEB64
    struct __timeb64 tv;
    _ftime64(&tv);
#else
    struct timeb tv;
    ftime(&tv);
#endif
    return (int64_t)tv.time * 1000 + tv.millitm;
}

int aacenc_seekable(FILE *fp)
{
    return GetFileType((HANDLE)_get_osfhandle(_fileno(fp))) == FILE_TYPE_DISK;
}

static
int codepage_decode_wchar(int codepage, const char *from, wchar_t **to)
{
    int nc = MultiByteToWideChar(codepage, 0, from, -1, 0, 0);
    if (nc == 0)
        return -1;
    *to = malloc(nc * sizeof(wchar_t));
    MultiByteToWideChar(codepage, 0, from, -1, *to, nc);
    return 0;
}

static
int codepage_encode_wchar(int codepage, const wchar_t *from, char **to)
{
    int nc = WideCharToMultiByte(codepage, 0, from, -1, 0, 0, 0, 0);
    if (nc == 0)
        return -1;
    *to = malloc(nc);
    WideCharToMultiByte(codepage, 0, from, -1, *to, nc, 0, 0);
    return 0;
}

FILE *aacenc_fopen(const char *name, const char *mode)
{
    wchar_t *wname, *wmode;
    FILE *fp;

    if (strcmp(name, "-") == 0) {
        fp = (mode[0] == 'r') ? stdin : stdout;
        _setmode(_fileno(fp), _O_BINARY);
    } else {
        int share = _SH_DENYRW;
        if (strchr(mode, 'r') && !strchr(mode, '+'))
            share = _SH_DENYWR;
        codepage_decode_wchar(CP_UTF8, name, &wname);
        codepage_decode_wchar(CP_UTF8, mode, &wmode);
        fp = _wfsopen(wname, wmode, share);
        free(wname);
        free(wmode);
    }
    return fp;
}

static char **__aacenc_argv__;

static
void aacenc_free_mainargs(void)
{
    char **p = __aacenc_argv__;
    for (; *p; ++p)
        free(*p);
    free(__aacenc_argv__);
}

void aacenc_getmainargs(int *argc, char ***argv)
{
    int i;
    wchar_t **wargv;

    wargv = CommandLineToArgvW(GetCommandLineW(), argc);
    *argv = malloc((*argc + 1) * sizeof(char*));
    for (i = 0; i < *argc; ++i)
        codepage_encode_wchar(CP_UTF8, wargv[i], &(*argv)[i]);
        LocalFree(wargv);
    (*argv)[*argc] = 0;
    __aacenc_argv__ = *argv;
    atexit(aacenc_free_mainargs);
}

char *aacenc_to_utf8(const char *s)
{
    return _strdup(s);
}

#if defined(__MINGW32__) && !defined(HAVE__VSCPRINTF)
int _vscprintf(const char *fmt, va_list ap) 
{
    static int (*fp_vscprintf)(const char *, va_list) = 0;
    if (!fp_vscprintf) {
        HANDLE h = GetModuleHandleA("msvcrt.dll");
        FARPROC fp = GetProcAddress(h, "_vscprintf");
        InterlockedCompareExchangePointer(&fp_vscprintf, fp, 0);
    }
    assert(fp_vscprintf);
    return fp_vscprintf(fmt, ap);
}
#endif

int aacenc_fprintf(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    int cnt;
    HANDLE fh = (HANDLE)_get_osfhandle(_fileno(fp));

    if (GetFileType(fh) != FILE_TYPE_CHAR) {
        va_start(ap, fmt);
        cnt = vfprintf(fp, fmt, ap);
        va_end(ap);
    } else {
        char *s;
        wchar_t *ws;
        DWORD nw;

        va_start(ap, fmt);
        cnt = _vscprintf(fmt, ap);
        va_end(ap);

        s = malloc(cnt + 1);
        
        va_start(ap, fmt);
        cnt = _vsnprintf(s, cnt + 1, fmt, ap);
        va_end(ap);

        codepage_decode_wchar(CP_UTF8, s, &ws);
        free(s);
        fflush(fp);
        WriteConsoleW(fh, ws, wcslen(ws), &nw, 0);
        free(ws);
    }
    return cnt;
}

const char *aacenc_basename(const char *path)
{
/*
 * Since path is encoded with UTF-8, naive usage of strrchr() shoule be safe.
 */
    const char *p = strrchr(path, '/');
    const char *q = strrchr(path, '\\');
    const char *r = strrchr(path, ':');
    if (q > p) p = q;
    if (r > p) p = r;
    return p ? p + 1 : path;
}
