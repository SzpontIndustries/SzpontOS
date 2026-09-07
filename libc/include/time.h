#ifndef _TIME_H
#define _TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef int64_t time_t;
typedef int64_t clock_t;
typedef int clockid_t;

#define CLOCKS_PER_SEC 1000000L

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#define CLOCK_UPTIME CLOCK_MONOTONIC
#define CLOCK_UPTIME_PRECISE CLOCK_MONOTONIC
#define CLOCK_UPTIME_FAST CLOCK_MONOTONIC
#define CLOCK_MONOTONIC_PRECISE CLOCK_MONOTONIC
#define CLOCK_MONOTONIC_FAST CLOCK_MONOTONIC
#define CLOCK_REALTIME_PRECISE CLOCK_REALTIME
#define CLOCK_REALTIME_FAST CLOCK_REALTIME

struct tm {
    int tm_sec;   /* Seconds (0-60) */
    int tm_min;   /* Minutes (0-59) */
    int tm_hour;  /* Hours (0-23) */
    int tm_mday;  /* Day of the month (1-31) */
    int tm_mon;   /* Month (0-11) */
    int tm_year;  /* Year since 1900 */
    int tm_wday;  /* Day of the week (0-6, Sunday = 0) */
    int tm_yday;  /* Day in the year (0-365) */
    int tm_isdst; /* Daylight saving time flag */
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

time_t time(time_t *tloc);
struct tm *gmtime(const time_t *timep);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
char *asctime(const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
char *ctime(const time_t *timep);
char *ctime_r(const time_t *timep, char *buf);
time_t mktime(struct tm *tm);
double difftime(time_t time1, time_t time0);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
void tzset(void);
clock_t clock(void);

int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_settime(clockid_t clk_id, const struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);
int nanosleep(const struct timespec *req, struct timespec *rem);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */
