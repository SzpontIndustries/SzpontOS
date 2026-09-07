#include <time.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <utime.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/syscall.h>

time_t time(time_t *tloc) {
    return (time_t)__syscall1(SYS_time, (int64_t)tloc);
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    return (int)__syscall2(SYS_gettimeofday, (int64_t)tv, (int64_t)tz);
}

clock_t clock(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (clock_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (clock_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    return (int)__syscall2(SYS_clock_gettime, (int64_t)clk_id, (int64_t)tp);
}

int clock_settime(clockid_t clk_id, const struct timespec *tp) {
    return (int)__syscall2(SYS_clock_settime, (int64_t)clk_id, (int64_t)tp);
}

int clock_getres(clockid_t clk_id, struct timespec *res) {
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 10000000; /* 10ms */
    }
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return (int)__syscall2(SYS_nanosleep, (int64_t)req, (int64_t)rem);
}

int ftime(struct timeb *tp) {
    if (!tp)
        return -1;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tp->time = tv.tv_sec;
    tp->millitm = tv.tv_usec / 1000;
    tp->timezone = 0;
    tp->dstflag = 0;
    return 0;
}

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    (void)which;
    (void)old_value;
    if (new_value) {
        alarm((unsigned int)new_value->it_value.tv_sec);
    }
    return 0;
}

int getitimer(int which, struct itimerval *curr_value) {
    (void)which;
    if (curr_value) {
        memset(curr_value, 0, sizeof(struct itimerval));
    }
    return 0;
}

int utimes(const char *filename, const struct timeval times[2]) {
    (void)filename;
    (void)times;
    return 0;
}

int utime(const char *filename, const struct utimbuf *times) {
    (void)filename;
    (void)times;
    return 0;
}

static struct tm g_tm;

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    if (!timep || !result)
        return NULL;
    time_t t = *timep;

    result->tm_sec = t % 60;
    t /= 60;
    result->tm_min = t % 60;
    t /= 60;
    result->tm_hour = t % 24;
    time_t days = t / 24;

    result->tm_wday = (days + 4) % 7; /* Jan 1, 1970 was Thursday (4) */

    /* Estimate year */
    long year = 1970;
    while (1) {
        long days_in_year = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (days < days_in_year)
            break;
        days -= days_in_year;
        year++;
    }

    result->tm_year = year - 1900;
    result->tm_yday = days;

    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    int month_days[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 0;
    while (month < 12 && days >= month_days[month]) {
        days -= month_days[month];
        month++;
    }

    result->tm_mon = month;
    result->tm_mday = days + 1;
    result->tm_isdst = 0;

    return result;
}

struct tm *gmtime(const time_t *timep) {
    return gmtime_r(timep, &g_tm);
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    return gmtime_r(timep, result);
}

struct tm *localtime(const time_t *timep) {
    return localtime_r(timep, &g_tm);
}

static char g_asctime_buf[32];

char *asctime_r(const struct tm *tm, char *buf) {
    if (!tm || !buf)
        return NULL;
    static const char wdays[] = "SunMonTueWedThuFriSat";
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

    char wday_str[4] = {wdays[(tm->tm_wday % 7) * 3], wdays[(tm->tm_wday % 7) * 3 + 1],
                        wdays[(tm->tm_wday % 7) * 3 + 2], '\0'};
    char mon_str[4] = {months[(tm->tm_mon % 12) * 3], months[(tm->tm_mon % 12) * 3 + 1],
                       months[(tm->tm_mon % 12) * 3 + 2], '\0'};

    snprintf(buf, 32, "%s %s %2d %02d:%02d:%02d %04d\n", wday_str, mon_str, tm->tm_mday, tm->tm_hour, tm->tm_min,
             tm->tm_sec, 1900 + tm->tm_year);
    return buf;
}

char *asctime(const struct tm *tm) {
    return asctime_r(tm, g_asctime_buf);
}

char *ctime_r(const time_t *timep, char *buf) {
    struct tm t;
    return asctime_r(localtime_r(timep, &t), buf);
}

char *ctime(const time_t *timep) {
    return ctime_r(timep, g_asctime_buf);
}

time_t mktime(struct tm *tm) {
    if (!tm)
        return (time_t)-1;
    time_t year = tm->tm_year + 1900;
    time_t days = 0;
    for (time_t y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    int month_days[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 0; m < tm->tm_mon && m < 12; m++) {
        days += month_days[m];
    }
    days += (tm->tm_mday - 1);
    return ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || !format || !tm || max == 0)
        return 0;
    return snprintf(s, max, "%04d-%02d-%02d %02d:%02d:%02d", 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void tzset(void) {}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}
