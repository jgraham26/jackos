#include "boot/bootinfo.h"
#include "boot/multiboot2.h"
#include "kernel.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "stdio.h"

struct multiboot_mmap_entry_packed {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot_tag_mmap_packed {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry_packed entries[];
} __attribute__((packed));

static uint64_t *bitmap = NULL;
static uint64_t total_frames = 0;
static uint64_t used_frames = 0;
uint64_t bitmap_size_bytes = 0;
uintptr_t bm_phys_start = 0;
static uint64_t g_highest_phys_addr = 0;

/* Running cursor for amortized O(1) frame allocations */
static uint64_t last_frame_allocated = 0;

static inline void bitmap_set(uint64_t frame) {
    bitmap[frame / 64] |= (1ULL << (frame % 64));
}

static inline void bitmap_clear(uint64_t frame) {
    bitmap[frame / 64] &= ~(1ULL << (frame % 64));
}

static inline int bitmap_test(uint64_t frame) {
    return (bitmap[frame / 64] & (1ULL << (frame % 64))) != 0;
}

void pmm_reserve_region(uintptr_t base, size_t size) {
    if (size == 0) return;

    uint64_t start_frame = base / PAGE_SIZE;
    uint64_t end_frame = (base + size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t frame = start_frame; frame < end_frame && frame < total_frames; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            used_frames++;
        }
    }
}

extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];

void pmm_init(uintptr_t mb2_phys_addr) {
    uintptr_t mb2_virt = PHYSICAL_TO_VIRTUAL(mb2_phys_addr);
    uint32_t total_size = *(uint32_t *)mb2_virt;
    struct multiboot_tag *tag = (struct multiboot_tag *)(mb2_virt + 8);

    struct multiboot_tag_mmap_packed *mmap_tag = NULL;
    uint64_t highest_addr = 0;

    /* 1. Locate the memory map tag */
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap_tag = (struct multiboot_tag_mmap_packed *)tag;
            break;
        }
        tag = (struct multiboot_tag *)(((uintptr_t)tag + tag->size + 7) & ~7ULL);
    }

    if (!mmap_tag || mmap_tag->entry_size == 0) {
        hcf();
    }

    /* 2. Determine highest addressable physical frame */
    uintptr_t entries_end = (uintptr_t)mmap_tag + mmap_tag->size;
    const uint8_t *curr = (const uint8_t *)mmap_tag->entries;

    while ((uintptr_t)curr < entries_end) {
        const struct multiboot_mmap_entry_packed *entry = (const struct multiboot_mmap_entry_packed *)curr;
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t top = entry->addr + entry->len;
            if (top > highest_addr) {
                highest_addr = top;
            }
        }
        curr += mmap_tag->entry_size;
    }

    g_highest_phys_addr = highest_addr;
    total_frames = highest_addr / PAGE_SIZE;

    /* Align bitmap size to 64-bit boundaries */
    uint64_t total_words = (total_frames + 63) / 64;
    bitmap_size_bytes = total_words * sizeof(uint64_t);

    /* 3. Place bitmap directly after kernel AND MODULES in physical memory */
    uintptr_t bitmap_phys = (uintptr_t)_kernel_phys_end;

    /* Push boundary past any loaded modules */
    for (size_t i = 0; i < g_boot_info.module_count; i++) {
        if (g_boot_info.modules[i].mod_end > bitmap_phys) {
            bitmap_phys = g_boot_info.modules[i].mod_end;
        }
    }

    /* Page-align upward */
    bitmap_phys = (bitmap_phys + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Place bitmap here */
    bitmap = (uint64_t *)PHYSICAL_TO_VIRTUAL(bitmap_phys);
    bm_phys_start = bitmap_phys;

    /* 4. Mark all frames as reserved initially */
    memset(bitmap, 0xFF, bitmap_size_bytes);
    used_frames = total_frames;

    /* 5. Free available RAM sections */
    curr = (const uint8_t *)mmap_tag->entries;
    while ((uintptr_t)curr < entries_end) {
        const struct multiboot_mmap_entry_packed *entry = (const struct multiboot_mmap_entry_packed *)curr;

        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t start_addr = (entry->addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end_addr = (entry->addr + entry->len) & ~(PAGE_SIZE - 1);

            if (end_addr > start_addr) {
                uint64_t start_frame = start_addr / PAGE_SIZE;
                uint64_t end_frame = end_addr / PAGE_SIZE;

                for (uint64_t f = start_frame; f < end_frame && f < total_frames; f++) {
                    if (bitmap_test(f)) {
                        bitmap_clear(f);
                        used_frames--;
                    }
                }
            }
        }
        curr += mmap_tag->entry_size;
    }

    /* 6. Reserve critical hardware & kernel memory */
    pmm_reserve_region(0x0, 0x100000); /* Real Mode IVT, BDA, EBDA, VGA buffers */
    pmm_reserve_region((uintptr_t)_kernel_phys_start, 
                       (uintptr_t)_kernel_phys_end - (uintptr_t)_kernel_phys_start);
    pmm_reserve_region(mb2_phys_addr, total_size);
    pmm_reserve_region(bitmap_phys, bitmap_size_bytes);

    for (size_t i = 0; i < g_boot_info.module_count; i++) {
        uintptr_t start = g_boot_info.modules[i].mod_start & ~(PAGE_SIZE - 1);
        uintptr_t end   = (g_boot_info.modules[i].mod_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        pmm_reserve_region(start, end - start);
    }
    

    last_frame_allocated = 0;
}

void pmm_remap_bitmap_to_hhdm(void) {
    bitmap = (uint64_t *)hhdm_physical_to_virtual(bm_phys_start);
}

/* Fast O(1) single-frame allocation using 64-bit word scanning */
uintptr_t pmm_alloc_frame(void) {
    if (used_frames >= total_frames) return 0;

    uint64_t total_words = (total_frames + 63) / 64;
    uint64_t start_word = last_frame_allocated / 64;

    for (uint64_t i = 0; i < total_words; i++) {
        uint64_t word_idx = (start_word + i) % total_words;

        if (bitmap[word_idx] != 0xFFFFFFFFFFFFFFFFULL) {
            /* Find lowest clear bit */
            int bit = __builtin_ctzll(~bitmap[word_idx]);
            uint64_t frame = word_idx * 64 + bit;

            if (frame >= total_frames) {
                return 0;
            }

            bitmap_set(frame);
            used_frames++;
            last_frame_allocated = frame;
            return (uintptr_t)(frame * PAGE_SIZE);
        }
    }

    return 0;
}

/* Contiguous multi-frame allocation for hardware buffers and huge page alignments */
uintptr_t pmm_alloc_frames(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_frame();
    if (used_frames + count > total_frames) return 0;

    uint64_t consecutive = 0;
    uint64_t start_frame = 0;

    for (uint64_t frame = 0; frame < total_frames; frame++) {
        if (!bitmap_test(frame)) {
            if (consecutive == 0) {
                start_frame = frame;
            }
            consecutive++;

            if (consecutive == count) {
                for (uint64_t f = start_frame; f < start_frame + count; f++) {
                    bitmap_set(f);
                }
                used_frames += count;
                last_frame_allocated = start_frame + count;
                return (uintptr_t)(start_frame * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }

    return 0;
}

uint64_t pmm_get_highest_addr(void) {
    return g_highest_phys_addr;
}

void pmm_free_frame(uintptr_t phys_addr) {
    uint64_t frame = phys_addr / PAGE_SIZE;
    if (frame >= total_frames) return;

    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        used_frames--;
        if (frame < last_frame_allocated) {
            last_frame_allocated = frame;
        }
    }
}

void pmm_free_frames(uintptr_t phys_addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_frame(phys_addr + (i * PAGE_SIZE));
    }
}

void pmm_dump(void) {
    if (!bitmap || total_frames == 0) {
        serial_printf("[PMM] Not initialized.\n");
        return;
    }

    uint64_t free_frames = total_frames - used_frames;
    uint64_t total_kib = (total_frames * PAGE_SIZE) / 1024;
    uint64_t used_kib = (used_frames * PAGE_SIZE) / 1024;
    uint64_t free_kib = (free_frames * PAGE_SIZE) / 1024;

    serial_printf("==================== PMM STATUS DUMP ====================\n");
    serial_printf("Total Memory : %llu MiB (%llu KiB, %llu frames)\n",
                  total_kib / 1024, total_kib, total_frames);
    serial_printf("Used Memory  : %llu MiB (%llu KiB, %llu frames)\n",
                  used_kib / 1024, used_kib, used_frames);
    serial_printf("Free Memory  : %llu MiB (%llu KiB, %llu frames)\n",
                  free_kib / 1024, free_kib, free_frames);
    serial_printf("Bitmap Base  : %p (Size: %llu bytes)\n",
                  (void *)bitmap, bitmap_size_bytes);
    serial_printf("----------------- Physical Memory Layout ----------------\n");

    int current_state = bitmap_test(0);
    uint64_t region_start_frame = 0;

    for (uint64_t frame = 1; frame < total_frames; frame++) {
        int state = bitmap_test(frame);

        if (state != current_state) {
            uint64_t count = frame - region_start_frame;
            uintptr_t start_addr = region_start_frame * PAGE_SIZE;
            uintptr_t end_addr = (frame * PAGE_SIZE) - 1;

            serial_printf("  [0x%016llx - 0x%016llx] %8llu KiB  %s\n",
                          (unsigned long long)start_addr,
                          (unsigned long long)end_addr,
                          (unsigned long long)(count * 4),
                          current_state ? "RESERVED" : "FREE");

            region_start_frame = frame;
            current_state = state;
        }
    }

    uint64_t count = total_frames - region_start_frame;
    uintptr_t start_addr = region_start_frame * PAGE_SIZE;
    uintptr_t end_addr = (total_frames * PAGE_SIZE) - 1;

    serial_printf("  [0x%016llx - 0x%016llx] %8llu KiB  %s\n",
                  (unsigned long long)start_addr,
                  (unsigned long long)end_addr,
                  (unsigned long long)(count * 4),
                  current_state ? "RESERVED" : "FREE");

    serial_printf("=========================================================\n");
}

uint64_t pmm_get_total_frames() {
    return total_frames;
}

uint64_t pmm_get_free_frames() {
    return total_frames - used_frames;
}