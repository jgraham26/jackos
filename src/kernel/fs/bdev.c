#include "fs/bdev.h"
#include <string.h>
#include "mem/kheap.h"
#include "fs/vfs.h"

#define ENOENT 2
#define EINVAL 22
#define EEXIST 17

static block_device_t *s_bdev_list = NULL;

void bdev_init() {
    s_bdev_list = NULL;
}

int bdev_register(block_device_t *dev) {
    if (!dev || !dev->ops || !dev->ops->read_blocks || dev->block_size == 0) {
        return -EINVAL;
    }

    /* Check for duplicate name */
    block_device_t *curr = s_bdev_list;
    while (curr) {
        if (strncmp(curr->name, dev->name, BDEV_NAME_MAX) == 0) {
            return -EEXIST;
        }
        curr = curr->next;
    }

    dev->next = s_bdev_list;
    s_bdev_list = dev;
    return 0;
}

int bdev_unregister(block_device_t *dev) {
    if (!dev) return -EINVAL;

    block_device_t **indirect = &s_bdev_list;
    while (*indirect && *indirect != dev) {
        indirect = &(*indirect)->next;
    }

    if (*indirect) {
        *indirect = dev->next;
        dev->next = NULL;
        return 0;
    }

    return -ENOENT;
}

block_device_t *bdev_get(const char *name) {
    if (!name) return NULL;

    block_device_t *curr = s_bdev_list;
    while (curr) {
        if (strncmp(curr->name, name, BDEV_NAME_MAX) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int bdev_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !buf) return -EINVAL;
    if (count == 0) return 0;
    if (dev->total_blocks > 0 && (lba + count > dev->total_blocks)) {
        return -EINVAL;
    }

    return dev->ops->read_blocks(dev, lba, count, buf);
}

int bdev_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (!dev || !buf) return -EINVAL;
    if (!dev->ops->write_blocks) return -EINVAL; /* Read-only device */
    if (count == 0) return 0;
    if (dev->total_blocks > 0 && (lba + count > dev->total_blocks)) {
        return -EINVAL;
    }

    return dev->ops->write_blocks(dev, lba, count, buf);
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

const vfs_ops_t s_bdev_node_ops = {
    .open    = NULL,
    .close   = NULL,
    .read    = bdev_node_read,
    .write   = bdev_node_write,
    .lookup  = NULL,
    .readdir = NULL,
};