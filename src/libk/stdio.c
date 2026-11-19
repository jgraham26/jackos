#include "stdio.h"
#include "stdarg.h"
#include "stdint.h"
#include "stddef.h"
#include "drivers/serial.h"

void serial_puts(const char *str) {
    while (*str) {
        write_serial(*str++);
    }
}

static void serial_putchar_int(char c, void* ctx) {
    (void)ctx;
    write_serial(c);
}

static void putchar_int(char c, void* ctx) {
    (void)ctx;
    putchar(c);
}

static int emit_string(putc_fn_t putc_fn, void *ctx, const char *str, int precision, int width, int pad_zero, int left_align) {
    int count = 0;
    int len = 0;

    if (!str) {
        str = "(null)";
    }

    /* Compute effective length considering precision truncation */
    while (str[len] && (precision < 0 || len < precision)) {
        len++;
    }

    int padding = (width > len) ? (width - len) : 0;
    char pad_char = pad_zero ? '0' : ' ';

    if (!left_align) {
        for (int i = 0; i < padding; i++) {
            putc_fn(pad_char, ctx);
            count++;
        }
    }

    for (int i = 0; i < len; i++) {
        putc_fn(str[i], ctx);
        count++;
    }

    if (left_align) {
        for (int i = 0; i < padding; i++) {
            putc_fn(' ', ctx);
            count++;
        }
    }

    return count;
}

/* Internal helper: formats integers with sign, base, prefix, and padding */
static int emit_int(putc_fn_t putc_fn, void *ctx, uint64_t val, int is_signed, int base,
                    int uppercase, int alt_form, int width, int pad_zero, int left_align) {
    char buf[65];
    int i = 0;
    int is_negative = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (is_signed && ((int64_t)val < 0)) {
        is_negative = 1;
        val = (uint64_t)(-(int64_t)val);
    }

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % (unsigned int)base];
            val /= (unsigned int)base;
        }
    }

    /* Reverse buffer in place */
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';

    /* Prefix handling ('0x', '0X', or '-') */
    char prefix[3] = {0};
    int prefix_len = 0;

    if (is_negative) {
        prefix[prefix_len++] = '-';
    } else if (alt_form && base == 16 && val != 0) {
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = uppercase ? 'X' : 'x';
    } else if (alt_form && base == 16 && val == 0) {
        /* Optional: standard printf keeps 0x on %#x even if 0 depending on POSIX/C99;
           many systems omit it for 0. For memory address alignment, emitting 0x is standard: */
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = uppercase ? 'X' : 'x';
    }

    int num_len = i;
    int total_len = prefix_len + num_len;
    int padding = (width > total_len) ? (width - total_len) : 0;
    int count = 0;

    /* When pad_zero is set, the prefix ('-', '0x') precedes the leading zeroes */
    if (pad_zero && !left_align) {
        for (int p = 0; p < prefix_len; p++) {
            putc_fn(prefix[p], ctx);
            count++;
        }
        for (int p = 0; p < padding; p++) {
            putc_fn('0', ctx);
            count++;
        }
        for (int p = 0; p < num_len; p++) {
            putc_fn(buf[p], ctx);
            count++;
        }
    } else {
        if (!left_align) {
            for (int p = 0; p < padding; p++) {
                putc_fn(' ', ctx);
                count++;
            }
        }
        for (int p = 0; p < prefix_len; p++) {
            putc_fn(prefix[p], ctx);
            count++;
        }
        for (int p = 0; p < num_len; p++) {
            putc_fn(buf[p], ctx);
            count++;
        }
        if (left_align) {
            for (int p = 0; p < padding; p++) {
                putc_fn(' ', ctx);
                count++;
            }
        }
    }

    return count;
}

/* Core engine: accepts va_list */
int custom_vcbprintf(putc_fn_t putc_fn, void *ctx, const char *fmt, va_list args) {
    if (!putc_fn || !fmt) return -1;

    int total_chars = 0;

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            putc_fn(*p, ctx);
            total_chars++;
            continue;
        }

        p++; /* Skip '%' */
        if (*p == '\0') break;

        /* Parse flags: '-', '0', '#', '+', ' ' */
        int left_align = 0;
        int pad_zero = 0;
        int alt_form = 0;

        while (*p == '-' || *p == '0' || *p == '#' || *p == '+' || *p == ' ') {
            if (*p == '-') left_align = 1;
            else if (*p == '0') pad_zero = 1;
            else if (*p == '#') alt_form = 1;
            p++;
        }
        if (left_align) pad_zero = 0; /* Left align overrides '0' */

        /* Parse minimum field width */
        int width = 0;
        if (*p == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                left_align = 1;
                width = -width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = (width * 10) + (*p - '0');
                p++;
            }
        }

        /* Parse precision */
        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(args, int);
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    precision = (precision * 10) + (*p - '0');
                    p++;
                }
            }
        }

        /* Parse length modifier */
        int is_long = 0;
        int is_long_long = 0;
        int is_size_t = 0;

        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                is_long_long = 1;
                p++;
            } else {
                is_long = 1;
            }
        } else if (*p == 'z') {
            is_size_t = 1;
            p++;
        }

        /* Conversion specifiers */
        switch (*p) {
            case 'c': {
                char c = (char)va_arg(args, int);
                char str[2] = { c, '\0' };
                total_chars += emit_string(putc_fn, ctx, str, -1, width, 0, left_align);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                total_chars += emit_string(putc_fn, ctx, s, precision, width, 0, left_align);
                break;
            }
            case 'd':
            case 'i': {
                int64_t val;
                if (is_long_long)      val = va_arg(args, long long);
                else if (is_long)     val = va_arg(args, long);
                else if (is_size_t)   val = (int64_t)va_arg(args, size_t);
                else                  val = va_arg(args, int);

                total_chars += emit_int(putc_fn, ctx, (uint64_t)val, 1, 10, 0, 0, width, pad_zero, left_align);
                break;
            }
            case 'u': {
                uint64_t val;
                if (is_long_long)      val = va_arg(args, unsigned long long);
                else if (is_long)     val = va_arg(args, unsigned long);
                else if (is_size_t)   val = (uint64_t)va_arg(args, size_t);
                else                  val = va_arg(args, unsigned int);

                total_chars += emit_int(putc_fn, ctx, val, 0, 10, 0, 0, width, pad_zero, left_align);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t val;
                if (is_long_long)      val = va_arg(args, unsigned long long);
                else if (is_long)     val = va_arg(args, unsigned long);
                else if (is_size_t)   val = (uint64_t)va_arg(args, size_t);
                else                  val = va_arg(args, unsigned int);

                total_chars += emit_int(putc_fn, ctx, val, 0, 16, (*p == 'X'), alt_form, width, pad_zero, left_align);
                break;
            }
            case 'p': {
                uintptr_t ptr = (uintptr_t)va_arg(args, void *);
                if (ptr == 0) {
                    total_chars += emit_string(putc_fn, ctx, "(nil)", -1, width, 0, left_align);
                } else {
                    /* Emits 0x followed by pointer address */
                    total_chars += emit_int(putc_fn, ctx, (uint64_t)ptr, 0, 16, 0, 1, width, 0, left_align);
                }
                break;
            }
            case '%': {
                putc_fn('%', ctx);
                total_chars++;
                break;
            }
            default: {
                putc_fn('%', ctx);
                putc_fn(*p, ctx);
                total_chars += 2;
                break;
            }
        }
    }

    return total_chars;
}

int serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = custom_vcbprintf(serial_putchar_int, NULL, fmt, args);
    va_end(args);
    return ret;
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = custom_vcbprintf(putchar_int, NULL, fmt, args);
    va_end(args);
    return ret;
}

typedef struct {
    char *buf;
    size_t size;
    size_t written;
} snprintf_ctx_t;

static void snprintf_putchar(char c, void *ctx) {
    snprintf_ctx_t *s = (snprintf_ctx_t *)ctx;
    if (s->buf && s->size > 0 && s->written < s->size - 1) {
        s->buf[s->written] = c;
    }
    s->written++;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list args) {
    snprintf_ctx_t ctx = {
        .buf = str,
        .size = size,
        .written = 0
    };

    int total = custom_vcbprintf(snprintf_putchar, &ctx, fmt, args);
    if (total < 0) return total;

    /* Null-terminate if buffer capacity allows */
    if (str && size > 0) {
        if (ctx.written < size) {
            str[ctx.written] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return total;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(str, size, fmt, args);
    va_end(args);
    return ret;
}