/*
 * SzpontOS - Real-Time Clock (CMOS RTC) Driver
 * (C) Copyright by Szpont Industries. All rights reserved.
 * Inspired by FreeBSD sys/x86/isa/atrtc.c & OpenBSD dev/isa/atrtc.c
 */

#include <drivers/rtc.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/pit.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>

#define RTC_REG_SECOND 0x00
#define RTC_REG_MINUTE 0x02
#define RTC_REG_HOUR 0x04
#define RTC_REG_DAY_OF_MONTH 0x07
#define RTC_REG_MONTH 0x08
#define RTC_REG_YEAR 0x09
#define RTC_REG_CENTURY 0x32
#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B

static uint64_t g_rtc_boot_epoch_sec = 0;
static uint64_t g_rtc_boot_pit_ticks = 0;
static spinlock_t g_rtc_lock = SPINLOCK_INIT;

static uint8_t cmos_read_register(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, reg);
    io_wait();
    return inb(CMOS_DATA_PORT);
}

static inline bool rtc_is_updating(void) {
    return (cmos_read_register(RTC_REG_STATUS_A) & 0x80) != 0;
}

static inline uint8_t bcd_to_bin(uint8_t val) {
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

static uint64_t datetime_to_epoch(const rtc_time_t *dt) {
    uint32_t year = dt->year;
    uint32_t month = dt->month;
    uint32_t day = dt->day;

    /* Days before each month (standard year) */
    static const uint16_t days_before_month[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    if (year < 1970 || month < 1 || month > 12 || day < 1) {
        return 1771718400ULL; /* Fallback year 2026 */
    }

    uint64_t total_days = 0;

    /* Count days for all completed years since 1970 */
    for (uint32_t y = 1970; y < year; y++) {
        bool is_leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        total_days += is_leap ? 366 : 365;
    }

    /* Add days in current year up to current month */
    total_days += days_before_month[month - 1];

    /* If current year is leap and we are past February, add 1 day */
    bool curr_leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (curr_leap && month > 2) {
        total_days += 1;
    }

    total_days += (day - 1);

    uint64_t total_seconds =
        total_days * 86400ULL + (uint64_t)dt->hour * 3600ULL + (uint64_t)dt->minute * 60ULL + (uint64_t)dt->second;

    return total_seconds;
}

void rtc_read_datetime(rtc_time_t *dt) {
    if (!dt)
        return;

    rtc_time_t last;
    rtc_time_t curr;

    /* Spin until CMOS is not in the middle of an update */
    while (rtc_is_updating()) {
        io_wait();
    }

    /* Read twice until both reads match to avoid race conditions during clock tick */
    while (1) {
        last.second = cmos_read_register(RTC_REG_SECOND);
        last.minute = cmos_read_register(RTC_REG_MINUTE);
        last.hour = cmos_read_register(RTC_REG_HOUR);
        last.day = cmos_read_register(RTC_REG_DAY_OF_MONTH);
        last.month = cmos_read_register(RTC_REG_MONTH);
        last.year = cmos_read_register(RTC_REG_YEAR);

        while (rtc_is_updating()) {
            io_wait();
        }

        curr.second = cmos_read_register(RTC_REG_SECOND);
        curr.minute = cmos_read_register(RTC_REG_MINUTE);
        curr.hour = cmos_read_register(RTC_REG_HOUR);
        curr.day = cmos_read_register(RTC_REG_DAY_OF_MONTH);
        curr.month = cmos_read_register(RTC_REG_MONTH);
        curr.year = cmos_read_register(RTC_REG_YEAR);

        if (last.second == curr.second && last.minute == curr.minute && last.hour == curr.hour &&
            last.day == curr.day && last.month == curr.month && last.year == curr.year) {
            break;
        }
    }

    uint8_t status_b = cmos_read_register(RTC_REG_STATUS_B);
    bool bcd = !(status_b & 0x04); /* Bit 2: 1 = Binary, 0 = BCD */
    bool pm = false;

    /* Handle 12-hour format */
    if (!(status_b & 0x02) && (curr.hour & 0x80)) {
        curr.hour &= 0x7F;
        pm = true;
    }

    /* Convert BCD to Binary if needed */
    if (bcd) {
        curr.second = bcd_to_bin(curr.second);
        curr.minute = bcd_to_bin(curr.minute);
        curr.hour = bcd_to_bin(curr.hour);
        curr.day = bcd_to_bin(curr.day);
        curr.month = bcd_to_bin(curr.month);
        curr.year = bcd_to_bin((uint8_t)curr.year);
    }

    if (pm && curr.hour < 12) {
        curr.hour += 12;
    }

    /* Calculate 4-digit year */
    uint8_t century = cmos_read_register(RTC_REG_CENTURY);
    if (bcd && century != 0) {
        century = bcd_to_bin(century);
    }
    if (century >= 19 && century <= 21) {
        curr.year = century * 100 + curr.year;
    } else {
        curr.year += (curr.year < 70) ? 2000 : 1900;
    }

    *dt = curr;
}

uint64_t g_tsc_freq_hz = 2400000000ULL; /* Default 2.4 GHz fallback */
static uint64_t g_tsc_boot_cycles = 0;



static void calibrate_tsc(void) {
#if defined(__x86_64__)
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    uint32_t max_leaf = 0;

    __asm__ volatile("cpuid" : "=a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));

    /* 1. Try CPUID leaf 0x16 (Processor Frequency Information) */
    if (max_leaf >= 0x16) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x16), "c"(0));
        if (eax > 0 && eax < 10000) {
            g_tsc_freq_hz = (uint64_t)eax * 1000000ULL;
            g_tsc_boot_cycles = rdtsc();
            return;
        }
    }

    /* 2. Try CPUID leaf 0x15 (Nominal Core Crystal Clock) */
    if (max_leaf >= 0x15) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x15), "c"(0));
        if (eax > 0 && ebx > 0 && ecx > 0) {
            uint64_t freq = (uint64_t)ecx * (uint64_t)ebx / (uint64_t)eax;
            if (freq >= 100000000ULL && freq <= 10000000000ULL) {
                g_tsc_freq_hz = freq;
                g_tsc_boot_cycles = rdtsc();
                return;
            }
        }
    }

    /* 3. Hardware PIT Channel 2 (Port 0x42 / 0x61) countdown calibration (10.0 ms) */
    uint8_t orig_port61 = inb(0x61);

    /* Gate 2 low (disable countdown), speaker data off */
    outb(0x61, (orig_port61 & ~0x02) & ~0x01);

    /* Channel 2, LSB/MSB, Mode 0 (one-shot countdown), Binary */
    outb(0x43, 0xB0);
    /* 11932 PIT counts = exactly 10.000 ms (1193182 Hz / 100) */
    outb(0x42, (uint8_t)(11932 & 0xFF));
    outb(0x42, (uint8_t)((11932 >> 8) & 0xFF));

    /* Reset gate 2 high to start counter */
    outb(0x61, (orig_port61 & ~0x02) | 0x01);

    uint64_t start_tsc = rdtsc();

    /* Wait until OUT2 (bit 5 of port 0x61) transitions from 0 to 1 */
    int timeout = 5000000;
    while ((inb(0x61) & 0x20) == 0 && --timeout > 0) {
        __asm__ volatile("pause");
    }

    uint64_t end_tsc = rdtsc();

    /* Restore original port 0x61 state */
    outb(0x61, orig_port61);

    if (timeout > 0 && end_tsc > start_tsc) {
        uint64_t diff = end_tsc - start_tsc;
        uint64_t freq = diff * 100ULL; /* 10 ms * 100 = 1 sec */
        if (freq >= 200000000ULL && freq <= 10000000000ULL) {
            g_tsc_freq_hz = freq;
        } else {
            g_tsc_freq_hz = 2400000000ULL;
        }
    } else {
        g_tsc_freq_hz = 2400000000ULL; /* 2.4 GHz fallback */
    }

    g_tsc_boot_cycles = start_tsc;
#else
    g_tsc_freq_hz = 1000000000ULL;
    g_tsc_boot_cycles = rdtsc();
#endif
}

void rtc_init(void) {
    spinlock_init(&g_rtc_lock);

    rtc_time_t dt;
    rtc_read_datetime(&dt);
    g_rtc_boot_epoch_sec = datetime_to_epoch(&dt);
    g_rtc_boot_pit_ticks = pit_get_ticks();

    calibrate_tsc();

    klog_info("RTC initialized: %04u-%02u-%02u %02u:%02u:%02u UTC (Epoch: %llu)", dt.year, dt.month, dt.day, dt.hour,
              dt.minute, dt.second, (unsigned long long)g_rtc_boot_epoch_sec);
    klog_info("TSC frequency calibrated: %llu MHz", (unsigned long long)(g_tsc_freq_hz / 1000000ULL));
}

uint64_t rtc_get_boot_epoch(void) {
    return g_rtc_boot_epoch_sec;
}

uint64_t rtc_get_current_epoch(void) {
    uint64_t now_tsc = rdtsc();
    uint64_t elapsed_cycles = (now_tsc >= g_tsc_boot_cycles) ? (now_tsc - g_tsc_boot_cycles) : 0;
    return g_rtc_boot_epoch_sec + (elapsed_cycles / g_tsc_freq_hz);
}

void rtc_get_timeval(struct timeval_kernel *tv) {
    if (!tv)
        return;
    uint64_t now_tsc = rdtsc();
    uint64_t elapsed_cycles = (now_tsc >= g_tsc_boot_cycles) ? (now_tsc - g_tsc_boot_cycles) : 0;

    tv->tv_sec = (int64_t)(g_rtc_boot_epoch_sec + (elapsed_cycles / g_tsc_freq_hz));
    tv->tv_usec = (int64_t)(((elapsed_cycles % g_tsc_freq_hz) * 1000000ULL) / g_tsc_freq_hz);
}

void rtc_get_timespec(struct timespec_kernel *ts) {
    if (!ts)
        return;
    uint64_t now_tsc = rdtsc();
    uint64_t elapsed_cycles = (now_tsc >= g_tsc_boot_cycles) ? (now_tsc - g_tsc_boot_cycles) : 0;

    ts->tv_sec = (int64_t)(g_rtc_boot_epoch_sec + (elapsed_cycles / g_tsc_freq_hz));
    ts->tv_nsec = (int64_t)(((elapsed_cycles % g_tsc_freq_hz) * 1000000000ULL) / g_tsc_freq_hz);
}

void rtc_get_monotonic(struct timespec_kernel *ts) {
    if (!ts)
        return;
    uint64_t now_tsc = rdtsc();
    uint64_t elapsed_cycles = (now_tsc >= g_tsc_boot_cycles) ? (now_tsc - g_tsc_boot_cycles) : 0;

    ts->tv_sec = (int64_t)(elapsed_cycles / g_tsc_freq_hz);
    ts->tv_nsec = (int64_t)(((elapsed_cycles % g_tsc_freq_hz) * 1000000000ULL) / g_tsc_freq_hz);
}

uint64_t rtc_get_monotonic_ns(void) {
    struct timespec_kernel ts;
    rtc_get_monotonic(&ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

