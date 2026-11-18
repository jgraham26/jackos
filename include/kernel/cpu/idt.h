#pragma once

#include "stdint.h"

#define IDT_MAX_ENTRIES 256

#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP      0x8F
#define IDT_GATE_USER_INT  0xEE

typedef struct {
    uint16_t offset_lo;     /* Bits 0..15 of handler address */
    uint16_t selector;      /* Code Segment selector (e.g. 0x08) */
    uint8_t  ist : 3;       /* IST index in TSS (1..7, 0 = no stack switch) */
    uint8_t  reserved0 : 5; /* Must be 0 */
    uint8_t  type_attr;     /* Gate type, DPL, Present bit */
    uint16_t offset_mid;    /* Bits 16..31 of handler address */
    uint32_t offset_hi;     /* Bits 32..63 of handler address */
    uint32_t reserved1;     /* Must be 0 */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t err_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) interrupt_frame_t;

void init_idt();
void set_idt_gate(int vector, void* handler, uint16_t selector, uint8_t flags, uint8_t ist);
extern void load_idtr(idtr_t *idtr);

static inline void sti() {
    __asm__ volatile ("sti");
}

static inline void cli() {
    __asm__ volatile ("cli");
}