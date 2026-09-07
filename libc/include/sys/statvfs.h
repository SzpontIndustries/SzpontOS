#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

struct statvfs {
    unsigned long f_bsize;   /* File system block size */
    unsigned long f_frsize;  /* Fundamental file system block size */
    fsblkcnt_t f_blocks;     /* Total blocks on file system in units of f_frsize */
    fsblkcnt_t f_bfree;      /* Total free blocks */
    fsblkcnt_t f_bavail;     /* Free blocks available to non-privileged user */
    fsfilcnt_t f_files;      /* Total file nodes in file system */
    fsfilcnt_t f_ffree;      /* Total free file nodes */
    fsfilcnt_t f_favail;     /* Free nodes available to non-privileged user */
    unsigned long f_fsid;    /* File system ID */
    unsigned long f_flag;    /* Mount flags */
    unsigned long f_namemax; /* Maximum filename length */
};

struct statfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);
int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);

#define ST_RDONLY 1
#define ST_NOSUID 2

#endif /* _SYS_STATVFS_H */
