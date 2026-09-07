#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>

struct timeval {
    time_t tv_sec;
    int64_t tv_usec;
};

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);
int utimes(const char *filename, const struct timeval times[2]);
int getitimer(int which, struct itimerval *curr_value);
int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);

#define timerclear(tvp)         ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timerisset(tvp)         ((tvp)->tv_sec || (tvp)->tv_usec)

#define timeradd(tvp, uvp, vvp)                                                \
    do {                                                                       \
        (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;                         \
        (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;                     \
        if ((vvp)->tv_usec >= 1000000) {                                       \
            (vvp)->tv_sec++;                                                   \
            (vvp)->tv_usec -= 1000000;                                         \
        }                                                                      \
    } while (0)

#define timersub(tvp, uvp, vvp)                                                \
    do {                                                                       \
        (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;                         \
        (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;                     \
        if ((vvp)->tv_usec < 0) {                                              \
            (vvp)->tv_sec--;                                                   \
            (vvp)->tv_usec += 1000000;                                         \
        }                                                                      \
    } while (0)

#define timercmp(tvp, uvp, cmp)                                                \
    (((tvp)->tv_sec == (uvp)->tv_sec) ?                                        \
        ((tvp)->tv_usec cmp (uvp)->tv_usec) :                                  \
        ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define timespecadd(tsp, usp, vsp)                                             \
    do {                                                                       \
        (vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;                         \
        (vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;                     \
        if ((vsp)->tv_nsec >= 1000000000L) {                                   \
            (vsp)->tv_sec++;                                                   \
            (vsp)->tv_nsec -= 1000000000L;                                     \
        }                                                                      \
    } while (0)

#define timespecsub(tsp, usp, vsp)                                             \
    do {                                                                       \
        (vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;                         \
        (vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;                     \
        if ((vsp)->tv_nsec < 0) {                                              \
            (vsp)->tv_sec--;                                                   \
            (vsp)->tv_nsec += 1000000000L;                                     \
        }                                                                      \
    } while (0)

#define timespeccmp(tsp, usp, cmp)                                             \
    (((tsp)->tv_sec == (usp)->tv_sec) ?                                        \
        ((tsp)->tv_nsec cmp (usp)->tv_nsec) :                                  \
        ((tsp)->tv_sec cmp (usp)->tv_sec))

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TIME_H */
