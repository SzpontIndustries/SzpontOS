#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <sys/cdefs.h>

typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef int32_t id_t;
typedef int64_t clock_t;
typedef int64_t time_t;
typedef int64_t suseconds_t;
typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;
typedef int32_t key_t;

typedef char *caddr_t;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

#ifndef major
#define major(dev) ((dev_t)(((dev) >> 8) & 0xff))
#define minor(dev) ((dev_t)((dev) & 0xff))
#define makedev(maj, min) ((dev_t)((((maj) & 0xff) << 8) | ((min) & 0xff)))
#endif

#include <sys/select.h>

#endif /* _SYS_TYPES_H */
