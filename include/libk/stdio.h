#pragma once

#include <stddef.h>

void serial_puts(const char* str);
int serial_printf(const char *fmt, ...);

void puts(const char *str);
void putchar(char c);
int printf(const char* fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);

#ifndef SEEK_SET
#define SEEK_SET 0 /* Seek relative to the beginning of the file */
#define SEEK_CUR 1 /* Seek relative to current file offset */
#define SEEK_END 2 /* Seek relative to end of file */
#endif