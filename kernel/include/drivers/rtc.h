/*
 * SzpontOS - Real-Time Clock (CMOS RTC) Driver Header
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef SZPONTOS_DRIVERS_RTC_H
#define SZPONTOS_DRIVERS_RTC_H

#include <kernel/types.h>

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT 0x71

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;

struct timeval_kernel {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timespec_kernel {
    int64_t tv_sec;
    int64_t tv_nsec;
};

void rtc_init(void);
void rtc_read_datetime(rtc_time_t *dt);
uint64_t rtc_get_boot_epoch(void);
uint64_t rtc_get_current_epoch(void);
void rtc_get_timeval(struct timeval_kernel *tv);
void rtc_get_timespec(struct timespec_kernel *ts);
void rtc_get_monotonic(struct timespec_kernel *ts);
uint64_t rtc_get_monotonic_ns(void);

#endif /* SZPONTOS_DRIVERS_RTC_H */
