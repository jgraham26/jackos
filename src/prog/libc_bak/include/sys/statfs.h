#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H 1

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Filesystem ID representation */
typedef struct {
    int val[2];
} fsid_t;

/* Standard 64-bit statfs structure (matching Linux x86_64 ABI) */
struct statfs {
    uint64_t f_type;       /* Type of filesystem (magic number) */
    uint64_t f_bsize;      /* Optimal transfer block size */
    uint64_t f_blocks;     /* Total data blocks in filesystem */
    uint64_t f_bfree;      /* Free blocks in filesystem */
    uint64_t f_bavail;     /* Free blocks available to unprivileged user */
    uint64_t f_files;      /* Total file nodes in filesystem */
    uint64_t f_ffree;      /* Free file nodes in filesystem */
    fsid_t   f_fsid;       /* Filesystem ID */
    uint64_t f_namelen;    /* Maximum length of filenames */
    uint64_t f_frsize;     /* Fragment size (since Linux 2.6) */
    uint64_t f_flags;      /* Mount flags of filesystem */
    uint64_t f_spare[4];   /* Padding bytes reserved for future expansion */
};

/* 64-bit ABI compatibility alias */
#define statfs64 statfs

/* --------------------------------------------------------------------------
 * Common Filesystem Magic Numbers (f_type)
 * -------------------------------------------------------------------------- */
#define EXT2_SUPER_MAGIC    0xEF53
#define EXT3_SUPER_MAGIC    0xEF53
#define EXT4_SUPER_MAGIC    0xEF53
#define MSDOS_SUPER_MAGIC   0x4d44
#define ISOFS_SUPER_MAGIC   0x9660
#define RAMFS_MAGIC         0x858458f6
#define TMPFS_MAGIC         0x01021994
#define DEVFS_SUPER_MAGIC   0x1373
#define PROC_SUPER_MAGIC    0x9fa0
#define SYSFS_MAGIC         0x62656572

/* Function declarations */
int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);
int statfs64(const char *path, struct statfs64 *buf);
int fstatfs64(int fd, struct statfs64 *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STATFS_H */