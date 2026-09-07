/*
 * SzpontOS - Fast Userspace Mutex (Futex) Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <sched/futex.h>
#include <sched/sched.h>
#include <sched/process.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <kernel/spinlock.h>
#include <kernel/kprint.h>

#define FUTEX_HASH_SIZE 64
#define FUTEX_HASH(addr) (((uintptr_t)(addr) >> 2) % FUTEX_HASH_SIZE)

typedef struct futex_bucket {
    spinlock_t lock;
    list_node_t waiters;
} futex_bucket_t;

static futex_bucket_t g_futex_buckets[FUTEX_HASH_SIZE];

void futex_init(void) {
    for (size_t i = 0; i < FUTEX_HASH_SIZE; i++) {
        spinlock_init(&g_futex_buckets[i].lock);
        list_init(&g_futex_buckets[i].waiters);
    }
    klog_info("Futex Subsystem initialized (64 hash buckets)");
}

int futex_wait(uintptr_t uaddr, int val, const struct timespec *timeout) {
    (void)timeout;
    process_t *proc = sched_get_current_process();
    thread_t *curr = sched_get_current_thread();
    if (!proc || !curr || uaddr == 0)
        return -1;

    /* Verify that uaddr is mapped and accessible in user space */
    if (!vmm_virt_to_phys(proc->pagemap, uaddr)) {
        return -1;
    }

    uint32_t bucket_idx = FUTEX_HASH(uaddr);
    futex_bucket_t *bucket = &g_futex_buckets[bucket_idx];

    spinlock_acquire(&bucket->lock);

    /* Atomic check: read current value at uaddr */
    int current_val = *(volatile int *)uaddr;
    if (current_val != val) {
        spinlock_release(&bucket->lock);
        return -11; /* -EAGAIN */
    }

    /* Block current thread and add to futex wait queue */
    curr->state = THREAD_BLOCKED;
    curr->futex_uaddr = uaddr;
    curr->futex_proc = proc;
    list_add_tail(&bucket->waiters, &curr->futex_node);

    spinlock_release(&bucket->lock);

    /* Yield CPU until awakened by futex_wake */
    sched_yield();

    return 0;
}

int futex_wake(uintptr_t uaddr, int count) {
    if (uaddr == 0 || count <= 0)
        return 0;
    process_t *proc = sched_get_current_process();
    if (!proc)
        return 0;

    uint32_t bucket_idx = FUTEX_HASH(uaddr);
    futex_bucket_t *bucket = &g_futex_buckets[bucket_idx];

    spinlock_acquire(&bucket->lock);

    int woken = 0;
    list_node_t *pos, *n;
    list_for_each_safe(pos, n, &bucket->waiters) {
        if (woken >= count)
            break;

        thread_t *t = container_of(pos, thread_t, futex_node);
        if (t->futex_uaddr == uaddr && (t->futex_proc == proc || t->futex_proc->pagemap == proc->pagemap)) {
            list_remove(pos);
            t->futex_uaddr = 0;
            t->futex_proc = NULL;
            t->state = THREAD_READY;
            sched_add_thread(t);
            woken++;
        }
    }

    spinlock_release(&bucket->lock);
    return woken;
}

/* Removes a thread from whatever futex bucket it's blocked in, if any.
 * Must be called before a blocked thread is force-transitioned to
 * THREAD_ZOMBIE (e.g. process_exit(), a fatal signal) — otherwise its
 * futex_node stays linked into g_futex_buckets[] and a later futex_wake()
 * on the same address can resurrect the (possibly already-freed) thread
 * into the ready queue. */
void futex_remove_thread(thread_t *t) {
    if (!t || !t->futex_proc)
        return;

    uint32_t bucket_idx = FUTEX_HASH(t->futex_uaddr);
    futex_bucket_t *bucket = &g_futex_buckets[bucket_idx];

    spinlock_acquire(&bucket->lock);
    if (t->futex_proc) {
        list_remove(&t->futex_node);
        t->futex_uaddr = 0;
        t->futex_proc = NULL;
    }
    spinlock_release(&bucket->lock);
}

int futex_requeue(uintptr_t uaddr1, int wake_count, uintptr_t uaddr2, int requeue_count) {
    if (uaddr1 == 0 || uaddr2 == 0)
        return -1;
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    uint32_t b1_idx = FUTEX_HASH(uaddr1);
    uint32_t b2_idx = FUTEX_HASH(uaddr2);

    futex_bucket_t *b1 = &g_futex_buckets[b1_idx];
    futex_bucket_t *b2 = &g_futex_buckets[b2_idx];

    /* Always acquire locks in bucket index order to prevent deadlock */
    if (b1_idx < b2_idx) {
        spinlock_acquire(&b1->lock);
        spinlock_acquire(&b2->lock);
    } else if (b1_idx > b2_idx) {
        spinlock_acquire(&b2->lock);
        spinlock_acquire(&b1->lock);
    } else {
        spinlock_acquire(&b1->lock);
    }

    int woken = 0;
    int requeued = 0;

    list_node_t *pos, *n;
    list_for_each_safe(pos, n, &b1->waiters) {
        thread_t *t = container_of(pos, thread_t, futex_node);
        if (t->futex_uaddr == uaddr1 && (t->futex_proc == proc || t->futex_proc->pagemap == proc->pagemap)) {
            if (woken < wake_count) {
                list_remove(pos);
                t->futex_uaddr = 0;
                t->futex_proc = NULL;
                t->state = THREAD_READY;
                sched_add_thread(t);
                woken++;
            } else if (requeued < requeue_count) {
                list_remove(pos);
                t->futex_uaddr = uaddr2;
                list_add_tail(&b2->waiters, &t->futex_node);
                requeued++;
            } else {
                break;
            }
        }
    }

    if (b1_idx != b2_idx) {
        spinlock_release(&b2->lock);
    }
    spinlock_release(&b1->lock);

    return woken + requeued;
}
