#pragma once

#include "stdint.h"

#define PAGE_SIZE 4096ULL
#define KERNEL_VIRTUAL_OFFSET 0xFFFFFFFF80000000ULL
#define PHYSICAL_TO_VIRTUAL(addr) ((uintptr_t)(addr) + KERNEL_VIRTUAL_OFFSET)
#define VIRTUAL_TO_PHYSICAL(addr) ((uintptr_t)(addr) - KERNEL_VIRTUAL_OFFSET)

extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];

void pmm_init(uintptr_t mb2_phys_addr);
uintptr_t pmm_alloc_frame();
void pmm_dump();
uint64_t pmm_get_highest_addr(void);
void pmm_remap_bitmap_to_hhdm();
void pmm_free_frame(uintptr_t phys_addr);
uint64_t pmm_get_total_frames(void);
uint64_t pmm_get_free_frames(void);
void pmm_ref_frame(uintptr_t phys_addr);
uint16_t pmm_get_refcount(uintptr_t phys_addr);