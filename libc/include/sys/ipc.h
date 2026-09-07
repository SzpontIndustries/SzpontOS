/*
 * SzpontOS Libc - sys/ipc.h (System V IPC header)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define IPC_PRIVATE ((key_t)0)

#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_NOWAIT 04000

#define IPC_RMID 0
#define IPC_SET  1
#define IPC_STAT 2
#define IPC_INFO 3

#define IPC_R 0400
#define IPC_W 0200
#define IPC_M 010000

struct ipc_perm {
    key_t key;
    uid_t uid;
    gid_t gid;
    uid_t cuid;
    gid_t cgid;
    mode_t mode;
    unsigned short __pad1;
    unsigned short seq;
    unsigned short __pad2;
    unsigned long __unused1;
    unsigned long __unused2;
};

key_t ftok(const char *pathname, int proj_id);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IPC_H */
