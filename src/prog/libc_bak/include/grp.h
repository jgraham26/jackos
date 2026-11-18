#ifndef _GRP_H
#define _GRP_H 1

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fallback gid_t if not provided by sys/types.h */
#ifndef _GID_T_DECLARED
typedef unsigned int gid_t;
#define _GID_T_DECLARED
#endif

/* Standard group database structure */
struct group {
    char   *gr_name;   /* Group name */
    char   *gr_passwd; /* Group password (usually 'x' or empty) */
    gid_t   gr_gid;    /* Numerical group ID */
    char  **gr_mem;    /* NULL-terminated array of member user names */
};

/* Standard POSIX function declarations */
struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);

int getgrgid_r(gid_t gid, struct group *grp,
               char *buf, size_t buflen, struct group **result);
int getgrnam_r(const char *name, struct group *grp,
               char *buf, size_t buflen, struct group **result);

void          setgrent(void);
void          endgrent(void);
struct group *getgrent(void);

/* BSD / Linux extensions frequently called by login, su, and shells */
int initgroups(const char *user, gid_t group);
int setgroups(size_t size, const gid_t *list);
int getgroups(int size, gid_t list[]);

#ifdef __cplusplus
}
#endif

#endif /* _GRP_H */