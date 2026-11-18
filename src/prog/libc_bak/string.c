#include <string.h>
#include <stdint.h>
#define NULL ((void*)0)

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && (*dest++ = *src++)) n--;
    while (n--) *dest++ = '\0';
    return ret;
}

char *strcat(char *dest, const char *src) {
    char *p = dest + strlen(dest);
    while ((*p++ = *src++));
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    char ch = (char)c;

    while (*s != '\0') {
        if (*s == ch) {
            last = s;
        }
        s++;
    }

    /* Check if the search character is the null terminator */
    if (ch == '\0') {
        return (char *)s;
    }

    return (char *)last;
}

char *strpbrk(const char *s, const char *accept) {
    if (!s || !accept) {
        return NULL;
    }

    // Lookup table for ASCII / unsigned char values (0-255)
    unsigned char table[256] = {0};

    // Mark all characters present in `accept`
    while (*accept) {
        table[(unsigned char)*accept] = 1;
        accept++;
    }

    // Scan `s` for the first character marked in the table
    while (*s) {
        if (table[(unsigned char)*s]) {
            return (char *)s;
        }
        s++;
    }

    return NULL;
}

char *strstr(const char *haystack, const char *needle) {
    // An empty needle returns the original haystack pointer
    if (!*needle) {
        return (char *)haystack;
    }

    for (; *haystack != '\0'; haystack++) {
        // Find matching first character
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;

            // Advance both pointers while characters match
            while (*h && *n && (*h == *n)) {
                h++;
                n++;
            }

            // If we reached the end of the needle, we found a match
            if (*n == '\0') {
                return (char *)haystack;
            }
        }
    }

    return NULL;
}