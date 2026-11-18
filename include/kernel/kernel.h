#pragma once

void __attribute__((noreturn)) hcf();

static inline void bochs_breakpoint() {
    __asm__ volatile ("xchg %bx, %bx");
}

void __attribute__((noreturn)) idle();

static inline void hlt() {
    __asm__ volatile ("hlt");
}