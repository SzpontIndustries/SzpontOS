/*
 * SzpontOS Libc - sys/shm.h (System V Shared Memory header)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/ipc.h>

#define SHM_RDONLY 010000
#define SHM_RND    020000
#define SHM_REMAP  040000
#define SHM_EXEC   0100000

#define SHMLBA     4096

#define SHM_R      IPC_R
#define SHM_W      IPC_W

#define SHM_LOCK   11
#define SHM_UNLOCK 12
#define SHM_STAT   13
#define SHM_INFO   14

typedef unsigned long shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm;
    size_t shm_segsz;
    time_t shm_atime;
    time_t shm_dtime;
    time_t shm_ctime;
    pid_t shm_cpid;
    pid_t shm_lpid;
    shmatt_t shm_nattch;
    unsigned long __unused4;
    unsigned long __unused5;
};

struct shminfo {
    unsigned long shmmax;
    unsigned long shmmin;
    unsigned long shmmni;
    unsigned long shmseg;
    unsigned long shmall;
    unsigned long __unused1;
    unsigned long __unused2;
    unsigned long __unused3;
    unsigned long __unused4;
};

int shmget(key_t key, size_t size, int shmflg);
void *shmat(int shmid, const void *shmaddr, int shmflg);
int shmdt(const void *shmaddr);
int shmctl(int shmid, int cmd, struct shmid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SHM_H */
