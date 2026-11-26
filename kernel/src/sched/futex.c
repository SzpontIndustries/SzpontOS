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
#include <mm/usercopy.h>
#include <arch/x86_64/pit.h>

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
    process_t *proc = sched_get_current_process();
    thread_t *curr = sched_get_current_thread();
    if (!proc || !curr || uaddr == 0)
        return -1;

    /* Copy and validate timeout before acquiring bucket lock */
    bool has_timeout = false;
    uint64_t expire_ticks = 0;
    if (timeout) {
        struct timespec ktimeout;
        if (!copy_from_user(&ktimeout, (uintptr_t)timeout, sizeof(struct timespec))) {
            return -14; /* -EFAULT */
        }
        if (ktimeout.tv_sec < 0 || ktimeout.tv_nsec < 0 || ktimeout.tv_nsec >= 1000000000L) {
            return -22; /* -EINVAL */
        }
        uint64_t wait_ms = (uint64_t)ktimeout.tv_sec * 1000 + (uint64_t)(ktimeout.tv_nsec / 1000000);
        if (wait_ms == 0 && ktimeout.tv_nsec > 0)
            wait_ms = 1;

        uint64_t start_ticks = pit_get_ticks();
        uint32_t freq = pit_get_frequency();
        if (freq == 0)
            freq = 1000;
        uint64_t duration_ticks = (wait_ms * freq) / 1000;
        if (duration_ticks == 0 && wait_ms > 0)
            duration_ticks = 1;
        expire_ticks = start_ticks + duration_ticks;
        has_timeout = true;
    }

    /* Verify that uaddr is mapped and accessible in user space */
    if (!vmm_virt_to_phys(proc->pagemap, uaddr)) {
        return -14; /* -EFAULT */
    }

    uint32_t bucket_idx = FUTEX_HASH(uaddr);
    futex_bucket_t *bucket = &g_futex_buckets[bucket_idx];

    uint64_t bflags;
    spinlock_acquire_irqsave(&bucket->lock, &bflags);

    /* Atomic check: read current value at uaddr using usercopy */
    int current_val = 0;
    if (!copy_from_user(&current_val, uaddr, sizeof(int))) {
        spinlock_release_irqrestore(&bucket->lock, bflags);
        return -14; /* -EFAULT */
    }

    if (current_val != val) {
        spinlock_release_irqrestore(&bucket->lock, bflags);
        return -11; /* -EAGAIN */
    }

    /* Register waiter under bucket->lock; state transition happens atomically
     * under g_sched_lock in sched_block_on_futex(). */
    list_remove(&curr->futex_node);
    curr->futex_proc = proc;
    __atomic_store_n(&curr->futex_uaddr, uaddr, __ATOMIC_RELEASE);
    list_add_tail(&bucket->waiters, &curr->futex_node);

    spinlock_release_irqrestore(&bucket->lock, bflags);

    while (__atomic_load_n(&curr->futex_uaddr, __ATOMIC_ACQUIRE) != 0) {
        if (has_timeout) {
            if (pit_get_ticks() >= expire_ticks) {
                spinlock_acquire_irqsave(&bucket->lock, &bflags);
                if (__atomic_load_n(&curr->futex_uaddr, __ATOMIC_ACQUIRE) != 0) {
                    list_remove(&curr->futex_node);
                    __atomic_store_n(&curr->futex_uaddr, 0, __ATOMIC_RELEASE);
                    curr->futex_proc = NULL;
                    spinlock_release_irqrestore(&bucket->lock, bflags);
                    return -110; /* -ETIMEDOUT */
                }
                spinlock_release_irqrestore(&bucket->lock, bflags);
                break;
            }
            sched_block_on_futex(&curr->futex_uaddr, expire_ticks, true);
        } else {
            sched_block_on_futex(&curr->futex_uaddr, 0, false);
        }
    }

    return 0;
}

int futex_wake_proc(process_t *proc, uintptr_t uaddr, int count) {
    if (uaddr == 0 || count <= 0 || !proc)
        return 0;

    uint32_t bucket_idx = FUTEX_HASH(uaddr);
    futex_bucket_t *bucket = &g_futex_buckets[bucket_idx];

    uint64_t bflags;
    spinlock_acquire_irqsave(&bucket->lock, &bflags);

    int woken = 0;
    list_node_t *pos, *n;
    list_for_each_safe(pos, n, &bucket->waiters) {
        if (woken >= count)
            break;

        thread_t *t = container_of(pos, thread_t, futex_node);
        if (t->futex_uaddr == uaddr && t->futex_proc &&
            (t->futex_proc == proc || t->futex_proc->pagemap == proc->pagemap)) {
            list_remove(pos);
            t->futex_proc = NULL;
            __atomic_store_n(&t->futex_uaddr, 0, __ATOMIC_RELEASE);
            sched_unblock_thread(t);
            woken++;
        }
    }

    spinlock_release_irqrestore(&bucket->lock, bflags);
    return woken;
}

int futex_wake(uintptr_t uaddr, int count) {
    if (uaddr == 0 || count <= 0)
        return 0;
    process_t *proc = sched_get_current_process();
    if (!proc)
        return 0;

    return futex_wake_proc(proc, uaddr, count);
}

/* Removes a thread from whatever futex bucket it's blocked in, if any.
 * Must be called before a blocked thread is force-transitioned to
 * THREAD_ZOMBIE (e.g. process_exit(), a fatal signal) — otherwise its
 * futex_node stays linked into g_futex_buckets[] and a later futex_wake()
 * on the same address can resurrect the (possibly already-freed) thread
 * into the ready queue. */
void futex_remove_thread(thread_t *t) {
    if (!t || (uintptr_t)t < 0x1000 || (uintptr_t)t > 0xffffffffffff0000ULL || !t->futex_proc)
        return;

    uint32_t bucket_idx = FUTEX_HASH(t->futex_uaddr);
    futex_bucket_t *bucket = &g_futex_buckets[bucket_idx];

    uint64_t bflags;
    spinlock_acquire_irqsave(&bucket->lock, &bflags);
    if (t->futex_proc) {
        list_remove(&t->futex_node);
        __atomic_store_n(&t->futex_uaddr, 0, __ATOMIC_RELEASE);
        t->futex_proc = NULL;
    }
    spinlock_release_irqrestore(&bucket->lock, bflags);
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
    uint64_t f1 = 0, f2 = 0;
    if (b1_idx < b2_idx) {
        spinlock_acquire_irqsave(&b1->lock, &f1);
        spinlock_acquire_irqsave(&b2->lock, &f2);
    } else if (b1_idx > b2_idx) {
        spinlock_acquire_irqsave(&b2->lock, &f2);
        spinlock_acquire_irqsave(&b1->lock, &f1);
    } else {
        spinlock_acquire_irqsave(&b1->lock, &f1);
    }

    int woken = 0;
    int requeued = 0;

    list_node_t *pos, *n;
    list_for_each_safe(pos, n, &b1->waiters) {
        thread_t *t = container_of(pos, thread_t, futex_node);
        if (t->futex_uaddr == uaddr1 && t->futex_proc &&
            (t->futex_proc == proc || t->futex_proc->pagemap == proc->pagemap)) {
            if (woken < wake_count) {
                list_remove(pos);
                t->futex_proc = NULL;
                __atomic_store_n(&t->futex_uaddr, 0, __ATOMIC_RELEASE);
                sched_unblock_thread(t);
                woken++;
            } else if (requeued < requeue_count) {
                list_remove(pos);
                __atomic_store_n(&t->futex_uaddr, uaddr2, __ATOMIC_RELEASE);
                list_add_tail(&b2->waiters, &t->futex_node);
                requeued++;
            } else {
                break;
            }
        }
    }

    if (b1_idx < b2_idx) {
        spinlock_release_irqrestore(&b2->lock, f2);
        spinlock_release_irqrestore(&b1->lock, f1);
    } else if (b1_idx > b2_idx) {
        spinlock_release_irqrestore(&b1->lock, f1);
        spinlock_release_irqrestore(&b2->lock, f2);
    } else {
        spinlock_release_irqrestore(&b1->lock, f1);
    }

    return woken + requeued;
}
