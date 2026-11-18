#ifndef _POLL_H
#define _POLL_H 1

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type used for the number of file descriptors */
typedef unsigned long nfds_t;

/* Data structure describing a polling request */
struct pollfd {
    int   fd;       /* File descriptor to poll */
    short events;   /* Requested events bitmask */
    short revents;  /* Returned events bitmask */
};

/* Event flags for events and revents fields */
#define POLLIN         0x0001  /* There is data to read */
#define POLLPRI        0x0002  /* There is urgent data to read */
#define POLLOUT        0x0004  /* Writing now will not block */

/* Linux / XPG4.2 source compatibility flags */
#define POLLRDNORM     0x0040  /* Normal data may be read */
#define POLLRDBAND     0x0080  /* Priority data may be read */
#define POLLWRNORM     0x0100  /* Writing now will not block */
#define POLLWRBAND     0x0200  /* Priority data may be written */

/* Return-only event flags (set in revents by kernel) */
#define POLLERR        0x0008  /* Error condition */
#define POLLHUP        0x0010  /* Hung up (e.g., connection closed or pipe write end closed) */
#define POLLNVAL       0x0020  /* Invalid request: fd not open */

/* Function prototypes */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#if defined(_GNU_SOURCE) || defined(_POSIX_C_SOURCE)
# include <signal.h>
# include <time.h>
int ppoll(struct pollfd *fds, nfds_t nfds,
          const struct timespec *tmo_p, const sigset_t *sigmask);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _POLL_H */