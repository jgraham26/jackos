#pragma once


#define VFS_MAX_PATH 256
#define VFS_MAX_NAME 64

#ifndef VFS_FILE
#define VFS_FILE        (1 << 0)  /* 0x01 */
#define VFS_DIRECTORY   (1 << 1)  /* 0x02 */
#define VFS_CHARDEVICE  (1 << 2)  /* 0x04 */
#define VFS_BLOCKDEVICE (1 << 3)  /* 0x08 */
#define VFS_SYMLINK     (1 << 4)  /* 0x10 */
#endif

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800

#include <stdint.h>
#include <stddef.h>
#include "fs/bdev.h"
#include "mem/vmm.h"

struct vfs_node;
struct vfs_file;

typedef struct vfs_dirent {
    char name[VFS_MAX_NAME];
    uint32_t inode;
    uint32_t type;
} vfs_dirent_t;

typedef struct vfs_ops {
    int      (*open)(struct vfs_node *node, uint32_t flags);
    void     (*close)(struct vfs_node *node);
    int64_t  (*read)(struct vfs_file *file, void *buf, size_t count);
    int64_t  (*write)(struct vfs_file *file, const void *buf, size_t count);
    struct vfs_node *(*lookup)(struct vfs_node *parent, const char *name);
    int      (*readdir)(struct vfs_node *dir, size_t index, vfs_dirent_t *out_entry);
    struct vfs_node *(*create)(struct vfs_node *parent, const char *name, uint32_t mode);
    int (*mkdir)(struct vfs_node *parent, const char *name, uint32_t mode);
    int64_t (*ioctl)(struct vfs_file *file, uint64_t request, void *arg);
    int64_t (*readlink)(struct vfs_node *node, char *buf, size_t size);
    int (*symlink)(struct vfs_node *parent, const char *name, const char *target);
    int (*unlink)(struct vfs_node *parent, const char *name);
    int (*rmdir)(struct vfs_node *parent, const char *name);
    int (*rename)(struct vfs_node *old_parent, const char *old_name, struct vfs_node *new_parent, const char *new_name);
    int (*truncate)(struct vfs_node *node, uint64_t length);
    int (*mmap)(struct vfs_node *node, vmm_pagemap_t *pagemap, uintptr_t vaddr, size_t len, uint64_t pte_flags, uint64_t offset);
} vfs_ops_t;

typedef struct vfs_node {
    char         name[VFS_MAX_NAME];
    uint32_t     flags;
    size_t       size;
    uint32_t     inode;
    vfs_ops_t   *ops;
    void        *device_data;

    struct vfs_node *parent;      /* Parent directory (NULL or self for root) */
    struct vfs_node *mount_point;
    uint32_t refcount;
    void *priv;         /* Holds block_device_t* when flags & VFS_BLOCKDEVICE */
} vfs_node_t;

typedef struct vfs_file {
    vfs_node_t *node;
    uint64_t    offset;
    uint32_t    flags;
    uint32_t    refcount;
} vfs_file_t;

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

void vfs_init(void);
int vfs_mount(const char *path, vfs_node_t *fs_root);
vfs_node_t *vfs_resolve_path(const char *path);
vfs_node_t *vfs_resolve_path_nofollow(const char *path);
vfs_file_t *vfs_open(const char *path, uint32_t flags);
int64_t vfs_read(vfs_file_t *file, void *buf, size_t count);
int64_t vfs_write(vfs_file_t *file, const void *buf, size_t count);
int vfs_symlink(const char *target, const char *linkpath);
void vfs_close(vfs_file_t *file);
void *vfs_read_entire_file(const char *path, size_t *out_size);
void path_canonicalize(char *path);
vfs_node_t *vfs_create(const char *path, uint32_t mode);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);

int vfs_register_bdev_node(block_device_t *bdev);
int vfs_mount_bdev(const char *target, const char *bdev_name, vfs_node_t *(*fs_mount)(block_device_t *bdev));