#include <stdint.h>
#include <string.h>
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "io.h"
#include "cpu/signal.h"
#include "mem/kstack.h"
#include <stdbool.h>
#include "sched/process.h"
#include "sched/spinlock.h"
#include "sched/sched.h"
#include "kernel.h"
#include "mem/kheap.h"
#include "mem/vmm.h"
#include "cpu/fpu.h"
#include "cpu/syscall.h"
#include "drivers/klog.h"
#include "cpu/percpu.h"

#define MSR_FS_BASE 0xC0000100

extern void idle(void);
extern void sched_switch_to_frame(interrupt_frame_t *frame);

static thread_t *ready_queue_head = NULL;
static thread_t *ready_queue_tail = NULL;
thread_t *s_wait_queue_head = NULL;
thread_t *g_current_thread = NULL;
uint64_t next_tid = 1;
static thread_t *s_dead_threads_head = NULL;
thread_t *s_sleep_queue_head = NULL;

static thread_t *idle_thread = NULL;

spinlock_t s_sched_lock = SPINLOCK_INIT;

extern volatile uint64_t g_ticks;

/* Internal helpers - Caller MUST hold s_sched_lock */
void queue_push_unlocked(thread_t *t) {
    t->next = NULL;
    if (!ready_queue_head) {
        ready_queue_head = t;
        ready_queue_tail = t;
    } else {
        ready_queue_tail->next = t;
        ready_queue_tail = t;
    }
}

void sched_wake_waiters_unlocked(process_t *parent, uint64_t child_pid) {
    thread_t **curr_ptr = &s_wait_queue_head;

    while (*curr_ptr) {
        thread_t *t = *curr_ptr;
        if (t->process == parent && (t->wait_pid == -1 || t->wait_pid == (int64_t)child_pid)) {
            *curr_ptr = t->wait_next;
            t->wait_next = NULL;
            t->state = THREAD_READY;
            queue_push_unlocked(t);
        } else {
            curr_ptr = &(*curr_ptr)->wait_next;
        }
    }
}

static thread_t *queue_pop_unlocked(void) {
    if (!ready_queue_head) return NULL;
    thread_t *t = ready_queue_head;
    ready_queue_head = ready_queue_head->next;
    if (!ready_queue_head) {
        ready_queue_tail = NULL;
    }
    t->next = NULL;
    return t;
}

static void sched_wake_sleeping_threads_unlocked(void) {
    thread_t **curr_ptr = &s_sleep_queue_head;

    while (*curr_ptr) {
        thread_t *t = *curr_ptr;

        if (g_ticks >= t->wake_tick) {
            *curr_ptr = t->sleep_next;
            t->sleep_next = NULL;

            t->state = THREAD_READY;
            queue_push_unlocked(t);
        } else {
            curr_ptr = &(*curr_ptr)->sleep_next;
        }
    }
}

void sleep_ms(uint32_t ms) {
    if (ms == 0) {
        sched_yield();
        return;
    }

    uint64_t flags = spinlock_irqsave(&s_sched_lock);

    g_current_thread->wake_tick = g_ticks + ms;
    g_current_thread->state = THREAD_BLOCKED;

    g_current_thread->sleep_next = s_sleep_queue_head;
    s_sleep_queue_head = g_current_thread;

    spinlock_irqrestore(&s_sched_lock, flags);

    sched_yield();
}

/* Frees dead threads. ONLY call when running on an active, non-dying thread stack */
static void sched_reap_dead_threads(void) {
    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    thread_t *curr = s_dead_threads_head;
    s_dead_threads_head = NULL;
    spinlock_irqrestore(&s_sched_lock, flags);

    while (curr) {
        thread_t *next = curr->zombie_next;
        /* Never free the stack if it happens to be the currently executing one */
        if (curr != g_current_thread) {
            if (curr->kstack.base) {
                kstack_free(&curr->kstack);
            }
            kfree(curr);
        }
        curr = next;
    }
}

void sched_unblock_thread(thread_t *t) {
    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    if (t->state == THREAD_BLOCKED) {
        t->state = THREAD_READY;
        queue_push_unlocked(t);
    }
    spinlock_irqrestore(&s_sched_lock, flags);
}

void thread_exit(void) {
    __asm__ volatile("cli");

    if (g_current_thread->clear_child_tid && g_current_thread->pagemap) {
        if (vmm_read_cr3() == g_current_thread->pagemap->pml4_phys) {
            *g_current_thread->clear_child_tid = 0;
        }
        g_current_thread->clear_child_tid = NULL;
    }

    uint64_t flags = spinlock_irqsave(&s_sched_lock);

    thread_t *dead_thread = g_current_thread;
    dead_thread->state = THREAD_DEAD;

    /* Enqueue to dead list for deferred reaping */
    dead_thread->zombie_next = s_dead_threads_head;
    s_dead_threads_head = dead_thread;
    g_current_thread = NULL;

    /* Dequeue next ready thread, or fall back to idle */
    thread_t *next = queue_pop_unlocked();
    if (!next) {
        next = idle_thread;
    }

    next->state = THREAD_RUNNING;
    g_current_thread = next;

    /* Release lock BEFORE switching stacks */
    spinlock_irqrestore(&s_sched_lock, flags);

    /* Update kernel stack for user interrupts & syscalls */
    if (next->kstack.top) {
        gdt_set_kernel_stack(next->kstack.top);
        g_bsp_cpu.kernel_rsp = next->kstack.top;
        g_syscall_kernel_rsp = next->kstack.top;
    }

    /* Switch address space if necessary */
    if (vmm_read_cr3() != next->pagemap->pml4_phys) {
        vmm_switch_pagemap(next->pagemap);
    }

    /* Restore target thread's FPU/SSE state */
    fpu_restore(g_current_thread->fpu_state);
    wrmsr(MSR_FS_BASE, next->fs_base);

    sched_switch_to_frame(next->rsp);
}

thread_t *thread_create(void (*entry_point)(void), vmm_pagemap_t *pagemap) {
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(thread_t));

    if (kstack_alloc(&t->kstack) != 0) {
        kfree(t);
        return NULL;
    }
    memset((void *)t->kstack.base, 0, KERNEL_STACK_SIZE);

    *(uint16_t *)(&t->fpu_state[0]) = 0x037F;
    *(uint32_t *)(&t->fpu_state[24]) = 0x1F80;

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    t->tid = next_tid++;
    t->state = THREAD_READY;
    t->pagemap = pagemap ? pagemap : &kernel_pagemap;

    /* 1. Start from top of guarded stack and align down to 16 bytes */
    uintptr_t stack_top = t->kstack.top & ~0xFULL;

    /* 2. Push thread_exit return address (8 bytes), keeping 16-byte alignment before call */
    stack_top -= sizeof(uint64_t);
    *(uint64_t *)stack_top = (uint64_t)thread_exit;

    /* Save this as the target RSP when iretq returns to entry_point */
    uint64_t target_rsp = stack_top;

    /* 3. Reserve space for the interrupt frame below target_rsp */
    stack_top -= sizeof(interrupt_frame_t);
    interrupt_frame_t *frame = (interrupt_frame_t *)stack_top;
    memset(frame, 0, sizeof(interrupt_frame_t));

    frame->rip = (uint64_t)entry_point;
    frame->cs = 0x08;                         /* Kernel Code Segment */
    frame->rflags = 0x202;                    /* IF enabled + bit 1 reserved */
    frame->rsp = target_rsp;                  /* Real stack pointer upon iretq */
    frame->ss = 0x10;                         /* Kernel Data Segment */

    t->rsp = frame;

    if (entry_point != idle) {
        queue_push_unlocked(t);
    }
    spinlock_irqrestore(&s_sched_lock, flags);

    return t;
}

void sched_init(void) {
    thread_t *boot_thread = (thread_t *)kmalloc(sizeof(thread_t));
    memset(boot_thread, 0, sizeof(thread_t));
    boot_thread->tid = 0;
    boot_thread->state = THREAD_RUNNING;
    boot_thread->pagemap = &kernel_pagemap;

    g_current_thread = boot_thread;

    idle_thread = thread_create(idle, &kernel_pagemap);
}

interrupt_frame_t *sched_schedule(interrupt_frame_t *current_frame) {
    if (!g_current_thread) {
        return current_frame;
    }

    g_current_thread->rsp = current_frame;
    fpu_save(g_current_thread->fpu_state);

    g_current_thread->fs_base = rdmsr(MSR_FS_BASE);

    uint64_t flags = spinlock_irqsave(&s_sched_lock);

    /* 1. Wake any expired sleeping threads */
    sched_wake_sleeping_threads_unlocked();

    /* 2. Handle outgoing thread state */
    if (g_current_thread->state == THREAD_RUNNING && g_current_thread != idle_thread) {
        g_current_thread->state = THREAD_READY;
        queue_push_unlocked(g_current_thread);
    } else if (g_current_thread->state == THREAD_DEAD) {
        g_current_thread->zombie_next = s_dead_threads_head;
        s_dead_threads_head = g_current_thread;
        g_current_thread = NULL;
    }

    /* 3. Pick next ready thread or fallback to idle */
    thread_t *next_thread = queue_pop_unlocked();
    if (!next_thread) {
        next_thread = idle_thread;
    }


    next_thread->state = THREAD_RUNNING;
    g_current_thread = next_thread;

    spinlock_irqrestore(&s_sched_lock, flags);

    if (next_thread->kstack.top) {
        gdt_set_kernel_stack(next_thread->kstack.top);
        g_bsp_cpu.kernel_rsp = next_thread->kstack.top;
        g_syscall_kernel_rsp = next_thread->kstack.top;
    }

    if (vmm_read_cr3() != next_thread->pagemap->pml4_phys) {
        vmm_switch_pagemap(next_thread->pagemap);
    }

    fpu_restore(g_current_thread->fpu_state);

    wrmsr(MSR_FS_BASE, next_thread->fs_base);

    sched_reap_dead_threads();

    if (next_thread->process && (next_thread->rsp->cs & 0x03) == 0x03) {
        process_t *proc = next_thread->process;
        uint32_t active = proc->sig_pending & ~proc->sig_blocked;

        if (active) {
            for (int sig = 1; sig < NSIG; sig++) {
                if (!(active & (1U << sig))) continue;

                /* Default action for termination signals */
                if (proc->sig_actions[sig].sa_handler == SIG_DFL) {
                    if (sig == SIGINT || sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV) {
                        proc->sig_pending &= ~(1U << sig);
                        klog_printf("[SIGNAL] Process PID %llu killed by signal %d via scheduler\n",
                                      proc->pid, sig);
                        sys_exit(128 + sig);
                    }
                }
            }
        }
    }
    
    return next_thread->rsp;
}

void sched_yield(void) {
    __asm__ volatile("int $32");
}

thread_t *thread_create_user(void (*entry_point)(), vmm_pagemap_t *pagemap, struct process *proc, uintptr_t initial_user_rsp) {
    if (!pagemap) return NULL;

    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(thread_t));

    if (kstack_alloc(&t->kstack) != 0) {
        kfree(t);
        return NULL;
    }
    memset((void *)t->kstack.base, 0, KERNEL_STACK_SIZE);

    *(uint16_t *)(&t->fpu_state[0]) = 0x037F;
    *(uint32_t *)(&t->fpu_state[24]) = 0x1F80;

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    t->tid = next_tid++;
    t->state = THREAD_READY;
    t->pagemap = pagemap;
    t->process = proc;

    uintptr_t kstack_top = t->kstack.top & ~0xFULL;

    kstack_top -= sizeof(interrupt_frame_t);
    interrupt_frame_t *frame = (interrupt_frame_t *)kstack_top;
    memset(frame, 0, sizeof(interrupt_frame_t));

    frame->rip = (uint64_t)entry_point;
    frame->cs = USER_CS_SELECTOR;
    frame->rflags = 0x202;
    frame->rsp = initial_user_rsp;
    frame->ss = USER_DS_SELECTOR;

    t->rsp = frame;
    t->fs_base = 0;

    queue_push_unlocked(t);
    spinlock_irqrestore(&s_sched_lock, flags);

    return t;
}