#include "boot/bootinfo.h"
#include "boot/multiboot2.h"
#include "mem/pmm.h"
#include <string.h>
#include <stdio.h>

struct boot_info g_boot_info;

/* Ensure GRUB_PACKED is handled if not defined by your toolchain */
#ifndef GRUB_PACKED
#define GRUB_PACKED __attribute__((packed))
#endif

static void safe_strncpy(char *dst, const char *src, size_t max_len) {
    if (!dst || !src || max_len == 0) return;
    size_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void boot_info_parse(uintptr_t mb2_phys_addr) {
    memset(&g_boot_info, 0, sizeof(struct boot_info));

    /* Access the MB2 information structure mapped in virtual memory */
    uintptr_t mb2_virt = PHYSICAL_TO_VIRTUAL(mb2_phys_addr);

    /* In Multiboot 2, tags start 8 bytes past the total size and reserved word */
    struct multiboot_tag *tag = (struct multiboot_tag *)(mb2_virt + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        switch (tag->type) {
            case MULTIBOOT_TAG_TYPE_CMDLINE: {
                struct multiboot_tag_string *t = (struct multiboot_tag_string *)tag;
                safe_strncpy(g_boot_info.cmdline, t->string, sizeof(g_boot_info.cmdline));
                break;
            }

            case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME: {
                struct multiboot_tag_string *t = (struct multiboot_tag_string *)tag;
                safe_strncpy(g_boot_info.bootloader_name, t->string, sizeof(g_boot_info.bootloader_name));
                break;
            }

            case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO: {
                struct multiboot_tag_basic_meminfo *t = (struct multiboot_tag_basic_meminfo *)tag;
                g_boot_info.mem_lower_kib = t->mem_lower;
                g_boot_info.mem_upper_kib = t->mem_upper;
                break;
            }

            case MULTIBOOT_TAG_TYPE_MODULE: {
                if (g_boot_info.module_count < BOOT_MODULES_MAX) {
                    struct multiboot_tag_module *t = (struct multiboot_tag_module *)tag;
                    size_t idx = g_boot_info.module_count++;

                    g_boot_info.modules[idx].mod_start = (uintptr_t)t->mod_start;
                    g_boot_info.modules[idx].mod_end   = (uintptr_t)t->mod_end;
                    safe_strncpy(g_boot_info.modules[idx].cmdline, t->cmdline, sizeof(g_boot_info.modules[idx].cmdline));
                }
                break;
            }

            case MULTIBOOT_TAG_TYPE_FRAMEBUFFER: {
                struct multiboot_tag_framebuffer *t = (struct multiboot_tag_framebuffer *)tag;
                g_boot_info.fb.addr   = t->common.framebuffer_addr;
                g_boot_info.fb.pitch  = t->common.framebuffer_pitch;
                g_boot_info.fb.width  = t->common.framebuffer_width;
                g_boot_info.fb.height = t->common.framebuffer_height;
                g_boot_info.fb.bpp    = t->common.framebuffer_bpp;
                g_boot_info.fb.type   = t->common.framebuffer_type;

                if (t->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
                    g_boot_info.fb.red_field_position   = t->framebuffer_red_field_position;
                    g_boot_info.fb.red_mask_size        = t->framebuffer_red_mask_size;
                    g_boot_info.fb.green_field_position = t->framebuffer_green_field_position;
                    g_boot_info.fb.green_mask_size      = t->framebuffer_green_mask_size;
                    g_boot_info.fb.blue_field_position  = t->framebuffer_blue_field_position;
                    g_boot_info.fb.blue_mask_size       = t->framebuffer_blue_mask_size;
                }
                g_boot_info.fb.is_present = true;
                break;
            }

            case MULTIBOOT_TAG_TYPE_ACPI_OLD: {
                /* If modern ACPI 2.0+ was not already encountered, capture ACPI 1.0 */
                if (!g_boot_info.rsdp_addr) {
                    /* t->rsdp represents the beginning of the RSDP structure in bytes */
                    struct multiboot_tag_old_acpi *t = (struct multiboot_tag_old_acpi *)tag;
                    g_boot_info.rsdp_addr = (uintptr_t)t->rsdp;
                    g_boot_info.rsdp_is_v2 = false;
                }
                break;
            }

            case MULTIBOOT_TAG_TYPE_ACPI_NEW: {
                /* ACPI 2.0+ (XSDP) takes precedence */
                struct multiboot_tag_new_acpi *t = (struct multiboot_tag_new_acpi *)tag;
                g_boot_info.rsdp_addr = (uintptr_t)t->rsdp;
                g_boot_info.rsdp_is_v2 = true;
                break;
            }

            default:
                break;
        }

        /* Multiboot 2 tags are strictly padded to 8-byte boundaries */
        tag = (struct multiboot_tag *)(((uintptr_t)tag + tag->size + 7) & ~7ULL);
    }
}