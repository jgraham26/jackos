#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int64_t read(int fd, void *buf, size_t count);
int64_t write(int fd, const void *buf, size_t count);
int64_t open(const char *path, int flags, ...);
int64_t close(int fd);
pid_t   fork(void);
int     execve(const char *path, char *const argv[], char *const envp[]);
uintptr_t brk(uintptr_t addr);
void   *sbrk(intptr_t increment);
int     sched_yield(void);

int access(const char *pathname, int mode);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *pathname);

int chown(const char *pathname, uid_t owner, gid_t group);
int lchown(const char *pathname, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);

extern char *optarg;
extern int   optind;
extern int   opterr;
extern int   optopt;

int link(const char *oldpath, const char *newpath);
int     symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

#define F_OK 0 /* Test for existence */
#define X_OK 1 /* Test for execute permission */
#define W_OK 2 /* Test for write permission */
#define R_OK 4 /* Test for read permission */

#ifndef SEEK_SET
#define SEEK_SET 0 /* Seek relative to the beginning of the file */
#define SEEK_CUR 1 /* Seek relative to current file offset */
#define SEEK_END 2 /* Seek relative to end of file */
#endif

#endif