#ifndef SZPONTOS_SCHED_SCHED_H
#define SZPONTOS_SCHED_SCHED_H

#include <sched/process.h>
#include <sched/thread.h>
#include <arch/x86_64/idt.h>

void sched_init(void);
void sched_init_cpu(uint32_t cpu_id);
void sched_start(void);
void sched_ap_start(void);
bool sched_is_started(void);
void sched_yield(void);
void sched_tick(void);

thread_t *sched_get_current_thread(void);
process_t *sched_get_current_process(void);

void sched_add_thread(thread_t *thread);
void sched_remove_thread(thread_t *thread);
void sched_block_current_thread(void);
void sched_unblock_thread(thread_t *thread);
bool sched_block_on_futex(volatile uintptr_t *uaddr_slot, uint64_t wakeup_tick, bool timed);
void sched_block_on_waitq(volatile bool *woken_flag);

void sched_get_cpu_ticks(uint64_t *user_ticks, uint64_t *sys_ticks, uint64_t *idle_ticks);

#endif /* SZPONTOS_SCHED_SCHED_H */
