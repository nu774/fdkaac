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
#include <locale.h>
#include <errno.h>
#include <getopt.h>
#include "compat.h"
#include "wav_reader.h"
#include "aacenc.h"
#include "m4af.h"
#include "progress.h"
#include "version.h"

#define PROGNAME "fdkaac"

static
int read_callback(void *cookie, void *data, uint32_t size)
{
    return fread(data, 1, size, (FILE*)cookie);
}

static
int write_callback(void *cookie, const void *data, uint32_t size)
{
    return fwrite(data, 1, size, (FILE*)cookie);
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
"                               129: MPEG-2 AAC LC\n"
"                               132: MPEG-2 HE-AAC (SBR)\n"
"                               156: MPEG-2 HE-AAC v2 (SBR+PS)\n"
" -b, --bitrate <n>             Bitrate in bits per seconds (for CBR)\n"
" -m, --bitrate-mode <n>        Bitrate configuration\n"
"                                 0: CBR (default)\n"
"                                 1-5: VBR\n"
"                               (VBR mode is not officially supported, and\n"
"                                works only on a certain combination of\n"
"                                parameter settings, sample rate, and\n"
"                                channel configuration)\n"
" -w, --bandwidth <n>           Frequency bandwidth in Hz (AAC LC only)\n"
" -a, --afterurner <n>          Afterburner\n"
"                                 0: Off\n"
"                                 1: On(default)\n"
" -L, --lowdelay-sbr            Enable ELD-SBR (AAC ELD only)\n"
" -s, --sbr-signaling <n>       SBR signaling mode\n"
"                                 0: Implicit, backward compatible(default)\n"
"                                 1: Explicit SBR and implicit PS\n"
"                                 2: Explicit hierarchical signaling\n"
" -f, --transport-format <n>    Transport format\n"
"                                 0: RAW (default, muxed into M4A)\n"
"                                 1: ADIF\n"
"                                 2: ADTS\n"
"                                 6: LATM MCP=1\n"
"                                 7: LATM MCP=0\n"
"                                10: LOAS/LATM (LATM within LOAS)\n"
" -c, --adts-crc-check          Add CRC protection on ADTS header\n"
" -h, --header-period <n>       StreamMuxConfig/PCE repetition period in\n"
"                               transport layer\n"
"\n"
" -o <filename>                 Output filename\n"
" --ignore-length               Ignore length of WAV header\n"
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
    , fdkaac_version);
}

typedef struct aacenc_tag_entry_t {
    uint32_t tag;
    const char *data;
} aacenc_tag_entry_t;

typedef struct aacenc_param_ex_t {
    AACENC_PARAMS

    char *input_filename;
    char *output_filename;
    unsigned ignore_length;

    aacenc_tag_entry_t *tag_table;
    unsigned tag_count;
    unsigned tag_table_capacity;
} aacenc_param_ex_t;

static
int parse_options(int argc, char **argv, aacenc_param_ex_t *params)
{
    int ch;
    unsigned n;
    aacenc_tag_entry_t *tag;

    static struct option long_options[] = {
        { "help",             no_argument,       0, 'h' },
        { "profile",          required_argument, 0, 'p' },
        { "bitrate",          required_argument, 0, 'b' },
        { "biterate-mode",    required_argument, 0, 'm' },
        { "bandwidth",        required_argument, 0, 'w' },
        { "afterburner",      required_argument, 0, 'a' },
        { "lowdelay-sbr",     no_argument,       0, 'L' },
        { "sbr-signaling",    required_argument, 0, 's' },
        { "transport-format", required_argument, 0, 'f' },
        { "adts-crc-check",   no_argument,       0, 'c' },
        { "header-period",    required_argument, 0, 'P' },

        { "ignore-length",    no_argument,       0, 'I' },

        { "title",            required_argument, 0,  M4AF_TAG_TITLE        },
        { "artist",           required_argument, 0,  M4AF_TAG_ARTIST       },
        { "album",            required_argument, 0,  M4AF_TAG_ALBUM        },
        { "genre",            required_argument, 0,  M4AF_TAG_GENRE        },
        { "date",             required_argument, 0,  M4AF_TAG_DATE         },
        { "composer",         required_argument, 0,  M4AF_TAG_COMPOSER     },
        { "grouping",         required_argument, 0,  M4AF_TAG_GROUPING     },
        { "comment",          required_argument, 0,  M4AF_TAG_COMMENT      },
        { "album-artist",     required_argument, 0,  M4AF_TAG_ALBUM_ARTIST },
        { "track",            required_argument, 0,  M4AF_TAG_TRACK        },
        { "disk",             required_argument, 0,  M4AF_TAG_DISK         },
        { "tempo",            required_argument, 0,  M4AF_TAG_TEMPO        },
    };
    params->afterburner = 1;

    aacenc_getmainargs(&argc, &argv);
    while ((ch = getopt_long(argc, argv, "hp:b:m:w:a:Ls:f:cP:Io:",
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
            params->lowdelay_sbr = 1;
            break;
        case 's':
            if (sscanf(optarg, "%u", &n) != 1 || n > 2) {
                fprintf(stderr, "invalid arg for sbr-signaling\n");
                return -1;
            }
            params->sbr_signaling = n;
            break;
        case 'f':
            if (sscanf(optarg, "%u", &n) != 1) {
                fprintf(stderr, "invalid arg for transport-format\n");
                return -1;
            }
            params->transport_format = n;
            break;
        case 'c':
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
        case 'I':
            params->ignore_length = 1;
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
            if (params->tag_count == params->tag_table_capacity) {
                unsigned newsize = params->tag_table_capacity;
                newsize = newsize ? newsize * 2 : 1;
                params->tag_table =
                    realloc(params->tag_table,
                            newsize * sizeof(aacenc_tag_entry_t));
                params->tag_table_capacity = newsize;
            }
            tag = params->tag_table + params->tag_count;
            tag->tag = ch;
            tag->data = optarg;
            params->tag_count++;
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
    params->input_filename = argv[optind];
    return 0;
};

static
int write_sample(FILE *ofp, m4af_writer_t *m4af,
                 const void *data, uint32_t size, uint32_t duration)
{
    if (!m4af) {
        if (fwrite(data, 1, size, ofp) < 0) {
            fprintf(stderr, "ERROR: fwrite(): %s\n", strerror(errno));
            return -1;
        }
    } else if (m4af_write_sample(m4af, 0, data, size, duration) < 0) {
        fprintf(stderr, "ERROR: failed to write m4a sample\n");
        return -1;
    }
    return 0;
}

static
int encode(wav_reader_t *wavf, HANDLE_AACENCODER encoder,
           uint32_t frame_length, FILE *ofp, m4af_writer_t *m4af)
{
    uint8_t *ibuf = 0;
    int16_t *pcmbuf = 0;
    uint32_t pcmsize = 0;
    uint8_t *obuf = 0;
    uint32_t olen;
    uint32_t osize = 0;
    int nread = 1;
    int consumed;
    int rc = -1;
    int frames_written = 0;
    aacenc_progress_t progress = { 0 };
    const pcm_sample_description_t *format = wav_get_format(wavf);

    ibuf = malloc(frame_length * format->bytes_per_frame);
    aacenc_progress_init(&progress, wav_get_length(wavf), format->sample_rate);
    do {
        if (nread) {
            if ((nread = wav_read_frames(wavf, ibuf, frame_length)) < 0) {
                fprintf(stderr, "ERROR: read failed\n");
                goto END;
            } else if (nread > 0) {
                if (pcm_convert_to_native_sint16(format, ibuf, nread,
                                                 &pcmbuf, &pcmsize) < 0) {
                    fprintf(stderr, "ERROR: unsupported sample format\n");
                    goto END;
                }
            }
            aacenc_progress_update(&progress, wav_get_position(wavf),
                                   format->sample_rate * 2);
        }
        if ((consumed = aac_encode_frame(encoder, format, pcmbuf, nread,
                                         &obuf, &olen, &osize)) < 0)
            goto END;
        if (olen > 0) {
            if (write_sample(ofp, m4af, obuf, olen, frame_length) < 0)
                goto END;
            ++frames_written;
        }
    } while (nread > 0 || olen > 0);
    aacenc_progress_finish(&progress, wav_get_position(wavf));
    rc = frames_written;
END:
    if (ibuf) free(ibuf);
    if (pcmbuf) free(pcmbuf);
    if (obuf) free(obuf);
    return rc;
}

static
int finalize_m4a(m4af_writer_t *m4af, const aacenc_param_ex_t *params,
                 HANDLE_AACENCODER encoder)
{
    unsigned i;
    aacenc_tag_entry_t *tag = params->tag_table;

    for (i = 0; i < params->tag_count; ++i, ++tag) {
        switch (tag->tag) {
        case M4AF_TAG_TRACK:
            {
                unsigned m, n = 0;
                if (sscanf(tag->data, "%u/%u", &m, &n) >= 1)
                    m4af_add_itmf_track_tag(m4af, m, n);
                break;
            }
        case M4AF_TAG_DISK:
            {
                unsigned m, n = 0;
                if (sscanf(tag->data, "%u/%u", &m, &n) >= 1)
                    m4af_add_itmf_disk_tag(m4af, m, n);
                break;
            }
        case M4AF_TAG_TEMPO:
            {
                unsigned n;
                if (sscanf(tag->data, "%u", &n) == 1)
                    m4af_add_itmf_int16_tag(m4af, tag->tag, n);
                break;
            }
        default:
            {
                char *u8 = aacenc_to_utf8(tag->data);
                m4af_add_itmf_string_tag(m4af, tag->tag, u8);
                free(u8);
            }
        }
    }
    {
        char tool_info[256];
        char *p = tool_info;
        LIB_INFO *lib_info = 0;

        p += sprintf(p, PROGNAME " %s, ", fdkaac_version);

        lib_info = calloc(FDK_MODULE_LAST, sizeof(LIB_INFO));
        if (aacEncGetLibInfo(lib_info) == AACENC_OK) {
            for (i = 0; i < FDK_MODULE_LAST; ++i)
                if (lib_info[i].module_id == FDK_AACENC)
                    break;
            p += sprintf(p, "libfdk-aac %s, ", lib_info[i].versionStr);
        }
        free(lib_info);
        if (params->bitrate_mode)
            sprintf(p, "VBR mode %d", params->bitrate_mode);
        else
            sprintf(p, "CBR %dkbps", params->bitrate / 1000);

        m4af_add_itmf_string_tag(m4af, M4AF_TAG_TOOL, tool_info);
    }
    if (m4af_finalize(m4af) < 0) {
        fprintf(stderr, "ERROR: failed to finalize m4a\n");
        return -1;
    }
    return 0;
}

static
const char *basename(const char *filename)
{
    char *p = strrchr(filename, '/');
#ifdef _WIN32
    char *q = strrchr(filename, '\\');
    if (p < q) p = q;
#endif
    return p ? p + 1 : filename;
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
        const char *base = basename(filename);
        size_t ilen = strlen(base);
        const char *ext_org = strrchr(base, '.');
        if (ext_org) ilen = ext_org - base;
        p = malloc(ilen + ext_len + 1);
        sprintf(p, "%.*s%s", ilen, base, ext);
    }
    return p;
}

int main(int argc, char **argv)
{
    wav_io_context_t wav_io = { read_callback, seek_callback };
    m4af_io_callbacks_t m4af_io = {
        write_callback, seek_callback, tell_callback };
    aacenc_param_ex_t params = { 0 };

    int result = 2;
    FILE *ifp = 0;
    FILE *ofp = 0;
    char *output_filename = 0;
    wav_reader_t *wavf = 0;
    HANDLE_AACENCODER encoder = 0;
    AACENC_InfoStruct aacinfo = { 0 };
    m4af_writer_t *m4af = 0;
    const pcm_sample_description_t *sample_format;
    int downsampled_timescale = 0;
    int frame_count = 0;

    setlocale(LC_CTYPE, "");
    setbuf(stderr, 0);

    if (parse_options(argc, argv, &params) < 0)
        return 1;

    if ((ifp = aacenc_fopen(params.input_filename, "rb")) == 0) {
        aacenc_fprintf(stderr, "ERROR: %s: %s\n", params.input_filename,
                       strerror(errno));
        goto END;
    }

    if (ifp == stdin)
        wav_io.seek = 0;

    if ((wavf = wav_open(&wav_io, ifp, params.ignore_length)) == 0) {
        fprintf(stderr, "ERROR: broken / unsupported input file\n");
        goto END;
    }
    sample_format = wav_get_format(wavf);

    if (aacenc_init(&encoder, (aacenc_param_t*)&params, sample_format,
                    &aacinfo) < 0)
        goto END;

    if (!params.output_filename) {
        const char *ext = params.transport_format ? ".aac" : ".m4a";
        output_filename = generate_output_filename(params.input_filename, ext);
        params.output_filename = output_filename;
    }

    if ((ofp = aacenc_fopen(params.output_filename, "wb")) == 0) {
        aacenc_fprintf(stderr, "ERROR: %s: %s\n", params.output_filename,
                       strerror(errno));
        goto END;
    }
    if (!params.transport_format) {
        uint32_t scale;
        unsigned framelen = aacinfo.frameLength;
	int sbr_mode = aacenc_is_sbr_active((aacenc_param_t*)&params);
	int sig_mode = aacEncoder_GetParam(encoder, AACENC_SIGNALING_MODE);
	if (sbr_mode && !sig_mode)
	    downsampled_timescale = 1;
        scale = sample_format->sample_rate >> downsampled_timescale;
        if ((m4af = m4af_create(M4AF_CODEC_MP4A, scale, &m4af_io, ofp)) < 0)
            goto END;
        m4af_decoder_specific_info(m4af, 0, aacinfo.confBuf, aacinfo.confSize);
        m4af_set_fixed_frame_duration(m4af, 0,
                                      framelen >> downsampled_timescale);
        m4af_begin_write(m4af);
    }
    frame_count = encode(wavf, encoder, aacinfo.frameLength, ofp, m4af);
    if (frame_count < 0)
        goto END;
    if (m4af) {
        uint32_t delay = aacinfo.encoderDelay;
	int64_t frames_read = wav_get_position(wavf);
        uint32_t padding = frame_count * aacinfo.frameLength
                            - frames_read - aacinfo.encoderDelay;
        m4af_set_priming(m4af, 0, delay >> downsampled_timescale,
                         padding >> downsampled_timescale);
        if (finalize_m4a(m4af, &params, encoder) < 0)
            goto END;
    }
    result = 0;
END:
    if (wavf) wav_teardown(&wavf);
    if (ifp) fclose(ifp);
    if (m4af) m4af_teardown(&m4af);
    if (ofp) fclose(ofp);
    if (encoder) aacEncClose(&encoder);
    if (output_filename) free(output_filename);
    if (params.tag_table) free(params.tag_table);

    return result;
}
