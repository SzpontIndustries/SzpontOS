#ifndef _STRING_H
#define _STRING_H

#include <sys/types.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memrchr(const void *s, int c, size_t n);
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcoll(const char *s1, const char *s2);
size_t strxfrm(char *dest, const char *src, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strcasestr(const char *haystack, const char *needle);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strsep(char **stringp, const char *delim);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);

char *strerror(int errnum);
char *strsignal(int sig);

int ffs(int i);
int ffsl(long int i);

char *strchrnul(const char *s, int c);
void *memccpy(void *dest, const void *src, int c, size_t n);
#undef bzero
void bzero(void *s, size_t n);
#undef bcopy
void bcopy(const void *src, void *dst, size_t n);
#undef bcmp
int bcmp(const void *s1, const void *s2, size_t n);
#undef index
char *index(const char *s, int c);
#undef rindex
char *rindex(const char *s, int c);

#endif /* _STRING_H */
