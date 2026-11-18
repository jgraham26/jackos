[bits 64]

section .text
global switch_to

; void switch_to(uint64_t *old_rsp_ptr, thread_context_t *next)
; RDI = pointer to outgoing RSP storage
; RSI = pointer to incoming struct { uint64_t rsp; uint64_t cr3; }
switch_to:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi], rsp
    mov rsp, [rsi]

    mov rax, [rsi + 8]
    mov rdx, cr3
    cmp rax, rdx
    je .same_pagemap
    test rax, rax
    je .same_pagemap
    mov cr3, rax

.same_pagemap:
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret