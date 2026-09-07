/*
 * SzpontOS - Wide Character and Multibyte Functions
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <string.h>

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps) {
    (void)ps;
    if (!s)
        return 0;
    if (n == 0)
        return (size_t)-2;

    unsigned char c = (unsigned char)*s;
    if (c == 0) {
        if (pwc)
            *pwc = 0;
        return 0;
    }

    if (c < 0x80) {
        if (pwc)
            *pwc = (wchar_t)c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        if (n < 2)
            return (size_t)-2;
        if (pwc)
            *pwc = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    } else if ((c & 0xF0) == 0xE0) {
        if (n < 3)
            return (size_t)-2;
        if (pwc)
            *pwc = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    } else if ((c & 0xF8) == 0xF0) {
        if (n < 4)
            return (size_t)-2;
        if (pwc)
            *pwc = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }

    if (pwc)
        *pwc = (wchar_t)c;
    return 1;
}

size_t mbrlen(const char *s, size_t n, mbstate_t *ps) {
    return mbrtowc(NULL, s, n, ps);
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
    (void)ps;
    if (!s)
        return 1;

    if (wc < 0x80) {
        s[0] = (char)wc;
        return 1;
    } else if (wc < 0x800) {
        s[0] = (char)(0xC0 | (wc >> 6));
        s[1] = (char)(0x80 | (wc & 0x3F));
        return 2;
    } else if (wc < 0x10000) {
        s[0] = (char)(0xE0 | (wc >> 12));
        s[1] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[2] = (char)(0x80 | (wc & 0x3F));
        return 3;
    } else {
        s[0] = (char)(0xF0 | (wc >> 18));
        s[1] = (char)(0x80 | ((wc >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[3] = (char)(0x80 | (wc & 0x3F));
        return 4;
    }
}

int mbsinit(const mbstate_t *ps) {
    return (ps == NULL || ps->count == 0);
}

size_t mbsrtowcs(wchar_t *dest, const char **src, size_t len, mbstate_t *ps) {
    (void)ps;
    if (!src || !*src)
        return 0;
    const char *s = *src;
    size_t count = 0;
    while (*s && (!dest || count < len)) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, s, 4, NULL);
        if (n == (size_t)-1 || n == (size_t)-2)
            return (size_t)-1;
        if (n == 0)
            break;
        if (dest)
            dest[count] = wc;
        s += n;
        count++;
    }
    if (dest && count < len)
        dest[count] = 0;
    if (*s == 0)
        *src = NULL;
    else
        *src = s;
    return count;
}

size_t wcsrtombs(char *dest, const wchar_t **src, size_t len, mbstate_t *ps) {
    (void)ps;
    if (!src || !*src)
        return 0;
    const wchar_t *w = *src;
    size_t count = 0;
    while (*w) {
        char buf[8];
        size_t n = wcrtomb(buf, *w, NULL);
        if (dest) {
            if (count + n > len)
                break;
            memcpy(dest + count, buf, n);
        }
        count += n;
        w++;
    }
    if (dest && count < len)
        dest[count] = '\0';
    if (*w == 0)
        *src = NULL;
    else
        *src = w;
    return count;
}

int mbtowc(wchar_t *pwc, const char *s, size_t n) {
    return (int)mbrtowc(pwc, s, n, NULL);
}

int wctomb(char *s, wchar_t wc) {
    return (int)wcrtomb(s, wc, NULL);
}

int mblen(const char *s, size_t n) {
    return (int)mbrtowc(NULL, s, n, NULL);
}

size_t mbstowcs(wchar_t *dest, const char *src, size_t n) {
    if (!src)
        return 0;
    size_t count = 0;
    while (*src && (!dest || count < n)) {
        wchar_t wc;
        size_t len = mbrtowc(&wc, src, 4, NULL);
        if (len == (size_t)-1 || len == (size_t)-2)
            return (size_t)-1;
        if (len == 0)
            break;
        if (dest)
            dest[count] = wc;
        src += len;
        count++;
    }
    if (dest && count < n)
        dest[count] = 0;
    return count;
}

size_t wcstombs(char *dest, const wchar_t *src, size_t n) {
    if (!src)
        return 0;
    size_t count = 0;
    while (*src) {
        char buf[8];
        size_t len = wcrtomb(buf, *src, NULL);
        if (dest) {
            if (count + len > n)
                break;
            memcpy(dest + count, buf, len);
        }
        count += len;
        src++;
    }
    if (dest && count < n)
        dest[count] = '\0';
    return count;
}

int wcwidth(wchar_t c) {
    if (c == 0)
        return 0;
    if (c < 32 || (c >= 0x7f && c < 0xa0))
        return -1;
    if (c >= 0x1100 && (c <= 0x115f || c == 0x2329 || c == 0x232a || (c >= 0x2e80 && c <= 0xa4cf && c != 0x303f) ||
                        (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe10 && c <= 0xfe19) ||
                        (c >= 0xfe30 && c <= 0xfe6f) || (c >= 0xff00 && c <= 0xff60) || (c >= 0xffe0 && c <= 0xffe6))) {
        return 2;
    }
    return 1;
}

int wcswidth(const wchar_t *pwcs, size_t n) {
    int width = 0;
    for (size_t i = 0; i < n && pwcs[i]; i++) {
        int w = wcwidth(pwcs[i]);
        if (w < 0)
            return -1;
        width += w;
    }
    return width;
}

size_t wcslen(const wchar_t *s) {
    size_t len = 0;
    while (s && s[len])
        len++;
    return len;
}

wchar_t *wcscpy(wchar_t *dest, const wchar_t *src) {
    wchar_t *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = 0;
    return dest;
}

wchar_t *wcscat(wchar_t *dest, const wchar_t *src) {
    wchar_t *d = dest;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n) {
    wchar_t *d = dest;
    while (*d)
        d++;
    size_t i = 0;
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    *d = 0;
    return dest;
}

int wcscmp(const wchar_t *s1, const wchar_t *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const wint_t *)s1 - *(const wint_t *)s2;
}

int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return (int)(s1[i] - s2[i]);
        if (!s1[i])
            break;
    }
    return 0;
}

int wcscoll(const wchar_t *s1, const wchar_t *s2) {
    return wcscmp(s1, s2);
}

wchar_t *wcschr(const wchar_t *s, wchar_t c) {
    while (*s) {
        if (*s == c)
            return (wchar_t *)s;
        s++;
    }
    return (c == 0) ? (wchar_t *)s : NULL;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
    const wchar_t *last = NULL;
    while (*s) {
        if (*s == c)
            last = s;
        s++;
    }
    return (c == 0) ? (wchar_t *)s : (wchar_t *)last;
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept) {
    while (*s) {
        if (wcschr(accept, *s))
            return (wchar_t *)s;
        s++;
    }
    return NULL;
}

wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle) {
    if (!*needle)
        return (wchar_t *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const wchar_t *h = haystack, *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n)
                return (wchar_t *)haystack;
        }
    }
    return NULL;
}

int iswalnum(wint_t wc) {
    return (wc < 128) ? isalnum((int)wc) : 1;
}
int iswalpha(wint_t wc) {
    return (wc < 128) ? isalpha((int)wc) : 1;
}
int iswblank(wint_t wc) {
    return (wc == ' ' || wc == '\t');
}
int iswcntrl(wint_t wc) {
    return (wc < 32 || wc == 127);
}
int iswdigit(wint_t wc) {
    return (wc >= '0' && wc <= '9');
}
int iswgraph(wint_t wc) {
    return (wc > 32 && wc < 127);
}
int iswlower(wint_t wc) {
    return (wc >= 'a' && wc <= 'z');
}
int iswprint(wint_t wc) {
    return (wc >= 32 && wc != 127);
}
int iswpunct(wint_t wc) {
    return (wc < 128) ? ispunct((int)wc) : 0;
}
int iswspace(wint_t wc) {
    return (wc < 128) ? isspace((int)wc) : 0;
}
int iswupper(wint_t wc) {
    return (wc >= 'A' && wc <= 'Z');
}
int iswxdigit(wint_t wc) {
    return (wc < 128) ? isxdigit((int)wc) : 0;
}

wint_t towlower(wint_t wc) {
    return (wc >= 'A' && wc <= 'Z') ? (wc + ('a' - 'A')) : wc;
}
wint_t towupper(wint_t wc) {
    return (wc >= 'a' && wc <= 'z') ? (wc - ('a' - 'A')) : wc;
}

wctype_t wctype(const char *property) {
    if (!strcmp(property, "alnum"))
        return 1;
    if (!strcmp(property, "alpha"))
        return 2;
    if (!strcmp(property, "blank"))
        return 3;
    if (!strcmp(property, "cntrl"))
        return 4;
    if (!strcmp(property, "digit"))
        return 5;
    if (!strcmp(property, "graph"))
        return 6;
    if (!strcmp(property, "lower"))
        return 7;
    if (!strcmp(property, "print"))
        return 8;
    if (!strcmp(property, "punct"))
        return 9;
    if (!strcmp(property, "space"))
        return 10;
    if (!strcmp(property, "upper"))
        return 11;
    if (!strcmp(property, "xdigit"))
        return 12;
    return 0;
}

int iswctype(wint_t wc, wctype_t desc) {
    switch (desc) {
    case 1:
        return iswalnum(wc);
    case 2:
        return iswalpha(wc);
    case 3:
        return iswblank(wc);
    case 4:
        return iswcntrl(wc);
    case 5:
        return iswdigit(wc);
    case 6:
        return iswgraph(wc);
    case 7:
        return iswlower(wc);
    case 8:
        return iswprint(wc);
    case 9:
        return iswpunct(wc);
    case 10:
        return iswspace(wc);
    case 11:
        return iswupper(wc);
    case 12:
        return iswxdigit(wc);
    default:
        return 0;
    }
}

wctrans_t wctrans(const char *property) {
    if (!property) return 0;
    if (!strcmp(property, "tolower")) return (wctrans_t)1;
    if (!strcmp(property, "toupper")) return (wctrans_t)2;
    return 0;
}

wint_t towctrans(wint_t wc, wctrans_t desc) {
    if ((uintptr_t)desc == 1) return towlower(wc);
    if ((uintptr_t)desc == 2) return towupper(wc);
    return wc;
}

wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, size_t n) {
    return (wchar_t *)memcpy(dest, src, n * sizeof(wchar_t));
}

wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, size_t n) {
    return (wchar_t *)memmove(dest, src, n * sizeof(wchar_t));
}

wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        s[i] = c;
    }
    return s;
}

int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (s1[i] > s2[i]) ? 1 : -1;
        }
    }
    return 0;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == c)
            return (wchar_t *)(s + i);
    }
    return NULL;
}

long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base) {
    const wchar_t *s = nptr;
    while (iswspace(*s)) s++;
    int sign = 1;
    if (*s == L'-') {
        sign = -1;
        s++;
    } else if (*s == L'+') {
        s++;
    }
    if (base == 0) {
        if (*s == L'0') {
            if (s[1] == L'x' || s[1] == L'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        s += 2;
    }
    long long acc = 0;
    const wchar_t *start = s;
    while (*s) {
        int val = -1;
        if (*s >= L'0' && *s <= L'9') val = *s - L'0';
        else if (*s >= L'a' && *s <= L'z') val = *s - L'a' + 10;
        else if (*s >= L'A' && *s <= L'Z') val = *s - L'A' + 10;
        if (val < 0 || val >= base) break;
        acc = acc * base + val;
        s++;
    }
    if (endptr) *endptr = (wchar_t *)((s == start) ? nptr : s);
    return sign * acc;
}

unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base) {
    const wchar_t *s = nptr;
    while (iswspace(*s)) s++;
    if (*s == L'+') s++;
    if (base == 0) {
        if (*s == L'0') {
            if (s[1] == L'x' || s[1] == L'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        s += 2;
    }
    unsigned long long acc = 0;
    const wchar_t *start = s;
    while (*s) {
        int val = -1;
        if (*s >= L'0' && *s <= L'9') val = *s - L'0';
        else if (*s >= L'a' && *s <= L'z') val = *s - L'a' + 10;
        else if (*s >= L'A' && *s <= L'Z') val = *s - L'A' + 10;
        if (val < 0 || val >= base) break;
        acc = acc * base + val;
        s++;
    }
    if (endptr) *endptr = (wchar_t *)((s == start) ? nptr : s);
    return acc;
}

long wcstol(const wchar_t *nptr, wchar_t **endptr, int base) {
    return (long)wcstoll(nptr, endptr, base);
}

unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base) {
    return (unsigned long)wcstoull(nptr, endptr, base);
}

extern double strtod(const char *nptr, char **endptr);

double wcstod(const wchar_t *nptr, wchar_t **endptr) {
    char buf[128];
    size_t i = 0;
    const wchar_t *s = nptr;
    while (iswspace(*s)) s++;
    while (*s && i < sizeof(buf) - 1) {
        if (*s > 127) break;
        buf[i++] = (char)*s++;
    }
    buf[i] = '\0';
    char *c_end = NULL;
    double val = strtod(buf, &c_end);
    if (endptr) {
        *endptr = (wchar_t *)(nptr + (c_end ? (c_end - buf) : 0));
    }
    return val;
}

float wcstof(const wchar_t *nptr, wchar_t **endptr) {
    return (float)wcstod(nptr, endptr);
}

long double wcstold(const wchar_t *nptr, wchar_t **endptr) {
    return (long double)wcstod(nptr, endptr);
}

wint_t btowc(int c) {
    if (c == EOF) return WEOF;
    return (wint_t)(unsigned char)c;
}

int wctob(wint_t c) {
    if (c == WEOF || c > 127) return EOF;
    return (int)c;
}

wint_t fgetwc(FILE *stream) {
    int c = fgetc(stream);
    if (c == EOF) return WEOF;
    return (wint_t)(unsigned char)c;
}

wchar_t *fgetws(wchar_t *ws, int n, FILE *stream) {
    if (!ws || n <= 0) return NULL;
    int i = 0;
    while (i < n - 1) {
        wint_t wc = fgetwc(stream);
        if (wc == WEOF) break;
        ws[i++] = (wchar_t)wc;
        if (wc == L'\n') break;
    }
    if (i == 0) return NULL;
    ws[i] = L'\0';
    return ws;
}

wint_t fputwc(wchar_t wc, FILE *stream) {
    if (fputc((int)(wc & 0xFF), stream) == EOF) return WEOF;
    return wc;
}

int fputws(const wchar_t *ws, FILE *stream) {
    if (!ws) return -1;
    while (*ws) {
        if (fputwc(*ws++, stream) == WEOF) return -1;
    }
    return 0;
}

int fwide(FILE *stream, int mode) {
    (void)stream;
    return mode;
}

wint_t getwc(FILE *stream) {
    return fgetwc(stream);
}

wint_t getwchar(void) {
    return fgetwc(stdin);
}

wint_t putwc(wchar_t wc, FILE *stream) {
    return fputwc(wc, stream);
}

wint_t putwchar(wchar_t wc) {
    return fputwc(wc, stdout);
}

wint_t ungetwc(wint_t wc, FILE *stream) {
    if (wc == WEOF) return WEOF;
    return (ungetc((int)(wc & 0xFF), stream) == EOF) ? WEOF : wc;
}

int fwprintf(FILE *stream, const wchar_t *format, ...) {
    (void)stream; (void)format;
    return 0;
}

int fwscanf(FILE *stream, const wchar_t *format, ...) {
    (void)stream; (void)format;
    return 0;
}

int swprintf(wchar_t *ws, size_t n, const wchar_t *format, ...) {
    (void)ws; (void)n; (void)format;
    return 0;
}

int swscanf(const wchar_t *ws, const wchar_t *format, ...) {
    (void)ws; (void)format;
    return 0;
}

int vfwprintf(FILE *stream, const wchar_t *format, va_list arg) {
    (void)stream; (void)format; (void)arg;
    return 0;
}

int vswprintf(wchar_t *ws, size_t n, const wchar_t *format, va_list arg) {
    (void)ws; (void)n; (void)format; (void)arg;
    return 0;
}

int vwprintf(const wchar_t *format, va_list arg) {
    (void)format; (void)arg;
    return 0;
}

int wprintf(const wchar_t *format, ...) {
    (void)format;
    return 0;
}

int wscanf(const wchar_t *format, ...) {
    (void)format;
    return 0;
}

size_t wcscspn(const wchar_t *s1, const wchar_t *s2) {
    size_t count = 0;
    while (*s1) {
        if (wcschr(s2, *s1)) return count;
        s1++;
        count++;
    }
    return count;
}

size_t wcsspn(const wchar_t *s1, const wchar_t *s2) {
    size_t count = 0;
    while (*s1) {
        if (!wcschr(s2, *s1)) return count;
        s1++;
        count++;
    }
    return count;
}

wchar_t *wcstok(wchar_t *ws1, const wchar_t *ws2, wchar_t **ptr) {
    if (!ws1) ws1 = *ptr;
    if (!ws1) return NULL;
    ws1 += wcsspn(ws1, ws2);
    if (!*ws1) {
        *ptr = NULL;
        return NULL;
    }
    wchar_t *token_end = ws1 + wcscspn(ws1, ws2);
    if (*token_end) {
        *token_end = L'\0';
        *ptr = token_end + 1;
    } else {
        *ptr = NULL;
    }
    return ws1;
}

size_t wcsxfrm(wchar_t *ws1, const wchar_t *ws2, size_t n) {
    size_t len = wcslen(ws2);
    if (len < n) wcscpy(ws1, ws2);
    return len;
}

size_t wcsftime(wchar_t *wcs, size_t maxsize, const wchar_t *format, const struct tm *timeptr) {
    (void)format; (void)timeptr;
    if (maxsize > 0 && wcs) *wcs = L'\0';
    return 0;
}
