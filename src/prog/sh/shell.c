#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_LINE_LEN 256
#define MAX_ARGS     16
#define MAX_PATH_LEN 256

/* Direct syscall wrappers */
static int sys_chdir_wrapper(const char *path) {
    long ret;
    register long r_num __asm__("rax") = 80;    /* SYS_CHDIR */
    register const char *r_arg1 __asm__("rdi") = path;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "r"(r_num), "r"(r_arg1)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

static char *sys_getcwd_wrapper(char *buf, size_t size) {
    long ret;
    register long r_num __asm__("rax") = 79;    /* SYS_GETCWD */
    register char *r_arg1 __asm__("rdi") = buf;
    register size_t r_arg2 __asm__("rsi") = size;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "r"(r_num), "r"(r_arg1), "r"(r_arg2)
        : "rcx", "r11", "memory"
    );

    if (ret < 0) {
        return NULL;
    }
    return (char *)ret;
}

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static void strcpy(char *dst, const char *src) {
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

static int read_line(char *buf, size_t max_len) {
    size_t i = 0;
    while (i < max_len - 1) {
        char ch;
        int64_t n = read(0, &ch, 1);
        if (n <= 0) {
            continue;
        }

        if (ch == '\b' || ch == 127) {
            if (i > 0) {
                i--;
                write(1, "\b \b", 3);
            }
            continue;
        }

        write(1, &ch, 1);

        if (ch == '\n' || ch == '\r') {
            break;
        }

        buf[i++] = ch;
    }

    buf[i] = '\0';
    return (int)i;
}

static int tokenize(char *line, char *argv[], size_t max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < (int)max_args - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            *p++ = '\0';
        }

        if (*p == '\0') {
            break;
        }

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

int main(void) {
    char line[MAX_LINE_LEN];
    char *argv[MAX_ARGS];
    char exec_path[MAX_LINE_LEN];
    char current_cwd[MAX_PATH_LEN];
    const char *envp[] = { "PATH=/bin", "TERM=xterm", NULL };

    /* Query initial CWD from kernel */
    if (!sys_getcwd_wrapper(current_cwd, sizeof(current_cwd))) {
        strcpy(current_cwd, "/");
    }

    printf("\n========================================\n");
    printf("        WELCOME TO KERNEL SHELL         \n");
    printf("========================================\n\n");

    
    while (1) {
        printf("sh:%s# ", current_cwd);
        fflush(stdout);

        int len = read_line(line, sizeof(line));
        if (len == 0) {
            continue;
        }

        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        /* Built-in: exit */
        if (strcmp(argv[0], "exit") == 0) {
            printf("Exiting shell...\n");
            break;
        }

        /* Built-in: help */
        if (strcmp(argv[0], "help") == 0) {
            printf("Builtin commands:\n");
            printf("  cd [dir] - change current directory\n");
            printf("  pwd      - print current directory\n");
            printf("  help     - show this message\n");
            printf("  exit     - terminate the shell\n");
            continue;
        }

        /* Built-in: pwd */
        if (strcmp(argv[0], "pwd") == 0) {
            char buf[MAX_PATH_LEN];
            if (sys_getcwd_wrapper(buf, sizeof(buf))) {
                printf("%s\n", buf);
            } else {
                printf("%s\n", current_cwd);
            }
            continue;
        }

        /* Built-in: cd */
        if (strcmp(argv[0], "cd") == 0) {
            const char *target = argv[1];
            if (!target) {
                target = "/";
            }

            int ret = sys_chdir_wrapper(target);
            if (ret < 0) {
                printf("cd: %s: No such directory\n", target);
            } else {
                /* Pull the kernel's canonicalized cwd */
                if (!sys_getcwd_wrapper(current_cwd, sizeof(current_cwd))) {
                    strcpy(current_cwd, "/");
                }
            }
            continue;
        }

        /* Fork and execute */
        int64_t pid = fork();
        if (pid < 0) {
            printf("sh: fork failed\n");
            continue;
        }

        if (pid == 0) {
            if (argv[0][0] == '/') {
                execve(argv[0], argv, (char *const *)envp);
            } else {
                execve(argv[0], argv, (char *const *)envp);

                strcpy(exec_path, "/bin/");
                char *dst = exec_path + 5;
                const char *src = argv[0];
                while (*src) {
                    *dst++ = *src++;
                }
                *dst = '\0';

                execve(exec_path, argv, (char *const *)envp);
            }

            printf("sh: command not found: ");
            printf(argv[0]);
            printf("\n");
            exit(127);
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }

    exit(0);
}