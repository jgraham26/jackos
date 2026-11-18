#include "fs/kbdev.h"
#include "cpu/apic.h"
#include "fs/vfs.h"
#include "sched/spinlock.h"
#include "fs/devfs.h"
#include <stdint.h>
#include <string.h>
#include <errno.h>

extern volatile uint64_t g_ticks;

#define KBD_RING_SIZE 256

static struct kbd_packet s_ring[KBD_RING_SIZE];
static volatile uint32_t s_head = 0;
static volatile uint32_t s_tail = 0;
static bool s_e0_prefix = false;
static spinlock_t s_kbd_lock = {0};

static const uint16_t s_set1_to_linux_key[128] = {
    [0x01] = 1,   /* KEY_ESC */
    [0x02] = 2,   /* KEY_1 */
    [0x03] = 3,   /* KEY_2 */
    [0x04] = 4,   /* KEY_3 */
    [0x05] = 5,   /* KEY_4 */
    [0x06] = 6,   /* KEY_5 */
    [0x07] = 7,   /* KEY_6 */
    [0x08] = 8,   /* KEY_7 */
    [0x09] = 9,   /* KEY_8 */
    [0x0A] = 10,  /* KEY_9 */
    [0x0B] = 11,  /* KEY_0 */
    [0x0C] = 12,  /* KEY_MINUS */
    [0x0D] = 13,  /* KEY_EQUAL */
    [0x0E] = 14,  /* KEY_BACKSPACE */
    [0x0F] = 15,  /* KEY_TAB */
    [0x10] = 16,  /* KEY_Q */
    [0x11] = 17,  /* KEY_W */
    [0x12] = 18,  /* KEY_E */
    [0x13] = 19,  /* KEY_R */
    [0x14] = 20,  /* KEY_T */
    [0x15] = 21,  /* KEY_Y */
    [0x16] = 22,  /* KEY_U */
    [0x17] = 23,  /* KEY_I */
    [0x18] = 24,  /* KEY_O */
    [0x19] = 25,  /* KEY_P */
    [0x1A] = 26,  /* KEY_LEFTBRACE */
    [0x1B] = 27,  /* KEY_RIGHTBRACE */
    [0x1C] = 28,  /* KEY_ENTER */
    [0x1D] = 29,  /* KEY_LEFTCTRL */
    [0x1E] = 30,  /* KEY_A */
    [0x1F] = 31,  /* KEY_S */
    [0x20] = 32,  /* KEY_D */
    [0x21] = 33,  /* KEY_F */
    [0x22] = 34,  /* KEY_G */
    [0x23] = 35,  /* KEY_H */
    [0x24] = 36,  /* KEY_J */
    [0x25] = 37,  /* KEY_K */
    [0x26] = 38,  /* KEY_L */
    [0x27] = 39,  /* KEY_SEMICOLON */
    [0x28] = 40,  /* KEY_APOSTROPHE */
    [0x29] = 41,  /* KEY_GRAVE */
    [0x2A] = 42,  /* KEY_LEFTSHIFT */
    [0x2B] = 43,  /* KEY_BACKSLASH */
    [0x2C] = 44,  /* KEY_Z */
    [0x2D] = 45,  /* KEY_X */
    [0x2E] = 46,  /* KEY_C */
    [0x2F] = 47,  /* KEY_V */
    [0x30] = 48,  /* KEY_B */
    [0x31] = 49,  /* KEY_N */
    [0x32] = 50,  /* KEY_M */
    [0x33] = 51,  /* KEY_COMMA */
    [0x34] = 52,  /* KEY_DOT */
    [0x35] = 53,  /* KEY_SLASH */
    [0x36] = 54,  /* KEY_RIGHTSHIFT */
    [0x38] = 56,  /* KEY_LEFTALT */
    [0x39] = 57,  /* KEY_SPACE */
    [0x3A] = 58,  /* KEY_CAPSLOCK */
    [0x3B] = 59,  /* KEY_F1 */
    [0x3C] = 60,  /* KEY_F2 */
    [0x3D] = 61,  /* KEY_F3 */
    [0x3E] = 62,  /* KEY_F4 */
    [0x3F] = 63,  /* KEY_F5 */
    [0x40] = 64,  /* KEY_F6 */
    [0x41] = 65,  /* KEY_F7 */
    [0x42] = 66,  /* KEY_F8 */
    [0x43] = 67,  /* KEY_F9 */
    [0x44] = 68,  /* KEY_F10 */
    [0x57] = 87,  /* KEY_F11 */
    [0x58] = 88,  /* KEY_F12 */
};

static uint16_t translate_to_linux_keycode(uint8_t scancode, uint8_t extended) {
    if (extended) {
        switch (scancode) {
            case 0x48: return 103; /* KEY_UP */
            case 0x50: return 108; /* KEY_DOWN */
            case 0x4B: return 105; /* KEY_LEFT */
            case 0x4D: return 106; /* KEY_RIGHT */
            case 0x1D: return 97;  /* KEY_RIGHTCTRL */
            case 0x38: return 100; /* KEY_RIGHTALT */
            case 0x1C: return 96;  /* KEY_KPENTER */
            default:   return 0;
        }
    }
    return (scancode < 128) ? s_set1_to_linux_key[scancode] : 0;
}

void kbddev_push_scancode(uint8_t raw_byte) {
    if (raw_byte == 0xE0) {
        s_e0_prefix = true;
        return;
    }
    if (raw_byte == 0xE1) {
        return; /* Ignore Pause/Break prefix */
    }

    uint8_t pressed  = (raw_byte & 0x80) ? 0 : 1;
    uint8_t scancode = raw_byte & 0x7F;
    uint8_t extended = s_e0_prefix ? 1 : 0;
    s_e0_prefix = false;

    uint64_t flags = spinlock_irqsave(&s_kbd_lock);
    uint32_t next_head = (s_head + 1) % KBD_RING_SIZE;
    if (next_head != s_tail) {
        s_ring[s_head] = (struct kbd_packet){
            .pressed  = pressed,
            .scancode = scancode,
            .extended = extended,
            ._pad     = 0,
        };
        s_head = next_head;
    }
    spinlock_irqrestore(&s_kbd_lock, flags);
}

static bool pop_packet(struct kbd_packet *out) {
    uint64_t flags = spinlock_irqsave(&s_kbd_lock);
    if (s_head == s_tail) {
        spinlock_irqrestore(&s_kbd_lock, flags);
        return false;
    }
    *out = s_ring[s_tail];
    s_tail = (s_tail + 1) % KBD_RING_SIZE;
    spinlock_irqrestore(&s_kbd_lock, flags);
    return true;
}

static int64_t kbd_read(vfs_file_t *file, void *buf, size_t count) {
    if (!buf) return -EFAULT;
    if (count < sizeof(struct kbd_packet)) return -EINVAL;

    struct kbd_packet pkt;
    while (!pop_packet(&pkt)) {
        if (file && (file->flags & O_NONBLOCK)) {
            return -EAGAIN;
        }
        __asm__ volatile("sti; hlt");
    }

    size_t written = 0;
    uint8_t *dst = (uint8_t *)buf;
    do {
        memcpy(dst + written, &pkt, sizeof(pkt));
        written += sizeof(pkt);
    } while (written + sizeof(pkt) <= count && pop_packet(&pkt));

    return (int64_t)written;
}

static const vfs_ops_t s_kbd_ops = {
    .read = kbd_read,
};

static int64_t evdev_read(vfs_file_t *file, void *buf, size_t count) {
    const size_t pair_size = 2 * sizeof(struct linux_input_event);
    if (!buf) return -EFAULT;
    if (count < pair_size) return -EINVAL;

    struct kbd_packet pkt;
    while (!pop_packet(&pkt)) {
        if (file && (file->flags & O_NONBLOCK)) {
            return -EAGAIN;
        }
        __asm__ volatile("sti; hlt");
    }

    size_t written = 0;
    uint8_t *dst = (uint8_t *)buf;

    do {
        uint16_t code = translate_to_linux_keycode(pkt.scancode, pkt.extended);
        if (code != 0) {
            uint64_t ms = g_ticks;
            struct linux_input_event evs[2] = {
                {
                    .tv_sec  = (int64_t)(ms / 1000),
                    .tv_usec = (int64_t)((ms % 1000) * 1000),
                    .type    = EV_KEY,
                    .code    = code,
                    .value   = pkt.pressed ? 1 : 0,
                },
                {
                    .tv_sec  = (int64_t)(ms / 1000),
                    .tv_usec = (int64_t)((ms % 1000) * 1000),
                    .type    = EV_SYN,
                    .code    = SYN_REPORT,
                    .value   = 0,
                },
            };
            memcpy(dst + written, evs, pair_size);
            written += pair_size;
        }
    } while (written + pair_size <= count && pop_packet(&pkt));

    if (written == 0) {
        return (file && (file->flags & O_NONBLOCK)) ? -EAGAIN : 0;
    }
    return (int64_t)written;
}


static int64_t evdev_ioctl(vfs_file_t *file, uint64_t request, void* arg) {
    (void)file;
    if (!arg) return -EFAULT;

    uint32_t req32 = (uint32_t)request;
    uint32_t base  = req32 & ~0x3FFF0000U; /* strip _IOC_SIZE bits */
    size_t   len   = (req32 >> 16) & 0x3FFFU;

    if (req32 == EVIOCGVERSION) {
        *(int *)arg = 0x010001; /* EV_VERSION 1.0.1 */
        return 0;
    }
    if (req32 == EVIOCGID) {
        uint16_t id[4] = { 0x0011 /* BUS_I8042 */, 0x0001, 0x0001, 0x0100 };
        memcpy((void *)arg, id, sizeof(id));
        return 0;
    }
    if (base == EVIOCGNAME(0)) {
        const char *name = "JackOS PS/2 Keyboard";
        size_t n = strlen(name) + 1;
        if (n > len) n = len;
        memcpy((void *)arg, name, n);
        return (int)n;
    }
    if (base == EVIOCGBIT(0, 0)) {
        /* Report EV_SYN (bit 0) and EV_KEY (bit 1) supported */
        memset((void *)arg, 0, len);
        if (len > 0) ((uint8_t *)arg)[0] = (1 << EV_SYN) | (1 << EV_KEY);
        return (int)len;
    }
    if (base == EVIOCGBIT(EV_KEY, 0)) {
        /* Report all standard keyboard keys (0..127) supported */
        memset((void *)arg, 0xFF, len < 16 ? len : 16);
        return (int)len;
    }

    return -EINVAL;
}

static const vfs_ops_t s_evdev_ops = {
    .read  = evdev_read,
    .ioctl = evdev_ioctl,
};

void kbddev_init(void) {
    devfs_register_device("kbd", &s_kbd_ops, NULL);
    /* Register under /dev/input/event0 (supports slash paths if devfs allows, or /dev/event0) */
    devfs_register_device("input/event0", &s_evdev_ops, NULL);
    devfs_register_device("event0", &s_evdev_ops, NULL);
}