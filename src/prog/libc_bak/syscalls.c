#include <sys/types.h>
#include <errno.h>

long __syscall_ret(long ret) {
    /* Linux syscall error range is [-4095, -1] */
    if ((unsigned long)ret > -4096UL) {
        errno = (int)(-ret);
        return -1;
    }
    return ret;
}

static inline int64_t __syscall0(uint64_t n) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall1(uint64_t n, uint64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall2(uint64_t n, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall4(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

int64_t read(int fd, void *buf, size_t count) {
    long ret = (long)__syscall3(0, fd, (uint64_t)buf, count);
    return (int64_t)__syscall_ret(ret);
}

int64_t write(int fd, const void *buf, size_t count) {
    long ret = (long)__syscall3(1, fd, (uint64_t)buf, count);
    return (int64_t)__syscall_ret(ret);
}

int64_t open(const char *path, int flags, ...) {
    long ret =(long)__syscall2(2, (uint64_t)path, flags);
    return (int64_t)__syscall_ret(ret);
}

int64_t close(int fd) {
    long ret = (long)__syscall1(3, fd);
    return (int64_t)__syscall_ret(ret);
}

uintptr_t brk(uintptr_t addr) {
    long ret = (long)__syscall1(12, addr);
    return (uintptr_t)__syscall_ret(ret);
}

int sched_yield(void) {
    long ret = (long)__syscall0(24);
    return (int)__syscall_ret(ret);
}

pid_t fork(void) {
    long ret = (long)__syscall0(57);
    return (pid_t)__syscall_ret(ret);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    long ret = (long)__syscall3(59, (uint64_t)path, (uint64_t)argv, (uint64_t)envp);
    return (int)__syscall_ret(ret);
}

void exit(int status) {
    __syscall1(60, status);
    for (;;) __asm__ volatile("hlt");
}

pid_t wait4(pid_t pid, int *wstatus, int options, void *rusage) {
    long ret = (long)__syscall4(61, pid, (uint64_t)wstatus, options, (uint64_t)rusage);
    return (int64_t)__syscall_ret(ret);
}

pid_t waitpid(pid_t pid, int *wstatus, int options) {
    long ret = (long)wait4(pid, wstatus, options, (void*)0);
    return (int64_t)__syscall_ret(ret);
}

pid_t wait(int *wstatus) {
    long ret = (long)waitpid(-1, wstatus, 0);
    return (int64_t)__syscall_ret(ret);
}

int64_t getdents64(int fd, void *dirp, size_t count) {
    long ret = (long)__syscall3(217, fd, (uint64_t)dirp, count); /* Linux x86_64 SYS_getdents64 */
    return (int64_t)__syscall_ret(ret);
}