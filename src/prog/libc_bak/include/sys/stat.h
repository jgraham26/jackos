#ifndef _SYS_STAT_H
#define _SYS_STAT_H 1

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fallback integer types if not provided by <sys/types.h> */
#ifndef _DEV_T_DECLARED
typedef unsigned long  dev_t;
#define _DEV_T_DECLARED
#endif

#ifndef _INO_T_DECLARED
typedef unsigned long  ino_t;
#define _INO_T_DECLARED
#endif

#ifndef _MODE_T_DECLARED
typedef unsigned int   mode_t;
#define _MODE_T_DECLARED
#endif

#ifndef _NLINK_T_DECLARED
typedef unsigned long  nlink_t;
#define _NLINK_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef unsigned int   uid_t;
#define _UID_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
typedef unsigned int   gid_t;
#define _GID_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef long           off_t;
#define _OFF_T_DECLARED
#endif

#ifndef _BLKSIZE_T_DECLARED
typedef long           blksize_t;
#define _BLKSIZE_T_DECLARED
#endif

#ifndef _BLKCNT_T_DECLARED
typedef long           blkcnt_t;
#define _BLKCNT_T_DECLARED
#endif

/* Standard x86_64 struct stat definition */
struct stat {
    dev_t           st_dev;      /* Device ID containing file */
    ino_t           st_ino;      /* Inode number */
    nlink_t         st_nlink;    /* Hard link count */
    mode_t          st_mode;     /* File protection and type */
    uid_t           st_uid;      /* User ID of owner */
    gid_t           st_gid;      /* Group ID of owner */
    unsigned int    __pad0;
    dev_t           st_rdev;     /* Device ID (if special file) */
    off_t           st_size;     /* Total size in bytes */
    blksize_t       st_blksize;  /* Blocksize for filesystem I/O */
    blkcnt_t        st_blocks;   /* Number of 512-byte blocks allocated */

    /* Timestamp structures with nanosecond precision */
    struct timespec st_atim;     /* Time of last access */
    struct timespec st_mtim;     /* Time of last modification */
    struct timespec st_ctim;     /* Time of last status change */

    long            __reserved[3];
};

/* POSIX backwards-compatibility aliases */
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

/* File type bitmask */
#define S_IFMT   00170000

/* File types */
#define S_IFSOCK 0140000 /* Socket */
#define S_IFLNK  0120000 /* Symbolic link */
#define S_IFREG  0100000 /* Regular file */
#define S_IFBLK  0060000 /* Block device */
#define S_IFDIR  0040000 /* Directory */
#define S_IFCHR  0020000 /* Character device */
#define S_IFIFO  0010000 /* FIFO / named pipe */

/* File type test macros */
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

/* Special execution permissions */
#define S_ISUID 0004000 /* Set user ID on execution */
#define S_ISGID 0002000 /* Set group ID on execution */
#define S_ISVTX 0001000 /* Sticky bit (save swapped text / restricted deletion) */

/* File access permissions */
#define S_IRWXU 00700   /* User (file owner) has read, write, and execute */
#define S_IRUSR 00400   /* User has read permission */
#define S_IWUSR 00200   /* User has write permission */
#define S_IXUSR 00100   /* User has execute permission */

#define S_IRWXG 00070   /* Group has read, write, and execute */
#define S_IRGRP 00040   /* Group has read permission */
#define S_IWGRP 00020   /* Group has write permission */
#define S_IXGRP 00010   /* Group has execute permission */

#define S_IRWXO 00007   /* Others have read, write, and execute */
#define S_IROTH 00004   /* Others have read permission */
#define S_IWOTH 00002   /* Others have write permission */
#define S_IXOTH 00001   /* Others have execute permission */

/* UTIME flags for utimensat */
#define UTIME_NOW  ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)

/* Standard function declarations */
int    stat(const char *path, struct stat *buf);
int    fstat(int fd, struct stat *buf);
int    lstat(const char *path, struct stat *buf);
int    fstatat(int dirfd, const char *pathname, struct stat *buf, int flags);

int    chmod(const char *path, mode_t mode);
int    fchmod(int fd, mode_t mode);
int    fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);

int    mkdir(const char *path, mode_t mode);
int    mkdirat(int dirfd, const char *pathname, mode_t mode);

int    mkfifo(const char *path, mode_t mode);
int    mkfifoat(int dirfd, const char *pathname, mode_t mode);

int    mknod(const char *path, mode_t mode, dev_t dev);
int    mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);

mode_t umask(mode_t mask);

int    futimens(int fd, const struct timespec times[2]);
int    utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STAT_H */