#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* File access modes (value & O_ACCMODE) */
#define O_RDONLY        00000000
#define O_WRONLY        00000001
#define O_RDWR          00000002
#define O_ACCMODE       00000003

/* File creation and status flags */
#define O_CREAT         00000100
#define O_EXCL          00000200
#define O_NOCTTY        00000400
#define O_TRUNC         00001000
#define O_APPEND        00002000
#define O_NONBLOCK      00004000
#define O_NDELAY        O_NONBLOCK
#define O_SYNC          04010000
#define O_FSYNC         O_SYNC
#define O_ASYNC         00020000

/* Linux-specific flags commonly supported on x86_64 */
#define O_DIRECTORY     00200000
#define O_NOFOLLOW      00400000
#define O_CLOEXEC       02000000

/* fcntl commands (F_*) */
#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_GETLK         5
#define F_SETLK         6
#define F_SETLKW        7
#define F_SETOWN        8
#define F_GETOWN        9
#define F_SETSIG        10
#define F_GETSIG        11

#define F_DUPFD_CLOEXEC 1030

/* File descriptor flags (for F_GETFD / F_SETFD) */
#define FD_CLOEXEC      1

/* Record locking flags (for struct flock) */
#define F_RDLCK         0
#define F_WRLCK         1
#define F_UNLCK         2

/* Standard file permission mode bitmasks (for creat / open mode) */
#define S_ISUID         0004000
#define S_ISGID         0002000
#define S_ISVTX         0001000

#include <sys/stat.h>

/* Advisory file record lock structure */
struct flock {
    short  l_type;      /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
    short  l_whence;    /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
    off_t  l_start;     /* Starting offset for lock */
    off_t  l_len;       /* Number of bytes to lock; 0 means to EOF */
    pid_t  l_pid;       /* PID of process blocking our lock (F_GETLK only) */
};

/* Core POSIX function prototypes */
int creat(const char *pathname, mode_t mode);
int fcntl(int fd, int cmd, ...);

#endif /* _FCNTL_H */