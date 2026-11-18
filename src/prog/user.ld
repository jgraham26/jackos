ENTRY(_start)

PHDRS
{
    /* PT_LOAD segment with the ELF headers explicitly embedded */
    text PT_LOAD FILEHDR PHDRS FLAGS(5); /* R-X */
    data PT_LOAD FLAGS(6);               /* RW- */
}

SECTIONS
{
    /* 
     * Starting at 0x400000:
     * Headers take up the first ~0x200 bytes, and .text immediately follows.
     */
    . = 0x400000 + 0x1000;

    .text : {
        *(.text._start)
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    /* Align data to the next 4 KiB page boundary */
    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    .eh_frame : {
        KEEP(*(.eh_frame))
        *(.eh_frame.*)
    }

    .eh_frame_hdr : {
        *(.eh_frame_hdr)
        *(.eh_frame_hdr.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}