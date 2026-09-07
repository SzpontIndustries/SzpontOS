/*
 * SzpontOS - POSIX/Linux/BSD shadow.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SHADOW_H
#define _SHADOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <paths.h>
#include <stddef.h>
#include <stdio.h>

#ifndef _PATH_SHADOW
#define _PATH_SHADOW "/etc/shadow"
#endif

struct spwd {
    char *sp_namp;     /* Login name */
    char *sp_pwdp;     /* Encrypted password */
    long sp_lstchg;    /* Date of last change (days since Jan 1, 1970) */
    long sp_min;       /* Minimum number of days between changes */
    long sp_max;       /* Maximum number of days between changes */
    long sp_warn;      /* Number of days before password expires to warn user */
    long sp_inact;     /* Number of days after password expires until account is disabled */
    long sp_expire;    /* Date when account expires (days since Jan 1, 1970) */
    unsigned long sp_flag; /* Reserved */
};

struct spwd *getspnam(const char *name);
struct spwd *getspent(void);
void setspent(void);
void endspent(void);

#ifdef __cplusplus
}
#endif

#endif /* _SHADOW_H */
