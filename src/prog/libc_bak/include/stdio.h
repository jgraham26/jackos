#ifndef _STDIO_H
#define _STDIO_H

#include <sys/types.h>
#include <stdarg.h>

int putchar(int c);
int puts(const char *s);
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int serial_printf(const char* format, ...);

#define EOF (-1)

typedef struct FILE {
    int fd;
    int flags;
    int eof;
    int error;
} FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
char* fgets(char* str, int n, FILE* stream);
int fputs(const char* str, FILE* stream);
int fputc(int c, FILE* stream);
int fgetc(FILE* stream);
int fileno(FILE* stream);
int fflush(FILE* stream);

#endif