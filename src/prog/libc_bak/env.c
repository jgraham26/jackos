#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Global pointer required by POSIX */
char **environ = NULL;

/* Tracks whether the array of pointers in `environ` was allocated on the heap */
static int environ_is_heap = 0;

/* Helper: Count elements in environ */
static size_t __env_len(void) {
    if (!environ) return 0;
    size_t count = 0;
    while (environ[count]) {
        count++;
    }
    return count;
}

/* Helper: Find the index of a variable by name. Returns -1 if not found. */
static int __find_env(const char *name, size_t name_len) {
    if (!environ || !name) return -1;

    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
            return i;
        }
    }
    return -1;
}

char *getenv(const char *name) {
    if (!name || *name == '\0' || !environ) {
        return NULL;
    }

    size_t len = strlen(name);
    int idx = __find_env(name, len);
    if (idx != -1) {
        return environ[idx] + len + 1; /* Skip "NAME=" */
    }

    return NULL;
}

int putenv(char *string) {
    if (!string || *string == '\0') {
        errno = EINVAL;
        return -1;
    }

    char *eq = strchr(string, '=');
    if (!eq || eq == string) {
        errno = EINVAL;
        return -1;
    }

    size_t name_len = (size_t)(eq - string);
    int idx = __find_env(string, name_len);

    if (idx != -1) {
        /* Already exists: simply replace the pointer */
        environ[idx] = string;
        return 0;
    }

    /* Doesn't exist: append to environ */
    size_t count = __env_len();
    char **new_env;

    if (environ_is_heap) {
        new_env = (char **)realloc(environ, (count + 2) * sizeof(char *));
        if (!new_env) {
            errno = ENOMEM;
            return -1;
        }
    } else {
        /* environ is currently on the stack from the ELF loader: copy to heap */
        new_env = (char **)malloc((count + 2) * sizeof(char *));
        if (!new_env) {
            errno = ENOMEM;
            return -1;
        }
        if (count > 0 && environ) {
            memcpy(new_env, environ, count * sizeof(char *));
        }
        environ_is_heap = 1;
    }

    new_env[count] = string;
    new_env[count + 1] = NULL;
    environ = new_env;

    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || *name == '\0' || strchr(name, '=') != NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!value) {
        value = "";
    }

    size_t name_len = strlen(name);
    int idx = __find_env(name, name_len);

    if (idx != -1 && !overwrite) {
        return 0;
    }

    /* Allocate buffer for "NAME=value\0" */
    size_t val_len = strlen(value);
    char *new_entry = (char *)malloc(name_len + 1 + val_len + 1);
    if (!new_entry) {
        errno = ENOMEM;
        return -1;
    }

    /* Construct NAME=value */
    memcpy(new_entry, name, name_len);
    new_entry[name_len] = '=';
    memcpy(new_entry + name_len + 1, value, val_len);
    new_entry[name_len + 1 + val_len] = '\0';

    if (putenv(new_entry) != 0) {
        free(new_entry);
        return -1;
    }

    return 0;
}

int unsetenv(const char *name) {
    if (!name || *name == '\0' || strchr(name, '=') != NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!environ) {
        return 0;
    }

    size_t name_len = strlen(name);

    /* Repeat search in case duplicate names were added via putenv */
    int idx;
    while ((idx = __find_env(name, name_len)) != -1) {
        /* Shift remaining pointers down to overwrite the removed entry */
        for (int i = idx; environ[i]; i++) {
            environ[i] = environ[i + 1];
        }
    }

    return 0;
}