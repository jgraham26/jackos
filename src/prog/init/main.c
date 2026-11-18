#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

static void delay(volatile uint64_t count) {
    while (count--) {
        __asm__ volatile("");
    }
}
int main(void) {
    puts("[INIT] Testing sys_execve...\n");

    int64_t pid = fork();

    if (pid == 0) {
        puts("[CHILD] Replacing process image with /bin/sh...\n");
        char *argv[] = { "-sh", "-i", NULL };
        char *envp[] = { "PATH=/bin:/usr/bin", "TERM=vt100", "HOME=/", NULL };

        execve("/usr/bin/busybox", argv, envp);

        /* Only reached if execve failed */
        puts("[CHILD] execve failed!\n");
        exit(127);
    } else {
        int status = 0;
        waitpid(pid, &status, 0);
        puts("[PARENT] Child finished with status: ");
        printf("%d\n", WEXITSTATUS(status));
        puts("\n");
    }

    while (1);
}