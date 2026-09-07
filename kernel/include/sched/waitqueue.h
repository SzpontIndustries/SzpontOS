/*
 * SzpontOS - Wait Queue & Event Synchronization Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Provides event-driven thread blocking and wakeups without CPU polling.
 * Inspired by Vinix OS event system and Linux wait queues.
 */

#ifndef SZPONTOS_SCHED_WAITQUEUE_H
#define SZPONTOS_SCHED_WAITQUEUE_H

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <sched/thread.h>

typedef struct wait_queue {
    spinlock_t lock;
    list_node_t waiters;
} wait_queue_t;

typedef struct wait_node {
    thread_t *thread;
    list_node_t node;
    bool woken;
} wait_node_t;

void wait_queue_init(wait_queue_t *wq);
void wait_queue_wait(wait_queue_t *wq);
void wait_queue_wake_one(wait_queue_t *wq);
void wait_queue_wake_all(wait_queue_t *wq);

#endif /* SZPONTOS_SCHED_WAITQUEUE_H */
