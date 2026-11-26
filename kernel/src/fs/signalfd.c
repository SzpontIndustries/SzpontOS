/*
 * SzpontOS - Linux-compatible signalfd(2) Kernel Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <fs/signalfd.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <mm/usercopy.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

static vfs_ops_t g_signalfd_ops;

static ssize_t signalfd_vfs_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer)
        return -14; /* -EFAULT */
    if (size < sizeof(struct signalfd_siginfo))
        return -22; /* -EINVAL */

    signalfd_ctx_t *ctx = (signalfd_ctx_t *)node->device_data;
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    struct signalfd_siginfo sinfo;
    memset(&sinfo, 0, sizeof(struct signalfd_siginfo));

    while (1) {
        spinlock_acquire(&ctx->lock);
        sigset_t match = proc->pending_signals & ctx->sigmask;
        if (match != 0) {
            /* Find lowest matching pending signal */
            int sig = 0;
            for (int s = 1; s < 32; s++) {
                if (match & (1U << s)) {
                    sig = s;
                    break;
                }
            }

            if (sig > 0) {
                proc->pending_signals &= ~(1U << sig);
                sinfo.ssi_signo = (uint32_t)sig;
                sinfo.ssi_pid = (uint32_t)proc->pid;
                sinfo.ssi_uid = (uint32_t)proc->uid;
                spinlock_release(&ctx->lock);
                break;
            }
        }

        /* Check nonblock */
        if (ctx->flags & SFD_NONBLOCK) {
            spinlock_release(&ctx->lock);
            return -11; /* -EAGAIN */
        }

        for (int i = 0; i < MAX_FD; i++) {
            if (proc->fds[i] && proc->fds[i]->node == node) {
                if (proc->fds[i]->flags & 0x0800 /* O_NONBLOCK */) {
                    spinlock_release(&ctx->lock);
                    return -11; /* -EAGAIN */
                }
                break;
            }
        }

        /* Check if unblocked signals are pending to break wait */
        thread_t *sfd_self = sched_get_current_thread();
        uint32_t sfd_pend = proc->pending_signals;
        uint32_t sfd_mask = 0;
        if (sfd_self) {
            sfd_pend |= sfd_self->thread_pending;
            sfd_mask = sfd_self->blocked_signals;
        }
        if (sfd_pend & ~sfd_mask) {
            spinlock_release(&ctx->lock);
            return -4; /* -EINTR */
        }

        spinlock_release(&ctx->lock);
        wait_queue_wait(&ctx->wq);
    }

    if (!copy_to_user((uintptr_t)buffer, &sinfo, sizeof(struct signalfd_siginfo))) {
        if ((uintptr_t)buffer > USER_ADDR_MAX) {
            memcpy(buffer, &sinfo, sizeof(struct signalfd_siginfo));
        } else {
            return -14; /* -EFAULT */
        }
    }

    return sizeof(struct signalfd_siginfo);
}

static int signalfd_vfs_close(vfs_node_t *node) {
    if (!node || !node->device_data)
        return 0;
    signalfd_ctx_t *ctx = (signalfd_ctx_t *)node->device_data;
    kfree(ctx);
    node->device_data = NULL;
    return 0;
}

static void signalfd_ops_init(void) {
    static bool init = false;
    if (!init) {
        memset(&g_signalfd_ops, 0, sizeof(vfs_ops_t));
        g_signalfd_ops.read = signalfd_vfs_read;
        g_signalfd_ops.close = signalfd_vfs_close;
        init = true;
    }
}

bool signalfd_has_pollin(vfs_node_t *node) {
    if (!node || !node->device_data)
        return false;
    process_t *proc = sched_get_current_process();
    if (!proc)
        return false;

    signalfd_ctx_t *ctx = (signalfd_ctx_t *)node->device_data;
    spinlock_acquire(&ctx->lock);
    bool ready = ((proc->pending_signals & ctx->sigmask) != 0);
    spinlock_release(&ctx->lock);
    return ready;
}

void signalfd_notify(void *proc_ptr, int sig) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc || sig < 1 || sig >= 32)
        return;

    for (int i = 0; i < MAX_FD; i++) {
        if (proc->fds[i] && proc->fds[i]->node && proc->fds[i]->node->ops == &g_signalfd_ops) {
            signalfd_ctx_t *ctx = (signalfd_ctx_t *)proc->fds[i]->node->device_data;
            if (ctx && (ctx->sigmask & (1U << sig))) {
                wait_queue_wake_all(&ctx->wq);
            }
        }
    }
}

int64_t sys_signalfd4(int fd, const sigset_t *mask, size_t sizemask, int flags) {
    if (sizemask != sizeof(sigset_t))
        return -22; /* -EINVAL */

    if (flags & ~(SFD_CLOEXEC | SFD_NONBLOCK))
        return -22; /* -EINVAL */

    if (!mask)
        return -14; /* -EFAULT */

    sigset_t k_mask = 0;
    if (!copy_from_user(&k_mask, (uintptr_t)mask, sizeof(sigset_t))) {
        if ((uintptr_t)mask > USER_ADDR_MAX) {
            memcpy(&k_mask, mask, sizeof(sigset_t));
        } else {
            return -14; /* -EFAULT */
        }
    }

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    signalfd_ops_init();

    if (fd >= 0) {
        /* Modify existing signalfd descriptor */
        if (fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
            return -9; /* -EBADF */
        vfs_node_t *node = proc->fds[fd]->node;
        if (node->ops != &g_signalfd_ops || !node->device_data)
            return -22; /* -EINVAL */

        signalfd_ctx_t *ctx = (signalfd_ctx_t *)node->device_data;
        spinlock_acquire(&ctx->lock);
        ctx->sigmask = k_mask;
        if (flags & SFD_NONBLOCK) {
            ctx->flags |= SFD_NONBLOCK;
            proc->fds[fd]->flags |= 0x0800; /* O_NONBLOCK */
        }
        spinlock_release(&ctx->lock);

        if (proc->pending_signals & k_mask) {
            wait_queue_wake_all(&ctx->wq);
        }
        return fd;
    }

    /* Allocate new signalfd descriptor */
    int new_fd = -1;
    for (int i = 3; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            new_fd = i;
            break;
        }
    }
    if (new_fd == -1)
        return -24; /* -EMFILE */

    signalfd_ctx_t *ctx = (signalfd_ctx_t *)kzalloc(sizeof(signalfd_ctx_t));
    if (!ctx)
        return -12; /* -ENOMEM */

    ctx->lock = SPINLOCK_INIT;
    ctx->sigmask = k_mask;
    ctx->flags = (uint32_t)flags;
    wait_queue_init(&ctx->wq);

    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) {
        kfree(ctx);
        return -12; /* -ENOMEM */
    }

    ksnprintf(node->name, sizeof(node->name), "signalfd:[%d]", new_fd);
    node->flags = VFS_TYPE_CHARDEVICE;
    node->ops = &g_signalfd_ops;
    node->device_data = ctx;

    file_descriptor_t *fdesc = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    if (!fdesc) {
        kfree(node);
        kfree(ctx);
        return -12; /* -ENOMEM */
    }

    fdesc->node = node;
    /* Linux creates signalfd descriptors O_RDWR (see timerfd note). */
    fdesc->flags = O_RDWR;
    if (flags & SFD_NONBLOCK)
        fdesc->flags |= 0x0800; /* O_NONBLOCK */
    fdesc->refcount = 1;

    proc->fds[new_fd] = fdesc;
    if (flags & SFD_CLOEXEC)
        proc->fd_cloexec[new_fd] = 1;

    return new_fd;
}

int64_t sys_signalfd(int fd, const sigset_t *mask, size_t sizemask) {
    return sys_signalfd4(fd, mask, sizemask, 0);
}
