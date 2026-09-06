/*
 * BSD / POSIX sys/cdefs.h for SzpontOS
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_CDEFS_H
#define _SYS_CDEFS_H

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#ifndef __FBSDID
#define __FBSDID(s) struct __hack
#endif

#ifndef __RCSID
#define __RCSID(s) struct __hack
#endif

#ifndef __dead2
#define __dead2 __attribute__((__noreturn__))
#endif

#ifndef __pure2
#define __pure2 __attribute__((__const__))
#endif

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#ifndef __used
#define __used __attribute__((__used__))
#endif

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__((__aligned__(x)))
#endif

#ifndef __section
#define __section(x) __attribute__((__section__(x)))
#endif

#ifndef __printflike
#define __printflike(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif

#ifndef __printf0like
#define __printf0like(fmtarg, firstvararg)
#endif

#ifndef __format_arg
#define __format_arg(fmtarg) __attribute__((__format_arg__(fmtarg)))
#endif

#ifndef __nonnull
#define __nonnull(args) __attribute__((__nonnull__ args))
#endif

#ifndef __nonstring
#define __nonstring
#endif

#ifndef __predict_true
#define __predict_true(exp) __builtin_expect((exp), 1)
#endif

#ifndef __predict_false
#define __predict_false(exp) __builtin_expect((exp), 0)
#endif

#ifndef __DECONST
#define __DECONST(type, var) ((type)(unsigned long)(const void *)(var))
#endif

#ifndef __DEVOLATILE
#define __DEVOLATILE(type, var) ((type)(unsigned long)(const volatile void *)(var))
#endif

#ifndef __DEQUALIFY
#define __DEQUALIFY(type, var) ((type)(unsigned long)(const volatile void *)(var))
#endif

#endif /* _SYS_CDEFS_H */
