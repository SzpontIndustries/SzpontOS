/*
 * SzpontOS - Linux-compatible timerfd(2) Kernel Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <fs/timerfd.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <mm/usercopy.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <drivers/rtc.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

static vfs_ops_t g_timerfd_ops;
static timerfd_ctx_t *g_timerfd_list = NULL;
static spinlock_t g_timerfd_list_lock = SPINLOCK_INIT;

static uint64_t timerfd_now_ns(int clockid) {
    if (clockid == 0) { /* CLOCK_REALTIME */
        struct timespec_kernel ts;
        rtc_get_timespec(&ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    } else { /* CLOCK_MONOTONIC / CLOCK_BOOTTIME */
        return rtc_get_monotonic_ns();
    }
}

static void timerfd_list_add(timerfd_ctx_t *ctx) {
    spinlock_acquire(&g_timerfd_list_lock);
    ctx->next = g_timerfd_list;
    g_timerfd_list = ctx;
    spinlock_release(&g_timerfd_list_lock);
}

static void timerfd_list_remove(timerfd_ctx_t *ctx) {
    spinlock_acquire(&g_timerfd_list_lock);
    if (g_timerfd_list == ctx) {
        g_timerfd_list = ctx->next;
        spinlock_release(&g_timerfd_list_lock);
        return;
    }
    timerfd_ctx_t *curr = g_timerfd_list;
    while (curr && curr->next) {
        if (curr->next == ctx) {
            curr->next = ctx->next;
            break;
        }
        curr = curr->next;
    }
    spinlock_release(&g_timerfd_list_lock);
}

void timerfd_tick(void) {
    if (!spinlock_try_acquire(&g_timerfd_list_lock))
        return;

    uint64_t now_mono = rtc_get_monotonic_ns();
    struct timespec_kernel ts_real;
    rtc_get_timespec(&ts_real);
    uint64_t now_real = (uint64_t)ts_real.tv_sec * 1000000000ULL + (uint64_t)ts_real.tv_nsec;

    for (timerfd_ctx_t *curr = g_timerfd_list; curr; curr = curr->next) {
        spinlock_acquire(&curr->lock);
        if (curr->armed) {
            uint64_t now = (curr->clockid == 0) ? now_real : now_mono;
            if (now >= curr->target_ns) {
                uint64_t exp = 1;
                if (curr->interval_ns > 0) {
                    uint64_t elapsed = now - curr->target_ns;
                    uint64_t extra = elapsed / curr->interval_ns;
                    exp += extra;
                    curr->target_ns += (extra + 1) * curr->interval_ns;
                } else {
                    curr->armed = false;
                }
                curr->expirations += exp;
                spinlock_release(&curr->lock);
                wait_queue_wake_all(&curr->wq);
                continue;
            }
        }
        spinlock_release(&curr->lock);
    }

    spinlock_release(&g_timerfd_list_lock);
}

static ssize_t timerfd_vfs_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer)
        return -14; /* -EFAULT */
    if (size < sizeof(uint64_t))
        return -22; /* -EINVAL */

    timerfd_ctx_t *ctx = (timerfd_ctx_t *)node->device_data;
    uint64_t return_val = 0;

    while (1) {
        spinlock_acquire(&ctx->lock);
        if (ctx->expirations > 0) {
            return_val = ctx->expirations;
            ctx->expirations = 0;
            spinlock_release(&ctx->lock);
            break;
        }

        /* Value is 0 */
        if (ctx->flags & TFD_NONBLOCK) {
            spinlock_release(&ctx->lock);
            return -11; /* -EAGAIN */
        }

        /* Check process descriptor nonblock flag */
        process_t *proc = sched_get_current_process();
        if (proc) {
            for (int i = 0; i < MAX_FD; i++) {
                if (proc->fds[i] && proc->fds[i]->node == node) {
                    if (proc->fds[i]->flags & 0x0800 /* O_NONBLOCK */) {
                        spinlock_release(&ctx->lock);
                        return -11; /* -EAGAIN */
                    }
                    break;
                }
            }
            thread_t *tfd_self = sched_get_current_thread();
            uint32_t tfd_pend = proc->pending_signals;
            uint32_t tfd_mask = 0;
            if (tfd_self) {
                tfd_pend |= tfd_self->thread_pending;
                tfd_mask = tfd_self->blocked_signals;
            }
            if (tfd_pend & ~tfd_mask) {
                spinlock_release(&ctx->lock);
                return -4; /* -EINTR */
            }
        }

        spinlock_release(&ctx->lock);
        wait_queue_wait(&ctx->wq);
    }

    if (!copy_to_user((uintptr_t)buffer, &return_val, sizeof(uint64_t))) {
        if ((uintptr_t)buffer > USER_ADDR_MAX) {
            memcpy(buffer, &return_val, sizeof(uint64_t));
        } else {
            return -14; /* -EFAULT */
        }
    }

    return sizeof(uint64_t);
}

static int timerfd_vfs_close(vfs_node_t *node) {
    if (!node || !node->device_data)
        return 0;
    timerfd_ctx_t *ctx = (timerfd_ctx_t *)node->device_data;
    timerfd_list_remove(ctx);
    kfree(ctx);
    node->device_data = NULL;
    return 0;
}

static void timerfd_ops_init(void) {
    static bool init = false;
    if (!init) {
        memset(&g_timerfd_ops, 0, sizeof(vfs_ops_t));
        g_timerfd_ops.read = timerfd_vfs_read;
        g_timerfd_ops.close = timerfd_vfs_close;
        init = true;
    }
}

bool timerfd_has_pollin(vfs_node_t *node) {
    if (!node || !node->device_data)
        return false;
    timerfd_ctx_t *ctx = (timerfd_ctx_t *)node->device_data;
    spinlock_acquire(&ctx->lock);
    bool ready = (ctx->expirations > 0);
    spinlock_release(&ctx->lock);
    return ready;
}

int64_t sys_timerfd_create(int clockid, int flags) {
    /* Valid clocks: CLOCK_REALTIME (0), CLOCK_MONOTONIC (1), CLOCK_BOOTTIME (7) */
    if (clockid != 0 && clockid != 1 && clockid != 7)
        return -22; /* -EINVAL */

    if (flags & ~(TFD_CLOEXEC | TFD_NONBLOCK))
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    timerfd_ops_init();

    int fd = -1;
    for (int i = 3; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1)
        return -24; /* -EMFILE */

    timerfd_ctx_t *ctx = (timerfd_ctx_t *)kzalloc(sizeof(timerfd_ctx_t));
    if (!ctx)
        return -12; /* -ENOMEM */

    ctx->lock = SPINLOCK_INIT;
    ctx->clockid = clockid;
    ctx->flags = (uint32_t)flags;
    ctx->armed = false;
    ctx->expirations = 0;
    wait_queue_init(&ctx->wq);

    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) {
        kfree(ctx);
        return -12; /* -ENOMEM */
    }

    ksnprintf(node->name, sizeof(node->name), "timerfd:[%d]", fd);
    node->flags = VFS_TYPE_CHARDEVICE;
    node->ops = &g_timerfd_ops;
    node->device_data = ctx;

    file_descriptor_t *fdesc = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    if (!fdesc) {
        kfree(node);
        kfree(ctx);
        return -12; /* -ENOMEM */
    }

    fdesc->node = node;
    /* Linux creates timerfd descriptors O_RDWR; O_RDONLY breaks writev(2)
     * on accepted sockets in ported event loops that share poll helpers. */
    fdesc->flags = O_RDWR;
    if (flags & TFD_NONBLOCK)
        fdesc->flags |= 0x0800; /* O_NONBLOCK */
    fdesc->refcount = 1;

    proc->fds[fd] = fdesc;
    if (flags & TFD_CLOEXEC)
        proc->fd_cloexec[fd] = 1;

    timerfd_list_add(ctx);
    return fd;
}

int64_t sys_timerfd_settime(int fd, int flags, const struct itimerspec_kernel *new_value, struct itimerspec_kernel *old_value) {
    if (flags & ~(TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET))
        return -22; /* -EINVAL */

    if (!new_value)
        return -14; /* -EFAULT */

    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return -9; /* -EBADF */

    vfs_node_t *node = proc->fds[fd]->node;
    if (node->ops != &g_timerfd_ops || !node->device_data)
        return -22; /* -EINVAL: not a timerfd */

    struct itimerspec_kernel k_new;
    if (!copy_from_user(&k_new, (uintptr_t)new_value, sizeof(struct itimerspec_kernel))) {
        if ((uintptr_t)new_value > USER_ADDR_MAX) {
            memcpy(&k_new, new_value, sizeof(struct itimerspec_kernel));
        } else {
            return -14; /* -EFAULT */
        }
    }

    if (k_new.it_value.tv_nsec < 0 || k_new.it_value.tv_nsec >= 1000000000LL ||
        k_new.it_interval.tv_nsec < 0 || k_new.it_interval.tv_nsec >= 1000000000LL) {
        return -22; /* -EINVAL */
    }

    timerfd_ctx_t *ctx = (timerfd_ctx_t *)node->device_data;
    uint64_t now = timerfd_now_ns(ctx->clockid);

    spinlock_acquire(&ctx->lock);

    /* Return old_value if requested */
    if (old_value) {
        struct itimerspec_kernel k_old;
        memset(&k_old, 0, sizeof(struct itimerspec_kernel));
        if (ctx->armed) {
            if (ctx->target_ns > now) {
                uint64_t diff = ctx->target_ns - now;
                k_old.it_value.tv_sec = (int64_t)(diff / 1000000000ULL);
                k_old.it_value.tv_nsec = (int64_t)(diff % 1000000000ULL);
            } else {
                /* Expired but not read yet: smallest positive interval */
                k_old.it_value.tv_sec = 0;
                k_old.it_value.tv_nsec = 1;
            }
            k_old.it_interval.tv_sec = (int64_t)(ctx->interval_ns / 1000000000ULL);
            k_old.it_interval.tv_nsec = (int64_t)(ctx->interval_ns % 1000000000ULL);
        }

        if (!copy_to_user((uintptr_t)old_value, &k_old, sizeof(struct itimerspec_kernel))) {
            if ((uintptr_t)old_value > USER_ADDR_MAX) {
                memcpy(old_value, &k_old, sizeof(struct itimerspec_kernel));
            } else {
                spinlock_release(&ctx->lock);
                return -14; /* -EFAULT */
            }
        }
    }

    /* Check if disarming */
    if (k_new.it_value.tv_sec == 0 && k_new.it_value.tv_nsec == 0) {
        ctx->armed = false;
        ctx->target_ns = 0;
        ctx->interval_ns = 0;
        spinlock_release(&ctx->lock);
        return 0;
    }

    /* Calculate interval in ns */
    ctx->interval_ns = (uint64_t)k_new.it_interval.tv_sec * 1000000000ULL + (uint64_t)k_new.it_interval.tv_nsec;

    /* Calculate target in ns */
    uint64_t val_ns = (uint64_t)k_new.it_value.tv_sec * 1000000000ULL + (uint64_t)k_new.it_value.tv_nsec;
    if (flags & TFD_TIMER_ABSTIME) {
        ctx->target_ns = val_ns;
    } else {
        ctx->target_ns = now + val_ns;
    }

    ctx->armed = true;
    ctx->expirations = 0;

    /* If target is already in the past, fire immediately */
    if (ctx->target_ns <= now) {
        ctx->expirations = 1;
        if (ctx->interval_ns > 0) {
            uint64_t elapsed = now - ctx->target_ns;
            uint64_t extra = elapsed / ctx->interval_ns;
            ctx->expirations += extra;
            ctx->target_ns += (extra + 1) * ctx->interval_ns;
        } else {
            ctx->armed = false;
        }
        spinlock_release(&ctx->lock);
        wait_queue_wake_all(&ctx->wq);
        return 0;
    }

    spinlock_release(&ctx->lock);
    return 0;
}

int64_t sys_timerfd_gettime(int fd, struct itimerspec_kernel *curr_value) {
    if (!curr_value)
        return -14; /* -EFAULT */

    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return -9; /* -EBADF */

    vfs_node_t *node = proc->fds[fd]->node;
    if (node->ops != &g_timerfd_ops || !node->device_data)
        return -22; /* -EINVAL */

    timerfd_ctx_t *ctx = (timerfd_ctx_t *)node->device_data;
    uint64_t now = timerfd_now_ns(ctx->clockid);

    struct itimerspec_kernel k_curr;
    memset(&k_curr, 0, sizeof(struct itimerspec_kernel));

    spinlock_acquire(&ctx->lock);
    if (ctx->armed) {
        if (ctx->target_ns > now) {
            uint64_t diff = ctx->target_ns - now;
            k_curr.it_value.tv_sec = (int64_t)(diff / 1000000000ULL);
            k_curr.it_value.tv_nsec = (int64_t)(diff % 1000000000ULL);
        } else {
            k_curr.it_value.tv_sec = 0;
            k_curr.it_value.tv_nsec = 1;
        }
        k_curr.it_interval.tv_sec = (int64_t)(ctx->interval_ns / 1000000000ULL);
        k_curr.it_interval.tv_nsec = (int64_t)(ctx->interval_ns % 1000000000ULL);
    }
    spinlock_release(&ctx->lock);

    if (!copy_to_user((uintptr_t)curr_value, &k_curr, sizeof(struct itimerspec_kernel))) {
        if ((uintptr_t)curr_value > USER_ADDR_MAX) {
            memcpy(curr_value, &k_curr, sizeof(struct itimerspec_kernel));
        } else {
            return -14; /* -EFAULT */
        }
    }

    return 0;
}
