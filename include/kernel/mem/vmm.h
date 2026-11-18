#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096ULL

#define PTE_PRESENT       (1ULL << 0)
#define PTE_WRITABLE      (1ULL << 1)
#define PTE_USER          (1ULL << 2)
#define PTE_WRITETHROUGH  (1ULL << 3)
#define PTE_NOCACHE       (1ULL << 4)
#define PTE_ACCESSED      (1ULL << 5)
#define PTE_DIRTY         (1ULL << 6)
#define PTE_HUGE          (1ULL << 7)
#define PTE_GLOBAL        (1ULL << 8)
#define PTE_NX            (1ULL << 63)

#define HHDM_VIRTUAL_BASE 0xFFFF800000000000ULL

#define PAGE_SIZE_2MIB        (2ULL * 1024 * 1024)

#define PTE_FRAME_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PTE_FLAGS_MASK      0xFFF0000000000FFFULL

typedef uint64_t pt_entry_t;

typedef struct page_table {
    pt_entry_t entries[512];
} __attribute__((packed)) page_table_t;

typedef struct vmm_pagemap {
    page_table_t* pml4;
    uintptr_t pml4_phys;
} vmm_pagemap_t;

void vmm_init(void);
vmm_pagemap_t *vmm_new_pagemap(void);
void vmm_destroy_pagemap(vmm_pagemap_t *map);
void vmm_switch_pagemap(vmm_pagemap_t *map);

int  vmm_map_page(vmm_pagemap_t *map, uintptr_t virt, uintptr_t phys, uint64_t flags);
void vmm_unmap_page(vmm_pagemap_t *map, uintptr_t virt);
int vmm_unmap_page_ex(vmm_pagemap_t *map, uintptr_t virt, bool free_phys_frame);
uintptr_t vmm_virt_to_phys(vmm_pagemap_t *map, uintptr_t virt);

pt_entry_t *vmm_get_pte(vmm_pagemap_t *pagemap, uintptr_t virt_addr);

static inline void vmm_invlpg(uintptr_t virt) {
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

static inline uintptr_t vmm_read_cr3() {
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void vmm_write_cr3(uintptr_t cr3) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

extern vmm_pagemap_t kernel_pagemap;

uintptr_t hhdm_physical_to_virtual(uintptr_t phys);
uintptr_t hhdm_virtual_to_physical(uintptr_t phys);

vmm_pagemap_t *vmm_clone_pagemap(vmm_pagemap_t *src);

#define KERNEL_HEAP_BASE 0xFFFF900000000000ULL
#define KERNEL_HEAP_MAX  0x40000000ULL /* 1 GiB virtual address space */