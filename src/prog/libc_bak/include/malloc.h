#ifndef _MALLOC_H
#define _MALLOC_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard memory allocation functions */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

/* Aligned allocations */
void *memalign(size_t alignment, size_t size);
void *valloc(size_t size);
void *pvalloc(size_t size);
int   posix_memalign(void **memptr, size_t alignment, size_t size);

/* Sizing and introspection */
size_t malloc_usable_size(void *ptr);

/* Legacy memory inspection (mallinfo structure matching standard GNU/musl layouts) */
struct mallinfo {
    int arena;    /* Non-mmapped space allocated (bytes) */
    int ordblks;  /* Number of free chunks */
    int smblks;   /* Number of free fastbin blocks */
    int hblks;    /* Number of mmapped regions */
    int hblkhd;   /* Space allocated in mmapped regions (bytes) */
    int usmblks;  /* Maximum total allocated space (bytes) */
    int fsmblks;  /* Space in freed fastbin blocks (bytes) */
    int uordblks; /* Total allocated space (bytes) */
    int fordblks; /* Total free space (bytes) */
    int keepcost; /* Top-most, releasable space (bytes) */
};

struct mallinfo mallinfo(void);

/* Tuning stubs */
#define M_TRIM_THRESHOLD -1
#define M_TOP_PAD        -2
#define M_MMAP_THRESHOLD -3
#define M_MMAP_MAX       -4
#define M_CHECK_ACTION   -5

int mallopt(int param, int value);
int malloc_trim(size_t pad);

#ifdef __cplusplus
}
#endif

#endif /* _MALLOC_H */