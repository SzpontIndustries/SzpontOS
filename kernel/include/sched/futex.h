#ifndef SZPONTOS_SCHED_FUTEX_H
#define SZPONTOS_SCHED_FUTEX_H

#include <kernel/types.h>
#include <kernel/list.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_FD 2
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_CMP_REQUEUE_PI 12

#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

struct timespec;
struct thread;

void futex_init(void);
int futex_wait(uintptr_t uaddr, int val, const struct timespec *timeout);
int futex_wake(uintptr_t uaddr, int count);
int futex_requeue(uintptr_t uaddr1, int wake_count, uintptr_t uaddr2, int requeue_count);
void futex_remove_thread(struct thread *t);

#endif /* SZPONTOS_SCHED_FUTEX_H */
