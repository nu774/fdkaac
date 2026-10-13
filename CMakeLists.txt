cmake_minimum_required(VERSION 3.10)

# fdkaac_version is a plain C string literal in version.h ("X.Y.Z");
# reuse it as the CMake project version too.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.h" FDKAAC_VERSION_LINE
     REGEX "fdkaac_version[ \t]*=")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
       FDKAAC_VERSION "${FDKAAC_VERSION_LINE}")

project(fdkaac VERSION ${FDKAAC_VERSION} LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED OFF)

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckTypeSize)
include(TestBigEndian)

# ---------------------------------------------------------------------------
# libfdk-aac: either the system package (default, via pkg-config) or built
# from source as a static library.
# ---------------------------------------------------------------------------
option(FDKAAC_BUILD_FDK_AAC
    "Fetch and build libfdk-aac from source as a static library instead of using the system package found via pkg-config"
    OFF)

if(FDKAAC_BUILD_FDK_AAC)
    # fdk-aac's own sources are C++; a C-only executable statically linking
    # it still needs the C++ runtime pulled in at final-link time, so we
    # need a C++ compiler available and must link fdkaac as C++.
    enable_language(CXX)
    include(FetchContent)

    set(FDKAAC_FDK_AAC_REPOSITORY "https://github.com/mstorsjo/fdk-aac"
        CACHE STRING "Git repository libfdk-aac is fetched from when FDKAAC_BUILD_FDK_AAC is ON")
    set(FDKAAC_FDK_AAC_VERSION "v2.0.3"
        CACHE STRING "Version of libfdk-aac to build when FDKAAC_BUILD_FDK_AAC is ON")

    # Pinning these before add_subdirectory() processes fdk-aac's own
    # CMakeLists.txt makes its own option() calls for the same names see
    # them already cached and keep our values.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(FDK_AAC_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(FDK_AAC_INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(fdk-aac-external
        URL "${FDKAAC_FDK_AAC_REPOSITORY}/archive/refs/tags/${FDKAAC_FDK_AAC_VERSION}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(fdk-aac-external)
    if(NOT fdk-aac-external_POPULATED)
        if(POLICY CMP0169)
            # Silence the deprecation warning for the single-argument
            # FetchContent_Populate() form below on CMake >= 3.30; we still
            # need that form (rather than FetchContent_MakeAvailable) so we
            # can add the subdirectory ourselves with EXCLUDE_FROM_ALL.
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(fdk-aac-external)
    endif()
    # EXCLUDE_FROM_ALL keeps fdk-aac's own install() rules (which
    # unconditionally install its static library and public headers) from
    # leaking into `cmake --install` of this project: fdkaac's own
    # install() below stays the only thing installed, matching the previous
    # ExternalProject_Add behavior where fdk-aac was built into an isolated,
    # never-installed prefix.
    add_subdirectory("${fdk-aac-external_SOURCE_DIR}" "${fdk-aac-external_BINARY_DIR}" EXCLUDE_FROM_ALL)
    set_target_properties(fdk-aac PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # fdkaac's sources #include <fdk-aac/....h>, matching the layout fdk-aac
    # installs to (include/fdk-aac/*.h) and what pkg-config's fdk-aac.pc
    # advertises. Its own build-tree include dirs (exposed via the "fdk-aac"
    # target's INTERFACE_INCLUDE_DIRECTORIES) are the bare per-component
    # source dirs instead (e.g. libAACenc/include/aacenc_lib.h with no
    # fdk-aac/ prefix) since that nesting is normally only produced by `make
    # install`, which we never run here. Recreate just that one directory
    # layer by copying the public headers into a fdk-aac/ subdirectory once
    # at configure time.
    set(FDK_AAC_HEADER_SHIM_DIR "${CMAKE_BINARY_DIR}/fdk-aac-include-shim")
    file(GLOB FDK_AAC_PUBLIC_HEADERS
        "${fdk-aac-external_SOURCE_DIR}/libAACdec/include/*.h"
        "${fdk-aac-external_SOURCE_DIR}/libAACenc/include/*.h"
        "${fdk-aac-external_SOURCE_DIR}/libSYS/include/*.h"
    )
    file(COPY ${FDK_AAC_PUBLIC_HEADERS} DESTINATION "${FDK_AAC_HEADER_SHIM_DIR}/fdk-aac")

    set(FDK_AAC_LINK_TARGET fdk-aac)
    set(FDK_AAC_INCLUDE_DIR "${FDK_AAC_HEADER_SHIM_DIR}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FDK_AAC REQUIRED IMPORTED_TARGET fdk-aac)
    set(FDK_AAC_LINK_TARGET PkgConfig::FDK_AAC)
endif()

# ---------------------------------------------------------------------------
# platform / feature checks (mirrors configure.ac)
# ---------------------------------------------------------------------------
test_big_endian(WORDS_BIGENDIAN)

check_include_file("stdint.h"    HAVE_STDINT_H)
check_include_file("inttypes.h"  HAVE_INTTYPES_H)
check_include_file("unistd.h"    HAVE_UNISTD_H)
check_include_file("endian.h"    HAVE_ENDIAN_H)
check_include_file("byteswap.h"  HAVE_BYTESWAP_H)
check_include_file("libcharset.h" HAVE_LIBCHARSET_H)
check_include_file("langinfo.h"  HAVE_LANGINFO_H)

check_symbol_exists(sigaction   "signal.h" HAVE_SIGACTION)
check_symbol_exists(getopt_long "getopt.h" HAVE_GETOPT_LONG)
check_function_exists(fseeko    HAVE_FSEEKO)
check_function_exists(fseeko64  HAVE_FSEEKO64)
check_symbol_exists(_vscprintf  "stdio.h"   HAVE__VSCPRINTF)
set(CMAKE_EXTRA_INCLUDE_FILES "sys/timeb.h")
check_type_size("struct __timeb64" STRUCT___TIMEB64)
unset(CMAKE_EXTRA_INCLUDE_FILES)

set(ICONV_LIBRARIES)
if(NOT WIN32)
    find_package(Iconv)
    if(Iconv_FOUND)
        set(HAVE_ICONV TRUE)
        set(ICONV_LIBRARIES ${Iconv_LIBRARIES})
    endif()
    if(HAVE_LIBCHARSET_H)
        find_library(CHARSET_LIBRARY NAMES charset iconv)
        if(CHARSET_LIBRARY)
            list(APPEND ICONV_LIBRARIES ${CHARSET_LIBRARY})
        endif()
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

# ---------------------------------------------------------------------------
# sources
# ---------------------------------------------------------------------------
set(FDKAAC_SOURCES
    src/aacenc.c
    src/caf_reader.c
    src/extrapolater.c
    src/limiter.c
    src/lpc.c
    src/m4af.c
    src/main.c
    src/metadata.c
    src/parson.c
    src/pcm_float_converter.c
    src/pcm_native_converter.c
    src/pcm_readhelper.c
    src/pcm_sint16_converter.c
    src/progress.c
    src/wav_reader.c
)

if(WIN32)
    list(APPEND FDKAAC_SOURCES src/compat_win32.c fdkaac.manifest)
    if (MINGW)
        list(APPEND FDKAAC_SOURCES fdkaac.rc)
    endif()
else()
    list(APPEND FDKAAC_SOURCES src/compat_posix.c)
endif()

if(NOT HAVE_GETOPT_LONG)
    list(APPEND FDKAAC_SOURCES missings/getopt.c)
endif()

add_executable(fdkaac ${FDKAAC_SOURCES})

target_include_directories(fdkaac PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
if(NOT HAVE_GETOPT_LONG)
    target_include_directories(fdkaac PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/missings")
endif()
if(FDKAAC_BUILD_FDK_AAC)
    target_include_directories(fdkaac PRIVATE "${FDK_AAC_INCLUDE_DIR}")
endif()

target_compile_definitions(fdkaac PRIVATE HAVE_CONFIG_H)
if(NOT WIN32)
    target_compile_definitions(fdkaac PRIVATE _FILE_OFFSET_BITS=64)
endif()
if(WIN32)
    target_compile_definitions(fdkaac PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    if(MSVC)
        target_compile_definitions(fdkaac PRIVATE inline=_inline)
    endif()
endif()

target_link_libraries(fdkaac PRIVATE ${FDK_AAC_LINK_TARGET} ${ICONV_LIBRARIES})
if(NOT WIN32)
    target_link_libraries(fdkaac PRIVATE m)
endif()

if(FDKAAC_BUILD_FDK_AAC)
    # fdk-aac's sources are C++; drive the final link with the C++ compiler
    # so its runtime (libstdc++/libc++) gets linked in automatically instead
    # of leaving unresolved C++ runtime symbols in a plain C link.
    set_target_properties(fdkaac PROPERTIES LINKER_LANGUAGE CXX)
endif()

install(TARGETS fdkaac RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/fdkaac.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
