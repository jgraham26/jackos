#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "drivers/klog.h"
#include "cpu/syscall.h"
#include "cpu/apic.h"
#include "cpu/idt.h"
#include "cpu/signal.h"
#include "kernel.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "io.h"
#include "fs/vfs.h"
#include "mem/uaccess.h"
#include "fs/stat.h"
#include "drivers/rtc.h"
#include <errno.h>
#include "fs/pipe.h"
#include "drivers/ps2_kbd.h"
#include "sched/spinlock.h"

#define IA32_EFER       0xC0000080
#define IA32_STAR       0xC0000081
#define IA32_LSTAR      0xC0000082
#define IA32_FMASK      0xC0000084

#define EFER_SCE        (1ULL << 0) /* System Call Extension Enable */



#define MSR_FS_BASE 0xC0000100

#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

#define SYSCALL_MAX 300

extern void syscall_entry();
extern volatile uint64_t g_ticks;
extern process_t *g_process_list_head;

typedef int64_t (*syscall_fn_t)(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, syscall_saved_regs_t* regs);

#define DEFINE_SYSCALL0(name) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)regs; \
        return (int64_t)name(); \
    }

#define DEFINE_SYSCALL1(name, t1) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)regs; \
        return (int64_t)name((t1)a1); \
    }

#define DEFINE_SYSCALL2(name, t1, t2) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a3; (void)a4; (void)a5; (void)a6; (void)regs; \
        return (int64_t)name((t1)a1, (t2)a2); \
    }

#define DEFINE_SYSCALL3(name, t1, t2, t3) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a4; (void)a5; (void)a6; (void)regs; \
        return (int64_t)name((t1)a1, (t2)a2, (t3)a3); \
    }

#define DEFINE_SYSCALL4(name, t1, t2, t3, t4) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a5; (void)a6; (void)regs; \
        return (int64_t)name((t1)a1, (t2)a2, (t3)a3, (t4)a4); \
    }

#define DEFINE_SYSCALL5(name, t1, t2, t3, t4, t5) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a6; (void)regs; \
        return (int64_t)name((t1)a1, (t2)a2, (t3)a3, (t4)a4, (t5)a5); \
    }

#define DEFINE_SYSCALL6(name, t1, t2, t3, t4, t5, t6) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)regs; \
        return (int64_t)name((t1)a1, (t2)a2, (t3)a3, (t4)a4, (t5)a5, (t6)a6); \
    }

/* For syscalls requiring the saved register frame (fork, execve, rt_sigreturn) */
#define DEFINE_SYSCALL_REGS0(name) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; \
        return (int64_t)name(regs); \
    }

#define DEFINE_SYSCALL_REGS3(name, t1, t2, t3) \
    static int64_t wrap_##name(uint64_t a1, uint64_t a2, uint64_t a3, \
                               uint64_t a4, uint64_t a5, uint64_t a6, \
                               syscall_saved_regs_t *regs) { \
        (void)a4; (void)a5; (void)a6; \
        return (int64_t)name((t1)a1, (t2)a2, (t3)a3, regs); \
    }

int64_t sys_ioctl(int fd, uint64_t request, void* arg) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    process_t* proc = g_current_thread->process;
    if (fd < 0 || fd >= MAX_FDS) {
        return -EBADF;
    }

    vfs_file_t* file = proc->fd_table[fd];
    if (!file) {
        return -EBADF;
    }

    klog_printf("[IOCTL] pid=%llu fd=%d node='%s' req=%#llx\n",
                  proc->pid, fd, file->node->name, request);

    if (file->node->ops && file->node->ops->ioctl) {
        return file->node->ops->ioctl(file, request, arg);
    }

    return -ENOTTY;
}

int64_t sys_set_tid_address(int* tidptr) {
    if (!g_current_thread) {
        return -ESRCH;
    }

    g_current_thread->clear_child_tid = tidptr;

    return (int64_t)g_current_thread->tid;
}

void syscall_init() {
    /* 1. Enable SCE in IA32_EFER */
    uint64_t efer = rdmsr(IA32_EFER);
    wrmsr(IA32_EFER, efer | EFER_SCE);

    /* 2. Configure IA32_STAR:
     * Bits [47:32] = Kernel CS/SS base selector (0x08)
     * Bits [63:48] = User CS/SS base selector (0x10) -> user SS = 0x1B, CS = 0x23
     */

    uint64_t star = ((uint64_t)(0x0010 | 3) << 48 | ((uint64_t)0x0008 << 32));
    wrmsr(IA32_STAR, star);

    /* 3. Configure entry point rip in IA32_LSTAR */
    wrmsr(IA32_LSTAR, (uint64_t)syscall_entry);

    wrmsr(IA32_FMASK, 0x0200);
}

int64_t sys_open(const char* path, int flags, int mode) {
    if (!path || !g_current_thread || !g_current_thread->process) return -EFAULT;

    char kpath[VFS_MAX_PATH];
    size_t len = 0;
    while (len < sizeof(kpath) - 1) {
        char c = path[len];
        if (c == '\0') break;
        kpath[len++] = c;
    }
    kpath[len] = '\0';

    process_t* proc = g_current_thread->process;


    /* Construct full canonical path */
    char full_path[VFS_MAX_PATH];

    if (kpath[0] == '/') {
        /* Absolute path */
        strncpy(full_path, kpath, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    } else {
        const char *base_cwd = (proc->cwd_path[0] != '\0') ? proc->cwd_path : "/";
        strncpy(full_path, base_cwd, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';

        /* If user opened "." or "./", the target is current directory itself */
        if (strcmp(kpath, ".") == 0 || strcmp(kpath, "./") == 0) {
            /* full_path is already base_cwd; do not append anything */
        } else {
            size_t clen = strlen(full_path);
            if (clen > 0 && full_path[clen - 1] != '/') {
                strncat(full_path, "/", sizeof(full_path) - strlen(full_path) - 1);
            }
            /* Skip leading "./" if provided (e.g. "./test") */
            const char *sub = kpath;
            if (sub[0] == '.' && sub[1] == '/') {
                sub += 2;
            }
            strncat(full_path, sub, sizeof(full_path) - strlen(full_path) - 1);
        }
    }

    /* Allocate FD */
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!proc->fd_table[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -EMFILE;

    uint32_t vfs_flags = (uint32_t)flags & (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_APPEND | O_TRUNC | O_NONBLOCK);

    vfs_file_t* f = vfs_open(full_path, vfs_flags);
    if (!f) {
        if (flags & 0x40) { /* O_CREAT */
            vfs_node_t *node = vfs_create(full_path, (uint32_t)mode);
            if (node) {
                f = vfs_open(full_path, vfs_flags);
            }
        }
    }

    if (proc->pid == 2) {
        klog_printf("[PID2 OPEN] path='%s' raw_flags=%#x -> f=%p\n", full_path, flags, f);
    }

    if (!f) return -ENOENT;

    proc->fd_table[fd] = f;
    return (int64_t)fd;
}

int64_t sys_read(int fd, void* buf, size_t count) {
    if (count == 0) return 0;
    if (fd < 0 || fd >= MAX_FDS || !buf) return -EFAULT;
    if (!validate_user_buffer(buf, count)) return -EFAULT;
    if (!g_current_thread || !g_current_thread->process) return -EFAULT;

    process_t* proc = g_current_thread->process;
    vfs_file_t* f = proc->fd_table[fd];
    if (!f) return -EBADF;
    if (f->flags & O_WRONLY) return -EBADF;

    return vfs_read(f, buf, count);
}

static void vfs_node_to_linux_stat(vfs_node_t *node, struct linux_stat *st) {
    memset(st, 0, sizeof(struct linux_stat));

    st->st_dev     = 1;
    st->st_ino     = node->inode ? node->inode : 1;
    st->st_nlink   = 1;
    st->st_uid     = 0;
    st->st_gid     = 0;
    st->st_size    = (int64_t)node->size;
    st->st_blksize = 4096;
    st->st_blocks  = ((int64_t)node->size + 511) / 512;

    uint32_t mode = 0755; /* Default rwxr-xr-x */
    if (node->flags & VFS_DIRECTORY) {
        mode |= S_IFDIR;
        st->st_nlink = 2;
    } else if (node->flags & VFS_SYMLINK) {
        mode = S_IFLNK | 0777;
    } else if (node->flags & VFS_CHARDEVICE) {
        mode |= S_IFCHR;
        st->st_rdev = node->inode;
    } else if (node->flags & VFS_BLOCKDEVICE) {
        mode |= S_IFBLK;
        st->st_rdev = node->inode;
    } else {
        mode |= S_IFREG;
    }

    st->st_mode = mode;
}

int64_t sys_stat(const char *path, struct linux_stat *statbuf) {
    if (!path || !statbuf || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }
    if (!validate_user_buffer((void *)path, 1) ||
        !validate_user_buffer(statbuf, sizeof(struct linux_stat))) {
        return -EFAULT;
    }

    char kpath[VFS_MAX_PATH];
    size_t len = 0;
    while (len < sizeof(kpath) - 1 && path[len] != '\0') {
        kpath[len] = path[len];
        len++;
    }
    kpath[len] = '\0';

    vfs_node_t *node = vfs_resolve_path(kpath);
    if (!node) {
        return -ENOENT;
    }

    struct linux_stat kstat;
    vfs_node_to_linux_stat(node, &kstat);
    memcpy(statbuf, &kstat, sizeof(struct linux_stat));
    return 0;
}

int64_t sys_fstat(int fd, struct linux_stat *statbuf) {
    if (!statbuf || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }
    if (!validate_user_buffer(statbuf, sizeof(struct linux_stat))) {
        return -EFAULT;
    }

    process_t *proc = g_current_thread->process;
    if (fd < 0 || fd >= MAX_FDS) {
        return -EBADF;
    }

    vfs_file_t *file = proc->fd_table[fd];
    if (!file || !file->node) {
        return -EBADF;
    }

    struct linux_stat kstat;
    vfs_node_to_linux_stat(file->node, &kstat);
    memcpy(statbuf, &kstat, sizeof(struct linux_stat));
    return 0;
}

int64_t sys_lstat(const char *path, struct linux_stat *statbuf) {
    if (!path || !statbuf || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }
    if (!validate_user_buffer((void *)path, 1) ||
        !validate_user_buffer(statbuf, sizeof(struct linux_stat))) {
        return -EFAULT;
    }

    char kpath[VFS_MAX_PATH];
    size_t len = 0;
    while (len < sizeof(kpath) - 1 && path[len] != '\0') {
        kpath[len] = path[len];
        len++;
    }
    kpath[len] = '\0';

    if (kpath[0] == '\0') {
        return -ENOENT;
    }

    vfs_node_t *node = vfs_resolve_path_nofollow(kpath);
    if (!node) {
        return -ENOENT;
    }

    struct linux_stat kstat;
    vfs_node_to_linux_stat(node, &kstat);
    memcpy(statbuf, &kstat, sizeof(struct linux_stat));
    return 0;
}


int64_t sys_newfstatat(int dirfd, const char *path, struct linux_stat *statbuf, int flags) {
    if (!path || !statbuf || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }
    if (!validate_user_buffer((void *)path, 1) ||
        !validate_user_buffer(statbuf, sizeof(struct linux_stat))) {
        return -EFAULT;
    }

    /* Handle AT_EMPTY_PATH: stat dirfd directly (used by musl fstat fallback) */
    if ((flags & AT_EMPTY_PATH) && path[0] == '\0') {
        return sys_fstat(dirfd, statbuf);
    }

    char kpath[VFS_MAX_PATH];
    size_t len = 0;
    while (len < sizeof(kpath) - 1 && path[len] != '\0') {
        kpath[len] = path[len];
        len++;
    }
    kpath[len] = '\0';

    /* Absolute path or AT_FDCWD resolves directly via vfs_resolve_path */
    if (kpath[0] == '/' || dirfd == AT_FDCWD) {
        vfs_node_t *node = vfs_resolve_path(kpath);
        if (!node) return -ENOENT;

        struct linux_stat kstat;
        vfs_node_to_linux_stat(node, &kstat);
        memcpy(statbuf, &kstat, sizeof(struct linux_stat));
        return 0;
    }

    /* Relative path anchored at an open directory fd */
    process_t *proc = g_current_thread->process;
    if (dirfd < 0 || dirfd >= MAX_FDS || !proc->fd_table[dirfd] || !proc->fd_table[dirfd]->node) {
        return -EBADF;
    }

    vfs_node_t *dir_node = proc->fd_table[dirfd]->node;
    if (!(dir_node->flags & VFS_DIRECTORY) || !dir_node->ops || !dir_node->ops->lookup) {
        return -ENOTDIR;
    }

    vfs_node_t *target = dir_node->ops->lookup(dir_node, kpath);
    if (!target) {
        return -ENOENT;
    }

    struct linux_stat kstat;
    vfs_node_to_linux_stat(target, &kstat);
    memcpy(statbuf, &kstat, sizeof(struct linux_stat));
    return 0;
}

int64_t sys_write(int fd, const void* buf, size_t count) {
    if (fd < 0 || fd >= MAX_FDS || !buf || !g_current_thread) return -EBADF;
    process_t* proc = g_current_thread->process;

    vfs_file_t* f = proc->fd_table[fd];
    if (!f) return -EBADF;

    return vfs_write(f, buf, count);
}

int64_t sys_getdents64(int fd, void *dirp, size_t count) {
    if (!dirp || count == 0 || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }

    process_t *proc = g_current_thread->process;
    if (fd < 0 || fd >= MAX_FDS || !proc->fd_table[fd]) {
        return -EBADF;
    }

    vfs_file_t *file = proc->fd_table[fd];
    if (!file || !file->node) {
        return -EBADF;
    }

    vfs_node_t *node = file->node;

    /* Verify it is a directory and has a valid readdir hook */
    if (!(node->flags & VFS_DIRECTORY)) {
        return -ENOTDIR;
    }

    if (!node->ops || !node->ops->readdir) {
        return -ENOTDIR;
    }

    uint8_t *buf_ptr = (uint8_t *)dirp;
    size_t bytes_written = 0;
    vfs_dirent_t dent;

    while (file->node->ops->readdir(file->node, file->offset, &dent) == 0) {
        size_t name_len = strlen(dent.name);
        size_t rec_len = (sizeof(struct linux_dirent64) + name_len + 1 + 7) & ~7ULL;

        if (bytes_written + rec_len > count) {
            if (bytes_written == 0) return -1;
            break;
        }

        struct linux_dirent64 *d = (struct linux_dirent64 *)(buf_ptr + bytes_written);
        d->d_ino = dent.inode;
        d->d_off = file->offset + 1;
        d->d_reclen = (uint16_t)rec_len;

        /* Convert kernel VFS types to standard Linux dirent DT_* types */
        if (dent.type == VFS_DIRECTORY) {
            d->d_type = DT_DIR;  /* 4 */
        } else if (dent.type == VFS_CHARDEVICE) {
            d->d_type = DT_CHR;  /* 2 */
        } else if (dent.type == VFS_BLOCKDEVICE) {
            d->d_type = DT_BLK;  /* 6 */
        } else if (dent.type == VFS_SYMLINK) {
            d->d_type = DT_LNK;  /* 10 */
        } else {
            d->d_type = DT_REG;  /* 8 */
        }
        memcpy(d->d_name, dent.name, name_len + 1);

        bytes_written += rec_len;
        file->offset++;
    }

    return (int64_t)bytes_written;
}

int64_t sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !g_current_thread) return -1;
    process_t* proc = g_current_thread->process;

    vfs_file_t* f = proc->fd_table[fd];
    if (!f) return -1;

    vfs_close(f);
    proc->fd_table[fd] = NULL;
    return 0;
}

uintptr_t sys_brk(uintptr_t new_brk) {
    if (!g_current_thread || !g_current_thread->process) {
        return 0;
    }

    process_t *proc = g_current_thread->process;

    /* A query call: brk(0) returns current break */
    if (new_brk == 0 || new_brk == proc->heap_end) {
        return proc->heap_end;
    }

    /* Reject addresses below heap baseline or above upper safety ceiling */
    if (new_brk < proc->heap_start || new_brk > proc->heap_limit) {
        return proc->heap_end;
    }

    uintptr_t old_page_boundary = PAGE_ALIGN_UP(proc->heap_end);
    uintptr_t new_page_boundary = PAGE_ALIGN_UP(new_brk);

    /* Case A: Expanding the heap */
    if (new_page_boundary > old_page_boundary) {
        vma_add(proc, old_page_boundary, new_page_boundary, VMA_READ | VMA_WRITE | VMA_ANON);
    }
    /* Case B: Shrinking the heap */
    else if (new_page_boundary < old_page_boundary) {
        for (uintptr_t vaddr = new_page_boundary; vaddr < old_page_boundary; vaddr += PAGE_SIZE) {
            pt_entry_t *entry = vmm_get_pte(proc->pagemap, vaddr);
            if (entry && (*entry & PTE_PRESENT)) {
                pmm_free_frame(*entry & PTE_FRAME_ADDR_MASK);
            }
            vmm_unmap_page(proc->pagemap, vaddr);
        }
        vma_remove(proc, new_page_boundary, old_page_boundary);
    }

    proc->heap_end = new_brk;
    return proc->heap_end;
}

int64_t sys_arch_prctl(int code, uintptr_t addr) {
    switch (code) {
    case ARCH_SET_FS:
        if (addr >= 0x0000800000000000ULL && addr < 0xFFFF800000000000ULL) {
            return -1;
        }
        wrmsr(MSR_FS_BASE, addr);
        if (g_current_thread) {
            g_current_thread->fs_base = addr;
        }
        return 0;

    case ARCH_GET_FS:
        if (!addr) return -1;
        *(uint64_t*)addr = rdmsr(MSR_FS_BASE);
        return 0;

    default:
        return -1;
    }
}

int64_t sys_rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact, size_t sigsetsize) {
    (void)sigsetsize;
    if (signum <= 0 || signum >= NSIG || signum == SIGKILL || signum == SIGSTOP) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;
    if (!proc) return -ESRCH;

    if (oldact) {
        *oldact = proc->sig_actions[signum];
    }

    if (act) {
        proc->sig_actions[signum] = *act;
    }

    return 0;
}

int64_t sys_rt_sigreturn(syscall_saved_regs_t *regs) {
    ucontext_frame_t *ctx = (ucontext_frame_t *)regs->rsp;

    /* Restore preserved CPU registers */
    regs->r15    = ctx->regs.r15;
    regs->r14    = ctx->regs.r14;
    regs->r13    = ctx->regs.r13;
    regs->r12    = ctx->regs.r12;
    regs->rbx    = ctx->regs.rbx;
    regs->rbp    = ctx->regs.rbp;
    regs->rdi    = ctx->regs.rdi;
    regs->rip    = ctx->regs.rip;
    regs->rflags = ctx->regs.rflags;
    regs->rsp    = ctx->regs.rsp;

    /* Restore blocked signal mask */
    if (g_current_thread && g_current_thread->process) {
        g_current_thread->process->sig_blocked = ctx->saved_sigmask;
    }

    /* RAX is returned directly by this function */
    return (int64_t)ctx->orig_rax;
}

int64_t sys_chdir(const char *path) {
    if (!path || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }
    if ((uintptr_t)path >= 0x0000800000000000ULL) {
        return -EFAULT;
    }

    char kpath[VFS_MAX_PATH];
    size_t len = 0;
    while (len < sizeof(kpath) - 1) {
        char c = path[len];
        if (c == '\0') break;
        kpath[len++] = c;
    }
    kpath[len] = '\0';

    process_t *proc = g_current_thread->process;
    vfs_node_t *node = vfs_resolve_path(kpath);
    if (!node) return -ENOENT;
    if (!(node->flags & VFS_DIRECTORY)) return -ENOTDIR;

    proc->cwd = (void *)node;

    /* Maintain canonical cwd_path string */
/* Maintain canonical cwd_path string */
    char full[VFS_MAX_PATH];
    if (kpath[0] == '/') {
        strncpy(full, kpath, sizeof(full) - 1);
    } else {
        const char *base = (proc->cwd_path[0] != '\0') ? proc->cwd_path : "/";
        strncpy(full, base, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';

        size_t blen = strlen(full);
        if (blen > 0 && full[blen - 1] != '/') {
            strncat(full, "/", sizeof(full) - blen - 1);
        }
        strncat(full, kpath, sizeof(full) - strlen(full) - 1);
    }
    full[sizeof(full) - 1] = '\0';

    /* Normalize '.' and '..' components */
    path_canonicalize(full);

    strncpy(proc->cwd_path, full, sizeof(proc->cwd_path) - 1);
    proc->cwd_path[sizeof(proc->cwd_path) - 1] = '\0';

    /* Strip trailing slashes, preserving "/" */
    size_t plen = strlen(proc->cwd_path);
    while (plen > 1 && proc->cwd_path[plen - 1] == '/') {
        proc->cwd_path[--plen] = '\0';
    }

    return 0;
}

int64_t sys_getcwd(char *buf, size_t size) {
    if (!buf || size == 0 || !g_current_thread || !g_current_thread->process) return -EFAULT;
    if (!validate_user_buffer(buf, size)) return -EFAULT;

    process_t *proc = g_current_thread->process;
    const char *cwd_str = (proc->cwd_path[0] != '\0') ? proc->cwd_path : "/";
    size_t len = strlen(cwd_str);
    if (len + 1 > size) return -ERANGE;

    memcpy(buf, cwd_str, len + 1);
    return (int64_t)buf;
}

int64_t sys_setpgid(pid_t pid, pid_t pgid) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    process_t *curr = g_current_thread->process;

    if (pid == 0) pid = (pid_t)curr->pid;
    if (pgid == 0) pgid = pid;
    if (pgid < 0) return -EINVAL;

    /* Find target process (self or direct child) */
    process_t *target = NULL;
    if ((uint64_t)pid == curr->pid) {
        target = curr;
    } else {
        for (process_t *c = curr->children_head; c; c = c->sibling_next) {
            if ((pid_t)c->pid == pid) {
                target = c;
                break;
            }
        }
    }

    if (!target) return -ESRCH;
    target->pgid = (uint64_t)pgid;
    return 0;
}

int64_t sys_getpgid(pid_t pid) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    process_t *curr = g_current_thread->process;

    if (pid == 0 || (uint64_t)pid == curr->pid) {
        return (int64_t)curr->pgid;
    }
    for (process_t *c = curr->children_head; c; c = c->sibling_next) {
        if ((pid_t)c->pid == pid) {
            return (int64_t)c->pgid;
        }
    }
    return -ESRCH;
}

int64_t sys_kill(pid_t pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;

    process_t *curr = g_current_thread->process;
    int sent = 0;

    if (pid > 0) {
        /* Signal specific PID */
        for (process_t *p = g_process_list_head; p; p = p->global_next) {
            if ((pid_t)p->pid == pid && p->state != PROC_ZOMBIE) {
                if (sig > 0) kernel_send_signal(p, sig);
                return 0;
            }
        }
        return -ESRCH;
    } else if (pid == 0 || pid < -1) {
        /* Signal process group: pid == 0 -> caller's pgid, pid < -1 -> pgid = -pid */
        uint64_t target_pgid = (pid == 0) ? curr->pgid : (uint64_t)(-pid);
        for (process_t *p = g_process_list_head; p; p = p->global_next) {
            if (p->pgid == target_pgid && p->state != PROC_ZOMBIE) {
                if (sig > 0) kernel_send_signal(p, sig);
                sent++;
            }
        }
        return (sent > 0) ? 0 : -ESRCH;
    }

    return -EPERM;
}

int64_t sys_writev(int fd, const iovec_t *iov, int iovcnt) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    process_t *proc = g_current_thread->process;
    if (fd < 0 || fd >= MAX_FDS) {
        return -EBADF;
    }

    vfs_file_t *file = proc->fd_table[fd];
    if (!file) {
        return -EBADF;
    }

    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        return -EINVAL;
    }

    if (iovcnt == 0) {
        return 0;
    }

    if (!iov) {
        return -EFAULT;
    }

    /* Pre-validate total byte count for signed 64-bit overflow (POSIX requirement) */
    size_t total_requested = 0;
    for (int i = 0; i < iovcnt; i++) {
        if ((int64_t)iov[i].iov_len < 0) {
            return -EINVAL;
        }
        if (iov[i].iov_len > (size_t)INT64_MAX - total_requested) {
            return -EINVAL;
        }
        total_requested += iov[i].iov_len;
    }

    int64_t total_written = 0;

    for (int i = 0; i < iovcnt; i++) {
        size_t len = iov[i].iov_len;
        if (len == 0) {
            continue;
        }

        if (!iov[i].iov_base) {
            return (total_written > 0) ? total_written : -EFAULT;
        }

        int64_t written = vfs_write(file, iov[i].iov_base, len);
        if (written < 0) {
            /* If partial data was already written, return bytes written so far */
            return (total_written > 0) ? total_written : written;
        }

        total_written += written;

        /* Short write: stop iterating and return partial count */
        if ((size_t)written < len) {
            break;
        }
    }

    return total_written;
}

int64_t sys_readv(int fd, const struct iovec *iov, int iovcnt) {
    if (!iov || iovcnt < 0 || iovcnt > 1024) return -EINVAL;
    if (iovcnt == 0) return 0;

    if (!validate_user_buffer((void *)iov, sizeof(struct iovec) * (size_t)iovcnt)) {
        return -EFAULT;
    }

    int64_t total_read = 0;

    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;

        if (!iov[i].iov_base || !validate_user_buffer(iov[i].iov_base, iov[i].iov_len)) {
            return total_read > 0 ? total_read : -EFAULT;
        }

        int64_t n = sys_read(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) {
            return total_read > 0 ? total_read : n;
        }

        total_read += n;

        /* Short read or EOF: stop immediately rather than blocking on subsequent iovecs */
        if ((size_t)n < iov[i].iov_len) {
            break;
        }
    }

    return total_read;
}

int64_t sys_rt_sigprocmask(int how, const uint64_t *set, uint64_t *oldset, size_t sigsetsize) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    if (sigsetsize != sizeof(uint64_t)) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;

    /* Convert internal 1-indexed mask back to Linux 0-indexed ABI */
    if (oldset) {
        *oldset = (uint64_t)(proc->sig_blocked >> 1);
    }

    if (!set) {
        return 0;
    }

    /* Convert Linux 0-indexed mask to internal 1-indexed mask */
    uint32_t new_mask = (uint32_t)((*set) << 1);

    /* Never allow blocking SIGKILL or SIGSTOP */
    new_mask &= ~((1U << SIGKILL) | (1U << SIGSTOP));

    switch (how) {
        case SIG_BLOCK:
            proc->sig_blocked |= new_mask;
            break;
        case SIG_UNBLOCK:
            proc->sig_blocked &= ~new_mask;
            break;
        case SIG_SETMASK:
            proc->sig_blocked = new_mask;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}


/* Protection flags */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

/* Map flags */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20

int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, int64_t offset) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    if (length == 0) {
        return -EINVAL;
    }

    /* POSIX requires either MAP_PRIVATE or MAP_SHARED */
    if ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;
    size_t aligned_len = PAGE_ALIGN_UP(length);
    if (aligned_len == 0) {
        return -ENOMEM; /* Overflow check */
    }

    vfs_file_t *file = NULL;
    if (!(flags & MAP_ANONYMOUS)) {
        klog_printf("[MMAP] fd=%d len=%zu off=%lld\n", fd, length, offset);
        bochs_breakpoint();
        if (fd < 0 || fd >= MAX_FDS) {
            return -EBADF;
        }
        if ((offset & (PAGE_SIZE - 1)) != 0 || offset < 0) {
            return -EINVAL;
        }
        file = proc->fd_table[fd];
        if (!file || !file->node) {
            return -EBADF;
        }
        if (!file->node->ops || !file->node->ops->mmap) {
            return -ENODEV;
        }
    }

    uintptr_t map_addr = 0;

    if (flags & MAP_FIXED) {
        if (!addr || ((uintptr_t)addr & (PAGE_SIZE - 1)) ||
            (uintptr_t)addr >= 0x0000800000000000ULL ||
            (uintptr_t)addr + aligned_len > 0x0000800000000000ULL) {
            return -EINVAL;
        }
        map_addr = (uintptr_t)addr;

        /* Unmap any existing pages in the fixed range before mapping */
        for (size_t off = 0; off < aligned_len; off += PAGE_SIZE) {
            uintptr_t vaddr = map_addr + off;
            pt_entry_t *existing = vmm_get_pte(proc->pagemap, vaddr);
            if (existing && (*existing & PTE_PRESENT)) {
                uintptr_t old_phys = *existing & PTE_FRAME_ADDR_MASK;
                if (old_phys < pmm_get_highest_addr()) {
                    pmm_free_frame(old_phys);
                }
                vmm_unmap_page(proc->pagemap, vaddr);
            }
            vmm_invlpg(vaddr);
        }
    } else {
        if (proc->mmap_next == 0) {
            proc->mmap_next = MMAP_BASE_ADDR;
        }

        /* Check if user hint is valid, page-aligned, in user space, and completely free */
        bool use_hint = false;
        uintptr_t hint = (uintptr_t)addr;
        if (hint >= MMAP_BASE_ADDR &&
            (hint & (PAGE_SIZE - 1)) == 0 &&
            hint + aligned_len <= 0x0000800000000000ULL) {
            use_hint = true;
            for (size_t off = 0; off < aligned_len; off += PAGE_SIZE) {
                pt_entry_t *existing = vmm_get_pte(proc->pagemap, hint + off);
                if (existing && (*existing & PTE_PRESENT)) {
                    use_hint = false;
                    break;
                }
            }
        }

        if (use_hint) {
            map_addr = hint;
        } else {
            map_addr = proc->mmap_next;
            proc->mmap_next += aligned_len;
        }
    }

    /* Convert POSIX prot flags to x86_64 page table flags */
    uint64_t pte_flags = PTE_PRESENT | PTE_USER;
    uint32_t vma_flags = 0;
    if (prot & PROT_READ)  vma_flags |= VMA_READ;
    if (prot & PROT_WRITE) {
        pte_flags |= PTE_WRITABLE;
        vma_flags |= VMA_WRITE;
    }
    if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;
    if (flags & MAP_SHARED) vma_flags |= VMA_SHARED;


    /* Device / file-backed mmap (e.g. /dev/fb0) */
    if (!(flags & MAP_ANONYMOUS)) {
        if (file->node->ops && file->node->ops->mmap) {
            int rc = file->node->ops->mmap(file->node, proc->pagemap, map_addr, aligned_len, pte_flags, (uint64_t)offset);
                                           
            if (rc < 0) {
                return rc;
            }
        }
        return (int64_t)map_addr;

        if (file->node->flags & VFS_FILE) {
            vma_add_file(proc, map_addr, map_addr + aligned_len, vma_flags, file->node, (uint64_t)offset);
            return (int64_t)map_addr;
        }

        return -ENODEV;
    }

    vma_flags |= VMA_ANON;
    vma_add(proc, map_addr, map_addr + aligned_len, vma_flags);

    return (int64_t)map_addr;
}

int64_t sys_munmap(void *addr, size_t length) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    if (!addr || ((uintptr_t)addr & (PAGE_SIZE - 1)) || length == 0) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;
    size_t aligned_len = PAGE_ALIGN_UP(length);
    uintptr_t start = (uintptr_t)addr;

    for (size_t off = 0; off < aligned_len; off += PAGE_SIZE) {
        uintptr_t vaddr = start + off;
        pt_entry_t *entry = vmm_get_pte(proc->pagemap, vaddr);
        if (entry && (*entry & PTE_PRESENT)) {
            pmm_free_frame(*entry & PTE_FRAME_ADDR_MASK);
            vmm_unmap_page(proc->pagemap, vaddr);
            vmm_invlpg(vaddr);
        }
    }

    vma_remove(proc, start, start + aligned_len);

    return 0;
}

int64_t sys_getpid(void) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }
    return (int64_t)g_current_thread->process->pid;
}

int64_t sys_getppid(void) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }
    process_t *parent = g_current_thread->process->parent;
    /* POSIX: orphaned processes report PID 1 (init) or 0 if no parent exists */
    return parent ? (int64_t)parent->pid : 1;
}

int64_t sys_gettid(void) {
    if (!g_current_thread) {
        return -ESRCH;
    }
    return (int64_t)g_current_thread->tid;
}

int64_t sys_getuid(void) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    return 0; /* root */
}

int64_t sys_geteuid(void) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    return 0; /* root */
}

int64_t sys_getgid(void) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    return 0; /* root group */
}

int64_t sys_getegid(void) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    return 0; /* root group */
}

int64_t sys_gettimeofday(struct linux_timeval *tv, void *tz) {
    (void)tz;
    if (!tv) {
        return 0;
    }
    if (!validate_user_buffer(tv, sizeof(struct linux_timeval))) {
        return -EFAULT;
    }

    uint64_t ticks = g_ticks;
    struct linux_timeval ktv;
    ktv.tv_sec  = (int64_t)(rtc_get_boot_epoch() + (ticks / 1000ULL));
    /* Change to .tv_nsec if your header defines struct linux_timeval with tv_nsec */
    ktv.tv_usec = (int64_t)((ticks % 1000ULL) * 1000ULL);

    memcpy(tv, &ktv, sizeof(struct linux_timeval));
    return 0;
}

int64_t sys_clock_gettime(int clockid, struct linux_timeval *tp) {
    if (!tp) {
        return -EFAULT;
    }
    if (!validate_user_buffer(tp, sizeof(struct linux_timeval))) {
        return -EFAULT;
    }

    uint64_t ticks = g_ticks;
    struct linux_timeval kts;

    switch (clockid) {
        case CLOCK_MONOTONIC:
        case CLOCK_MONOTONIC_RAW:
        case CLOCK_MONOTONIC_COARSE:
        case CLOCK_BOOTTIME:
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
            kts.tv_sec  = (int64_t)(ticks / 1000ULL);
            kts.tv_usec = (int64_t)((ticks % 1000ULL) * 1000000ULL);
            break;

        case CLOCK_REALTIME:
        case CLOCK_REALTIME_COARSE:
            kts.tv_sec  = (int64_t)(rtc_get_boot_epoch() + (ticks / 1000ULL));
            kts.tv_usec = (int64_t)((ticks % 1000ULL) * 1000000ULL);
            break;

        default:
            return -EINVAL;
    }

    memcpy(tp, &kts, sizeof(struct linux_timeval));
    return 0;
}

int64_t sys_clock_getres(int clockid, struct linux_timeval *res) {
    switch (clockid) {
        case CLOCK_REALTIME:
        case CLOCK_MONOTONIC:
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
        case CLOCK_MONOTONIC_RAW:
        case CLOCK_REALTIME_COARSE:
        case CLOCK_MONOTONIC_COARSE:
        case CLOCK_BOOTTIME:
            break;
        default:
            return -EINVAL;
    }

    if (res) {
        if (!validate_user_buffer(res, sizeof(struct linux_timeval))) {
            return -EFAULT;
        }
        struct linux_timeval kres = {
            .tv_sec  = 0,
            .tv_usec = 1000000L
        };
        memcpy(res, &kres, sizeof(struct linux_timeval));
    }

    return 0;
}

static int alloc_fd_from(process_t *proc, int min_fd, vfs_file_t *file, uint8_t flags) {
    if (min_fd < 0 || min_fd >= MAX_FDS) {
        return -EINVAL;
    }
    for (int i = min_fd; i < MAX_FDS; i++) {
        if (!proc->fd_table[i]) {
            file->refcount++;
            proc->fd_table[i] = file;
            proc->fd_flags[i] = flags;
            return i;
        }
    }
    return -EMFILE;
}

int64_t sys_fcntl(int fd, int cmd, uint64_t arg) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    process_t *proc = g_current_thread->process;
    if (fd < 0 || fd >= MAX_FDS || !proc->fd_table[fd]) {
        return -EBADF;
    }

    klog_printf("[FCNTL] pid=%llu fd=%d cmd=%d arg=%llu\n",
              proc->pid, fd, cmd, arg);

    vfs_file_t *file = proc->fd_table[fd];

    switch (cmd) {
        case F_DUPFD:
            return alloc_fd_from(proc, (int)arg, file, 0);

        case F_DUPFD_CLOEXEC:
            return alloc_fd_from(proc, (int)arg, file, FD_CLOEXEC);

        case F_GETFD:
            return (int64_t)(proc->fd_flags[fd] & FD_CLOEXEC);

        case F_SETFD:
            if (arg & FD_CLOEXEC) {
                proc->fd_flags[fd] |= FD_CLOEXEC;
            } else {
                proc->fd_flags[fd] &= ~FD_CLOEXEC;
            }
            return 0;

        case F_GETFL:
            return (int64_t)file->flags;

        case F_SETFL: {
            /* Linux only allows modifying O_APPEND and O_NONBLOCK via F_SETFL */
            const uint32_t mutable_mask = O_APPEND | O_NONBLOCK;
            file->flags = (file->flags & ~mutable_mask) | ((uint32_t)arg & mutable_mask);
            return 0;
        }

        default:
            klog_printf("[FCNTL] Unsupported cmd=%d on fd=%d\n", cmd, fd);
            return -EINVAL;
    }
}

int64_t sys_dup(int oldfd) {
    if (!g_current_thread || !g_current_thread->process) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;
    if (oldfd < 0 || oldfd >= MAX_FDS || !proc->fd_table[oldfd]) {
        return -EBADF;
    }

    /* Find lowest-numbered unused file descriptor >= 0 */
    int newfd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!proc->fd_table[i]) {
            newfd = i;
            break;
        }
    }

    if (newfd < 0) {
        return -EMFILE;
    }

    vfs_file_t *file = proc->fd_table[oldfd];
    file->refcount++;
    proc->fd_table[newfd] = file;
    proc->fd_flags[newfd] = 0; /* POSIX: dup clears FD_CLOEXEC */

    return newfd;
}

int64_t sys_dup2(int oldfd, int newfd) {
    if (!g_current_thread || !g_current_thread->process) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;
    if (oldfd < 0 || oldfd >= MAX_FDS || !proc->fd_table[oldfd]) {
        return -EBADF;
    }
    if (newfd < 0 || newfd >= MAX_FDS) {
        return -EBADF;
    }

    /* POSIX: If oldfd == newfd and oldfd is valid, do nothing and return newfd */
    if (oldfd == newfd) {
        return newfd;
    }

    /* Atomically close newfd if it is already open (e.g. closing stdout before redirecting) */
    if (proc->fd_table[newfd]) {
        vfs_close(proc->fd_table[newfd]);
        proc->fd_table[newfd] = NULL;
        proc->fd_flags[newfd] = 0;
    }

    vfs_file_t *file = proc->fd_table[oldfd];
    file->refcount++;
    proc->fd_table[newfd] = file;
    proc->fd_flags[newfd] = 0; /* POSIX: dup2 clears FD_CLOEXEC */

    return newfd;
}

int64_t sys_dup3(int oldfd, int newfd, int flags) {
    if (!g_current_thread || !g_current_thread->process) {
        return -EINVAL;
    }

    /* Linux dup3 only accepts 0 or O_CLOEXEC */
    if (flags & ~O_CLOEXEC) {
        return -EINVAL;
    }

    /* Linux dup3 explicitly fails with EINVAL if oldfd == newfd */
    if (oldfd == newfd) {
        return -EINVAL;
    }

    int64_t res = sys_dup2(oldfd, newfd);
    if (res < 0) {
        return res;
    }

    if (flags & O_CLOEXEC) {
        g_current_thread->process->fd_flags[newfd] |= FD_CLOEXEC;
    }

    return res;
}

int64_t sys_pipe2(int *pipefd, int flags) {
    if (!g_current_thread || !g_current_thread->process) {
        return -EINVAL;
    }
    if (!pipefd || !validate_user_buffer(pipefd, 2 * sizeof(int))) {
        return -EFAULT;
    }
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
        return -EINVAL;
    }

    process_t *proc = g_current_thread->process;

    /* Find two free file descriptors */
    int fd0 = -1, fd1 = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!proc->fd_table[i]) {
            if (fd0 < 0) {
                fd0 = i;
            } else {
                fd1 = i;
                break;
            }
        }
    }

    if (fd0 < 0 || fd1 < 0) {
        return -EMFILE;
    }

    vfs_file_t *rfile = NULL;
    vfs_file_t *wfile = NULL;
    int err = pipe_create(&rfile, &wfile, flags);
    if (err < 0) {
        return err;
    }

    proc->fd_table[fd0] = rfile;
    proc->fd_table[fd1] = wfile;
    proc->fd_flags[fd0] = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    proc->fd_flags[fd1] = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;

    pipefd[0] = fd0;
    pipefd[1] = fd1;
    return 0;
}

int64_t sys_pipe(int *pipefd) {
    return sys_pipe2(pipefd, 0);
}

int64_t sys_poll(struct linux_pollfd *fds, uint64_t nfds, int timeout_ms) {
    if (!g_current_thread || !g_current_thread->process) return -ESRCH;
    if (nfds > 0 && (!fds || !validate_user_buffer(fds, nfds * sizeof(struct linux_pollfd)))) {
        return -EFAULT;
    }

    process_t *proc = g_current_thread->process;
    uint64_t start_tick = g_ticks;

    while (1) {
        int ready = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            int fd = fds[i].fd;
            if (fd < 0) continue;

            if (fd >= MAX_FDS || !proc->fd_table[fd]) {
                fds[i].revents = POLLNVAL;
                ready++;
                continue;
            }

            vfs_file_t* f = proc->fd_table[fd];
            bool is_console = (f && f->node && (strcmp(f->node->name, "console") == 0 || strcmp(f->node->name, "tty") == 0));
            /* Check keyboard buffer for console stdin */
            if (fds[i].events & POLLIN) {
                if (is_console) {
                    if (kbd_has_data()) {
                        fds[i].revents |= POLLIN;
                    }
                } else {
                    fds[i].revents |= POLLIN;
                }
            }

            if (fds[i].events & POLLOUT) {
                fds[i].revents |= POLLOUT;
            }

            if (fds[i].revents) {
                ready++;
            }
        }

        if (ready > 0 || timeout_ms == 0) {
            return ready;
        }

        if (timeout_ms > 0 && (g_ticks - start_tick) >= (uint64_t)timeout_ms) {
            return 0;
        }

        if (proc->sig_pending & ~proc->sig_blocked) {
            return -EINTR;
        }

        sched_yield();
    }
}

int64_t sys_symlinkat(const char *target, int newdirfd, const char *linkpath) {
    if (!target || !linkpath || !g_current_thread || !g_current_thread->process) {
        return -EFAULT;
    }
    if (!validate_user_buffer((void *)target, 1) || !validate_user_buffer((void *)linkpath, 1)) {
        return -EFAULT;
    }

    char ktarget[VFS_MAX_PATH];
    size_t tlen = 0;
    while (tlen < sizeof(ktarget) - 1 && target[tlen] != '\0') {
        ktarget[tlen] = target[tlen];
        tlen++;
    }
    ktarget[tlen] = '\0';

    char klinkpath[VFS_MAX_PATH];
    size_t llen = 0;
    while (llen < sizeof(klinkpath) - 1 && linkpath[llen] != '\0') {
        klinkpath[llen] = linkpath[llen];
        llen++;
    }
    klinkpath[llen] = '\0';

    if (ktarget[0] == '\0' || klinkpath[0] == '\0') {
        return -ENOENT;
    }

    /* Fast path: absolute linkpath or relative to AT_FDCWD */
    if (klinkpath[0] == '/' || newdirfd == AT_FDCWD) {
        return vfs_symlink(ktarget, klinkpath);
    }

    /* Relative linkpath anchored at an open directory fd */
    process_t *proc = g_current_thread->process;
    if (newdirfd < 0 || newdirfd >= MAX_FDS || !proc->fd_table[newdirfd] || !proc->fd_table[newdirfd]->node) {
        return -EBADF;
    }

    vfs_node_t *dir_node = proc->fd_table[newdirfd]->node;
    if (!(dir_node->flags & VFS_DIRECTORY) || !dir_node->ops || !dir_node->ops->symlink) {
        return -ENOTDIR;
    }

    /* If klinkpath has subdirectories (e.g. "sub/link"), resolve parent relative to dir_node */
    char *slash = strrchr(klinkpath, '/');
    const char *name = klinkpath;
    if (slash) {
        *slash = '\0';
        name = slash + 1;
        if (name[0] == '\0') return -EINVAL;

        /* Walk intermediate directories from dir_node */
        char *saveptr = NULL;
        char *tok = strtok_r(klinkpath, "/", &saveptr);
        while (tok) {
            if (strcmp(tok, ".") != 0) {
                if (!dir_node->ops || !dir_node->ops->lookup) return -ENOTDIR;
                dir_node = dir_node->ops->lookup(dir_node, tok);
                if (!dir_node) return -ENOENT;
            }
            tok = strtok_r(NULL, "/", &saveptr);
        }
        if (!(dir_node->flags & VFS_DIRECTORY) || !dir_node->ops || !dir_node->ops->symlink) {
            return -ENOTDIR;
        }
    }

    if (dir_node->ops->lookup && dir_node->ops->lookup(dir_node, name)) {
        return -EEXIST;
    }

    return dir_node->ops->symlink(dir_node, name, ktarget);
}

int64_t sys_symlink(const char *target, const char *linkpath) {
    return sys_symlinkat(target, AT_FDCWD, linkpath);
}

/* Helper: build a full path from (dirfd, user_pathname) */
static int resolve_at_path(int dirfd, const char *user_path, char *out_path, size_t out_max) {
    if (!user_path || !validate_user_buffer((void *)user_path, 1)) return -EFAULT;

    char raw[VFS_MAX_PATH];
    size_t len = 0;
    while (len < sizeof(raw) - 1 && user_path[len] != '\0') {
        raw[len] = user_path[len];
        len++;
    }
    raw[len] = '\0';
    if (raw[0] == '\0') return -ENOENT;

    if (raw[0] == '/' || dirfd == AT_FDCWD) {
        strncpy(out_path, raw, out_max - 1);
        out_path[out_max - 1] = '\0';
        return 0;
    }

    process_t *proc = g_current_thread ? g_current_thread->process : NULL;
    if (!proc || dirfd < 0 || dirfd >= MAX_FDS || !proc->fd_table[dirfd] || !proc->fd_table[dirfd]->node) {
        return -EBADF;
    }

    vfs_node_t *dir = proc->fd_table[dirfd]->node;
    if (!(dir->flags & VFS_DIRECTORY)) return -ENOTDIR;

    /* Reconstruct path from dir node chain */
    char dir_path[VFS_MAX_PATH];
    dir_path[0] = '\0';
    vfs_node_t *stack[32];
    int depth = 0;
    vfs_node_t *cur = dir;
    while (cur && cur->parent && cur != cur->parent && depth < 32) {
        stack[depth++] = cur;
        cur = cur->parent;
    }

    strcpy(dir_path, "/");
    for (int i = depth - 1; i >= 0; i--) {
        if (strlen(dir_path) > 1) strncat(dir_path, "/", sizeof(dir_path) - strlen(dir_path) - 1);
        strncat(dir_path, stack[i]->name, sizeof(dir_path) - strlen(dir_path) - 1);
    }

    snprintf(out_path, out_max, "%s%s%s",
             dir_path, (strcmp(dir_path, "/") == 0) ? "" : "/", raw);
    return 0;
}

int64_t sys_mkdirat(int dirfd, const char *pathname, uint32_t mode) {
    char kpath[VFS_MAX_PATH];
    int err = resolve_at_path(dirfd, pathname, kpath, sizeof(kpath));
    if (err != 0) return err;
    return vfs_mkdir(kpath, mode);
}

int64_t sys_mkdir(const char *pathname, uint32_t mode) {
    return sys_mkdirat(AT_FDCWD, pathname, mode);
}

int64_t sys_unlinkat(int dirfd, const char *pathname, int flags) {
    char kpath[VFS_MAX_PATH];
    int err = resolve_at_path(dirfd, pathname, kpath, sizeof(kpath));
    if (err != 0) return err;

    if (flags & AT_REMOVEDIR) {
        return vfs_rmdir(kpath);
    }
    return vfs_unlink(kpath);
}

int64_t sys_unlink(const char *pathname) {
    return sys_unlinkat(AT_FDCWD, pathname, 0);
}

int64_t sys_rmdir(const char *pathname) {
    return sys_unlinkat(AT_FDCWD, pathname, AT_REMOVEDIR);
}

int64_t sys_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    char kold[VFS_MAX_PATH];
    char knew[VFS_MAX_PATH];

    int err = resolve_at_path(olddirfd, oldpath, kold, sizeof(kold));
    if (err != 0) return err;

    err = resolve_at_path(newdirfd, newpath, knew, sizeof(knew));
    if (err != 0) return err;

    return vfs_rename(kold, knew);
}

int64_t sys_rename(const char *oldpath, const char *newpath) {
    return sys_renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

int64_t sys_lseek(int fd, int64_t offset, int whence) {
    if (!g_current_thread || !g_current_thread->process) return -EINVAL;
    process_t *proc = g_current_thread->process;

    if (fd < 0 || fd >= MAX_FDS || !proc->fd_table[fd]) return -EBADF;
    vfs_file_t *file = proc->fd_table[fd];
    if (!file->node) return -EBADF;

    /* Pipes, sockets, and character devices (console/tty) cannot be seeked */
    if (file->node->flags & VFS_CHARDEVICE) {
        return -ESPIPE;
    }

    int64_t new_offset = 0;
    if (whence == SEEK_SET) {
        new_offset = offset;
    } else if (whence == SEEK_CUR) {
        new_offset = (int64_t)file->offset + offset;
    } else if (whence == SEEK_END) {
        new_offset = (int64_t)file->node->size + offset;
    } else {
        return -EINVAL;
    }

    if (new_offset < 0) return -EINVAL;

    file->offset = (uint64_t)new_offset;
    return new_offset;
}

int64_t sys_ftruncate(int fd, uint64_t length) {
    if (!g_current_thread || !g_current_thread->process) return -EINVAL;
    process_t *proc = g_current_thread->process;
    if (fd < 0 || fd >= MAX_FDS || !proc->fd_table[fd]) return -EBADF;

    vfs_node_t *node = proc->fd_table[fd]->node;
    if (!node || !node->ops || !node->ops->truncate) return -EINVAL;
    return node->ops->truncate(node, length);
}

int64_t sys_truncate(const char *path, uint64_t length) {
    if (!path || !validate_user_buffer((void *)path, 1)) return -EFAULT;
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return -ENOENT;
    if (!node->ops || !node->ops->truncate) return -EINVAL;
    return node->ops->truncate(node, length);
}

int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    if (!buf || bufsiz == 0 || !validate_user_buffer(buf, bufsiz)) return -EFAULT;

    char kpath[VFS_MAX_PATH];
    int err = resolve_at_path(dirfd, pathname, kpath, sizeof(kpath));
    if (err != 0) return err;

    vfs_node_t *node = vfs_resolve_path_nofollow(kpath);
    if (!node) return -ENOENT;
    if (!(node->flags & VFS_SYMLINK) || !node->ops || !node->ops->readlink) {
        return -EINVAL;
    }

    char ktarget[VFS_MAX_PATH];
    int64_t n = node->ops->readlink(node, ktarget, sizeof(ktarget));
    if (n < 0) return n;

    size_t to_copy = ((size_t)n < bufsiz) ? (size_t)n : bufsiz;
    memcpy(buf, ktarget, to_copy);
    return (int64_t)to_copy;
}

int64_t sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
    return sys_readlinkat(AT_FDCWD, pathname, buf, bufsiz);
}

int64_t sys_nanosleep(const struct linux_timeval* ts, struct linux_timeval* unused) {
    (void)unused;
    if (!ts || (uintptr_t)ts >= 0x0000800000000000ULL) {
        return (uint64_t)-EFAULT;
    }

    if (ts->tv_usec < 0 || ts->tv_usec < 0 || ts->tv_usec >= 1000000000L) {
        return (uint64_t)-EINVAL;
    }

    uint32_t ms = (uint32_t)(ts->tv_sec * 1000 + ts->tv_usec / 1000000L);
    sleep_ms(ms);
    return 0;
}

/* cpu/syscall.c */
int64_t sys_uname(struct linux_utsname *buf) {
    if (!buf || !validate_user_buffer(buf, sizeof(struct linux_utsname))) {
        return -EFAULT;
    }

    struct linux_utsname uts;
    memset(&uts, 0, sizeof(uts));

    strcpy(uts.sysname,    "JackOS");
    strcpy(uts.nodename,   "jackos");
    strcpy(uts.release,    "0.1.0");
    strcpy(uts.version,    "#1 SMP x86_64");
    strcpy(uts.machine,    "x86_64");
    strcpy(uts.domainname, "(none)");

    memcpy(buf, &uts, sizeof(uts));
    return 0;
}

extern spinlock_t s_sched_lock;

int64_t sys_sysinfo(struct linux_sysinfo *info) {
    if (!info || !validate_user_buffer(info, sizeof(struct linux_sysinfo))) {
        return -EFAULT;
    }

    struct linux_sysinfo kinfo;
    memset(&kinfo, 0, sizeof(kinfo));

    uint64_t total_frames = pmm_get_total_frames();
    uint64_t free_frames  = pmm_get_free_frames();

    kinfo.uptime   = (int64_t)(g_ticks / 1000);
    kinfo.totalram = total_frames * PAGE_SIZE;
    kinfo.freeram  = free_frames * PAGE_SIZE;
    kinfo.mem_unit = 1;

    uint16_t proc_count = 0;
    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    for (process_t *p = g_process_list_head; p != NULL; p = p->global_next) {
        proc_count++;
    }
    spinlock_irqrestore(&s_sched_lock, flags);
    kinfo.procs = proc_count;

    memcpy(info, &kinfo, sizeof(kinfo));
    return 0;
}

#define LINUX_REBOOT_MAGIC1        0xfee1dead
#define LINUX_REBOOT_MAGIC2        672274793
#define LINUX_REBOOT_CMD_RESTART   0x01234567
#define LINUX_REBOOT_CMD_HALT      0xcdef0123
#define LINUX_REBOOT_CMD_CAD_ON    0x89abcdef
#define LINUX_REBOOT_CMD_CAD_OFF   0x00000000
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

int64_t sys_reboot(int magic1, int magic2, uint32_t cmd, void* arg) {
    (void)arg;
    if ((uint32_t)magic1 != LINUX_REBOOT_MAGIC1 || magic2 != LINUX_REBOOT_MAGIC2) {
        return -EINVAL;
    }

    if (cmd == LINUX_REBOOT_CMD_CAD_ON || cmd == LINUX_REBOOT_CMD_CAD_OFF) {
        return 0;
    }

    if (cmd == LINUX_REBOOT_CMD_RESTART) {
        cli();
        while (inb(0x64) & 0x02);
        outb(0x64, 0xFE);
        while (1) hlt();
    }

    if (cmd == LINUX_REBOOT_CMD_POWER_OFF || cmd == LINUX_REBOOT_CMD_HALT) {
        cli();
        outw(0x604, 0x2000); // i440fx acpi shutdown
        outw(0xB004, 0x2000); // bochs / old qemu shutdown
        outw(0x4004, 0x3400); // virtualbox shutdown
        while (1) hlt();
    } 

    return -EINVAL;
}

int64_t sys_sync() {
    return 0;
}

int64_t sys_syslog(int type, char *bufp, int len) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    switch (type) {
        case SYSLOG_ACTION_CLOSE:
        case SYSLOG_ACTION_OPEN:
            return 0;

        case SYSLOG_ACTION_READ: {
            if (len < 0) return -EINVAL;
            if (len == 0) return 0;
            if (!bufp || !validate_user_buffer(bufp, (size_t)len)) {
                return -EFAULT;
            }
            while (klog_size_unread() == 0) {
                if (g_current_thread->process->sig_pending &
                    ~g_current_thread->process->sig_blocked) {
                    return -EINTR;
                }
                sched_yield();
            }
            return (int64_t)klog_read_consume(bufp, (size_t)len);
        }

        case SYSLOG_ACTION_READ_ALL:
        case SYSLOG_ACTION_READ_CLEAR: {
            if (len < 0) return -EINVAL;
            if (len == 0) return 0;
            if (!bufp || !validate_user_buffer(bufp, (size_t)len)) {
                return -EFAULT;
            }
            bool clear = (type == SYSLOG_ACTION_READ_CLEAR);
            return (int64_t)klog_read_all(bufp, (size_t)len, clear);
        }

        case SYSLOG_ACTION_CLEAR:
            klog_clear();
            return 0;

        case SYSLOG_ACTION_CONSOLE_OFF:
        case SYSLOG_ACTION_CONSOLE_ON:
        case SYSLOG_ACTION_CONSOLE_LEVEL:
            /* No-op console loglevel controls (return success for dmesg -n / -D / -E) */
            if (type == SYSLOG_ACTION_CONSOLE_LEVEL && (len < 1 || len > 8)) {
                return -EINVAL;
            }
            return 0;

        case SYSLOG_ACTION_SIZE_UNREAD:
            return (int64_t)klog_size_unread();

        case SYSLOG_ACTION_SIZE_BUFFER:
            return (int64_t)klog_size_total();

        default:
            return -EINVAL;
    }
}

int64_t sys_mprotect(void *addr, size_t len, int prot) {
    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    uintptr_t start = (uintptr_t)addr;
    if ((start & (PAGE_SIZE - 1)) != 0) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }

    size_t aligned_len = PAGE_ALIGN_UP(len);
    uintptr_t end = start + aligned_len;
    if (end < start || end > 0x0000800000000000ULL) {
        return -ENOMEM;
    }

    process_t *proc = g_current_thread->process;
    bool writable = (prot & PROT_WRITE) != 0;

    /* Update existing present PTEs in the range */
    for (uintptr_t vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        pt_entry_t *pte = vmm_get_pte(proc->pagemap, vaddr);
        if (pte && (*pte & PTE_PRESENT)) {
            if (writable && !(*pte & PTE_COW)) {
                *pte |= PTE_WRITABLE;
            } else if (!writable) {
                *pte &= ~PTE_WRITABLE;
            }
            vmm_invlpg(vaddr);
        }
    }

    /* Update VMA flags for un-faulted pages in this range */
    uint32_t new_vma_perms = 0;
    if (prot & PROT_READ)  new_vma_perms |= VMA_READ;
    if (prot & PROT_WRITE) new_vma_perms |= VMA_WRITE;
    if (prot & PROT_EXEC)  new_vma_perms |= VMA_EXEC;

    for (uintptr_t vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        vma_t *vma = vma_find(proc, vaddr);
        if (vma) {
            /* Split if mprotect covers a sub-range of a larger VMA */
            uint32_t kept_flags = vma->flags & ~(VMA_READ | VMA_WRITE | VMA_EXEC);
            vma_add_file(proc, vaddr, vaddr + PAGE_SIZE,
                         kept_flags | new_vma_perms,
                         vma->file,
                         vma->file ? (vma->file_offset + (vaddr - vma->start)) : 0);
        }
    }

    return 0;
}

int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    (void)flags;

    if (!g_current_thread || !g_current_thread->process) {
        return -ESRCH;
    }

    if (mode != F_OK && (mode & ~(R_OK | W_OK | X_OK)) != 0) {
        return -EINVAL;
    }

    char kpath[VFS_MAX_PATH];
    int err = resolve_at_path(dirfd, pathname, kpath, sizeof(kpath));
    if (err != 0) {
        return err;
    }

    vfs_node_t *node = vfs_resolve_path(kpath);
    if (!node) {
        return -ENOENT;
    }


    return 0;
}

int64_t sys_access(const char *pathname, int mode) {
    return sys_faccessat(AT_FDCWD, pathname, mode, 0);
}


// define syscall wrappers

/* I/O & File Descriptors */
DEFINE_SYSCALL3(sys_read,          int, void *, size_t)
DEFINE_SYSCALL3(sys_write,         int, const void *, size_t)
DEFINE_SYSCALL3(sys_open,          const char *, int, int)
DEFINE_SYSCALL1(sys_close,         int)
DEFINE_SYSCALL3(sys_lseek,         int, int64_t, int)
DEFINE_SYSCALL3(sys_ioctl,         int, uint64_t, void*)
DEFINE_SYSCALL3(sys_readv,         int, const struct iovec *, int)
DEFINE_SYSCALL3(sys_writev,        int, const struct iovec *, int)
DEFINE_SYSCALL3(sys_poll,          struct linux_pollfd *, uint64_t, int)
DEFINE_SYSCALL1(sys_pipe,          int *)
DEFINE_SYSCALL2(sys_pipe2,         int *, int)
DEFINE_SYSCALL1(sys_dup,           int)
DEFINE_SYSCALL2(sys_dup2,          int, int)
DEFINE_SYSCALL3(sys_dup3,          int, int, int)
DEFINE_SYSCALL3(sys_fcntl,         int, int, uint64_t)
DEFINE_SYSCALL3(sys_getdents64,    int, void *, size_t)
DEFINE_SYSCALL0(sys_sync)

/* Filesystem & Metadata */
DEFINE_SYSCALL2(sys_stat,          const char *, struct linux_stat *)
DEFINE_SYSCALL2(sys_fstat,         int, struct linux_stat *)
DEFINE_SYSCALL2(sys_lstat, const char *, struct linux_stat *)
DEFINE_SYSCALL4(sys_newfstatat,    int, const char *, struct linux_stat *, int)
DEFINE_SYSCALL2(sys_truncate,      const char *, uint64_t)
DEFINE_SYSCALL2(sys_ftruncate,     int, uint64_t)
DEFINE_SYSCALL2(sys_getcwd,        char *, size_t)
DEFINE_SYSCALL1(sys_chdir,         const char *)
DEFINE_SYSCALL2(sys_rename,        const char *, const char *)
DEFINE_SYSCALL2(sys_mkdir,         const char *, uint32_t)
DEFINE_SYSCALL1(sys_rmdir,         const char *)
DEFINE_SYSCALL1(sys_unlink,        const char *)
DEFINE_SYSCALL2(sys_symlink,       const char *, const char *)
DEFINE_SYSCALL3(sys_readlink,      const char *, char *, size_t)
DEFINE_SYSCALL3(sys_mkdirat,       int, const char *, uint32_t)
DEFINE_SYSCALL3(sys_unlinkat,      int, const char *, int)
DEFINE_SYSCALL4(sys_renameat,      int, const char *, int, const char *)
DEFINE_SYSCALL3(sys_symlinkat,     const char *, int, const char *)
DEFINE_SYSCALL4(sys_readlinkat,    int, const char *, char *, size_t)
DEFINE_SYSCALL3(sys_syslog, int, char *, int)
DEFINE_SYSCALL2(sys_access,        const char *, int)
DEFINE_SYSCALL4(sys_faccessat,     int, const char *, int, int)

/* Memory Management */
DEFINE_SYSCALL6(sys_mmap,          void *, size_t, int, int, int, int64_t)
DEFINE_SYSCALL2(sys_munmap,        void *, size_t)
DEFINE_SYSCALL1(sys_brk,           uintptr_t)
DEFINE_SYSCALL3(sys_mprotect, void*, size_t, int)

/* Process, Signals, Credentials & Time */
DEFINE_SYSCALL_REGS0(sys_fork)
DEFINE_SYSCALL_REGS3(sys_execve,   const char *, char *const *, char *const *)
DEFINE_SYSCALL1(sys_exit,          int)
DEFINE_SYSCALL4(sys_wait4,         int64_t, int *, int, void *)
DEFINE_SYSCALL2(sys_kill,          int64_t, int)
DEFINE_SYSCALL4(sys_rt_sigaction,  int, const struct sigaction *, struct sigaction *, size_t)
DEFINE_SYSCALL4(sys_rt_sigprocmask,int, const uint64_t *, uint64_t *, size_t)
DEFINE_SYSCALL_REGS0(sys_rt_sigreturn)
DEFINE_SYSCALL0(sys_getpid)
DEFINE_SYSCALL0(sys_getppid)
DEFINE_SYSCALL0(sys_getuid)
DEFINE_SYSCALL0(sys_geteuid)
DEFINE_SYSCALL0(sys_getgid)
DEFINE_SYSCALL0(sys_getegid)
DEFINE_SYSCALL2(sys_setpgid,       int64_t, int64_t)
DEFINE_SYSCALL1(sys_getpgid,       int64_t)
DEFINE_SYSCALL2(sys_arch_prctl,    int, uintptr_t)
DEFINE_SYSCALL1(sys_set_tid_address, int *)
DEFINE_SYSCALL2(sys_nanosleep,     const struct linux_timeval *, struct linux_timeval *)
DEFINE_SYSCALL2(sys_gettimeofday,  struct linux_timeval *, void *)
DEFINE_SYSCALL2(sys_clock_gettime, int, struct linux_timeval *)
DEFINE_SYSCALL2(sys_clock_getres,  int, struct linux_timeval *)
DEFINE_SYSCALL1(sys_uname, struct linux_utsname *)
DEFINE_SYSCALL1(sys_sysinfo, struct linux_sysinfo *)
DEFINE_SYSCALL4(sys_reboot, int, int, uint32_t, void*)

static const syscall_fn_t s_syscall_table[SYSCALL_MAX] = {
    [SYS_READ]            = wrap_sys_read,
    [SYS_WRITE]           = wrap_sys_write,
    [SYS_OPEN]            = wrap_sys_open,
    [SYS_CLOSE]           = wrap_sys_close,
    [SYS_STAT]            = wrap_sys_stat,
    [SYS_FSTAT]           = wrap_sys_fstat,
    [SYS_LSTAT]           = wrap_sys_lstat,
    [SYS_POLL]            = wrap_sys_poll,
    [SYS_LSEEK]           = wrap_sys_lseek,
    [SYS_MMAP]            = wrap_sys_mmap,
    [SYS_MPROTECT]        = wrap_sys_mprotect,
    [SYS_MUNMAP]          = wrap_sys_munmap,
    [SYS_BRK]             = wrap_sys_brk,
    [SYS_RT_SIGACTION]    = wrap_sys_rt_sigaction,
    [SYS_RT_SIGPROCMASK]  = wrap_sys_rt_sigprocmask,
    [SYS_RT_SIGRETURN]    = wrap_sys_rt_sigreturn,
    [SYS_IOCTL]           = wrap_sys_ioctl,
    [SYS_READV]           = wrap_sys_readv,
    [SYS_WRITEV]          = wrap_sys_writev,
    [SYS_ACCESS]          = wrap_sys_access,
    [SYS_PIPE]            = wrap_sys_pipe,
    [SYS_DUP]             = wrap_sys_dup,
    [SYS_DUP2]            = wrap_sys_dup2,
    [SYS_NANOSLEEP]       = wrap_sys_nanosleep,
    [SYS_GETPID]          = wrap_sys_getpid,
    [SYS_FORK]            = wrap_sys_fork,
    [SYS_EXECVE]          = wrap_sys_execve,
    [SYS_EXIT]            = wrap_sys_exit,
    [SYS_WAIT4]           = wrap_sys_wait4,
    [SYS_KILL]            = wrap_sys_kill,
    [SYS_UNAME]           = wrap_sys_uname,
    [SYS_FCNTL]           = wrap_sys_fcntl,
    [SYS_TRUNCATE]        = wrap_sys_truncate,
    [SYS_FTRUNCATE]       = wrap_sys_ftruncate,
    [SYS_GETCWD]          = wrap_sys_getcwd,
    [SYS_CHDIR]           = wrap_sys_chdir,
    [SYS_RENAME]          = wrap_sys_rename,
    [SYS_MKDIR]           = wrap_sys_mkdir,
    [SYS_RMDIR]           = wrap_sys_rmdir,
    [SYS_UNLINK]          = wrap_sys_unlink,
    [SYS_SYMLINK]         = wrap_sys_symlink,
    [SYS_READLINK]        = wrap_sys_readlink,
    [SYS_GETTIMEOFDAY]    = wrap_sys_gettimeofday,
    [SYS_SYSINFO]         = wrap_sys_sysinfo,
    [SYS_GETUID]          = wrap_sys_getuid,
    [SYS_SYSLOG]          = wrap_sys_syslog,
    [SYS_GETGID]          = wrap_sys_getgid,
    [SYS_GETEUID]         = wrap_sys_geteuid,
    [SYS_GETEGID]         = wrap_sys_getegid,
    [SYS_SETPGID]         = wrap_sys_setpgid,
    [SYS_GETPPID]         = wrap_sys_getppid,
    [SYS_GETPGID]         = wrap_sys_getpgid,
    [SYS_ARCH_PRCTL]      = wrap_sys_arch_prctl,
    [SYS_SYNC]            = wrap_sys_sync,
    [SYS_REBOOT]          = wrap_sys_reboot,
    [SYS_GETDENTS64]      = wrap_sys_getdents64,
    [SYS_SET_TID_ADDRESS] = wrap_sys_set_tid_address,
    [SYS_CLOCK_GETTIME]   = wrap_sys_clock_gettime,
    [SYS_CLOCK_GETRES]    = wrap_sys_clock_getres,
    [SYS_EXIT_GROUP]      = wrap_sys_exit,
    [SYS_MKDIRAT]         = wrap_sys_mkdirat,
    [SYS_NEWFSTATAT]      = wrap_sys_newfstatat,
    [SYS_UNLINKAT]        = wrap_sys_unlinkat,
    [SYS_RENAMEAT]        = wrap_sys_renameat,
    [SYS_SYMLINKAT]       = wrap_sys_symlinkat,
    [SYS_READLINKAT]      = wrap_sys_readlinkat,
    [SYS_FACCESSAT]       = wrap_sys_faccessat,
    [SYS_DUP3]            = wrap_sys_dup3,
    [SYS_PIPE2]           = wrap_sys_pipe2,
};

int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, syscall_saved_regs_t *regs) {
    if (num >= SYSCALL_MAX || !s_syscall_table[num]) {
        klog_printf("[SYSCALL] Unknown syscall: %llu\n", num);
        return -ENOSYS;
    }

    return s_syscall_table[num](a1, a2, a3, a4, a5, a6, regs);
}


int64_t syscall_entry_c(syscall_saved_regs_t *regs) {
    uint64_t num = regs->rax;
    int64_t ret = syscall_dispatch(num, regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8, regs->r9, regs);

    /* Check and deliver pending signals before sysret */
    if (num != SYS_RT_SIGRETURN) {
        handle_pending_signals(regs, &ret);
    }

    return ret;
}