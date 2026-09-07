/*
 * SzpontOS - Wait Queue & Event Synchronization Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <sched/waitqueue.h>
#include <sched/sched.h>
#include <kernel/spinlock.h>

void wait_queue_init(wait_queue_t *wq) {
    if (!wq)
        return;
    spinlock_init(&wq->lock);
    list_init(&wq->waiters);
}

void wait_queue_wait(wait_queue_t *wq) {
    thread_t *curr = sched_get_current_thread();
    if (!curr || !wq)
        return;

    wait_node_t node;
    node.thread = curr;
    node.woken = false;
    list_init(&node.node);

    uint64_t flags;
    spinlock_acquire_irqsave(&wq->lock, &flags);
    list_add_tail(&wq->waiters, &node.node);
    spinlock_release_irqrestore(&wq->lock, flags);

    /* Block current thread and yield until awakened */
    sched_block_current_thread();

    /* Clean up our node if still in the list */
    spinlock_acquire_irqsave(&wq->lock, &flags);
    if (node.node.next != NULL && node.node.prev != NULL &&
        node.node.next != &node.node) {
        list_remove(&node.node);
    }
    spinlock_release_irqrestore(&wq->lock, flags);
}

void wait_queue_wake_one(wait_queue_t *wq) {
    if (!wq)
        return;

    uint64_t flags;
    spinlock_acquire_irqsave(&wq->lock, &flags);
    if (!list_is_empty(&wq->waiters)) {
        list_node_t *first = wq->waiters.next;
        wait_node_t *node = container_of(first, wait_node_t, node);
        list_remove(first);
        node->woken = true;
        sched_unblock_thread(node->thread);
    }
    spinlock_release_irqrestore(&wq->lock, flags);
}

void wait_queue_wake_all(wait_queue_t *wq) {
    if (!wq)
        return;

    uint64_t flags;
    spinlock_acquire_irqsave(&wq->lock, &flags);
    list_node_t *pos, *n;
    list_for_each_safe(pos, n, &wq->waiters) {
        wait_node_t *node = container_of(pos, wait_node_t, node);
        list_remove(pos);
        node->woken = true;
        sched_unblock_thread(node->thread);
    }
    spinlock_release_irqrestore(&wq->lock, flags);
}
