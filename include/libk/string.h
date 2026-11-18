#include "stddef.h"


void *memset(void *dest, int c, size_t n);
size_t strlen(const char *s);
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
char *strtok_r(char *str, const char *delim, char **saveptr);
int strcmp(const char *s1, const char *s2);
char *strncpy(char *dest, const char *src, size_t n);
char *strcpy(char *dest, const char *src);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strncat(char *dest, const char *src, size_t n);
char *strrchr(const char *s, int c);
void *memmove(void *dest, const void *src, size_t n);