#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H 1

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fundamental socket types */
#ifndef _SOCKLEN_T_DECLARED
typedef unsigned int socklen_t;
#define _SOCKLEN_T_DECLARED
#endif

#ifndef _SA_FAMILY_T_DECLARED
typedef unsigned short sa_family_t;
#define _SA_FAMILY_T_DECLARED
#endif

/* Generic socket address structure */
struct sockaddr {
    sa_family_t sa_family;   /* Address family (AF_*) */
    char        sa_data[14]; /* Direct address data */
};

/* Large socket address structure with proper 64-bit alignment */
struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad1[6];  /* Pad to 8-byte boundary */
    int64_t     __ss_align;    /* Enforce 64-bit alignment */
    char        __ss_pad2[112];
};

/* Scatter/gather vector used by sendmsg/recvmsg */
#ifndef _STRUCT_IOVEC_DECLARED
#define _STRUCT_IOVEC_DECLARED
struct iovec {
    void  *iov_base; /* Starting address */
    size_t iov_len;  /* Number of bytes to transfer */
};
#endif

/* Message header for sendmsg/recvmsg */
struct msghdr {
    void         *msg_name;       /* Optional address */
    socklen_t     msg_namelen;    /* Size of address */
    struct iovec *msg_iov;        /* Scatter/gather array */
    int           msg_iovlen;     /* # elements in msg_iov */
    void         *msg_control;    /* Ancillary data */
    socklen_t     msg_controllen; /* Ancillary data buffer len */
    int           msg_flags;      /* Flags on received message */
};

/* Ancillary data header (control messages) */
struct cmsghdr {
    socklen_t cmsg_len;   /* Data byte count, including hdr */
    int       cmsg_level; /* Originating protocol */
    int       cmsg_type;  /* Protocol-specific type */
    /* Followed by unsigned char cmsg_data[] */
};

#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_DATA(cmsg) ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#define CMSG_FIRSTHDR(mhdr) \
    ((size_t)(mhdr)->msg_controllen >= sizeof(struct cmsghdr) \
     ? (struct cmsghdr *)(mhdr)->msg_control : (struct cmsghdr *)0)
#define CMSG_NXTHDR(mhdr, cmsg) \
    (((char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) + sizeof(struct cmsghdr) > \
      (char *)(mhdr)->msg_control + (mhdr)->msg_controllen) \
     ? (struct cmsghdr *)0 \
     : (struct cmsghdr *)((char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len)))

/* Socket types */
#define SOCK_STREAM     1  /* Sequenced, reliable, two-way connection (TCP) */
#define SOCK_DGRAM      2  /* Connectionless, unreliable datagrams (UDP) */
#define SOCK_RAW        3  /* Raw network protocol access */
#define SOCK_RDM        4  /* Reliable datagrams */
#define SOCK_SEQPACKET  5  /* Sequenced, reliable packet stream */
#define SOCK_PACKET     10 /* Linux-specific raw packet socket */

/* Linux socket flags for socket() / socketpair() */
#define SOCK_NONBLOCK   00004000
#define SOCK_CLOEXEC    02000000

/* Address and Protocol Families */
#define AF_UNSPEC       0
#define AF_UNIX         1  /* Local Unix domain sockets */
#define AF_LOCAL        AF_UNIX
#define AF_INET         2  /* Internet IP Protocol (IPv4) */
#define AF_INET6        10 /* IPv6 */
#define AF_NETLINK      16 /* Kernel user interface device */
#define AF_PACKET       17 /* Packet device */

#define PF_UNSPEC       AF_UNSPEC
#define PF_UNIX         AF_UNIX
#define PF_LOCAL        AF_LOCAL
#define PF_INET         AF_INET
#define PF_INET6        AF_INET6
#define PF_NETLINK      AF_NETLINK
#define PF_PACKET       AF_PACKET

/* Socket options level */
#define SOL_SOCKET      1

/* Socket options (getsockopt / setsockopt) */
#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_NO_CHECK     11
#define SO_PRIORITY     12
#define SO_LINGER       13
#define SO_BSDCOMPAT    14
#define SO_REUSEPORT    15
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21

/* Flags for send, sendto, recv, recvfrom */
#define MSG_OOB         0x01
#define MSG_PEEK        0x02
#define MSG_DONTROUTE   0x04
#define MSG_CTRUNC      0x08
#define MSG_TRUNC       0x20
#define MSG_DONTWAIT    0x40
#define MSG_EOR         0x80
#define MSG_WAITALL     0x100
#define MSG_NOSIGNAL    0x4000

/* Values for shutdown() */
#define SHUT_RD         0 /* Shut down reading half */
#define SHUT_WR         1 /* Shut down writing half */
#define SHUT_RDWR       2 /* Shut down both halves */

/* Standard function declarations */
int     socket(int domain, int type, int protocol);
int     socketpair(int domain, int type, int protocol, int sv[2]);
int     bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     listen(int sockfd, int backlog);
int     accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);

int     getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);

ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);

int     getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int     setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

int     shutdown(int sockfd, int how);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H */