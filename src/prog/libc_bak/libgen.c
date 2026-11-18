#include <libgen.h>
#include <string.h>

char *basename(char *path) {
    static char dot[] = ".";
    static char slash[] = "/";

    if (!path || !*path) {
        return dot;
    }

    size_t len = strlen(path);

    /* Strip trailing slashes, but keep "/" intact */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }

    if (len == 1 && path[0] == '/') {
        return slash;
    }

    /* Find the last slash */
    char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

char *dirname(char *path) {
    static char dot[] = ".";
    static char slash[] = "/";

    if (!path || !*path) {
        return dot;
    }

    size_t len = strlen(path);

    /* Strip trailing slashes, but keep "/" intact */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }

    /* Find the last slash */
    char *p = strrchr(path, '/');
    if (!p) {
        return dot; /* No slash found -> current directory */
    }

    /* If slash is at root (e.g. "/usr" -> p == path) */
    if (p == path) {
        return slash;
    }

    /* Strip any consecutive slashes preceding the component */
    while (p > path && *(p - 1) == '/') {
        p--;
    }

    *p = '\0';
    return path;
}