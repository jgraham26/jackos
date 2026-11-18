#include "drivers/ata.h"
#include "io.h"      /* Provides inb, outb, inw, outw */
#include "fs/bdev.h"
#include <string.h>

#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SEC_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7
#define ATA_PRIMARY_DEV_CTRL     0x3F6

/* Status Register Bits */
#define ATA_SR_ERR  0x01    /* Error */
#define ATA_SR_DRQ  0x08    /* Data Request Ready */
#define ATA_SR_SRV  0x10    /* Overlapped Mode Service Request */
#define ATA_SR_DF   0x20    /* Drive Fault Error */
#define ATA_SR_RDY  0x40    /* Drive Ready */
#define ATA_SR_BSY  0x80    /* Busy */

/* ATA Commands */
#define ATA_CMD_READ_PIO          0x20
#define ATA_CMD_WRITE_PIO         0x30
#define ATA_CMD_CACHE_FLUSH       0xE7
#define ATA_CMD_IDENTIFY          0xEC

/* 400ns delay required after drive selection or reset */
static void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_DEV_CTRL);
    }
}

/* Poll until BSY clears. Returns 0 on success, -1 on timeout or drive fault. */
static int ata_wait_not_busy(void) {
    uint32_t timeout = 100000;
    while (--timeout) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

/* Poll until BSY clears and DRQ sets (or error occurs). */
static int ata_wait_ready_for_data(void) {
    uint32_t timeout = 100000;
    while (--timeout) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if (status & ATA_SR_DF) {
            return -1;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -1;
}

int ata_init(void) {
    /* Select master drive (0xA0) */
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0);
    ata_400ns_delay();

    /* Clear sector count and LBA registers */
    outb(ATA_PRIMARY_SEC_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);

    /* Send IDENTIFY command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        /* No device present on bus */
        return -1;
    }

    if (ata_wait_not_busy() < 0) {
        return -1;
    }

    /* Check for ATAPI or SATA signatures */
    uint8_t mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t hi  = inb(ATA_PRIMARY_LBA_HI);
    if (mid != 0 || hi != 0) {
        /* Non-ATA device (e.g. ATAPI optical drive) */
        return -1;
    }

    if (ata_wait_ready_for_data() < 0) {
        return -1;
    }

    /* Discard the 256 words (512 bytes) of IDENTIFY data buffer */
    for (int i = 0; i < 256; i++) {
        inw(ATA_PRIMARY_DATA);
    }

    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, void *buf) {
    if (!buf || sector_count == 0) return -1;
    if (lba > 0x0FFFFFFF) return -1; /* LBA28 limit */

    if (ata_wait_not_busy() < 0) return -1;

    /* Select drive + upper 4 bits of LBA (0xE0 = Master + LBA mode) */
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    outb(ATA_PRIMARY_SEC_COUNT, sector_count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *dest = (uint16_t *)buf;

    for (int s = 0; s < sector_count; s++) {
        if (ata_wait_ready_for_data() < 0) {
            return -1;
        }

        /* Read 256 16-bit words per sector */
        for (int i = 0; i < 256; i++) {
            *dest++ = inw(ATA_PRIMARY_DATA);
        }

        ata_400ns_delay();
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const void *buf) {
    if (!buf || sector_count == 0) return -1;
    if (lba > 0x0FFFFFFF) return -1;

    if (ata_wait_not_busy() < 0) return -1;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    outb(ATA_PRIMARY_SEC_COUNT, sector_count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *src = (const uint16_t *)buf;

    for (int s = 0; s < sector_count; s++) {
        if (ata_wait_ready_for_data() < 0) {
            return -1;
        }

        /* Write 256 16-bit words per sector */
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, *src++);
        }

        ata_400ns_delay();
    }

    /* Flush hardware write cache to ensure physical persistence */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ata_wait_not_busy();
}

static int ata_bdev_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    (void)dev;
    uint8_t *ptr = (uint8_t *)buf;

    while (count > 0) {
        /* ATA PIO sector count register takes 1 byte (max 255 sectors per chunk) */
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;
        if (ata_read_sectors((uint32_t)lba, chunk, ptr) != 0) {
            return -1;
        }
        lba += chunk;
        count -= chunk;
        ptr += chunk * ATA_SECTOR_SIZE;
    }
    return 0;
}

static int ata_bdev_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    (void)dev;
    const uint8_t *ptr = (const uint8_t *)buf;

    while (count > 0) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;
        if (ata_write_sectors((uint32_t)lba, chunk, ptr) != 0) {
            return -1;
        }
        lba += chunk;
        count -= chunk;
        ptr += chunk * ATA_SECTOR_SIZE;
    }
    return 0;
}

static const block_device_ops_t s_ata_ops = {
    .read_blocks  = ata_bdev_read,
    .write_blocks = ata_bdev_write,
    .flush        = NULL,
};

static block_device_t s_ata_primary_master;

void ata_bdev_register(void) {
    memset(&s_ata_primary_master, 0, sizeof(block_device_t));
    strncpy(s_ata_primary_master.name, "hda", BDEV_NAME_MAX - 1);
    s_ata_primary_master.block_size = ATA_SECTOR_SIZE;
    s_ata_primary_master.total_blocks = 0; /* Can be extracted from IDENTIFY if needed */
    s_ata_primary_master.ops = &s_ata_ops;

    bdev_register(&s_ata_primary_master);
}