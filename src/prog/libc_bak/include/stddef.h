#ifndef _STDDEF_H
#define _STDDEF_H

/* NULL Pointer Constant */
#ifndef NULL
    #if defined(__cplusplus)
        #define NULL 0
    #else
        #define NULL ((void *)0)
    #endif
#endif

/* Unsigned integer type of the result of the sizeof operator */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
    #if defined(__SIZE_TYPE__)
        typedef __SIZE_TYPE__ size_t;
    #else
        typedef unsigned long long size_t;
    #endif
#endif

/* Signed integer type of the result of subtracting two pointers */
#ifndef _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
    #if defined(__PTRDIFF_TYPE__)
        typedef __PTRDIFF_TYPE__ ptrdiff_t;
    #else
        typedef long long ptrdiff_t;
    #endif
#endif

/* Integer type whose range of values can represent distinct wide character codes */
#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
    #if defined(__WCHAR_TYPE__)
        typedef __WCHAR_TYPE__ wchar_t;
    #else
        typedef int wchar_t;
    #endif
#endif

/* Type whose alignment requirement is at least as strict as that of every scalar type */
#ifndef _MAX_ALIGN_T_DEFINED
#define _MAX_ALIGN_T_DEFINED
typedef struct {
    long long __ll __attribute__((__aligned__(__alignof__(long long))));
    long double __ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;
#endif

/* Offset of member MEMBER in a struct of type TYPE */
#ifndef offsetof
    #if defined(__GNUC__) || defined(__clang__)
        #define offsetof(type, member) __builtin_offsetof(type, member)
    #else
        #define offsetof(type, member) ((size_t)&(((type *)0)->member))
    #endif
#endif

#endif /* _STDDEF_H */