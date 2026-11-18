ENTRY(_start)

KERNEL_PHYS_BASE = 0x100000;
KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

PHDRS {
    boot_text PT_LOAD FLAGS(5); /* R + X */
    boot_data PT_LOAD FLAGS(6); /* R + W */
    text      PT_LOAD FLAGS(5); /* R + X */
    rodata    PT_LOAD FLAGS(4); /* R */
    data      PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS {
    . = KERNEL_PHYS_BASE;
    _kernel_phys_start = .;

    .boot : ALIGN(4K) {
        KEEP(*(.multiboot_header))
        *(.boot .boot.*)
    } :boot_text

    .boot_bss : ALIGN(4K) {
        *(.boot_bss)
    } :boot_data

    . = KERNEL_VIRT_BASE + . ;

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(.text .text.*)
    } :text

    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(.rodata .rodata.*)
    } :rodata

    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(.data .data.*)
    } :data

    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }

    _kernel_phys_end = . - 0xFFFFFFFF80000000;
}