#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define SI_LOAD_SHIFT 16

struct sysinfo {
    long uptime;             /* Seconds since boot */
    unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
    unsigned long totalram;  /* Total usable main memory size */
    unsigned long freeram;   /* Available memory size */
    unsigned long sharedram; /* Amount of shared memory */
    unsigned long bufferram; /* Memory used by buffers */
    unsigned long totalswap; /* Total swap space size */
    unsigned long freeswap;  /* swap space still available */
    unsigned short procs;    /* Number of current processes */
    unsigned long totalhigh; /* Total high memory size */
    unsigned long freehigh;  /* Available high memory size */
    unsigned int mem_unit;   /* Memory unit size in bytes */
};

typedef struct proc_info {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    gid_t gid;
    int state;
    char name[64];
} proc_info_t;

int sysinfo(struct sysinfo *info);
int getprocs(proc_info_t *buf, size_t max_count);

int get_nprocs(void);
int get_nprocs_conf(void);
long get_phys_pages(void);
long get_avphys_pages(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSINFO_H */
