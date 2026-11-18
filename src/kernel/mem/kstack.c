#include "mem/kstack.h"
#include <stdbool.h>
#include "mem/vmm.h"
#include "mem/pmm.h"
#include <string.h>
#include <stdio.h>

static uintptr_t s_next_slot_addr = KSTACK_REGION_BASE;

void kstack_init(void) {
    /*
     * Pre-populate PML4 entry for KSTACK_REGION_BASE in kernel_pagemap
     * BEFORE vmm_new_pagemap() copies entries[256..511] to user processes.
     * Map & immediately unmap one scratch page so the PDPT frame stays pinned in PML4[320].
     */
    vmm_pagemap_t *kmap = &kernel_pagemap;
    uintptr_t dummy_phys = pmm_alloc_frame();
    if (dummy_phys) {
        vmm_map_page(kmap, KSTACK_REGION_BASE, dummy_phys, PTE_WRITABLE);
        /* Clear only the leaf PTE without tearing down the upper PDPT/PD/PT tables */
        pt_entry_t *pte = vmm_get_pte(kmap, KSTACK_REGION_BASE);
        if (pte) *pte = 0;
        vmm_invlpg(KSTACK_REGION_BASE);
        pmm_free_frame(dummy_phys);
    }

    uint64_t pml4_idx = (KSTACK_REGION_BASE >> 39) & 0x1FF;
    serial_printf("[KSTACK] Pinned PML4[%llu] = %#llx\n",
                  pml4_idx, kmap->pml4->entries[pml4_idx]);
}

int kstack_alloc(kstack_t *out) {
    if (!out) return -1;

    vmm_pagemap_t *kmap = &kernel_pagemap;
    uintptr_t slot_base = s_next_slot_addr;
    s_next_slot_addr += KSTACK_SLOT_PAGES * PAGE_SIZE;

    uintptr_t guard_base = slot_base;
    uintptr_t data_base  = slot_base + (KSTACK_GUARD_PAGES * PAGE_SIZE);

    /* Leave guard_base explicitly unmapped */
    for (size_t i = 0; i < KSTACK_DATA_PAGES; i++) {
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            /* Roll back any pages allocated so far */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page_ex(kmap, data_base + j * PAGE_SIZE, true);
            }
            return -1;
        }
        memset((void *)hhdm_physical_to_virtual(phys), 0, PAGE_SIZE);
        if (vmm_map_page(kmap, data_base + i * PAGE_SIZE, phys, PTE_WRITABLE) != 0) {
            pmm_free_frame(phys);
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page_ex(kmap, data_base + j * PAGE_SIZE, true);
            }
            return -1;
        }
    }

    out->guard_base = guard_base;
    out->base       = data_base;
    out->top        = data_base + KSTACK_SIZE;
    return 0;
}

void kstack_free(kstack_t *stack) {
    if (!stack || !stack->base) return;

    vmm_pagemap_t *kmap = &kernel_pagemap;
    for (size_t i = 0; i < KSTACK_DATA_PAGES; i++) {
        uintptr_t vaddr = stack->base + i * PAGE_SIZE;
        pt_entry_t *pte = vmm_get_pte(kmap, vaddr);
        if (pte && (*pte & PTE_PRESENT)) {
            pmm_free_frame(*pte & PTE_FRAME_ADDR_MASK);
            *pte = 0;
            vmm_invlpg(vaddr);
        }
    }

    stack->guard_base = 0;
    stack->base       = 0;
    stack->top        = 0;
}