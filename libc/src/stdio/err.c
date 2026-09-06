/*
 * SzpontOS Libc - BSD Error and Warning Functions (<err.h>)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void vwarn(const char *fmt, va_list args) {
    int saved_errno = errno;
    const char *prog = getprogname();
    if (prog && *prog) {
        fprintf(stderr, "%s: ", prog);
    }
    if (fmt != NULL) {
        vfprintf(stderr, fmt, args);
        fprintf(stderr, ": %s\n", strerror(saved_errno));
    } else {
        fprintf(stderr, "%s\n", strerror(saved_errno));
    }
}

void vwarnx(const char *fmt, va_list args) {
    const char *prog = getprogname();
    if (prog && *prog) {
        fprintf(stderr, "%s: ", prog);
    }
    if (fmt != NULL) {
        vfprintf(stderr, fmt, args);
    }
    fprintf(stderr, "\n");
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void warnx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

void verr(int eval, const char *fmt, va_list args) {
    vwarn(fmt, args);
    exit(eval);
}

void verrx(int eval, const char *fmt, va_list args) {
    vwarnx(fmt, args);
    exit(eval);
}

void err(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verr(eval, fmt, ap);
    va_end(ap);
}

void errx(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verrx(eval, fmt, ap);
    va_end(ap);
}
