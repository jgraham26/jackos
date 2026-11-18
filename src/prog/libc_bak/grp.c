#include <grp.h>
#include <string.h>
#include <errno.h>

static char *root_members[] = { "root", NULL };

static struct group root_grp = {
    .gr_name   = "root",
    .gr_passwd = "x",
    .gr_gid    = 0,
    .gr_mem    = root_members
};

struct group *getgrgid(gid_t gid) {
    if (gid == 0) {
        return &root_grp;
    }
    return NULL;
}

struct group *getgrnam(const char *name) {
    if (name && strcmp(name, "root") == 0) {
        return &root_grp;
    }
    return NULL;
}

int getgrgid_r(gid_t gid, struct group *grp,
               char *buf, size_t buflen, struct group **result) {
    if (!grp || !buf || !result) {
        return EINVAL;
    }
    if (gid == 0) {
        *grp = root_grp;
        *result = grp;
        return 0;
    }
    *result = NULL;
    return 0;
}

int getgrnam_r(const char *name, struct group *grp,
               char *buf, size_t buflen, struct group **result) {
    if (!name || !grp || !buf || !result) {
        return EINVAL;
    }
    if (strcmp(name, "root") == 0) {
        *grp = root_grp;
        *result = grp;
        return 0;
    }
    *result = NULL;
    return 0;
}

void setgrent(void) {}
void endgrent(void) {}
struct group *getgrent(void) { return NULL; }

/* Group list management stubs */
int initgroups(const char *user, gid_t group) {
    (void)user;
    (void)group;
    return 0;
}

int setgroups(size_t size, const gid_t *list) {
    (void)size;
    (void)list;
    return 0;
}

int getgroups(int size, gid_t list[]) {
    if (size > 0 && list) {
        list[0] = 0;
        return 1;
    }
    return 1;
}