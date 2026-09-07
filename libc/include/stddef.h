#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned long size_t;
typedef long ptrdiff_t;

#ifndef NULL
#ifdef __cplusplus
#define NULL __null
#else
#define NULL ((void *)0)
#endif
#endif

#ifndef _WCHAR_T_DECLARED
#define _WCHAR_T_DECLARED
#ifndef __cplusplus
typedef unsigned int wchar_t;
#endif
#endif

#ifndef offsetof
#if defined(__GNUC__) || defined(__clang__)
#define offsetof(type, member) __builtin_offsetof(type, member)
#else
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
#endif
#endif

typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#endif /* _STDDEF_H */
