#include "cpu/percpu.h"
#include "io.h"
#include <stdint.h>

#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102
#define MSR_FMASK          0xC0000084

cpu_local_t g_bsp_cpu = {0};

void percpu_init() {
    wrmsr(MSR_GS_BASE, (uint64_t)&g_bsp_cpu);
    wrmsr(MSR_KERNEL_GS_BASE, 0);

    wrmsr(MSR_FMASK, 0x200 | 0x400);
}