#include "fs/mbr.h"
#include "fs/devfs.h"
#include "mem/kheap.h"
#include <string.h>
#include "drivers/klog.h"
#include <stdio.h>

typedef struct {
    block_device_t *parent;
    uint32_t start_lba;
} partition_priv_t;

static int part_read_blocks(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    partition_priv_t *priv = (partition_priv_t *)dev->priv;
    return bdev_read(priv->parent, priv->start_lba + lba, count, buf);
}

static int part_write_blocks(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    partition_priv_t *priv = (partition_priv_t *)dev->priv;
    return bdev_write(priv->parent, priv->start_lba + lba, count, buf);
}

static const block_device_ops_t s_partition_ops = {
    .read_blocks  = part_read_blocks,
    .write_blocks = part_write_blocks,
};

int mbr_scan_and_register(block_device_t *parent_dev) {
    if (!parent_dev) return -1;

    mbr_sector_t mbr;
    if (bdev_read(parent_dev, 0, 1, &mbr) != 0) {
        return -1;
    }

    if (mbr.boot_signature != 0xAA55) {
        return -1; /* Not a valid MBR */
    }

    for (int i = 0; i < 4; i++) {
        mbr_partition_entry_t *entry = &mbr.partitions[i];
        if (entry->system_id == 0 || entry->total_sectors == 0) {
            continue;
        }

        block_device_t *part_bdev = (block_device_t *)kmalloc(sizeof(block_device_t));
        partition_priv_t *priv = (partition_priv_t *)kmalloc(sizeof(partition_priv_t));
        if (!part_bdev || !priv) return -1;

        priv->parent = parent_dev;
        priv->start_lba = entry->starting_lba;

        memset(part_bdev, 0, sizeof(block_device_t));
        snprintf(part_bdev->name, BDEV_NAME_MAX, "%s%d", parent_dev->name, i + 1);
        part_bdev->block_size   = parent_dev->block_size;
        part_bdev->total_blocks = entry->total_sectors;
        part_bdev->ops          = &s_partition_ops;
        part_bdev->priv         = priv;

        bdev_register(part_bdev);
        devfs_register_bdev(part_bdev);

        klog_printf("[MBR] Registered partition %s: LBA %u..%u (type 0x%02X)\n",
                      part_bdev->name, entry->starting_lba,
                      entry->starting_lba + entry->total_sectors - 1,
                      entry->system_id);
    }

    return 0;
}