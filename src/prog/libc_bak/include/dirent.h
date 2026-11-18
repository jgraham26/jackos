#ifndef _DIRENT_H
#define _DIRENT_H

#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _INO_T_DECLARED
typedef unsigned long ino_t;
#define _INO_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef long off_t;
#define _OFF_T_DECLARED
#endif

/* File type constants returned in d_type */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

/* Macro to convert struct stat st_mode to dirent d_type */
#define IFTODT(mode) (((mode) & 0170000) >> 12)
#define DTTOIF(dirtype) ((dirtype) << 12)

/* Standard POSIX struct dirent (64-bit alignment) */
struct dirent {
    ino_t          d_ino;       /* 64-bit Inode number */
    off_t          d_off;       /* 64-bit Offset to next dirent */
    unsigned short d_reclen;    /* Length of this record */
    unsigned char  d_type;      /* Type of file */
    char           d_name[256]; /* Null-terminated filename */
};

#define dirent64 dirent

/* Internal opaque DIR structure */
#define _DIR_BUF_SIZE 2048

typedef struct __dirstream {
    int           fd;                         /* File descriptor */
    int           buf_pos;                    /* Current offset within buf */
    int           buf_end;                    /* Valid bytes in buf */
    struct dirent cur_entry;                  /* Statically returned dirent */
    char          buf[_DIR_BUF_SIZE] __attribute__((aligned(8)));
} DIR;

/* Standard POSIX Directory Stream Operations */
DIR           *opendir(const char *name);
DIR           *fdopendir(int fd);
struct dirent *readdir(DIR *dirp);
int            readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
void           rewinddir(DIR *dirp);
int            closedir(DIR *dirp);
long           telldir(DIR *dirp);
void           seekdir(DIR *dirp, long loc);
int            dirfd(DIR *dirp);

int            scandir(const char *dirp, struct dirent ***namelist,
                       int (*filter)(const struct dirent *),
                       int (*compar)(const struct dirent **, const struct dirent **));
int            alphasort(const struct dirent **a, const struct dirent **b);

#ifdef __cplusplus
}
#endif

int64_t getdents64(int fd, void *dirp, size_t count);

#endif