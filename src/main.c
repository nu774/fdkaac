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
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#elif defined(_MSC_VER)
#  define SCNd64 "I64d"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <sys/stat.h>
#include <getopt.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SIGACTION
#include <signal.h>
#endif
#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "compat.h"
#include "pcm_reader.h"
#include "aacenc.h"
#include "m4af.h"
#include "progress.h"
#include "version.h"
#include "metadata.h"

#define PROGNAME "fdkaac"

static volatile int g_interrupted = 0;

#if HAVE_SIGACTION
static void signal_handler(int signum)
{
    g_interrupted = 1;
}
static void handle_signals(void)
{
    int i, sigs[] = { SIGINT, SIGHUP, SIGTERM };
    for (i = 0; i < sizeof(sigs)/sizeof(sigs[0]); ++i) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = signal_handler;
        sa.sa_flags |= SA_RESTART;
        sigaction(sigs[i], &sa, 0);
    }
}
#elif defined(_WIN32)
static BOOL WINAPI signal_handler(DWORD type)
{
    g_interrupted = 1;
    return TRUE;
}

static void handle_signals(void)
{
    SetConsoleCtrlHandler(signal_handler, TRUE);
}
#else
static void handle_signals(void)
{
}
#endif

static
int read_callback(void *cookie, void *data, uint32_t size)
{
    size_t rc = fread(data, 1, size, (FILE*)cookie);
    return ferror((FILE*)cookie) ? -1 : (int)rc;
}

static
int write_callback(void *cookie, const void *data, uint32_t size)
{
    size_t rc = fwrite(data, 1, size, (FILE*)cookie);
    return ferror((FILE*)cookie) ? -1 : (int)rc;
}

static
int seek_callback(void *cookie, int64_t off, int whence)
{
    return fseeko((FILE*)cookie, off, whence);
}

static
int64_t tell_callback(void *cookie)
{
    return ftello((FILE*)cookie);
}

static
void usage(void)
{
    printf(
PROGNAME " %s\n"
"Usage: " PROGNAME " [options] input_file\n"
"Options:\n"
" -h, --help                    Print this help message\n"
" -p, --profile <n>             Profile (audio object type)\n"
"                                 2: MPEG-4 AAC LC (default)\n"
"                                 5: MPEG-4 HE-AAC (SBR)\n"
"                                29: MPEG-4 HE-AAC v2 (SBR+PS)\n"
"                                23: MPEG-4 AAC LD\n"
"                                39: MPEG-4 AAC ELD\n"
" -b, --bitrate <n>             Bitrate in bits per seconds (for CBR)\n"
" -m, --bitrate-mode <n>        Bitrate configuration\n"
"                                 0: CBR (default)\n"
"                                 1-5: VBR\n"
"                               (VBR mode is not officially supported, and\n"
"                                works only on a certain combination of\n"
"                                parameter settings, sample rate, and\n"
"                                channel configuration)\n"
" -w, --bandwidth <n>           Frequency bandwidth in Hz (AAC LC only)\n"
" -a, --afterburner <n>         Afterburner\n"
"                                 0: Off\n"
"                                 1: On(default)\n"
" -L, --lowdelay-sbr <-1|0|1>   Configure SBR activity on AAC ELD\n"
"                                -1: Use ELD SBR auto configurator\n"
"                                 0: Disable SBR on ELD (default)\n"
"                                 1: Enable SBR on ELD\n"
" -s, --sbr-ratio <0|1|2>       Controls activation of downsampled SBR\n"
"                                 0: Use lib default (default)\n"
"                                 1: downsampled SBR (default for ELD+SBR)\n"
"                                 2: dual-rate SBR (default for HE-AAC)\n"
" -f, --transport-format <n>    Transport format\n"
"                                 0: RAW (default, muxed into M4A)\n"
"                                 1: ADIF\n"
"                                 2: ADTS\n"
"                                 6: LATM MCP=1\n"
"                                 7: LATM MCP=0\n"
"                                10: LOAS/LATM (LATM within LOAS)\n"
" -C, --adts-crc-check          Add CRC protection on ADTS header\n"
" -h, --header-period <n>       StreamMuxConfig/PCE repetition period in\n"
"                               transport layer\n"
"\n"
" -o <filename>                 Output filename\n"
" -G, --gapless-mode <n>        Encoder delay signaling for gapless playback\n"
"                                 0: iTunSMPB (default)\n"
"                                 1: ISO standard (edts + sgpd)\n"
"                                 2: Both\n"
" --include-sbr-delay           Count SBR decoder delay in encoder delay\n"
"                               This is not iTunes compatible, but is default\n"
"                               behavior of FDK library.\n"
" -I, --ignorelength            Ignore length of WAV header\n"
" -S, --silent                  Don't print progress messages\n"
" --moov-before-mdat            Place moov box before mdat box on m4a output\n"
" --no-timestamp                Don't inject timestamp in the file\n"
"\n"
"Options for raw (headerless) input:\n"
" -R, --raw                     Treat input as raw (by default WAV is\n"
"                               assumed)\n"
" --raw-channels <n>            Number of channels (default: 2)\n"
" --raw-rate     <n>            Sample rate (default: 44100)\n"
" --raw-format   <spec>         Sample format, default is \"S16L\".\n"
"                               Spec is as follows:\n"
"                                1st char: S(igned)|U(nsigned)|F(loat)\n"
"                                2nd part: bits per channel\n"
"                                Last char: L(ittle)|B(ig)\n"
"                               Last char can be omitted, in which case L is\n"
"                               assumed. Spec is case insensitive, therefore\n"
"                               \"u16b\" is same as \"U16B\".\n"
"\n"
"Tagging options:\n"
" --title <string>\n"
" --artist <string>\n"
" --album <string>\n"
" --genre <string>\n"
" --date <string>\n"
" --composer <string>\n"
" --grouping <string>\n"
" --comment <string>\n"
" --album-artist <string>\n"
" --track <number[/total]>\n"
" --disk <number[/total]>\n"
" --tempo <n>\n"
" --tag <fcc>:<value>          Set iTunes predefined tag with four char code.\n"
" --tag-from-file <fcc>:<filename>\n"
"                              Same as above, but value is read from file.\n"
" --long-tag <name>:<value>    Set arbitrary tag as iTunes custom metadata.\n"
" --tag-from-json <filename[?dot_notation]>\n"
"                              Read tags from JSON. By default, tags are\n"
"                              assumed to be direct children of the root\n"
"                              object(dictionary).\n"
"                              Optionally, position of the dictionary\n"
"                              that contains tags can be specified with\n"
"                              dotted notation.\n"
"                              Example:\n"
"                                --tag-from-json /path/to/json?format.tags\n"
    , fdkaac_version);
}

typedef struct aacenc_param_ex_t {
    AACENC_PARAMS

    char *input_filename;
    FILE *input_fp;
    char *output_filename;
    FILE *output_fp;
    unsigned gapless_mode;
    unsigned include_sbr_delay;
    unsigned ignore_length;
    int silent;
    int moov_before_mdat;

    int is_raw;
    unsigned raw_channels;
    unsigned raw_rate;
    const char *raw_format;

    int no_timestamp;

    aacenc_tag_store_t tags;
    aacenc_tag_store_t source_tags;
    aacenc_translate_generic_text_tag_ctx_t source_tag_ctx;

    char *json_filename;
} aacenc_param_ex_t;

static
int parse_options(int argc, char **argv, aacenc_param_ex_t *params)
{
    int ch;
    int n;

#define OPT_INCLUDE_SBR_DELAY    M4AF_FOURCC('s','d','l','y')
#define OPT_MOOV_BEFORE_MDAT     M4AF_FOURCC('m','o','o','v')
#define OPT_RAW_CHANNELS         M4AF_FOURCC('r','c','h','n')
#define OPT_RAW_RATE             M4AF_FOURCC('r','r','a','t')
#define OPT_RAW_FORMAT           M4AF_FOURCC('r','f','m','t')
#define OPT_SHORT_TAG            M4AF_FOURCC('s','t','a','g')
#define OPT_SHORT_TAG_FILE       M4AF_FOURCC('s','t','g','f')
#define OPT_LONG_TAG             M4AF_FOURCC('l','t','a','g')
#define OPT_TAG_FROM_JSON        M4AF_FOURCC('t','f','j','s')

    static const struct option long_options[] = {
        { "help",             no_argument,       0, 'h' },
        { "profile",          required_argument, 0, 'p' },
        { "bitrate",          required_argument, 0, 'b' },
        { "bitrate-mode",     required_argument, 0, 'm' },
        { "bandwidth",        required_argument, 0, 'w' },
        { "afterburner",      required_argument, 0, 'a' },
        { "lowdelay-sbr",     required_argument, 0, 'L' },
        { "sbr-ratio",        required_argument, 0, 's' },
        { "transport-format", required_argument, 0, 'f' },
        { "adts-crc-check",   no_argument,       0, 'C' },
        { "header-period",    required_argument, 0, 'P' },

        { "gapless-mode",     required_argument, 0, 'G' },
        { "include-sbr-delay", no_argument,      0, OPT_INCLUDE_SBR_DELAY  },
        { "ignorelength",     no_argument,       0, 'I' },
        { "silent",           no_argument,       0, 'S' },
        { "moov-before-mdat", no_argument,       0, OPT_MOOV_BEFORE_MDAT   },

        { "raw",              no_argument,       0, 'R' },
        { "raw-channels",     required_argument, 0, OPT_RAW_CHANNELS       },
        { "raw-rate",         required_argument, 0, OPT_RAW_RATE           },
        { "raw-format",       required_argument, 0, OPT_RAW_FORMAT         },

        { "title",            required_argument, 0, M4AF_TAG_TITLE         },
        { "artist",           required_argument, 0, M4AF_TAG_ARTIST        },
        { "album",            required_argument, 0, M4AF_TAG_ALBUM         },
        { "genre",            required_argument, 0, M4AF_TAG_GENRE         },
        { "date",             required_argument, 0, M4AF_TAG_DATE          },
        { "composer",         required_argument, 0, M4AF_TAG_COMPOSER      },
        { "grouping",         required_argument, 0, M4AF_TAG_GROUPING      },
        { "comment",          required_argument, 0, M4AF_TAG_COMMENT       },
        { "album-artist",     required_argument, 0, M4AF_TAG_ALBUM_ARTIST  },
        { "track",            required_argument, 0, M4AF_TAG_TRACK         },
        { "disk",             required_argument, 0, M4AF_TAG_DISK          },
        { "tempo",            required_argument, 0, M4AF_TAG_TEMPO         },
        { "tag",              required_argument, 0, OPT_SHORT_TAG          },
        { "tag-from-file",    required_argument, 0, OPT_SHORT_TAG_FILE     },
        { "long-tag",         required_argument, 0, OPT_LONG_TAG           },
        { "tag-from-json",    required_argument, 0, OPT_TAG_FROM_JSON      },

        { "no-timestamp",     no_argument,       0, '#' },
        { 0,                  0,                 0, 0                      },
    };
    params->afterburner = 1;

    aacenc_getmainargs(&argc, &argv);
    while ((ch = getopt_long(argc, argv, "hp:b:m:w:a:L:s:f:CP:G:Io:SR",
                             long_options, 0)) != EOF) {
        switch (ch) {
        case 'h':
            return usage(), -1;
        case 'p':
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for profile\n");
                return -1;
            }
            params->profile = n;
            break;
        case 'b':
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for bitrate\n");
                return -1;
            }
            params->bitrate = n;
            break;
        case 'm':
            if (sscanf(optarg, "%u", &n) != 1 || n > 5) {
                fprintf(stderr, "invalid arg for bitrate-mode\n");
                return -1;
            }
            params->bitrate_mode = n;
            break;
        case 'w':
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for bandwidth\n");
                return -1;
            }
            params->bandwidth = n;
            break;
        case 'a':
            if (sscanf(optarg, "%u", &n) != 1 || n > 1) {
                fprintf(stderr, "invalid arg for afterburner\n");
                return -1;
            }
            params->afterburner = n;
            break;
        case 'L':
            if (sscanf(optarg, "%d", &n) != 1 || n < -1 || n > 1) {
                fprintf(stderr, "invalid arg for lowdelay-sbr\n");
                return -1;
            }
            params->lowdelay_sbr = n;
            break;
        case 's':
            if (sscanf(optarg, "%u", &n) != 1 || n > 2) {
                fprintf(stderr, "invalid arg for sbr-ratio\n");
                return -1;
            }
            params->sbr_ratio = n;
            break;
        case 'f':
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for transport-format\n");
                return -1;
            }
            params->transport_format = n;
            break;
        case 'C':
            params->adts_crc_check = 1;
            break;
        case 'P':
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for header-period\n");
                return -1;
            }
            params->header_period = n;
            break;
        case 'o':
            params->output_filename = optarg;
            break;
        case 'G':
            if (sscanf(optarg, "%u", &n) != 1 || n > 2) {
                fprintf(stderr, "invalid arg for gapless-mode\n");
                return -1;
            }
            params->gapless_mode = n;
            break;
        case OPT_INCLUDE_SBR_DELAY:
            params->include_sbr_delay = 1;
            break;
        case 'I':
            params->ignore_length = 1;
            break;
        case 'S':
            params->silent = 1;
            break;
        case OPT_MOOV_BEFORE_MDAT:
            params->moov_before_mdat = 1;
            break;
        case 'R':
            params->is_raw = 1;
            break;
        case OPT_RAW_CHANNELS:
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for raw-channels\n");
                return -1;
            }
            params->raw_channels = n;
            break;
        case OPT_RAW_RATE:
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for raw-rate\n");
                return -1;
            }
            params->raw_rate = n;
            break;
        case OPT_RAW_FORMAT:
            params->raw_format = optarg;
            break;
        case M4AF_TAG_TITLE:
        case M4AF_TAG_ARTIST:
        case M4AF_TAG_ALBUM:
        case M4AF_TAG_GENRE:
        case M4AF_TAG_DATE:
        case M4AF_TAG_COMPOSER:
        case M4AF_TAG_GROUPING:
        case M4AF_TAG_COMMENT:
        case M4AF_TAG_ALBUM_ARTIST:
        case M4AF_TAG_TRACK:
        case M4AF_TAG_DISK:
        case M4AF_TAG_TEMPO:
            aacenc_add_tag_to_store(&params->tags, ch, 0, optarg,
                                    strlen(optarg), 0);
            break;
        case OPT_SHORT_TAG:
        case OPT_SHORT_TAG_FILE:
        case OPT_LONG_TAG:
            {
                char *val;
                size_t klen;
                unsigned fcc = M4AF_FOURCC('-','-','-','-');

                if ((val = strchr(optarg, ':')) == 0) {
                    fprintf(stderr, "invalid arg for tag\n");
                    return -1;
                }
                *val++ = '\0';
                if (ch == OPT_SHORT_TAG || ch == OPT_SHORT_TAG_FILE) {
                    /*
                     * take care of U+00A9(COPYRIGHT SIGN).
                     * 1) if length of fcc is 3, we prepend '\xa9'.
                     * 2) U+00A9 becomes "\xc2\xa9" in UTF-8. Therefore
                     *    we remove first '\xc2'.
                     */
                    if (optarg[0] == '\xc2')
                        ++optarg;
                    if ((klen = strlen(optarg))== 3)
                        fcc = 0xa9;
                    else if (klen != 4) {
                        fprintf(stderr, "invalid arg for tag\n");
                        return -1;
                    }
                    for (; *optarg; ++optarg)
                        fcc = ((fcc << 8) | (*optarg & 0xff));
                }
                aacenc_add_tag_to_store(&params->tags, fcc, optarg,
                                        val, strlen(val),
                                        ch == OPT_SHORT_TAG_FILE);
            }
            break;
        case OPT_TAG_FROM_JSON:
            params->json_filename = optarg;
            break;
        case '#':
            params->no_timestamp = 1;
            break;
        default:
            return usage(), -1;
        }
    }
    if (argc == optind)
        return usage(), -1;

    if (!params->bitrate && !params->bitrate_mode) {
        fprintf(stderr, "bitrate or bitrate-mode is mandatory\n");
        return -1;
    }
    if (params->output_filename && !strcmp(params->output_filename, "-") &&
        !params->transport_format) {
        fprintf(stderr, "stdout streaming is not available on M4A output\n");
        return -1;
    }
    if (params->bitrate && params->bitrate < 10000)
        params->bitrate *= 1000;

    if (params->is_raw) {
        if (!params->raw_channels)
            params->raw_channels = 2;
        if (!params->raw_rate)
            params->raw_rate = 44100;
        if (!params->raw_format)
            params->raw_format = "S16L";
    }
    params->input_filename = argv[optind];
    return 0;
};

static
int write_sample(FILE *ofp, m4af_ctx_t *m4af, aacenc_frame_t *frame)
{
    if (!m4af) {
        fwrite(frame->data, 1, frame->size, ofp);
        if (ferror(ofp)) {
            fprintf(stderr, "ERROR: fwrite(): %s\n", strerror(errno));
            return -1;
        }
    } else if (m4af_write_sample(m4af, 0, frame->data, frame->size, 0) < 0) {
        fprintf(stderr, "ERROR: failed to write m4a sample\n");
        return -1;
    }
    return 0;
}

static int do_smart_padding(int profile)
{
    return profile == 2 || profile == 5 || profile == 29;
}

static
int encode(aacenc_param_ex_t *params, pcm_reader_t *reader,
           HANDLE_AACENCODER encoder, uint32_t frame_length, 
           m4af_ctx_t *m4af)
{
    int16_t *ibuf = 0, *ip;
    aacenc_frame_t obuf[2] = {{ 0 }}, *obp;
    unsigned flip = 0;
    int nread = 1;
    int rc = -1;
    int remaining, consumed;
    int frames_written = 0, encoded = 0;
    aacenc_progress_t progress = { 0 };
    const pcm_sample_description_t *fmt = pcm_get_format(reader);
    const int is_padding = do_smart_padding(params->profile);

    ibuf = malloc(frame_length * fmt->bytes_per_frame);
    aacenc_progress_init(&progress, pcm_get_length(reader), fmt->sample_rate);

    for (;;) {
        if (g_interrupted)
            nread = 0;
        if (nread > 0) {
            if ((nread = pcm_read_frames(reader, ibuf, frame_length)) < 0) {
                fprintf(stderr, "ERROR: read failed\n");
                goto END;
            }
            if (!params->silent)
                aacenc_progress_update(&progress, pcm_get_position(reader),
                                       fmt->sample_rate * 2);
        }
        ip = ibuf;
        remaining = nread;
        do {
            obp = &obuf[flip];
            consumed = aac_encode_frame(encoder, fmt, ip, remaining, obp);
            if (consumed < 0) goto END;
            if (consumed == 0 && obp->size == 0) goto DONE;
            if (obp->size == 0) break;

            remaining -= consumed;
            ip += consumed * fmt->channels_per_frame;
            if (is_padding) {
            /*
             * As we pad 1 frame at beginning and ending by our extrapolator,
             * we want to drop them.
             * We delay output by 1 frame by double buffering, and discard
             * second frame and final frame from the encoder.
             * Since sbr_header is included in the first frame (in case of
             * SBR), we cannot discard first frame. So we pick second instead.
             */
                flip ^= 1;
                ++encoded;
                if (encoded == 1 || encoded == 3)
                    continue;
            }
            if (write_sample(params->output_fp, m4af, &obuf[flip]) < 0)
                goto END;
            ++frames_written;
        } while (remaining > 0);
    }
DONE:
    /*
     * When interrupted, we haven't pulled out last extrapolated frames
     * from the reader. Therefore, we have to write the final outcome.
     */
    if (g_interrupted) {
        if (write_sample(params->output_fp, m4af, &obp[flip^1]) < 0)
            goto END;
        ++frames_written;
    }
    if (!params->silent)
        aacenc_progress_finish(&progress, pcm_get_position(reader));
    rc = frames_written;
END:
    if (ibuf) free(ibuf);
    if (obuf[0].data) free(obuf[0].data);
    if (obuf[1].data) free(obuf[1].data);
    return rc;
}

static
void put_tool_tag(m4af_ctx_t *m4af, const aacenc_param_ex_t *params,
                  HANDLE_AACENCODER encoder)
{
    char tool_info[256];
    char *p = tool_info;
    LIB_INFO lib_info;

    p += sprintf(p, PROGNAME " %s, ", fdkaac_version);
    aacenc_get_lib_info(&lib_info);
    p += sprintf(p, "libfdk-aac %s, ", lib_info.versionStr);
    if (params->bitrate_mode)
        sprintf(p, "VBR mode %d", params->bitrate_mode);
    else
        sprintf(p, "CBR %dkbps",
                aacEncoder_GetParam(encoder, AACENC_BITRATE) / 1000);

    m4af_add_itmf_string_tag(m4af, M4AF_TAG_TOOL, tool_info);
}

static
int finalize_m4a(m4af_ctx_t *m4af, const aacenc_param_ex_t *params,
                 HANDLE_AACENCODER encoder)
{
    unsigned i;
    aacenc_tag_entry_t *tag;
    
    tag = params->source_tags.tag_table;
    for (i = 0; i < params->source_tags.tag_count; ++i, ++tag)
        aacenc_write_tag_entry(m4af, tag);

    if (params->json_filename)
        aacenc_write_tags_from_json(m4af, params->json_filename);

    tag = params->tags.tag_table;
    for (i = 0; i < params->tags.tag_count; ++i, ++tag)
        aacenc_write_tag_entry(m4af, tag);

    put_tool_tag(m4af, params, encoder);

    if (m4af_finalize(m4af, params->moov_before_mdat) < 0) {
        fprintf(stderr, "ERROR: failed to finalize m4a\n");
        return -1;
    }
    return 0;
}

static
char *generate_output_filename(const char *filename, const char *ext)
{
    char *p = 0;
    size_t ext_len = strlen(ext);

    if (strcmp(filename, "-") == 0) {
        p = malloc(ext_len + 6);
        sprintf(p, "stdin%s", ext);
    } else {
        const char *base = aacenc_basename(filename);
        size_t ilen = strlen(base);
        const char *ext_org = strrchr(base, '.');
        if (ext_org) ilen = ext_org - base;
        p = malloc(ilen + ext_len + 1);
        sprintf(p, "%.*s%s", (int)ilen, base, ext);
    }
    return p;
}

static
int parse_raw_spec(const char *spec, pcm_sample_description_t *desc)
{
    unsigned bits;
    unsigned char c_type, c_endian = 'L';
    int type;

    if (sscanf(spec, "%c%u%c", &c_type, &bits, &c_endian) < 2)
        return -1;
    c_type = toupper(c_type);
    c_endian = toupper(c_endian);

    if (c_type == 'S')
        type = 1;
    else if (c_type == 'U')
        type = 2;
    else if (c_type == 'F')
        type = 4;
    else
        return -1;

    if (c_endian == 'B')
        type |= 8;
    else if (c_endian != 'L')
        return -1;

    if (c_type == 'F' && bits != 32 && bits != 64)
        return -1;
    if (c_type != 'F' && (bits < 8 || bits > 32))
        return -1;

    desc->sample_type = type;
    desc->bits_per_channel = bits;
    return 0;
}

static pcm_io_vtbl_t pcm_io_vtbl = {
    read_callback, seek_callback, tell_callback
};
static pcm_io_vtbl_t pcm_io_vtbl_noseek = { read_callback, 0, tell_callback };

static
pcm_reader_t *open_input(aacenc_param_ex_t *params)
{
    pcm_io_context_t io = { 0 };
    pcm_reader_t *reader = 0;

    if ((params->input_fp = aacenc_fopen(params->input_filename, "rb")) == 0) {
        aacenc_fprintf(stderr, "ERROR: %s: %s\n", params->input_filename,
                       strerror(errno));
        goto FAIL;
    }
    io.cookie = params->input_fp;
    if (aacenc_seekable(params->input_fp))
        io.vtbl = &pcm_io_vtbl;
    else
        io.vtbl = &pcm_io_vtbl_noseek;

    if (params->is_raw) {
        int bytes_per_channel;
        pcm_sample_description_t desc = { 0 };
        if (parse_raw_spec(params->raw_format, &desc) < 0) {
            fprintf(stderr, "ERROR: invalid raw-format spec\n");
            goto FAIL;
        }
        desc.sample_rate = params->raw_rate;
        desc.channels_per_frame = params->raw_channels;
        bytes_per_channel = (desc.bits_per_channel + 7) / 8;
        desc.bytes_per_frame = params->raw_channels * bytes_per_channel;
        if ((reader = raw_open(&io, &desc)) == 0) {
            fprintf(stderr, "ERROR: failed to open raw input\n");
            goto FAIL;
        }
    } else {
        int c;
        ungetc(c = getc(params->input_fp), params->input_fp);

        switch (c) {
        case 'R':
            if ((reader = wav_open(&io, params->ignore_length)) == 0) {
                fprintf(stderr, "ERROR: broken / unsupported input file\n");
                goto FAIL;
            }
            break;
        case 'c':
            params->source_tag_ctx.add = aacenc_add_tag_entry_to_store;
            params->source_tag_ctx.add_ctx = &params->source_tags;
            if ((reader = caf_open(&io,
                                   aacenc_translate_generic_text_tag,
                                   &params->source_tag_ctx)) == 0) {
                fprintf(stderr, "ERROR: broken / unsupported input file\n");
                goto FAIL;
            }
            break;
        default:
            fprintf(stderr, "ERROR: unsupported input file\n");
            goto FAIL;
        }
    }
    reader = pcm_open_native_converter(reader);
    if (reader && PCM_IS_FLOAT(pcm_get_format(reader)))
        reader = limiter_open(reader);
    if (reader && (reader = pcm_open_sint16_converter(reader)) != 0) {
        if (do_smart_padding(params->profile))
            reader = extrapolater_open(reader);
    }
    return reader;
FAIL:
    return 0;
}

int main(int argc, char **argv)
{
    static m4af_io_callbacks_t m4af_io = {
        read_callback, write_callback, seek_callback, tell_callback
    };
    aacenc_param_ex_t params = { 0 };

    int result = 2;
    char *output_filename = 0;
    pcm_reader_t *reader = 0;
    HANDLE_AACENCODER encoder = 0;
    AACENC_InfoStruct aacinfo = { 0 };
    m4af_ctx_t *m4af = 0;
    const pcm_sample_description_t *sample_format;
    int frame_count = 0;
    int sbr_mode = 0;
    unsigned scale_shift = 0;

    setlocale(LC_CTYPE, "");
    setbuf(stderr, 0);

    if (parse_options(argc, argv, &params) < 0)
        return 1;

    if ((reader = open_input(&params)) == 0)
        goto END;

    sample_format = pcm_get_format(reader);

    sbr_mode = aacenc_is_sbr_active((aacenc_param_t*)&params);
    if (sbr_mode && !aacenc_is_sbr_ratio_available()) {
        fprintf(stderr, "WARNING: Only dual-rate SBR is available "
                        "for this version\n");
        params.sbr_ratio = 2;
    }
    scale_shift = aacenc_is_dual_rate_sbr((aacenc_param_t*)&params);
    params.sbr_signaling = 0;
    if (sbr_mode) {
        if (params.transport_format == TT_MP4_LOAS || !scale_shift)
            params.sbr_signaling = 2;
        if (params.transport_format == TT_MP4_RAW &&
            aacenc_is_explicit_bw_compatible_sbr_signaling_available())
            params.sbr_signaling = 1;
    }
    if (aacenc_init(&encoder, (aacenc_param_t*)&params, sample_format,
                    &aacinfo) < 0)
        goto END;

    if (!params.output_filename) {
        const char *ext = params.transport_format ? ".aac" : ".m4a";
        output_filename = generate_output_filename(params.input_filename, ext);
        params.output_filename = output_filename;
    }

    if ((params.output_fp = aacenc_fopen(params.output_filename, "wb+")) == 0) {
        aacenc_fprintf(stderr, "ERROR: %s: %s\n", params.output_filename,
                       strerror(errno));
        goto END;
    }
    handle_signals();

    if (!params.transport_format) {
        uint32_t scale;
        unsigned framelen = aacinfo.frameLength;
        scale = sample_format->sample_rate >> scale_shift;
        if ((m4af = m4af_create(M4AF_CODEC_MP4A, scale, &m4af_io,
                                params.output_fp, params.no_timestamp)) < 0)
            goto END;
        m4af_set_num_channels(m4af, 0, sample_format->channels_per_frame);
        m4af_set_fixed_frame_duration(m4af, 0, framelen >> scale_shift);
        if (aacenc_is_explicit_bw_compatible_sbr_signaling_available())
            m4af_set_decoder_specific_info(m4af, 0,
                                           aacinfo.confBuf, aacinfo.confSize);
        else {
            uint8_t mp4asc[32];
            uint32_t ascsize = sizeof(mp4asc);
            aacenc_mp4asc((aacenc_param_t*)&params, aacinfo.confBuf,
                          aacinfo.confSize, mp4asc, &ascsize);
            m4af_set_decoder_specific_info(m4af, 0, mp4asc, ascsize);
        }
        m4af_set_vbr_mode(m4af, 0, params.bitrate_mode);
        m4af_set_priming_mode(m4af, params.gapless_mode + 1);
        m4af_begin_write(m4af);
    }
    frame_count = encode(&params, reader, encoder, aacinfo.frameLength, m4af);
    if (frame_count < 0)
        goto END;
    if (m4af) {
        uint32_t padding;
#if AACENCODER_LIB_VL0 < 4
        uint32_t delay = aacinfo.encoderDelay;
        if (sbr_mode && params.profile != AOT_ER_AAC_ELD
            && !params.include_sbr_delay)
            delay -= 481 << scale_shift;
#else
        uint32_t delay = params.include_sbr_delay ? aacinfo.nDelay
                                                  : aacinfo.nDelayCore;
#endif
        int64_t frames_read = pcm_get_position(reader);

        padding = frame_count * aacinfo.frameLength - frames_read - delay;
        m4af_set_priming(m4af, 0, delay >> scale_shift, padding >> scale_shift);
        if (finalize_m4a(m4af, &params, encoder) < 0)
            goto END;
    }
    result = 0;
END:
    if (reader) pcm_teardown(&reader);
    if (params.input_fp) fclose(params.input_fp);
    if (m4af) m4af_teardown(&m4af);
    if (params.output_fp) fclose(params.output_fp);
    if (encoder) aacEncClose(&encoder);
    if (output_filename) free(output_filename);
    if (params.tags.tag_table)
        aacenc_free_tag_store(&params.tags);
    if (params.source_tags.tag_table)
        aacenc_free_tag_store(&params.source_tags);

    return result;
}
