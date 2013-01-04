/* 
 * Copyright (C) 2013 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef M4AF_ENDIAN_H
#define M4AF_ENDIAN_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_STDINT_H
#  include <stdint.h>
#endif

#if HAVE_ENDIAN_H
#  include <endian.h>
#  define m4af_htob16(x) htobe16(x)
#  define m4af_htob32(x) htobe32(x)
#  define m4af_htob64(x) htobe64(x)
#  define m4af_btoh16(x) be16toh(x)
#  define m4af_btoh32(x) be32toh(x)
#  define m4af_btoh64(x) be64toh(x)
#  define m4af_htol16(x) htole16(x)
#  define m4af_htol32(x) htole32(x)
#  define m4af_htol64(x) htole64(x)
#  define m4af_ltoh16(x) le16toh(x)
#  define m4af_ltoh32(x) le32toh(x)
#  define m4af_ltoh64(x) le64toh(x)
#elif WORDS_BIGENDIAN
#  define m4af_htob16(x) (x)
#  define m4af_htob32(x) (x)
#  define m4af_htob64(x) (x)
#  define m4af_btoh16(x) (x)
#  define m4af_btoh32(x) (x)
#  define m4af_btoh64(x) (x)
#  define m4af_ltoh16(x) m4af_swap16(x)
#  define m4af_ltoh32(x) m4af_swap32(x)
#  define m4af_ltoh64(x) m4af_swap64(x)
#  define m4af_htol16(x) m4af_swap16(x)
#  define m4af_htol32(x) m4af_swap32(x)
#  define m4af_htol64(x) m4af_swap64(x)
#else
#  define m4af_htob16(x) m4af_swap16(x)
#  define m4af_htob32(x) m4af_swap32(x)
#  define m4af_htob64(x) m4af_swap64(x)
#  define m4af_btoh16(x) m4af_swap16(x)
#  define m4af_btoh32(x) m4af_swap32(x)
#  define m4af_btoh64(x) m4af_swap64(x)
#  define m4af_ltoh16(x) (x)
#  define m4af_ltoh32(x) (x)
#  define m4af_ltoh64(x) (x)
#  define m4af_htol16(x) (x)
#  define m4af_htol32(x) (x)
#  define m4af_htol64(x) (x)
#endif

#if _MSC_VER >= 1400
#  include <stdlib.h>
#  define m4af_swap16(x) _byteswap_ushort(x)
#  define m4af_swap32(x) _byteswap_ulong(x)
#  define m4af_swap64(x) _byteswap_uint64(x)
#elif HAVE_BYTESWAP_H
#  include <byteswap.h>
#  define m4af_swap16(x) bswap_16(x)
#  define m4af_swap32(x) bswap_32(x)
#  define m4af_swap64(x) bswap_64(x)
#else
static inline uint16_t m4af_swap16(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static inline uint32_t m4af_swap32(uint32_t x)
{
    return (m4af_htob16(x) << 16) | m4af_htob16(x >> 16);
}

static inline uint64_t m4af_swap64(uint64_t x)
{
    return ((uint64_t)m4af_htob32(x) << 32) | m4af_htob32(x >> 32);
}
#endif

#endif /* M4AF_ENDIAN_H */

