#include "cpu/signal.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "sched/spinlock.h"
#include "drivers/klog.h"
#include "mem/uaccess.h"
#include <string.h>

extern spinlock_t s_sched_lock;

int kernel_send_signal(process_t *proc, int signum) {
    if (!proc || signum <= 0 || signum >= NSIG) {
        return -1;
    }

    if (proc->sig_actions[signum].sa_handler == SIG_IGN) {
        return 0;
    }

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    proc->sig_pending |= (1U << signum);

    /* 1. Wake threads on wait queues (e.g., sys_wait4) */
    extern thread_t *s_wait_queue_head;
    thread_t *curr = s_wait_queue_head;
    while (curr) {
        if (curr->process == proc && curr->state == THREAD_BLOCKED) {
            curr->state = THREAD_READY;
            queue_push_unlocked(curr);
        }
        curr = curr->wait_next;
    }

    /* 2. Wake threads on sleep queues (e.g., sys_nanosleep / sleep_ms) */
    extern thread_t *s_sleep_queue_head;
    thread_t **sleep_ptr = &s_sleep_queue_head;
    while (*sleep_ptr) {
        thread_t *st = *sleep_ptr;
        if (st->process == proc) {
            *sleep_ptr = st->sleep_next;
            st->sleep_next = NULL;
            st->state = THREAD_READY;
            queue_push_unlocked(st);
        } else {
            sleep_ptr = &(*sleep_ptr)->sleep_next;
        }
    }

    spinlock_irqrestore(&s_sched_lock, flags);
    return 0;
}

void handle_pending_signals(struct syscall_saved_regs *regs, int64_t *sys_ret_rax) {
    if (!g_current_thread || !g_current_thread->process || !regs || !sys_ret_rax) {
        return;
    }

    process_t *proc = g_current_thread->process;

    /* Compute unmasked signals */
    uint32_t active = proc->sig_pending & ~proc->sig_blocked;
    if (!active) {
        return;
    }

    for (int sig = 1; sig < NSIG; sig++) {
        if (!(active & (1U << sig))) {
            continue;
        }

        /* Consume pending signal */
        proc->sig_pending &= ~(1U << sig);

        struct sigaction *act = &proc->sig_actions[sig];

        /* 1. Default action (SIG_DFL) */
        if (act->sa_handler == SIG_DFL) {
            if (sig == SIGINT || sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV) {
                klog_printf("[SIGNAL] Process PID %llu killed by signal %d\n", proc->pid, sig);
                sys_exit(128 + sig);
            }
            /* Other default signals like SIGCHLD are ignored */
            continue;
        }

        /* 2. Explicit ignore (SIG_IGN) */
        if (act->sa_handler == SIG_IGN) {
            continue;
        }

        /* 3. Custom user signal handler */

        /* Account for the 128-byte System V ABI Red Zone */
        uintptr_t user_rsp = regs->rsp - 128;

        /* Allocate space for saved context aligned to 16 bytes */
        user_rsp -= sizeof(ucontext_frame_t);
        user_rsp &= ~0xFULL;

        /* Verify user stack memory is accessible and writable */
        if (!validate_user_buffer((void *)(user_rsp - sizeof(uint64_t)),
                                  sizeof(ucontext_frame_t) + sizeof(uint64_t))) {
            klog_printf("[SIGNAL] Stack overflow/invalid stack for PID %llu\n", proc->pid);
            sys_exit(128 + SIGSEGV);
            return;
        }

        /* Save current user context, return value, and signal mask */
        ucontext_frame_t *ctx = (ucontext_frame_t *)user_rsp;
        memcpy(&ctx->regs, regs, sizeof(syscall_saved_regs_t));
        ctx->orig_rax = (uint64_t)*sys_ret_rax;
        ctx->saved_sigmask = proc->sig_blocked;

        /*
         * Push restorer address onto stack as the fake return address.
         * Subtracting 8 bytes leaves user_rsp % 16 == 8, matching standard
         * AMD64 function entry conventions after a CALL.
         */
        user_rsp -= sizeof(uint64_t);
        *(uint64_t *)user_rsp = (uint64_t)act->sa_restorer;

        /* Setup CPU state to enter the handler */
        regs->rsp = user_rsp;
        regs->rip = (uint64_t)act->sa_handler;
        regs->rflags = 0x202; /* IF enabled */
        regs->rdi = (uint64_t)sig; /* 1st argument (sig) */

        /* Mask configured signals and the active signal during execution */
        proc->sig_blocked |= (act->sa_mask | (1U << sig));
        return;
    }
}