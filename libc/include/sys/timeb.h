/*
 * SzpontOS - POSIX sys/timeb.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

struct timeb {
    time_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};

int ftime(struct timeb *tp);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TIMEB_H */
