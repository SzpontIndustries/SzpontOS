/*
 * SzpontOS Libc - BSD <err.h> Header
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _ERR_H
#define _ERR_H

#include <sys/cdefs.h>
#include <stdarg.h>

__BEGIN_DECLS

void warn(const char *fmt, ...) __printflike(1, 2);
void vwarn(const char *fmt, va_list args) __printflike(1, 0);
void warnx(const char *fmt, ...) __printflike(1, 2);
void vwarnx(const char *fmt, va_list args) __printflike(1, 0);

void err(int eval, const char *fmt, ...) __dead2 __printflike(2, 3);
void verr(int eval, const char *fmt, va_list args) __dead2 __printflike(2, 0);
void errx(int eval, const char *fmt, ...) __dead2 __printflike(2, 3);
void verrx(int eval, const char *fmt, va_list args) __dead2 __printflike(2, 0);

__END_DECLS

#endif /* _ERR_H */
