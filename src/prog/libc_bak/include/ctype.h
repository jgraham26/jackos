#ifndef _CTYPE_H
#define _CTYPE_H 1

#ifdef __cplusplus
extern "C" {
#endif

/* Standard ASCII character testing and conversion prototypes */
int isalnum(int c);
int isalpha(int c);
int isascii(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);

int tolower(int c);
int toupper(int c);
int toascii(int c);

/* Fast macro implementations (evaluated in standard "C" / ASCII locale) */
#define isdigit(c)  ((unsigned)(c) - '0' < 10)
#define islower(c)  ((unsigned)(c) - 'a' < 26)
#define isupper(c)  ((unsigned)(c) - 'A' < 26)
#define isalpha(c)  (islower(c) || isupper(c))
#define isalnum(c)  (isalpha(c) || isdigit(c))
#define isascii(c)  (((unsigned)(c) & ~0x7F) == 0)
#define toascii(c)  ((c) & 0x7F)

#define isblank(c)  ((c) == ' ' || (c) == '\t')
#define isspace(c)  ((c) == ' ' || ((unsigned)(c) - '\t' < 5)) /* \t, \n, \v, \f, \r */
#define iscntrl(c)  ((unsigned)(c) < 32 || (c) == 127)
#define isprint(c)  ((unsigned)(c) - 32 < 95)
#define isgraph(c)  ((unsigned)(c) - 33 < 94)
#define ispunct(c)  (isgraph(c) && !isalnum(c))

#define isxdigit(c) (isdigit(c) || \
                    ((unsigned)(c) - 'a' < 6) || \
                    ((unsigned)(c) - 'A' < 6))

#define tolower(c)  (isupper(c) ? ((c) + ('a' - 'A')) : (c))
#define toupper(c)  (islower(c) ? ((c) - ('a' - 'A')) : (c))

/* glibc / POSIX _l thread-safe locale re-entrancy variants (BusyBox often checks for these) */
#ifndef _LOCALE_T_DEFINED
typedef void *locale_t;
#define _LOCALE_T_DEFINED
#endif

#define isalnum_l(c, l)  isalnum(c)
#define isalpha_l(c, l)  isalpha(c)
#define isblank_l(c, l)  isblank(c)
#define iscntrl_l(c, l)  iscntrl(c)
#define isdigit_l(c, l)  isdigit(c)
#define isgraph_l(c, l)  isgraph(c)
#define islower_l(c, l)  islower(c)
#define isprint_l(c, l)  isprint(c)
#define ispunct_l(c, l)  ispunct(c)
#define isspace_l(c, l)  isspace(c)
#define isupper_l(c, l)  isupper(c)
#define isxdigit_l(c, l) isxdigit(c)
#define tolower_l(c, l)  tolower(c)
#define toupper_l(c, l)  toupper(c)

#ifdef __cplusplus
}
#endif

#endif /* _CTYPE_H */