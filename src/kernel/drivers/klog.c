#include "drivers/klog.h"
#include "drivers/serial.h"
#include "sched/spinlock.h"
#include "fs/vfs.h"
#include "fs/devfs.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define KLOG_BUF_SIZE (64 * 1024) /* Must be a power of 2 */

extern volatile uint64_t g_ticks;

static char       s_klog_buf[KLOG_BUF_SIZE];
static uint64_t   s_write_pos  = 0; /* Total bytes ever written */
static uint64_t   s_oldest_pos = 0; /* Oldest byte still retained in buffer */
static uint64_t s_consumed_pos = 0;
static bool       s_at_line_start = true;
static spinlock_t s_klog_lock  = SPINLOCK_INIT;

static void klog_append_unlocked(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        s_klog_buf[s_write_pos & (KLOG_BUF_SIZE - 1)] = data[i];
        s_write_pos++;

        if (s_write_pos - s_oldest_pos > KLOG_BUF_SIZE) {
            s_oldest_pos = s_write_pos - KLOG_BUF_SIZE;
        }
    }
}

void klog_puts(const char *str) {
    if (!str) return;

    /* Mirror to serial console so early boot & crash output is always visible */
    serial_puts(str);

    uint64_t flags = spinlock_irqsave(&s_klog_lock);

    const char *p = str;
    while (*p) {
        if (s_at_line_start) {
            uint64_t ms = g_ticks;
            char prefix[32];
            int plen = snprintf(prefix, sizeof(prefix), "[%5llu.%03llu] ",
                                (unsigned long long)(ms / 1000ULL),
                                (unsigned long long)(ms % 1000ULL));
            if (plen > 0) {
                klog_append_unlocked(prefix, (size_t)plen);
            }
            s_at_line_start = false;
        }

        const char *nl = strchr(p, '\n');
        if (nl) {
            size_t chunk = (size_t)(nl - p) + 1;
            klog_append_unlocked(p, chunk);
            s_at_line_start = true;
            p = nl + 1;
        } else {
            klog_append_unlocked(p, strlen(p));
            break;
        }
    }

    spinlock_irqrestore(&s_klog_lock, flags);
}

void klog_putchar(char c) {
    /* Mirror single character to serial console */
    char s[2] = { c, '\0' };
    write_serial(c);

    uint64_t flags = spinlock_irqsave(&s_klog_lock);

    if (s_at_line_start) {
        uint64_t ms = g_ticks;
        char prefix[32];
        int plen = snprintf(prefix, sizeof(prefix), "[%5llu.%03llu] ",
                            (unsigned long long)(ms / 1000ULL),
                            (unsigned long long)(ms % 1000ULL));
        if (plen > 0) {
            klog_append_unlocked(prefix, (size_t)plen);
        }
        s_at_line_start = false;
    }

    klog_append_unlocked(&c, 1);

    if (c == '\n') {
        s_at_line_start = true;
    }

    spinlock_irqrestore(&s_klog_lock, flags);
}

static void __klog_putchar_int(char c, void* ctx) {
    (void)ctx;
    klog_putchar(c);
}

void klog_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    custom_vcbprintf(__klog_putchar_int, NULL, fmt, args);
    va_end(args);
}

size_t klog_read_snapshot(char *dst, size_t max_len, size_t *offset) {
    if (!dst || max_len == 0 || !offset) return 0;

    uint64_t flags = spinlock_irqsave(&s_klog_lock);

    uint64_t pos = *offset;
    /* If reader fell behind overwritten ring data, fast-forward to oldest valid byte */
    if (pos < s_oldest_pos) {
        pos = s_oldest_pos;
    }

    if (pos >= s_write_pos) {
        *offset = (size_t)s_write_pos;
        spinlock_irqrestore(&s_klog_lock, flags);
        return 0; /* EOF */
    }

    size_t avail = (size_t)(s_write_pos - pos);
    size_t to_copy = (avail < max_len) ? avail : max_len;

    for (size_t i = 0; i < to_copy; i++) {
        dst[i] = s_klog_buf[(pos + i) & (KLOG_BUF_SIZE - 1)];
    }

    *offset = (size_t)(pos + to_copy);
    spinlock_irqrestore(&s_klog_lock, flags);
    return to_copy;
}

static int64_t kmsg_read(vfs_file_t *file, void *buf, size_t count) {
    if (!file || !buf) return -EFAULT;
    if (count == 0) return 0;

    size_t off = (size_t)file->offset;
    size_t n = klog_read_snapshot((char *)buf, count, &off);
    file->offset = off;
    return (int64_t)n;
}

static const vfs_ops_t s_kmsg_ops = {
    .read = kmsg_read,
};

void klog_init(void) {
    memset(s_klog_buf, 0, sizeof(s_klog_buf));
    s_write_pos = 0;
    s_oldest_pos = 0;
    s_at_line_start = true;
}

void klog_devfs_init(void) {
    devfs_register_device("kmsg", &s_kmsg_ops, NULL);
}

size_t klog_size_total() {
    return KLOG_BUF_SIZE;
}

size_t klog_size_unread() {
    uint64_t flags = spinlock_irqsave(&s_klog_lock);
    uint64_t start = (s_consumed_pos < s_oldest_pos) ? s_oldest_pos : s_consumed_pos;
    size_t unread = (size_t)(s_write_pos - start);
    spinlock_irqrestore(&s_klog_lock, flags);
    return unread;
}

void klog_clear() {
    uint64_t flags = spinlock_irqsave(&s_klog_lock);
    s_oldest_pos   = s_write_pos;
    s_consumed_pos = s_write_pos;
    spinlock_irqrestore(&s_klog_lock, flags);
}

/* Non-destructive dump of the entire retained ring buffer (used by dmesg) */
size_t klog_read_all(char *dst, size_t max_len, bool clear) {
    if (!dst || max_len == 0) return 0;

    uint64_t flags = spinlock_irqsave(&s_klog_lock);

    uint64_t avail = s_write_pos - s_oldest_pos;
    /* If ring buffer holds more than max_len, return the most recent max_len bytes */
    uint64_t start = (avail > max_len) ? (s_write_pos - max_len) : s_oldest_pos;
    size_t to_copy = (size_t)(s_write_pos - start);

    for (size_t i = 0; i < to_copy; i++) {
        dst[i] = s_klog_buf[(start + i) & (KLOG_BUF_SIZE - 1)];
    }

    if (clear) {
        s_oldest_pos   = s_write_pos;
        s_consumed_pos = s_write_pos;
    }

    spinlock_irqrestore(&s_klog_lock, flags);
    return to_copy;
}

/* Destructive read of unread messages (syslog action 2) */
size_t klog_read_consume(char *dst, size_t max_len) {
    if (!dst || max_len == 0) return 0;

    uint64_t flags = spinlock_irqsave(&s_klog_lock);

    if (s_consumed_pos < s_oldest_pos) {
        s_consumed_pos = s_oldest_pos;
    }

    size_t avail = (size_t)(s_write_pos - s_consumed_pos);
    size_t to_copy = (avail < max_len) ? avail : max_len;

    for (size_t i = 0; i < to_copy; i++) {
        dst[i] = s_klog_buf[(s_consumed_pos + i) & (KLOG_BUF_SIZE - 1)];
    }

    s_consumed_pos += to_copy;
    spinlock_irqrestore(&s_klog_lock, flags);
    return to_copy;
}