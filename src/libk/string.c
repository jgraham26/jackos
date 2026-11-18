#include <stddef.h>
#include <string.h>
#include <stdint.h>

void* memset(void* dest, int c, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    unsigned char val = (unsigned char)c;

    while (n--) {
        *d++ = val;
    }

    return dest;
}

size_t strlen(const char *s) {
    const char *p = s;

    while (*p != '\0') {
        p++;
    }

    return (size_t)(p - s);
}

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    typedef uintptr_t word_t;
    const size_t word_size = sizeof(word_t);
    const size_t word_mask = word_size - 1;

    /* If both src and dest share the same alignment offset, align dest to word boundary */
    if (((uintptr_t)d & word_mask) == ((uintptr_t)s & word_mask)) {
        while (((uintptr_t)d & word_mask) != 0) {
            if (n == 0) return dest;
            *d++ = *s++;
            n--;
        }

        /* Copy word by word */
        word_t *wd = (word_t *)d;
        const word_t *ws = (const word_t *)s;

        while (n >= word_size) {
            *wd++ = *ws++;
            n -= word_size;
        }

        d = (unsigned char *)wd;
        s = (const unsigned char *)ws;
    }

    /* Copy any remaining trailing bytes (or unaligned mismatch cases) */
    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;

    typedef size_t word_t;

    const size_t word_size = sizeof(word_t);

    while (n >= word_size) {
        word_t w1 = *(const word_t*)(const void*)p1;
        word_t w2 = *(const word_t*)(const void*)p2;

        if (w1 != w2) {
            break;
        }

        p1 += word_size;
        p2 += word_size;
        n -= word_size;
    }

    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *token_start;

    /* If str is NULL, continue parsing from saveptr */
    if (str == NULL) {
        str = *saveptr;
    }

    if (str == NULL) {
        return NULL;
    }

    /* 1. Skip leading delimiters */
    while (*str != '\0') {
        const char *d = delim;
        int is_delim = 0;

        while (*d != '\0') {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }

        if (!is_delim) {
            break;
        }
        str++;
    }

    /* If reached the end of the string, no tokens left */
    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    token_start = str;

    /* 2. Scan to the end of the current token */
    while (*str != '\0') {
        const char *d = delim;
        int is_delim = 0;

        while (*d != '\0') {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }

        if (is_delim) {
            /* Terminate current token and update saveptr to the next char */
            *str = '\0';
            *saveptr = str + 1;
            return token_start;
        }
        str++;
    }

    /* 3. Reached end of string while scanning the last token */
    *saveptr = str;
    return token_start;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;

    /* Copy characters from src to dest up to n bytes */
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    /* Pad the remainder of dest with null bytes up to n */
    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;

    while ((*dest++ = *src++) != '\0') {
        /* Copies each character including the terminating null byte */
    }

    return ret;
}

#include <stddef.h>

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0) {
        // If characters differ, return the difference between them
        if (*s1 != *s2) {
            return (unsigned char)*s1 - (unsigned char)*s2;
        }

        // If characters match and we reached the null terminator, strings are equal
        if (*s1 == '\0') {
            return 0;
        }

        s1++;
        s2++;
        n--;
    }

    return 0;
}

char *strchr(const char *s, int c) {
    char target = (char)c;

    while (*s != '\0') {
        if (*s == target) {
            return (char *)s;
        }
        s++;
    }

    // According to the C standard, the null terminator is part of the string.
    // If searching for '\0', return a pointer to it.
    if (target == '\0') {
        return (char *)s;
    }

    return NULL;
}


char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;

    /* Move pointer to the null terminator of dest */
    while (*dest != '\0') {
        dest++;
    }

    /* Copy up to n characters from src to dest */
    while (n > 0 && *src != '\0') {
        *dest = *src;
        dest++;
        src++;
        n--;
    }

    /* Always append the terminating null byte */
    *dest = '\0';

    return ret;
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


void *memmove(void *dest, const void *src, size_t n) {
    // Cast void pointers to unsigned char pointers for byte-level arithmetic
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    // Nothing to copy, or source and destination are identical
    if (d == s || n == 0) {
        return dest;
    }

    // Case 1: Destination is before source (or non-overlapping)
    // Safe to copy forward from beginning to end
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } 
    // Case 2: Destination is after source and may overlap
    // Copy backward from end to beginning to avoid overwriting unread source bytes
    else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}