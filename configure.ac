m4_define([VERSION_H],m4_esyscmd([cat version.h]))
changequote({{,}})dnl
m4_define({{XX_VERSION}},m4_bregexp(VERSION_H,{{^const.*"\(.+\)";}},{{\1}}))
changequote([,])dnl

AC_INIT([fdkaac], [XX_VERSION], [honeycomb77@gmail.com])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE

AC_PROG_CC
AM_PROG_CC_C_O

AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([libcharset.h langinfo.h endian.h byteswap.h])
AC_CHECK_HEADERS([fdk-aac/aacenc_lib.h], ,
                 AC_MSG_ERROR([libfdk-aac is required]))

AC_C_INLINE
AC_C_BIGENDIAN
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_TYPES([ptrdiff_t])

AC_SYS_LARGEFILE
AC_CHECK_TYPES([struct __timeb64],[],[],[[#include <sys/timeb.h>]])
AC_FUNC_FSEEKO
AC_CHECK_FUNCS([sigaction gettimeofday nl_langinfo _vscprintf fseeko64])
AC_CHECK_FUNC(getopt_long)
AM_CONDITIONAL([FDK_NO_GETOPT_LONG],[test "$ac_cv_func_getopt_long" != "yes"])
AC_SEARCH_LIBS([aacEncOpen],[fdk-aac],[],[],[])

CHARSET_LIB=
AC_CHECK_LIB([iconv], [locale_charset],
       [CHARSET_LIB=-liconv],
       [AC_CHECK_LIB([charset], [locale_charset], [CHARSET_LIB=-lcharset])])
AC_SUBST([CHARSET_LIB])

AC_CANONICAL_HOST

X_PLATFORM=posix
case ${host} in
*-*-mingw*)
    X_PLATFORM=win32
    ;;
*)
    AM_ICONV
esac
AM_CONDITIONAL([FDK_PLATFORM_POSIX],[test "$X_PLATFORM" = "posix"])
AM_CONDITIONAL([FDK_PLATFORM_WIN32],[test "$X_PLATFORM" = "win32"])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
