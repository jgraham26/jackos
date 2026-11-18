#ifndef _TIME_H
#define _TIME_H 1

#include <stddef.h> /* For NULL, size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Fundamental POSIX time types */
typedef long           time_t;
typedef long           suseconds_t;
typedef long           clock_t;
typedef int            clockid_t;
typedef void*          timer_t;

/* Standard clock IDs for clock_gettime */
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3

/* Clocks per second for clock() */
#define CLOCKS_PER_SEC ((clock_t)1000000)

/* POSIX timespec structure */
struct timespec {
    time_t tv_sec;  /* Seconds */
    long   tv_nsec; /* Nanoseconds [0, 999999999] */
};

/* Timeval structure used by select/gettimeofday */
#ifndef _STRUCT_TIMEVAL_DECLARED
#define _STRUCT_TIMEVAL_DECLARED
struct timeval {
    time_t      tv_sec;  /* Seconds */
    suseconds_t tv_usec; /* Microseconds */
};
#endif


/* Broken-down time structure */
struct tm {
    int tm_sec;    /* Seconds [0, 60] */
    int tm_min;    /* Minutes [0, 59] */
    int tm_hour;   /* Hours [0, 23] */
    int tm_mday;   /* Day of month [1, 31] */
    int tm_mon;    /* Month [0, 11] */
    int tm_year;   /* Years since 1900 */
    int tm_wday;   /* Day of week [0, 6] (Sunday = 0) */
    int tm_yday;   /* Day of year [0, 365] */
    int tm_isdst;  /* Daylight saving flag (>0: DST, 0: not DST, <0: unknown) */
    long tm_gmtoff;/* Offset from UTC in seconds (POSIX/GNU extension) */
    const char *tm_zone; /* Timezone abbreviation (POSIX/GNU extension) */
};

/* Minimal prototypes needed by GCC/libgcc and basic runtimes */
clock_t clock(void);
time_t  time(time_t *tloc);
double  difftime(time_t time1, time_t time0);
time_t  mktime(struct tm *tm);

struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
struct tm *gmtime_r(const time_t *timer, struct tm *result);
struct tm *localtime_r(const time_t *timer, struct tm *result);

char *asctime(const struct tm *tm);
char *ctime(const time_t *timer);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);
int nanosleep(const struct timespec *req, struct timespec *rem);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */