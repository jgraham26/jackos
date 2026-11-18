#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#ifndef ULLONG_MAX
#define ULLONG_MAX (~0ULL)
#endif
#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif
#ifndef LLONG_MIN
#define LLONG_MIN (-LLONG_MAX - 1LL)
#endif


void *sbrk(intptr_t increment) {
    uintptr_t current_brk = brk(0);
    if (increment == 0) {
        return (void *)current_brk;
    }

    uintptr_t new_brk = current_brk + increment;
    uintptr_t actual_brk = brk(new_brk);

    if (actual_brk != new_brk) {
        return (void *)-1;
    }
    return (void *)current_brk;
}

typedef struct block {
    size_t size;
    struct block *next;
    int is_free;
} block_t;

#define BLOCK_HEADER_SIZE sizeof(block_t)
static block_t *g_block_head = NULL;

void *malloc(size_t size) {
    if (size == 0) return NULL;

    /* 16-byte alignment */
    size = (size + 15) & ~15ULL;

    block_t *curr = g_block_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            curr->is_free = 0;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    /* Allocate new block via sbrk */
    size_t total_size = BLOCK_HEADER_SIZE + size;
    void *chunk = sbrk(total_size);
    if (chunk == (void *)-1) {
        return NULL;
    }

    block_t *new_block = (block_t *)chunk;
    new_block->size = size;
    new_block->is_free = 0;
    new_block->next = g_block_head;
    g_block_head = new_block;

    return (void *)(new_block + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_t *block = ((block_t *)ptr) - 1;
    block->is_free = 1;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_t *block = ((block_t *)ptr) - 1;
    if (block->size >= size) {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

int atoi(const char *s) {
    int res = 0;
    int sign = 1;

    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    unsigned long long cutoff;
    int cutlim;
    int c;
    int any = 0;
    int neg = 0;

    /* 1. Skip leading whitespace */
    do {
        c = (unsigned char)*s++;
    } while (isspace(c));

    /* 2. Check for optional leading sign */
    if (c == '-') {
        neg = 1;
        c = (unsigned char)*s++;
    } else if (c == '+') {
        c = (unsigned char)*s++;
    }

    /* 3. Validate or auto-detect radix base */
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = (unsigned char)s[1];
        s += 2;
        base = 16;
    }
    if (base == 0) {
        base = (c == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (endptr) {
            *endptr = (char *)nptr;
        }
        return 0;
    }

    /* 4. Precompute cutoff boundaries for overflow detection */
    cutoff = ULLONG_MAX / (unsigned long long)base;
    cutlim = (int)(ULLONG_MAX % (unsigned long long)base);

    /* 5. Parse valid digits in range [0, base - 1] */
    for (;; c = (unsigned char)*s++) {
        int val;
        if (isdigit(c)) {
            val = c - '0';
        } else if (isalpha(c)) {
            val = (c >= 'a' && c <= 'z') ? (c - 'a' + 10) : (c - 'A' + 10);
        } else {
            break;
        }

        if (val >= base) {
            break;
        }

        if (any < 0 || acc > cutoff || (acc == cutoff && val > cutlim)) {
            any = -1; /* Overflow flag */
        } else {
            any = 1;
            acc = acc * (unsigned long long)base + (unsigned long long)val;
        }
    }

    /* 6. Handle overflow or return accumulator */
    if (any < 0) {
        acc = ULLONG_MAX;
        errno = ERANGE;
    } else if (neg) {
        acc = -acc;
    }

    if (endptr) {
        *endptr = (char *)(any ? s - 1 : nptr);
    }

    return acc;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    unsigned long long cutoff;
    int cutlim;
    int c;
    int any = 0;
    int neg = 0;

    /* 1. Skip leading whitespace */
    do {
        c = (unsigned char)*s++;
    } while (isspace(c));

    /* 2. Check for optional leading sign */
    if (c == '-') {
        neg = 1;
        c = (unsigned char)*s++;
    } else if (c == '+') {
        c = (unsigned char)*s++;
    }

    /* 3. Validate or auto-detect radix base */
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = (unsigned char)s[1];
        s += 2;
        base = 16;
    }
    if (base == 0) {
        base = (c == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (endptr) {
            *endptr = (char *)nptr;
        }
        return 0;
    }

    /* 4. Precompute cutoff boundaries against absolute max signed limit */
    cutoff = neg ? ((unsigned long long)-(LLONG_MIN + LLONG_MAX) + LLONG_MAX) : LLONG_MAX;
    cutlim = (int)(cutoff % (unsigned long long)base);
    cutoff /= (unsigned long long)base;

    /* 5. Parse digits */
    for (;; c = (unsigned char)*s++) {
        int val;
        if (isdigit(c)) {
            val = c - '0';
        } else if (isalpha(c)) {
            val = (c >= 'a' && c <= 'z') ? (c - 'a' + 10) : (c - 'A' + 10);
        } else {
            break;
        }

        if (val >= base) {
            break;
        }

        if (any < 0 || acc > cutoff || (acc == cutoff && val > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc = acc * (unsigned long long)base + (unsigned long long)val;
        }
    }

    /* 6. Bound checks and return value */
    if (any < 0) {
        acc = neg ? (unsigned long long)LLONG_MIN : (unsigned long long)LLONG_MAX;
        errno = ERANGE;
    } else if (neg) {
        acc = -acc;
    }

    if (endptr) {
        *endptr = (char *)(any ? s - 1 : nptr);
    }

    return (long long)acc;
}

long strtol(const char *nptr, char **endptr, int base) {
    return (long)strtoll(nptr, endptr, base);
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtoull(nptr, endptr, base);
}

long atol(const char *nptr) {
    return (long)strtoll(nptr, NULL, 10);
}

long long atoll(const char *nptr) {
    return strtoll(nptr, NULL, 10);
}