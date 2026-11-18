#ifndef _STDARG_H
#define _STDARG_H

#if defined(__GNUC__) || defined(__clang__)

/* Use compiler built-in types and primitives */
typedef __builtin_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)
#define va_copy(d, s)  __builtin_va_copy(d, s)

#else

/* Manual System V AMD64 ABI structure layout */
typedef struct {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} va_list[1];

#error "Unsupported compiler: stdarg.h requires GCC or Clang compiler intrinsics on x86_64"

#endif

#endif /* _STDARG_H */