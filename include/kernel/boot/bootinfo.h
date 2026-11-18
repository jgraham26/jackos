#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BOOT_CMDLINE_MAX_LEN 256
#define BOOT_MODULES_MAX     16

struct boot_module {
    uintptr_t mod_start;   /* Physical start address */
    uintptr_t mod_end;     /* Physical end address */
    char cmdline[64];
};

struct boot_framebuffer {
    uint64_t addr;         /* Physical framebuffer address */
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  type;
    uint8_t  red_field_position;
    uint8_t  red_mask_size;
    uint8_t  green_field_position;
    uint8_t  green_mask_size;
    uint8_t  blue_field_position;
    uint8_t  blue_mask_size;
    bool     is_present;
};

struct boot_info {
    char cmdline[BOOT_CMDLINE_MAX_LEN];
    char bootloader_name[64];

    /* Physical address of the ACPI RSDP/XSDP table */
    uintptr_t rsdp_addr;
    bool      rsdp_is_v2;

    /* Framebuffer information */
    struct boot_framebuffer fb;

    /* Boot Modules (e.g., initramfs) */
    struct boot_module modules[BOOT_MODULES_MAX];
    size_t module_count;

    /* BIOS/bootloader basic memory report (in KiB) */
    uint32_t mem_lower_kib;
    uint32_t mem_upper_kib;
};

extern struct boot_info g_boot_info;

void boot_info_parse(uintptr_t mb2_phys_addr);