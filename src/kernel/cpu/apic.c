#include <string.h>
#include <io.h>
#include <stdint.h>
#include <string.h>
#include "cpu/apic.h"
#include "boot/bootinfo.h"
#include "mem/vmm.h"
#include "mem/pmm.h"
#include "drivers/pit.h"
#include "sched/sched.h"
#include "sched/spinlock.h"
#include "stdio.h"

#define LAPIC_DEFAULT_PHYS 0xFEE00000ULL

#define IA32_APIC_BASE_MSR        0x1B
#define IA32_APIC_BASE_MSR_ENABLE (1ULL << 11)

static uintptr_t s_lapic_virt = 0;

/* Ensure an MMIO or ACPI page is mapped into kernel_pagemap at its offset */
static void ensure_range_mapped(uintptr_t phys_addr, size_t length, uint64_t extra_flags) {
    if (length == 0) return;

    uintptr_t start_page = phys_addr & ~((uintptr_t)PAGE_SIZE - 1);
    uintptr_t end_page = (phys_addr + length + PAGE_SIZE - 1) & ~((uintptr_t)PAGE_SIZE - 1);

    for (uintptr_t p = start_page; p < end_page; p += PAGE_SIZE) {
        uintptr_t v = hhdm_physical_to_virtual(p);
        if (vmm_virt_to_phys(&kernel_pagemap, v) == 0) {
            vmm_map_page(&kernel_pagemap, v, p, PTE_PRESENT | PTE_WRITABLE | extra_flags);
            vmm_invlpg(v);
        }
    }
}

#define MAX_ISO_ENTRIES 16

static uintptr_t s_ioapic_virt = 0;
static uint32_t  s_ioapic_gsi_base = 0;
static size_t    s_iso_count = 0;
static struct madt_iso s_iso_entries[MAX_ISO_ENTRIES];


/* --- ACPI Table Headers --- */
struct acpi_rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_rsdp_descriptor20 {
    struct acpi_rsdp_descriptor first_part;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

static void ioapic_parse_madt(struct acpi_madt *madt) {
    if (!madt) return;

    uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (ptr < end) {
        struct madt_entry_header *entry = (struct madt_entry_header *)ptr;
        if (entry->length == 0) break;

        if (entry->type == MADT_ENTRY_IO_APIC) {
            struct madt_ioapic *io = (struct madt_ioapic *)entry;
            uintptr_t ioapic_phys = io->ioapic_address;
            s_ioapic_gsi_base = io->gsi_base;

            /* Map I/O APIC physical MMIO page */
            s_ioapic_virt = hhdm_physical_to_virtual(ioapic_phys);
            vmm_map_page(&kernel_pagemap,
                         s_ioapic_virt,
                         ioapic_phys,
                         PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE | PTE_WRITETHROUGH);
            vmm_invlpg(s_ioapic_virt);
        } else if (entry->type == MADT_ENTRY_INT_SOURCE_OVERRIDE) {
            if (s_iso_count < MAX_ISO_ENTRIES) {
                s_iso_entries[s_iso_count++] = *(struct madt_iso *)entry;
            }
        }

        ptr += entry->length;
    }
}

static uintptr_t apic_get_base_phys_from_acpi(void) {
    serial_printf("rsdp_addr: %#llx\n", g_boot_info.rsdp_addr);
    if (!g_boot_info.rsdp_addr) {
        return LAPIC_DEFAULT_PHYS;
    }
    
    struct acpi_madt *madt = NULL;
    
    // g_boot_info.rsdp_addr points inside the Multiboot2 tag structure,
    // which was parsed from mb2_virt in boot_info_parse() and is already mapped!
    if (g_boot_info.rsdp_is_v2) {
        struct acpi_rsdp_descriptor20 *rsdp = 
        (struct acpi_rsdp_descriptor20 *)g_boot_info.rsdp_addr;
        
        // xsdt_address IS a true physical address reported by hardware
        uintptr_t xsdt_phys = (uintptr_t)rsdp->xsdt_address;
        if (!xsdt_phys) {
            return LAPIC_DEFAULT_PHYS;
        }
        
        ensure_range_mapped(xsdt_phys, sizeof(struct acpi_sdt_header), 0);
        struct acpi_sdt_header *xsdt = 
            (struct acpi_sdt_header *)hhdm_physical_to_virtual(xsdt_phys);
        
        // Map any extra pages if the XSDT table crosses a 4KiB page boundary
        ensure_range_mapped(xsdt_phys, xsdt->length, 0);
        
        size_t entries = (xsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint64_t);
        uint64_t *pointers = (uint64_t *)((uintptr_t)xsdt + sizeof(struct acpi_sdt_header));
        
        for (size_t i = 0; i < entries; i++) {
            uintptr_t entry_phys = (uintptr_t)pointers[i];
            ensure_range_mapped(entry_phys, sizeof(struct acpi_sdt_header), 0);
            
            struct acpi_sdt_header *header = (struct acpi_sdt_header *)hhdm_physical_to_virtual(entry_phys);
            
            if (memcmp(header->signature, "APIC", 4) == 0) {
                madt = (struct acpi_madt *)header;
                break;
            }
        }
    } else {
        struct acpi_rsdp_descriptor *rsdp = (struct acpi_rsdp_descriptor *)g_boot_info.rsdp_addr;
            
        uintptr_t rsdt_phys = (uintptr_t)rsdp->rsdt_address;
        if (!rsdt_phys) {
            return LAPIC_DEFAULT_PHYS;
        }
        
        ensure_range_mapped(rsdt_phys, sizeof(struct acpi_sdt_header), 0);
        struct acpi_sdt_header *rsdt = (struct acpi_sdt_header *)hhdm_physical_to_virtual(rsdt_phys);
        
        ensure_range_mapped(rsdt_phys, rsdt->length, 0);
        
        size_t entries = (rsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint32_t);
        uint32_t *pointers = (uint32_t *)((uintptr_t)rsdt + sizeof(struct acpi_sdt_header));
        
        for (size_t i = 0; i < entries; i++) {
            uintptr_t entry_phys = (uintptr_t)pointers[i];
            if (!entry_phys) continue;

            ensure_range_mapped(entry_phys, sizeof(struct acpi_sdt_header), 0);
            
            struct acpi_sdt_header *header = 
            (struct acpi_sdt_header *)hhdm_physical_to_virtual(entry_phys);
            
            if (memcmp(header->signature, "APIC", 4) == 0) {
                madt = (struct acpi_madt *)header;
                break;
            }
        }
    }
    
    if (madt) {
        ioapic_parse_madt(madt);
        if (madt->local_apic_address) {
            return (uintptr_t)madt->local_apic_address;
        }
    }

    return LAPIC_DEFAULT_PHYS;
}

static void pic_disable(void) {
    outb(0x21, 0xFF);
    io_wait();
    outb(0xA1, 0xFF);
    io_wait();
}

uint32_t apic_read(uint32_t reg) {
    return *(volatile uint32_t *)(s_lapic_virt + reg);
}

void apic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(s_lapic_virt + reg) = val;
    __asm__ volatile("" ::: "memory");
}

void apic_eoi(void) {
    apic_write(LAPIC_EOI_REG, 0);
}

void apic_init(void) {
    /* 1. Hardware enable LAPIC via MSR */
    uint64_t msr = rdmsr(IA32_APIC_BASE_MSR);
    wrmsr(IA32_APIC_BASE_MSR, msr | IA32_APIC_BASE_MSR_ENABLE);

    /* 2. Disable legacy 8259 PIC */
    pic_disable();
    
    /* 3. Determine Local APIC physical address */
    uintptr_t lapic_phys = apic_get_base_phys_from_acpi();
    
    /* 4. Compute virtual address using offset macro */
    s_lapic_virt = hhdm_physical_to_virtual(lapic_phys);


    
    /* 5. Map the physical MMIO page */
    vmm_map_page(&kernel_pagemap,
        s_lapic_virt,
        lapic_phys,
        PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE | PTE_WRITETHROUGH);
    vmm_invlpg(s_lapic_virt);

    /* 6. Accept all interrupt priorities */
    apic_write(LAPIC_TPR_REG, 0);

    /* 7. Mask all Local Vector Table entries until registered */
    apic_write(LAPIC_LVT_TIMER, 0x10000);
    apic_write(LAPIC_LVT_LINT0, 0x10000);
    apic_write(LAPIC_LVT_LINT1, 0x10000);
    apic_write(LAPIC_LVT_ERROR, 0x10000);

    /* 8. Enable the Local APIC software enable bit and set the spurious vector */
    apic_write(LAPIC_SVR_REG, LAPIC_SVR_ENABLE | APIC_SPURIOUS_IRQ);
}

static uint32_t s_ticks_per_ms = 0;

void apic_timer_init(uint32_t target_hz) {
    // tell apic to divide bus clock by 16
    apic_write(LAPIC_TIMER_DIV, APIC_TIMER_DIV_16);

    // set timer to one shot mode and mask interrupts
    apic_write(LAPIC_LVT_TIMER, APIC_TIMER_MASKED | APIC_TIMER_MODE_ONESHOT);

    apic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);

    pit_delay_ms(10);

    uint32_t ticks_remaining = apic_read(LAPIC_TIMER_CURRCNT);
    uint32_t ticks_elapsed = 0xFFFFFFFF - ticks_remaining;

    apic_write(LAPIC_LVT_TIMER, APIC_TIMER_MASKED);

    s_ticks_per_ms = ticks_elapsed / 10;

    if (s_ticks_per_ms == 0) {
        s_ticks_per_ms = 1;
    }

    uint32_t ticks_per_quantum = (s_ticks_per_ms * 1000) / target_hz;
    apic_write(LAPIC_TIMER_DIV, APIC_TIMER_DIV_16);
    apic_write(LAPIC_LVT_TIMER, APIC_TIMER_MODE_PERIODIC | APIC_TIMER_INT_VECTOR);
    apic_write(LAPIC_TIMER_INITCNT, ticks_per_quantum);
}


// io apic

static uint32_t ioapic_read(uint32_t reg_index) {
    *(volatile uint32_t *)(s_ioapic_virt + IOAPIC_REGSEL) = reg_index;
    return *(volatile uint32_t *)(s_ioapic_virt + IOAPIC_IOWIN);
}

static void ioapic_write(uint32_t reg_index, uint32_t val) {
    *(volatile uint32_t *)(s_ioapic_virt + IOAPIC_REGSEL) = reg_index;
    *(volatile uint32_t *)(s_ioapic_virt + IOAPIC_IOWIN) = val;
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) {
    if (!s_ioapic_virt) {
        return;
    }

    uint32_t gsi = irq;
    uint32_t flags = IOAPIC_TRIGGER_EDGE | IOAPIC_POLARITY_ACTIVE_HIGH;

    /* Check ACPI Interrupt Source Overrides for this ISA IRQ */
    for (size_t i = 0; i < s_iso_count; i++) {
        if (s_iso_entries[i].irq_source == irq) {
            gsi = s_iso_entries[i].gsi;

            /* Check polarity: bits [1:0] of MPS INTI flags */
            uint16_t polarity = s_iso_entries[i].flags & 0x03;
            if (polarity == 0x03) {
                flags |= IOAPIC_POLARITY_ACTIVE_LOW;
            } else {
                flags |= IOAPIC_POLARITY_ACTIVE_HIGH;
            }

            /* Check trigger mode: bits [3:2] of MPS INTI flags */
            uint16_t trigger = (s_iso_entries[i].flags >> 2) & 0x03;
            if (trigger == 0x03) {
                flags |= IOAPIC_TRIGGER_LEVEL;
            } else {
                flags |= IOAPIC_TRIGGER_EDGE;
            }
            break;
        }
    }

    /* Redirection index within this I/O APIC */
    uint32_t redtbl_index = gsi - s_ioapic_gsi_base;

    /* Lower 32 bits: vector, delivery mode, polarity, trigger, and unmasked (bit 16 = 0) */
    uint32_t low_word = (uint32_t)vector | flags | IOAPIC_DELIVERY_FIXED | IOAPIC_DEST_PHYSICAL;
    /* Upper 32 bits: destination APIC ID in bits [63:56] (offset 24 in high word) */
    uint32_t high_word = (uint32_t)dest_apic_id << 24;

    /* Write to the two 32-bit registers */
    ioapic_write(IOAPIC_REG_REDTBL(redtbl_index), low_word);
    ioapic_write(IOAPIC_REG_REDTBL(redtbl_index) + 1, high_word);
}