#include <string.h>
#include <stdio.h>
#include "sched/process.h"
#include "sched/spinlock.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "mem/kheap.h"
#include "mem/vmm.h"
#include "mem/pmm.h"
#include "mem/uaccess.h"
#include "cpu/gdt.h"
#include "cpu/signal.h"
#include "boot/elf.h"
#include "io.h"
#include "mem/kstack.h"
#include "stdbool.h"
#include <errno.h>

#define WNOHANG 1

#define ENOENT  2
#define ENOEXEC 8
#define ECHILD  10
#define ENOMEM  12
#define EFAULT  14
#define EINVAL  22


static uint64_t s_next_pid = 1;
process_t *g_process_list_head = NULL;

extern spinlock_t s_sched_lock;
extern void queue_push_unlocked(thread_t *t);
extern uint64_t next_tid;
extern thread_t *s_wait_queue_head;
extern void sched_wake_waiters_unlocked(process_t *parent, uint64_t child_pid);
extern vfs_node_t *s_root_node;

/* Helper: resolve relative paths against proc->cwd_path */
static void resolve_proc_path(process_t *proc, const char *input, char *out, size_t out_size) {
    if (input[0] == '/') {
        strncpy(out, input, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    const char *base_cwd = (proc->cwd_path[0] != '\0') ? proc->cwd_path : "/";
    strncpy(out, base_cwd, out_size - 1);
    out[out_size - 1] = '\0';

    if (strcmp(input, ".") == 0 || strcmp(input, "./") == 0) {
        return;
    }

    size_t clen = strlen(out);
    if (clen > 0 && out[clen - 1] != '/') {
        strncat(out, "/", out_size - strlen(out) - 1);
    }

    const char *sub = input;
    if (sub[0] == '.' && sub[1] == '/') {
        sub += 2;
    }
    strncat(out, sub, out_size - strlen(out) - 1);
}

#define MAX_SHEBANG_DEPTH 4
#define MAX_SHEBANG_LINE  256

/*
 * Parses "#!<interpreter> [optional_arg]\n" from file_buf.
 * Returns true if a valid shebang line was found, populating interp_out and arg_out.
 */
static bool parse_shebang(const uint8_t *buf, size_t size,
                          char *interp_out, size_t interp_max,
                          char *arg_out, size_t arg_max, bool *has_arg) {
    if (!buf || size < 3 || buf[0] != '#' || buf[1] != '!') {
        return false;
    }

    *has_arg = false;
    interp_out[0] = '\0';
    arg_out[0] = '\0';

    /* Find end of first line (capped at MAX_SHEBANG_LINE) */
    size_t limit = (size < MAX_SHEBANG_LINE) ? size : MAX_SHEBANG_LINE;
    size_t eol = 2;
    while (eol < limit && buf[eol] != '\n' && buf[eol] != '\r') {
        eol++;
    }

    /* Skip leading whitespace after "#!" */
    size_t pos = 2;
    while (pos < eol && (buf[pos] == ' ' || buf[pos] == '\t')) {
        pos++;
    }
    if (pos >= eol) {
        return false; /* Empty "#!" line */
    }

    /* Extract interpreter path */
    size_t interp_start = pos;
    while (pos < eol && buf[pos] != ' ' && buf[pos] != '\t') {
        pos++;
    }
    size_t interp_len = pos - interp_start;
    if (interp_len == 0 || interp_len >= interp_max) {
        return false;
    }
    memcpy(interp_out, buf + interp_start, interp_len);
    interp_out[interp_len] = '\0';

    /* Skip whitespace between interpreter and optional single argument */
    while (pos < eol && (buf[pos] == ' ' || buf[pos] == '\t')) {
        pos++;
    }

    /* Trim trailing whitespace from optional argument (Linux binfmt_script behavior) */
    size_t end = eol;
    while (end > pos && (buf[end - 1] == ' ' || buf[end - 1] == '\t')) {
        end--;
    }

    if (end > pos) {
        size_t arg_len = end - pos;
        if (arg_len >= arg_max) {
            return false;
        }
        memcpy(arg_out, buf + pos, arg_len);
        arg_out[arg_len] = '\0';
        *has_arg = true;
    }

    return true;
}

process_t *process_create() {
    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) return NULL;
    memset(proc, 0, sizeof(process_t));

    strcpy(proc->cwd_path, "/");
    proc->cwd = s_root_node;

    proc->pagemap = vmm_new_pagemap();
    if (!proc->pagemap) {
        kfree(proc);
        return NULL;
    }

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    proc->pid = s_next_pid++;
    proc->pgid = proc->pid;
    proc->global_next = g_process_list_head;
    g_process_list_head = proc;
    spinlock_irqrestore(&s_sched_lock, flags);

    proc->state = PROC_ACTIVE;
    proc->heap_start = 0x0000000040000000ULL;
    proc->heap_end = proc->heap_start;
    proc->heap_limit = 0x0000700000000000ULL;
    proc->mmap_next = MMAP_BASE_ADDR;
    proc->pgid = proc->pid;

    proc->fd_table[0] = vfs_open("/dev/console", O_RDONLY);
    proc->fd_table[1] = vfs_open("/dev/console", O_WRONLY);
    proc->fd_table[2] = vfs_open("/dev/console", O_WRONLY);

    return proc;
}

void process_destroy(process_t *proc) {
    if (!proc) return;

    /* Close any remaining open file descriptors */
    for (int i = 0; i < MAX_FDS; i++) {
        if (proc->fd_table[i]) {
            vfs_close(proc->fd_table[i]);
            proc->fd_table[i] = NULL;
        }
    }

    if (proc->pagemap) {
        vmm_destroy_pagemap(proc->pagemap);
        kfree(proc->pagemap);
    }

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    process_t **curr = &g_process_list_head;
    while (*curr) {
        if (*curr == proc) {
            *curr = proc->global_next;
            break;
        }
        curr = &(*curr)->global_next;
    }
    spinlock_irqrestore(&s_sched_lock, flags);

    kfree(proc);
}

void spawn_isolated_user_process(const uint8_t *code, size_t code_size) {
    process_t *proc = process_create();
    if (!proc) return;

    uintptr_t code_phys = pmm_alloc_frame();
    void *code_hhdm = (void *)hhdm_physical_to_virtual(code_phys);
    memset(code_hhdm, 0, PAGE_SIZE);
    memcpy(code_hhdm, code, code_size);

    vmm_map_page(proc->pagemap, USER_CODE_VIRT_ADDR, code_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    uintptr_t stack_phys = pmm_alloc_frame();
    vmm_map_page(proc->pagemap, USER_STACK_VIRT_TOP - PAGE_SIZE, stack_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    thread_create_user((void (*)())USER_CODE_VIRT_ADDR, proc->pagemap, proc, USER_STACK_VIRT_TOP - 16);
}

int64_t sys_fork(syscall_saved_regs_t *regs) {
    if (!g_current_thread || !g_current_thread->process || !regs) {
        return -EINVAL;
    }

    process_t *parent_proc = g_current_thread->process;

    /* 1. Allocate structures up front before linking into global/parent lists */
    process_t *child_proc = (process_t *)kmalloc(sizeof(process_t));
    if (!child_proc) return -ENOMEM;
    memset(child_proc, 0, sizeof(process_t));

    child_proc->pagemap = vmm_clone_pagemap(parent_proc->pagemap);
    if (!child_proc->pagemap) {
        kfree(child_proc);
        return -ENOMEM;
    }

    thread_t *child_thread = (thread_t *)kmalloc(sizeof(thread_t));
    if (!child_thread) {
        vmm_destroy_pagemap(child_proc->pagemap);
        kfree(child_proc->pagemap);
        kfree(child_proc);
        return -ENOMEM;
    }
    memset(child_thread, 0, sizeof(thread_t));

    if (kstack_alloc(&child_thread->kstack) != 0) {
        kfree(child_thread);
        vmm_destroy_pagemap(child_proc->pagemap);
        kfree(child_proc->pagemap);
        kfree(child_proc);
        return -ENOMEM;
    }
    memset((void *)child_thread->kstack.base, 0, KERNEL_STACK_SIZE);

    /* 2. Copy process metadata & POSIX signal state */
    child_proc->heap_start = parent_proc->heap_start;
    child_proc->heap_end   = parent_proc->heap_end;
    child_proc->heap_limit = parent_proc->heap_limit;
    child_proc->mmap_next  = parent_proc->mmap_next;
    child_proc->cwd        = parent_proc->cwd;
    child_proc->parent     = parent_proc;
    child_proc->state      = PROC_ACTIVE;
    child_proc->exit_status = 0;
    child_proc->pgid       = parent_proc->pgid;
    memcpy(child_proc->comm, parent_proc->comm, sizeof(child_proc->comm));
    memcpy(child_proc->cmdline, parent_proc->cmdline, sizeof(child_proc->cmdline));
    child_proc->cmdline_len = parent_proc->cmdline_len;

    strncpy(child_proc->cwd_path, parent_proc->cwd_path, sizeof(child_proc->cwd_path) - 1);
    child_proc->cwd_path[sizeof(child_proc->cwd_path) - 1] = '\0';

    /* POSIX: Child inherits signal handlers and blocked mask, pending signals cleared */
    memcpy(child_proc->sig_actions, parent_proc->sig_actions, sizeof(parent_proc->sig_actions));
    child_proc->sig_blocked = parent_proc->sig_blocked;
    child_proc->sig_pending = 0;

    /* 3. Clone file descriptors */
    for (int i = 0; i < MAX_FDS; i++) {
        child_proc->fd_table[i] = parent_proc->fd_table[i];
        child_proc->fd_flags[i] = parent_proc->fd_flags[i];
        if (child_proc->fd_table[i]) {
            child_proc->fd_table[i]->refcount++;
        }
    }

    /* 4. Configure child thread */
    memcpy(child_thread->fpu_state, g_current_thread->fpu_state, sizeof(child_thread->fpu_state));
    child_thread->state           = THREAD_READY;
    child_thread->pagemap         = child_proc->pagemap;
    child_thread->process         = child_proc;
    child_thread->fs_base         = g_current_thread->fs_base;
    child_thread->clear_child_tid = NULL;

    /* 5. Populate interrupt frame for return to userland */
    uintptr_t kstack_top = child_thread->kstack.top & ~0xFULL;
    kstack_top -= sizeof(interrupt_frame_t);

    interrupt_frame_t *child_frame = (interrupt_frame_t *)kstack_top;
    
    memset(child_frame, 0, sizeof(interrupt_frame_t));

    child_frame->rbp    = regs->rbp;
    child_frame->rbx    = regs->rbx;
    child_frame->r12    = regs->r12;
    child_frame->r13    = regs->r13;
    child_frame->r14    = regs->r14;
    child_frame->r15    = regs->r15;
    child_frame->rip    = regs->rip;
    child_frame->cs     = USER_CS_SELECTOR;
    child_frame->rflags = regs->rflags | 0x202;
    child_frame->rsp    = regs->rsp;
    child_frame->ss     = USER_DS_SELECTOR;
    child_frame->rax    = 0; /* Child returns 0 */

    child_thread->rsp = child_frame;

    /* 6. Commit atomically: assign PID/TID, link to parent, and enqueue */
    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    uint64_t new_pid = s_next_pid++;
    child_proc->pid = new_pid;
    child_thread->tid = next_tid++;

    child_proc->global_next = g_process_list_head;
    g_process_list_head = child_proc;

    child_proc->sibling_next = parent_proc->children_head;
    parent_proc->children_head = child_proc;

    queue_push_unlocked(child_thread);
    spinlock_irqrestore(&s_sched_lock, flags);

    return (int64_t)new_pid;
}

int64_t sys_exit(int status) {
    __asm__ volatile("cli");

    process_t *proc = g_current_thread ? g_current_thread->process : NULL;
    if (proc) {
        /* Close open file descriptors immediately so pipes/consoles see EOF */
        for (int i = 0; i < MAX_FDS; i++) {
            if (proc->fd_table[i]) {
                vfs_close(proc->fd_table[i]);
                proc->fd_table[i] = NULL;
            }
        }

        uint64_t flags = spinlock_irqsave(&s_sched_lock);
        proc->exit_status = (status & 0xFF) << 8;
        proc->state = PROC_ZOMBIE;

        /* Detach any living children so they don't reference a freed parent later */
        process_t *child = proc->children_head;
        while (child) {
            process_t *next_child = child->sibling_next;
            child->parent = NULL;
            child->sibling_next = NULL;
            if (child->state == PROC_ZOMBIE) {
                /* Reap orphaned zombies */
                process_destroy(child);
            }
            child = next_child;
        }
        proc->children_head = NULL;

        /* Wake parent waiting in wait4 */
        if (proc->parent) {
            sched_wake_waiters_unlocked(proc->parent, proc->pid);
        }
        spinlock_irqrestore(&s_sched_lock, flags);
    }

    thread_exit();

    return 0;
}

static process_t *find_and_unlink_zombie(process_t *parent, int64_t target_pid, bool *has_living_children) {
    process_t **curr = &parent->children_head;
    *has_living_children = false;

    while (*curr) {
        process_t *child = *curr;

        if (target_pid == -1 || (int64_t)child->pid == target_pid) {
            if (child->state == PROC_ZOMBIE) {
                *curr = child->sibling_next;
                child->sibling_next = NULL;
                return child;
            }
            *has_living_children = true;
        }
        curr = &(*curr)->sibling_next;
    }

    return NULL;
}

int64_t sys_wait4(int64_t pid, int *wstatus, int options, void *rusage) {
    (void)rusage;
    if (!g_current_thread || !g_current_thread->process) {
        return -EINVAL;
    }

    if (wstatus && !validate_user_buffer(wstatus, sizeof(int))) {
        return -EFAULT;
    }

    process_t *parent = g_current_thread->process;

    while (1) {
        bool has_living_children = false;

        uint64_t flags = spinlock_irqsave(&s_sched_lock);
        process_t *zombie = find_and_unlink_zombie(parent, pid, &has_living_children);

        if (zombie) {
            spinlock_irqrestore(&s_sched_lock, flags);
            int64_t zombie_pid = (int64_t)zombie->pid;

            if (wstatus) {
                *wstatus = zombie->exit_status;
            }

            process_destroy(zombie);
            return zombie_pid;
        }

        if (!has_living_children) {
            spinlock_irqrestore(&s_sched_lock, flags);
            return -ECHILD;
        }

        if (options & WNOHANG) {
            spinlock_irqrestore(&s_sched_lock, flags);
            return 0;
        }

        g_current_thread->state = THREAD_BLOCKED;
        g_current_thread->wait_pid = pid;
        g_current_thread->wait_next = s_wait_queue_head;
        s_wait_queue_head = g_current_thread;

        spinlock_irqrestore(&s_sched_lock, flags);

        sched_yield();
    }
}

#define MAX_EXEC_ARGS 64
#define MAX_ARG_LEN   256

int64_t sys_execve(const char *path, char *const argv[], char *const envp[], syscall_saved_regs_t *regs) {
    if (!path || !regs || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }

    if (!validate_user_buffer((void *)path, 1)) {
        return -EFAULT;
    }

    process_t *proc = g_current_thread->process;
    int err_code = -ENOEXEC;

    /* 1. Copy path safely into kernel buffer and resolve relative to CWD */
    char raw_path[VFS_MAX_PATH];
    size_t path_len = 0;
    while (path[path_len] && path_len < sizeof(raw_path) - 1) {
        raw_path[path_len] = path[path_len];
        path_len++;
    }
    raw_path[path_len] = '\0';

    char kpath[VFS_MAX_PATH];
    resolve_proc_path(proc, raw_path, kpath, sizeof(kpath));

    /* 2. Copy argv into temporary kernel buffers */
    int argc = 0;
    char *kargv[MAX_EXEC_ARGS];
    memset(kargv, 0, sizeof(kargv));

    if (argv) {
        while (argv[argc] && argc < MAX_EXEC_ARGS - 1) {
            kargv[argc] = (char *)kmalloc(MAX_ARG_LEN);
            if (!kargv[argc]) {
                err_code = -ENOMEM;
                goto cleanup_args;
            }

            size_t arg_len = 0;
            while (argv[argc][arg_len] && arg_len < MAX_ARG_LEN - 1) {
                kargv[argc][arg_len] = argv[argc][arg_len];
                arg_len++;
            }
            kargv[argc][arg_len] = '\0';
            argc++;
        }
    }
    kargv[argc] = NULL;

    /* 3. Copy envp into temporary kernel buffers */
    int envc = 0;
    char *kenvp[MAX_EXEC_ARGS];
    memset(kenvp, 0, sizeof(kenvp));

    if (envp) {
        while (envp[envc] && envc < MAX_EXEC_ARGS - 1) {
            kenvp[envc] = (char *)kmalloc(MAX_ARG_LEN);
            if (!kenvp[envc]) {
                err_code = -ENOMEM;
                goto cleanup_env;
            }

            size_t env_len = 0;
            while (envp[envc][env_len] && env_len < MAX_ARG_LEN - 1) {
                kenvp[envc][env_len] = envp[envc][env_len];
                env_len++;
            }
            kenvp[envc][env_len] = '\0';
            envc++;
        }
    }
    kenvp[envc] = NULL;

    /* Fallback default argv[0] if none provided */
    if (argc == 0) {
        kargv[0] = (char *)kmalloc(MAX_ARG_LEN);
        if (!kargv[0]) {
            err_code = -ENOMEM;
            goto cleanup_env;
        }
        strncpy(kargv[0], raw_path, MAX_ARG_LEN - 1);
        kargv[0][MAX_ARG_LEN - 1] = '\0';
        kargv[1] = NULL;
        argc = 1;
    }

    const char* prog = (argc > 0 && kargv[0]) ? kargv[0] : kpath;
    const char* slash = strrchr(prog, '/');
    const char* base = slash ? (slash + 1) : prog;
    strncpy(proc->comm, base, sizeof(proc->comm) - 1);
    proc->comm[sizeof(proc->comm) - 1] = '\0';

    size_t pos = 0;
    memset(proc->cmdline, 0, sizeof(proc->cmdline));
    for (int i = 0; i < argc && pos < sizeof(proc->cmdline) - 1; i++) {
        if (!kargv[i]) continue;
        size_t alen = strlen(kargv[i]);
        if (pos + alen + 1 > sizeof(proc->cmdline)) {
            alen = sizeof(proc->cmdline) - pos - 1;
        }
        memcpy(proc->cmdline + pos, kargv[i], alen);
        pos += alen;
        proc->cmdline[pos++] = '\0';
    }
    proc->cmdline_len = pos;
    
    serial_printf("[EXECVE] kpath='%s' argc=%d envc=%d\n", kpath, argc, envc);
    /* 4. Open and read executable binary via VFS (resolving #! shebangs up to 4 hops) */
    int shebang_depth = 0;
    size_t file_size = 0;
    uint8_t *file_buf = NULL;

    while (1) {
        vfs_file_t *file = vfs_open(kpath, 0);
        if (!file) {
            serial_printf("[EXECVE] FAIL: vfs_open('%s') returned NULL\n", kpath);
            err_code = -ENOENT;
            goto cleanup_env;
        }

        file_size = file->node ? file->node->size : 0;
        if (file_size < 3) {
            vfs_close(file);
            err_code = -ENOEXEC;
            goto cleanup_env;
        }

        file_buf = (uint8_t *)kmalloc(file_size);
        if (!file_buf) {
            serial_printf("[EXECVE] FAIL: kmalloc(%llu) OOM!\n", (uint64_t)file_size);
            vfs_close(file);
            err_code = -ENOMEM;
            goto cleanup_env;
        }

        int64_t bytes_read = vfs_read(file, file_buf, file_size);
        vfs_close(file);
        if (bytes_read <= 0) {
            kfree(file_buf);
            err_code = -ENOEXEC;
            goto cleanup_env;
        }

        char interp[VFS_MAX_PATH];
        char interp_arg[MAX_SHEBANG_LINE];
        bool has_arg = false;

        if (parse_shebang(file_buf, (size_t)bytes_read, interp, sizeof(interp),
                          interp_arg, sizeof(interp_arg), &has_arg)) {
            kfree(file_buf);
            file_buf = NULL;

            if (++shebang_depth > MAX_SHEBANG_DEPTH) {
                err_code = -ELOOP;
                goto cleanup_env;
            }

            int extra = has_arg ? 2 : 1;
            if (argc + extra >= MAX_EXEC_ARGS) {
                err_code = -E2BIG;
                goto cleanup_env;
            }

            char *interp_copy = (char *)kmalloc(MAX_ARG_LEN);
            char *script_copy = (char *)kmalloc(MAX_ARG_LEN);
            char *arg_copy    = has_arg ? (char *)kmalloc(MAX_ARG_LEN) : NULL;
            if (!interp_copy || !script_copy || (has_arg && !arg_copy)) {
                if (interp_copy) kfree(interp_copy);
                if (script_copy) kfree(script_copy);
                if (arg_copy)    kfree(arg_copy);
                err_code = -ENOMEM;
                goto cleanup_env;
            }

            strncpy(interp_copy, interp, MAX_ARG_LEN - 1);
            interp_copy[MAX_ARG_LEN - 1] = '\0';

            strncpy(script_copy, kpath, MAX_ARG_LEN - 1);
            script_copy[MAX_ARG_LEN - 1] = '\0';

            if (has_arg) {
                strncpy(arg_copy, interp_arg, MAX_ARG_LEN - 1);
                arg_copy[MAX_ARG_LEN - 1] = '\0';
            }

            /* Free old argv[0] and shift kargv[1..argc-1] right by 'extra' slots */
            if (argc > 0 && kargv[0]) {
                kfree(kargv[0]);
            }
            for (int i = argc - 1; i >= 1; i--) {
                kargv[i + extra] = kargv[i];
            }

            int idx = 0;
            kargv[idx++] = interp_copy;
            if (has_arg) {
                kargv[idx++] = arg_copy;
            }
            kargv[idx++] = script_copy;

            argc += extra;
            kargv[argc] = NULL;

            /* Resolve interpreter path (supports relative interpreters too) and re-loop */
            resolve_proc_path(proc, interp, kpath, sizeof(kpath));
            continue;
        }

        if (file_size < sizeof(elf64_ehdr_t)) {
            kfree(file_buf);
            err_code = -ENOEXEC;
            goto cleanup_env;
        }

        break; /* Valid ELF candidate in file_buf */
    }


    /* 5. Create a clean pagemap and parse ELF into it */
    vmm_pagemap_t *new_pagemap = vmm_new_pagemap();
    if (!new_pagemap) {
        kfree(file_buf);
        err_code = -ENOMEM;
        goto cleanup_env;
    }

    uintptr_t entry_point = 0;
    uintptr_t heap_start = 0;
    uintptr_t phdr_vaddr = 0;
    elf64_ehdr_t ehdr;

    if (elf_load_binary_into_pagemap(file_buf, file_size, new_pagemap,
                                     &entry_point, &heap_start,
                                     &phdr_vaddr, &ehdr) != 0) {
        vmm_destroy_pagemap(new_pagemap);
        kfree(new_pagemap);
        kfree(file_buf);
        err_code = -ENOEXEC;
        goto cleanup_env;
    }

    kfree(file_buf);

    /* 6. Populate Linux System V user stack */
    uintptr_t user_rsp = setup_user_stack(
        new_pagemap,
        USER_STACK_VIRT_TOP,
        &ehdr,
        phdr_vaddr,
        argc,
        (const char **)kargv,
        (const char **)kenvp
    );

    if (!user_rsp) {
        vmm_destroy_pagemap(new_pagemap);
        kfree(new_pagemap);
        err_code = -ENOMEM;
        goto cleanup_env;
    }

    /*
     * 7. POINT OF NO RETURN: Swap process address spaces.
     */
    vmm_pagemap_t *old_pagemap = proc->pagemap;

    proc->pagemap = new_pagemap;
    g_current_thread->pagemap = new_pagemap;
    g_current_thread->clear_child_tid = NULL;
    g_current_thread->fs_base = 0;

    proc->heap_start = heap_start;
    proc->heap_end   = heap_start;
    proc->mmap_next  = MMAP_BASE_ADDR;

    /* POSIX: Reset custom signal handlers to SIG_DFL; keep SIG_IGN and sig_blocked */
    for (int sig = 1; sig < NSIG; sig++) {
        if (proc->sig_actions[sig].sa_handler != SIG_IGN) {
            memset(&proc->sig_actions[sig], 0, sizeof(struct sigaction));
            proc->sig_actions[sig].sa_handler = SIG_DFL;
        }
    }

    /* POSIX: Close file descriptors marked O_CLOEXEC */
    for (int i = 0; i < MAX_FDS; i++) {
        if (proc->fd_table[i] &&
            ((proc->fd_flags[i] & FD_CLOEXEC) || (proc->fd_table[i]->flags & O_CLOEXEC))) {
            vfs_close(proc->fd_table[i]);
            proc->fd_table[i] = NULL;
            proc->fd_flags[i] = 0;
        }
    }

    /* Switch CR3 to new process image and reset FS base */
    vmm_switch_pagemap(new_pagemap);
    wrmsr(0xC0000100, 0);

    /* Tear down and reclaim old process physical pages */
    vmm_destroy_pagemap(old_pagemap);
    kfree(old_pagemap);

    /* 8. Reset register context for return to Ring 3 */
    regs->rip    = entry_point;
    regs->rsp    = user_rsp;
    regs->rflags = 0x202;
    regs->rbp    = 0;
    regs->rbx    = 0;
    regs->r12    = 0;
    regs->r13    = 0;
    regs->r14    = 0;
    regs->r15    = 0;

    /* Free kernel copies of argv/envp */
    for (int i = 0; i < argc; i++) kfree(kargv[i]);
    for (int i = 0; i < envc; i++) kfree(kenvp[i]);

    return 0;

cleanup_env:
    for (int i = 0; i < envc; i++) {
        if (kenvp[i]) kfree(kenvp[i]);
    }
cleanup_args:
    for (int i = 0; i < argc; i++) {
        if (kargv[i]) kfree(kargv[i]);
    }
    return err_code;
}