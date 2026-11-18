#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define KBD_BUFFER_SIZE 256

typedef struct {
    char buffer[KBD_BUFFER_SIZE];
    size_t head;
    size_t tail;
} kbd_ring_buffer_t;

void kbd_init(void);
void kbd_irq_handler(void);
int kbd_getchar_nonblock(void);
char kbd_getchar(void);
bool kbd_has_data(void);

extern int32_t g_console_fg_pgid;