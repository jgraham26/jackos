#include "fs/proc.h"
#include "fs/vfs.h"
#include "sched/process.h"
#include "sched/spinlock.h"
#include "mem/kheap.h"
#include "mem/pmm.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

extern process_t* g_process_list_head;
extern spinlock_t s_sched_lock;
extern volatile uint64_t g_ticks;

typedef enum {
    PROC_NODE_ROOT,
    PROC_NODE_UPTIME,
    PROC_NODE_MEMINFO,
    PROC_NODE_VERSION,
    PROC_NODE_MOUNTS,
    PROC_NODE_STAT,
    PROC_NODE_PID_DIR,
    PROC_NODE_PID_STAT,
    PROC_NODE_PID_STATUS,
    PROC_NODE_PID_CMDLINE,
    PROC_NODE_PID_CWD,
} procfs_node_type_t;

typedef struct {
    procfs_node_type_t type;
    uint64_t pid;
} procfs_priv_t;


static const vfs_ops_t s_procfs_root_ops;
static const vfs_ops_t s_procfs_pid_dir_ops;
static const vfs_ops_t s_procfs_file_ops;
static const vfs_ops_t s_procfs_symlink_ops;

static vfs_node_t *procfs_alloc_node(const char *name, uint32_t flags, uint64_t inode,
                                     procfs_node_type_t type, uint64_t pid,
                                     const vfs_ops_t *ops) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(vfs_node_t));

    procfs_priv_t *priv = (procfs_priv_t *)kmalloc(sizeof(procfs_priv_t));
    if (!priv) {
        kfree(node);
        return NULL;
    }
    priv->type = type;
    priv->pid  = pid;

    strncpy(node->name, name, sizeof(node->name) - 1);
    node->flags = flags;
    node->inode = inode;
    node->size  = 0;
    node->priv  = priv;
    node->ops   = ops;
    return node;
}

static bool parse_pid_str(const char *s, uint64_t *out_pid) {
    if (!s || !*s) return false;
    uint64_t val = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        val = val * 10 + (uint64_t)(*p - '0');
    }
    *out_pid = val;
    return true;
}

static process_t *find_process_by_pid_unlocked(uint64_t pid) {
    process_t *curr = g_process_list_head;
    while (curr) {
        if (curr->pid == pid) return curr;
        curr = curr->global_next;
    }
    return NULL;
}

/* --- File Read Synthesis --- */
static int64_t procfs_file_read(vfs_file_t *file, void *buf, size_t count) {
    if (!file || !file->node || !file->node->priv || !buf) return -EINVAL;
    procfs_priv_t *priv = (procfs_priv_t *)file->node->priv;

    char tmp[512];
    size_t len = 0;

    switch (priv->type) {
        case PROC_NODE_UPTIME: {
            /* Assuming 1000 Hz PIT/APIC timer (1 tick = 1 ms) */
            uint64_t total_cs = g_ticks / 10;
            uint64_t secs = total_cs / 100;
            uint64_t cs   = total_cs % 100;
            len = (size_t)snprintf(tmp, sizeof(tmp), "%llu.%02llu %llu.%02llu\n",
                                   secs, cs, secs, cs);
            break;
        }
        case PROC_NODE_MEMINFO: {
            uint64_t total_frames = pmm_get_total_frames();
            uint64_t free_frames  = pmm_get_free_frames();

            uint64_t total_kb = total_frames * 4;
            uint64_t free_kb  = free_frames * 4;
            /* With 0 page cache/buffers, MemAvailable equals MemFree */
            uint64_t avail_kb = free_kb;

            len = (size_t)snprintf(tmp, sizeof(tmp),
                "MemTotal:       %8llu kB\n"
                "MemFree:        %8llu kB\n"
                "MemAvailable:   %8llu kB\n"
                "Buffers:               0 kB\n"
                "Cached:                0 kB\n",
                total_kb, free_kb, avail_kb);
            break;
        }
        case PROC_NODE_VERSION: {
            len = (size_t)snprintf(tmp, sizeof(tmp),
                "Linux version 6.1.0-jackos (gcc) #1 SMP x86_64\n");
            break;
        }
        case PROC_NODE_MOUNTS: {
            len = (size_t)snprintf(tmp, sizeof(tmp),
                "/dev/sda / ext2 rw 0 0\n"
                "proc /proc proc rw 0 0\n");
            break;
        }
        case PROC_NODE_PID_STAT: {
            uint64_t flags = spinlock_irqsave(&s_sched_lock);
            process_t *p = find_process_by_pid_unlocked(priv->pid);
            if (!p) {
                spinlock_irqrestore(&s_sched_lock, flags);
                return -ENOENT;
            }
            char state_ch = (p->state == PROC_ZOMBIE) ? 'Z' : 'R';
            uint64_t ppid = p->parent ? p->parent->pid : 0;
            uint64_t pgid = p->pgid;
            const char *comm = (p->comm[0] != '\0') ? p->comm : "jackos";
            /* Standard 52-field Linux /proc/[pid]/stat layout expected by BusyBox ps/top */
            len = (size_t)snprintf(tmp, sizeof(tmp),
                "%llu (%s) %c %llu %llu %llu 0 -1 4194304 0 0 0 0 0 0 0 0 20 0 1 0 0 4096 0 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
                p->pid, comm, state_ch, ppid, pgid, pgid);
            spinlock_irqrestore(&s_sched_lock, flags);
            break;
        }
        case PROC_NODE_PID_STATUS: {
            uint64_t flags = spinlock_irqsave(&s_sched_lock);
            process_t *p = find_process_by_pid_unlocked(priv->pid);
            if (!p) {
                spinlock_irqrestore(&s_sched_lock, flags);
                return -ENOENT;
            }
            const char *state_str = (p->state == PROC_ZOMBIE) ? "Z (zombie)" : "R (running)";
            uint64_t ppid = p->parent ? p->parent->pid : 0;
            const char *comm = (p->comm[0] != '\0') ? p->comm : "jackos";
            len = (size_t)snprintf(tmp, sizeof(tmp),
                "Name:\t%s\n"
                "State:\t%s\n"
                "Tgid:\t%llu\n"
                "Pid:\t%llu\n"
                "PPid:\t%llu\n"
                "Uid:\t0\t0\t0\t0\n"
                "Gid:\t0\t0\t0\t0\n"
                "VmRSS:\t    1024 kB\n",
                comm, state_str, p->pid, p->pid, ppid);
            spinlock_irqrestore(&s_sched_lock, flags);
            break;
        }
        case PROC_NODE_PID_CMDLINE: {
            uint64_t flags = spinlock_irqsave(&s_sched_lock);
            process_t *p = find_process_by_pid_unlocked(priv->pid);
            if (!p) {
                spinlock_irqrestore(&s_sched_lock, flags);
                return -ENOENT;
            }
            if (p->cmdline_len > 0 && p->cmdline_len <= sizeof(tmp)) {
                memcpy(tmp, p->cmdline, p->cmdline_len);
                len = p->cmdline_len;
            } else if (p->comm[0] != '\0') {
                len = (size_t)snprintf(tmp, sizeof(tmp), "%s", p->comm) + 1;
            }
            spinlock_irqrestore(&s_sched_lock, flags);
            break;
        }
        case PROC_NODE_STAT: {
            /* Convert g_ticks (ms) to 100 Hz USER_HZ jiffies */
            uint64_t total_jiffies = g_ticks / 10;
            uint64_t sys_jiffies   = total_jiffies / 10;
            uint64_t idle_jiffies  = total_jiffies - sys_jiffies;

            uint64_t procs_running = 0;
            uint64_t flags = spinlock_irqsave(&s_sched_lock);
            for (process_t *p = g_process_list_head; p != NULL; p = p->global_next) {
                if (p->state == PROC_ACTIVE) procs_running++;
            }
            spinlock_irqrestore(&s_sched_lock, flags);

            len = (size_t)snprintf(tmp, sizeof(tmp),
                "cpu  0 0 %llu %llu 0 0 0 0 0 0\n"
                "cpu0 0 0 %llu %llu 0 0 0 0 0 0\n"
                "intr 0\n"
                "ctxt 0\n"
                "btime 0\n"
                "processes %llu\n"
                "procs_running %llu\n"
                "procs_blocked 0\n",
                sys_jiffies, idle_jiffies,
                sys_jiffies, idle_jiffies,
                procs_running, procs_running);
            break;
        }
        default:
            return -EINVAL;
    }

    if (file->offset >= len) return 0;
    size_t avail = len - (size_t)file->offset;
    size_t to_copy = (count < avail) ? count : avail;
    memcpy(buf, tmp + file->offset, to_copy);
    file->offset += to_copy;
    return (int64_t)to_copy;
}

/* --- /proc/<pid>/cwd Symlink Readlink --- */
static int64_t procfs_cwd_readlink(vfs_node_t *node, char *buf, size_t size) {
    if (!node || !node->priv || !buf || size == 0) return -EINVAL;
    procfs_priv_t *priv = (procfs_priv_t *)node->priv;

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    process_t *p = find_process_by_pid_unlocked(priv->pid);
    if (!p) {
        spinlock_irqrestore(&s_sched_lock, flags);
        return -ENOENT;
    }
    const char *cwd = (p->cwd_path[0] != '\0') ? p->cwd_path : "/";
    size_t len = strlen(cwd);
    size_t to_copy = (len < size) ? len : size;
    memcpy(buf, cwd, to_copy);
    spinlock_irqrestore(&s_sched_lock, flags);
    return (int64_t)to_copy;
}

/* --- /proc/<pid>/ Directory Lookup & Readdir --- */
static vfs_node_t *procfs_pid_lookup(vfs_node_t *dir, const char *name) {
    if (!dir || !dir->priv || !name) return NULL;
    procfs_priv_t *priv = (procfs_priv_t *)dir->priv;
    uint64_t pid = priv->pid;

    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    bool exists = (find_process_by_pid_unlocked(pid) != NULL);
    spinlock_irqrestore(&s_sched_lock, flags);
    if (!exists) return NULL;

    uint64_t base_ino = (pid << 16);
    if (strcmp(name, "stat") == 0)
        return procfs_alloc_node("stat", VFS_FILE, base_ino + 1, PROC_NODE_PID_STAT, pid, &s_procfs_file_ops);
    if (strcmp(name, "status") == 0)
        return procfs_alloc_node("status", VFS_FILE, base_ino + 2, PROC_NODE_PID_STATUS, pid, &s_procfs_file_ops);
    if (strcmp(name, "cmdline") == 0)
        return procfs_alloc_node("cmdline", VFS_FILE, base_ino + 3, PROC_NODE_PID_CMDLINE, pid, &s_procfs_file_ops);
    if (strcmp(name, "cwd") == 0)
        return procfs_alloc_node("cwd", VFS_SYMLINK, base_ino + 4, PROC_NODE_PID_CWD, pid, &s_procfs_symlink_ops);

    return NULL;
}

static int procfs_pid_readdir(vfs_node_t *dir, uint64_t index, struct vfs_dirent *out) {
    if (!dir || !dir->priv || !out) return -EINVAL;
    procfs_priv_t *priv = (procfs_priv_t *)dir->priv;
    uint64_t base_ino = (priv->pid << 16);

    static const struct { const char *name; uint8_t type; uint64_t off; } entries[] = {
        { ".",       VFS_DIRECTORY, 0 },
        { "..",      VFS_DIRECTORY, 0 },
        { "stat",    VFS_FILE,      1 },
        { "status",  VFS_FILE,      2 },
        { "cmdline", VFS_FILE,      3 },
        { "cwd",     VFS_SYMLINK,   4 },
    };

    if (index >= 6) return -ENOENT;
    strncpy(out->name, entries[index].name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->type  = entries[index].type;
    out->inode = base_ino + entries[index].off;
    return 0;
}

/* --- /proc Root Lookup & Readdir --- */
static vfs_node_t *procfs_root_lookup(vfs_node_t *dir, const char *name) {
    (void)dir;
    if (!name) return NULL;

    if (strcmp(name, "uptime") == 0)
        return procfs_alloc_node("uptime", VFS_FILE, 2, PROC_NODE_UPTIME, 0, &s_procfs_file_ops);
    if (strcmp(name, "meminfo") == 0)
        return procfs_alloc_node("meminfo", VFS_FILE, 3, PROC_NODE_MEMINFO, 0, &s_procfs_file_ops);
    if (strcmp(name, "version") == 0)
        return procfs_alloc_node("version", VFS_FILE, 4, PROC_NODE_VERSION, 0, &s_procfs_file_ops);
    if (strcmp(name, "mounts") == 0)
        return procfs_alloc_node("mounts", VFS_FILE, 5, PROC_NODE_MOUNTS, 0, &s_procfs_file_ops);
    if (strcmp(name, "stat") == 0)
        return procfs_alloc_node("stat", VFS_FILE, 6, PROC_NODE_STAT, 0, &s_procfs_file_ops);

    uint64_t pid = 0;
    if (parse_pid_str(name, &pid) && pid > 0) {
        uint64_t flags = spinlock_irqsave(&s_sched_lock);
        bool exists = (find_process_by_pid_unlocked(pid) != NULL);
        spinlock_irqrestore(&s_sched_lock, flags);
        if (exists) {
            return procfs_alloc_node(name, VFS_DIRECTORY, (pid << 16), PROC_NODE_PID_DIR, pid, &s_procfs_pid_dir_ops);
        }
    }

    return NULL;
}

static int procfs_root_readdir(vfs_node_t *dir, uint64_t index, struct vfs_dirent *out) {
    (void)dir;
    if (!out) return -EINVAL;

    static const struct { const char *name; uint8_t type; uint64_t ino; } static_entries[] = {
        { ".",       VFS_DIRECTORY, 1 },
        { "..",      VFS_DIRECTORY, 1 },
        { "uptime",  VFS_FILE,      2 },
        { "meminfo", VFS_FILE,      3 },
        { "version", VFS_FILE,      4 },
        { "mounts",  VFS_FILE,      5 },
        {"stat", VFS_FILE, 6}
    };

    if (index < 7) {
        strncpy(out->name, static_entries[index].name, sizeof(out->name) - 1);
        out->name[sizeof(out->name) - 1] = '\0';
        out->type  = static_entries[index].type;
        out->inode = static_entries[index].ino;
        return 0;
    }

    uint64_t target_idx = index - 7;
    uint64_t flags = spinlock_irqsave(&s_sched_lock);
    process_t *curr = g_process_list_head;
    uint64_t i = 0;
    while (curr) {
        if (i == target_idx) {
            snprintf(out->name, sizeof(out->name), "%llu", curr->pid);
            out->type  = VFS_DIRECTORY;
            out->inode = (curr->pid << 16);
            spinlock_irqrestore(&s_sched_lock, flags);
            return 0;
        }
        i++;
        curr = curr->global_next;
    }
    spinlock_irqrestore(&s_sched_lock, flags);
    return -ENOENT;
}

static const vfs_ops_t s_procfs_root_ops = {
    .lookup  = procfs_root_lookup,
    .readdir = procfs_root_readdir,
};

static const vfs_ops_t s_procfs_pid_dir_ops = {
    .lookup  = procfs_pid_lookup,
    .readdir = procfs_pid_readdir,
};

static const vfs_ops_t s_procfs_file_ops = {
    .read = procfs_file_read,
};

static const vfs_ops_t s_procfs_symlink_ops = {
    .readlink = procfs_cwd_readlink,
};

vfs_node_t *procfs_create_root(void) {
    return procfs_alloc_node("proc", VFS_DIRECTORY, 1, PROC_NODE_ROOT, 0, &s_procfs_root_ops);
}