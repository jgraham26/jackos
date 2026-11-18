#ifndef _SYS_TIME_H
#define _SYS_TIME_H 1

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ensure struct timeval is defined (also defined in time.h) */
#ifndef _STRUCT_TIMEVAL_DECLARED
#define _STRUCT_TIMEVAL_DECLARED
struct timeval {
    time_t      tv_sec;  /* Seconds */
    suseconds_t tv_usec; /* Microseconds */
};
#endif

/* Timezone structure for gettimeofday (legacy/obsolete in POSIX, but required by libbb) */
struct timezone {
    int tz_minuteswest; /* Minutes west of Greenwich */
    int tz_dsttime;     /* Type of DST correction */
};

/* Interval timer structure */
struct itimerval {
    struct timeval it_interval; /* Timer interval */
    struct timeval it_value;    /* Current value */
};

/* Interval timer kinds */
#define ITIMER_REAL    0 /* Decrements in real time; delivers SIGALRM */
#define ITIMER_VIRTUAL 1 /* Decrements only when process executes; delivers SIGVTALRM */
#define ITIMER_PROF    2 /* Decrements both when process executes and on its behalf; delivers SIGPROF */

/* Convenience macros for manipulating struct timeval */
#define timerisset(tvp)         ((tvp)->tv_sec || (tvp)->tv_usec)
#define timerclear(tvp)         ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timercmp(a, b, CMP) \
    (((a)->tv_sec == (b)->tv_sec) ? \
     ((a)->tv_usec CMP (b)->tv_usec) : \
     ((a)->tv_sec CMP (b)->tv_sec))

#define timeradd(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec; \
        (result)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
        if ((result)->tv_usec >= 1000000) { \
            (result)->tv_sec++; \
            (result)->tv_usec -= 1000000; \
        } \
    } while (0)

#define timersub(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((result)->tv_usec < 0) { \
            (result)->tv_sec--; \
            (result)->tv_usec += 1000000; \
        } \
    } while (0)

/* Standard function declarations */
int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);

int getitimer(int which, struct itimerval *curr_value);
int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);

int utimes(const char *filename, const struct timeval times[2]);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TIME_H */