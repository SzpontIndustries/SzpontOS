/*
 * SzpontOS - Linux-compatible inotify(7) Kernel Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <fs/inotify.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <mm/usercopy.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

static vfs_ops_t g_inotify_ops;
static inotify_instance_t *g_inotify_head = NULL;
static spinlock_t g_inotify_global_lock = SPINLOCK_INIT;

static ssize_t inotify_vfs_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer)
        return -14; /* -EFAULT */
    if (size < sizeof(struct inotify_event))
        return -22; /* -EINVAL */

    inotify_instance_t *inst = (inotify_instance_t *)node->device_data;
    size_t bytes_copied = 0;

    while (1) {
        spinlock_acquire(&inst->lock);
        if (inst->events_head != NULL) {
            break;
        }

        if (inst->flags & IN_NONBLOCK) {
            spinlock_release(&inst->lock);
            return -11; /* -EAGAIN */
        }

        process_t *proc = sched_get_current_process();
        if (proc) {
            for (int i = 0; i < MAX_FD; i++) {
                if (proc->fds[i] && proc->fds[i]->node == node) {
                    if (proc->fds[i]->flags & 0x0800 /* O_NONBLOCK */) {
                        spinlock_release(&inst->lock);
                        return -11; /* -EAGAIN */
                    }
                    break;
                }
            }
        }

        spinlock_release(&inst->lock);
        wait_queue_wait(&inst->wq);
    }

    /* We have at least one event */
    while (inst->events_head != NULL) {
        inotify_event_node_t *ev = inst->events_head;
        size_t event_size = sizeof(struct inotify_event) + ev->len;

        if (size - bytes_copied < event_size) {
            if (bytes_copied > 0)
                break;
            spinlock_release(&inst->lock);
            return -22; /* -EINVAL: buffer too small for first event */
        }

        struct inotify_event header;
        header.wd = ev->wd;
        header.mask = ev->mask;
        header.cookie = ev->cookie;
        header.len = ev->len;

        uintptr_t dest_user = (uintptr_t)buffer + bytes_copied;
        if (!copy_to_user(dest_user, &header, sizeof(header))) {
            if (dest_user > USER_ADDR_MAX) {
                memcpy((void *)dest_user, &header, sizeof(header));
            } else {
                spinlock_release(&inst->lock);
                return -14; /* -EFAULT */
            }
        }

        if (ev->len > 0 && ev->name) {
            uintptr_t name_dest = dest_user + sizeof(header);
            char padded_name[256];
            memset(padded_name, 0, ev->len);
            strncpy(padded_name, ev->name, ev->len - 1);
            if (!copy_to_user(name_dest, padded_name, ev->len)) {
                if (name_dest > USER_ADDR_MAX) {
                    memcpy((void *)name_dest, padded_name, ev->len);
                } else {
                    spinlock_release(&inst->lock);
                    return -14; /* -EFAULT */
                }
            }
        }

        bytes_copied += event_size;

        /* Dequeue */
        inst->events_head = ev->next;
        if (!inst->events_head)
            inst->events_tail = NULL;
        inst->event_count--;

        if (ev->name)
            kfree(ev->name);
        kfree(ev);
    }

    spinlock_release(&inst->lock);
    return (ssize_t)bytes_copied;
}

static int inotify_vfs_close(vfs_node_t *node) {
    if (!node)
        return 0;
    inotify_instance_t *inst = (inotify_instance_t *)node->device_data;
    if (inst) {
        /* Remove from global instance list */
        spinlock_acquire(&g_inotify_global_lock);
        inotify_instance_t **cur = &g_inotify_head;
        while (*cur) {
            if (*cur == inst) {
                *cur = inst->global_next;
                break;
            }
            cur = &(*cur)->global_next;
        }
        spinlock_release(&g_inotify_global_lock);

        spinlock_acquire(&inst->lock);
        /* Free all watches */
        inotify_watch_t *w = inst->watches;
        while (w) {
            inotify_watch_t *next = w->next;
            kfree(w);
            w = next;
        }
        inst->watches = NULL;

        /* Free all queued events */
        inotify_event_node_t *ev = inst->events_head;
        while (ev) {
            inotify_event_node_t *next = ev->next;
            if (ev->name)
                kfree(ev->name);
            kfree(ev);
            ev = next;
        }
        inst->events_head = NULL;
        inst->events_tail = NULL;
        inst->event_count = 0;
        spinlock_release(&inst->lock);

        wait_queue_wake_all(&inst->wq);
        kfree(inst);
        node->device_data = NULL;
    }
    kfree(node);
    return 0;
}

static void inotify_ops_init(void) {
    static bool init = false;
    if (!init) {
        memset(&g_inotify_ops, 0, sizeof(vfs_ops_t));
        g_inotify_ops.read = inotify_vfs_read;
        g_inotify_ops.close = inotify_vfs_close;
        init = true;
    }
}

int64_t sys_inotify_init1(int flags) {
    if (flags & ~(IN_CLOEXEC | IN_NONBLOCK))
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    inotify_ops_init();

    int fd = -1;
    for (int i = 3; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1)
        return -24; /* -EMFILE */

    inotify_instance_t *inst = (inotify_instance_t *)kzalloc(sizeof(inotify_instance_t));
    if (!inst)
        return -12; /* -ENOMEM */

    inst->lock = SPINLOCK_INIT;
    inst->watches = NULL;
    inst->next_wd = 1;
    inst->events_head = NULL;
    inst->events_tail = NULL;
    inst->event_count = 0;
    inst->flags = (uint32_t)flags;
    wait_queue_init(&inst->wq);

    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) {
        kfree(inst);
        return -12; /* -ENOMEM */
    }

    ksnprintf(node->name, sizeof(node->name), "inotify:[%d]", fd);
    node->flags = VFS_TYPE_CHARDEVICE;
    node->ops = &g_inotify_ops;
    node->device_data = inst;

    file_descriptor_t *fdesc = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    if (!fdesc) {
        kfree(node);
        kfree(inst);
        return -12; /* -ENOMEM */
    }

    fdesc->node = node;
    /* Linux creates inotify descriptors O_RDWR (see timerfd note). */
    fdesc->flags = O_RDWR;
    if (flags & IN_NONBLOCK)
        fdesc->flags |= 0x0800; /* O_NONBLOCK */
    fdesc->refcount = 1;

    proc->fds[fd] = fdesc;
    if (flags & IN_CLOEXEC)
        proc->fd_cloexec[fd] = true;

    /* Register globally */
    spinlock_acquire(&g_inotify_global_lock);
    inst->global_next = g_inotify_head;
    g_inotify_head = inst;
    spinlock_release(&g_inotify_global_lock);

    return fd;
}

int64_t sys_inotify_init(void) {
    return sys_inotify_init1(0);
}

int64_t sys_inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -9; /* -EBADF */

    file_descriptor_t *fdesc = proc->fds[fd];
    if (!fdesc->node || fdesc->node->ops != &g_inotify_ops)
        return -22; /* -EINVAL: Not an inotify fd */

    inotify_instance_t *inst = (inotify_instance_t *)fdesc->node->device_data;
    if (!inst)
        return -22;

    if (!pathname)
        return -14; /* -EFAULT */

    char kpath[256];
    if (!copy_string_from_user(kpath, (uintptr_t)pathname, sizeof(kpath))) {
        if ((uintptr_t)pathname > USER_ADDR_MAX) {
            strncpy(kpath, pathname, sizeof(kpath) - 1);
            kpath[sizeof(kpath) - 1] = '\0';
        } else {
            return -14; /* -EFAULT */
        }
    }

    char resolved[256];
    if (kpath[0] != '/') {
        if (strcmp(proc->cwd, "/") == 0)
            ksnprintf(resolved, sizeof(resolved), "/%s", kpath);
        else
            ksnprintf(resolved, sizeof(resolved), "%s/%s", proc->cwd, kpath);
    } else {
        strncpy(resolved, kpath, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    /* Verify that path exists */
    vfs_node_t *target = vfs_lookup(resolved);
    if (!target)
        return -2; /* -ENOENT */

    if ((mask & IN_ONLYDIR) && target->flags != VFS_TYPE_DIRECTORY)
        return -20; /* -ENOTDIR */

    spinlock_acquire(&inst->lock);

    /* Check if watch already exists for this path */
    inotify_watch_t *w = inst->watches;
    while (w) {
        if (strcmp(w->path, resolved) == 0) {
            if (mask & IN_MASK_ADD)
                w->mask |= mask;
            else
                w->mask = mask;
            int wd = w->wd;
            spinlock_release(&inst->lock);
            return wd;
        }
        w = w->next;
    }

    inotify_watch_t *new_watch = (inotify_watch_t *)kzalloc(sizeof(inotify_watch_t));
    if (!new_watch) {
        spinlock_release(&inst->lock);
        return -12; /* -ENOMEM */
    }

    new_watch->wd = inst->next_wd++;
    strncpy(new_watch->path, resolved, sizeof(new_watch->path) - 1);
    new_watch->mask = mask;
    new_watch->node = target;
    new_watch->next = inst->watches;
    inst->watches = new_watch;

    int assigned_wd = new_watch->wd;
    spinlock_release(&inst->lock);
    return assigned_wd;
}

int64_t sys_inotify_rm_watch(int fd, int wd) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -9; /* -EBADF */

    file_descriptor_t *fdesc = proc->fds[fd];
    if (!fdesc->node || fdesc->node->ops != &g_inotify_ops)
        return -22; /* -EINVAL: Not an inotify fd */

    inotify_instance_t *inst = (inotify_instance_t *)fdesc->node->device_data;
    if (!inst)
        return -22;

    spinlock_acquire(&inst->lock);
    inotify_watch_t *prev = NULL;
    inotify_watch_t *cur = inst->watches;
    while (cur) {
        if (cur->wd == wd) {
            if (prev)
                prev->next = cur->next;
            else
                inst->watches = cur->next;
            kfree(cur);
            spinlock_release(&inst->lock);
            return 0;
        }
        prev = cur;
        cur = cur->next;
    }
    spinlock_release(&inst->lock);
    return -22; /* -EINVAL: wd not found */
}

void inotify_emit(const char *path, const char *name, uint32_t mask, uint32_t cookie) {
    if (!path)
        return;

    spinlock_acquire(&g_inotify_global_lock);
    inotify_instance_t *inst = g_inotify_head;
    while (inst) {
        spinlock_acquire(&inst->lock);
        inotify_watch_t *w = inst->watches;
        while (w) {
            if (strcmp(w->path, path) == 0) {
                if (w->mask & mask) {
                    /* Allocate event */
                    inotify_event_node_t *ev = (inotify_event_node_t *)kzalloc(sizeof(inotify_event_node_t));
                    if (ev) {
                        ev->wd = w->wd;
                        ev->mask = mask;
                        ev->cookie = cookie;
                        if (name && name[0]) {
                            size_t nlen = strlen(name);
                            ev->len = (uint32_t)((nlen + 1 + 3) & ~3);
                            ev->name = (char *)kmalloc(ev->len);
                            if (ev->name) {
                                memset(ev->name, 0, ev->len);
                                strcpy(ev->name, name);
                            } else {
                                ev->len = 0;
                            }
                        } else {
                            ev->len = 0;
                            ev->name = NULL;
                        }

                        ev->next = NULL;
                        if (inst->events_tail)
                            inst->events_tail->next = ev;
                        else
                            inst->events_head = ev;
                        inst->events_tail = ev;
                        inst->event_count++;
                    }
                }
            }
            w = w->next;
        }
        spinlock_release(&inst->lock);
        wait_queue_wake_all(&inst->wq);
        inst = inst->global_next;
    }
    spinlock_release(&g_inotify_global_lock);
}

bool inotify_has_pollin(vfs_node_t *node) {
    if (!node || !node->device_data)
        return false;
    inotify_instance_t *inst = (inotify_instance_t *)node->device_data;
    spinlock_acquire(&inst->lock);
    bool ready = (inst->events_head != NULL);
    spinlock_release(&inst->lock);
    return ready;
}
