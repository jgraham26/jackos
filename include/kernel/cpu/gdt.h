#pragma once

#include "stdint.h"

typedef struct {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_hi : 4;
    uint8_t flags : 4;
    uint8_t base_hi;
} __attribute__((packed)) gdt_t;

typedef struct {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed)) gdtr_t;

#define GDT_NUM_ENTRIES 7

#define KERNEL_CS_SELECTOR 0x08
#define KERNEL_DS_SELECTOR 0x10
#define USER_DS_SELECTOR   0x1B  /* (3 * 8) | 3 */
#define USER_CS_SELECTOR   0x23  /* (4 * 8) | 3 */
#define TSS_SELECTOR       0x28  /* (5 * 8) */

extern void load_gdt(gdtr_t*);
void init_gdt();
void gdt_set_kernel_stack(uint64_t stack_top);

// TSS

typedef struct {
uint32_t reserved0;
    uint64_t rsp0;       /* Ring 0 stack pointer (used for privilege changes) */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];     /* IST1 to IST7 stack pointers (top of stack)        */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;/* Offset from TSS start to I/O Permission Bitmap     */
} __attribute__((packed)) tss_t;

typedef struct {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;     /* 0x89: Present, Ring 0, Available 64-bit TSS */
    uint8_t  limit_hi : 4;
    uint8_t  flags    : 4;/* 0x00: Byte granularity */
    uint8_t  base_hi;
    uint32_t base_upper; /* Upper 32 bits of 64-bit base (Index 04, bytes 8-11) */
    uint32_t reserved;   /* Must be 0 (Index 04, bytes 12-15) */
} __attribute__((packed)) tss_desc64_t;

void set_tss_descriptor(gdt_t *gdt, int index, uint64_t base, uint32_t limit);

static inline void load_tss(uint16_t tss_selector) {
    __asm__ volatile("ltr %0" : : "r"(tss_selector));
}