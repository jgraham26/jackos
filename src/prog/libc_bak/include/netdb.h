#ifndef _NETDB_H
#define _NETDB_H 1

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard hostent structure */
struct hostent {
    char  *h_name;       /* Official name of host */
    char **h_aliases;    /* Alias list */
    int    h_addrtype;   /* Host address type (e.g. AF_INET) */
    int    h_length;     /* Length of address in bytes */
    char **h_addr_list;  /* List of addresses */
};
#define h_addr h_addr_list[0] /* Backwards compatibility */

/* Network entry structure */
struct netent {
    char     *n_name;     /* Official name of network */
    char    **n_aliases;  /* Alias list */
    int       n_addrtype; /* Net address type */
    uint32_t  n_net;      /* Network number */
};

/* Service entry structure */
struct servent {
    char  *s_name;     /* Official name of service */
    char **s_aliases;  /* Alias list */
    int    s_port;     /* Port number (network byte order) */
    char  *s_proto;    /* Protocol to use */
};

/* Protocol entry structure */
struct protoent {
    char  *p_name;     /* Official name of protocol */
    char **p_aliases;  /* Alias list */
    int    p_proto;    /* Protocol number */
};

/* Modern POSIX addrinfo structure */
struct addrinfo {
    int              ai_flags;     /* Input flags */
    int              ai_family;    /* Protocol family for socket (AF_INET, etc.) */
    int              ai_socktype;  /* Socket type (SOCK_STREAM, etc.) */
    int              ai_protocol;  /* Protocol for socket */
    socklen_t        ai_addrlen;   /* Length of socket address */
    struct sockaddr *ai_addr;      /* Socket address for socket */
    char            *ai_canonname; /* Canonical name for service location */
    struct addrinfo *ai_next;      /* Pointer to next in list */
};

/* Flags for getaddrinfo() */
#define AI_PASSIVE     0x0001 /* Socket address is intended for bind() */
#define AI_CANONNAME   0x0002 /* Request for canonical name */
#define AI_NUMERICHOST 0x0004 /* Don't resolve hostnames (IP only) */
#define AI_V4MAPPED    0x0008 /* IPv4-mapped IPv6 address */
#define AI_ALL         0x0010 /* Return both IPv4 and IPv6 */
#define AI_ADDRCONFIG  0x0020 /* Use config to determine IPv4/v6 query */
#define AI_NUMERICSERV 0x0400 /* Don't resolve service names (port only) */

/* Error codes for getaddrinfo() / gai_strerror() */
#define EAI_BADFLAGS   -1  /* Invalid flags parameter */
#define EAI_NONAME     -2  /* Host or service not provided, or not known */
#define EAI_AGAIN      -3  /* Temporary failure in name resolution */
#define EAI_FAIL       -4  /* Non-recoverable failure in name resolution */
#define EAI_FAMILY     -6  /* ai_family not supported */
#define EAI_SOCKTYPE   -7  /* ai_socktype not supported */
#define EAI_SERVICE    -8  /* Service not supported for ai_socktype */
#define EAI_MEMORY     -10 /* Memory allocation failure */
#define EAI_SYSTEM     -11 /* System error returned in errno */
#define EAI_OVERFLOW   -12 /* Argument buffer overflow */

/* Flags for getnameinfo() */
#define NI_MAXHOST      1025
#define NI_MAXSERV      32

#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02
#define NI_NOFQDN      0x04
#define NI_NAMEREQD    0x08
#define NI_DGRAM       0x10

/* Legacy h_errno error codes */
extern int h_errno;

#define NETDB_INTERNAL -1 /* See errno */
#define NETDB_SUCCESS   0 /* No problem */
#define HOST_NOT_FOUND  1 /* Authoritative Answer Host not found */
#define TRY_AGAIN       2 /* Non-Authoritative Host not found, or SERVERFAIL */
#define NO_RECOVERY     3 /* Non-recoverable errors: FORMERR, REFUSED, NOTIMP */
#define NO_DATA         4 /* Valid name, no data record of requested type */
#define NO_ADDRESS      NO_DATA

/* Function declarations */
void             sethostent(int stayopen);
void             endhostent(void);
struct hostent  *gethostent(void);
struct hostent  *gethostbyname(const char *name);
struct hostent  *gethostbyaddr(const void *addr, socklen_t len, int type);

void             setnetent(int stayopen);
void             endnetent(void);
struct netent   *getnetent(void);
struct netent   *getnetbyname(const char *name);
struct netent   *getnetbyaddr(uint32_t net, int type);

void             setprotoent(int stayopen);
void             endprotoent(void);
struct protoent *getprotoent(void);
struct protoent *getprotobyname(const char *name);
struct protoent *getprotobynumber(int proto);

void             setservent(int stayopen);
void             endservent(void);
struct servent  *getservent(void);
struct servent  *getservbyname(const char *name, const char *proto);
struct servent  *getservbyport(int port, const char *proto);

int              getaddrinfo(const char *node, const char *service,
                             const struct addrinfo *hints,
                             struct addrinfo **res);
void             freeaddrinfo(struct addrinfo *res);
const char      *gai_strerror(int errcode);

int              getnameinfo(const struct sockaddr *sa, socklen_t salen,
                             char *host, socklen_t hostlen,
                             char *serv, socklen_t servlen, int flags);

void             herror(const char *s);
const char      *hstrerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* _NETDB_H */