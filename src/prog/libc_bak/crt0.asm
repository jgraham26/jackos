[bits 64]
global _start
extern main
extern __libc_start_main
extern exit

section .text progbits alloc exec nowrite
_start:
    xchg bx, bx
    xor rbp, rbp            ; Clear frame pointer for stack backtraces

    mov rdi, main          ; rdi = main
    mov rsi, [rsp]      ; rsi = argc
    lea rdx, [rsp + 8] ; rdx = envv
    lea rcx, [rdx + rsi*8 + 8] ; rcx = envp

    ; System V ABI: before call, stack must be 16-byte aligned.
    ; rsp is currently aligned to 16. The `call` instruction will push 
    ; an 8-byte return address, making it 16-byte aligned inside main.
    and rsp, -16
    call __libc_start_main

    xor rdi, rdi
    jmp exit

