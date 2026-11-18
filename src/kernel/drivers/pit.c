#include <stdint.h>
#include "drivers/pit.h"
#include "io.h"

void pit_delay_ms(uint32_t ms) {
    // 1 ms = 1193 PIT ticks
    for (uint32_t i = 0; i < ms; i++) {
        outb(0x43, 0xB0);
        outb(0x42, (uint8_t)(1193 & 0xFF));
        outb(0x42, (uint8_t)((1193 >> 8) & 0xFF));

        uint8_t reg = inb(0x61) & 0xFC;
        outb(0x61, reg);
        outb(0x61, reg | 0x01);

        while ((inb(0x61) & 0x20) == 0);
    }
}