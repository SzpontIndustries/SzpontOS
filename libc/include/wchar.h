/*
 * SzpontOS - POSIX wchar.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _WCHAR_H
#define _WCHAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

struct tm;

#ifndef WEOF
#define WEOF ((wint_t) - 1)
#endif

#ifndef _WCHAR_T_DECLARED
#define _WCHAR_T_DECLARED
#ifndef __cplusplus
typedef unsigned int wchar_t;
#endif
#endif

#ifndef _WINT_T_DECLARED
#define _WINT_T_DECLARED
typedef unsigned int wint_t;
#endif

#ifndef _MBSTATE_T_DECLARED
#define _MBSTATE_T_DECLARED
typedef struct {
    int count;
    unsigned int value;
} mbstate_t;
#endif

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
int mbsinit(const mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dest, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dest, const wchar_t **src, size_t len, mbstate_t *ps);
size_t mbstowcs(wchar_t *dest, const char *src, size_t n);
size_t wcstombs(char *dest, const wchar_t *src, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);
int mblen(const char *s, size_t n);

int wcwidth(wchar_t c);
int wcswidth(const wchar_t *pwcs, size_t n);

size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wcscat(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
int wcscoll(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept);
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle);

wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);

long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base);
float wcstof(const wchar_t *nptr, wchar_t **endptr);
double wcstod(const wchar_t *nptr, wchar_t **endptr);
long double wcstold(const wchar_t *nptr, wchar_t **endptr);

wint_t btowc(int c);
int wctob(wint_t c);
wint_t fgetwc(FILE *stream);
wchar_t *fgetws(wchar_t *ws, int n, FILE *stream);
wint_t fputwc(wchar_t wc, FILE *stream);
int fputws(const wchar_t *ws, FILE *stream);
int fwide(FILE *stream, int mode);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t putwc(wchar_t wc, FILE *stream);
wint_t putwchar(wchar_t wc);
wint_t ungetwc(wint_t wc, FILE *stream);
int fwprintf(FILE *stream, const wchar_t *format, ...);
int fwscanf(FILE *stream, const wchar_t *format, ...);
int swprintf(wchar_t *ws, size_t n, const wchar_t *format, ...);
int swscanf(const wchar_t *ws, const wchar_t *format, ...);
int vfwprintf(FILE *stream, const wchar_t *format, va_list arg);
int vswprintf(wchar_t *ws, size_t n, const wchar_t *format, va_list arg);
int vwprintf(const wchar_t *format, va_list arg);
int wprintf(const wchar_t *format, ...);
int wscanf(const wchar_t *format, ...);
size_t wcscspn(const wchar_t *s1, const wchar_t *s2);
size_t wcsspn(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcstok(wchar_t *ws1, const wchar_t *ws2, wchar_t **ptr);
size_t wcsxfrm(wchar_t *ws1, const wchar_t *ws2, size_t n);
size_t wcsftime(wchar_t *wcs, size_t maxsize, const wchar_t *format, const struct tm *timeptr);

#ifdef __cplusplus
}
#endif

#endif /* _WCHAR_H */
