#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <stdint.h>
#include <sys/types.h>

#define SIGHUP       1
#define SIGINT       2
#define SIGQUIT      3
#define SIGILL       4
#define SIGTRAP      5
#define SIGABRT      6
#define SIGIOT       6
#define SIGBUS       7
#define SIGFPE       8
#define SIGKILL      9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGXCPU     24   /* CPU time limit exceeded */
#define SIGXFSZ     25   /* File size limit exceeded */
#define SIGVTALRM   26   /* Virtual timer expired (ITIMER_VIRTUAL) */
#define SIGPROF     27   /* Profiling timer expired (ITIMER_PROF) */
#define SIGWINCH    28
#define SIGIO       29
#define SIGPOLL     SIGIO
#define SIGPWR      30
#define SIGSYS      31
#define SIGUNUSED   31

#define _NSIG       64   /* Total signals including real-time signals */

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

typedef uint32_t sigset_t;

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

int kill(pid_t pid, int sig);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
sighandler_t signal(int signum, sighandler_t handler);

#endif /* _SIGNAL_H */