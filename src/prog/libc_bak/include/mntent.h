#ifndef _MNTENT_H
#define _MNTENT_H 1

#include <stdio.h>
#include <paths.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File containing active mount entries */
#ifndef _PATH_MOUNTED
#define _PATH_MOUNTED "/etc/mtab"
#endif

/* File containing configured static file systems */
#ifndef _PATH_MNTTAB
#define _PATH_MNTTAB  "/etc/fstab"
#endif

/* Structure describing a mounted filesystem entry */
struct mntent {
    char *mnt_fsname; /* Device or server providing the filesystem */
    char *mnt_dir;    /* Filesystem path prefix (mount point) */
    char *mnt_type;   /* Filesystem type (ext2, vfat, devfs, etc.) */
    char *mnt_opts;   /* Mount options (ro, rw, noexec, etc.) */
    int   mnt_freq;   /* Dump frequency in days */
    int   mnt_passno; /* Pass number on parallel fsck */
};

/* Standard function declarations */
FILE          *setmntent(const char *filename, const char *type);
struct mntent *getmntent(FILE *stream);
struct mntent *getmntent_r(FILE *stream, struct mntent *mp,
                           char *buffer, int bufsiz);
int            addmntent(FILE *stream, const struct mntent *mp);
int            endmntent(FILE *stream);
char          *hasmntopt(const struct mntent *mp, const char *opt);

#ifdef __cplusplus
}
#endif

#endif /* _MNTENT_H */