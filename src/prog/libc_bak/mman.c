#include <sys/mman.h>
#include <errno.h>

#define SYS_mmap     9
#define SYS_munmap   11
#define SYS_mprotect 10

extern long __syscall6(long number, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);
extern long __syscall2(long number, long arg1, long arg2);
extern long __syscall3(long number, long arg1, long arg2, long arg3);

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    /* System V ABI: 4th argument is in r10 inside syscalls, offset is in r9 */
    long ret = __syscall6(SYS_mmap, (long)addr, (long)length, (long)prot, (long)flags, (long)fd, (long)offset);
    if (ret < 0) {
        errno = (int)-ret;
        return MAP_FAILED;
    }
    return (void *)ret;
}

int munmap(void *addr, size_t length) {
    long ret = __syscall2(SYS_munmap, (long)addr, (long)length);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t length, int prot) {
    long ret = __syscall3(SYS_mprotect, (long)addr, (long)length, (long)prot);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

/* Stubs for advisory/sync calls */
int msync(void *addr, size_t length, int flags) {
    (void)addr; (void)length; (void)flags;
    return 0;
}

int madvise(void *addr, size_t length, int advice) {
    (void)addr; (void)length; (void)advice;
    return 0;
}

int posix_madvise(void *addr, size_t len, int advice) {
    (void)addr; (void)len; (void)advice;
    return 0;
}