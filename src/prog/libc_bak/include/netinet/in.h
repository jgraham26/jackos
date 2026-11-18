#ifndef _NETINET_IN_H
#define _NETINET_IN_H 1

#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

/* IPv4 address structure */
struct in_addr {
    in_addr_t s_addr;
};

/* IPv4 socket address structure */
struct sockaddr_in {
    sa_family_t    sin_family; /* AF_INET */
    in_port_t      sin_port;   /* Port in network byte order */
    struct in_addr sin_addr;   /* Internet address */
    unsigned char  sin_zero[8];/* Padding to size of struct sockaddr */
};

/* IPv6 address structure */
struct in6_addr {
    uint8_t s6_addr[16];
};

/* IPv6 socket address structure */
struct sockaddr_in6 {
    sa_family_t     sin6_family;   /* AF_INET6 */
    in_port_t       sin6_port;     /* Port number */
    uint32_t        sin6_flowinfo; /* IPv6 flow information */
    struct in6_addr sin6_addr;     /* IPv6 address */
    uint32_t        sin6_scope_id; /* Scope ID */
};

/* Standard well-known addresses */
#define INADDR_ANY          ((in_addr_t) 0x00000000)
#define INADDR_LOOPBACK     ((in_addr_t) 0x7f000001)
#define INADDR_BROADCAST    ((in_addr_t) 0xffffffff)
#define INADDR_NONE         ((in_addr_t) 0xffffffff)

/* Standard string length limits */
#define INET_ADDRSTRLEN     16
#define INET6_ADDRSTRLEN    46

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_IN_H */