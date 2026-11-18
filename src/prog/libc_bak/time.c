#include <sys/time.h>
#include <time.h>
#include <errno.h>

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (!tv) {
        return 0;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        return -1;
    }

    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;

    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }

    return 0;
}

int getitimer(int which, struct itimerval *curr_value) {
    (void)which;
    if (!curr_value) {
        errno = EFAULT;
        return -1;
    }
    timerclear(&curr_value->it_interval);
    timerclear(&curr_value->it_value);
    return 0;
}

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    (void)which;
    (void)new_value;
    if (old_value) {
        timerclear(&old_value->it_interval);
        timerclear(&old_value->it_value);
    }
    return 0;
}