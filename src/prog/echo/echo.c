#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int newline = 1;
    int start_idx = 1;

    /* Handle -n flag */
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        start_idx = 2;
    }

    for (int i = start_idx; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            putchar(' ');
        }
    }

    if (newline) {
        putchar('\n');
    }

    return 0;
}