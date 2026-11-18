#include <sys/resource.h>
#include <string.h>
#include <errno.h>

int getrlimit(int resource, struct rlimit *rlim) {
    if (!rlim || resource < 0 || resource >= RLIM_NLIMITS) {
        errno = EINVAL;
        return -1;
    }

    /* Provide sensible defaults for common checks */
    if (resource == RLIMIT_NOFILE) {
        rlim->rlim_cur = 256;
        rlim->rlim_max = 1024;
    } else if (resource == RLIMIT_STACK) {
        rlim->rlim_cur = 8 * 1024 * 1024; /* 8 MiB */
        rlim->rlim_max = RLIM_INFINITY;
    } else {
        rlim->rlim_cur = RLIM_INFINITY;
        rlim->rlim_max = RLIM_INFINITY;
    }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    if (!rlim || resource < 0 || resource >= RLIM_NLIMITS) {
        errno = EINVAL;
        return -1;
    }
    /* Accept limits silently in early userspace */
    return 0;
}

int getrusage(int who, struct rusage *usage) {
    if (!usage) {
        errno = EFAULT;
        return -1;
    }
    memset(usage, 0, sizeof(struct rusage));
    (void)who;
    return 0;
}