#include <stdint.h>
#include <string.h>
#include "fs/bdev.h"

typedef struct {
    uint8_t *base_addr;
    size_t size_bytes;
} ramdisk_priv_t;

static int ramdisk_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    ramdisk_priv_t *priv = (ramdisk_priv_t *)dev->priv;
    size_t offset = lba * dev->block_size;
    size_t bytes = count * dev->block_size;

    if (offset + bytes > priv->size_bytes) return -1;
    memcpy(buf, priv->base_addr + offset, bytes);
    return 0;
}

static int ramdisk_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    ramdisk_priv_t *priv = (ramdisk_priv_t *)dev->priv;
    size_t offset = lba * dev->block_size;
    size_t bytes = count * dev->block_size;

    if (offset + bytes > priv->size_bytes) return -1;
    memcpy(priv->base_addr + offset, buf, bytes);
    return 0;
}

static const block_device_ops_t s_ramdisk_ops = {
    .read_blocks  = ramdisk_read,
    .write_blocks = ramdisk_write,
};