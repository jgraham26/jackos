#include "fs/vfs.h"
#include "mem/kheap.h"
#include <string.h>
#include "sched/process.h"
#include "drivers/klog.h"
#include <stdio.h>
#include <errno.h>

#define MAX_MOUNTS 16

typedef struct {
    char path[VFS_MAX_PATH];
    size_t path_len;
    vfs_node_t *fs_root;
} vfs_mount_entry_t;

static vfs_mount_entry_t s_mounts[MAX_MOUNTS];
static size_t s_mount_count = 0;
vfs_node_t *s_root_node = NULL;



void path_canonicalize(char *path) {
    if (!path || path[0] == '\0') return;

    char temp[VFS_MAX_PATH];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *segments[64];
    int seg_count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(temp, "/", &saveptr);

    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            // Current directory: do nothing
        } else if (strcmp(token, "..") == 0) {
            // Parent directory: pop previous component if any
            if (seg_count > 0) {
                seg_count--;
            }
        } else if (strlen(token) > 0) {
            if (seg_count < 64) {
                segments[seg_count++] = token;
            }
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    /* Reconstruct canonical absolute path */
    path[0] = '/';
    path[1] = '\0';
    size_t cur_len = 1;

    for (int i = 0; i < seg_count; i++) {
        size_t seg_len = strlen(segments[i]);
        if (cur_len + seg_len + 1 >= VFS_MAX_PATH) break;

        if (cur_len > 1) {
            path[cur_len++] = '/';
            path[cur_len] = '\0';
        }
        memcpy(path + cur_len, segments[i], seg_len + 1);
        cur_len += seg_len;
    }
}

void vfs_init(void) {
    s_mount_count = 0;
    s_root_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(s_root_node, 0, sizeof(vfs_node_t));
    strcpy(s_root_node->name, "/");
    s_root_node->flags = VFS_DIRECTORY;
    s_root_node->parent = s_root_node;
}

int vfs_mount(const char *path, vfs_node_t *fs_root) {
    if (!path || !fs_root || s_mount_count >= MAX_MOUNTS) return -1;

    /* Normalize root */
    if (strcmp(path, "/") == 0) {
        s_root_node = fs_root;
        s_root_node->parent = s_root_node;
        return 0;
    }

    /* Strip any trailing slashes from mount path */
    char clean_path[VFS_MAX_PATH];
    strncpy(clean_path, path, sizeof(clean_path) - 1);
    clean_path[sizeof(clean_path) - 1] = '\0';
    size_t len = strlen(clean_path);
    while (len > 1 && clean_path[len - 1] == '/') {
        clean_path[--len] = '\0';
    }

    strncpy(s_mounts[s_mount_count].path, clean_path, VFS_MAX_PATH - 1);
    s_mounts[s_mount_count].path[VFS_MAX_PATH - 1] = '\0';
    s_mounts[s_mount_count].path_len = len;
    s_mounts[s_mount_count].fs_root = fs_root;
    if (fs_root && !fs_root->parent) fs_root->parent = s_root_node;
    s_mount_count++;
    return 0;
}

static vfs_node_t *vfs_resolve_internal(const char *path, bool follow_last) {
    if (!path || path[0] == '\0') return NULL;

    char cur_path[VFS_MAX_PATH];
    cur_path[0] = '\0';

    /* 1. Build initial absolute path */
    if (path[0] == '/') {
        strncpy(cur_path, path, sizeof(cur_path) - 1);
        cur_path[sizeof(cur_path) - 1] = '\0';
    } else {
        const char *cwd_path = "/";
        if (g_current_thread && g_current_thread->process) {
            process_t *proc = g_current_thread->process;
            if (proc->cwd_path[0] != '\0') {
                cwd_path = proc->cwd_path;
            }
        }

        strncpy(cur_path, cwd_path, sizeof(cur_path) - 1);
        cur_path[sizeof(cur_path) - 1] = '\0';

        if (strcmp(path, ".") != 0 && strcmp(path, "./") != 0) {
            size_t cwd_len = strlen(cur_path);
            if (cwd_len > 0 && cur_path[cwd_len - 1] != '/') {
                strncat(cur_path, "/", sizeof(cur_path) - cwd_len - 1);
            }
            const char *rel = path;
            if (rel[0] == '.' && rel[1] == '/') {
                rel += 2;
            }
            strncat(cur_path, rel, sizeof(cur_path) - strlen(cur_path) - 1);
        }
    }

    int symlink_depth = 0;

restart_resolve:
    path_canonicalize(cur_path);

    /* Find longest matching mount point */
    int best_mount = -1;
    size_t best_len = 0;

    for (size_t m = 0; m < s_mount_count; m++) {
        size_t mlen = s_mounts[m].path_len;
        if (strncmp(cur_path, s_mounts[m].path, mlen) == 0) {
            if (cur_path[mlen] == '\0' || cur_path[mlen] == '/') {
                if (mlen > best_len) {
                    best_len = mlen;
                    best_mount = (int)m;
                }
            }
        }
    }

    vfs_node_t *curr = NULL;
    const char *remainder = cur_path;
    char resolved_prefix[VFS_MAX_PATH];

    if (best_mount != -1) {
        curr = s_mounts[best_mount].fs_root;
        remainder = cur_path + best_len;
        strncpy(resolved_prefix, s_mounts[best_mount].path, sizeof(resolved_prefix) - 1);
        resolved_prefix[sizeof(resolved_prefix) - 1] = '\0';
    } else {
        curr = s_root_node;
        remainder = cur_path;
        strcpy(resolved_prefix, "/");
    }

    if (!curr) return NULL;

    while (*remainder == '/') {
        remainder++;
    }
    if (*remainder == '\0') {
        return curr;
    }

    /* Walk components */
    char walk_buf[VFS_MAX_PATH];
    strncpy(walk_buf, remainder, sizeof(walk_buf) - 1);
    walk_buf[sizeof(walk_buf) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(walk_buf, "/", &saveptr);

    while (token != NULL) {
        char *next_token = strtok_r(NULL, "/", &saveptr);
        bool is_last = (next_token == NULL);

        if (strcmp(token, ".") == 0) {
            token = next_token;
            continue;
        }

        if (strcmp(token, "..") == 0) {
            if (curr->parent) {
                curr = curr->parent;
            }
            char *slash = strrchr(resolved_prefix, '/');
            if (slash && slash != resolved_prefix) {
                *slash = '\0';
            } else {
                strcpy(resolved_prefix, "/");
            }
            token = next_token;
            continue;
        }

        if (!(curr->flags & VFS_DIRECTORY) || !curr->ops || !curr->ops->lookup) {
            return NULL;
        }

        vfs_node_t *next = curr->ops->lookup(curr, token);
        if (!next) {
            return NULL;
        }

        if (!next->parent) {
            next->parent = curr;
        }

        /* Handle symbolic link traversal */
        if ((next->flags & VFS_SYMLINK) && (!is_last || follow_last)) {
            if (++symlink_depth > 8 || !next->ops || !next->ops->readlink) {
                return NULL;
            }

            char link_target[VFS_MAX_PATH];
            int64_t n = next->ops->readlink(next, link_target, sizeof(link_target));
            if (n <= 0) {
                return NULL;
            }
            link_target[n] = '\0';

            char rebuilt[VFS_MAX_PATH];
            rebuilt[0] = '\0';

            if (link_target[0] == '/') {
                /* Absolute symlink target (e.g. /usr/bin/busybox) */
                strncpy(rebuilt, link_target, sizeof(rebuilt) - 1);
                rebuilt[sizeof(rebuilt) - 1] = '\0';
            } else {
                /* Relative symlink target (e.g. busybox or ../usr/bin/busybox) */
                strncpy(rebuilt, resolved_prefix, sizeof(rebuilt) - 1);
                rebuilt[sizeof(rebuilt) - 1] = '\0';
                size_t rlen = strlen(rebuilt);
                if (rlen > 0 && rebuilt[rlen - 1] != '/') {
                    strncat(rebuilt, "/", sizeof(rebuilt) - rlen - 1);
                }
                strncat(rebuilt, link_target, sizeof(rebuilt) - strlen(rebuilt) - 1);
            }

            /* Append any remaining unparsed path components if this was an intermediate symlink */
            if (!is_last) {
                size_t rlen = strlen(rebuilt);
                if (rlen > 0 && rebuilt[rlen - 1] != '/') {
                    strncat(rebuilt, "/", sizeof(rebuilt) - rlen - 1);
                }
                strncat(rebuilt, next_token, sizeof(rebuilt) - strlen(rebuilt) - 1);

                /* Append anything left after next_token in saveptr */
                if (saveptr && *saveptr != '\0') {
                    strncat(rebuilt, "/", sizeof(rebuilt) - strlen(rebuilt) - 1);
                    strncat(rebuilt, saveptr, sizeof(rebuilt) - strlen(rebuilt) - 1);
                }
            }

            strncpy(cur_path, rebuilt, sizeof(cur_path) - 1);
            cur_path[sizeof(cur_path) - 1] = '\0';
            goto restart_resolve;
        }

        /* Advance resolved_prefix with accepted component */
        size_t plen = strlen(resolved_prefix);
        if (plen > 0 && resolved_prefix[plen - 1] != '/') {
            strncat(resolved_prefix, "/", sizeof(resolved_prefix) - plen - 1);
        }
        strncat(resolved_prefix, token, sizeof(resolved_prefix) - strlen(resolved_prefix) - 1);

        curr = next;
        token = next_token;
    }

    return curr;
}

vfs_node_t *vfs_resolve_path(const char *path) {
    return vfs_resolve_internal(path, true);
}

vfs_node_t *vfs_resolve_path_nofollow(const char *path) {
    return vfs_resolve_internal(path, false);
}

vfs_file_t *vfs_open(const char *path, uint32_t flags) {
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) return NULL;

    if (node->ops && node->ops->open) {
        if (node->ops->open(node, flags) != 0) {
            return NULL;
        }
    }

    vfs_file_t *file = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    if (!file) return NULL;

    file->node = node;
    file->offset = 0;
    file->flags = flags;
    file->refcount = 1;
    node->refcount++;

    return file;
}

int64_t vfs_read(vfs_file_t *file, void *buf, size_t count) {
    if (!file || !file->node || !buf || count == 0) return 0;
    if (!file->node->ops || !file->node->ops->read) return -1;

    return file->node->ops->read(file, buf, count);
}

int64_t vfs_write(vfs_file_t *file, const void *buf, size_t count) {
    if (!file || !file->node || !buf || count == 0) return 0;
    if (!file->node->ops || !file->node->ops->write) return -1;

    return file->node->ops->write(file, buf, count);
}

void vfs_close(vfs_file_t *file) {
    if (!file) return;

    if (file->refcount > 1) {
        file->refcount--;
        return;
    }

    /* refcount reached 0: invoke underlying close (e.g., pipe_close -> wake blocked readers with EOF) */
    if (file->node && file->node->ops && file->node->ops->close) {
        file->node->ops->close(file->node);
    }
    kfree(file);
}

void *vfs_read_entire_file(const char *path, size_t *out_size) {
    if (!path || !out_size) return NULL;

    vfs_file_t *file = vfs_open(path, O_RDONLY);
    if (!file) {
        klog_printf("[VFS] vfs_open('%s') returned NULL\n", path);
        return NULL;
    }

    if (!file->node) {
        klog_printf("[VFS] file->node is NULL for '%s'\n", path);
        vfs_close(file);
        return NULL;
    }

    if (file->node->flags & VFS_DIRECTORY) {
        klog_printf("[VFS] '%s' is a directory, not a regular file\n", path);
        vfs_close(file);
        return NULL;
    }

    size_t file_size = file->node->size;
    if (file_size == 0) {
        klog_printf("[VFS] '%s' reports size == 0\n", path);
        vfs_close(file);
        return NULL;
    }

    void *buf = kmalloc(file_size);
    if (!buf) {
        klog_printf("[VFS] kmalloc(%llu) failed for '%s'\n", (uint64_t)file_size, path);
        vfs_close(file);
        return NULL;
    }

    int64_t bytes_read = vfs_read(file, buf, file_size);
    klog_printf("[VFS] Read %lld / %llu bytes from '%s'\n", bytes_read, (uint64_t)file_size, path);

    vfs_close(file);

    if (bytes_read != (int64_t)file_size) {
        klog_printf("[VFS] Read size mismatch! Expected %llu, got %lld\n",
                      (uint64_t)file_size, bytes_read);
        kfree(buf);
        return NULL;
    }

    *out_size = file_size;
    return buf;
}

/* --- Block Device Node File Operations --- */

static int64_t bdev_node_read(vfs_file_t *file, void *buf, size_t count) {
    if (!file || !file->node || !file->node->priv || !buf || count == 0) return 0;

    block_device_t *bdev = (block_device_t *)file->node->priv;
    size_t bsize = bdev->block_size;
    if (bsize == 0) return -1;

    uint64_t start_lba = file->offset / bsize;
    size_t offset_in_block = file->offset % bsize;
    size_t bytes_read = 0;

    uint8_t *temp_block = (uint8_t *)kmalloc(bsize);
    if (!temp_block) return -1;

    uint8_t *dest = (uint8_t *)buf;

    while (bytes_read < count) {
        if (bdev_read(bdev, start_lba, 1, temp_block) != 0) {
            kfree(temp_block);
            return bytes_read > 0 ? (int64_t)bytes_read : -1;
        }

        size_t to_copy = bsize - offset_in_block;
        if (to_copy > (count - bytes_read)) {
            to_copy = count - bytes_read;
        }

        memcpy(dest + bytes_read, temp_block + offset_in_block, to_copy);
        bytes_read += to_copy;
        file->offset += to_copy;

        offset_in_block = 0;
        start_lba++;
    }

    kfree(temp_block);
    return (int64_t)bytes_read;
}

static int64_t bdev_node_write(vfs_file_t *file, const void *buf, size_t count) {
    if (!file || !file->node || !file->node->priv || !buf || count == 0) return 0;

    block_device_t *bdev = (block_device_t *)file->node->priv;
    size_t bsize = bdev->block_size;
    if (bsize == 0) return -1;

    uint64_t start_lba = file->offset / bsize;
    size_t offset_in_block = file->offset % bsize;
    size_t bytes_written = 0;

    uint8_t *temp_block = (uint8_t *)kmalloc(bsize);
    if (!temp_block) return -1;

    const uint8_t *src = (const uint8_t *)buf;

    while (bytes_written < count) {
        size_t to_copy = bsize - offset_in_block;
        if (to_copy > (count - bytes_written)) {
            to_copy = count - bytes_written;
        }

        /* Read-modify-write if partial block */
        if (offset_in_block != 0 || to_copy < bsize) {
            if (bdev_read(bdev, start_lba, 1, temp_block) != 0) {
                kfree(temp_block);
                return bytes_written > 0 ? (int64_t)bytes_written : -1;
            }
        }

        memcpy(temp_block + offset_in_block, src + bytes_written, to_copy);

        if (bdev_write(bdev, start_lba, 1, temp_block) != 0) {
            kfree(temp_block);
            return bytes_written > 0 ? (int64_t)bytes_written : -1;
        }

        bytes_written += to_copy;
        file->offset += to_copy;

        offset_in_block = 0;
        start_lba++;
    }

    kfree(temp_block);
    return (int64_t)bytes_written;
}

vfs_node_t *vfs_create(const char *path, uint32_t mode) {
    if (!path || path[0] == '\0') return NULL;

    char parent_path[VFS_MAX_PATH];
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    /* Find the last slash in the original path string */
    const char *last_slash_in_path = strrchr(path, '/');
    const char *name = path;

    if (last_slash_in_path) {
        name = last_slash_in_path + 1;  /* Points into original 'path', safely untouched */

        char *last_slash_in_parent = strrchr(parent_path, '/');
        if (last_slash_in_parent == parent_path) {
            parent_path[1] = '\0';      /* Root directory "/" */
        } else if (last_slash_in_parent) {
            *last_slash_in_parent = '\0';
        }
    } else {
        strcpy(parent_path, ".");
    }

    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) return NULL;
    if (!(parent->flags & VFS_DIRECTORY)) return NULL;
    if (!parent->ops || !parent->ops->create) return NULL;

    return parent->ops->create(parent, name, mode);
}

int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath || target[0] == '\0' || linkpath[0] == '\0') {
        return -EINVAL;
    }

    char parent_path[VFS_MAX_PATH];
    strncpy(parent_path, linkpath, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    /* Strip trailing slashes from linkpath if any */
    size_t len = strlen(parent_path);
    while (len > 1 && parent_path[len - 1] == '/') {
        parent_path[--len] = '\0';
    }

    const char *last_slash = strrchr(parent_path, '/');
    const char *name = parent_path;

    if (last_slash) {
        name = last_slash + 1;
        if (last_slash == parent_path) {
            /* Creating symlink in root "/" — copy name out before truncating parent_path */
            char name_copy[256];
            strncpy(name_copy, name, sizeof(name_copy) - 1);
            name_copy[sizeof(name_copy) - 1] = '\0';

            vfs_node_t *root = vfs_resolve_path("/");
            if (!root || !(root->flags & VFS_DIRECTORY) || !root->ops || !root->ops->symlink) {
                return -ENOSYS;
            }
            if (root->ops->lookup && root->ops->lookup(root, name_copy)) {
                return -EEXIST;
            }
            return root->ops->symlink(root, name_copy, target);
        }
        *(char *)last_slash = '\0';
    } else {
        /* Relative path in current directory */
        vfs_node_t *cwd = vfs_resolve_path(".");
        if (!cwd || !(cwd->flags & VFS_DIRECTORY) || !cwd->ops || !cwd->ops->symlink) {
            return -ENOSYS;
        }
        if (cwd->ops->lookup && cwd->ops->lookup(cwd, parent_path)) {
            return -EEXIST;
        }
        return cwd->ops->symlink(cwd, parent_path, target);
    }

    if (name[0] == '\0') return -EINVAL;

    vfs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent) return -ENOENT;
    if (!(parent->flags & VFS_DIRECTORY)) return -ENOTDIR;
    if (!parent->ops || !parent->ops->symlink) return -ENOSYS;

    /* Fail with -EEXIST if linkpath already exists */
    if (parent->ops->lookup && parent->ops->lookup(parent, name)) {
        return -EEXIST;
    }

    return parent->ops->symlink(parent, name, target);
}

/* Resolves parent directory of 'path' and copies the final component into 'out_name' */
static int vfs_resolve_parent_and_name(const char *path, vfs_node_t **out_parent, char *out_name, size_t name_max) {
    if (!path || path[0] == '\0' || !out_parent || !out_name) return -EINVAL;

    char buf[VFS_MAX_PATH];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Strip trailing slashes (preserving "/") */
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') {
        buf[--len] = '\0';
    }

    if (strcmp(buf, "/") == 0) return -EBUSY;

    char *last_slash = strrchr(buf, '/');
    const char *name = buf;
    vfs_node_t *parent = NULL;

    if (last_slash) {
        name = last_slash + 1;
        if (name[0] == '\0') return -EINVAL;
        strncpy(out_name, name, name_max - 1);
        out_name[name_max - 1] = '\0';

        if (last_slash == buf) {
            parent = vfs_resolve_path("/");
        } else {
            *last_slash = '\0';
            parent = vfs_resolve_path(buf);
        }
    } else {
        strncpy(out_name, buf, name_max - 1);
        out_name[name_max - 1] = '\0';
        parent = vfs_resolve_path(".");
    }

    if (!parent) return -ENOENT;
    if (!(parent->flags & VFS_DIRECTORY)) return -ENOTDIR;

    *out_parent = parent;
    return 0;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (!path || path[0] == '\0') return -EINVAL;

    /* Check if canonical path is "/" (e.g. mkdir -p walking from root) */
    char canon[VFS_MAX_PATH];
    if (path[0] == '/') {
        strncpy(canon, path, sizeof(canon) - 1);
        canon[sizeof(canon) - 1] = '\0';
        path_canonicalize(canon);
        if (strcmp(canon, "/") == 0) {
            return -EEXIST;
        }
    }

    vfs_node_t *parent = NULL;
    char name[256];
    int err = vfs_resolve_parent_and_name(path, &parent, name, sizeof(name));
    if (err != 0) return err;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -EEXIST;

    vfs_node_t *existing = (parent->ops && parent->ops->lookup) ? parent->ops->lookup(parent, name) : NULL;
    if (existing) {
        if (existing->priv) kfree(existing->priv);
        kfree(existing);
        return -EEXIST;
    }

    if (!parent->ops || !parent->ops->mkdir) return -ENOSYS;
    return parent->ops->mkdir(parent, name, mode);
}

int vfs_unlink(const char *path) {
    vfs_node_t *parent = NULL;
    char name[256];
    int err = vfs_resolve_parent_and_name(path, &parent, name, sizeof(name));
    if (err != 0) return err;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -EISDIR;
    if (!parent->ops || !parent->ops->unlink) return -ENOSYS;

    return parent->ops->unlink(parent, name);
}

int vfs_rmdir(const char *path) {
    vfs_node_t *parent = NULL;
    char name[256];
    int err = vfs_resolve_parent_and_name(path, &parent, name, sizeof(name));
    if (err != 0) return err;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -EINVAL;
    if (!parent->ops || !parent->ops->rmdir) return -ENOSYS;

    return parent->ops->rmdir(parent, name);
}

int vfs_rename(const char *oldpath, const char *newpath) {
    vfs_node_t *old_parent = NULL;
    vfs_node_t *new_parent = NULL;
    char old_name[256];
    char new_name[256];

    int err = vfs_resolve_parent_and_name(oldpath, &old_parent, old_name, sizeof(old_name));
    if (err != 0) return err;

    err = vfs_resolve_parent_and_name(newpath, &new_parent, new_name, sizeof(new_name));
    if (err != 0) return err;

    if (strcmp(old_name, ".") == 0 || strcmp(old_name, "..") == 0 ||
        strcmp(new_name, ".") == 0 || strcmp(new_name, "..") == 0) {
        return -EINVAL;
    }

    if (!old_parent->ops || !old_parent->ops->rename) return -ENOSYS;
    return old_parent->ops->rename(old_parent, old_name, new_parent, new_name);
}

static const vfs_ops_t s_bdev_node_ops = {
    .open    = NULL,
    .close   = NULL,
    .read    = bdev_node_read,
    .write   = bdev_node_write,
    .lookup  = NULL,
    .readdir = NULL,
    .create  = NULL,
    .mkdir   = NULL,
};