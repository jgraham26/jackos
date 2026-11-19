#include "drivers/terminal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "drivers/klog.h"
#include "boot/multiboot2.h"
#include "boot/bootinfo.h"
#include "kernel.h"
#include "mem/vmm.h"
#include "drivers/font.h"

#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static video_info_t video_info;

/* Stateful ANSI CSI parser state */
typedef enum {
    ANSI_NORMAL = 0,
    ANSI_ESC,
    ANSI_CSI
} ansi_state_t;

static ansi_state_t s_ansi_state = ANSI_NORMAL;
static int s_csi_params[8];
static int s_csi_param_count = 0;
static int s_csi_current_param = 0;
static bool s_csi_has_digit = false;

struct multiboot_tag_framebuffer* find_framebuffer_tag(uintptr_t mbi_addr) {
    uint8_t* ptr = (uint8_t*)mbi_addr;

    /* Skip the 8-byte MBI header (total_size + reserved) */
    ptr += 8;

    while (1) {
        struct multiboot_tag* tag = (struct multiboot_tag*)ptr;

        if (tag->type == MULTIBOOT_TAG_TYPE_END) {
            break;
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            return (struct multiboot_tag_framebuffer*)tag;
        }

        /* Multiboot2 tags are padded to 8-byte boundaries */
        ptr += ((tag->size + 7) & ~7);
    }

    return NULL;
}

void putpixel(uint64_t x, uint64_t y, color_t color) {
    if (x >= video_info.width || y >= video_info.height) {
        return;
    }

    uint8_t* pixel_addr = video_info.framebuffer + (y * video_info.pitch);

    switch (video_info.bpp) {
    case 32: {
        uint32_t* pixel = (uint32_t*)(pixel_addr + (x * 4));
        *pixel = color.color;
        break;
    }
    case 24: {
        uint8_t* pixel = pixel_addr + (x * 3);
        pixel[0] = color.rgba[0]; // blue
        pixel[1] = color.rgba[1]; // green
        pixel[2] = color.rgba[2]; // red
        break;
    }
    case 16: {
        uint16_t *pixel = (uint16_t *)(pixel_addr + (x * 2));
        uint16_t r = (color.rgba[2] >> 3) & 0x1F;
        uint16_t g = (color.rgba[1] >> 2) & 0x3F;
        uint16_t b = (color.rgba[0] >> 3) & 0x1F;
        *pixel = (uint16_t)((r << 11) | (g << 5) | b);
        break;
    }
    default:
        break;
    }
}

/* Invert pixels in the cursor cell so underlying glyphs remain intact when moving */
static void cursor_toggle_pixels(void) {
    if (!video_info.framebuffer) return;

    uint64_t cx = video_info.x;
    uint64_t cy = video_info.y;

    /* Clamp cursor inside visible screen bounds */
    uint64_t max_x = (video_info.width / FONT_WIDTH) * FONT_WIDTH;
    uint64_t max_y = (video_info.height / FONT_HEIGHT) * FONT_HEIGHT;
    if (cx >= max_x) cx = (max_x >= FONT_WIDTH) ? (max_x - FONT_WIDTH) : 0;
    if (cy >= max_y) cy = (max_y >= FONT_HEIGHT) ? (max_y - FONT_HEIGHT) : 0;

    for (uint64_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t *line = video_info.framebuffer + ((cy + row) * video_info.pitch);
        for (uint64_t col = 0; col < FONT_WIDTH; col++) {
            uint64_t px = cx + col;
            if (px >= video_info.width || (cy + row) >= video_info.height) continue;

            switch (video_info.bpp) {
                case 32: {
                    uint32_t *p = (uint32_t *)(line + px * 4);
                    *p ^= 0x00FFFFFF;
                    break;
                }
                case 24: {
                    uint8_t *p = line + px * 3;
                    p[0] ^= 0xFF;
                    p[1] ^= 0xFF;
                    p[2] ^= 0xFF;
                    break;
                }
                case 16: {
                    uint16_t *p = (uint16_t *)(line + px * 2);
                    *p ^= 0xFFFF;
                    break;
                }
                default:
                    break;
            }
        }
    }
}

static void cursor_hide(void) {
    if (video_info.cursor_drawn) {
        cursor_toggle_pixels();
        video_info.cursor_drawn = false;
    }
}

static void cursor_show(void) {
    if (video_info.cursor_visible && !video_info.cursor_drawn) {
        cursor_toggle_pixels();
        video_info.cursor_drawn = true;
    }
}

void init_terminal(const struct boot_info *info) {
    if (!info || !info->fb.is_present) {
        klog_printf("Error: Framebuffer not present in boot_info\n");
        hcf();
    }

    const struct boot_framebuffer *fb = &info->fb;

    /* Calculate total size and page boundaries */
    uint64_t total_size  = (uint64_t)fb->pitch * fb->height;
    uintptr_t start_phys = (uintptr_t)PAGE_ALIGN_DOWN(fb->addr);
    uintptr_t end_phys   = (uintptr_t)PAGE_ALIGN_UP(fb->addr + total_size);

    /* Explicitly map the framebuffer into the higher-half direct map */
    for (uintptr_t p = start_phys; p < end_phys; p += PAGE_SIZE) {
        vmm_map_page(&kernel_pagemap, hhdm_physical_to_virtual(p), p, PTE_PRESENT | PTE_WRITABLE);
    }

    video_info.framebuffer    = (uint8_t *)hhdm_physical_to_virtual(fb->addr);
    video_info.width          = fb->width;
    video_info.height         = fb->height;
    video_info.pitch          = fb->pitch;
    video_info.bpp            = fb->bpp;

    /* Cursor & default colors */
    video_info.x              = 0;
    video_info.y              = 0;
    video_info.fg_color.color = 0x00FFFFFF;
    video_info.bg_color.color = 0x00000000;
    video_info.cursor_visible = true;
    video_info.cursor_drawn   = false;

    cursor_show();
}

void draw_char_at(char c, uint64_t px, uint64_t py, color_t fg, color_t bg) {
    uint8_t uc = (uint8_t)c;
    if (uc > 127) {
        uc = 0; /* Fall back to null glyph for non-ASCII */
    }

    const unsigned char *glyph = font8x8_basic[uc];

    for (uint64_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t row_bits = glyph[row];

        for (uint64_t col = 0; col < FONT_WIDTH; col++) {
            /* Bit 0 is the leftmost pixel */
            bool pixel_on = (row_bits >> col) & 1;
            putpixel(px + col, py + row, pixel_on ? fg : bg);
        }
    }
}

static void clear_rect(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1) {
    if (x1 > video_info.width)  x1 = video_info.width;
    if (y1 > video_info.height) y1 = video_info.height;
    for (uint64_t y = y0; y < y1; y++) {
        for (uint64_t x = x0; x < x1; x++) {
            putpixel(x, y, video_info.bg_color);
        }
    }
}

static void terminal_scroll(void) {
    uint64_t usable_height = video_info.height - (video_info.height % FONT_HEIGHT);
    uint64_t line_bytes = video_info.pitch * FONT_HEIGHT;
    uint64_t total_display_bytes = video_info.pitch * usable_height;

    /* Move all screen scanlines up by FONT_HEIGHT */
    memmove(video_info.framebuffer,
            video_info.framebuffer + line_bytes,
            (size_t)(total_display_bytes - line_bytes));

    /* Clear the vacated bottom line */
    uint64_t bottom_start_y = usable_height - FONT_HEIGHT;
    clear_rect(0, bottom_start_y, video_info.width, bottom_start_y + FONT_HEIGHT);
}

static void terminal_newline(void) {
    video_info.x = 0;
    video_info.y += FONT_HEIGHT;

    if (video_info.y + FONT_HEIGHT > video_info.height) {
        terminal_scroll();
        video_info.y -= FONT_HEIGHT;
    }
}

static void terminal_backspace(void) {
    if (video_info.x >= FONT_WIDTH) {
        video_info.x -= FONT_WIDTH;
    } else if (video_info.y >= FONT_HEIGHT) {
        video_info.y -= FONT_HEIGHT;
        video_info.x = (video_info.width / FONT_WIDTH) * FONT_WIDTH - FONT_WIDTH;
    } else {
        return;
    }

    draw_char_at(' ', video_info.x, video_info.y, video_info.fg_color, video_info.bg_color);
}

void terminal_clear(void) {
    if (!video_info.framebuffer) return;

    cursor_hide();

    if (video_info.bg_color.color == 0) {
        memset(video_info.framebuffer, 0, (size_t)(video_info.pitch * video_info.height));
    } else {
        clear_rect(0, 0, video_info.width, video_info.height);
    }

    video_info.x = 0;
    video_info.y = 0;
    cursor_show();
}

static void ansi_dispatch_csi(char final_char) {
    uint64_t cols = video_info.width / FONT_WIDTH;
    uint64_t rows = video_info.height / FONT_HEIGHT;
    uint64_t cur_col = video_info.x / FONT_WIDTH;
    uint64_t cur_row = video_info.y / FONT_HEIGHT;

    int p0 = (s_csi_param_count > 0) ? s_csi_params[0] : 0;
    int p1 = (s_csi_param_count > 1) ? s_csi_params[1] : 0;

    switch (final_char) {
        case 'A': { /* Cursor Up */
            uint64_t n = (p0 > 0) ? (uint64_t)p0 : 1;
            cur_row = (cur_row >= n) ? (cur_row - n) : 0;
            video_info.y = cur_row * FONT_HEIGHT;
            break;
        }
        case 'B': { /* Cursor Down */
            uint64_t n = (p0 > 0) ? (uint64_t)p0 : 1;
            cur_row = (cur_row + n < rows) ? (cur_row + n) : (rows - 1);
            video_info.y = cur_row * FONT_HEIGHT;
            break;
        }
        case 'C': { /* Cursor Forward (Right) */
            uint64_t n = (p0 > 0) ? (uint64_t)p0 : 1;
            cur_col = (cur_col + n < cols) ? (cur_col + n) : (cols - 1);
            video_info.x = cur_col * FONT_WIDTH;
            break;
        }
        case 'D': { /* Cursor Backward (Left) */
            uint64_t n = (p0 > 0) ? (uint64_t)p0 : 1;
            cur_col = (cur_col >= n) ? (cur_col - n) : 0;
            video_info.x = cur_col * FONT_WIDTH;
            break;
        }
        case 'G': { /* Cursor Horizontal Absolute (1-based) */
            uint64_t target = (p0 > 0) ? (uint64_t)(p0 - 1) : 0;
            if (target >= cols) target = cols - 1;
            video_info.x = target * FONT_WIDTH;
            break;
        }
        case 'H':
        case 'f': { /* Cursor Position row;col (1-based) */
            uint64_t r = (p0 > 0) ? (uint64_t)(p0 - 1) : 0;
            uint64_t c = (p1 > 0) ? (uint64_t)(p1 - 1) : 0;
            if (r >= rows) r = rows - 1;
            if (c >= cols) c = cols - 1;
            video_info.y = r * FONT_HEIGHT;
            video_info.x = c * FONT_WIDTH;
            break;
        }
        case 'J': { /* Erase in Display */
            if (p0 == 2 || p0 == 3) {
                terminal_clear();
            } else if (p0 == 0) {
                clear_rect(video_info.x, video_info.y, video_info.width, video_info.y + FONT_HEIGHT);
                clear_rect(0, video_info.y + FONT_HEIGHT, video_info.width, video_info.height);
            } else if (p0 == 1) {
                clear_rect(0, 0, video_info.width, video_info.y);
                clear_rect(0, video_info.y, video_info.x + FONT_WIDTH, video_info.y + FONT_HEIGHT);
            }
            break;
        }
        case 'K': { /* Erase in Line (used heavily by ash lineedit) */
            if (p0 == 0) {
                clear_rect(video_info.x, video_info.y, video_info.width, video_info.y + FONT_HEIGHT);
            } else if (p0 == 1) {
                clear_rect(0, video_info.y, video_info.x + FONT_WIDTH, video_info.y + FONT_HEIGHT);
            } else if (p0 == 2) {
                clear_rect(0, video_info.y, video_info.width, video_info.y + FONT_HEIGHT);
            }
            break;
        }
        case 'm': { /* SGR Colors & Reset */
            static const uint32_t ansi_palette[8] = {
                0x00000000, /* Black */
                0x00AA0000, /* Red */
                0x0000AA00, /* Green */
                0x00AA5500, /* Yellow/Brown */
                0x000000AA, /* Blue */
                0x00AA00AA, /* Magenta */
                0x0000AAAA, /* Cyan */
                0x00AAAAAA, /* Light Gray */
            };
            if (s_csi_param_count == 0) {
                video_info.fg_color.color = 0x00FFFFFF;
                video_info.bg_color.color = 0x00000000;
            }
            for (int i = 0; i < s_csi_param_count; i++) {
                int code = s_csi_params[i];
                if (code == 0) {
                    video_info.fg_color.color = 0x00FFFFFF;
                    video_info.bg_color.color = 0x00000000;
                } else if (code >= 30 && code <= 37) {
                    video_info.fg_color.color = ansi_palette[code - 30];
                } else if (code == 39) {
                    video_info.fg_color.color = 0x00FFFFFF;
                } else if (code >= 40 && code <= 47) {
                    video_info.bg_color.color = ansi_palette[code - 40];
                } else if (code == 49) {
                    video_info.bg_color.color = 0x00000000;
                } else if (code >= 90 && code <= 97) {
                    video_info.fg_color.color = ansi_palette[code - 90] | 0x00555555;
                }
            }
            break;
        }
        default:
            break;
    }
}

void putchar(char c) {
    if (c == '\0') return;

    cursor_hide();

    /* 1. Handle ANSI Escape Sequence State Machine */
    if (s_ansi_state == ANSI_ESC) {
        if (c == '[') {
            s_ansi_state = ANSI_CSI;
            s_csi_param_count = 0;
            s_csi_current_param = 0;
            s_csi_has_digit = false;
            memset(s_csi_params, 0, sizeof(s_csi_params));
        } else {
            s_ansi_state = ANSI_NORMAL;
        }
        cursor_show();
        return;
    }

    if (s_ansi_state == ANSI_CSI) {
        if (c >= '0' && c <= '9') {
            s_csi_current_param = s_csi_current_param * 10 + (c - '0');
            s_csi_has_digit = true;
            cursor_show();
            return;
        }
        if (c == ';') {
            if (s_csi_param_count < 8) {
                s_csi_params[s_csi_param_count++] = s_csi_has_digit ? s_csi_current_param : 0;
            }
            s_csi_current_param = 0;
            s_csi_has_digit = false;
            cursor_show();
            return;
        }
        if (c == '?' || c == '>') {
            /* Private mode prefix (e.g. \x1b[?25l) - ignore prefix and continue parsing */
            cursor_show();
            return;
        }
        if (c >= 0x40 && c <= 0x7E) {
            if (s_csi_has_digit && s_csi_param_count < 8) {
                s_csi_params[s_csi_param_count++] = s_csi_current_param;
            }
            ansi_dispatch_csi(c);
            s_ansi_state = ANSI_NORMAL;
            cursor_show();
            return;
        }
        /* Unexpected control byte aborts CSI */
        s_ansi_state = ANSI_NORMAL;
    }

    if (c == '\033') {
        s_ansi_state = ANSI_ESC;
        cursor_show();
        return;
    }

    /* 2. Normal Control Characters */
    if (c == '\b' || c == 127) {
        terminal_backspace();
        cursor_show();
        return;
    }

    if (c == '\n') {
        terminal_newline();
        cursor_show();
        return;
    }

    if (c == '\r') {
        video_info.x = 0;
        cursor_show();
        return;
    }

    if (c == '\t') {
        uint64_t tab_width = FONT_WIDTH * 4;
        uint64_t next_x = ((video_info.x / tab_width) + 1) * tab_width;

        while (video_info.x < next_x && video_info.x + FONT_WIDTH <= video_info.width) {
            draw_char_at(' ', video_info.x, video_info.y, video_info.fg_color, video_info.bg_color);
            video_info.x += FONT_WIDTH;
        }

        if (video_info.x + FONT_WIDTH > video_info.width) {
            terminal_newline();
        }
        cursor_show();
        return;
    }

    /* 3. Printable ASCII Glyph */
    if (video_info.x + FONT_WIDTH > video_info.width) {
        terminal_newline();
    }

    draw_char_at(c, video_info.x, video_info.y, video_info.fg_color, video_info.bg_color);
    video_info.x += FONT_WIDTH;

    cursor_show();
}

void puts(const char *str) {
    if (!str) return;

    while (*str) {
        putchar(*str++);
    }
}

uint16_t terminal_get_cols() {
    return (uint16_t)(video_info.width / FONT_WIDTH);
}

uint16_t terminal_get_rows() {
    return (uint16_t)(video_info.height / FONT_HEIGHT);
}