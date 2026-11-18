#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>

extern int64_t __syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3);

int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int fcntl(int fd, int cmd, ...) {
    uint64_t arg = 0;

    /* Some commands require an extra argument */
    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC || cmd == F_SETFD || 
        cmd == F_SETFL || cmd == F_GETLK || cmd == F_SETLK || 
        cmd == F_SETLKW || cmd == F_SETOWN || cmd == F_SETSIG) {
        va_list ap;
        va_start(ap, cmd);
        arg = va_arg(ap, uint64_t);
        va_end(ap);
    }

    return (int)__syscall3(72, (uint64_t)fd, (uint64_t)cmd, arg); /* SYS_fcntl = 72 */
}