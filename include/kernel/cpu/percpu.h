#pragma once

#include <stdint.h>

typedef struct __attribute__((packed)) cpu_local {
    uint64_t kernel_rsp;   /* offset +0: current thread's kstack.top */
    uint64_t user_rsp;     /* offset +8: scratch slot for user RSP */
    uint64_t cpu_id;       /* offset +16 */
} cpu_local_t;

extern cpu_local_t g_bsp_cpu;
void percpu_init();