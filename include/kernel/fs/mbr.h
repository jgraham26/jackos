#pragma once

#include <stdint.h>
#include "fs/bdev.h"

typedef struct __attribute__((packed)) {
    uint8_t  boot_indicator;   /* 0x80 = bootable, 0x00 = not bootable */
    uint8_t  starting_chs[3];
    uint8_t  system_id;        /* Partition type: 0x83 for Linux/ext2, 0x0B/0x0C for FAT32 */
    uint8_t  ending_chs[3];
    uint32_t starting_lba;     /* First LBA of the partition */
    uint32_t total_sectors;    /* Total sector count */
} mbr_partition_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t               bootstrap[446];
    mbr_partition_entry_t partitions[4];
    uint16_t              boot_signature; /* 0xAA55 */
} mbr_sector_t;

int mbr_scan_and_register(block_device_t *parent_dev);