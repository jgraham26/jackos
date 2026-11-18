#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define CHUNK_SIZE 512

static void dump_fd(int fd) {
    char buf[CHUNK_SIZE];
    int64_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, (size_t)n);
    }
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        /* Read from stdin */
        dump_fd(STDIN_FILENO);
        return 0;
    }

    int exit_code = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: No such file or directory\n", argv[i]);
            exit_code = 1;
            continue;
        }

        dump_fd(fd);
        close(fd);
    }

    return exit_code;
}