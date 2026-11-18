/* libc/setjmp/sigsetjmp.c */
#include <setjmp.h>

int sigsetjmp(sigjmp_buf env, int savesigs) {
    env[0].__mask_was_saved = 0;
    (void)savesigs;
    return setjmp(env[0].__jmpbuf);
}

void siglongjmp(sigjmp_buf env, int val) {
    longjmp(env[0].__jmpbuf, val);
}