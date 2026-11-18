#pragma once

#include <stdint.h>

struct linux_input_event {
    int64_t tv_sec;
    int64_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define EV_SYN       0x00
#define EV_KEY       0x01
#define SYN_REPORT   0

#define EVIOCGVERSION 0x80044501
#define EVIOCGID      0x80084502
#define EVIOCGNAME(len) (0x80004506 | ((len) << 16))
#define EVIOCGBIT(ev, len) (0x80004520 | ((len) << 16) | ((ev) << 0))

struct kbd_packet {
    uint8_t pressed;  /* 1 = make (down), 0 = break (up) */
    uint8_t scancode; /* PS/2 Set 1 make code (0x01 - 0x7F) */
    uint8_t extended; /* 1 if prefixed with 0xE0 (arrows, right ctrl/alt) */
    uint8_t _pad;
};

void kbddev_init(void);
void kbddev_push_scancode(uint8_t raw_byte);