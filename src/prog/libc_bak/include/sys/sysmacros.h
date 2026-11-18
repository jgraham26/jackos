#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H 1

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standard 64-bit Linux / glibc dev_t bit allocation:
 * dev_t is an unsigned 64-bit integer (unsigned long).
 *
 * Major: 12 bits (bits 8-19, with upper bits in 32-43)
 * Minor: 20 bits (bits 0-7, with upper bits in 20-31)
 */

#define major(dev) \
    ((unsigned int)((((dev) >> 8) & 0xfff) | ((unsigned int)((dev) >> 32) & ~0xfff)))

#define minor(dev) \
    ((unsigned int)(((dev) & 0xff) | ((unsigned int)((dev) >> 12) & ~0xff)))

#define makedev(ma, mi) \
    (((dev_t)(((unsigned int)(ma) & 0xfff) << 8)) | \
     ((dev_t)(((unsigned int)(ma) & ~0xfff) << 32)) | \
     ((dev_t)((unsigned int)(mi) & 0xff)) | \
     ((dev_t)(((unsigned int)(mi) & ~0xff) << 12)))

/* GNU / glibc compatibility macros */
#define gnu_dev_major(dev) major(dev)
#define gnu_dev_minor(dev) minor(dev)
#define gnu_dev_makedev(ma, mi) makedev(ma, mi)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSMACROS_H */