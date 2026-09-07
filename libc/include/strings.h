/*
 * SzpontOS - POSIX strings.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _STRINGS_H
#define _STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
int ffs(int i);
int ffsl(long int i);

#undef bcmp
int bcmp(const void *s1, const void *s2, size_t n);
#undef bcopy
void bcopy(const void *src, void *dst, size_t n);
#undef bzero
void bzero(void *s, size_t n);
#undef index
char *index(const char *s, int c);
#undef rindex
char *rindex(const char *s, int c);

#ifdef __cplusplus
}
#endif

#endif /* _STRINGS_H */
