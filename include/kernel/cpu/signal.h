#ifndef _CPU_SIGNAL_H
#define _CPU_SIGNAL_H

#include <stdint.h>
#include <stddef.h>

struct syscall_saved_regs;
struct process;

#define SIG_BLOCK     0     /* Add set to current blocked mask */
#define SIG_UNBLOCK   1     /* Remove set from current blocked mask */
#define SIG_SETMASK   2     /* Replace current blocked mask with set */

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20

#define NSIG      32

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

typedef uint32_t sigset_t;
typedef uint64_t k_sigset_t;

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

typedef int32_t pid_t;

int kernel_send_signal(struct process *proc, int signum);
void handle_pending_signals(struct syscall_saved_regs *regs, int64_t *sys_ret_rax);

#endif