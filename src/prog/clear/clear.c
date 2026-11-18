#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Write clear sequence to stdout */
    const char *seq = "\033[2J\033[H";
    write(1, seq, 7);
    return 0;
}