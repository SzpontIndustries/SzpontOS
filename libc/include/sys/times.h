/*
 * SzpontOS - POSIX sys/times.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

struct tms {
    clock_t tms_utime;  /* User CPU time */
    clock_t tms_stime;  /* System CPU time */
    clock_t tms_cutime; /* User CPU time of terminated children */
    clock_t tms_cstime; /* System CPU time of terminated children */
};

clock_t times(struct tms *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TIMES_H */
