#include "fs/devfs.h"
#include "boot/bootinfo.h"
#include "cpu/syscall.h"
#include "fs/vfs.h"
#include "fs/bdev.h"
#include "mem/kheap.h"
#include "drivers/serial.h"
#include "drivers/ps2_kbd.h"
#include "cpu/idt.h"
#include "sched/sched.h"
#include "drivers/terminal.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static devfs_node_t *g_dev_list = NULL;
static vfs_node_t    g_devfs_root;

/* Defined in vfs.c */
extern vfs_node_t *s_root_node;
extern const vfs_ops_t s_bdev_node_ops;

static struct termios s_console_termios = {
    .c_iflag = ICRNL,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = 0000060 | 0000200, /* CS8 | CREAD */
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK,
    .c_line  = 0,
    .c_cc    = { [6] = 1, [5] = 0 }, /* VMIN = 1, VTIME = 0 */
};

/* --- Directory Ops for /dev --- */

static vfs_node_t *devfs_root_lookup(vfs_node_t *dir, const char *name) {
    (void)dir;
    if (!name) return NULL;

    /* Handle current and parent directory lookups */
    if (strcmp(name, ".") == 0) {
        return &g_devfs_root;
    }
    if (strcmp(name, "..") == 0) {
        return s_root_node ? s_root_node : &g_devfs_root;
    }

    devfs_node_t *curr = g_dev_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return &curr->vfs_node;
        }
        curr = curr->next;
    }
    return NULL;
}

static int devfs_root_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (!out) return -1;

    if (index == 0) {
        strcpy(out->name, ".");
        out->inode = 1;
        out->type = VFS_DIRECTORY;
        return 0;
    }
    if (index == 1) {
        strcpy(out->name, "..");
        out->inode = 1;
        out->type = VFS_DIRECTORY;
        return 0;
    }

    uint64_t curr_idx = 2;
    devfs_node_t *curr = g_dev_list;
    while (curr) {
        if (curr_idx == index) {
            strncpy(out->name, curr->name, sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = '\0';
            out->inode = curr->vfs_node.inode;

            if (curr->vfs_node.flags & VFS_DIRECTORY) {
                out->type = VFS_DIRECTORY;
            } else if (curr->vfs_node.flags & VFS_CHARDEVICE) {
                out->type = VFS_CHARDEVICE;  /* 0x04 */
            } else if (curr->vfs_node.flags & VFS_BLOCKDEVICE) {
                out->type = VFS_BLOCKDEVICE; /* 0x08 */
            } else {
                out->type = VFS_FILE;
            }
            return 0;
        }
        curr_idx++;
        curr = curr->next;
    }

    return -1;
}

/* --- Common Device Handlers --- */

static int dev_generic_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return 0;
}

static void dev_generic_close(vfs_node_t *node) {
    (void)node;
}

/* --- /dev/null --- */

static int64_t dev_null_read(vfs_file_t *file, void *buf, size_t count) {
    (void)file; (void)buf; (void)count;
    return 0; /* EOF */
}

static int64_t dev_null_write(vfs_file_t *file, const void *buf, size_t count) {
    (void)file; (void)buf;
    return (int64_t)count; /* Discard all data */
}

/* --- /dev/zero --- */

static int64_t dev_zero_read(vfs_file_t *file, void *buf, size_t count) {
    (void)file;
    if (!buf || count == 0) return 0;
    memset(buf, 0, count);
    return (int64_t)count;
}

static int64_t dev_zero_write(vfs_file_t *file, const void *buf, size_t count) {
    (void)file; (void)buf;
    return (int64_t)count;
}

/* --- /dev/serial --- */

static int64_t dev_serial_read(vfs_file_t *file, void *buf, size_t count) {
    (void)file;
    if (!buf || count == 0) return 0;
    char *dest = (char *)buf;
    size_t i = 0;
    while (i < count) {
        char c = (char)read_serial();
        dest[i++] = c;
        if (c == '\n' || c == '\r') break;
    }
    return (int64_t)i;
}

static int64_t dev_serial_write(vfs_file_t *file, const void *buf, size_t count) {
    (void)file;
    if (!buf || count == 0) return 0;
    const char *src = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        write_serial(src[i]);
    }
    return (int64_t)count;
}

/* --- /dev/console --- */

static int64_t dev_console_read(vfs_file_t *file, void *buf, size_t count) {
    (void)file;
    if (!buf || count == 0) return 0;

    char *dest = (char *)buf;
    size_t read_bytes = 0;
    bool echo   = (s_console_termios.c_lflag & ECHO) != 0;
    bool icanon = (s_console_termios.c_lflag & ICANON) != 0;
    bool icrnl  = (s_console_termios.c_iflag & ICRNL) != 0;

    while (read_bytes < count) {
        int ch = kbd_getchar_nonblock();
        if (ch == -1) {
            if (!icanon && read_bytes > 0) break;
            if (g_current_thread && g_current_thread->process &&
                (g_current_thread->process->sig_pending & ~g_current_thread->process->sig_blocked)) {
                return (read_bytes > 0) ? (int64_t)read_bytes : -EINTR;
            }
            sched_yield();
            continue;
        }

        if (ch == '\r' && icrnl) {
            ch = '\n';
        }

        if (icanon && (ch == '\b' || ch == 127)) {
            if (read_bytes > 0) {
                read_bytes--;
                if (echo) {
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
            }
            continue;
        }

        dest[read_bytes++] = (char)ch;

        if (echo) {
            putchar((char)ch);
        }

        if (icanon && ch == '\n') {
            break;
        }
    }

    return (int64_t)read_bytes;
}

static int64_t dev_console_write(vfs_file_t *file, const void *buf, size_t count) {
    (void)file;
    if (!buf || count == 0) return 0;
    const char *src = (const char *)buf;
    bool onlcr = (s_console_termios.c_oflag & ONLCR) != 0;

    for (size_t i = 0; i < count; i++) {
        if (src[i] == '\f') {
            terminal_clear();
            continue;
        }
        if (src[i] == '\n' && onlcr) {
            putchar('\r');
        }
        putchar(src[i]);
    }

    return (int64_t)count;
}

static int64_t dev_console_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    (void)file;

    switch (request) {
        case TCGETS:
            if (!arg) return -14; /* -EFAULT */
            memcpy(arg, &s_console_termios, sizeof(struct termios));
            return 0;

        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            if (!arg) return -EFAULT;
            memcpy(&s_console_termios, arg, sizeof(struct termios));
            serial_printf("[TTY] TCSETS lflag=%#x iflag=%#x\n",
                          s_console_termios.c_lflag, s_console_termios.c_iflag);            memcpy(&s_console_termios, arg, sizeof(struct termios));
            return 0;

        case TIOCGWINSZ: {
            if (!arg) return -EFAULT;
            struct winsize *ws = (struct winsize *)arg;
            uint16_t rows = terminal_get_rows();
            uint16_t cols = terminal_get_cols();
            ws->ws_row = rows ? rows : 25;
            ws->ws_col = cols ? cols : 80;
            ws->ws_xpixel = (uint16_t)g_boot_info.fb.width;
            ws->ws_ypixel = (uint16_t)g_boot_info.fb.height;
            return 0;
        }

        case TIOCGPGRP:
            if (!arg) return -EFAULT;
            if (g_console_fg_pgid == 0 && g_current_thread && g_current_thread->process) {
                g_console_fg_pgid = (int32_t)g_current_thread->process->pgid;
            }
            *(int32_t *)arg = g_console_fg_pgid;
            return 0;

        
        case TIOCSPGRP:
            if (!arg) return -EFAULT;
            g_console_fg_pgid = *(int32_t *)arg;
            return 0;

        case TIOCSWINSZ:
            return 0;

        default:
            return -25; /* -ENOTTY */
    }
}

/* --- Operation Tables --- */

static const vfs_ops_t dev_console_ops = {
    .open    = dev_generic_open,
    .close   = dev_generic_close,
    .read    = dev_console_read,
    .write   = dev_console_write,
    .ioctl   = dev_console_ioctl,
    .lookup  = NULL,
    .readdir = NULL,
    .create  = NULL,
    .mkdir   = NULL,
};

static const vfs_ops_t dev_null_ops = {
    .open    = dev_generic_open,
    .close   = dev_generic_close,
    .read    = dev_null_read,
    .write   = dev_null_write,
    .lookup  = NULL,
    .readdir = NULL,
    .create  = NULL,
    .mkdir   = NULL,
};

static const vfs_ops_t dev_zero_ops = {
    .open    = dev_generic_open,
    .close   = dev_generic_close,
    .read    = dev_zero_read,
    .write   = dev_zero_write,
    .lookup  = NULL,
    .readdir = NULL,
    .create  = NULL,
    .mkdir   = NULL,
};

static const vfs_ops_t dev_serial_ops = {
    .open    = dev_generic_open,
    .close   = dev_generic_close,
    .read    = dev_serial_read,
    .write   = dev_serial_write,
    .lookup  = NULL,
    .readdir = NULL,
    .create  = NULL,
    .mkdir   = NULL,
};

static const vfs_ops_t devfs_dir_ops = {
    .open    = dev_generic_open,
    .close   = dev_generic_close,
    .read    = NULL,
    .write   = NULL,
    .lookup  = devfs_root_lookup,
    .readdir = devfs_root_readdir,
    .create  = NULL,
    .mkdir   = NULL,
};

/* --- Registration API --- */

static uint64_t s_dev_inode = 1000;

static devfs_node_t *g_input_dev_list = NULL;
static devfs_node_t *g_input_dir_node = NULL;

static vfs_node_t *devfs_input_lookup(vfs_node_t *dir, const char *name) {
    (void)dir;
    if (!name) return NULL;

    if (strcmp(name, ".") == 0) {
        return g_input_dir_node ? &g_input_dir_node->vfs_node : NULL;
    }
    if (strcmp(name, "..") == 0) {
        return &g_devfs_root;
    }

    devfs_node_t *curr = g_input_dev_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return &curr->vfs_node;
        }
        curr = curr->next;
    }
    return NULL;
}

static int devfs_input_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (!out) return -1;

    if (index == 0) {
        strcpy(out->name, ".");
        out->inode = g_input_dir_node ? g_input_dir_node->vfs_node.inode : 3;
        out->type = VFS_DIRECTORY;
        return 0;
    }
    if (index == 1) {
        strcpy(out->name, "..");
        out->inode = g_devfs_root.inode;
        out->type = VFS_DIRECTORY;
        return 0;
    }

    uint64_t curr_idx = 2;
    devfs_node_t *curr = g_input_dev_list;
    while (curr) {
        if (curr_idx == index) {
            strncpy(out->name, curr->name, sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = '\0';
            out->inode = curr->vfs_node.inode;
            out->type = VFS_CHARDEVICE;
            return 0;
        }
        curr_idx++;
        curr = curr->next;
    }

    return -1;
}

static const vfs_ops_t devfs_input_dir_ops = {
    .open    = dev_generic_open,
    .close   = dev_generic_close,
    .read    = NULL,
    .write   = NULL,
    .lookup  = devfs_input_lookup,
    .readdir = devfs_input_readdir,
    .create  = NULL,
    .mkdir   = NULL,
};

int devfs_register_device(const char *name, const vfs_ops_t *ops, void *private_data) {
    if (!name || !ops) return -1;

    bool is_input = (strncmp(name, "input/", 6) == 0);
    const char *dev_name = is_input ? (name + 6) : name;
    if (dev_name[0] == '\0') return -1;

    devfs_node_t *node = (devfs_node_t *)kmalloc(sizeof(devfs_node_t));
    if (!node) return -1;

    memset(node, 0, sizeof(devfs_node_t));
    strncpy(node->name, dev_name, sizeof(node->name) - 1);
    strncpy(node->vfs_node.name, dev_name, sizeof(node->vfs_node.name) - 1);

    node->vfs_node.inode  = s_dev_inode++;
    node->vfs_node.flags  = VFS_CHARDEVICE;
    node->vfs_node.ops    = (vfs_ops_t *)ops;
    node->vfs_node.priv   = private_data;

    if (is_input && g_input_dir_node) {
        node->vfs_node.parent = &g_input_dir_node->vfs_node;
        node->next = g_input_dev_list;
        g_input_dev_list = node;
    } else {
        node->vfs_node.parent = &g_devfs_root;
        node->next = g_dev_list;
        g_dev_list = node;
    }
    return 0;
}

int devfs_register_bdev(block_device_t *bdev) {
    if (!bdev) return -1;

    devfs_node_t *node = (devfs_node_t *)kmalloc(sizeof(devfs_node_t));
    if (!node) return -1;

    memset(node, 0, sizeof(devfs_node_t));
    strncpy(node->name, bdev->name, sizeof(node->name) - 1);
    strncpy(node->vfs_node.name, bdev->name, sizeof(node->vfs_node.name) - 1);

    node->vfs_node.inode  = s_dev_inode++;
    node->vfs_node.flags  = VFS_BLOCKDEVICE;
    node->vfs_node.ops    = (vfs_ops_t *)&s_bdev_node_ops;
    node->vfs_node.priv   = (void *)bdev;
    node->vfs_node.size   = bdev->total_blocks * bdev->block_size;
    node->vfs_node.parent = &g_devfs_root;

    node->next = g_dev_list;
    g_dev_list = node;
    return 0;
}

vfs_node_t *devfs_get_root(void) {
    return &g_devfs_root;
}

void devfs_init(void) {
    memset(&g_devfs_root, 0, sizeof(vfs_node_t));
    strcpy(g_devfs_root.name, "dev");
    g_devfs_root.inode  = 2;
    g_devfs_root.flags  = VFS_DIRECTORY;
    g_devfs_root.ops    = (vfs_ops_t *)&devfs_dir_ops;
    g_devfs_root.parent = s_root_node;

    /* Create /dev/input subdirectory node */
    g_input_dir_node = (devfs_node_t *)kmalloc(sizeof(devfs_node_t));
    if (g_input_dir_node) {
        memset(g_input_dir_node, 0, sizeof(devfs_node_t));
        strcpy(g_input_dir_node->name, "input");
        strcpy(g_input_dir_node->vfs_node.name, "input");
        g_input_dir_node->vfs_node.inode  = s_dev_inode++;
        g_input_dir_node->vfs_node.flags  = VFS_DIRECTORY;
        g_input_dir_node->vfs_node.ops    = (vfs_ops_t *)&devfs_input_dir_ops;
        g_input_dir_node->vfs_node.parent = &g_devfs_root;

        g_input_dir_node->next = g_dev_list;
        g_dev_list = g_input_dir_node;
    }

    /* Register standard character devices */
    devfs_register_device("null",    &dev_null_ops,    NULL);
    devfs_register_device("zero",    &dev_zero_ops,    NULL);
    devfs_register_device("serial",  &dev_serial_ops,  NULL);
    devfs_register_device("console", &dev_console_ops, NULL);
    devfs_register_device("tty",     &dev_console_ops, NULL);
}