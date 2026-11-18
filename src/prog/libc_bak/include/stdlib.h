#ifndef _STDLIB_H
#define _STDLIB_H

#include <sys/types.h>

void *malloc(size_t size);
void  free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void  exit(int status) __attribute__((noreturn));
int   atoi(const char *str);

char *getenv(const char *name);
int putenv(char *string);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

long long           strtoll(const char *nptr, char **endptr, int base);
unsigned long long  strtoull(const char *nptr, char **endptr, int base);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif