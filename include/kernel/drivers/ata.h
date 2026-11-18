#pragma once

#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512


int ata_init();
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);
void ata_bdev_register(void);