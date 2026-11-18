#pragma once

#include <stdint.h>
#include <stddef.h>

#include "fs/stat.h"

#define SYS_READ         0
#define SYS_WRITE        1
#define SYS_OPEN         2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSTAT       6
#define SYS_POLL   7
#define SYS_LSEEK  8
#define SYS_MMAP        9
#define SYS_MPROTECT    10
#define SYS_MUNMAP      11
#define SYS_BRK          12
#define SYS_RT_SIGACTION 13
#define SYS_RT_SIGPROCMASK 14
#define SYS_RT_SIGRETURN 15
#define SYS_IOCTL 16
#define SYS_READV  19
#define SYS_WRITEV 20
#define SYS_PIPE 22
#define SYS_SCHED_YIELD  24
#define SYS_DUP   32
#define SYS_DUP2  33
#define SYS_NANOSLEEP    35
#define SYS_GETPID   39
#define SYS_FORK 57
#define SYS_EXECVE 59
#define SYS_EXIT         60
#define SYS_WAIT4 61
#define SYS_KILL         62
#define SYS_UNAME 63
#define SYS_FCNTL 72
#define SYS_TRUNCATE   76
#define SYS_FTRUNCATE  77
#define SYS_GETCWD 79
#define SYS_CHDIR  80
#define SYS_RENAME     82
#define SYS_MKDIR      83
#define SYS_RMDIR      84
#define SYS_UNLINK     87
#define SYS_SYMLINK    88
#define SYS_READLINK    89
#define SYS_GETTIMEOFDAY 96
#define SYS_SYSINFO 99
#define SYS_GETUID   102
#define SYS_GETGID   104
#define SYS_GETEUID  107
#define SYS_GETEGID  108
#define SYS_SETPGID 109
#define SYS_GETPPID  110
#define SYS_GETPGRP 111
#define SYS_GETPGID 121
#define SYS_ARCH_PRCTL 158
#define SYS_SYNC 162
#define SYS_REBOOT 169
#define SYS_GETTID   186
#define SYS_GETDENTS64  217
#define SYS_SET_TID_ADDRESS 218
#define SYS_CLOCK_GETTIME   228
#define SYS_CLOCK_GETRES    229
#define SYS_EXIT_GROUP   231
#define SYS_MKDIRAT    258
#define SYS_NEWFSTATAT  262
#define SYS_UNLINKAT   263
#define SYS_RENAMEAT   264
#define SYS_SYMLINKAT  266
#define SYS_READLINKAT  267
#define SYS_PPOLL  271
#define SYS_DUP3  292
#define SYS_PIPE2 293

#define AT_REMOVEDIR   0x200

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7


#define F_DUPFD            0
#define F_GETFD            1
#define F_SETFD            2
#define F_GETFL            3
#define F_SETFL            4
#define F_DUPFD_CLOEXEC    1030

#define FD_CLOEXEC         1

#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2



void syscall_init();

extern uint64_t g_syscall_kernel_rsp;
extern uint64_t g_syscall_user_rsp;

typedef struct iovec {
    void  *iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
} iovec_t;

struct linux_pollfd {
    int   fd;
    short events;
    short revents;
};

#define UIO_MAXIOV 1024

struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct linux_sysinfo {
    int64_t  uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
    char _f[20 - 2 * sizeof(uint64_t) - sizeof(uint32_t)];
};