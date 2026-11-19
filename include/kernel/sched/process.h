#pragma once

#include <stdint.h>
#include "mem/vmm.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "cpu/signal.h"

struct sigaction;

#define MAX_FDS 64
#define WNOHANG 1
#define MMAP_BASE_ADDR 0x0000100000000000ULL

#define O_CLOEXEC 02000000
#define FD_CLOEXEC 1

typedef enum {
    PROC_ACTIVE,
    PROC_ZOMBIE,
    PROC_DEAD
} proc_state_t;

typedef struct process {
    uint64_t pid;
    uint64_t pgid;
    proc_state_t state;
    int exit_status;

    vmm_pagemap_t *pagemap;
    uintptr_t heap_start;
    uintptr_t heap_end;
    uintptr_t heap_limit;
    uintptr_t mmap_next;
    void *cwd;
    char cwd_path[VFS_MAX_PATH];
    vfs_file_t *fd_table[MAX_FDS];
    uint8_t fd_flags[MAX_FDS];

    /* Parent-child tracking */
    struct process *parent;
    struct process *children_head;
    struct process *sibling_next;
    struct process *foreground_child;
    struct process *global_next;

    /* Global process list link */
    struct process *next;

    struct sigaction sig_actions[NSIG];
    uint32_t         sig_pending;
    uint32_t         sig_blocked;

    char comm[32];
    char cmdline[256];
    size_t cmdline_len;
    vma_t *vma_list;
} process_t;


typedef struct syscall_saved_regs {
    uint64_t rax;       /* +0   */
    uint64_t rdx;       /* +8   */
    uint64_t rsi;       /* +16  */
    uint64_t rdi;       /* +24  */
    uint64_t r8;        /* +32  */
    uint64_t r9;        /* +40  */
    uint64_t r10;       /* +48  */
    uint64_t r12;       /* +56  */
    uint64_t r13;       /* +64  */
    uint64_t r14;       /* +72  */
    uint64_t r15;       /* +80  */
    uint64_t rbx;       /* +88  */
    uint64_t rbp;       /* +96  */
    uint64_t rip;       /* +104 */
    uint64_t rflags;    /* +112 */
    uint64_t rsp;       /* +120 */
} __attribute__((packed)) syscall_saved_regs_t;

typedef struct ucontext_frame {
    struct syscall_saved_regs regs;
    uint64_t             orig_rax;
    uint32_t             saved_sigmask;
} ucontext_frame_t;

process_t* process_create();
void process_destroy(process_t* proc);
void spawn_isolated_user_process(const uint8_t *code, size_t code_size);
int64_t sys_fork(syscall_saved_regs_t *regs);
int64_t sys_wait4(int64_t pid, int *wstatus, int options, void *rusage);
int64_t sys_exit(int status);
int64_t sys_execve(const char *path, char *const argv[], char *const envp[], syscall_saved_regs_t *regs);