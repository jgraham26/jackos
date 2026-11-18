#include "cpu/gdt.h"
#include "stdint.h"
#include <string.h>

static gdt_t gdt_entries[GDT_NUM_ENTRIES];
static gdtr_t gdtr;
static tss_t kernel_tss;

static uint8_t df_stack[4096]  __attribute__((aligned(16)));
static uint8_t nmi_stack[4096] __attribute__((aligned(16)));

void init_gdt_descriptor(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt_t* entry = &gdt_entries[index];
    
    entry->base_lo = (uint16_t)(base & 0xFFFF);
    entry->base_mid = (uint8_t)((base >> 16) & 0xFF);
    entry->base_hi = (uint8_t)((base >> 24) & 0xFF);

    entry->limit_lo = (uint16_t)(limit & 0xFFFF);
    entry->limit_hi = (uint8_t)((limit >> 16) & 0x0F);

    entry->access = access;
    entry->flags = (uint8_t)(flags & 0x0F);
}

void init_gdt() {
    init_gdt_descriptor(0, 0, 0, 0, 0);                 // 0x00: null descriptor
    init_gdt_descriptor(1, 0, 0xFFFFF, 0x9A, 0x0A);     // 0x08: kernel 64-bit code (G=1, L=1)
    init_gdt_descriptor(2, 0, 0xFFFFF, 0x92, 0x0C);     // 0x10: kernel data (G=1, D/B=1)
    init_gdt_descriptor(3, 0, 0xFFFFF, 0xF2, 0x0C);     // 0x18: user data (DPL 3, G=1, D/B=1)
    init_gdt_descriptor(4, 0, 0xFFFFF, 0xFA, 0x0A);     // 0x20: user 64-bit code (DPL 3, G=1, L=1)

    memset(&kernel_tss, 0, sizeof(tss_t));
    kernel_tss.ist[0] = (uint64_t)(df_stack + sizeof(df_stack));
    kernel_tss.ist[1] = (uint64_t)(nmi_stack + sizeof(nmi_stack));
    kernel_tss.iopb_offset = sizeof(tss_t);

    int tss_index = 5;
    set_tss_descriptor(gdt_entries, tss_index, (uint64_t)&kernel_tss, sizeof(tss_t) - 1);

    gdtr.offset = (uint64_t)&(gdt_entries[0]);
    gdtr.size = (sizeof(gdt_t) * GDT_NUM_ENTRIES) - 1;
    load_gdt(&gdtr);

    load_tss(TSS_SELECTOR);
}

void set_tss_descriptor(gdt_t *gdt, int index, uint64_t base, uint32_t limit) {
    tss_desc64_t *desc = (tss_desc64_t *)&gdt[index];

    desc->limit_lo   = (uint16_t)(limit & 0xFFFF);
    desc->base_lo    = (uint16_t)(base & 0xFFFF);
    desc->base_mid   = (uint8_t)((base >> 16) & 0xFF);
    desc->access     = 0x89; /* Available 64-bit TSS */
    desc->limit_hi   = (uint8_t)((limit >> 16) & 0x0F);
    desc->flags      = 0x00;
    desc->base_hi    = (uint8_t)((base >> 24) & 0xFF);
    
    /* Populates Index 04 */
    desc->base_upper = (uint32_t)(base >> 32);
    desc->reserved   = 0;
}

void gdt_set_kernel_stack(uint64_t stack_top) {
    kernel_tss.rsp0 = stack_top;
}