[bits 64]

section .text
global load_idtr
extern interrupt_dispatcher

load_idtr:
    lidt [rdi]
    ret

%macro ISR_NOERR 1
global isr_%1
isr_%1:
    push 0
    push %1
    jmp common_isr_stub
%endmacro

%macro ISR_ERR 1
global isr_%1
isr_%1:
    push %1
    jmp common_isr_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

global isr_255
isr_255:
    push 0
    push dword 0xff
    jmp common_isr_stub

%assign i 32
%rep 16
    ISR_NOERR i
%assign i i+1
%endrep

common_isr_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    test qword [rsp + 0x90], 3
    je .skip_swapgs_entry
    swapgs

.skip_swapgs_entry:
    mov rdi, rsp
    cld
    call interrupt_dispatcher
    mov rsp, rax

    test qword [rsp + 0x90], 3
    je .skip_swapgs_exit
    swapgs

.skip_swapgs_exit:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 0x10
    iretq