#pragma once

#include <stdint.h>
#include <stddef.h>

#define KSTACK_REGION_BASE  0xffffa00000000000ULL
#define KSTACK_GUARD_PAGES  1
#define KSTACK_DATA_PAGES   4   /* 16 KiB usable stack */
#define KSTACK_SLOT_PAGES   (KSTACK_GUARD_PAGES + KSTACK_DATA_PAGES) /* 20 KiB stride */
#define KSTACK_SIZE         (KSTACK_DATA_PAGES * 4096ULL)

typedef struct kstack {
    uintptr_t guard_base;
    uintptr_t base;
    uintptr_t top;
} kstack_t;

void kstack_init();
int kstack_alloc(kstack_t* out);
void kstack_free(kstack_t* stack);