#pragma once

#include <stdint.h>
#include <stddef.h>


static inline void fpu_save(void *region) {
    __asm__ volatile("fxsave64 %0" : "=m"(*(uint8_t (*)[512])region) :: "memory");
}

static inline void fpu_restore(const void *region) {
    __asm__ volatile("fxrstor64 %0" :: "m"(*(const uint8_t (*)[512])region) : "memory");
}