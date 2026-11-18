#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#define BUF_SIZE 1024

static void list_dir(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot open '%s'\n", path);
        return;
    }

    char buf[BUF_SIZE];
    while (1) {
        int64_t nread = getdents64(fd, buf, sizeof(buf));
        if (nread <= 0) {
            break;
        }

        for (int64_t pos = 0; pos < nread;) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + pos);

            /* Filter out current and parent directory pointers if preferred */
            if (d->d_type == DT_DIR) {
                printf("%s/  ", d->d_name);
            } else {
                printf("%s  ", d->d_name);
            }

            pos += d->d_reclen;
        }
    }

    putchar('\n');
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        list_dir(".");
    } else {
        for (int i = 1; i < argc; i++) {
            if (argc > 2) {
                printf("%s:\n", argv[i]);
            }
            list_dir(argv[i]);
            if (i < argc - 1) {
                putchar('\n');
            }
        }
    }
    return 0;
}