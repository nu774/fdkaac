/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef COMPAT_H
#define COMPAT_H

#ifndef HAVE_FSEEKO
#  if _MSC_VER >= 1400
#    define fseeko _fseeki64
#    define ftello _ftelli64
#  else
#    define fseeko fseek
#    define ftello ftell
#  endif
#endif

int64_t aacenc_timer(void);
FILE *aacenc_fopen(const char *name, const char *mode);
void aacenc_getmainargs(int *argc, char ***argv);
char *aacenc_to_utf8(const char *s);
int aacenc_fprintf(FILE *fp, const char *fmt, ...);

#endif
