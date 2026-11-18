#pragma once

#include <stdint.h>

#define LAPIC_ID_REG          0x0020
#define LAPIC_VERSION_REG     0x0030
#define LAPIC_TPR_REG         0x0080
#define LAPIC_EOI_REG         0x00B0
#define LAPIC_SVR_REG         0x00F0
#define LAPIC_ESR_REG         0x0280
#define LAPIC_ICR_LOW         0x0300
#define LAPIC_ICR_HIGH        0x0310
#define LAPIC_LVT_TIMER       0x0320
#define LAPIC_LVT_LINT0       0x0350
#define LAPIC_LVT_LINT1       0x0360
#define LAPIC_LVT_ERROR       0x0370
#define LAPIC_TIMER_INITCNT   0x0380
#define LAPIC_TIMER_CURRCNT   0x0390
#define LAPIC_TIMER_DIV       0x03E0

#define LAPIC_SVR_ENABLE      0x0100   /* Bit 8 enables software APIC */
#define APIC_SPURIOUS_IRQ     0xFF     /* Conventional vector for spurious interrupt */

#define APIC_TIMER_MODE_PERIODIC (1 << 17)
#define APIC_TIMER_MODE_ONESHOT  (0 << 17)
#define APIC_TIMER_MASKED        (1 << 16)

#define APIC_TIMER_DIV_16        0x03   // Divide configuration: divide by 16
#define APIC_TIMER_INT_VECTOR    0x20   // Vector in IDT for timer ticks

void apic_init(void);
void apic_eoi(void);
uint32_t apic_read(uint32_t reg);
void apic_write(uint32_t reg, uint32_t val);

void apic_timer_init(uint32_t target_hz);

extern volatile uint64_t g_ticks;

void sleep_ms(uint32_t ms);

/* I/O APIC MMIO Register Offsets */
#define IOAPIC_REGSEL 0x00
#define IOAPIC_IOWIN  0x10

/* I/O APIC Register Indexes */
#define IOAPIC_REG_ID     0x00
#define IOAPIC_REG_VER    0x01
#define IOAPIC_REG_ARB    0x02
#define IOAPIC_REG_REDTBL(n) (0x10 + 2 * (n))

/* Redirection Entry Flags */
#define IOAPIC_DELIVERY_FIXED        (0 << 8)
#define IOAPIC_DEST_PHYSICAL         (0 << 11)
#define IOAPIC_POLARITY_ACTIVE_HIGH  (0 << 13)
#define IOAPIC_POLARITY_ACTIVE_LOW   (1 << 13)
#define IOAPIC_TRIGGER_EDGE          (0 << 15)
#define IOAPIC_TRIGGER_LEVEL         (1 << 15)
#define IOAPIC_MASKED                (1 << 16)

/* MADT Record Types */
#define MADT_ENTRY_LOCAL_APIC           0
#define MADT_ENTRY_IO_APIC              1
#define MADT_ENTRY_INT_SOURCE_OVERRIDE  2

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct madt_ioapic {
    struct madt_entry_header header;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_iso {
    struct madt_entry_header header;
    uint8_t  bus_source;    /* 0 = ISA */
    uint8_t  irq_source;    /* ISA IRQ */
    uint32_t gsi;           /* Global System Interrupt */
    uint16_t flags;         /* MPS INTI flags (polarity, trigger mode) */
} __attribute__((packed));

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id);