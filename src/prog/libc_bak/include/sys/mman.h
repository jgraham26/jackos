#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H 1

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _OFF_T_DECLARED
typedef long off_t;
#define _OFF_T_DECLARED
#endif

/* Return value on mmap failure */
#define MAP_FAILED ((void *) -1)

/* --------------------------------------------------------------------------
 * Memory Protection Flags (prot argument)
 * -------------------------------------------------------------------------- */
#define PROT_NONE       0x0     /* Page cannot be accessed */
#define PROT_READ       0x1     /* Page can be read */
#define PROT_WRITE      0x2     /* Page can be written */
#define PROT_EXEC       0x4     /* Page can be executed */
#define PROT_GROWSDOWN  0x01000000 /* Stack-like segment growth */
#define PROT_GROWSUP    0x02000000

/* --------------------------------------------------------------------------
 * Sharing / Mapping Flags (flags argument)
 * -------------------------------------------------------------------------- */
#define MAP_SHARED      0x01    /* Share changes across processes */
#define MAP_PRIVATE     0x02    /* Changes are private (copy-on-write) */
#define MAP_TYPE        0x0F    /* Mask for type of mapping */
#define MAP_FIXED       0x10    /* Interpret addr exactly */
#define MAP_ANONYMOUS   0x20    /* Allocate zeroed pages, not backed by file */
#define MAP_ANON        MAP_ANONYMOUS

/* Linux-compatible extended flags often inspected by userland */
#define MAP_32BIT       0x40    /* Put allocation into first 2GB of address space */
#define MAP_NORESERVE   0x04000 /* Don't reserve swap space */
#define MAP_POPULATE    0x08000 /* Populate (prefault) page tables */
#define MAP_NONBLOCK    0x10000 /* Do not block on IO */
#define MAP_STACK       0x20000 /* Allocation is suitable for a process/thread stack */
#define MAP_HUGETLB     0x40000 /* Create huge page mapping */

/* --------------------------------------------------------------------------
 * msync() Flags
 * -------------------------------------------------------------------------- */
#define MS_ASYNC        1       /* Perform asynchronous writes */
#define MS_INVALIDATE   2       /* Invalidate cache */
#define MS_SYNC         4       /* Synchronous memory-to-disk write */

/* --------------------------------------------------------------------------
 * mlockall() Flags
 * -------------------------------------------------------------------------- */
#define MCL_CURRENT     1       /* Lock currently mapped pages */
#define MCL_FUTURE      2       /* Lock pages mapped in the future */

/* --------------------------------------------------------------------------
 * madvise() Advice Constants
 * -------------------------------------------------------------------------- */
#define MADV_NORMAL      0      /* No special treatment */
#define MADV_RANDOM      1      /* Expect page references in random order */
#define MADV_SEQUENTIAL  2      /* Expect page references in sequential order */
#define MADV_WILLNEED    3      /* Expect access in the near future */
#define MADV_DONTNEED    4      /* Do not expect access in the near future */

/* POSIX posix_madvise aliases */
#define POSIX_MADV_NORMAL       MADV_NORMAL
#define POSIX_MADV_SEQUENTIAL   MADV_SEQUENTIAL
#define POSIX_MADV_RANDOM       MADV_RANDOM
#define POSIX_MADV_WILLNEED     MADV_WILLNEED
#define POSIX_MADV_DONTNEED     MADV_DONTNEED

/* Standard function declarations */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);
int   mprotect(void *addr, size_t length, int prot);
int   msync(void *addr, size_t length, int flags);
int   mlock(const void *addr, size_t len);
int   munlock(const void *addr, size_t len);
int   mlockall(int flags);
int   munlockall(void);
int   madvise(void *addr, size_t length, int advice);
int   posix_madvise(void *addr, size_t len, int advice);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MMAN_H */