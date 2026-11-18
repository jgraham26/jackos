#include <pwd.h>
#include <string.h>
#include <errno.h>

static struct passwd root_pw = {
    .pw_name   = "root",
    .pw_passwd = "x",
    .pw_uid    = 0,
    .pw_gid    = 0,
    .pw_gecos  = "root",
    .pw_dir    = "/root",
    .pw_shell  = "/bin/sh"
};

struct passwd *getpwuid(uid_t uid) {
    if (uid == 0) {
        return &root_pw;
    }
    return NULL;
}

struct passwd *getpwnam(const char *name) {
    if (name && strcmp(name, "root") == 0) {
        return &root_pw;
    }
    return NULL;
}

int getpwuid_r(uid_t uid, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result) {
    if (!pwd || !buf || !result) {
        return EINVAL;
    }
    if (uid == 0) {
        *pwd = root_pw;
        *result = pwd;
        return 0;
    }
    *result = NULL;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result) {
    if (!name || !pwd || !buf || !result) {
        return EINVAL;
    }
    if (strcmp(name, "root") == 0) {
        *pwd = root_pw;
        *result = pwd;
        return 0;
    }
    *result = NULL;
    return 0;
}

void setpwent(void) {}
void endpwent(void) {}
struct passwd *getpwent(void) { return NULL; }