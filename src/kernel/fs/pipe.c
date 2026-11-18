#include "fs/pipe.h"
#include "mem/kheap.h"
#include "sched/sched.h"
#include "sched/spinlock.h"
#include "cpu/syscall.h"
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#define PIPE_BUF_SIZE 4096

typedef struct pipe_buf {
    uint8_t    data[PIPE_BUF_SIZE];
    size_t     read_pos;
    size_t     write_pos;
    size_t     count;
    uint32_t   readers;
    uint32_t   writers;
    spinlock_t lock;
} pipe_buf_t;

typedef struct pipe_end {
    pipe_buf_t *pipe;
    bool        is_writer;
} pipe_end_t;

static int64_t pipe_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->node || !file->node->priv) return -EINVAL;
    if (size == 0) return 0;

    pipe_end_t *end = (pipe_end_t *)file->node->priv;
    pipe_buf_t *p = end->pipe;
    uint8_t *dst = (uint8_t *)buf;

    while (1) {
        uint64_t flags = spinlock_irqsave(&p->lock);

        if (p->count > 0) {
            size_t to_read = (size < p->count) ? size : p->count;
            for (size_t i = 0; i < to_read; i++) {
                dst[i] = p->data[p->read_pos];
                p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
            }
            p->count -= to_read;
            spinlock_irqrestore(&p->lock, flags);
            return (int64_t)to_read;
        }

        /* Buffer empty: if all write ends are closed, return EOF */
        if (p->writers == 0) {
            spinlock_irqrestore(&p->lock, flags);
            return 0;
        }

        if (file->flags & O_NONBLOCK) {
            spinlock_irqrestore(&p->lock, flags);
            return -EAGAIN;
        }

        spinlock_irqrestore(&p->lock, flags);
        sched_yield();
    }
}

static int64_t pipe_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->node || !file->node->priv) return -EINVAL;
    if (size == 0) return 0;

    pipe_end_t *end = (pipe_end_t *)file->node->priv;
    pipe_buf_t *p = end->pipe;
    const uint8_t *src = (const uint8_t *)buf;
    size_t total_written = 0;

    while (total_written < size) {
        uint64_t flags = spinlock_irqsave(&p->lock);

        /* Broken pipe: all read ends closed */
        if (p->readers == 0) {
            spinlock_irqrestore(&p->lock, flags);
            return (total_written > 0) ? (int64_t)total_written : -EPIPE;
        }

        size_t space = PIPE_BUF_SIZE - p->count;
        if (space > 0) {
            size_t chunk = size - total_written;
            if (chunk > space) chunk = space;

            for (size_t i = 0; i < chunk; i++) {
                p->data[p->write_pos] = src[total_written + i];
                p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
            }
            p->count += chunk;
            total_written += chunk;

            spinlock_irqrestore(&p->lock, flags);
            continue;
        }

        if (file->flags & O_NONBLOCK) {
            spinlock_irqrestore(&p->lock, flags);
            return (total_written > 0) ? (int64_t)total_written : -EAGAIN;
        }

        spinlock_irqrestore(&p->lock, flags);
        sched_yield();
    }

    return (int64_t)total_written;
}

static void pipe_close(vfs_node_t *node) {
    if (!node || !node->priv) return;

    pipe_end_t *end = (pipe_end_t *)node->priv;
    pipe_buf_t *p = end->pipe;

    uint64_t flags = spinlock_irqsave(&p->lock);
    if (end->is_writer) {
        if (p->writers > 0) p->writers--;
    } else {
        if (p->readers > 0) p->readers--;
    }
    bool free_pipe = (p->readers == 0 && p->writers == 0);
    spinlock_irqrestore(&p->lock, flags);

    if (free_pipe) {
        kfree(p);
    }
    kfree(end);
    kfree(node);
}

static vfs_ops_t s_pipe_ops = {
    .read  = pipe_read,
    .write = pipe_write,
    .close = pipe_close,
};

int pipe_create(vfs_file_t **read_file_out, vfs_file_t **write_file_out, int flags) {
    pipe_buf_t *p = (pipe_buf_t *)kmalloc(sizeof(pipe_buf_t));
    pipe_end_t *rend = (pipe_end_t *)kmalloc(sizeof(pipe_end_t));
    pipe_end_t *wend = (pipe_end_t *)kmalloc(sizeof(pipe_end_t));
    vfs_node_t *rnode = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    vfs_node_t *wnode = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    vfs_file_t *rfile = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    vfs_file_t *wfile = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));

    if (!p || !rend || !wend || !rnode || !wnode || !rfile || !wfile) {
        if (p) kfree(p);
        if (rend) kfree(rend);
        if (wend) kfree(wend);
        if (rnode) kfree(rnode);
        if (wnode) kfree(wnode);
        if (rfile) kfree(rfile);
        if (wfile) kfree(wfile);
        return -ENOMEM;
    }

    memset(p, 0, sizeof(pipe_buf_t));
    p->readers = 1;
    p->writers = 1;
    p->lock = SPINLOCK_INIT;

    rend->pipe = p;
    rend->is_writer = false;

    wend->pipe = p;
    wend->is_writer = true;

    memset(rnode, 0, sizeof(vfs_node_t));
    strcpy(rnode->name, "pipe:[r]");
    rnode->flags = VFS_CHARDEVICE;
    rnode->ops   = &s_pipe_ops;
    rnode->priv  = rend;

    memset(wnode, 0, sizeof(vfs_node_t));
    strcpy(wnode->name, "pipe:[w]");
    wnode->flags = VFS_CHARDEVICE;
    wnode->ops   = &s_pipe_ops;
    wnode->priv  = wend;

    int nonblock = (flags & O_NONBLOCK) ? O_NONBLOCK : 0;

    memset(rfile, 0, sizeof(vfs_file_t));
    rfile->node     = rnode;
    rfile->flags    = O_RDONLY | nonblock;
    rfile->refcount = 1;

    memset(wfile, 0, sizeof(vfs_file_t));
    wfile->node     = wnode;
    wfile->flags    = O_WRONLY | nonblock;
    wfile->refcount = 1;

    *read_file_out  = rfile;
    *write_file_out = wfile;
    return 0;
}