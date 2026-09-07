#ifndef _UCHAR_H
#define _UCHAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#ifndef __cplusplus
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#endif

typedef uint_least8_t char8_t;

size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);

#ifdef __cplusplus
}
#endif

#endif /* _UCHAR_H */
