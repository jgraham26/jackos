#include "mem/vmm.h"
#include "mem/pmm.h"
#include "mem/kheap.h"
#include "kernel.h"
#include "sched/process.h"
#include "fs/vfs.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "drivers/klog.h"


#define EARLY_PT_POOL_ENTRIES 128

/* Statically mapped table pool inside the kernel's .bss */
static page_table_t early_pt_pool[EARLY_PT_POOL_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static size_t early_pt_pool_used = 0;

vmm_pagemap_t kernel_pagemap;

/* External boundary symbols from linker.ld */
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];
extern uint8_t _kernel_heap_start[];
extern uint8_t _kernel_heap_end[];

static bool vmm_dynamic_tables_ready = false;

uintptr_t hhdm_physical_to_virtual(uintptr_t phys) {
    if (vmm_dynamic_tables_ready)
        return HHDM_VIRTUAL_BASE + phys;
    else
     return KERNEL_VIRTUAL_OFFSET + phys;
}

uintptr_t hhdm_virtual_to_physical(uintptr_t virt) {
    if (vmm_dynamic_tables_ready)
        return virt - HHDM_VIRTUAL_BASE;
    else return virt - KERNEL_VIRTUAL_OFFSET;
}

/* Canonical address index extraction */
static inline size_t pml4_index(uintptr_t virt) { return (virt >> 39) & 0x1FF; }
static inline size_t pdpt_index(uintptr_t virt) { return (virt >> 30) & 0x1FF; }
static inline size_t pd_index(uintptr_t virt)   { return (virt >> 21) & 0x1FF; }
static inline size_t pt_index(uintptr_t virt)   { return (virt >> 12) & 0x1FF; }

/* Allocates a page table from the statically mapped pool */
static page_table_t *alloc_table_frame(uintptr_t *phys_out) {
    if (!vmm_dynamic_tables_ready) {
        if (early_pt_pool_used >= EARLY_PT_POOL_ENTRIES) {
            klog_printf("[VMM] Early page table pool exhausted!\n");
            return NULL;
        }

        page_table_t* table = &early_pt_pool[early_pt_pool_used++];
        memset(table, 0, sizeof(page_table_t));
        
        uintptr_t virt = (uintptr_t)table;
        /* Convert virtual pool pointer back to physical address for CPU CR3 / table entries */
        uintptr_t phys = hhdm_virtual_to_physical(virt);

        if (phys_out) *phys_out = phys;
        return table;
    }

    uintptr_t frame = pmm_alloc_frame();
    if (!frame) return NULL;

    page_table_t* table = (page_table_t*)hhdm_physical_to_virtual(frame);
    memset(table, 0, sizeof(page_table_t));
    
    if (phys_out) *phys_out = frame;
    return table;
}

/* Traverses or creates a next-level table */
static page_table_t *get_or_create_next_level(pt_entry_t *entry, uint64_t flags) {
    if (*entry & PTE_PRESENT) {
        if (flags & PTE_USER) {
            *entry |= PTE_USER;
        }
        uintptr_t phys = *entry & PTE_FRAME_ADDR_MASK;
        return (page_table_t *)hhdm_physical_to_virtual(phys);
    }

    uintptr_t phys = 0;
    page_table_t *next_table = alloc_table_frame(&phys);
    if (!next_table) {
        return NULL;
    }

    *entry = phys | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    return next_table;
}

// helper to check if a table frame has any present entries
static inline bool is_table_empty(page_table_t* tbl) {
    for (size_t i = 0; i < 512; i++) {
        if (tbl->entries[i] & PTE_PRESENT) {
            return false;
        }
    }

    return true;
}

// helper to safely free a page table if dynamic memory is ready
static inline void free_table_frame(uintptr_t phys) {
    if (vmm_dynamic_tables_ready) {
        pmm_free_frame(phys);
    }
}


int vmm_map_page(vmm_pagemap_t *map, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    if (!map || !map->pml4) return -1;
    
    size_t pml4_i = pml4_index(virt);
    size_t pdpt_i = pdpt_index(virt);
    size_t pd_i   = pd_index(virt);
    size_t pt_i   = pt_index(virt);
    
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

    page_table_t *pdpt = get_or_create_next_level(&map->pml4->entries[pml4_i], flags);
    if (!pdpt) return -1;

    page_table_t *pd = get_or_create_next_level(&pdpt->entries[pdpt_i], flags);
    if (!pd) return -1;

    page_table_t *pt = get_or_create_next_level(&pd->entries[pd_i], flags);
    if (!pt) return -1;

#pragma GCC diagnostic pop

    pt->entries[pt_i] = (phys & PTE_FRAME_ADDR_MASK) | flags | PTE_PRESENT;

    if (vmm_read_cr3() == map->pml4_phys) {
        vmm_invlpg(virt);
    }

    return 0;
}

int vmm_map_huge_page(vmm_pagemap_t *map, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    if (!map || !map->pml4) return -1;

    size_t pml4_i = pml4_index(virt);
    size_t pdpt_i = pdpt_index(virt);
    size_t pd_i   = pd_index(virt);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

    // Level 4 -> Level 3
    page_table_t *pdpt = get_or_create_next_level(&map->pml4->entries[pml4_i], flags);
    if (!pdpt) return -1;

    // Level 3 -> Level 2
    page_table_t *pd = get_or_create_next_level(&pdpt->entries[pdpt_i], flags);
    if (!pd) return -1;

#pragma GCC diagnostic pop

    // Set the 2 MiB page directly at Level 2 (Page Directory) with the Huge Page (bit 7) flag
    pd->entries[pd_i] = (phys & ~((uintptr_t)PAGE_SIZE_2MIB - 1)) | flags | PTE_PRESENT | PTE_HUGE;

    if (vmm_read_cr3() == map->pml4_phys) {
        vmm_invlpg(virt);
    }

    return 0;
}

int vmm_unmap_page_ex(vmm_pagemap_t *map, uintptr_t virt, bool free_phys_frame) {
    if (!map || !map->pml4) return -1;

    size_t pml4_i = pml4_index(virt);
    size_t pdpt_i = pdpt_index(virt);
    size_t pd_i   = pd_index(virt);
    size_t pt_i   = pt_index(virt);

    pt_entry_t pml4e = map->pml4->entries[pml4_i];
    if (!(pml4e & PTE_PRESENT)) return -1;

    uintptr_t pdpt_phys = pml4e & PTE_FRAME_ADDR_MASK;
    page_table_t *pdpt = (page_table_t *)hhdm_physical_to_virtual(pdpt_phys);
    pt_entry_t pdpte = pdpt->entries[pdpt_i];
    if (!(pdpte & PTE_PRESENT)) return -1;

    uintptr_t pd_phys = pdpte & PTE_FRAME_ADDR_MASK;
    page_table_t *pd = (page_table_t *)hhdm_physical_to_virtual(pd_phys);
    pt_entry_t pde = pd->entries[pd_i];
    if (!(pde & PTE_PRESENT)) return -1;

    /* Handle 2 MiB huge page */
    if (pde & PTE_HUGE) {
        uintptr_t phys = pde & ~((uintptr_t)PAGE_SIZE_2MIB - 1);
        pd->entries[pd_i] = 0;

        if (free_phys_frame && vmm_dynamic_tables_ready) {
            for (size_t i = 0; i < (PAGE_SIZE_2MIB / PAGE_SIZE); i++) {
                pmm_free_frame(phys + (i * PAGE_SIZE));
            }
        }

        if (vmm_read_cr3() == map->pml4_phys) {
            vmm_invlpg(virt);
        }

        /* Clean up empty directories upwards */
        if (is_table_empty(pd)) {
            pdpt->entries[pdpt_i] = 0;
            free_table_frame(pd_phys);

            if (is_table_empty(pdpt)) {
                map->pml4->entries[pml4_i] = 0;
                free_table_frame(pdpt_phys);
            }
        }
        return 0;
    }

    uintptr_t pt_phys = pde & PTE_FRAME_ADDR_MASK;
    page_table_t *pt = (page_table_t *)hhdm_physical_to_virtual(pt_phys);
    pt_entry_t pte = pt->entries[pt_i];
    if (!(pte & PTE_PRESENT)) return -1;

    uintptr_t phys = pte & PTE_FRAME_ADDR_MASK;
    pt->entries[pt_i] = 0;

    if (free_phys_frame && vmm_dynamic_tables_ready) {
        pmm_free_frame(phys);
    }

    if (vmm_read_cr3() == map->pml4_phys) {
        vmm_invlpg(virt);
    }

    /* Clean up empty PT, PD, and PDPT tables */
    if (is_table_empty(pt)) {
        pd->entries[pd_i] = 0;
        free_table_frame(pt_phys);

        if (is_table_empty(pd)) {
            pdpt->entries[pdpt_i] = 0;
            free_table_frame(pd_phys);

            if (is_table_empty(pdpt)) {
                map->pml4->entries[pml4_i] = 0;
                free_table_frame(pdpt_phys);
            }
        }
    }

    return 0;
}

void vmm_unmap_page(vmm_pagemap_t *map, uintptr_t virt) {
    vmm_unmap_page_ex(map, virt, false);
}

uintptr_t vmm_virt_to_phys(vmm_pagemap_t *map, uintptr_t virt) {
    if (!map || !map->pml4) return 0;

    pt_entry_t pml4e = map->pml4->entries[pml4_index(virt)];
    if (!(pml4e & PTE_PRESENT)) return 0;

    page_table_t *pdpt = (page_table_t *)hhdm_physical_to_virtual(pml4e & PTE_FRAME_ADDR_MASK);
    pt_entry_t pdpte = pdpt->entries[pdpt_index(virt)];
    if (!(pdpte & PTE_PRESENT)) return 0;
    if (pdpte & (1ULL << 7)) {
        // 1GiB page
        return (pdpte & ~((1ULL << 30) - 1)) | (virt & ((1ULL << 30) - 1));
    }

    page_table_t *pd = (page_table_t *)hhdm_physical_to_virtual(pdpte & PTE_FRAME_ADDR_MASK);
    pt_entry_t pde = pd->entries[pd_index(virt)];
    if (!(pde & PTE_PRESENT)) return 0;
    if (pde & (1ULL << 7)) {
        // 2 MiB page
        return (pde & ~((1ULL << 21) - 1)) | (virt & ((1ULL << 21) - 1));
    }

    page_table_t *pt = (page_table_t *)hhdm_physical_to_virtual(pde & PTE_FRAME_ADDR_MASK);
    pt_entry_t pte = pt->entries[pt_index(virt)];
    if (!(pte & PTE_PRESENT)) return 0;

    return (pte & PTE_FRAME_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

void vmm_destroy_pagemap(vmm_pagemap_t *map) {
    if (!map || !map->pml4) return;

    for (size_t pml4_i = 0; pml4_i < 256; pml4_i++) {
        pt_entry_t pml4e = map->pml4->entries[pml4_i];
        if (!(pml4e & PTE_PRESENT)) continue;

        page_table_t *pdpt = (page_table_t *)hhdm_physical_to_virtual(pml4e & PTE_FRAME_ADDR_MASK);

        for (size_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            pt_entry_t pdpte = pdpt->entries[pdpt_i];
            if (!(pdpte & PTE_PRESENT) || (pdpte & PTE_HUGE)) continue;

            page_table_t *pd = (page_table_t *)hhdm_physical_to_virtual(pdpte & PTE_FRAME_ADDR_MASK);

            for (size_t pd_i = 0; pd_i < 512; pd_i++) {
                pt_entry_t pde = pd->entries[pd_i];
                if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) continue;

                page_table_t *pt = (page_table_t *)hhdm_physical_to_virtual(pde & PTE_FRAME_ADDR_MASK);

                /* Free each mapped physical frame */
                for (size_t pt_i = 0; pt_i < 512; pt_i++) {
                    pt_entry_t pte = pt->entries[pt_i];
                    if (pte & PTE_PRESENT) {
                        free_table_frame(pte & PTE_FRAME_ADDR_MASK);
                    }
                }

                /* Free leaf Page Table */
                free_table_frame(pde & PTE_FRAME_ADDR_MASK);
            }
            /* Free Page Directory */
            free_table_frame(pdpte & PTE_FRAME_ADDR_MASK);
        }
        /* Free Page Directory Pointer Table */
        free_table_frame(pml4e & PTE_FRAME_ADDR_MASK);
    }

    /* Free the root PML4 frame */
    free_table_frame(map->pml4_phys);
    map->pml4 = NULL;
    map->pml4_phys = 0;
}

void vmm_switch_pagemap(vmm_pagemap_t *map) {
    if (!map) return;
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    if (rsp < 0xffff800000000000ULL) {
        klog_printf("[PANIC] vmm_switch_pagemap called with user RSP=%#llx from %p!\n",
                      rsp, __builtin_return_address(0));
        hcf();
    }
    vmm_write_cr3(map->pml4_phys);
}

vmm_pagemap_t *vmm_get_kernel_pagemap(void) {
    return &kernel_pagemap;
}

void vmm_init(void) {
    /* 1. Allocate root PML4 from the statically mapped pool */
    kernel_pagemap.pml4 = alloc_table_frame(&kernel_pagemap.pml4_phys);
    if (!kernel_pagemap.pml4) {
        hcf();
    }

    /* 2. Higher-half map the kernel binary + early static pool */
    uintptr_t kphys_start = (uintptr_t)_kernel_phys_start & ~(PAGE_SIZE - 1);
    uintptr_t kphys_end   = ((uintptr_t)_kernel_phys_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t phys = kphys_start; phys < kphys_end; phys += PAGE_SIZE) {
        uintptr_t virt = phys + KERNEL_VIRTUAL_OFFSET;
        vmm_map_page(&kernel_pagemap, virt, phys, PTE_WRITABLE);
    }


    /* 4. Map first 1 MiB for BIOS / VGA memory access */
    for (uintptr_t addr = 0x0; addr < 0x100000; addr += PAGE_SIZE) {
        vmm_map_page(&kernel_pagemap, addr + KERNEL_VIRTUAL_OFFSET, addr, PTE_WRITABLE);
    }

    // construct HHDM
    uint64_t max_phys = pmm_get_highest_addr();
    max_phys = (max_phys + PAGE_SIZE - 1) & ~(PAGE_SIZE_2MIB - 1);

    for (uintptr_t phys = 0; phys < max_phys; phys += PAGE_SIZE_2MIB) {
        uintptr_t virt = phys + HHDM_VIRTUAL_BASE;
        vmm_map_huge_page(&kernel_pagemap, virt, phys, PTE_WRITABLE);
    }

    
    /* 5. Switch to the new architectural kernel page tables */
    vmm_switch_pagemap(&kernel_pagemap);

    vmm_dynamic_tables_ready = true;
    pmm_remap_bitmap_to_hhdm();

    /* 6. Initialize heap data structures */
    kheap_init(KERNEL_HEAP_BASE, 64 * 1024, KERNEL_HEAP_MAX);
}

vmm_pagemap_t* vmm_new_pagemap() {
    /* 1. Allocate a page map descriptor from the heap */
    vmm_pagemap_t* map = (vmm_pagemap_t*)kmalloc(sizeof(vmm_pagemap_t));
    if (!map) return NULL;

    /* 2. Allocate physical frame for the new process PML4 */
    uintptr_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) {
        kfree(map);
        return NULL;
    }

    page_table_t* pml4_virt = (page_table_t*)hhdm_physical_to_virtual(pml4_phys);
    memset(pml4_virt, 0, sizeof(page_table_t));

    for (size_t i = 256; i < 512; i++) {
        pml4_virt->entries[i] = kernel_pagemap.pml4->entries[i];
    }

    map->pml4 = pml4_virt;
    map->pml4_phys = pml4_phys;

    return map;
}

pt_entry_t *vmm_get_pte(vmm_pagemap_t *pagemap, uintptr_t virt_addr) {
    if (!pagemap || !pagemap->pml4) {
        return NULL;
    }

    /* Extract 9-bit indices for 4-level paging */
    size_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    size_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    size_t pd_idx   = (virt_addr >> 21) & 0x1FF;
    size_t pt_idx   = (virt_addr >> 12) & 0x1FF;

    /* Level 4: PML4 -> PDPT */
    pt_entry_t pml4e = pagemap->pml4->entries[pml4_idx];
    if (!(pml4e & PTE_PRESENT)) {
        return NULL;
    }
    page_table_t *pdpt = (page_table_t *)hhdm_physical_to_virtual(pml4e & PTE_FRAME_ADDR_MASK);

    /* Level 3: PDPT -> PD (Reject 1 GiB huge pages) */
    pt_entry_t pdpte = pdpt->entries[pdpt_idx];
    if (!(pdpte & PTE_PRESENT) || (pdpte & PTE_HUGE)) {
        return NULL;
    }
    page_table_t *pd = (page_table_t *)hhdm_physical_to_virtual(pdpte & PTE_FRAME_ADDR_MASK);

    /* Level 2: PD -> PT (Reject 2 MiB huge pages) */
    pt_entry_t pde = pd->entries[pd_idx];
    if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) {
        return NULL;
    }
    page_table_t *pt = (page_table_t *)hhdm_physical_to_virtual(pde & PTE_FRAME_ADDR_MASK);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Waddress-of-packed-member"

    /* Level 1: Return direct pointer to the leaf PTE */
    return &pt->entries[pt_idx];

    #pragma GCC diagnostic pop
}

vmm_pagemap_t *vmm_clone_pagemap(vmm_pagemap_t *src) {
    if (!src || !src->pml4) return NULL;

    /* 1. Allocate a clean destination pagemap with shared kernel higher-half (entries 256-511) */
    vmm_pagemap_t *dst = vmm_new_pagemap();
    if (!dst) return NULL;

    /* 2. Traverse only user space: PML4 indices 0 through 255 */
    for (size_t pml4_i = 0; pml4_i < 256; pml4_i++) {
        pt_entry_t pml4e = src->pml4->entries[pml4_i];
        if (!(pml4e & PTE_PRESENT)) continue;

        uintptr_t pdpt_phys = pml4e & PTE_FRAME_ADDR_MASK;
        page_table_t *src_pdpt = (page_table_t *)hhdm_physical_to_virtual(pdpt_phys);

        for (size_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            pt_entry_t pdpte = src_pdpt->entries[pdpt_i];
            if (!(pdpte & PTE_PRESENT)) continue;

            /* If 1 GiB huge page is present in user space, skip or handle separately */
            if (pdpte & PTE_HUGE) continue;

            uintptr_t pd_phys = pdpte & PTE_FRAME_ADDR_MASK;
            page_table_t *src_pd = (page_table_t *)hhdm_physical_to_virtual(pd_phys);

            for (size_t pd_i = 0; pd_i < 512; pd_i++) {
                pt_entry_t pde = src_pd->entries[pd_i];
                if (!(pde & PTE_PRESENT)) continue;

                /* Handle 2 MiB huge page */
                if (pde & PTE_HUGE) {
                    uintptr_t virt = ((uint64_t)pml4_i << 39) |
                                    ((uint64_t)pdpt_i << 30) |
                                    ((uint64_t)pd_i   << 21);

                    uintptr_t src_phys = pde & ~((uintptr_t)PAGE_SIZE_2MIB - 1);
                    uint64_t flags = pde & 0xFFF;

                    /* Allocate 512 contiguous 4KiB frames (or huge frame allocator if present) */
                    uintptr_t dst_phys = pmm_alloc_frame();
                    if (!dst_phys) {
                        vmm_destroy_pagemap(dst);
                        kfree(dst);
                        return NULL;
                    }

                    /* Copy 2 MiB */
                    memcpy((void *)hhdm_physical_to_virtual(dst_phys),
                           (void *)hhdm_physical_to_virtual(src_phys),
                           PAGE_SIZE_2MIB);

                    vmm_map_huge_page(dst, virt, dst_phys, flags);
                    continue;
                }

                uintptr_t pt_phys = pde & PTE_FRAME_ADDR_MASK;
                page_table_t *src_pt = (page_table_t *)hhdm_physical_to_virtual(pt_phys);

                for (size_t pt_i = 0; pt_i < 512; pt_i++) {
                    pt_entry_t pte = src_pt->entries[pt_i];
                    if (!(pte & PTE_PRESENT)) continue;

                    uint64_t virt = ((uint64_t)pml4_i << 39) |
                                    ((uint64_t)pdpt_i << 30) |
                                    ((uint64_t)pd_i   << 21) |
                                    ((uint64_t)pt_i   << 12);
                    if (virt & (1ULL << 47)) {
                        virt |= 0xFFFF000000000000ULL;
                    }

                    uintptr_t src_frame = pte & PTE_FRAME_ADDR_MASK;
                    uint64_t flags = pte & PTE_FLAGS_MASK;

                    /* Device MMIO pages (e.g. /dev/fb0) are shared directly without CoW */
                    if (src_frame >= pmm_get_highest_addr()) {
                        if (vmm_map_page(dst, virt, src_frame, flags) != 0) {
                            vmm_destroy_pagemap(dst);
                            kfree(dst);
                            return NULL;
                        }
                        continue;
                    }

                    /* Mark writable private RAM pages Read-Only + CoW in BOTH parent and child */
                    if (flags & PTE_WRITABLE) {
                        flags = (flags & ~PTE_WRITABLE) | PTE_COW;
                        src_pt->entries[pt_i] = src_frame | flags;
                    }

                    pmm_ref_frame(src_frame);

                    if (vmm_map_page(dst, virt, src_frame, flags) != 0) {
                        pmm_free_frame(src_frame);
                        vmm_destroy_pagemap(dst);
                        kfree(dst);
                        return NULL;
                    }
                }
            }
        }
    }

    // TLB flush
    if (vmm_read_cr3() == src->pml4_phys) {
        vmm_write_cr3(src->pml4_phys);
    }

    return dst;
}

void vma_add_file(process_t *proc, uintptr_t start, uintptr_t end,
                  uint32_t flags, vfs_node_t *file, uint64_t file_offset) {
    if (!proc || start >= end) return;

    vma_remove(proc, start, end);

    vma_t *node = (vma_t *)kmalloc(sizeof(vma_t));
    if (!node) return;
    node->start       = start;
    node->end         = end;
    node->flags       = flags;
    node->file        = file;
    node->file_offset = file_offset;
    node->next        = NULL;

    if (file) {
        file->refcount++;
    }

    vma_t **curr = &proc->vma_list;
    while (*curr && (*curr)->start < start) {
        curr = &(*curr)->next;
    }
    node->next = *curr;
    *curr = node;

    /* Coalesce adjacent compatible VMAs (only anonymous VMAs for simplicity) */
    vma_t *it = proc->vma_list;
    while (it && it->next) {
        if (!it->file && !it->next->file &&
            it->end == it->next->start && it->flags == it->next->flags) {
            vma_t *merged = it->next;
            it->end = merged->end;
            it->next = merged->next;
            kfree(merged);
        } else {
            it = it->next;
        }
    }
}

void vma_add(process_t *proc, uintptr_t start, uintptr_t end, uint32_t flags) {
    vma_add_file(proc, start, end, flags, NULL, 0);
}

vma_t *vma_find(process_t *proc, uintptr_t addr) {
    if (!proc) return NULL;
    for (vma_t *curr = proc->vma_list; curr; curr = curr->next) {
        if (addr >= curr->start && addr < curr->end) {
            return curr;
        }
        if (curr->start > addr) break;
    }
    return NULL;
}

void vma_remove(process_t *proc, uintptr_t start, uintptr_t end) {
    if (!proc || start >= end) return;

    vma_t **curr = &proc->vma_list;
    while (*curr) {
        vma_t *v = *curr;
        if (v->end <= start || v->start >= end) {
            curr = &v->next;
            continue;
        }

        if (start <= v->start && end >= v->end) {
            /* Fully covered: delete node */
            *curr = v->next;
            if (v->file && v->file->refcount > 0) v->file->refcount--;
            kfree(v);
        } else if (start > v->start && end < v->end) {
            /* Hole punched in middle: split into two */
            vma_t *right = (vma_t *)kmalloc(sizeof(vma_t));
            if (right) {
                right->start = end;
                right->end   = v->end;
                right->flags = v->flags;
                right->file =  v->file;
                right->file_offset = v->file ? (v->file_offset + (end - v->start)) : 0;
                if (right->file) right->file->refcount++;
                right->next  = v->next;
                v->end       = start;
                v->next      = right;
            }
            curr = &v->next;
        } else if (start <= v->start) {
            /* Trim left side */
            v->start = end;
            curr = &v->next;
            if (v->file) {
                v->file_offset += (end - v->start);
            }
            v->start = end;
        } else {
            /* Trim right side */
            v->end = start;
            curr = &v->next;
        }
    }
}

vma_t *vma_clone_list(vma_t *src_head) {
    vma_t *dst_head = NULL;
    vma_t **tail = &dst_head;

    for (vma_t *s = src_head; s; s = s->next) {
        vma_t *copy = (vma_t *)kmalloc(sizeof(vma_t));
        if (!copy) {
            vma_free_list(dst_head);
            return NULL;
        }
        *copy = *s;
        copy->next = NULL;
        *tail = copy;
        tail = &copy->next;
        if (copy->file) copy->file->refcount++;
    }
    return dst_head;
}

void vma_free_list(vma_t *head) {
    while (head) {
        vma_t *next = head->next;
        if (head->file) head->file->refcount--;
        kfree(head);
        head = next;
    }
}