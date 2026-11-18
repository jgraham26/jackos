#pragma once

#include <stdint.h>
#include "cpu/idt.h"
#include "mem/vmm.h"
#include "mem/kstack.h"
#include "sched/process.h"

#define KERNEL_STACK_SIZE (16 * 1024) /* 16 KiB */
#define USER_STACK_VIRT_TOP 0x00007FFFFFFFE000ULL
#define USER_CODE_VIRT_ADDR 0x0000000000400000ULL

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
} thread_state_t;

typedef struct thread {
    uint64_t tid;
    thread_state_t state;
    interrupt_frame_t* rsp;
    kstack_t kstack;
    vmm_pagemap_t* pagemap;
    struct thread* next;
    struct thread* zombie_next;

    // sleep queue
    uint64_t            wake_tick;
    struct thread      *sleep_next;

    uint8_t             fpu_state[512] __attribute__((aligned(16)));

    uint64_t fs_base;

    int* clear_child_tid;

    struct process* process;

    struct thread *wait_next;
    int64_t wait_pid;      /* Target child PID, or -1 for any child */
} thread_t;

void sched_init(void);
thread_t *thread_create(void (*entry_point)(void), vmm_pagemap_t *pagemap);
thread_t* thread_create_user(void (*entry_point)(), vmm_pagemap_t* pagemap, struct process* proc, uintptr_t initial_user_rsp);
interrupt_frame_t *sched_schedule(interrupt_frame_t *current_frame);
void sched_yield(void);
void thread_exit(void);
void sched_wake_waiters_unlocked(struct process *parent, uint64_t child_pid);
void queue_push_unlocked(thread_t *t);

extern thread_t* g_current_thread;
extern thread_t *s_wait_queue_head;
extern thread_t *s_sleep_queue_head;