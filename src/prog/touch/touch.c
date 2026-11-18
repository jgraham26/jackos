#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file...>\n", argv[0]);
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_CREAT | O_RDWR, 0644);
        if (fd < 0) {
            printf("%s: cannot touch '%s'\n", argv[0], argv[i]);
            status = 1;
            continue;
        }

        close(fd);
    }

    return status;
}