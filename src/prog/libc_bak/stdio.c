#include "errno.h"
#include "fcntl.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
    putchar('\n');
    return 0;
}

static void buf_emit(char *buf, size_t size, size_t *written, char c) {
    if (*written + 1 < size) {
        buf[*written] = c;
    }
    (*written)++;
}

int vsnprintf(char *buf, size_t size, const char *format, va_list ap) {
    size_t written = 0;

    for (size_t i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            buf_emit(buf, size, &written, format[i]);
            continue;
        }

        i++;
        switch (format[i]) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                buf_emit(buf, size, &written, c);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) buf_emit(buf, size, &written, *s++);
                break;
            }
            case 'd':
            case 'i': {
                int64_t val = (int64_t)va_arg(ap, int);
                if (val < 0) {
                    buf_emit(buf, size, &written, '-');
                    val = -val;
                }
                char num_buf[32];
                int idx = 0;
                if (val == 0) num_buf[idx++] = '0';
                while (val > 0) {
                    num_buf[idx++] = '0' + (val % 10);
                    val /= 10;
                }
                while (idx > 0) buf_emit(buf, size, &written, num_buf[--idx]);
                break;
            }
            case 'x':
            case 'p': {
                uint64_t val = (format[i] == 'p') ? (uintptr_t)va_arg(ap, void *) : (uint32_t)va_arg(ap, unsigned int);
                if (format[i] == 'p') {
                    buf_emit(buf, size, &written, '0');
                    buf_emit(buf, size, &written, 'x');
                }
                char hex_buf[32];
                int idx = 0;
                const char digits[] = "0123456789abcdef";
                if (val == 0) hex_buf[idx++] = '0';
                while (val > 0) {
                    hex_buf[idx++] = digits[val & 0xF];
                    val >>= 4;
                }
                while (idx > 0) buf_emit(buf, size, &written, hex_buf[--idx]);
                break;
            }
            case '%':
                buf_emit(buf, size, &written, '%');
                break;
            default:
                buf_emit(buf, size, &written, '%');
                buf_emit(buf, size, &written, format[i]);
                break;
        }
    }

    if (size > 0) {
        size_t null_pos = (written < size) ? written : (size - 1);
        buf[null_pos] = '\0';
    }
    return (int)written;
}

int snprintf(char *buf, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buf, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(buf, 0x7FFFFFFF, format, ap);
    va_end(ap);
    return ret;
}

int printf(const char *format, ...) {
    char out_buf[1024];
    va_list ap;
    va_start(ap, format);
    int len = vsnprintf(out_buf, sizeof(out_buf), format, ap);
    va_end(ap);

    if (len > 0) {
        size_t count = (size_t)len < sizeof(out_buf) ? (size_t)len : sizeof(out_buf) - 1;
        write(STDOUT_FILENO, out_buf, count);
    }
    return len;
}

int serial_printf(const char* format, ...) {
    char out_buf[1024];
    va_list ap;
    va_start(ap, format);
    int len = vsnprintf(out_buf, sizeof(out_buf), format, ap);
    va_end(ap);

    if (len > 0) {
        size_t count = (size_t)len < sizeof(out_buf) ? (size_t)len : sizeof(out_buf) - 1;
        int fd = open("/dev/serial", O_WRONLY);
        write(fd, out_buf, count);
        close(fd);
    }
    return len;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    if (!stream || !format) {
        errno = EINVAL;
        return -1;
    }

    char out_buf[1024];
    int len = vsnprintf(out_buf, sizeof(out_buf), format, ap);

    if (len > 0) {
        size_t count = (size_t)len < sizeof(out_buf) ? (size_t)len : sizeof(out_buf) - 1;
        size_t written = fwrite(out_buf, 1, count, stream);
        if (written < count) {
            return -1;
        }
    }
    return len;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

static FILE _stdin_file  = { .fd = 0, .flags = 0, .eof = 0, .error = 0 };
static FILE _stdout_file = { .fd = 1, .flags = 0, .eof = 0, .error = 0 };
static FILE _stderr_file = { .fd = 2, .flags = 0, .eof = 0, .error = 0 };

FILE* stdin = &_stdin_file;
FILE* stdout = &_stdout_file;
FILE* stderr = &_stderr_file;

int fileno(FILE* stream) {
    if (!stream) {
        errno = EBADF;
        return -1;
    }

    return stream->fd;
}

FILE* fopen(const char* path, const char* mode) {
    if (!path || !mode) {
        errno = EINVAL;
        return NULL;
    }

    int flags = 0;
    if (mode[0] == 'r') {
        flags = (mode[1] == '+') ? O_RDWR : O_RDONLY;
    } else if (mode[0] == 'w') {
        flags = (mode[1] == '+') ? (O_RDWR | O_CREAT | O_TRUNC) : (O_WRONLY | O_CREAT | O_TRUNC);
    } else if (mode[0] == 'a') {
        flags = (mode[1] == '+') ? (O_RDWR | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_APPEND);
    } else {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(path, flags, 0666);
    if (fd < 0) return NULL;

    FILE* f = (FILE*)malloc(sizeof(FILE));

    if (!f) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }

    f->fd = fd;
    f->flags = flags;
    f->eof = 0;
    f->error = 0;
    return f;
}

int fclose(FILE* stream) {
    if (!stream) return EOF;
    int res = close(stream->fd);
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }

    return (res < 0) ? EOF : 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    ssize_t bytes_read = read(stream->fd, ptr, total_bytes);

    if (bytes_read < 0) {
        stream->error = 1;
        return 0;
    }
    if (bytes_read == 0) {
        stream->eof = 1;
        return 0;
    }
    return (size_t)bytes_read / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    ssize_t written = write(stream->fd, ptr, total_bytes);

    if (written < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)written / size;
}

int fgetc(FILE *stream) {
    unsigned char ch;
    if (fread(&ch, 1, 1, stream) == 1) return (int)ch;
    return EOF;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1) return (int)ch;
    return EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0 || !stream) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream) return EOF;
    size_t len = 0;
    while (s[len]) len++;
    return (fwrite(s, 1, len, stream) == len) ? 0 : EOF;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0; // Unbuffered, nothing to flush
}