#ifndef _SCHED_H
#define _SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define CSIGNAL 0x000000ff
#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID 0x01000000

struct sched_param {
    int sched_priority;
};

int clone(int (*fn)(void *), void *child_stack, int flags, void *arg, ...);
int sched_yield(void);
int sched_get_priority_max(int policy);
int sched_get_priority_min(int policy);

#ifdef __cplusplus
}
#endif

#endif /* _SCHED_H */
