ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = fdkaac

fdkaac_SOURCES = \
    src/aacenc.c     \
    src/lpcm.c       \
    src/m4af.c       \
    src/main.c       \
    src/progress.c   \
    src/wav_reader.c

fdkaac_LDADD = \
    @LIBICONV@ -lfdk-aac -lm

if FDK_PLATFORM_POSIX
    fdkaac_SOURCES += \
	src/compat_posix.c
endif

if FDK_PLATFORM_WIN32
    fdkaac_SOURCES += \
	src/compat_win32.c
endif

if FDK_NO_GETOPT_LONG
    fdkaac_SOURCES += \
	missings/getopt.c
    AM_CPPFLAGS = -Imissings
endif

EXTRA_DIST = \
    m4/.gitkeep  \
    src/*.h      \
    missings/*.c \
    missings/*.h \
    MSVC/*.vcxproj \
    MSVC/*.vcxproj.filters \
    MSVC/*.sln
