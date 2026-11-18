#include "fs/fbdev.h"
#include "fs/vfs.h"
#include "boot/bootinfo.h"
#include "mem/kheap.h"
#include "mem/vmm.h"
#include "fs/devfs.h"
#include "mem/uaccess.h"
#include <string.h>
#include <errno.h>

static uintptr_t s_fb_phys   = 0;
static uint8_t  *s_fb_virt   = NULL;
static uint32_t  s_fb_width  = 0;
static uint32_t  s_fb_height = 0;
static uint32_t  s_fb_pitch  = 0;
static uint32_t  s_fb_bpp    = 0;
static size_t    s_fb_size   = 0;

static int64_t fbdev_read(vfs_file_t *file, void *buf, size_t count) {
    if (!file || !buf || !s_fb_virt) return -EINVAL;
    if (file->offset >= s_fb_size) return 0;

    size_t avail = s_fb_size - (size_t)file->offset;
    size_t to_copy = (count < avail) ? count : avail;
    memcpy(buf, s_fb_virt + file->offset, to_copy);
    file->offset += to_copy;
    return (int64_t)to_copy;
}

static int64_t fbdev_write(vfs_file_t *file, const void *buf, size_t count) {
    if (!file || !buf || !s_fb_virt) return -EINVAL;
    if (file->offset >= s_fb_size) return 0;

    size_t avail = s_fb_size - (size_t)file->offset;
    size_t to_copy = (count < avail) ? count : avail;
    memcpy(s_fb_virt + file->offset, buf, to_copy);
    file->offset += to_copy;
    return (int64_t)to_copy;
}

static int64_t fbdev_ioctl(vfs_file_t *file, uint64_t request, void* arg) {
    (void)file;
    if (!arg) return -EFAULT;

    if (request == FBIOGET_FSCREENINFO) {
        if (!validate_user_buffer(arg, sizeof(struct fb_fix_screeninfo))) {
            return -EFAULT;
        }
        struct fb_fix_screeninfo finfo;
        memset(&finfo, 0, sizeof(finfo));
        strcpy(finfo.id, "JackOS FB");
        finfo.smem_start  = (uint64_t)s_fb_phys;
        finfo.smem_len    = (uint32_t)s_fb_size;
        finfo.type        = FB_TYPE_PACKED_PIXELS;
        finfo.visual      = FB_VISUAL_TRUECOLOR;
        finfo.line_length = s_fb_pitch;

        memcpy(arg, &finfo, sizeof(finfo));
        return 0;
    }

    if (request == FBIOGET_VSCREENINFO) {
        if (!validate_user_buffer((void *)arg, sizeof(struct fb_var_screeninfo))) {
            return -EFAULT;
        }
        struct fb_var_screeninfo vinfo;
        memset(&vinfo, 0, sizeof(vinfo));
        vinfo.xres           = s_fb_width;
        vinfo.yres           = s_fb_height;
        vinfo.xres_virtual   = s_fb_width;
        vinfo.yres_virtual   = s_fb_height;
        vinfo.bits_per_pixel = s_fb_bpp;

        /* Standard 32-bit BGRX layout (use g_boot_info RGB shifts if available) */
        vinfo.red    = (struct fb_bitfield){ .offset = 16, .length = 8, .msb_right = 0 };
        vinfo.green  = (struct fb_bitfield){ .offset = 8,  .length = 8, .msb_right = 0 };
        vinfo.blue   = (struct fb_bitfield){ .offset = 0,  .length = 8, .msb_right = 0 };
        vinfo.transp = (struct fb_bitfield){ .offset = 24, .length = 8, .msb_right = 0 };

        memcpy((void *)arg, &vinfo, sizeof(vinfo));
        return 0;
    }

    if (request == FBIOPUT_VSCREENINFO || request == FBIOPAN_DISPLAY) {
        /* Accept mode-set requests with current geometry */
        return 0;
    }

    return -EINVAL;
}

static int fbdev_mmap(vfs_node_t *node, vmm_pagemap_t *pagemap, uintptr_t vaddr,
                      size_t len, uint64_t pte_flags, uint64_t offset) {
    (void)node;
    if (offset >= s_fb_size || offset + len > ((s_fb_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))) {
        return -EINVAL;
    }
    size_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys = s_fb_phys + offset + (i * PAGE_SIZE);
        if (vmm_map_page(pagemap, vaddr + (i * PAGE_SIZE), phys, pte_flags | PTE_USER) != 0) {
            return -ENOMEM;
        }
    }
    return 0;
}

static const vfs_ops_t s_fbdev_ops = {
    .read  = fbdev_read,
    .write = fbdev_write,
    .ioctl = fbdev_ioctl,
    .mmap  = fbdev_mmap,
};

void fbdev_init(void) {
    if (!g_boot_info.fb.addr) return;

    s_fb_phys   = (uintptr_t)g_boot_info.fb.addr;
    s_fb_width  = g_boot_info.fb.width;
    s_fb_height = g_boot_info.fb.height;
    s_fb_pitch  = g_boot_info.fb.pitch;
    s_fb_bpp    = g_boot_info.fb.bpp;
    s_fb_size   = (size_t)s_fb_pitch * (size_t)s_fb_height;
    s_fb_virt   = (uint8_t *)hhdm_physical_to_virtual(s_fb_phys);

    vfs_node_t *fb0 = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!fb0) return;
    memset(fb0, 0, sizeof(vfs_node_t));

    devfs_register_device("fb0", &s_fbdev_ops, NULL); /* Or add to your devfs device list */
}