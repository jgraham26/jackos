#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "boot/bootinfo.h"

typedef union {
    uint32_t color;
    uint8_t rgba[4];
} color_t;

typedef struct {
    uint8_t* framebuffer;
    uint64_t x;
    uint64_t y;
    color_t fg_color;
    color_t bg_color;
    uint64_t bpp;
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    bool cursor_visible;
    bool cursor_drawn;
} video_info_t;

void init_terminal(const struct boot_info *info);
void putpixel(uint64_t x, uint64_t y, color_t color);
void terminal_clear(void);
uint16_t terminal_get_rows();
uint16_t terminal_get_cols();