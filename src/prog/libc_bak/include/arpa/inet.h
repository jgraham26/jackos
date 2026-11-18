#ifndef _ARPA_INET_H
#define _ARPA_INET_H 1

#include <netinet/in.h>
#include <inttypes.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard byte-order conversions (typically inline or intrinsics on x86_64) */
#ifndef _BYTE_ORDER_MACROS_DEFINED
#define _BYTE_ORDER_MACROS_DEFINED
#define htonl(x) __builtin_bswap32(x)
#define ntohl(x) __builtin_bswap32(x)
#define htons(x) __builtin_bswap16(x)
#define ntohs(x) __builtin_bswap16(x)
#endif

/* Traditional IPv4 conversion functions */
in_addr_t    inet_addr(const char *cp);
int          inet_aton(const char *cp, struct in_addr *inp);
char        *inet_ntoa(struct in_addr in);
in_addr_t    inet_network(const char *cp);
struct in_addr inet_makeaddr(in_addr_t net, in_addr_t lna);
in_addr_t    inet_lnaof(struct in_addr in);
in_addr_t    inet_netof(struct in_addr in);

/* Modern POSIX address conversions (IPv4 and IPv6) */
int          inet_pton(int af, const char *src, void *dst);
const char  *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H */