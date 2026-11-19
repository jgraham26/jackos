#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

void klog_init(void);
void klog_devfs_init(void);

void klog_puts(const char *str);
void klog_vprintf(const char *fmt, va_list args);
void klog_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Optional helper if you want a dedicated syscall or internal dump */
size_t klog_read_snapshot(char *dst, size_t max_len, size_t *offset);

size_t klog_size_total(void);
size_t klog_size_unread(void);
size_t klog_read_all(char *dst, size_t max_len, bool clear);
size_t klog_read_consume(char *dst, size_t max_len);
void   klog_clear(void);