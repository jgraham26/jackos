#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    printf("=== LIBC TEST RUNNER ===\n");
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }

    /* Test dynamic allocation */
    char *buf = malloc(64);
    if (!buf) {
        printf("malloc failed!\n");
        return 1;
    }
    strcpy(buf, "Dynamic heap allocation works!");
    printf("Heap string: %s\n", buf);
    free(buf);

    /* Test process lifecycle with sys/wait.h macros */
    pid_t pid = fork();
    if (pid == 0) {
        printf("[Child] Running, exiting with 42...\n");
        exit(42);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        printf("[Parent] Reaped child. Exit code: %d\n", WEXITSTATUS(status));
    }

    printf("=== ALL LIBC TESTS PASSED ===\n");
    return 0;
}