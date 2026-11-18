#include "drivers/ps2_kbd.h"
#include "io.h"
#include "sched/spinlock.h"
#include "sched/sched.h"
#include "sched/process.h"
#include "cpu/apic.h"
#include "cpu/signal.h"
#include "drivers/serial.h"
#include <stdio.h>

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

#define KBD_STATUS_OUTPUT_FULL 0x01

static kbd_ring_buffer_t s_kbd_buf = {0};
static spinlock_t s_kbd_lock = SPINLOCK_INIT;

static bool s_shift = false;
static bool s_capslock = false;
static bool s_extended = false;
static bool s_ctrl = false; /* Tracks Ctrl state */

int32_t g_console_fg_pgid = 0;
extern process_t *g_process_list_head;

/* Scan Code Set 1 (US QWERTY) */
static const char kbd_scancode_std[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, /* Left Ctrl (0x1D) */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, /* Left Shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, /* Right Shift */
    '*',
    0, /* Left Alt */
    ' ',
    0, /* Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0, /* Num Lock */
    0, /* Scroll Lock */
    0, 0, 0, /* Home, Up, Page Up */
    '-',
    0, 0, 0, /* Left, Center, Right */
    '+',
    0, 0, 0, /* End, Down, Page Down */
    0, /* Insert */
    0, /* Delete */
    0, 0, 0,
    0, 0, /* F11, F12 */
};

static const char kbd_scancode_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, /* Left Ctrl */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, /* Left Shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, /* Right Shift */
    '*',
    0, /* Left Alt */
    ' ',
    0, /* Caps Lock */
};

static void ring_buffer_push(char c) {
    size_t next = (s_kbd_buf.head + 1) % KBD_BUFFER_SIZE;
    if (next != s_kbd_buf.tail) {
        s_kbd_buf.buffer[s_kbd_buf.head] = c;
        s_kbd_buf.head = next;
    }
}

int kbd_getchar_nonblock(void) {
    uint64_t flags = spinlock_irqsave(&s_kbd_lock);

    if (s_kbd_buf.head == s_kbd_buf.tail) {
        spinlock_irqrestore(&s_kbd_lock, flags);
        return -1;
    }

    char ch = s_kbd_buf.buffer[s_kbd_buf.tail];
    s_kbd_buf.tail = (s_kbd_buf.tail + 1) % KBD_BUFFER_SIZE;

    spinlock_irqrestore(&s_kbd_lock, flags);
    return (int)(unsigned char)ch;
}

char kbd_getchar(void) {
    int ch;
    while ((ch = kbd_getchar_nonblock()) == -1) {
        sched_yield();
    }
    return (char)ch;
}

static void kbd_wait_write(void) {
    while (inb(KBD_STATUS_PORT) & 0x02) {
        io_wait();
    }
}

static void kbd_wait_read(void) {
    while (!(inb(KBD_STATUS_PORT) & KBD_STATUS_OUTPUT_FULL)) {
        io_wait();
    }
}

void kbd_init(void) {
    while (inb(KBD_STATUS_PORT) & KBD_STATUS_OUTPUT_FULL) {
        inb(KBD_DATA_PORT);
    }

    kbd_wait_write();
    outb(KBD_STATUS_PORT, 0xAE);

    kbd_wait_write();
    outb(KBD_STATUS_PORT, 0x20);
    kbd_wait_read();
    uint8_t config = inb(KBD_DATA_PORT);

    config |= 0x01;  /* Enable Port 1 interrupt */
    config &= ~0x10; /* Enable Port 1 clock line */

    kbd_wait_write();
    outb(KBD_STATUS_PORT, 0x60);
    kbd_wait_write();
    outb(KBD_DATA_PORT, config);

    ioapic_route_irq(1, 33, 0);
}

void kbd_irq_handler(void) {
    uint8_t status = inb(KBD_STATUS_PORT);
    if (!(status & KBD_STATUS_OUTPUT_FULL)) {
        return;
    }

    uint8_t scancode = inb(KBD_DATA_PORT);

    if (scancode == 0xE0) {
        s_extended = true;
        return;
    }

    /* Key Release (Break Code: bit 7 set) */
    if (scancode & 0x80) {
        uint8_t released = scancode & ~0x80;
        if (released == 0x2A || released == 0x36) {
            s_shift = false;
        } else if (released == 0x1D) { /* Left or Right (extended) Ctrl */
            s_ctrl = false;
        }
        s_extended = false;
        return;
    }

    /* Key Press (Make Code) */
    if (scancode == 0x1D) { /* Ctrl pressed */
        s_ctrl = true;
        s_extended = false;
        return;
    }

    if (s_extended) {
        s_extended = false;

        const char* seq = NULL;
        switch (scancode) {
            case 0x48: seq = "\x1b[A"; break; /* Up Arrow */
            case 0x50: seq = "\x1b[B"; break; /* Down Arrow */
            case 0x4D: seq = "\x1b[C"; break; /* Right Arrow */
            case 0x4B: seq = "\x1b[D"; break; /* Left Arrow */
            case 0x47: seq = "\x1b[H"; break; /* Home */
            case 0x4F: seq = "\x1b[F"; break; /* End */
            case 0x53: seq = "\x1b[3~"; break; /* Delete */
            default: break;
        }

        if (seq) {
            uint64_t flags = spinlock_irqsave(&s_kbd_lock);
            for (const char* p = seq; *p; p++) {
                ring_buffer_push(*p);
            }
            spinlock_irqrestore(&s_kbd_lock, flags);
        }
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        s_shift = true;
        return;
    }

    if (scancode == 0x3A) {
        s_capslock = !s_capslock;
        return;
    }

    if (scancode < 128) {
        char base = kbd_scancode_std[scancode];

        /* Check for Ctrl + C (0x2E is 'c' make code) */
        if (s_ctrl && (base == 'c' || base == 'C')) {
            putchar('^');
            putchar('C');
            putchar('\n');

            /* 1. If console has a foreground PGID > 1 (set via TIOCSPGRP), signal that group */
            bool sent = false;
            if (g_console_fg_pgid > 1) {
                for (process_t *p = g_process_list_head; p; p = p->global_next) {
                    if (p->pgid == (uint64_t)g_console_fg_pgid && p->state != PROC_ZOMBIE) {
                        kernel_send_signal(p, SIGINT);
                        sent = true;
                    }
                }
            }

            /* 2. Fallback (when job control is off): signal the newest active child (highest PID > 2) */
            if (!sent) {
                process_t *newest = NULL;
                for (process_t *p = g_process_list_head; p; p = p->global_next) {
                    if (p->state != PROC_ZOMBIE && p->pid > 2) {
                        if (!newest || p->pid > newest->pid) {
                            newest = p;
                        }
                    }
                }
                if (newest) {
                    kernel_send_signal(newest, SIGINT);
                }
            }
            return;
        }

        bool use_shift = s_shift;
        if (base >= 'a' && base <= 'z') {
            if (s_capslock) use_shift = !use_shift;
        }

        char ch = use_shift ? kbd_scancode_shift[scancode] : kbd_scancode_std[scancode];

        if (ch) {
            uint64_t flags = spinlock_irqsave(&s_kbd_lock);
            ring_buffer_push(ch);
            spinlock_irqrestore(&s_kbd_lock, flags);
        }
    }
}

bool kbd_has_data(void) {
    uint64_t flags = spinlock_irqsave(&s_kbd_lock);
    bool has_data = (s_kbd_buf.head != s_kbd_buf.tail);
    spinlock_irqrestore(&s_kbd_lock, flags);
    return has_data;
}