[bits 64]

section .data
align 8
global g_syscall_user_rsp
global g_syscall_kernel_rsp
g_syscall_user_rsp:   dq 0
g_syscall_kernel_rsp: dq 0

section .text
global syscall_entry
global sched_switch_to_frame
extern syscall_entry_c

syscall_entry:
    swapgs
    mov [gs:0x8], rsp
    mov rsp, [gs:0x0]

    push qword [gs:0x8]         ; +120: rsp
    push r11                    ; +112: rflags
    push rcx                    ; +104: rip
    push rbp                    ; +96:  rbp
    push rbx                    ; +88:  rbx
    push r15                    ; +80:  r15
    push r14                    ; +72:  r14
    push r13                    ; +64:  r13
    push r12                    ; +56:  r12
    push r10                    ; +48:  r10
    push r9                     ; +40:  r9
    push r8                     ; +32:  r8
    push rdi                    ; +24:  rdi
    push rsi                    ; +16:  rsi
    push rdx                    ; +8:   rdx
    push rax                    ; +0:   rax

    mov rdi, rsp
    call syscall_entry_c

    mov [rsp], rax
    pop rax
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbx
    pop rbp
    cli
    pop rcx
    pop r11
    pop rsp
    swapgs
    o64 sysret

sched_switch_to_frame:
    mov rsp, rdi
    test qword [rsp + 0x90], 3
    je .skip_swapgs
    swapgs

.skip_swapgs:
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