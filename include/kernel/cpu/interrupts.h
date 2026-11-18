#pragma once

void disable_pic();


#define LAPIC_BASE_MSR      0x1B
#define LAPIC_ENABLE_BIT    (1 << 11)

#define LAPIC_SVR           0x0F0
#define LAPIC_EOI           0x0B0
#define LAPIC_TIMER_LVT     0x320
#define LAPIC_TIMER_INIT    0x380
#define LAPIC_TIMER_CURR    0x390
#define LAPIC_TIMER_DIV     0x3E0