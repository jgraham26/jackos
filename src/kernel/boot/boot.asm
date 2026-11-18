[bits 32]
DEFAULT REL

KERNEL_VMA_BASE equ 0xffffffff80000000

section .boot
align 8
header_start:
    dd 0xe85250d6                                      ; Multiboot2 magic[cite: 9]
    dd 0                                               ; Architecture: i386
    dd header_end - header_start                       ; Header length (0x30)[cite: 9]
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; Checksum

framebuffer_tag_start:
    dw 5                                               ; Type: framebuffer[cite: 9]
    dw 0                                               ; Flags
    dd framebuffer_tag_end - framebuffer_tag_start     ; Size: 20 (0x14)[cite: 9]
    dd 1024                                            ; Width (0x400)[cite: 9]
    dd 768                                             ; Height (0x300)[cite: 9]
    dd 32                                              ; Depth (0x20)[cite: 9]
framebuffer_tag_end:

align 8
    dw 0                                               ; End tag type[cite: 9]
    dw 0                                               ; End tag flags
    dd 8                                               ; End tag size[cite: 9]
header_end:

global _start
extern fpu_init_cpu
extern kmain

_start:
    cli
    mov esp, early_stack_top
    push ebx                                           ; Save Multiboot2 info pointer

    ; Check CPUID support
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid

    ; Check Extended CPUID & Long Mode (LM bit 29)
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; Setup identity + higher-half 2MiB paging (PML4=0x101000, PDPT_low=0x102000, PDPT_high=0x103000)[cite: 9]
    mov eax, 0x102000
    or eax, 0x3
    mov [0x101000], eax

    mov eax, 0x103000
    or eax, 0x3
    mov [0x101ff8], eax

    mov dword [0x102000], 0x83
    mov dword [0x102004], 0

    mov dword [0x103ff0], 0x83
    mov dword [0x103ff4], 0

    ; Enable PSE (bit 4) and PAE (bit 5) in CR4[cite: 9]
    mov eax, cr4
    bts eax, 4
    bts eax, 5
    mov cr4, eax

    ; Load PML4 into CR3[cite: 9]
    mov eax, 0x101000
    mov cr3, eax

    ; Enable Long Mode (LME bit 8) in IA32_EFER[cite: 9]
    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable Paging (bit 31) and Protection (bit 0) in CR0[cite: 9]
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    ; Load 64-bit GDT and far jump to 64-bit code segment[cite: 9]
    lgdt [gdt64.pointer]
    pop ebx                                            ; Restore Multiboot2 info pointer
    jmp gdt64.code:.entry64_low

.no_cpuid:
.no_long_mode:
    mov dword [0xb8000], 0x4f524f45                    ; "ER"[cite: 9]
    mov word  [0xb8004], 0x4f52                        ; "R"[cite: 9]
.hang:
    hlt
    jmp .hang

[bits 64]
.entry64_low:
    mov rax, _start.entry64_high
    jmp rax

section .boot_bss nobits alloc noexec write
align 16
early_stack_bottom:
    resb 16384                                         ; 0x104000 .. 0x108000[cite: 9]
early_stack_top:

; Placed in .boot (or .boot_data per your linker script) so gdt64 stays in low physical memory (< 4 GiB)[cite: 9]
section .boot
align 8
gdt64:
    dq 0                                               ; 0x00: Null descriptor[cite: 9]
.code: equ $ - gdt64                                   ; 0x08: 64-bit Kernel Code[cite: 9]
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.data: equ $ - gdt64                                   ; 0x10: 64-bit Kernel Data[cite: 9]
    dq (1 << 41) | (1 << 44) | (1 << 47)
.pointer:
    dw $ - gdt64 - 1                                   ; Limit: 0x0017[cite: 9]
    dq gdt64                                           ; Base: physical address of gdt64[cite: 9]

section .text
_start.entry64_high:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp
    and rsp, -16

    mov edi, ebx                                       ; Arg 1: Multiboot2 physical info struct

    ; Unmap identity PML4[0] and flush TLB[cite: 9]
    mov rax, KERNEL_VMA_BASE
    mov qword [abs 0x101000], 0
    invlpg [abs 0]

    call fpu_init_cpu
    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384                                         ; 16 KiB kernel boot stack[cite: 9]
stack_top: