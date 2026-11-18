#include <poll.h>
#include <errno.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    if (nfds == 0) {
        return 0;
    }
    if (!fds) {
        errno = EFAULT;
        return -1;
    }

    /* Minimal stub: report POLLIN / POLLOUT on valid non-negative descriptors */
    int ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (fds[i].fd < 0) {
            continue;
        }

        /* Echo requested read/write readiness */
        fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
        if (fds[i].revents) {
            ready++;
        }
    }

    (void)timeout;
    return ready;
}