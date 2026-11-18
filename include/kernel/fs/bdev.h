#pragma once

#include <stdint.h>
#include <stddef.h>

#define BDEV_NAME_MAX 32

struct block_device;

typedef struct block_device_ops {
    int (*read_blocks)(struct block_device *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write_blocks)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buf);
    int (*flush)(struct block_device *dev);
} block_device_ops_t;

typedef struct block_device {
    char name[BDEV_NAME_MAX];           /* e.g., "ata0", "ram0" */
    uint32_t block_size;                /* Typically 512 or 4096 */
    uint64_t total_blocks;              /* Capacity in blocks */
    const block_device_ops_t *ops;      /* Device operations */
    void *priv;                         /* Driver-specific private data */
    struct block_device *next;          /* Linked list node */
} block_device_t;

void bdev_init(void);
int bdev_register(block_device_t *dev);
int bdev_unregister(block_device_t *dev);
block_device_t *bdev_get(const char *name);

int bdev_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf);
int bdev_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf);