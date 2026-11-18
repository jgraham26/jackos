#ifndef _FNMATCH_H
#define _FNMATCH_H 1

#ifdef __cplusplus
extern "C" {
#endif

/* Return values */
#define FNM_NOMATCH     1       /* Match failed */
#define FNM_NOSYS       (-1)    /* Function not supported (obsolete) */

/* Match flags */
#define FNM_NOESCAPE    (1 << 0) /* Treat backslash as an ordinary character */
#define FNM_PATHNAME    (1 << 1) /* Slash must match slash, cannot match '*' or '?' */
#define FNM_PERIOD      (1 << 2) /* Leading period in string must match explicit '.' in pattern */

/* GNU / BSD extensions commonly checked by BusyBox / ash / hush */
#define FNM_FILE_NAME   FNM_PATHNAME
#define FNM_LEADING_DIR (1 << 3) /* Ignore /... after a match */
#define FNM_CASEFOLD    (1 << 4) /* Case-insensitive match */
#define FNM_EXTMATCH    (1 << 5) /* Extended pattern matching (ksh-style) */

/* Function prototype */
int fnmatch(const char *pattern, const char *string, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _FNMATCH_H */