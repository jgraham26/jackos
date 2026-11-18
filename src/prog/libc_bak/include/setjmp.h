#ifndef _SETSETJMP_H
#define _SETJMP_H 1

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Buffer layout for x86_64 System V ABI:
 * [0] rbx
 * [1] rbp
 * [2] r12
 * [3] r13
 * [4] r14
 * [5] r15
 * [6] rsp (stack pointer at call site)
 * [7] rip (return address)
 * [8..] optional space for signal mask if sigsetjmp is used
 */
typedef long jmp_buf[8];

/* POSIX signal variants */
typedef struct {
    jmp_buf __jmpbuf;
    int     __mask_was_saved;
    unsigned long __saved_mask;
} sigjmp_buf[1];

/* Standard declarations */
int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((__noreturn__));

/* POSIX declarations */
int  sigsetjmp(sigjmp_buf env, int savesigs);
void siglongjmp(sigjmp_buf env, int val) __attribute__((__noreturn__));

#define _setjmp(env)       setjmp(env)
#define _longjmp(env, val) longjmp(env, val)

#ifdef __cplusplus
}
#endif

#endif /* _SETJMP_H */