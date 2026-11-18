#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT ((spinlock_t){ .lock = 0 })

static inline void spinlock_init(spinlock_t* l) {
    l->lock = 0;
}

static inline void spinlock_acquire(spinlock_t* l) {
    while (__atomic_test_and_set(&l->lock, __ATOMIC_ACQUIRE)) {
        __builtin_ia32_pause();
    }
}

static inline bool spinlock_try_acquire(spinlock_t* l) {
    return !__atomic_test_and_set(&l->lock, __ATOMIC_ACQUIRE);
}

static inline void spinlock_release(spinlock_t* l) {
    __atomic_clear(&l->lock, __ATOMIC_RELEASE);
}

static inline uint64_t spinlock_irqsave(spinlock_t* l) {
    uint64_t rflags;
    __asm__ volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(rflags)
        :
        : "memory"
    );

    spinlock_acquire(l);
    return rflags;
}

static inline void spinlock_irqrestore(spinlock_t* l, uint64_t rflags) {
    spinlock_release(l);
    __asm__ volatile(
        "pushq %0\n\t"
        "popfq"
        :
        : "r"(rflags)
        : "memory", "cc"
    );
}