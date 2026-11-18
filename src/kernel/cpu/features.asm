global fpu_init_cpu

fpu_init_cpu:
    mov rax, cr0
    and rax, 0xfffffffffffffffb     ; Clear EM (bit 2)
    or  rax, 0x2                    ; Set MP (bit 1)
    and rax, 0xfffffffffffffff7     ; Clear TS (bit 3)
    mov cr0, rax

    mov rax, cr4
    or  rax, 0x600                  ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, rax

    fninit
    sub rsp, 8
    mov dword [rsp], 0x1f80         ; Default MXCSR mask (all exceptions masked)
    ldmxcsr [rsp]
    add rsp, 8
    ret