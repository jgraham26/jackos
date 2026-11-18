[bits 64]
global setjmp
global _setjmp

section .text

; int setjmp(jmp_buf env);
; Argument in rdi (pointer to 8-element 64-bit array)
setjmp:
_setjmp:
    ; Save callee-preserved general purpose registers
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15

    ; Save stack pointer as it was BEFORE the call instruction
    ; (rsp + 8 skips the return address pushed onto the stack)
    lea rax, [rsp + 8]
    mov [rdi + 48], rax

    ; Save the return address (the rip on top of the stack)
    mov rax, [rsp]
    mov [rdi + 56], rax

    ; setjmp returns 0 on direct invocation
    xor eax, eax
    ret

global longjmp
global _longjmp

; void longjmp(jmp_buf env, int val);
; Arguments: rdi = env, rsi = val
longjmp:
_longjmp:
    ; Move val into eax; if val == 0, POSIX dictates returning 1
    mov eax, esi
    test eax, eax
    jnz .val_nonzero
    mov eax, 1
.val_nonzero:

    ; Restore callee-preserved general purpose registers
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]

    ; Restore stack pointer
    mov rsp, [rdi + 48]

    ; Fetch return address and jump directly to it
    mov rdx, [rdi + 56]
    jmp rdx