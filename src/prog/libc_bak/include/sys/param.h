#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H 1

#include <sys/types.h>
#include <limits.h>
#include <endian.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * System and Path Limits
 * -------------------------------------------------------------------------- */
#ifndef PATH_MAX
#define PATH_MAX        4096
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN      PATH_MAX
#endif

#ifndef NAME_MAX
#define NAME_MAX        255
#endif

#ifndef MAXNAMLEN
#define MAXNAMLEN       NAME_MAX
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN  64
#endif

#ifndef NGROUPS_MAX
#define NGROUPS_MAX     32
#endif

#ifndef NGROUPS
#define NGROUPS         NGROUPS_MAX
#endif

#ifndef NOFILE
#define NOFILE          256
#endif

#ifndef CANBSIZ
#define CANBSIZ         256
#endif

/* --------------------------------------------------------------------------
 * Unit Conversion and Memory Constants
 * -------------------------------------------------------------------------- */
#ifndef HZ
#define HZ              100     /* Standard timer tick frequency */
#endif

#ifndef EXEC_PAGESIZE
#define EXEC_PAGESIZE   4096    /* x86_64 standard 4 KiB page size */
#endif

#ifndef PAGESIZE
#define PAGESIZE        EXEC_PAGESIZE
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE       EXEC_PAGESIZE
#endif

#ifndef PAGE_MASK
#define PAGE_MASK       (~(PAGE_SIZE - 1))
#endif

/* --------------------------------------------------------------------------
 * Bit Manipulation and Arithmetic Macros
 * -------------------------------------------------------------------------- */
#ifndef MIN
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)       (((a) > (b)) ? (a) : (b))
#endif

/* Number of elements in array of size 'y' needed to contain 'x' elements */
#ifndef howmany
#define howmany(x, y)   (((x) + ((y) - 1)) / (y))
#endif

/* Rounding helpers */
#ifndef roundup
#define roundup(x, y)   ((((x) + ((y) - 1)) / (y)) * (y))
#endif

#ifndef powerof2
#define powerof2(x)     ((((x) - 1) & (x)) == 0)
#endif

/* Bit manipulation macros used by BSD/select code */
#ifndef setbit
#define setbit(a, i)    ((a)[(i) / 8] |= (1 << ((i) % 8)))
#define clrbit(a, i)    ((a)[(i) / 8] &= ~(1 << ((i) % 8)))
#define isset(a, i)     ((a)[(i) / 8] & (1 << ((i) % 8)))
#define isclr(a, i)     (((a)[(i) / 8] & (1 << ((i) % 8))) == 0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SYS_PARAM_H */