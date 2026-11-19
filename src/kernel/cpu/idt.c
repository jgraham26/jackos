#include "cpu/idt.h"
#include "cpu/apic.h"
#include "drivers/ps2_kbd.h"
#include "fs/vfs.h"
#include "kernel.h"
#include "mem/vmm.h"
#include "mem/pagecache.h"
#include "sched/process.h"
#include "sched/sched.h"
#include "mem/pmm.h"
#include "string.h"
#include "io.h"
#include <stddef.h>
#include <stdint.h>
#include "drivers/klog.h"
#include <stdio.h>

static idt_entry_t idt_entries[IDT_MAX_ENTRIES];
static idtr_t idtr;

typedef void (*interrupt_handler_t)(interrupt_frame_t*);
interrupt_handler_t isr_handlers[IDT_MAX_ENTRIES - 0x20] = {0};

void register_handlers();

#define DECLARE_ISR(n) extern void isr_##n(void)

DECLARE_ISR(0);  DECLARE_ISR(1);  DECLARE_ISR(2);  DECLARE_ISR(3);
DECLARE_ISR(4);  DECLARE_ISR(5);  DECLARE_ISR(6);  DECLARE_ISR(7);
DECLARE_ISR(8);  DECLARE_ISR(9);  DECLARE_ISR(10); DECLARE_ISR(11);
DECLARE_ISR(12); DECLARE_ISR(13); DECLARE_ISR(14); DECLARE_ISR(15);
DECLARE_ISR(16); DECLARE_ISR(17); DECLARE_ISR(18); DECLARE_ISR(19);
DECLARE_ISR(20); DECLARE_ISR(21); DECLARE_ISR(22); DECLARE_ISR(23);
DECLARE_ISR(24); DECLARE_ISR(25); DECLARE_ISR(26); DECLARE_ISR(27);
DECLARE_ISR(28); DECLARE_ISR(29); DECLARE_ISR(30); DECLARE_ISR(31);

/* IRQs 0-15 mapped to vectors 32-47 */
DECLARE_ISR(32); DECLARE_ISR(33); DECLARE_ISR(34); DECLARE_ISR(35);
DECLARE_ISR(36); DECLARE_ISR(37); DECLARE_ISR(38); DECLARE_ISR(39);
DECLARE_ISR(40); DECLARE_ISR(41); DECLARE_ISR(42); DECLARE_ISR(43);
DECLARE_ISR(44); DECLARE_ISR(45); DECLARE_ISR(46); DECLARE_ISR(47);

DECLARE_ISR(255);


static void *isr_stub_table[48] = {
    isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
    isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39,
    isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47
};

static const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "x87 FPU Floating-Point Error", "Alignment Check", "Machine Check", "SIMD Exception",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

void set_idt_gate(int vector, void *handler, uint16_t selector, uint8_t flags, uint8_t ist) {
    uint64_t addr = (uint64_t)handler;

    idt_entries[vector].offset_lo   = (uint16_t)(addr & 0xFFFF);
    idt_entries[vector].selector    = selector;
    idt_entries[vector].ist         = (ist & 0x07);
    idt_entries[vector].reserved0   = 0;
    idt_entries[vector].type_attr   = flags;
    idt_entries[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt_entries[vector].offset_hi   = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt_entries[vector].reserved1   = 0;
}

void init_idt() {
    memset(idt_entries, 0, sizeof(idt_entries));

    for (int i = 0; i < 32; i++) {
        uint8_t ist_val = 0;
        if (i == 2) {
            ist_val = 2;
        } else if (i == 8) {
            ist_val = 1;
        }

        set_idt_gate(i, isr_stub_table[i], 0x08, IDT_GATE_INTERRUPT, ist_val);
    }

    for (int i = 32; i < 48; i++) {
        set_idt_gate(i, isr_stub_table[i], 0x08, IDT_GATE_INTERRUPT, 0);
    }

    set_idt_gate(0xFF, isr_255, 0x08, IDT_GATE_INTERRUPT, 0);

    idtr.limit = sizeof(idt_entries) - 1;
    idtr.base = (uint64_t)&idt_entries[0];

    load_idtr(&idtr);
    register_handlers();
}

void sent_pic_eoi(uint8_t vector) {
    if (vector >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static bool handle_page_fault(interrupt_frame_t *frame, uint64_t cr2) {
    if (cr2 >= 0x0000800000000000ULL) {
        return false;
    }

    process_t *proc = g_current_thread ? g_current_thread->process : NULL;
    if (!proc || !proc->pagemap) {
        return false;
    }

    uintptr_t page_vaddr = cr2 & ~(PAGE_SIZE - 1);
    bool present = (frame->err_code & (1ULL << 0)) != 0;
    bool write   = (frame->err_code & (1ULL << 1)) != 0;

    /* Case 1: Protection violation on a present page -> check Copy-on-Write */
    if (present) {
        if (!write) {
            return false; /* Read/exec permission fault */
        }

        pt_entry_t *pte = vmm_get_pte(proc->pagemap, page_vaddr);
        if (!pte || !(*pte & PTE_PRESENT) || !(*pte & PTE_COW)) {
            return false; /* Genuine write to read-only page (e.g. .rodata) */
        }

        uintptr_t old_phys = *pte & PTE_FRAME_ADDR_MASK;
        uint64_t flags = (*pte & PTE_FLAGS_MASK & ~PTE_COW) | PTE_WRITABLE;

        if (pmm_get_refcount(old_phys) <= 1) {
            /* Sole remaining owner: reclaim write permission in-place with zero copy */
            *pte = old_phys | flags;
            vmm_invlpg(page_vaddr);
            return true;
        }

        /* Shared by multiple processes or page cache: allocate private copy */
        uintptr_t new_phys = pmm_alloc_frame();
        if (!new_phys) {
            return false; /* OOM */
        }

        memcpy((void *)hhdm_physical_to_virtual(new_phys),
               (void *)hhdm_physical_to_virtual(old_phys),
               PAGE_SIZE);

        *pte = new_phys | flags;
        pmm_free_frame(old_phys); /* Decrements old_phys refcount */
        vmm_invlpg(page_vaddr);
        return true;
    }

    /* Case 2: Non-present page -> check Demand Paging VMA */
    vma_t *vma = vma_find(proc, page_vaddr);
    if (!vma) {
        return false;
    }
    if (write && !(vma->flags & VMA_WRITE)) {
        return false;
    }

    bool is_ro_file = (vma->file != NULL) && !(vma->flags & VMA_WRITE);
    uint64_t file_off = vma->file ? (vma->file_offset + (page_vaddr - vma->start)) : 0;

    uintptr_t phys = 0;
    if (is_ro_file) {
        phys = pagecache_lookup(vma->file, file_off);
    }

    if (!phys) {
        phys = pmm_alloc_frame();
        if (!phys) {
            return false; /* OOM */
        }

        void *page_kptr = (void *)hhdm_physical_to_virtual(phys);
        memset(page_kptr, 0, PAGE_SIZE);

        if (vma->file && vma->file->ops && vma->file->ops->read) {
            if (file_off < vma->file->size) {
                size_t to_read = PAGE_SIZE;
                if (file_off + to_read > vma->file->size) {
                    to_read = vma->file->size - (size_t)file_off;
                }
                vfs_file_t tmp_file = {
                    .node     = vma->file,
                    .offset   = file_off,
                    .flags    = O_RDONLY,
                    .refcount = 1
                };
                vma->file->ops->read(&tmp_file, page_kptr, to_read);
            }

            if (is_ro_file) {
                pagecache_insert(vma->file, file_off, phys);
            }
        }
    }

    uint64_t pte_flags = PTE_PRESENT | PTE_USER;
    if (vma->flags & VMA_WRITE) {
        pte_flags |= PTE_WRITABLE;
    }

    if (vmm_map_page(proc->pagemap, page_vaddr, phys, pte_flags) != 0) {
        pmm_free_frame(phys);
        return false;
    }

    return true;
}

interrupt_frame_t* interrupt_dispatcher(interrupt_frame_t* frame) {
    if (frame->vector == 0xFF) {
        return frame;
    }
    if (frame->vector < 32) {

        uint64_t cr2 = 0;
        if (frame->vector == 14) {
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            if (handle_page_fault(frame, cr2)) {
                return frame;
            }
            klog_printf("[PAGE FAULT] Unhandled CR2=%#llx RIP=%#llx CS=%#llx RSP=%#llx err=%#llx\n",
                          cr2, frame->rip, frame->cs, frame->rsp, frame->err_code);
            
            if ((frame->cs & 0x03) == 0x03 && g_current_thread && g_current_thread->process) {
                kernel_send_signal(g_current_thread->process, SIGSEGV);
                sys_exit(128 + SIGSEGV);
            }
            hcf();
        } else if (frame->vector == 13) {
            cli();
            klog_printf("Faulting RIP:    %#llx\n", frame->rip);
            klog_printf("Faulting CS:     %#llx\n", frame->cs);
            klog_printf("Faulting RFLAGS: %#llx\n", frame->rflags);
            klog_printf("Faulting RSP:    %#llx\n", frame->rsp);
            klog_printf("Faulting SS:     %#llx\n", frame->ss);
            for (;;) __asm__ volatile("hlt");
        } else {
            uint64_t cr2 = 0, cr3 = 0;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
            const char* msg = exception_messages[frame->vector];
            klog_printf("CPU EXCEPTION %llu (%s) on TID %llu\n",
                          frame->vector, msg, g_current_thread ? g_current_thread->tid : 0);
            klog_printf("  RIP=%#llx CS=%#llx RSP=%#llx CR2=%#llx CR3=%#llx err=%#llx\n",
                          frame->rip, frame->cs, frame->rsp, cr2, cr3, frame->err_code);
            hcf();
        }

        const char* msg = (frame->vector < 32) ? exception_messages[frame->vector] : "Reserved";
        klog_printf("CPU EXCEPTION: %s on TID %llu\n", msg, g_current_thread ? g_current_thread->tid : 0);

        bochs_breakpoint();

        hcf();
    } else if (frame->vector >= 32 && frame->vector < 48) {
        if (isr_handlers[frame->vector - 0x20]) {
            isr_handlers[frame->vector - 0x20](frame);
        }
        apic_eoi();
    }

    if (frame->vector == 0x20) { // timer
        return sched_schedule(frame);
    }

    return frame;
}



// handlers

volatile uint64_t g_ticks = 0;

void apic_timer_handler(interrupt_frame_t* frame) {
    (void)frame;
    g_ticks++;
}

void kbd_irq_handler_int(interrupt_frame_t* frame) {
    (void)frame;
    kbd_irq_handler();
}

void register_handlers() {
    isr_handlers[0] = apic_timer_handler;
    isr_handlers[1] = kbd_irq_handler_int;
}