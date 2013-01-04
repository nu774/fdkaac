ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = COPYING

bin_PROGRAMS = fdkaac

fdkaac_SOURCES = \
	src/aacenc.c     \
	src/lpcm.c       \
	src/m4af.c       \
	src/main.c       \
	src/progress.c   \
	src/wav_reader.c

fdkaac_LDADD = \
	@LIBICONV@ -lm

if FDK_PLATFORM_POSIX
    fdkaac_SOURCES += \
	src/compat_posix.c
endif

if FDK_PLATFORM_WIN32
    fdkaac_SOURCES += \
	src/compat_win32.c
endif
