#ifndef _BYTESWAP_H
#define _BYTESWAP_H 1

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)

#define bswap_16(x) __builtin_bswap16((uint16_t)(x))
#define bswap_32(x) __builtin_bswap32((uint32_t)(x))
#define bswap_64(x) __builtin_bswap64((uint64_t)(x))

#else

static inline uint16_t bswap_16(uint16_t x) {
    return (uint16_t)(((x & 0x00FFU) << 8) |
                     ((x & 0xFF00U) >> 8));
}

static inline uint32_t bswap_32(uint32_t x) {
    return ((x & 0x000000FFUL) << 24) |
           ((x & 0x0000FF00UL) << 8)  |
           ((x & 0x00FF0000UL) >> 8)  |
           ((x & 0xFF000000UL) >> 24);
}

static inline uint64_t bswap_64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8)  |
           ((x & 0x000000FF00000000ULL) >> 8)  |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

#endif

#endif /* _BYTESWAP_H */