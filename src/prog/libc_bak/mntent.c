#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>

FILE *setmntent(const char *filename, const char *type) {
    return fopen(filename, type);
}

int endmntent(FILE *stream) {
    if (stream) {
        fclose(stream);
    }
    return 1;
}

/* Decode in-place octal escapes like \040 -> ' ' */
static char *decode_octal(char *s) {
    char *src = s;
    char *dst = s;
    while (*src) {
        if (src[0] == '\\' && src[1] >= '0' && src[1] <= '7' &&
            src[2] >= '0' && src[2] <= '7' && src[3] >= '0' && src[3] <= '7') {
            *dst++ = (char)(((src[1] - '0') << 6) | ((src[2] - '0') << 3) | (src[3] - '0'));
            src += 4;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return s;
}

struct mntent *getmntent_r(FILE *stream, struct mntent *mp, char *buffer, int bufsiz) {
    char *line;
    char *p;

    if (!stream || !mp || !buffer || bufsiz <= 0) {
        return NULL;
    }

    do {
        line = fgets(buffer, bufsiz, stream);
        if (!line) {
            return NULL;
        }

        /* Skip leading whitespace */
        while (*line && isspace((unsigned char)*line)) {
            line++;
        }
    } while (*line == '\0' || *line == '#');

    /* Strip trailing newline */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    /* 1. Device / FS Name */
    mp->mnt_fsname = line;
    p = strpbrk(line, " \t");
    if (!p) return NULL;
    *p++ = '\0';
    while (*p && isspace((unsigned char)*p)) p++;

    /* 2. Mount Point Directory */
    mp->mnt_dir = p;
    p = strpbrk(p, " \t");
    if (!p) return NULL;
    *p++ = '\0';
    while (*p && isspace((unsigned char)*p)) p++;

    /* 3. Filesystem Type */
    mp->mnt_type = p;
    p = strpbrk(p, " \t");
    if (!p) return NULL;
    *p++ = '\0';
    while (*p && isspace((unsigned char)*p)) p++;

    /* 4. Options */
    mp->mnt_opts = p;
    p = strpbrk(p, " \t");
    if (p) {
        *p++ = '\0';
        while (*p && isspace((unsigned char)*p)) p++;

        /* 5. Dump Frequency */
        mp->mnt_freq = atoi(p);
        p = strpbrk(p, " \t");
        if (p) {
            while (*p && isspace((unsigned char)*p)) p++;
            /* 6. Pass Number */
            mp->mnt_passno = atoi(p);
        } else {
            mp->mnt_passno = 0;
        }
    } else {
        mp->mnt_freq = 0;
        mp->mnt_passno = 0;
    }

    decode_octal(mp->mnt_fsname);
    decode_octal(mp->mnt_dir);

    return mp;
}

struct mntent *getmntent(FILE *stream) {
    static char static_buf[1024];
    static struct mntent static_entry;
    return getmntent_r(stream, &static_entry, static_buf, sizeof(static_buf));
}

int addmntent(FILE *stream, const struct mntent *mp) {
    if (!stream || !mp) {
        return 1;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        return 1;
    }
    return fprintf(stream, "%s %s %s %s %d %d\n",
                   mp->mnt_fsname ? mp->mnt_fsname : "none",
                   mp->mnt_dir ? mp->mnt_dir : "none",
                   mp->mnt_type ? mp->mnt_type : "generic",
                   mp->mnt_opts ? mp->mnt_opts : "rw",
                   mp->mnt_freq,
                   mp->mnt_passno) < 0 ? 1 : 0;
}

char *hasmntopt(const struct mntent *mp, const char *opt) {
    if (!mp || !mp->mnt_opts || !opt) {
        return NULL;
    }

    size_t optlen = strlen(opt);
    char *p = mp->mnt_opts;

    while ((p = strstr(p, opt)) != NULL) {
        if ((p == mp->mnt_opts || p[-1] == ',') &&
            (p[optlen] == '\0' || p[optlen] == ',' || p[optlen] == '=')) {
            return p;
        }
        p += optlen;
    }

    return NULL;
}