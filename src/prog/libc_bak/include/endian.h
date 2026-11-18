#ifndef _ENDIAN_H
#define _ENDIAN_H 1

#include <byteswap.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard endianness identifiers */
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412

/* POSIX / BSD compatibility aliases */
#define LITTLE_ENDIAN   __LITTLE_ENDIAN
#define BIG_ENDIAN      __BIG_ENDIAN
#define PDP_ENDIAN      __PDP_ENDIAN

/* Architecture Byte Order (x86_64 is strictly little-endian) */
#define __BYTE_ORDER    __LITTLE_ENDIAN
#define BYTE_ORDER      __BYTE_ORDER

#if __BYTE_ORDER == __LITTLE_ENDIAN

/* Conversion to/from Big Endian (Network Order) */
#define htobe16(x) bswap_16(x)
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) bswap_16(x)
#define le16toh(x) ((uint16_t)(x))

#define htobe32(x) bswap_32(x)
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) bswap_32(x)
#define le32toh(x) ((uint32_t)(x))

#define htobe64(x) bswap_64(x)
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) bswap_64(x)
#define le64toh(x) ((uint64_t)(x))

/* POSIX network-to-host and host-to-network aliases */
#ifndef htons
#define htons(x) htobe16(x)
#endif
#ifndef ntohs
#define ntohs(x) be16toh(x)
#endif
#ifndef htonl
#define htonl(x) htobe32(x)
#endif
#ifndef ntohl
#define ntohl(x) be32toh(x)
#endif

/* glibc / Linux 64-bit network conversion aliases */
#ifndef htonq
#define htonq(x) htobe64(x)
#endif
#ifndef ntohq
#define ntohq(x) be64toh(x)
#endif

/* glibc internal double-underscore aliases */
#define __htobe16(x) htobe16(x)
#define __htole16(x) htole16(x)
#define __be16toh(x) be16toh(x)
#define __le16toh(x) le16toh(x)

#define __htobe32(x) htobe32(x)
#define __htole32(x) htole32(x)
#define __be32toh(x) be32toh(x)
#define __le32toh(x) le32toh(x)

#define __htobe64(x) htobe64(x)
#define __htole64(x) htole64(x)
#define __be64toh(x) be64toh(x)
#define __le64toh(x) le64toh(x)

#else
#error "Big endian targets are not supported on this x86_64 toolchain"
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ENDIAN_H */