#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

#include <stdint.h>

/* Common POSIX primitives */
typedef int64_t            ssize_t;
typedef int64_t            off64_t;

/* Process & thread identification */
typedef int32_t            pid_t;
typedef int32_t            tid_t;
typedef uint32_t           uid_t;
typedef uint32_t           gid_t;

/* File system primitives */
#include <sys/stat.h>

/* Time primitives */
#include <time.h>

#ifndef _ID_T_DECLARED
typedef unsigned int id_t;
#define _ID_T_DECLARED
#endif

/* BSD / legacy integer abbreviations */
typedef unsigned char      u_char;
typedef unsigned short     u_short;
typedef unsigned int       u_int;
typedef unsigned long      u_long;
typedef uint8_t            u_int8_t;
typedef uint16_t           u_int16_t;
typedef uint32_t           u_int32_t;
typedef uint64_t           u_int64_t;
typedef char*              caddr_t;
typedef uint32_t           daddr_t;

#endif /* _SYS_TYPES_H */