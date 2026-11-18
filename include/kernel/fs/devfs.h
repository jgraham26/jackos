#pragma once

#include <stdint.h>
#include <stddef.h>
#include "fs/vfs.h"

typedef struct devfs_node {
    char name[32];
    vfs_node_t vfs_node;
    struct devfs_node *next;
} devfs_node_t;

void devfs_init();
vfs_node_t *devfs_get_root(void);
int  devfs_register_device(const char *name, const vfs_ops_t *ops, void *private_data);
int devfs_register_bdev(block_device_t *bdev);
int devfs_register_input_device(const char *name, const vfs_ops_t *ops, void *private_data);

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define ISIG        0000001
#define ICANON      0000002
#define ECHO        0000010
#define ECHOE       0000020
#define ECHOK       0000040

#define ICRNL   0000400

#define OPOST   0000001
#define ONLCR   0000004

#define NCCS 19
struct termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[NCCS];
    unsigned int c_ispeed;
    unsigned int c_ospeed;
};