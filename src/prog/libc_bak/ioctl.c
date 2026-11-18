#include <sys/ioctl.h>
#include <stdarg.h>
#include <errno.h>

/* Assuming syscall3(SYS_ioctl, fd, request, arg) */
extern long __syscall3(long number, long arg1, long arg2, long arg3);
#define SYS_ioctl 16 /* Linux x86_64 syscall number for ioctl */

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    long ret = __syscall3(SYS_ioctl, (long)fd, (long)request, (long)arg);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}