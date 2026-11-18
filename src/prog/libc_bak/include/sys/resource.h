#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H 1

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type used for resource limits (64-bit unsigned under LP64) */
typedef uint64_t rlim_t;

#define RLIM_INFINITY  (~0ULL)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY

/* Resource limit container */
struct rlimit {
    rlim_t rlim_cur; /* Soft limit */
    rlim_t rlim_max; /* Hard limit (ceiling for rlim_cur) */
};

/* Resource usage accounting structure */
struct rusage {
    struct timeval ru_utime; /* User CPU time used */
    struct timeval ru_stime; /* System CPU time used */
    long   ru_maxrss;        /* Maximum resident set size (in kilobytes) */
    long   ru_ixrss;         /* Integral shared memory size (unmaintained) */
    long   ru_idrss;         /* Integral unshared data size (unmaintained) */
    long   ru_isrss;         /* Integral unshared stack size (unmaintained) */
    long   ru_minflt;        /* Page reclaims (soft page faults) */
    long   ru_majflt;        /* Page faults (hard page faults, required I/O) */
    long   ru_nswap;         /* Swaps */
    long   ru_inblock;       /* Block input operations */
    long   ru_oublock;       /* Block output operations */
    long   ru_msgsnd;        /* IPC messages sent */
    long   ru_msgrcv;        /* IPC messages received */
    long   ru_nsignals;      /* Signals delivered */
    long   ru_nvcsw;         /* Voluntary context switches */
    long   ru_nivcsw;        /* Involuntary context switches */
};

/* Target options for getrusage() */
#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

/* --------------------------------------------------------------------------
 * Resource limit constants (Linux x86_64 matching order)
 * -------------------------------------------------------------------------- */
#define RLIMIT_CPU        0  /* CPU time in seconds */
#define RLIMIT_FSIZE      1  /* Maximum filesize in bytes */
#define RLIMIT_DATA       2  /* Max data size */
#define RLIMIT_STACK      3  /* Max stack size */
#define RLIMIT_CORE       4  /* Max core file size */
#define RLIMIT_RSS        5  /* Max resident set size */
#define RLIMIT_NPROC      6  /* Max number of processes */
#define RLIMIT_NOFILE     7  /* Max number of open files */
#define RLIMIT_MEMLOCK    8  /* Max locked-in-memory address space */
#define RLIMIT_AS         9  /* Address space limit */
#define RLIMIT_LOCKS      10 /* Maximum file locks held */
#define RLIMIT_SIGPENDING 11 /* Max number of pending signals */
#define RLIMIT_MSGQUEUE   12 /* Maximum bytes in POSIX message queues */
#define RLIMIT_NICE       13 /* Max nice priority allowed to raise to */
#define RLIMIT_RTPRIO     14 /* Maximum real-time priority */
#define RLIMIT_RTTIME     15 /* Timeout for real-time tasks */
#define RLIM_NLIMITS      16

/* Targets for getpriority / setpriority */
#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

/* Function declarations */
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);

int getrusage(int who, struct rusage *usage);

int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_RESOURCE_H */