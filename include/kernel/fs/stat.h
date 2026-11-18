#pragma once

#include <stdint.h>

/* File type bits (st_mode & S_IFMT) */
#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

/* Permission bits */
#define S_IRWXU  00700
#define S_IRUSR  00400
#define S_IWUSR  00200
#define S_IXUSR  00100
#define S_IRWXG  00070
#define S_IRGRP  00040
#define S_IWGRP  00020
#define S_IXGRP  00010
#define S_IRWXO  00007
#define S_IROTH  00004
#define S_IWOTH  00002
#define S_IXOTH  00001

/* AT_* constants for newfstatat (syscall 262) */
#define AT_FDCWD            -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EMPTY_PATH       0x1000

struct linux_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};



/* Exact 144-byte Linux x86_64 struct stat */
struct linux_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    struct linux_timeval st_atim;
    struct linux_timeval st_mtim;
    struct linux_timeval st_ctim;
    int64_t  __unused[3];
};