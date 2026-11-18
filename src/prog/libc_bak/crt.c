#include <stdlib.h>

extern char** environ;

void __libc_start_main(int (*main_func)(int, char**), int argc, char** argv, char** envp) {
    environ = envp;

    int exit_code = main_func(argc, argv);

    exit(exit_code);
}