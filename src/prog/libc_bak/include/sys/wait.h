#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

/* Options for waitpid / wait4 */
#define WNOHANG     1   /* Return immediately if no child has exited */
#define WUNTRACED   2   /* Report status of stopped children */
#define WCONTINUED  8   /* Report status of continued children */

/*
 * Process termination macros
 *
 * Your kernel stores the 8-bit exit code shifted left by 8 bits:
 *   proc->exit_status = (status & 0xFF) << 8;
 * Low 7 bits represent the terminating signal (0 for normal exit).
 */

/* True if the child terminated normally (e.g. via exit() or returning from main) */
#define WIFEXITED(status)       (((status) & 0x7F) == 0)

/* Extracts the 8-bit exit status passed to exit() */
#define WEXITSTATUS(status)     (((status) >> 8) & 0xFF)

/* True if the child process was terminated by an unhandled signal */
#define WIFSIGNALED(status)     (((signed char)(((status) & 0x7F) + 1) >> 1) > 0)

/* Extracts the signal number that caused the child process to terminate */
#define WTERMSIG(status)        ((status) & 0x7F)

/* True if the process generated a core dump */
#define WCOREDUMP(status)       ((status) & 0x80)

/* True if the child process is currently stopped */
#define WIFSTOPPED(status)      (((status) & 0xFF) == 0x7F)

/* Extracts the signal number that caused the child to stop */
#define WSTOPSIG(status)        WEXITSTATUS(status)

/* True if the child was resumed by delivery of SIGCONT */
#define WIFCONTINUED(status)    ((status) == 0xFFFF)

/* Function prototypes */
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
pid_t wait4(pid_t pid, int *wstatus, int options, void *rusage);

#endif /* _SYS_WAIT_H */