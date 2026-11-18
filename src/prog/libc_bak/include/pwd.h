#ifndef _PWD_H
#define _PWD_H 1

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fallback integer types if not provided by sys/types.h */
#ifndef _UID_T_DECLARED
typedef unsigned int uid_t;
#define _UID_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
typedef unsigned int gid_t;
#define _GID_T_DECLARED
#endif

/* Standard password database structure */
struct passwd {
    char  *pw_name;   /* Username */
    char  *pw_passwd; /* Hashed passphrase or 'x' */
    uid_t  pw_uid;    /* Numerical user ID */
    gid_t  pw_gid;    /* Numerical group ID */
    char  *pw_gecos;  /* User real name / info */
    char  *pw_dir;    /* Home directory */
    char  *pw_shell;  /* Initial login shell */
};

/* Standard POSIX function declarations */
struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uid_t uid);

int getpwnam_r(const char *name, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result);
int getpwuid_r(uid_t uid, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result);

void           setpwent(void);
void           endpwent(void);
struct passwd *getpwent(void);

#ifdef __cplusplus
}
#endif

#endif /* _PWD_H */