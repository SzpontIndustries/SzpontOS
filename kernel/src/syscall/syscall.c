#include <syscall/syscall.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <sched/futex.h>
#include <kernel/module.h>
#include <kernel/signal.h>
#include <net/socket.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/shm.h>
#include <mm/usercopy.h>
#include <fs/vfs.h>
#include <fs/bcache.h>
#include <fs/elf.h>
#include <fs/pipe.h>
#include <fs/eventfd.h>
#include <fs/epoll.h>
#include <fs/inotify.h>
#include <fs/timerfd.h>
#include <fs/signalfd.h>
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <drivers/block.h>
#include <drivers/rtc.h>
#include <drivers/random.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/pit.h>
#include <arch/x86_64/io.h>
#include <kernel/smp.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/sysctl.h>
#include <kernel/kqueue.h>
#include <drivers/power.h>
#include <drivers/tty.h>
#include <drivers/evdev.h>
#include <drivers/pty.h>
#include <drivers/drm.h>
#include <drivers/mouse.h>
#include <drivers/ps2_mouse.h>
#include <drivers/xhci.h>
#include <drivers/ehci.h>

struct pollfd {
    int fd;
    short events;
    short revents;
};
#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID 0x01000000

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

extern void arch_enter_user_mode(uintptr_t rip, uintptr_t rsp);
extern void arch_syscall_return(void);
extern void syscall_arch_init(void);

static void build_at_path(int dirfd, const char *pathname, char *out, size_t out_len);

#define S_IFMT 0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    off_t st_size;
};

struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
};

struct statfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

/*
 * Usercopy helpers for syscall layer:
 * Validates userspace pointers page-by-page through HHDM and returns EFAULT (-14)
 * on invalid addresses, while safely permitting kernel-space buffers (> USER_ADDR_MAX).
 */
static inline bool put_user_buffer(void *dst, const void *src, size_t len) {
    if (!dst || len == 0)
        return true;
    if ((uintptr_t)dst > USER_ADDR_MAX) {
        memcpy(dst, src, len);
        return true;
    }
    return copy_to_user((uintptr_t)dst, src, len);
}

static inline bool get_user_buffer(void *dst, const void *src, size_t len) {
    if (!src || len == 0)
        return true;
    if ((uintptr_t)src > USER_ADDR_MAX) {
        memcpy(dst, src, len);
        return true;
    }
    return copy_from_user(dst, (uintptr_t)src, len);
}

static inline bool get_user_string(char *dst, const char *src, size_t max_len) {
    if (!src || !dst || max_len == 0)
        return false;
    if ((uintptr_t)src > USER_ADDR_MAX) {
        strncpy(dst, src, max_len - 1);
        dst[max_len - 1] = '\0';
        return true;
    }
    return copy_string_from_user(dst, (uintptr_t)src, max_len) >= 0;
}

/* Validate all pages, including permissions at every paging level. */
static bool user_buffer(const void *buf, size_t count, bool write) {
    process_t *proc = sched_get_current_process();
    return proc && vmm_user_access(proc->pagemap, (uintptr_t)buf, count, write);
}

static uint64_t user_protection(int prot) {
    /* PROT_NONE retains the frame as a supervisor-only leaf, so protection
     * can be restored and munmap/fork can still find its physical owner. */
    uint64_t flags = prot ? VMM_FLAG_USER : 0;
    if (prot & 2)
        flags |= VMM_FLAG_WRITABLE;
    if (!(prot & 4))
        flags |= VMM_FLAG_NO_EXECUTE;
    return flags;
}

static int64_t sys_read(int fd, void *buf, size_t count) {
    if (count == 0)
        return 0;

    if (!user_buffer(buf, count, true))
        return -14; /* EFAULT */

    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD)
        return -1;

    if (!proc->fds[fd] || !proc->fds[fd]->node) {
        if (fd == 0) {
            char *p = (char *)buf;
            size_t read_bytes = 0;
            while (read_bytes < count) {
                char c = 0;
                while (!c) {
                    if (keyboard_has_char()) {
                        c = keyboard_getc();
                    } else if (serial_received()) {
                        c = serial_getc();
                    } else {
                        __asm__ volatile("sti" ::: "memory");
                        if (sched_get_current_thread() != NULL) {
                            sched_yield();
                        }
                        keyboard_relax();
                    }
                }

                /* Ctrl+C */
                if (c == 0x03) {
                    if (proc && proc->pid > 1) {
                        if (proc->pgid > 1)
                            process_signal_pgrp(proc->pgid, SIGINT);
                        else
                            process_send_signal(proc, SIGINT);
                    }
                    p[0] = 0x03;
                    return 1;
                }

                /* Ctrl+D / EOF */
                if (c == 0x04) {
                    if (read_bytes == 0)
                        return 0;
                    break;
                }

                p[read_bytes++] = c;
                if (c == '\r' || c == '\n') {
                    break;
                }
            }
            return (int64_t)read_bytes;
        }
        return -1;
    }

    file_descriptor_t *f = proc->fds[fd];
    if (!f->node->ops || !f->node->ops->read)
        return -1;

    ssize_t bytes = f->node->ops->read(f->node, f->offset, count, buf);
    if (bytes > 0) {
        f->offset += bytes;
    }
    return bytes;
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
    if (count == 0)
        return 0;

    if (!user_buffer(buf, count, false))
        return -14; /* EFAULT */

    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD)
        return -1;

    if (!proc->fds[fd] || !proc->fds[fd]->node) {
        if (fd == 1 || fd == 2) {
            fb_console_write((const char *)buf, count);
            serial_write((const char *)buf, count);
            return (int64_t)count;
        }
        return -1;
    }

    file_descriptor_t *f = proc->fds[fd];
    if (!f->node->ops || !f->node->ops->write) {
        if (fd == 1 || fd == 2) {
            fb_console_write((const char *)buf, count);
            serial_write((const char *)buf, count);
            return (int64_t)count;
        }
        return -1;
    }

    ssize_t bytes = f->node->ops->write(f->node, f->offset, count, buf);
    if (bytes > 0) {
        f->offset += bytes;
    }
    return bytes;
}

__attribute__((unused)) static void ensure_std_fd(process_t *proc, int fd) {
    if (!proc || fd < 0 || fd >= 3)
        return;
    if (!proc->fds[fd]) {
        vfs_node_t *tty = vfs_lookup("/dev/tty");
        if (tty) {
            file_descriptor_t *f = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
            f->node = tty;
            f->flags = (fd == 0) ? O_RDONLY : O_WRONLY;
            f->refcount = 1;
            proc->fds[fd] = f;
        }
    }
}

static int64_t sys_open(const char *path, int flags, mode_t mode) {
    process_t *proc = sched_get_current_process();
    if (!proc || !path)
        return -22; /* EINVAL */

    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */

    char full_path[256];
    if (vfs_resolve_path(kpath, full_path, sizeof(full_path)) != 0)
        return -2; /* ENOENT */

    /* Special handling for /dev/tty (process controlling terminal) */
    vfs_node_t *node = NULL;
    if (strcmp(full_path, "/dev/tty") == 0) {
        if (!proc->has_ctty || !proc->ctty) {
            return -6; /* -ENXIO: No controlling terminal */
        }
        node = proc->ctty;
    } else {
        node = vfs_lookup(full_path);
    }
    bool newly_created = false;
    if (!node) {
        /* File doesn't exist. Check if O_CREAT is set */
        if (flags & O_CREAT) {
            char parent_path[256];
            char file_name[128];
            strncpy(parent_path, full_path, sizeof(parent_path) - 1);
            parent_path[sizeof(parent_path) - 1] = '\0';

            char *last_slash = strrchr(parent_path, '/');
            if (!last_slash || last_slash == parent_path) {
                strcpy(file_name, last_slash ? last_slash + 1 : parent_path);
                strcpy(parent_path, "/");
            } else {
                strcpy(file_name, last_slash + 1);
                *last_slash = '\0';
            }

            vfs_node_t *parent = vfs_lookup(parent_path);
            if (!parent)
                return -2; /* ENOENT */
            if (parent->flags != VFS_TYPE_DIRECTORY)
                return -20; /* ENOTDIR */

            if (vfs_check_permission(parent, VFS_WRITE | VFS_EXEC) != 0) {
                return -13; /* EACCES: Permission denied in parent directory */
            }

            if (!parent->ops || !parent->ops->create)
                return -38; /* ENOSYS */

            mode_t actual_mode = (mode ? mode : 0666) & ~proc->umask;
            int r = parent->ops->create(parent, file_name, actual_mode);
            if (r != 0)
                return (r < 0) ? r : -5;

            node = vfs_lookup(full_path);
            if (!node)
                return -2;
            newly_created = true;
            inotify_emit(parent_path, file_name, IN_CREATE, 0);
        } else {
            return -2; /* ENOENT: File not found */
        }
    } else {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            return -17; /* EEXIST */
        }
    }

    /* Check access permissions (POSIX: permission check is bypassed for newly created file) */
    int access_mask = 0;
    if ((flags & 3) == O_RDONLY) {
        access_mask = VFS_READ;
    } else if ((flags & 3) == O_WRONLY) {
        access_mask = VFS_WRITE;
    } else if ((flags & 3) == O_RDWR) {
        access_mask = VFS_READ | VFS_WRITE;
    }

    if (!newly_created && vfs_check_permission(node, access_mask) != 0) {
        return -13; /* EACCES: Permission denied */
    }

    /* Truncate if O_TRUNC requested with write permission */
    if ((flags & O_TRUNC) && (access_mask & VFS_WRITE)) {
        if (node->ops && node->ops->truncate) {
            node->ops->truncate(node, 0);
        } else {
            node->length = 0;
        }
    }

    /* Call open operation if defined */
    vfs_node_t *open_node = node;
    if (node->ops && node->ops->open) {
        vfs_node_t *cloned = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
        memcpy(cloned, node, sizeof(vfs_node_t));
        int r = cloned->ops->open(cloned, flags);
        if (r < 0) {
            kfree(cloned);
            return r;
        }
        open_node = cloned;
    }

    /* Find free file descriptor */
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        if (open_node != node) {
            kfree(open_node);
        }
        return -1;
    }

    file_descriptor_t *f = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    f->node = open_node;
    f->flags = flags;
    f->offset = (flags & O_APPEND) ? (off_t)open_node->length : 0;
    f->refcount = 1;

    proc->fds[fd] = f;
    proc->fd_cloexec[fd] = (flags & 0x80000) ? true : false; /* O_CLOEXEC */

    /* POSIX controlling terminal acquisition for session leaders */
    if (!(flags & O_NOCTTY)) {
        if (proc->sid == proc->pid && (!proc->has_ctty || !proc->ctty)) {
            if (open_node->flags == VFS_TYPE_CHARDEVICE &&
                (strncmp(open_node->name, "console", 7) == 0 ||
                 strncmp(open_node->name, "serial", 6) == 0 ||
                 strncmp(open_node->name, "pts", 3) == 0 ||
                 strncmp(full_path, "/dev/pts", 8) == 0 ||
                 pty_is_slave_node(open_node))) {
                proc->has_ctty = true;
                proc->ctty = node;
            }
        }
    }

    return fd;
}

static ssize_t pipe_read_op(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer || size == 0)
        return 0;
    pipe_chan_t *p = (pipe_chan_t *)node->device_data;
    uint8_t *buf = (uint8_t *)buffer;

    process_t *proc = sched_get_current_process();
    bool is_nonblock = false;
    if (proc) {
        for (int i = 0; i < MAX_FD; i++) {
            if (proc->fds[i] && proc->fds[i]->node == node) {
                if (proc->fds[i]->flags & 0x800) {
                    is_nonblock = true;
                }
                break;
            }
        }
    }

    if (is_nonblock && p->count == 0) {
        if (p->writers <= 0) {
            return 0; /* EOF: all writers closed */
        }
        return -11; /* -EAGAIN */
    }

    while (p->count == 0) {
        if (p->writers <= 0) {
            return 0; /* EOF: all writers closed */
        }
        thread_sleep(1);
    }

    size_t read_bytes = 0;
    while (read_bytes < size && p->count > 0) {
        buf[read_bytes++] = p->data[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
        p->count--;
    }

    return (ssize_t)read_bytes;
}

static ssize_t pipe_write_op(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer || size == 0)
        return 0;
    pipe_chan_t *p = (pipe_chan_t *)node->device_data;
    const uint8_t *buf = (const uint8_t *)buffer;

    if (p->readers <= 0) {
        return -1; /* EPIPE: broken pipe */
    }

    process_t *proc = sched_get_current_process();
    bool is_nonblock = false;
    if (proc) {
        for (int i = 0; i < MAX_FD; i++) {
            if (proc->fds[i] && proc->fds[i]->node == node) {
                if (proc->fds[i]->flags & 0x800) {
                    is_nonblock = true;
                }
                break;
            }
        }
    }

    size_t written = 0;
    while (written < size) {
        while (p->count >= PIPE_BUF_SIZE) {
            if (p->readers <= 0)
                return -1;
            if (is_nonblock) {
                return (written > 0) ? (ssize_t)written : -11; /* -EAGAIN */
            }
            thread_sleep(1);
        }
        p->data[p->head] = buf[written++];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
        p->count++;
    }

    return (ssize_t)written;
}

static vfs_ops_t g_pipe_read_ops = {.read = pipe_read_op,
                                    .write = NULL,
                                    .open = NULL,
                                    .close = NULL,
                                    .readdir = NULL,
                                    .finddir = NULL,
                                    .create = NULL,
                                    .mkdir = NULL,
                                    .chmod = NULL,
                                    .chown = NULL,
                                    .unlink = NULL};

static vfs_ops_t g_pipe_write_ops = {.read = NULL,
                                     .write = pipe_write_op,
                                     .open = NULL,
                                     .close = NULL,
                                     .readdir = NULL,
                                     .finddir = NULL,
                                     .create = NULL,
                                     .mkdir = NULL,
                                     .chmod = NULL,
                                     .chown = NULL,
                                     .unlink = NULL};

vfs_ops_t g_fifo_ops = {.read = pipe_read_op,
                        .write = pipe_write_op,
                        .open = NULL,
                        .close = NULL,
                        .readdir = NULL,
                        .finddir = NULL,
                        .create = NULL,
                        .mkdir = NULL,
                        .chmod = NULL,
                        .chown = NULL,
                        .unlink = NULL};

static int64_t sys_pipe2(int *pipefd, int flags) {
    if (!pipefd)
        return -22; /* -EINVAL */
    if (flags & ~(0x00080000 /* O_CLOEXEC */ | 0x0800 /* O_NONBLOCK */))
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    int fd0 = -1, fd1 = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            if (fd0 == -1)
                fd0 = i;
            else if (fd1 == -1) {
                fd1 = i;
                break;
            }
        }
    }
    if (fd0 == -1 || fd1 == -1)
        return -24; /* -EMFILE */

    pipe_chan_t *p = (pipe_chan_t *)kzalloc(sizeof(pipe_chan_t));
    if (!p)
        return -12; /* -ENOMEM */
    p->readers = 1;
    p->writers = 1;

    vfs_node_t *rnode = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!rnode) {
        kfree(p);
        return -12;
    }
    strcpy(rnode->name, "pipe_read");
    rnode->flags = VFS_TYPE_PIPE;
    rnode->device_data = p;
    rnode->ops = &g_pipe_read_ops;

    vfs_node_t *wnode = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!wnode) {
        kfree(rnode);
        kfree(p);
        return -12;
    }
    strcpy(wnode->name, "pipe_write");
    wnode->flags = VFS_TYPE_PIPE;
    wnode->device_data = p;
    wnode->ops = &g_pipe_write_ops;

    file_descriptor_t *f0 = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    if (!f0) {
        kfree(wnode);
        kfree(rnode);
        kfree(p);
        return -12;
    }
    f0->node = rnode;
    f0->flags = O_RDONLY | (flags & 0x0800 /* O_NONBLOCK */);
    f0->refcount = 1;

    file_descriptor_t *f1 = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    if (!f1) {
        kfree(f0);
        kfree(wnode);
        kfree(rnode);
        kfree(p);
        return -12;
    }
    f1->node = wnode;
    f1->flags = O_WRONLY | (flags & 0x0800 /* O_NONBLOCK */);
    f1->refcount = 1;

    proc->fds[fd0] = f0;
    proc->fds[fd1] = f1;

    if (flags & 0x00080000 /* O_CLOEXEC */) {
        proc->fd_cloexec[fd0] = true;
        proc->fd_cloexec[fd1] = true;
    }

    int kfds[2] = {fd0, fd1};
    if (!put_user_buffer(pipefd, kfds, sizeof(kfds))) {
        proc->fds[fd0] = NULL;
        proc->fds[fd1] = NULL;
        proc->fd_cloexec[fd0] = false;
        proc->fd_cloexec[fd1] = false;
        kfree(f0);
        kfree(f1);
        kfree(rnode);
        kfree(wnode);
        kfree(p);
        return -14; /* -EFAULT */
    }
    return 0;
}

static int64_t sys_pipe(int *pipefd) {
    return sys_pipe2(pipefd, 0);
}

static int64_t sys_close(int fd) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -1;

    epoll_on_fd_close(proc, fd);

    file_descriptor_t *f = proc->fds[fd];
    proc->fds[fd] = NULL;
    proc->fd_cloexec[fd] = false;

    if (f && f->node) {
        bool still_open = false;
        for (int i = 0; i < MAX_FD; i++) {
            if (proc->fds[i] && proc->fds[i]->node == f->node) {
                still_open = true;
                break;
            }
        }
        if (!still_open) {
            if (f->node->lock_type == 2 && f->node->lock_owner == proc->pid) {
                f->node->lock_type = 0;
                f->node->lock_owner = 0;
            } else if (f->node->lock_type == 1) {
                if (f->node->lock_count > 0)
                    f->node->lock_count--;
                if (f->node->lock_count == 0)
                    f->node->lock_type = 0;
            }
        }
    }

    fd_release(f);
    return 0;
}

static int64_t sys_dup(int oldfd) {
    process_t *proc = sched_get_current_process();
    if (!proc || oldfd < 0 || oldfd >= MAX_FD || !proc->fds[oldfd])
        return -1;

    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            proc->fds[i] = proc->fds[oldfd];
            proc->fds[i]->refcount++;
            proc->fd_cloexec[i] = false; /* dup clears cloexec per POSIX */
            return i;
        }
    }
    return -1;
}

static int64_t sys_dup2(int oldfd, int newfd) {
    process_t *proc = sched_get_current_process();
    if (!proc || oldfd < 0 || oldfd >= MAX_FD || newfd < 0 || newfd >= MAX_FD || !proc->fds[oldfd])
        return -1;

    if (oldfd == newfd)
        return newfd;

    if (proc->fds[newfd]) {
        sys_close(newfd);
    }

    proc->fds[newfd] = proc->fds[oldfd];
    proc->fds[newfd]->refcount++;
    proc->fd_cloexec[newfd] = false; /* dup2 clears cloexec per POSIX */
    return newfd;
}

static int64_t sys_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd)
        return -22; /* -EINVAL */
    if (flags & ~0x00080000 /* O_CLOEXEC */)
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc || oldfd < 0 || oldfd >= MAX_FD || newfd < 0 || newfd >= MAX_FD || !proc->fds[oldfd])
        return -9; /* -EBADF */

    if (proc->fds[newfd]) {
        sys_close(newfd);
    }

    proc->fds[newfd] = proc->fds[oldfd];
    proc->fds[newfd]->refcount++;
    proc->fd_cloexec[newfd] = (flags & 0x00080000 /* O_CLOEXEC */) ? true : false;
    return newfd;
}

static int64_t sys_fcntl(int fd, int cmd, uint64_t arg) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD)
        return -1;
    if (!proc->fds[fd])
        return -1;

    switch (cmd) {
    case 0: /* F_DUPFD */
    case 1030: /* F_DUPFD_CLOEXEC */ {
        int min_fd = (int)arg;
        if (min_fd < 0 || min_fd >= MAX_FD)
            return -1;
        for (int i = min_fd; i < MAX_FD; i++) {
            if (!proc->fds[i]) {
                proc->fds[i] = proc->fds[fd];
                proc->fds[i]->refcount++;
                proc->fd_cloexec[i] = (cmd == 1030);
                return i;
            }
        }
        return -1;
    }
    case 1: /* F_GETFD */
        return proc->fd_cloexec[fd] ? 1 : 0;
    case 2: /* F_SETFD */
        proc->fd_cloexec[fd] = (arg & 1) ? true : false;
        return 0;
    case 3: /* F_GETFL */
        return proc->fds[fd]->flags;
    case 4: /* F_SETFL */
        proc->fds[fd]->flags = (int)arg;
        return 0;
    default:
        return 0;
    }
}

static int64_t sys_brk(uintptr_t new_brk) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    if (new_brk == 0 || new_brk < proc->brk_start) {
        return proc->brk_current;
    }

    /* Guard: Prevent heap from expanding into mmap area (0x600000000000) or exceeding 256 MiB */
    if (new_brk >= 0x0000600000000000ULL || (new_brk - proc->brk_start) > (256 * 1024 * 1024ULL)) {
        return proc->brk_current;
    }

    if (new_brk > proc->brk_current) {
        uintptr_t start_page = ALIGN_DOWN(proc->brk_current, PAGE_SIZE);
        uintptr_t end_page = ALIGN_UP(new_brk, PAGE_SIZE);

        for (uintptr_t p = start_page; p < end_page; p += PAGE_SIZE) {
            if (!vmm_alloc_user_page(proc->pagemap, p, VMM_FLAG_WRITABLE)) {
                return proc->brk_current; /* Return current brk on allocation failure */
            }
        }
    }

    proc->brk_current = new_brk;
    return proc->brk_current;
}

static inline uint32_t vfs_type_to_dt(uint32_t vfs_type) {
    switch (vfs_type) {
    case 1: /* VFS_TYPE_FILE */
        return 8; /* DT_REG */
    case 2: /* VFS_TYPE_DIRECTORY */
        return 4; /* DT_DIR */
    case 3: /* VFS_TYPE_CHARDEVICE */
        return 2; /* DT_CHR */
    case 4: /* VFS_TYPE_BLOCKDEVICE */
        return 6; /* DT_BLK */
    case 5: /* VFS_TYPE_PIPE */
        return 1; /* DT_FIFO */
    case 6: /* VFS_TYPE_SYMLINK */
        return 10; /* DT_LNK */
    case 7: /* VFS_TYPE_SOCKET */
        return 12; /* DT_SOCK */
    default:
        return 0; /* DT_UNKNOWN */
    }
}

static int64_t sys_getdents(int fd, void *dirp, size_t count) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !dirp)
        return -1;

    file_descriptor_t *f = proc->fds[fd];
    if (!f->node || !f->node->ops || !f->node->ops->readdir)
        return -1;

    vfs_dirent_t *dent = f->node->ops->readdir(f->node, (uint32_t)f->offset);
    if (!dent)
        return 0;

    vfs_dirent_t out;
    memset(&out, 0, sizeof(out));
    strncpy(out.name, dent->name, sizeof(out.name) - 1);
    out.inode = dent->inode;
    out.type = vfs_type_to_dt(dent->type);

    size_t copy_size = sizeof(vfs_dirent_t);
    if (copy_size > count)
        copy_size = count;

    if (!put_user_buffer(dirp, &out, copy_size))
        return -14; /* -EFAULT */
    f->offset++;
    return copy_size;
}

static int64_t sys_stat(const char *path, struct stat *buf) {
    if (!path || !buf)
        return -22;

    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */

    char full_path[256];
    if (vfs_resolve_path(kpath, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup(full_path);
    if (!node)
        return -2;

    struct stat kbuf;
    memset(&kbuf, 0, sizeof(struct stat));
    kbuf.st_ino = node->inode;

    uint32_t type_flag = S_IFREG;
    if (node->flags == VFS_TYPE_DIRECTORY)
        type_flag = S_IFDIR;
    else if (node->flags == VFS_TYPE_CHARDEVICE)
        type_flag = S_IFCHR;
    else if (node->flags == VFS_TYPE_BLOCKDEVICE)
        type_flag = S_IFBLK;
    else if (node->flags == VFS_TYPE_PIPE)
        type_flag = S_IFIFO;
    else if (node->flags == VFS_TYPE_SYMLINK)
        type_flag = S_IFLNK;
    else if (node->flags == VFS_TYPE_SOCKET)
        type_flag = S_IFSOCK;

    kbuf.st_mode = type_flag | (node->permissions & 07777);
    kbuf.st_size = node->length;
    kbuf.st_uid = node->uid;
    kbuf.st_gid = node->gid;
    kbuf.st_rdev = node->rdev;

    if (!put_user_buffer(buf, &kbuf, sizeof(struct stat)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_lstat(const char *path, struct stat *buf) {
    if (!path || !buf)
        return -22;

    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */

    char full_path[256];
    if (vfs_resolve_path(kpath, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup_nofollow(full_path);
    if (!node)
        return -2;

    struct stat kbuf;
    memset(&kbuf, 0, sizeof(struct stat));
    kbuf.st_ino = node->inode;

    uint32_t type_flag = S_IFREG;
    if (node->flags == VFS_TYPE_DIRECTORY)
        type_flag = S_IFDIR;
    else if (node->flags == VFS_TYPE_CHARDEVICE)
        type_flag = S_IFCHR;
    else if (node->flags == VFS_TYPE_BLOCKDEVICE)
        type_flag = S_IFBLK;
    else if (node->flags == VFS_TYPE_PIPE)
        type_flag = S_IFIFO;
    else if (node->flags == VFS_TYPE_SYMLINK)
        type_flag = S_IFLNK;
    else if (node->flags == VFS_TYPE_SOCKET)
        type_flag = S_IFSOCK;

    kbuf.st_mode = type_flag | (node->permissions & 07777);
    kbuf.st_size = node->length;
    kbuf.st_uid = node->uid;
    kbuf.st_gid = node->gid;
    kbuf.st_rdev = node->rdev;

    if (!put_user_buffer(buf, &kbuf, sizeof(struct stat)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_fstat(int fd, struct stat *buf) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !buf)
        return -1;

    file_descriptor_t *f = proc->fds[fd];
    if (!f->node)
        return -1;

    struct stat kbuf;
    memset(&kbuf, 0, sizeof(struct stat));
    kbuf.st_ino = f->node->inode;

    uint32_t type_flag = S_IFREG;
    if (f->node->flags == VFS_TYPE_DIRECTORY)
        type_flag = S_IFDIR;
    else if (f->node->flags == VFS_TYPE_CHARDEVICE)
        type_flag = S_IFCHR;
    else if (f->node->flags == VFS_TYPE_BLOCKDEVICE)
        type_flag = S_IFBLK;
    else if (f->node->flags == VFS_TYPE_PIPE)
        type_flag = S_IFIFO;
    else if (f->node->flags == VFS_TYPE_SYMLINK)
        type_flag = S_IFLNK;
    else if (f->node->flags == VFS_TYPE_SOCKET)
        type_flag = S_IFSOCK;

    kbuf.st_mode = type_flag | (f->node->permissions & 07777);
    kbuf.st_size = f->node->length;
    kbuf.st_uid = f->node->uid;
    kbuf.st_gid = f->node->gid;
    kbuf.st_rdev = f->node->rdev;

    if (!put_user_buffer(buf, &kbuf, sizeof(struct stat)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_uname(struct utsname *buf) {
    if (!buf)
        return -22;

    struct utsname kubuf;
    memset(&kubuf, 0, sizeof(struct utsname));
    strcpy(kubuf.sysname, "SzpontOS");

    char hostname[64] = "szpontos";
    vfs_node_t *node = vfs_lookup("/etc/hostname");
    if (node && node->ops && node->ops->read) {
        char file_buf[64];
        ssize_t bytes = node->ops->read(node, 0, sizeof(file_buf) - 1, (uint8_t *)file_buf);
        if (bytes > 0) {
            file_buf[bytes] = '\0';
            while (bytes > 0 && (file_buf[bytes - 1] == '\n' || file_buf[bytes - 1] == '\r' ||
                                 file_buf[bytes - 1] == ' ' || file_buf[bytes - 1] == '\t')) {
                file_buf[bytes - 1] = '\0';
                bytes--;
            }
            if (bytes > 0) {
                strncpy(hostname, file_buf, sizeof(hostname) - 1);
                hostname[sizeof(hostname) - 1] = '\0';
            }
        }
    }
    strcpy(kubuf.nodename, hostname);
    strcpy(kubuf.release, "0.1.0");
    strcpy(kubuf.version, "SzpontOS 0.1.0 (Higher-Half x86_64)");
    strcpy(kubuf.machine, "x86_64");

    if (!put_user_buffer(buf, &kubuf, sizeof(struct utsname)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_getcwd(char *buf, size_t size) {
    process_t *proc = sched_get_current_process();
    if (!proc || !buf || size == 0)
        return -22;

    size_t cwd_len = strlen(proc->cwd) + 1;
    if (size < cwd_len)
        return -34; /* -ERANGE */

    if (!put_user_buffer(buf, proc->cwd, cwd_len))
        return -14; /* -EFAULT */
    return (int64_t)buf;
}

static int64_t sys_chdir(const char *path) {
    process_t *proc = sched_get_current_process();
    if (!proc || !path)
        return -22;

    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */

    char full_path[256];
    if (kpath[0] == '/') {
        strncpy(full_path, kpath, sizeof(full_path) - 1);
    } else {
        if (strcmp(proc->cwd, "/") == 0) {
            ksnprintf(full_path, sizeof(full_path), "/%s", kpath);
        } else {
            ksnprintf(full_path, sizeof(full_path), "%s/%s", proc->cwd, kpath);
        }
    }
    full_path[sizeof(full_path) - 1] = '\0';

    vfs_node_t *node = vfs_lookup(full_path);
    if (!node || node->flags != VFS_TYPE_DIRECTORY)
        return -1;

    if (vfs_check_permission(node, VFS_EXEC) != 0) {
        return -1; /* EACCES */
    }

    char norm_cwd[256];
    vfs_normalize_path(full_path, norm_cwd, sizeof(norm_cwd));
    strncpy(proc->cwd, norm_cwd, sizeof(proc->cwd) - 1);
    proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    return 0;
}

static int64_t sys_fchdir(int fd) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return -9; /* -EBADF */

    vfs_node_t *node = proc->fds[fd]->node;
    if (node->flags != VFS_TYPE_DIRECTORY)
        return -20; /* -ENOTDIR */

    if (vfs_check_permission(node, VFS_EXEC) != 0)
        return -13; /* -EACCES */

    char full_path[256];
    if (node->name[0] == '/') {
        strncpy(full_path, node->name, sizeof(full_path) - 1);
    } else {
        ksnprintf(full_path, sizeof(full_path), "/%s", node->name);
    }
    full_path[sizeof(full_path) - 1] = '\0';

    char norm_cwd[256];
    vfs_normalize_path(full_path, norm_cwd, sizeof(norm_cwd));
    strncpy(proc->cwd, norm_cwd, sizeof(proc->cwd) - 1);
    proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    return 0;
}

static int64_t sys_setuid(uid_t uid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    if (proc->euid == 0) {
        proc->uid = uid;
        proc->euid = uid;
        proc->suid = uid;
        return 0;
    }

    if (uid == proc->uid || uid == proc->euid || uid == proc->suid) {
        proc->euid = uid;
        return 0;
    }

    return -1; /* EPERM */
}

static int64_t sys_setgid(gid_t gid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    if (proc->euid == 0) {
        proc->gid = gid;
        proc->egid = gid;
        proc->sgid = gid;
        return 0;
    }

    if (gid == proc->gid || gid == proc->egid || gid == proc->sgid) {
        proc->egid = gid;
        return 0;
    }

    return -1; /* EPERM */
}

static int64_t sys_seteuid(uid_t euid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    if (proc->euid == 0 || euid == proc->uid || euid == proc->suid) {
        proc->euid = euid;
        return 0;
    }
    return -1; /* EPERM */
}

static int64_t sys_setegid(gid_t egid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    if (proc->euid == 0 || egid == proc->gid || egid == proc->sgid) {
        proc->egid = egid;
        return 0;
    }
    return -1; /* EPERM */
}

static int64_t sys_setreuid(uid_t ruid, uid_t euid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    uid_t old_ruid = proc->uid;
    uid_t old_euid = proc->euid;
    if (ruid != (uid_t)-1) {
        if (proc->euid == 0 || ruid == proc->uid || ruid == proc->euid) {
            proc->uid = ruid;
        } else
            return -1;
    }
    if (euid != (uid_t)-1) {
        if (old_euid == 0 || euid == old_ruid || euid == old_euid || euid == proc->suid) {
            proc->euid = euid;
        } else
            return -1;
    }
    if (ruid != (uid_t)-1 || (euid != (uid_t)-1 && euid != old_ruid)) {
        proc->suid = proc->euid;
    }
    return 0;
}

static int64_t sys_setregid(gid_t rgid, gid_t egid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    gid_t old_rgid = proc->gid;
    gid_t old_egid = proc->egid;
    if (rgid != (gid_t)-1) {
        if (proc->euid == 0 || rgid == proc->gid || rgid == proc->egid) {
            proc->gid = rgid;
        } else
            return -1;
    }
    if (egid != (gid_t)-1) {
        if (proc->euid == 0 || egid == old_rgid || egid == old_egid || egid == proc->sgid) {
            proc->egid = egid;
        } else
            return -1;
    }
    if (rgid != (gid_t)-1 || (egid != (gid_t)-1 && egid != old_rgid)) {
        proc->sgid = proc->egid;
    }
    return 0;
}

static int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    if (proc->euid == 0) {
        if (ruid != (uid_t)-1)
            proc->uid = ruid;
        if (euid != (uid_t)-1)
            proc->euid = euid;
        if (suid != (uid_t)-1)
            proc->suid = suid;
        return 0;
    }
    if ((ruid != (uid_t)-1 && ruid != proc->uid && ruid != proc->euid && ruid != proc->suid) ||
        (euid != (uid_t)-1 && euid != proc->uid && euid != proc->euid && euid != proc->suid) ||
        (suid != (uid_t)-1 && suid != proc->uid && suid != proc->euid && suid != proc->suid)) {
        return -1;
    }
    if (ruid != (uid_t)-1)
        proc->uid = ruid;
    if (euid != (uid_t)-1)
        proc->euid = euid;
    if (suid != (uid_t)-1)
        proc->suid = suid;
    return 0;
}

static int64_t sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    if (ruid) {
        uid_t kruid = proc->uid;
        if (!put_user_buffer(ruid, &kruid, sizeof(uid_t)))
            return -14; /* -EFAULT */
    }
    if (euid) {
        uid_t keuid = proc->euid;
        if (!put_user_buffer(euid, &keuid, sizeof(uid_t)))
            return -14; /* -EFAULT */
    }
    if (suid) {
        uid_t ksuid = proc->suid;
        if (!put_user_buffer(suid, &ksuid, sizeof(uid_t)))
            return -14; /* -EFAULT */
    }
    return 0;
}

static int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    if (proc->euid == 0) {
        if (rgid != (gid_t)-1)
            proc->gid = rgid;
        if (egid != (gid_t)-1)
            proc->egid = egid;
        if (sgid != (gid_t)-1)
            proc->sgid = sgid;
        return 0;
    }
    if ((rgid != (gid_t)-1 && rgid != proc->gid && rgid != proc->egid && rgid != proc->sgid) ||
        (egid != (gid_t)-1 && egid != proc->gid && egid != proc->egid && egid != proc->sgid) ||
        (sgid != (gid_t)-1 && sgid != proc->gid && sgid != proc->egid && sgid != proc->sgid)) {
        return -1;
    }
    if (rgid != (gid_t)-1)
        proc->gid = rgid;
    if (egid != (gid_t)-1)
        proc->egid = egid;
    if (sgid != (gid_t)-1)
        proc->sgid = sgid;
    return 0;
}

static int64_t sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;
    if (rgid) {
        gid_t krgid = proc->gid;
        if (!put_user_buffer(rgid, &krgid, sizeof(gid_t)))
            return -14; /* -EFAULT */
    }
    if (egid) {
        gid_t kegid = proc->egid;
        if (!put_user_buffer(egid, &kegid, sizeof(gid_t)))
            return -14; /* -EFAULT */
    }
    if (sgid) {
        gid_t ksgid = proc->sgid;
        if (!put_user_buffer(sgid, &ksgid, sizeof(gid_t)))
            return -14; /* -EFAULT */
    }
    return 0;
}

static int64_t sys_sysctl_syscall(const char *name, void *oldp, size_t *oldlenp, const void *newp, size_t newlen) {
    return sysctl_byname(name, oldp, oldlenp, newp, newlen);
}

static int64_t sys_syslog_syscall(int type, char *bufp, int len) {
    if (type == 2 || type == 3 || type == 4) {
        if (!bufp || len <= 0)
            return 0;
        char *kbuf = (char *)kmalloc((size_t)len);
        if (!kbuf)
            return -12; /* -ENOMEM */
        size_t n = klog_read_ring(kbuf, (size_t)len, 0);
        if (n > 0) {
            if (!put_user_buffer(bufp, kbuf, n)) {
                kfree(kbuf);
                return -14; /* -EFAULT */
            }
        }
        kfree(kbuf);
        return (int64_t)n;
    }
    if (type == 9 || type == 10) {
        return (int64_t)klog_get_ring_size();
    }
    return 0;
}

static int64_t sys_chmod(const char *path, mode_t mode) {
    if (!path)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    return vfs_chmod(kpath, mode);
}

static int64_t sys_fchmod(int fd, mode_t mode) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -1;
    file_descriptor_t *f = proc->fds[fd];
    if (!f->node)
        return -1;

    if (proc->euid != 0 && proc->euid != f->node->uid) {
        return -1; /* EPERM */
    }

    f->node->permissions = (mode & 07777);
    return 0;
}

static int64_t sys_chown(const char *path, uid_t uid, gid_t gid) {
    if (!path)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    return vfs_chown(kpath, uid, gid);
}

static int64_t sys_fchown(int fd, uid_t uid, gid_t gid) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -1;
    file_descriptor_t *f = proc->fds[fd];
    if (!f->node)
        return -1;

    if (proc->euid != 0) {
        return -1; /* EPERM */
    }

    if (uid != (uid_t)-1)
        f->node->uid = uid;
    if (gid != (gid_t)-1)
        f->node->gid = gid;
    return 0;
}

static int64_t sys_mkdir(const char *path, mode_t mode) {
    if (!path)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    process_t *proc = sched_get_current_process();
    mode_t actual_mode = (mode ? mode : 0777) & ~(proc ? proc->umask : 0022);
    return vfs_mkdir(kpath, actual_mode);
}

static int64_t sys_access(const char *path, int mode) {
    if (!path)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    return vfs_access(kpath, mode);
}

static int64_t sys_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -22;
    char kold[256];
    char knew[256];
    if (!get_user_string(kold, oldpath, sizeof(kold)) || !get_user_string(knew, newpath, sizeof(knew)))
        return -14; /* -EFAULT */
    return vfs_rename(kold, knew);
}

static int64_t sys_rmdir(const char *path) {
    if (!path)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    return vfs_rmdir(kpath);
}

static int64_t sys_truncate(const char *path, off_t length) {
    if (!path || length < 0)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    return vfs_truncate(kpath, length);
}

static int64_t sys_ftruncate(int fd, off_t length) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node || length < 0)
        return -1;
    file_descriptor_t *f = proc->fds[fd];
    if ((f->flags & 3) == O_RDONLY)
        return -1;
    if (f->node->ops && f->node->ops->truncate)
        return f->node->ops->truncate(f->node, length);
    f->node->length = (size_t)length;
    return 0;
}

static int64_t sys_umask(mode_t mask) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return 022;
    mode_t old = proc->umask;
    proc->umask = mask & 0777;
    return old;
}

static int64_t sys_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath)
        return -22;
    char ktarget[256];
    char klink[256];
    if (!get_user_string(ktarget, target, sizeof(ktarget)) || !get_user_string(klink, linkpath, sizeof(klink)))
        return -14; /* -EFAULT */
    return vfs_symlink(ktarget, klink);
}

static int64_t sys_readlink(const char *path, char *buf, size_t bufsiz) {
    if (!path || !buf || bufsiz == 0)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    char kbuf[256];
    size_t kbufsiz = (bufsiz < sizeof(kbuf)) ? bufsiz : sizeof(kbuf);
    ssize_t ret = vfs_readlink(kpath, kbuf, kbufsiz);
    if (ret > 0) {
        if (!put_user_buffer(buf, kbuf, (size_t)ret))
            return -14; /* -EFAULT */
    }
    return ret;
}

static int64_t sys_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -22; /* -EINVAL */
    char kold[256];
    char knew[256];
    if (!get_user_string(kold, oldpath, sizeof(kold)) || !get_user_string(knew, newpath, sizeof(knew)))
        return -14; /* -EFAULT */
    return (int64_t)vfs_link(kold, knew);
}

static int64_t sys_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    (void)flags;
    char full_old[256];
    char full_new[256];
    build_at_path(olddirfd, oldpath, full_old, sizeof(full_old));
    build_at_path(newdirfd, newpath, full_new, sizeof(full_new));
    return (int64_t)vfs_link(full_old, full_new);
}

static int64_t sys_fork(void);

static int64_t sys_gettid(void) {
    thread_t *curr = sched_get_current_thread();
    return curr ? (int64_t)curr->tid : -1;
}

static int64_t sys_set_tid_address(uintptr_t tidptr) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return -1;
    curr->clear_child_tid = tidptr;
    return (int64_t)curr->tid;
}

static int64_t sys_arch_prctl(int code, uintptr_t addr) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return -1;

    if (code == ARCH_SET_FS) {
        curr->fs_base = addr;
        wrmsr(0xC0000100, addr);
        return 0;
    } else if (code == ARCH_GET_FS) {
        if (!addr)
            return -22;
        if (!put_user_buffer((void *)addr, &curr->fs_base, sizeof(uintptr_t)))
            return -14; /* -EFAULT */
        return 0;
    } else if (code == ARCH_SET_GS) {
        wrmsr(0xC0000101, addr);
        return 0;
    }
    return -1;
}

#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
#define TASK_COMM_LEN 16

static int64_t sys_prctl(int option, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    process_t *proc = sched_get_current_process();
    thread_t *curr = sched_get_current_thread();
    if (!proc || !curr)
        return -3; /* -ESRCH */

    switch (option) {
    case PR_SET_NAME: {
        char name[TASK_COMM_LEN];
        memset(name, 0, sizeof(name));
        if (copy_string_from_user(name, (uintptr_t)arg2, sizeof(name)) < 0)
            return -14; /* -EFAULT */
        memcpy(curr->name, name, sizeof(curr->name));
        /* The first thread is the one ps/top identify the process by. */
        if (proc->threads.next == &curr->proc_node) {
            memset(proc->name, 0, sizeof(proc->name));
            memcpy(proc->name, name, sizeof(name));
        }
        return 0;
    }
    case PR_GET_NAME: {
        char name[TASK_COMM_LEN];
        memset(name, 0, sizeof(name));
        strncpy(name, curr->name[0] ? curr->name : proc->name, sizeof(name) - 1);
        if (!copy_to_user((uintptr_t)arg2, name, sizeof(name)))
            return -14; /* -EFAULT */
        return 0;
    }
    case PR_SET_NO_NEW_PRIVS:
        if (arg2 != 1 || arg3 || arg4 || arg5)
            return -22; /* -EINVAL */
        proc->no_new_privs = true;
        return 0;
    case PR_GET_NO_NEW_PRIVS:
        if (arg2 || arg3 || arg4 || arg5)
            return -22; /* -EINVAL */
        return proc->no_new_privs ? 1 : 0;
    default:
        return -22; /* -EINVAL */
    }
}

static int64_t sys_sched_getaffinity(pid_t pid, size_t len, uintptr_t user_mask) {
    process_t *proc = sched_get_current_process();
    thread_t *curr = sched_get_current_thread();
    if (!proc || !curr)
        return -3; /* -ESRCH */

    /* SMP_MAX_CPUS == 64, so the kernel cpumask is exactly one word. */
    if (len < sizeof(uint64_t) || (len & (sizeof(uint64_t) - 1)))
        return -22; /* -EINVAL */
    if (pid != 0 && pid != proc->pid && pid != curr->tid && !process_get_by_pid(pid))
        return -3; /* -ESRCH */

    /* Threads are not pinned yet, so every online CPU is eligible. */
    uint32_t count = smp_get_cpu_count();
    uint64_t mask = count >= 64 ? ~0ULL : (1ULL << count) - 1;
    if (!copy_to_user(user_mask, &mask, sizeof(mask)))
        return -14; /* -EFAULT */
    return (int64_t)sizeof(mask);
}

#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_FREE 8
#define MADV_MERGEABLE 12
#define MADV_UNMERGEABLE 13
#define MADV_HUGEPAGE 14
#define MADV_NOHUGEPAGE 15
#define MADV_DONTDUMP 16
#define MADV_DODUMP 17

static int64_t sys_madvise(void *addr, size_t len, int advice) {
    if (((uintptr_t)addr & (PAGE_SIZE - 1)) || !vmm_user_range((uintptr_t)addr, len))
        return -22; /* -EINVAL */

    switch (advice) {
    case MADV_NORMAL:
    case MADV_RANDOM:
    case MADV_SEQUENTIAL:
    case MADV_WILLNEED:
    case MADV_MERGEABLE:
    case MADV_UNMERGEABLE:
    case MADV_HUGEPAGE:
    case MADV_NOHUGEPAGE:
    case MADV_DONTDUMP:
    case MADV_DODUMP:
    case MADV_FREE:
        /* Pure hints: nothing to tune, and MADV_FREE may legally keep the old contents. */
        return 0;
    default:
        /* MADV_DONTNEED must zero private pages and MADV_DONTFORK must change
         * fork(); both need per-mapping metadata the VMM does not track yet. */
        return -22; /* -EINVAL */
    }
}

static int64_t sys_futex(uintptr_t uaddr, int futex_op, int val, uintptr_t timeout_or_val2, uintptr_t uaddr2,
                         int val3) {
    UNUSED(val3);
    int cmd = futex_op & FUTEX_CMD_MASK;

    switch (cmd) {
    case FUTEX_WAIT:
    case FUTEX_WAIT_BITSET:
        return futex_wait(uaddr, val, (const struct timespec *)timeout_or_val2);

    case FUTEX_WAKE:
    case FUTEX_WAKE_BITSET:
        return futex_wake(uaddr, val);

    case FUTEX_REQUEUE:
    case FUTEX_CMP_REQUEUE:
        return futex_requeue(uaddr, val, uaddr2, (int)timeout_or_val2);

    default:
        return -38; /* -ENOSYS */
    }
}

static int64_t sys_clone(uint64_t flags, uintptr_t child_stack, uintptr_t ptid, uintptr_t ctid, uintptr_t tls) {
    if (!(flags & CLONE_THREAD)) {
        return sys_fork();
    }

    process_t *proc = sched_get_current_process();
    thread_t *parent_t = sched_get_current_thread();
    if (!proc || !parent_t)
        return -1;

    thread_t *child_t = (thread_t *)kzalloc(sizeof(thread_t));
    if (!child_t)
        return -1;

    static tid_t s_clone_tid = 100;
    child_t->tid = s_clone_tid++;
    child_t->process = proc;
    child_t->state = THREAD_READY;
    child_t->user_entry = parent_t->user_entry;
    child_t->user_stack = child_stack ? child_stack : parent_t->user_stack;

    if (flags & CLONE_SETTLS) {
        child_t->fs_base = tls;
    } else {
        child_t->fs_base = parent_t->fs_base;
    }

    if (flags & CLONE_CHILD_CLEARTID) {
        child_t->clear_child_tid = ctid;
    }

    if (flags & CLONE_CHILD_SETTID) {
        if (vmm_virt_to_phys(proc->pagemap, ctid)) {
            *(int *)ctid = child_t->tid;
        }
    }

    if (flags & CLONE_PARENT_SETTID) {
        if (vmm_virt_to_phys(proc->pagemap, ptid)) {
            *(int *)ptid = child_t->tid;
        }
    }

    memcpy(child_t->fpu_state, parent_t->fpu_state, 512);

    /* Allocate 16 KiB kernel stack */
    size_t stack_pages = 16 * 1024 / PAGE_SIZE;
    uintptr_t stack_phys = pmm_alloc_pages(stack_pages);
    if (!stack_phys) {
        kfree(child_t);
        return -1;
    }

    child_t->kernel_stack_bottom = (uintptr_t)PHYS_TO_VIRT(stack_phys);
    child_t->kernel_stack_top = child_t->kernel_stack_bottom + 16 * 1024;

    /* Copy user context frame from parent's top of kernel stack */
    uint64_t *sp = (uint64_t *)child_t->kernel_stack_top;
    uint64_t *parent_frame = (uint64_t *)(parent_t->kernel_stack_top - (9 * sizeof(uint64_t)));

    sp -= 9;
    memcpy(sp, parent_frame, 9 * sizeof(uint64_t));

    if (child_stack) {
        sp[8] = child_stack; /* User RSP */
    }

    /* Return address for arch_switch_context `ret` */
    *(--sp) = (uint64_t)arch_syscall_return;

    /* 7 callee-saved registers for arch_switch_context: popped in order rflags, r15, r14, r13, r12, rbp, rbx */
    *(--sp) = 0;     /* RBX */
    *(--sp) = 0;     /* RBP */
    *(--sp) = 0;     /* R12 */
    *(--sp) = 0;     /* R13 */
    *(--sp) = 0;     /* R14 */
    *(--sp) = 0;      /* R15 */
    *(--sp) = 0x3202; /* RFLAGS (IOPL=3) */

    child_t->rsp = (uintptr_t)sp;

    list_add_tail(&proc->threads, &child_t->proc_node);
    sched_add_thread(child_t);

    return child_t->tid;
}

static int64_t sys_fork(void) {
    process_t *parent = sched_get_current_process();
    if (!parent)
        return -1;

    process_t *child = process_create(parent->name);
    if (!child)
        return -1;

    child->ppid = parent->pid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;
    child->has_ctty = parent->has_ctty;
    child->ctty = parent->ctty;
    child->uid = parent->uid;
    child->gid = parent->gid;
    child->euid = parent->euid;
    child->egid = parent->egid;
    child->suid = parent->suid;
    child->sgid = parent->sgid;
    child->priority = parent->priority;
    child->umask = parent->umask;
    child->no_new_privs = parent->no_new_privs;
    child->ngroups = parent->ngroups;
    memcpy(child->groups, parent->groups, sizeof(child->groups));
    child->blocked_signals = parent->blocked_signals;
    memcpy(child->signal_handlers, parent->signal_handlers, sizeof(child->signal_handlers));
    memcpy(child->sigactions, parent->sigactions, sizeof(child->sigactions));
    memcpy(child->rlimits, parent->rlimits, sizeof(child->rlimits));
    child->brk_start = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->mmap_current = parent->mmap_current;
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);

    /* Clone address space */
    if (parent->pagemap) {
        pagemap_t *old_pagemap = child->pagemap;
        child->pagemap = vmm_clone_address_space(parent->pagemap);
        vmm_destroy_address_space(old_pagemap);
        if (!child->pagemap) {
            process_destroy_unstarted(child);
            return -12; /* -ENOMEM */
        }
    }

    /* Free default standard streams allocated by process_create */
    for (int i = 0; i < 3; i++) {
        if (child->fds[i]) {
            kfree(child->fds[i]);
            child->fds[i] = NULL;
        }
    }

    /* Clone open file descriptors */
    for (int i = 0; i < MAX_FD; i++) {
        if (parent->fds[i]) {
            child->fds[i] = parent->fds[i];
            parent->fds[i]->refcount++;
            child->fd_cloexec[i] = parent->fd_cloexec[i];
        }
    }

    /* Allocate and initialize child thread */
    thread_t *parent_t = sched_get_current_thread();
    if (!parent_t)
        return -1;

    thread_t *child_t = (thread_t *)kzalloc(sizeof(thread_t));
    if (!child_t)
        return -1;

    static tid_t s_fork_tid = 100;
    child_t->tid = s_fork_tid++;
    child_t->process = child;
    child_t->state = THREAD_READY;
    child_t->user_entry = parent_t->user_entry;
    child_t->user_stack = parent_t->user_stack;
    child_t->fs_base = parent_t->fs_base;
    memcpy(child_t->fpu_state, parent_t->fpu_state, 512);

    /* Allocate 16 KiB kernel stack */
    size_t stack_pages = 16 * 1024 / PAGE_SIZE;
    uintptr_t stack_phys = pmm_alloc_pages(stack_pages);
    if (!stack_phys) {
        kfree(child_t);
        return -1;
    }

    child_t->kernel_stack_bottom = (uintptr_t)PHYS_TO_VIRT(stack_phys);
    child_t->kernel_stack_top = child_t->kernel_stack_bottom + 16 * 1024;

    /* Copy user context frame from parent's top of kernel stack */
    uint64_t *sp = (uint64_t *)child_t->kernel_stack_top;
    uint64_t *parent_frame = (uint64_t *)(parent_t->kernel_stack_top - (9 * sizeof(uint64_t)));

    sp -= 9;
    memcpy(sp, parent_frame, 9 * sizeof(uint64_t));

    /* Return address for arch_switch_context `ret` */
    *(--sp) = (uint64_t)arch_syscall_return;

    /* 7 callee-saved registers for arch_switch_context: popped in order rflags, r15, r14, r13, r12, rbp, rbx */
    *(--sp) = 0;     /* RBX */
    *(--sp) = 0;     /* RBP */
    *(--sp) = 0;     /* R12 */
    *(--sp) = 0;     /* R13 */
    *(--sp) = 0;     /* R14 */
    *(--sp) = 0;      /* R15 */
    *(--sp) = 0x3202; /* RFLAGS (IOPL=3) */

    child_t->rsp = (uintptr_t)sp;

    list_add_tail(&child->threads, &child_t->proc_node);
    sched_add_thread(child_t);

    return child->pid;
}

static int64_t sys_iopl(int level) {
    if (level < 0 || level > 3)
        return -1;
    process_t *proc = sched_get_current_process();
    if (proc && proc->euid != 0)
        return -1; /* -EPERM */
    extern uint64_t g_current_kernel_stack;
    cpu_t *cpu = smp_current_cpu();
    uint64_t kstack = (cpu && cpu->kernel_stack) ? cpu->kernel_stack : g_current_kernel_stack;
    if (kstack) {
        uint64_t *rflags_ptr = (uint64_t *)(kstack - 16);
        *rflags_ptr = (*rflags_ptr & ~0x3000ULL) | ((uint64_t)(level & 3) << 12);
    }
    return 0;
}

static int64_t sys_ioperm(unsigned long from, unsigned long num, int turn_on) {
    if (from + num > 0x10000 || from + num < from)
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (proc && proc->euid != 0)
        return -1; /* -EPERM */

    extern uint64_t g_current_kernel_stack;
    cpu_t *cpu = smp_current_cpu();
    uint64_t kstack = (cpu && cpu->kernel_stack) ? cpu->kernel_stack : g_current_kernel_stack;
    if (kstack) {
        uint64_t *rflags_ptr = (uint64_t *)(kstack - 16);
        if (turn_on) {
            *rflags_ptr = (*rflags_ptr & ~0x3000ULL) | ((uint64_t)3 << 12); /* IOPL = 3 */
        } else {
            *rflags_ptr = (*rflags_ptr & ~0x3000ULL); /* IOPL = 0 */
        }
    }
    return 0;
}

static int64_t sys_getpriority(int which, id_t who) {
    int prio = 0;
    int ret = process_getpriority(which, who, &prio);
    if (ret < 0)
        return ret;
    /* POSIX/Linux convention: return 20 - nice on success so non-negative */
    return (int64_t)(20 - prio);
}

static int64_t sys_setpriority(int which, id_t who, int prio) {
    return process_setpriority(which, who, prio);
}

#define MAX_EXEC_ARGS 128
#define MAX_EXEC_ENVS 256
#define MAX_EXEC_STRLEN 4096

static int64_t sys_execve(const char *pathname, char *const argv[], char *const envp[]) {
    if (!pathname)
        return -1;

    char resolved_path[256];
    if (pathname[0] != '/') {
        process_t *curr = sched_get_current_process();
        if (curr && strcmp(curr->cwd, "/") != 0) {
            ksnprintf(resolved_path, sizeof(resolved_path), "%s/%s", curr->cwd, pathname);
        } else {
            ksnprintf(resolved_path, sizeof(resolved_path), "/%s", pathname);
        }
    } else {
        strncpy(resolved_path, pathname, sizeof(resolved_path) - 1);
        resolved_path[sizeof(resolved_path) - 1] = '\0';
    }

    vfs_node_t *file = vfs_lookup(resolved_path);
    if (!file)
        return -1;

    /* Check execute permission */
    if (vfs_check_permission(file, VFS_EXEC) != 0) {
        return -1; /* EACCES: Execute permission denied */
    }

    process_t *proc = sched_get_current_process();
    thread_t *t = sched_get_current_thread();
    if (!proc || !t)
        return -1;

    /* Copy argv strings to temporary kernel storage */
    int argc = 0;
    char **k_argv = (char **)kzalloc(MAX_EXEC_ARGS * sizeof(char *));
    if (!k_argv) return -12; /* -ENOMEM */

    if (argv) {
        for (int i = 0; i < MAX_EXEC_ARGS - 1; i++) {
            const char *u_str = NULL;
            if (!get_user_buffer(&u_str, &argv[i], sizeof(const char *)))
                break;
            if (!u_str)
                break;
            char *buf = (char *)kmalloc(MAX_EXEC_STRLEN);
            if (!buf) break;
            if (!get_user_string(buf, u_str, MAX_EXEC_STRLEN)) {
                kfree(buf);
                break;
            }
            k_argv[argc++] = buf;
        }
    }
    k_argv[argc] = NULL;

    /* Copy envp strings to temporary kernel storage */
    int envc = 0;
    char **k_envp = (char **)kzalloc(MAX_EXEC_ENVS * sizeof(char *));
    if (!k_envp) {
        for (int i = 0; i < argc; i++) kfree(k_argv[i]);
        kfree(k_argv);
        return -12;
    }

    if (envp) {
        for (int i = 0; i < MAX_EXEC_ENVS - 1; i++) {
            const char *u_str = NULL;
            if (!get_user_buffer(&u_str, &envp[i], sizeof(const char *)))
                break;
            if (!u_str)
                break;
            char *buf = (char *)kmalloc(MAX_EXEC_STRLEN);
            if (!buf) break;
            if (!get_user_string(buf, u_str, MAX_EXEC_STRLEN)) {
                kfree(buf);
                break;
            }
            k_envp[envc++] = buf;
        }
    }
    k_envp[envc] = NULL;

    /* If no environment was provided, supply standard minimal defaults */
    if (envc == 0) {
        const char *def_envs[] = {
            "PATH=/bin:/usr/bin:/usr/tbin:/usr/local/bin:/sbin:/usr/sbin",
            "USER=root",
            "HOME=/root",
            "SHELL=/bin/sh",
            "TERM=xterm-256color",
            "MAGIC=/etc/magic:/usr/share/misc/magic",
            NULL
        };
        for (int i = 0; def_envs[i] && envc < MAX_EXEC_ENVS - 1; i++) {
            char *buf = (char *)kmalloc(strlen(def_envs[i]) + 1);
            if (buf) {
                strcpy(buf, def_envs[i]);
                k_envp[envc++] = buf;
            }
        }
        k_envp[envc] = NULL;
    }

    pagemap_t *new_map = vmm_create_address_space();
    elf_exec_info_t elf_info;

    if (elf_load_binary_info(file, new_map, &elf_info) != 0) {
        vmm_destroy_address_space(new_map);
        for (int i = 0; i < argc; i++) kfree(k_argv[i]);
        kfree(k_argv);
        for (int i = 0; i < envc; i++) kfree(k_envp[i]);
        kfree(k_envp);
        return -1;
    }

    pagemap_t *old_map = proc->pagemap;
    proc->pagemap = new_map;
    proc->brk_start = elf_info.brk_start;
    proc->brk_current = proc->brk_start;
    proc->mmap_current = 0x0000600000000000ULL;
    strncpy(proc->name, resolved_path, sizeof(proc->name) - 1);
    thread_t *exec_thread = sched_get_current_thread();
    if (exec_thread)
        exec_thread->name[0] = '\0';
    vmm_switch_address_space(new_map);
    vmm_destroy_address_space(old_map);

    /* Close all FD_CLOEXEC file descriptors */
    for (int i = 0; i < MAX_FD; i++) {
        if (proc->fds[i] && proc->fd_cloexec[i]) {
            sys_close(i);
        }
    }

    /* Handle SUID and SGID execution bits */
    if (file->permissions & 04000) {
        proc->euid = file->uid;
        proc->suid = file->uid;
    }
    if (file->permissions & 02000) {
        proc->egid = file->gid;
        proc->sgid = file->gid;
    }

    /* Setup user stack with environment strings, argument strings, auxv, and pointer arrays */
    uintptr_t sp = elf_info.user_stack;
    uintptr_t envp_ptrs[MAX_EXEC_ENVS];
    uintptr_t argv_ptrs[MAX_EXEC_ARGS];

    /* 1. Copy envp strings onto user stack (highest addresses) */
    for (int i = envc - 1; i >= 0; i--) {
        size_t slen = strlen(k_envp[i]) + 1;
        sp -= slen;
        memcpy((void *)sp, k_envp[i], slen);
        envp_ptrs[i] = sp;
    }

    /* 2. Copy argv strings onto user stack */
    for (int i = argc - 1; i >= 0; i--) {
        size_t slen = strlen(k_argv[i]) + 1;
        sp -= slen;
        memcpy((void *)sp, k_argv[i], slen);
        argv_ptrs[i] = sp;
    }

    /* Clean up temporary kernel string copies */
    for (int i = 0; i < argc; i++) kfree(k_argv[i]);
    kfree(k_argv);
    for (int i = 0; i < envc; i++) kfree(k_envp[i]);
    kfree(k_envp);

    /* 3. 16 random bytes for AT_RANDOM */
    sp &= ~15ULL; /* 16-byte align */
    sp -= 16;
    uintptr_t random_ptr = sp;
    random_get_bytes((void *)random_ptr, 16);

    /* 4. Prepare Auxiliary Vector (System V AMD64 ABI) */
    struct {
        uint64_t a_type;
        uint64_t a_val;
    } auxv[] = {
        { AT_SECURE, 0 },
        { AT_HWCAP2, 0 },
        { AT_HWCAP, 0 },
        { AT_RANDOM, random_ptr },
        { AT_PAGESZ, PAGE_SIZE },
        { AT_CLKTCK, 100 },
        { AT_UID, (uint64_t)proc->uid },
        { AT_EUID, (uint64_t)proc->euid },
        { AT_GID, (uint64_t)proc->gid },
        { AT_EGID, (uint64_t)proc->egid },
        { AT_ENTRY, (uint64_t)elf_info.entry },
        { AT_PHDR, (uint64_t)elf_info.phdr_vaddr },
        { AT_PHENT, (uint64_t)elf_info.phent },
        { AT_PHNUM, (uint64_t)elf_info.phnum },
        { AT_BASE, (elf_info.interp_path[0] != '\0') ? (uint64_t)elf_info.base_vaddr : 0 },
        { AT_EXECFN, (argc > 0) ? argv_ptrs[0] : 0 },
        { AT_NULL, 0 }
    };
    size_t auxv_count = sizeof(auxv) / sizeof(auxv[0]);

    /*
     * System V AMD64 ABI layout on entry to _start:
     *   [rsp]                          = argc
     *   [rsp + 8 .. rsp + 8*argc]      = argv[0] .. argv[argc-1]
     *   [rsp + 8*(argc+1)]             = NULL
     *   [rsp + 8*(argc+2) ..]          = envp[0] .. envp[envc-1]
     *   [rsp + 8*(argc+2+envc)]        = NULL
     *   [rsp + 8*(argc+3+envc) ..]     = auxv[0] .. auxv[N]
     *
     * Total 64-bit words pushed = 1 (argc) + argc + 1 (NULL) + envc + 1 (NULL) + (auxv_count * 2).
     * Since (auxv_count * 2) is even, total_words % 2 == (argc + envc + 3) % 2.
     * To ensure (sp % 16 == 0) when sp points to argc:
     * if (total_words % 2 != 0), pad with 8 bytes before pushing auxv.
     */
    size_t total_words = 1 + (size_t)argc + 1 + (size_t)envc + 1 + (auxv_count * 2);
    if ((total_words % 2) != 0) {
        sp -= 8;
        *(uint64_t *)sp = 0;
    }

    /* Push auxv in reverse order */
    for (int i = (int)auxv_count - 1; i >= 0; i--) {
        sp -= 8;
        *(uint64_t *)sp = auxv[i].a_val;
        sp -= 8;
        *(uint64_t *)sp = auxv[i].a_type;
    }

    /* Push envp NULL terminator */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Push envp pointers in reverse order */
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8;
        *(uint64_t *)sp = envp_ptrs[i];
    }

    /* Push argv NULL terminator */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Push argv pointers in reverse order */
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        *(uint64_t *)sp = argv_ptrs[i];
    }

    /* Push argc */
    sp -= 8;
    *(uint64_t *)sp = (uint64_t)argc;

    t->user_entry = elf_info.entry;
    t->user_stack = sp;

    /* Jump directly into new executable in Ring 3 */
    arch_enter_user_mode(elf_info.entry, sp);
    return 0;
}

static int64_t sys_sysinfo(struct sysinfo *info) {
    if (!info)
        return -22;
    struct sysinfo kinfo;
    memset(&kinfo, 0, sizeof(struct sysinfo));
    uint32_t freq = pit_get_frequency();
    kinfo.uptime = (long)(pit_get_ticks() / (freq ? freq : 1000));
    kinfo.totalram = pmm_get_total_memory();
    kinfo.freeram = pmm_get_free_memory();
    kinfo.bufferram = 64 * 4096;
    kinfo.mem_unit = 1;

    proc_info_t procs[64];
    kinfo.procs = (unsigned short)process_get_list(procs, 64);
    if (!put_user_buffer(info, &kinfo, sizeof(struct sysinfo)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_statfs(const char *path, struct statfs *buf) {
    if (!path || !buf)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, path, sizeof(kpath)))
        return -14; /* -EFAULT */
    struct statfs kbuf;
    memset(&kbuf, 0, sizeof(struct statfs));

    char full_path[256];
    vfs_normalize_path(kpath, full_path, sizeof(full_path));

    if (strncmp(full_path, "/mnt", 4) == 0) {
        block_device_t *hda = block_device_get("hda");
        kbuf.f_type = 0xEF53; /* EXT2_SUPER_MAGIC */
        kbuf.f_bsize = 1024;
        kbuf.f_blocks = hda ? (hda->sector_count * 512) / 1024 : 32768;
        kbuf.f_bfree = (kbuf.f_blocks > 266) ? kbuf.f_blocks - 266 : 0;
        kbuf.f_bavail = kbuf.f_bfree;
        kbuf.f_files = 8192;
        kbuf.f_ffree = 8192 - 14;
        kbuf.f_namelen = 255;
    } else if (strncmp(full_path, "/dev", 4) == 0) {
        kbuf.f_type = 0x1373; /* DEVFS_SUPER_MAGIC */
        kbuf.f_bsize = 512;
        kbuf.f_blocks = 1024;
        kbuf.f_bfree = 1024;
        kbuf.f_bavail = 1024;
        kbuf.f_files = 64;
        kbuf.f_ffree = 58;
        kbuf.f_namelen = 128;
    } else {
        /* Default root / (RAMFS / Initramfs) */
        kbuf.f_type = 0x858458F6; /* RAMFS_MAGIC */
        kbuf.f_bsize = 4096;
        kbuf.f_blocks = pmm_get_total_memory() / 4096;
        kbuf.f_bfree = pmm_get_free_memory() / 4096;
        kbuf.f_bavail = kbuf.f_bfree;
        kbuf.f_files = 4096;
        kbuf.f_ffree = 4000;
        kbuf.f_namelen = 255;
    }

    if (!put_user_buffer(buf, &kbuf, sizeof(struct statfs)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_fstatfs(int fd, struct statfs *buf) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !buf)
        return -1;
    return sys_statfs("/", buf);
}

static int64_t sys_getprocs(proc_info_t *buf, size_t max_count) {
    if (!buf || max_count == 0)
        return -22;
    size_t count = (max_count > 128) ? 128 : max_count;
    proc_info_t *kprocs = (proc_info_t *)kmalloc(count * sizeof(proc_info_t));
    if (!kprocs)
        return -12; /* -ENOMEM */
    int n = process_get_list(kprocs, count);
    if (n > 0) {
        if (!put_user_buffer(buf, kprocs, n * sizeof(proc_info_t))) {
            kfree(kprocs);
            return -14; /* -EFAULT */
        }
    }
    kfree(kprocs);
    return (int64_t)n;
}

static int64_t sys_unlink(const char *pathname) {
    if (!pathname)
        return -22;
    char kpath[256];
    if (!get_user_string(kpath, pathname, sizeof(kpath)))
        return -14; /* -EFAULT */
    return (int64_t)vfs_unlink(kpath);
}

static int64_t sys_pread64(int fd, void *buf, size_t count, off_t offset) {
    if (count == 0)
        return 0;
    if (offset < 0)
        return -22;
    if (!user_buffer(buf, count, true))
        return -14; /* EFAULT */
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return -1;
    vfs_node_t *node = proc->fds[fd]->node;
    if (!node->ops || !node->ops->read)
        return -1;
    return node->ops->read(node, offset, count, buf);
}

static int64_t sys_pwrite64(int fd, const void *buf, size_t count, off_t offset) {
    if (count == 0)
        return 0;
    if (offset < 0)
        return -22;
    if (!user_buffer(buf, count, false))
        return -14; /* EFAULT */
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return -1;
    vfs_node_t *node = proc->fds[fd]->node;
    if (!node->ops || !node->ops->write)
        return -1;
    return node->ops->write(node, offset, count, buf);
}

struct iovec_k {
    void *iov_base;
    size_t iov_len;
};

static int64_t sys_readv(int fd, const struct iovec_k *iov, int iovcnt) {
    if (!iov || iovcnt <= 0)
        return 0;
    if (iovcnt > 128)
        return -22; /* -EINVAL */
    struct iovec_k kiov[128];
    if (!get_user_buffer(kiov, iov, iovcnt * sizeof(struct iovec_k)))
        return -14; /* -EFAULT */
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!kiov[i].iov_base || kiov[i].iov_len == 0)
            continue;
        int64_t r = sys_read(fd, kiov[i].iov_base, kiov[i].iov_len);
        if (r < 0)
            return (total > 0) ? total : r;
        total += r;
        if ((size_t)r < kiov[i].iov_len)
            break;
    }
    return total;
}

static int64_t sys_writev(int fd, const struct iovec_k *iov, int iovcnt) {
    if (!iov || iovcnt <= 0)
        return 0;
    if (iovcnt > 128)
        return -22; /* -EINVAL */
    struct iovec_k kiov[128];
    if (!get_user_buffer(kiov, iov, iovcnt * sizeof(struct iovec_k)))
        return -14; /* -EFAULT */
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!kiov[i].iov_base || kiov[i].iov_len == 0)
            continue;
        int64_t r = sys_write(fd, kiov[i].iov_base, kiov[i].iov_len);
        if (r < 0)
            return (total > 0) ? total : r;
        total += r;
        if ((size_t)r < kiov[i].iov_len)
            break;
    }
    return total;
}


static int64_t sys_mprotect(void *addr, size_t len, int prot) {
    process_t *proc = sched_get_current_process();
    if (!proc || ((uintptr_t)addr & (PAGE_SIZE - 1)) ||
        (prot & ~7) || !vmm_user_range((uintptr_t)addr, len))
        return -22;
    if (len == 0)
        return 0;
    return vmm_set_range_flags(proc->pagemap, (uintptr_t)addr, len, user_protection(prot)) ? 0 : -12;
}

struct tms_k {
    int64_t tms_utime;
    int64_t tms_stime;
    int64_t tms_cutime;
    int64_t tms_cstime;
};

static int64_t sys_times(struct tms_k *buf) {
    uint64_t ticks = pit_get_ticks();
    if (buf) {
        process_t *proc = sched_get_current_process();
        struct tms_k ktms = {0};
        if (proc) {
            /* 100 Hz timer tick = 10,000,000 ns per tick */
            uint64_t proc_ticks = proc->cpu_time_ns / 10000000ULL;
            ktms.tms_utime = (int64_t)(proc_ticks * 3 / 4);
            ktms.tms_stime = (int64_t)(proc_ticks / 4);
        }
        if (!put_user_buffer(buf, &ktms, sizeof(struct tms_k)))
            return -14; /* -EFAULT */
    }
    return (int64_t)ticks;
}

struct rusage_kernel {
    struct timeval_kernel ru_utime;
    struct timeval_kernel ru_stime;
    int64_t ru_maxrss;
    int64_t ru_ixrss;
    int64_t ru_idrss;
    int64_t ru_isrss;
    int64_t ru_minflt;
    int64_t ru_majflt;
    int64_t ru_nswap;
    int64_t ru_inblock;
    int64_t ru_oublock;
    int64_t ru_msgsnd;
    int64_t ru_msgrcv;
    int64_t ru_nsignals;
    int64_t ru_nvcsw;
    int64_t ru_nivcsw;
};

static int64_t sys_getrlimit(int resource, struct rlimit_k *rlim) {
    if (!rlim)
        return -14; /* -EFAULT */
    if (resource < 0 || resource >= RLIM_NLIMITS)
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    struct rlimit_k krlim = proc->rlimits[resource];
    if (!put_user_buffer(rlim, &krlim, sizeof(struct rlimit_k)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_setrlimit(int resource, const struct rlimit_k *rlim) {
    if (!rlim)
        return -14; /* -EFAULT */
    if (resource < 0 || resource >= RLIM_NLIMITS)
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    struct rlimit_k krlim;
    if (!get_user_buffer(&krlim, rlim, sizeof(struct rlimit_k)))
        return -14; /* -EFAULT */

    if (krlim.rlim_cur > krlim.rlim_max)
        return -22; /* -EINVAL: soft limit cannot exceed hard limit */

    /* Only root can raise hard limit */
    if (krlim.rlim_max > proc->rlimits[resource].rlim_max && proc->euid != 0)
        return -1; /* -EPERM */

    proc->rlimits[resource] = krlim;
    return 0;
}

static int64_t sys_getrusage(int who, void *usage) {
    if (!usage)
        return -14; /* -EFAULT */
    if (who != 0 /* RUSAGE_SELF */ && who != -1 /* RUSAGE_CHILDREN */)
        return -22; /* -EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    struct rusage_kernel kru;
    memset(&kru, 0, sizeof(kru));

    /* CPU time */
    uint64_t utime_us = (proc->cpu_time_ns * 3 / 4) / 1000;
    uint64_t stime_us = (proc->cpu_time_ns * 1 / 4) / 1000;
    kru.ru_utime.tv_sec = utime_us / 1000000;
    kru.ru_utime.tv_usec = utime_us % 1000000;
    kru.ru_stime.tv_sec = stime_us / 1000000;
    kru.ru_stime.tv_usec = stime_us % 1000000;

    /* Approximate Resident Set Size in KiB */
    uint64_t heap_kib = (proc->brk_current > proc->brk_start) ?
                        (proc->brk_current - proc->brk_start) / 1024 : 0;
    kru.ru_maxrss = (int64_t)(heap_kib + 8192); /* heap + stack (8 MiB) */
    kru.ru_minflt = (int64_t)proc->minflt;
    kru.ru_majflt = (int64_t)proc->majflt;
    kru.ru_nvcsw = (int64_t)proc->nvcsw;
    kru.ru_nivcsw = (int64_t)proc->nivcsw;

    if (!put_user_buffer(usage, &kru, sizeof(kru)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_alarm(unsigned int seconds) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return 0;
    uint64_t cur = pit_get_ticks();
    uint32_t freq = pit_get_frequency();
    if (freq == 0)
        freq = 1000;
    uint64_t old = 0;
    if (proc->alarm_ticks > cur) {
        old = (proc->alarm_ticks - cur) / freq;
    }
    proc->alarm_ticks = seconds ? (cur + (uint64_t)seconds * freq) : 0;
    return (int64_t)old;
}

static int64_t sys_clock_getres(int clk_id, struct timespec_kernel *res) {
    if (!res)
        return -14; /* -EFAULT */

    struct timespec_kernel kres;
    kres.tv_sec = 0;

    extern uint64_t g_tsc_freq_hz;
    if (clk_id == 0) { /* CLOCK_REALTIME */
        uint32_t freq = pit_get_frequency();
        kres.tv_nsec = freq ? (1000000000ULL / freq) : 1000000;
    } else if (clk_id == 1 || clk_id == 4) { /* CLOCK_MONOTONIC / CLOCK_MONOTONIC_RAW */
        kres.tv_nsec = g_tsc_freq_hz ? (1000000000ULL / g_tsc_freq_hz) : 1;
        if (kres.tv_nsec == 0) kres.tv_nsec = 1;
    } else if (clk_id == 2 || clk_id == 3) { /* CLOCK_PROCESS_CPUTIME_ID / CLOCK_THREAD_CPUTIME_ID */
        kres.tv_nsec = 1;
    } else {
        return -22; /* -EINVAL */
    }

    if (!put_user_buffer(res, &kres, sizeof(kres)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_clock_settime(int clk_id, const struct timespec_kernel *tp) {
    if (!tp)
        return -14; /* -EFAULT */

    process_t *proc = sched_get_current_process();
    if (!proc || proc->euid != 0)
        return -1; /* -EPERM */

    if (clk_id != 0) { /* Only CLOCK_REALTIME is settable */
        return -22; /* -EINVAL */
    }

    struct timespec_kernel ktp;
    if (!get_user_buffer(&ktp, tp, sizeof(ktp)))
        return -14; /* -EFAULT */

    return 0;
}

#define AT_FDCWD -100

static void build_at_path(int dirfd, const char *pathname, char *out, size_t out_len) {
    if (!pathname || !out || out_len == 0) {
        if (out && out_len > 0) out[0] = '\0';
        return;
    }
    char kname[256];
    if (!get_user_string(kname, pathname, sizeof(kname))) {
        out[0] = '\0';
        return;
    }
    if (kname[0] == '/') {
        strncpy(out, kname, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    process_t *proc = sched_get_current_process();
    if (dirfd == AT_FDCWD || !proc || dirfd < 0 || dirfd >= MAX_FD || !proc->fds[dirfd]) {
        vfs_resolve_path(kname, out, out_len);
        return;
    }
    vfs_node_t *dir_node = proc->fds[dirfd]->node;
    if (dir_node && dir_node->flags == VFS_TYPE_DIRECTORY) {
        ksnprintf(out, out_len, "/%s/%s", dir_node->name, kname);
        char norm[256];
        vfs_normalize_path(out, norm, sizeof(norm));
        strncpy(out, norm, out_len - 1);
        out[out_len - 1] = '\0';
    } else {
        vfs_resolve_path(kname, out, out_len);
    }
}

static int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return sys_open(full, flags, mode);
}

static int64_t sys_mkdirat(int dirfd, const char *pathname, mode_t mode) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return sys_mkdir(full, mode);
}

static int64_t sys_mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return vfs_mknod(full, mode, dev);
}

static int64_t sys_mknod(const char *pathname, mode_t mode, dev_t dev) {
    return sys_mknodat(-100 /* AT_FDCWD */, pathname, mode, dev);
}

static int64_t sys_fsync(int fd) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -9; /* -EBADF */

    file_descriptor_t *f = proc->fds[fd];
    if (!f || !f->node)
        return -9; /* -EBADF */

    if (f->node->ops && f->node->ops->fsync) {
        int r = f->node->ops->fsync(f->node);
        if (r < 0)
            return r;
    }

    bflush(NULL);
    return 0;
}

static int64_t sys_fdatasync(int fd) {
    return sys_fsync(fd);
}

static int64_t sys_flock(int fd, int operation) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -9; /* -EBADF */

    file_descriptor_t *f = proc->fds[fd];
    if (!f || !f->node)
        return -9; /* -EBADF */

    int op = operation & ~4; /* Strip LOCK_NB (4) */
    bool nonblock = (operation & 4) != 0;

    if (op != 1 && op != 2 && op != 8) {
        return -22; /* -EINVAL */
    }

    vfs_node_t *node = f->node;

    if (op == 8) { /* LOCK_UN */
        if (node->lock_type == 2 && node->lock_owner == proc->pid) {
            node->lock_type = 0;
            node->lock_owner = 0;
        } else if (node->lock_type == 1) {
            if (node->lock_count > 0)
                node->lock_count--;
            if (node->lock_count == 0)
                node->lock_type = 0;
        }
        return 0;
    }

    if (op == 2) { /* LOCK_EX */
        while (1) {
            if (node->lock_type == 0) {
                node->lock_type = 2;
                node->lock_owner = proc->pid;
                return 0;
            }
            if (node->lock_type == 2 && node->lock_owner == proc->pid) {
                return 0;
            }
            if (nonblock) {
                return -11; /* -EWOULDBLOCK / -EAGAIN */
            }
            if (proc->pending_signals & ~proc->blocked_signals) {
                return -4; /* -EINTR */
            }
            thread_sleep(2);
        }
    }

    if (op == 1) { /* LOCK_SH */
        while (1) {
            if (node->lock_type == 0 || node->lock_type == 1) {
                node->lock_type = 1;
                node->lock_count++;
                return 0;
            }
            if (node->lock_type == 2 && node->lock_owner == proc->pid) {
                node->lock_type = 1;
                node->lock_owner = 0;
                node->lock_count = 1;
                return 0;
            }
            if (nonblock) {
                return -11; /* -EWOULDBLOCK / -EAGAIN */
            }
            if (proc->pending_signals & ~proc->blocked_signals) {
                return -4; /* -EINTR */
            }
            thread_sleep(2);
        }
    }

    return -22; /* -EINVAL */
}

static int64_t sys_unlinkat(int dirfd, const char *pathname, int flags) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    if (flags & 0x200) { /* AT_REMOVEDIR */
        return sys_rmdir(full);
    }
    return sys_unlink(full);
}

static int64_t sys_newfstatat(int dirfd, const char *pathname, struct stat *buf, int flags) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    if (flags & 0x100) { /* AT_SYMLINK_NOFOLLOW */
        return sys_lstat(full, buf);
    }
    return sys_stat(full, buf);
}

static int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    (void)flags;
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return sys_access(full, mode);
}

static int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return sys_readlink(full, buf, bufsiz);
}

static int64_t sys_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags) {
    (void)flags;
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return sys_chmod(full, mode);
}

static int64_t sys_fchownat(int dirfd, const char *pathname, uid_t uid, gid_t gid, int flags) {
    (void)flags;
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (full[0] == '\0')
        return -14; /* -EFAULT */
    return sys_chown(full, uid, gid);
}

static int64_t sys_lseek(int fd, off_t offset, int whence) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD)
        return -1;
    if (!proc->fds[fd] || !proc->fds[fd]->node)
        return -1;

    vfs_node_t *node = proc->fds[fd]->node;
    off_t new_offset = 0;

    switch (whence) {
    case 0: /* SEEK_SET */
        new_offset = offset;
        break;
    case 1: /* SEEK_CUR */
        new_offset = (off_t)proc->fds[fd]->offset + offset;
        break;
    case 2: /* SEEK_END */
        new_offset = (off_t)node->length + offset;
        break;
    default:
        return -1;
    }

    if (new_offset < 0)
        return -1;
    proc->fds[fd]->offset = (size_t)new_offset;
    return new_offset;
}

static int64_t sys_ioctl(int fd, unsigned long request, void *argp) {
    if (fd < 0 || fd >= MAX_FD)
        return -9; /* -EBADF */

    process_t *proc = sched_get_current_process();
    if (proc && proc->fds[fd]) {
        if (!proc->fds[fd]->node)
            return -9; /* -EBADF */
        vfs_node_t *node = proc->fds[fd]->node;
        if (node->ops && node->ops->ioctl) {
            return node->ops->ioctl(node, request, (uintptr_t)argp);
        }
        return -25; /* -ENOTTY: Inappropriate ioctl for device */
    }

    if (fd == 0 || fd == 1 || fd == 2) {
        return tty_ioctl(request, argp);
    }

    return -9; /* -EBADF */
}

static void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    process_t *proc = sched_get_current_process();
    bool fixed = flags & 0x10; /* MAP_FIXED */
    bool anonymous = flags & 0x20;
    if (!proc || length == 0 || length > VMM_USER_END - PAGE_SIZE ||
        (prot & ~7) || ((flags & 3) != 1 && (flags & 3) != 2) ||
        offset < 0 || ((uintptr_t)offset & (PAGE_SIZE - 1)))
        return (void *)-22;
    size_t span = ALIGN_UP(length, PAGE_SIZE);
    uintptr_t vaddr = (uintptr_t)addr;
    if ((fixed && (vaddr & (PAGE_SIZE - 1))) ||
        (vaddr && !vmm_user_range(vaddr, span)) || (fixed && !vaddr))
        return (void *)-22;
    vaddr = ALIGN_DOWN(vaddr, PAGE_SIZE);

    vfs_node_t *node = NULL;
    if (!anonymous) {
        if (fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
            return (void *)-9;
        node = proc->fds[fd]->node;
        if (!node->ops || (!node->ops->read && !node->ops->mmap))
            return (void *)-19;
    }
    if (!proc->mmap_current)
        proc->mmap_current = 0x0000600000000000ULL;
    if (!vaddr)
        vaddr = proc->mmap_current;
    /* A non-fixed address is a hint: never overwrite an existing mapping. */
    if (!fixed) {
        for (;;) {
            if (!vmm_user_range(vaddr, span))
                return (void *)-12;
            bool available = true;
            for (size_t off = 0; off < span; off += PAGE_SIZE) {
                if (vmm_virt_to_phys(proc->pagemap, vaddr + off)) {
                    vaddr += off + PAGE_SIZE;
                    available = false;
                    break;
                }
            }
            if (available)
                break;
        }
    }
    if (!vmm_user_range(vaddr, span))
        return (void *)-12;

    if (node && node->ops->mmap) {
        void *out = NULL;
        int result = node->ops->mmap(node, (void *)vaddr, length, prot, flags, offset, &out);
        if (result < 0)
            return (void *)(intptr_t)result;
        if (!vmm_set_range_flags(proc->pagemap, (uintptr_t)out, span, user_protection(prot)))
            return (void *)-12;
        if (vaddr + span > proc->mmap_current)
            proc->mmap_current = vaddr + span;
        return out;
    }

    size_t mapped = 0;
    for (; mapped < span; mapped += PAGE_SIZE) {
        uintptr_t phys = pmm_alloc_page();
        if (!phys)
            goto fail;
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        if (node && (size_t)offset < node->length && mapped < node->length - (size_t)offset) {
            size_t count = MIN(PAGE_SIZE, length - mapped);
            count = MIN(count, node->length - (size_t)offset - mapped);
            ssize_t n = node->ops->read(node, offset + mapped, count, PHYS_TO_VIRT(phys));
            if (n < 0) {
                pmm_free_page(phys);
                goto fail;
            }
        }
        if (fixed)
            vmm_release_user_page(proc->pagemap, vaddr + mapped);
        if (!vmm_map_page(proc->pagemap, vaddr + mapped, phys, user_protection(prot))) {
            pmm_free_page(phys);
            goto fail;
        }
    }
    if (vaddr + span > proc->mmap_current)
        proc->mmap_current = vaddr + span;
    return (void *)vaddr;
fail:
    for (size_t off = 0; off < mapped; off += PAGE_SIZE)
        vmm_release_user_page(proc->pagemap, vaddr + off);
    return (void *)-12;
}

static int sys_munmap(void *addr, size_t length) {
    process_t *proc = sched_get_current_process();
    if (!proc || !length || ((uintptr_t)addr & (PAGE_SIZE - 1)) ||
        !vmm_user_range((uintptr_t)addr, length))
        return -22;
    size_t span = ALIGN_UP(length, PAGE_SIZE);
    for (size_t off = 0; off < span; off += PAGE_SIZE)
        vmm_release_user_page(proc->pagemap, (uintptr_t)addr + off);
    return 0;
}

short vfs_poll_node(file_descriptor_t *fdesc, short events) {
    if (!fdesc || !fdesc->node)
        return 0;

    vfs_node_t *node = fdesc->node;
    short revents = 0;

    if (node->flags == VFS_TYPE_SOCKET) {
        socket_t *sock = (socket_t *)node->device_data;
        if (sock) {
            if (sock->domain == AF_UNIX) {
                if (sock->state == SS_LISTENING) {
                    if ((events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                        sock->accept_count > 0) {
                        revents |= (events & (0x0001 | 0x0040 | 0x0080));
                    }
                    if (sock->state == SS_CLOSED) {
                        revents |= (POLLERR | 0x0010 /* POLLHUP */);
                    }
                } else {
                    if ((events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                        (sock->rx_len > 0 || sock->state == SS_CLOSED ||
                         (sock->state == SS_CONNECTED && (!sock->peer || sock->peer->state == SS_CLOSED)))) {
                        revents |= (events & (0x0001 | 0x0040 | 0x0080));
                    }
                    if (sock->state == SS_CLOSED ||
                        (sock->state == SS_CONNECTED && (!sock->peer || sock->peer->state == SS_CLOSED))) {
                        revents |= 0x0010 /* POLLHUP */;
                    }
                    if ((events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */ | 0x0200 /* POLLWRBAND */)) &&
                        sock->state == SS_CONNECTED && sock->peer && sock->peer->state != SS_CLOSED &&
                        sock->peer->rx_len < SOCK_RX_BUF_SIZE) {
                        revents |= (events & (0x0004 | 0x0100 | 0x0200));
                    }
                }
            } else if (sock->type == SOCK_DGRAM || sock->type == SOCK_RAW) {
                if ((events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                    (sock->dgram_count > 0 || sock->rx_len > 0)) {
                    revents |= (events & (0x0001 | 0x0040 | 0x0080));
                }
                if (events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */ | 0x0200 /* POLLWRBAND */)) {
                    revents |= (events & (0x0004 | 0x0100 | 0x0200));
                }
            } else if (sock->type == SOCK_STREAM) {
                if ((events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                    (sock->rx_len > 0 || sock->accept_count > 0 || sock->state == SS_CLOSED ||
                     sock->tcp_state == TCP_STATE_CLOSE_WAIT || sock->tcp_state == TCP_STATE_CLOSED)) {
                    revents |= (events & (0x0001 | 0x0040 | 0x0080));
                }
                if ((events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */ | 0x0200 /* POLLWRBAND */)) &&
                    (sock->state == SS_CONNECTED || sock->tcp_state == TCP_STATE_ESTABLISHED)) {
                    revents |= (events & (0x0004 | 0x0100 | 0x0200));
                }
                if (sock->tcp_state == TCP_STATE_CLOSED && sock->state == SS_CONNECTING) {
                    revents |= (POLLERR | POLLHUP);
                }
            }
        }
    } else if (strcmp(node->name, "event0") == 0) {
        if ((events & POLLIN) && evdev_mouse_has_events())
            revents |= POLLIN;
    } else if (strcmp(node->name, "event1") == 0) {
        if ((events & POLLIN) && evdev_kbd_has_events())
            revents |= POLLIN;
    } else if (strcmp(node->name, "mice") == 0) {
        keyboard_poll_hardware();
        xhci_poll();
        ehci_poll();
        if ((events & POLLIN) && evdev_mice_has_data())
            revents |= POLLIN;
    } else if (strcmp(node->name, "psaux") == 0) {
        keyboard_poll_hardware();
        xhci_poll();
        ehci_poll();
        if ((events & POLLIN) && (ps2_mouse_has_packet() || evdev_mice_has_data()))
            revents |= POLLIN;
    } else if (strcmp(node->name, "mouse") == 0) {
        keyboard_poll_hardware();
        xhci_poll();
        ehci_poll();
        if ((events & POLLIN) && (mouse_has_event() || evdev_mouse_has_events()))
            revents |= POLLIN;
    } else if (strncmp(node->name, "ptmx", 4) == 0 || strncmp(node->name, "pts", 3) == 0 || pty_is_slave_node(node)) {
        if (strncmp(node->name, "ptmx", 4) == 0 && pty_node_is_hungup(node)) {
            revents |= (0x0010 /* POLLHUP */ | POLLIN);
        }
        if ((events & POLLIN) && pty_node_has_pollin(node))
            revents |= POLLIN;
        if ((events & POLLOUT) && pty_node_has_pollout(node))
            revents |= POLLOUT;
    } else if (node->flags == VFS_TYPE_PIPE && node->device_data) {
        pipe_chan_t *p = (pipe_chan_t *)node->device_data;
        if (fdesc->flags & O_WRONLY) {
            if ((events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */)) &&
                p->count < PIPE_BUF_SIZE && p->readers > 0) {
                revents |= (events & (0x0004 | 0x0100));
            }
            if (p->readers <= 0) {
                revents |= (POLLERR | 0x0010 /* POLLHUP */);
            }
        } else {
            if ((events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */)) &&
                (p->count > 0 || p->writers == 0)) {
                revents |= (events & (0x0001 | 0x0040));
            }
            if (p->writers == 0) {
                revents |= 0x0010 /* POLLHUP */;
            }
        }
    } else if (strncmp(node->name, "eventfd:", 8) == 0) {
        if ((events & POLLIN) && eventfd_has_pollin(node))
            revents |= POLLIN;
        if ((events & POLLOUT) && eventfd_has_pollout(node))
            revents |= POLLOUT;
    } else if (strncmp(node->name, "epoll:", 6) == 0) {
        if ((events & POLLIN) && epoll_has_pollin(node))
            revents |= POLLIN;
    } else if (strncmp(node->name, "inotify:", 8) == 0) {
        if ((events & POLLIN) && inotify_has_pollin(node))
            revents |= POLLIN;
    } else if (strncmp(node->name, "timerfd:", 8) == 0) {
        if ((events & POLLIN) && timerfd_has_pollin(node))
            revents |= POLLIN;
    } else if (strncmp(node->name, "signalfd:", 9) == 0) {
        if ((events & POLLIN) && signalfd_has_pollin(node))
            revents |= POLLIN;
    } else if (strncmp(node->name, "tty", 3) == 0 || strcmp(node->name, "console") == 0 || strcmp(node->name, "serial") == 0) {
        if ((events & POLLIN) && tty_has_input())
            revents |= POLLIN;
        if (events & POLLOUT)
            revents |= POLLOUT;
    } else if (strcmp(node->name, "card0") == 0 || strcmp(node->name, "renderD128") == 0 || strncmp(node->name, "dri/", 4) == 0) {
        if ((events & POLLIN) && drm_has_events())
            revents |= POLLIN;
        if (events & POLLOUT)
            revents |= POLLOUT;
    } else if (node->flags == VFS_TYPE_FILE) {
        if ((events & POLLIN) && (fdesc->offset < (off_t)node->length))
            revents |= POLLIN;
        if (events & POLLOUT)
            revents |= POLLOUT;
    } else {
        if (events & POLLOUT)
            revents |= POLLOUT;
    }

    return revents;
}

static int kernel_sys_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    if (!fds || nfds == 0)
        return 0;
    process_t *proc = sched_get_current_process();
    if (!proc)
        return 0;
    int ready = 0;
    netif_poll_all();

    uint64_t start_tick = pit_get_ticks();
    uint64_t timeout_ticks = (timeout > 0) ? ((uint64_t)timeout * pit_get_frequency() + 999) / 1000 : 0;

    while (1) {
        ready = 0;
        for (unsigned int i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            int fd = fds[i].fd;
            if (fd < 0 || fd >= MAX_FD || !proc->fds[fd]) {
                fds[i].revents = POLLNVAL;
                ready++;
                continue;
            }

            fds[i].revents = vfs_poll_node(proc->fds[fd], fds[i].events);
            if (fds[i].revents)
                ready++;
        }

        if (ready > 0 || timeout == 0)
            break;
        if (timeout > 0 && (pit_get_ticks() - start_tick) >= timeout_ticks)
            break;
        netif_poll_all();
        xhci_poll();
        ehci_poll();
        keyboard_poll_hardware();
        thread_sleep(2);
    }
    return ready;
}

typedef struct {
    uint64_t fds_bits[16]; /* 1024 descriptors */
} kernel_fd_set_t;

static int64_t sys_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    if (nfds < 0 || nfds > 1024)
        return -22; /* -EINVAL */

    kernel_fd_set_t rfds, wfds, efds;
    memset(&rfds, 0, sizeof(rfds));
    memset(&wfds, 0, sizeof(wfds));
    memset(&efds, 0, sizeof(efds));

    size_t fds_bytes = ((nfds + 63) / 64) * sizeof(uint64_t);

    if (readfds && !get_user_buffer(&rfds, readfds, fds_bytes))
        return -14; /* -EFAULT */
    if (writefds && !get_user_buffer(&wfds, writefds, fds_bytes))
        return -14; /* -EFAULT */
    if (exceptfds && !get_user_buffer(&efds, exceptfds, fds_bytes))
        return -14; /* -EFAULT */

    int timeout_ms = -1;
    if (timeout) {
        struct timeval_kernel ktv;
        if (!get_user_buffer(&ktv, timeout, sizeof(ktv)))
            return -14; /* -EFAULT */
        timeout_ms = (int)(ktv.tv_sec * 1000 + ktv.tv_usec / 1000);
        if (timeout_ms < 0) timeout_ms = 0;
    }

    struct pollfd pfds[MAX_FD];
    int fd_map[MAX_FD];
    int poll_count = 0;

    for (int fd = 0; fd < nfds && poll_count < MAX_FD; fd++) {
        short events = 0;
        int word = fd / 64;
        uint64_t bit = 1ULL << (fd % 64);

        if (readfds && (rfds.fds_bits[word] & bit))
            events |= 0x0001; /* POLLIN */
        if (writefds && (wfds.fds_bits[word] & bit))
            events |= 0x0004; /* POLLOUT */
        if (exceptfds && (efds.fds_bits[word] & bit))
            events |= 0x0002; /* POLLPRI */

        if (events != 0) {
            pfds[poll_count].fd = fd;
            pfds[poll_count].events = events;
            pfds[poll_count].revents = 0;
            fd_map[poll_count] = fd;
            poll_count++;
        }
    }

    kernel_fd_set_t out_r, out_w, out_e;
    memset(&out_r, 0, sizeof(out_r));
    memset(&out_w, 0, sizeof(out_w));
    memset(&out_e, 0, sizeof(out_e));

    int ready_count = 0;
    if (poll_count > 0) {
        int pret = kernel_sys_poll(pfds, (unsigned int)poll_count, timeout_ms);
        if (pret < 0)
            return pret;

        for (int i = 0; i < poll_count; i++) {
            int fd = fd_map[i];
            int word = fd / 64;
            uint64_t bit = 1ULL << (fd % 64);

            if ((pfds[i].revents & (0x0001 | 0x0010 | 0x0008)) && readfds && (rfds.fds_bits[word] & bit)) {
                out_r.fds_bits[word] |= bit;
                ready_count++;
            }
            if ((pfds[i].revents & 0x0004) && writefds && (wfds.fds_bits[word] & bit)) {
                out_w.fds_bits[word] |= bit;
                ready_count++;
            }
            if ((pfds[i].revents & 0x0002) && exceptfds && (efds.fds_bits[word] & bit)) {
                out_e.fds_bits[word] |= bit;
                ready_count++;
            }
        }
    } else if (timeout_ms > 0) {
        thread_sleep((uint32_t)timeout_ms);
    }

    if (readfds && !put_user_buffer(readfds, &out_r, fds_bytes))
        return -14; /* -EFAULT */
    if (writefds && !put_user_buffer(writefds, &out_w, fds_bytes))
        return -14; /* -EFAULT */
    if (exceptfds && !put_user_buffer(exceptfds, &out_e, fds_bytes))
        return -14; /* -EFAULT */

    return ready_count;
}

static int64_t sys_gettimeofday(struct timeval_kernel *tv, void *tz) {
    (void)tz;
    if (tv) {
        struct timeval_kernel ktv;
        rtc_get_timeval(&ktv);
        if (!put_user_buffer(tv, &ktv, sizeof(struct timeval_kernel)))
            return -14; /* -EFAULT */
    }
    return 0;
}

static int64_t sys_clock_gettime(int clk_id, struct timespec_kernel *tp) {
    if (!tp)
        return -22; /* -EINVAL */

    struct timespec_kernel ktp;
    if (clk_id == 1 || clk_id == 4) { /* CLOCK_MONOTONIC / CLOCK_MONOTONIC_RAW */
        rtc_get_monotonic(&ktp);
    } else {
        /* CLOCK_REALTIME */
        rtc_get_timespec(&ktp);
    }

    if (!put_user_buffer(tp, &ktp, sizeof(struct timespec_kernel)))
        return -14; /* -EFAULT */
    return 0;
}

static int64_t sys_time(int64_t *tloc) {
    int64_t now = (int64_t)rtc_get_current_epoch();
    if (tloc) {
        if (!put_user_buffer(tloc, &now, sizeof(int64_t)))
            return -14; /* -EFAULT */
    }
    return now;
}

static int64_t sys_nanosleep(const struct timespec_kernel *req, struct timespec_kernel *rem) {
    if (!req)
        return -22; /* -EINVAL */

    struct timespec_kernel kreq;
    if (!get_user_buffer(&kreq, req, sizeof(struct timespec_kernel)))
        return -14; /* -EFAULT */

    uint64_t total_ms = (uint64_t)kreq.tv_sec * 1000 + (uint64_t)(kreq.tv_nsec / 1000000);
    if (total_ms == 0 && kreq.tv_nsec > 0) {
        total_ms = 1;
    }
    thread_sleep((uint32_t)total_ms);
    if (rem) {
        struct timespec_kernel krem = {0, 0};
        put_user_buffer(rem, &krem, sizeof(struct timespec_kernel));
    }
    return 0;
}

static int64_t sys_clock_nanosleep(int clock_id, int flags, const struct timespec_kernel *request, struct timespec_kernel *remain) {
    if (!request)
        return -14; /* -EFAULT */

    struct timespec_kernel kreq;
    if (!get_user_buffer(&kreq, request, sizeof(struct timespec_kernel)))
        return -14; /* -EFAULT */

    if (kreq.tv_nsec < 0 || kreq.tv_nsec >= 1000000000LL || kreq.tv_sec < 0)
        return -22; /* -EINVAL */

    if (clock_id != 0 && clock_id != 1 && clock_id != 7)
        return -22; /* -EINVAL */

    uint64_t sleep_ms = 0;
    if (flags & 1 /* TIMER_ABSTIME */) {
        uint64_t target_ns = (uint64_t)kreq.tv_sec * 1000000000ULL + (uint64_t)kreq.tv_nsec;
        uint64_t now_ns = 0;
        if (clock_id == 0) {
            struct timespec_kernel ts_real;
            rtc_get_timespec(&ts_real);
            now_ns = (uint64_t)ts_real.tv_sec * 1000000000ULL + (uint64_t)ts_real.tv_nsec;
        } else {
            now_ns = rtc_get_monotonic_ns();
        }

        if (target_ns <= now_ns) {
            return 0;
        }
        sleep_ms = (target_ns - now_ns) / 1000000ULL;
        if (sleep_ms == 0) sleep_ms = 1;
    } else {
        sleep_ms = (uint64_t)kreq.tv_sec * 1000ULL + (uint64_t)(kreq.tv_nsec / 1000000ULL);
        if (sleep_ms == 0 && kreq.tv_nsec > 0)
            sleep_ms = 1;
    }

    thread_sleep((uint32_t)sleep_ms);

    if (remain && !(flags & 1)) {
        struct timespec_kernel krem = {0, 0};
        put_user_buffer(remain, &krem, sizeof(struct timespec_kernel));
    }
    return 0;
}

static int64_t sys_shmget(key_t key, size_t size, int shmflg) {
    int shmid = 0;
    int ret = shm_get(key, size, shmflg, &shmid);
    if (ret < 0) return ret;
    return shmid;
}

static uint64_t sys_shmat_handler(int shmid, const void *shmaddr, int shmflg) {
    process_t *proc = sched_get_current_process();
    return (uint64_t)shm_at(shmid, shmaddr, shmflg, proc);
}

static int64_t sys_shmdt_handler(const void *shmaddr) {
    process_t *proc = sched_get_current_process();
    return shm_dt(shmaddr, proc);
}

static int64_t sys_shmctl_handler(int shmid, int cmd, struct shmid_ds *buf) {
    process_t *proc = sched_get_current_process();
    return shm_ctl(shmid, cmd, buf, proc);
}

static int64_t sys_memfd_create_handler(const char *name, unsigned int flags) {
    (void)name;
    char path[64];
    static int memfd_id = 1;
    process_t *proc = sched_get_current_process();
    ksnprintf(path, sizeof(path), "/tmp/.memfd_%d_%d", proc ? proc->pid : 0, memfd_id++);
    int fd = sys_open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0 && (flags & 0x0001 /* MFD_CLOEXEC */)) {
        if (proc && fd < MAX_FD) {
            proc->fd_cloexec[fd] = true;
        }
    }
    return fd;
}

static inline void check_fatal_signals(uint64_t sys_no) {
    if (sys_no == SYS_exit || sys_no == SYS_exit_group)
        return;
    process_t *curr_proc = sched_get_current_process();
    if (!curr_proc)
        return;
    if (curr_proc->status == PROCESS_ZOMBIE) {
        process_exit(curr_proc->exit_code ? curr_proc->exit_code : 128);
    }
    uint32_t fatal_mask = (1U << SIGHUP) | (1U << SIGINT) | (1U << SIGQUIT) |
                          (1U << SIGKILL) | (1U << SIGTERM) | (1U << SIGSEGV) |
                          (1U << SIGILL);
    uint32_t pending_fatal = curr_proc->pending_signals & fatal_mask & ~curr_proc->blocked_signals;
    if (pending_fatal) {
        for (int s = 1; s < 32; s++) {
            if (pending_fatal & (1U << s)) {
                if (curr_proc->signal_handlers[s] == SIG_DFL || s == SIGKILL) {
                    process_exit(128 + s);
                }
            }
        }
    }
}

static uint64_t syscall_dispatch_inner(uint64_t sys_no, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                                       uint64_t a6) {
    (void)a6;

    switch (sys_no) {
    case SYS_read:
        return sys_read((int)a1, (void *)a2, (size_t)a3);
    case SYS_write:
        return sys_write((int)a1, (const void *)a2, (size_t)a3);
    case SYS_open:
        return sys_open((const char *)a1, (int)a2, (mode_t)a3);
    case SYS_close:
        return sys_close((int)a1);
    case SYS_stat:
        return sys_stat((const char *)a1, (struct stat *)a2);
    case SYS_fstat:
        return sys_fstat((int)a1, (struct stat *)a2);
    case SYS_lstat:
        return sys_lstat((const char *)a1, (struct stat *)a2);
    case SYS_poll:
        return kernel_sys_poll((struct pollfd *)a1, (unsigned int)a2, (int)a3);
    case SYS_lseek:
        return sys_lseek((int)a1, (off_t)a2, (int)a3);
    case SYS_mmap:
        return (uint64_t)sys_mmap((void *)a1, (size_t)a2, (int)a3, (int)a4, (int)a5, (off_t)a6);
    case SYS_mprotect:
        return sys_mprotect((void *)a1, (size_t)a2, (int)a3);
    case SYS_munmap:
        return sys_munmap((void *)a1, (size_t)a2);
    case SYS_brk:
        return sys_brk((uintptr_t)a1);
    case SYS_ioctl:
        return sys_ioctl((int)a1, (unsigned long)a2, (void *)a3);
    case SYS_pread64:
        return sys_pread64((int)a1, (void *)a2, (size_t)a3, (off_t)a4);
    case SYS_pwrite64:
        return sys_pwrite64((int)a1, (const void *)a2, (size_t)a3, (off_t)a4);
    case SYS_readv:
        return sys_readv((int)a1, (const struct iovec_k *)a2, (int)a3);
    case SYS_writev:
        return sys_writev((int)a1, (const struct iovec_k *)a2, (int)a3);
    case SYS_access:
        return sys_access((const char *)a1, (int)a2);
    case SYS_pipe:
        return sys_pipe((int *)a1);
    case SYS_select:
        return sys_select((int)a1, (void *)a2, (void *)a3, (void *)a4, (void *)a5);
    case SYS_shmget:
        return sys_shmget((key_t)a1, (size_t)a2, (int)a3);
    case SYS_shmat:
        return sys_shmat_handler((int)a1, (const void *)a2, (int)a3);
    case SYS_shmctl:
        return sys_shmctl_handler((int)a1, (int)a2, (struct shmid_ds *)a3);
    case SYS_shmdt:
        return sys_shmdt_handler((const void *)a1);
    case SYS_memfd_create:
        return sys_memfd_create_handler((const char *)a1, (unsigned int)a2);
    case SYS_dup:
        return sys_dup((int)a1);
    case SYS_dup2:
        return sys_dup2((int)a1, (int)a2);
    case SYS_alarm:
        return sys_alarm((unsigned int)a1);
    case SYS_fcntl:
        return sys_fcntl((int)a1, (int)a2, (uint64_t)a3);
    case SYS_flock:
        return sys_flock((int)a1, (int)a2);
    case SYS_fsync:
        return sys_fsync((int)a1);
    case SYS_fdatasync:
        return sys_fdatasync((int)a1);
    case SYS_truncate:
        return sys_truncate((const char *)a1, (off_t)a2);
    case SYS_ftruncate:
        return sys_ftruncate((int)a1, (off_t)a2);
    case SYS_rename:
        return sys_rename((const char *)a1, (const char *)a2);
    case SYS_mkdir:
        return sys_mkdir((const char *)a1, (mode_t)a2);
    case SYS_rmdir:
        return sys_rmdir((const char *)a1);
    case SYS_creat:
        return sys_open((const char *)a1, O_CREAT | O_WRONLY | O_TRUNC, (mode_t)a2);
    case SYS_link:
        return sys_link((const char *)a1, (const char *)a2);
    case SYS_unlink:
        return sys_unlink((const char *)a1);
    case SYS_symlink:
        return sys_symlink((const char *)a1, (const char *)a2);
    case SYS_readlink:
        return sys_readlink((const char *)a1, (char *)a2, (size_t)a3);
    case SYS_chmod:
        return sys_chmod((const char *)a1, (mode_t)a2);
    case SYS_fchmod:
        return sys_fchmod((int)a1, (mode_t)a2);
    case SYS_chown:
        return sys_chown((const char *)a1, (uid_t)a2, (gid_t)a3);
    case SYS_fchown:
        return sys_fchown((int)a1, (uid_t)a2, (gid_t)a3);
    case SYS_umask:
        return sys_umask((mode_t)a1);
    case SYS_getrlimit:
        return sys_getrlimit((int)a1, (struct rlimit_k *)a2);
    case SYS_getrusage:
        return sys_getrusage((int)a1, (void *)a2);
    case SYS_times:
        return sys_times((struct tms_k *)a1);
    case SYS_setrlimit:
        return sys_setrlimit((int)a1, (const struct rlimit_k *)a2);
    case SYS_getpid:
        return sched_get_current_process() ? sched_get_current_process()->pid : 0;
    case SYS_getppid:
        return sched_get_current_process() ? sched_get_current_process()->ppid : 0;
    case SYS_getuid:
        return sched_get_current_process() ? sched_get_current_process()->uid : 0;
    case SYS_getgid:
        return sched_get_current_process() ? sched_get_current_process()->gid : 0;
    case SYS_geteuid:
        return sched_get_current_process() ? sched_get_current_process()->euid : 0;
    case SYS_getegid:
        return sched_get_current_process() ? sched_get_current_process()->egid : 0;
    case SYS_setuid:
        return sys_setuid((uid_t)a1);
    case SYS_setgid:
        return sys_setgid((gid_t)a1);
    case SYS_seteuid:
        return sys_seteuid((uid_t)a1);
    case SYS_setegid:
        return sys_setegid((gid_t)a1);
    case SYS_setreuid:
        return sys_setreuid((uid_t)a1, (uid_t)a2);
    case SYS_setregid:
        return sys_setregid((gid_t)a1, (gid_t)a2);
    case SYS_setresuid:
        return sys_setresuid((uid_t)a1, (uid_t)a2, (uid_t)a3);
    case SYS_getresuid:
        return sys_getresuid((uid_t *)a1, (uid_t *)a2, (uid_t *)a3);
    case SYS_setresgid:
        return sys_setresgid((gid_t)a1, (gid_t)a2, (gid_t)a3);
    case SYS_getresgid:
        return sys_getresgid((gid_t *)a1, (gid_t *)a2, (gid_t *)a3);
    case SYS_getgroups:
        return process_getgroups((size_t)a1, (gid_t *)a2);
    case SYS_setgroups:
        return process_setgroups((size_t)a1, (const gid_t *)a2);
    case SYS_setpgid:
        return process_setpgid((pid_t)a1, (pid_t)a2);
    case SYS_getpgid:
        return process_getpgid((pid_t)a1);
    case SYS_getpgrp:
        return process_getpgid(0);
    case SYS_setsid:
        return process_setsid();
    case SYS_getsid:
        return process_getsid((pid_t)a1);
    case SYS_pause:
        thread_sleep(100000000);
        return (uint64_t)-1;
    case SYS_rt_sigaction:
        return process_sigaction((int)a1, (const struct sigaction *)a2, (struct sigaction *)a3);
    case SYS_rt_sigprocmask:
        return process_sigprocmask((int)a1, (const sigset_t *)a2, (sigset_t *)a3);
    case SYS_rt_sigpending:
        return process_sigpending((sigset_t *)a1);
    case SYS_mknod:
        return sys_mknod((const char *)a1, (mode_t)a2, (dev_t)a3);
    case SYS_syslog:
        return sys_syslog_syscall((int)a1, (char *)a2, (int)a3);
    case SYS_sysctl:
        return sys_sysctl_syscall((const char *)a1, (void *)a2, (size_t *)a3, (const void *)a4, (size_t)a5);
    case SYS_socket:
        return sys_socket((int)a1, (int)a2, (int)a3);
    case SYS_connect:
        return sys_connect((int)a1, (const struct sockaddr *)a2, (uint32_t)a3);
    case SYS_accept:
        return sys_accept((int)a1, (struct sockaddr *)a2, (uint32_t *)a3);
    case SYS_sendto:
        return sys_sendto((int)a1, (const void *)a2, (size_t)a3, (int)a4, (const struct sockaddr *)a5, (uint32_t)a6);
    case SYS_recvfrom:
        return sys_recvfrom((int)a1, (void *)a2, (size_t)a3, (int)a4, (struct sockaddr *)a5, (uint32_t *)a6);
    case SYS_sendmsg:
        return sys_sendmsg((int)a1, (const struct msghdr *)a2, (int)a3);
    case SYS_recvmsg:
        return sys_recvmsg((int)a1, (struct msghdr *)a2, (int)a3);
    case SYS_shutdown:
        return sys_shutdown((int)a1, (int)a2);
    case SYS_bind:
        return sys_bind((int)a1, (const struct sockaddr *)a2, (uint32_t)a3);
    case SYS_listen:
        return sys_listen((int)a1, (int)a2);
    case SYS_getsockname:
        return sys_getsockname((int)a1, (struct sockaddr *)a2, (uint32_t *)a3);
    case SYS_getpeername:
        return sys_getpeername((int)a1, (struct sockaddr *)a2, (uint32_t *)a3);
    case SYS_socketpair:
        return sys_socketpair((int)a1, (int)a2, (int)a3, (int *)a4);
    case SYS_setsockopt:
        return sys_setsockopt((int)a1, (int)a2, (int)a3, (const void *)a4, (uint32_t)a5);
    case SYS_getsockopt:
        return sys_getsockopt((int)a1, (int)a2, (int)a3, (void *)a4, (uint32_t *)a5);
    case SYS_nanosleep:
        return sys_nanosleep((const struct timespec_kernel *)a1, (struct timespec_kernel *)a2);
    case SYS_gettimeofday:
        return sys_gettimeofday((struct timeval_kernel *)a1, (void *)a2);
    case SYS_clock_gettime:
        return sys_clock_gettime((int)a1, (struct timespec_kernel *)a2);
    case SYS_clock_settime:
        return sys_clock_settime((int)a1, (const struct timespec_kernel *)a2);
    case SYS_clock_getres:
        return sys_clock_getres((int)a1, (struct timespec_kernel *)a2);
    case SYS_time:
        return sys_time((int64_t *)a1);
    case SYS_sysinfo:
        return sys_sysinfo((struct sysinfo *)a1);
    case SYS_statfs:
        return sys_statfs((const char *)a1, (struct statfs *)a2);
    case SYS_fstatfs:
        return sys_fstatfs((int)a1, (struct statfs *)a2);
    case SYS_getpriority:
        return sys_getpriority((int)a1, (id_t)a2);
    case SYS_setpriority:
        return sys_setpriority((int)a1, (id_t)a2, (int)a3);
    case SYS_getprocs:
        return sys_getprocs((proc_info_t *)a1, (size_t)a2);
    case SYS_sleep:
        thread_sleep((uint32_t)a1 * 1000);
        return 0;
    case SYS_iopl:
        return sys_iopl((int)a1);
    case SYS_ioperm:
        return sys_ioperm((unsigned long)a1, (unsigned long)a2, (int)a3);
    case SYS_fork:
        return sys_fork();
    case SYS_clone:
        return sys_clone(a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    case SYS_execve:
        return sys_execve((const char *)a1, (char *const *)a2, (char *const *)a3);
    case SYS_exit:
        thread_exit((int)a1);
        return 0;
    case SYS_exit_group:
        process_exit((int)a1);
        return 0;
    case SYS_wait4:
        return process_waitpid((pid_t)a1, (int *)a2, (int)a3);
    case SYS_uname:
        return sys_uname((struct utsname *)a1);
    case SYS_getdents:
        return sys_getdents((int)a1, (void *)a2, (size_t)a3);
    case SYS_getcwd:
        return sys_getcwd((char *)a1, (size_t)a2);
    case SYS_chdir:
        return sys_chdir((const char *)a1);
    case SYS_kill:
        return process_kill((pid_t)a1, (int)a2);
    case SYS_tkill:
        return process_kill((pid_t)a1, (int)a2);
    case SYS_gettid:
        return sys_gettid();
    case SYS_set_tid_address:
        return sys_set_tid_address((uintptr_t)a1);
    case SYS_arch_prctl:
        return sys_arch_prctl((int)a1, (uintptr_t)a2);
    case SYS_prctl:
        return sys_prctl((int)a1, a2, a3, a4, a5);
    case SYS_sched_getaffinity:
        return sys_sched_getaffinity((pid_t)a1, (size_t)a2, (uintptr_t)a3);
    case SYS_madvise:
        return sys_madvise((void *)a1, (size_t)a2, (int)a3);
    case SYS_init_module: {
        process_t *proc = sched_get_current_process();
        if (!proc || proc->euid != 0)
            return (uint64_t)-1;
        return (uint64_t)module_load((const void *)a1, (size_t)a2, (const char *)a3, NULL);
    }
    case SYS_delete_module: {
        process_t *proc = sched_get_current_process();
        if (!proc || proc->euid != 0)
            return (uint64_t)-1;
        return (uint64_t)module_unload((const char *)a1, (unsigned int)a2);
    }
    case SYS_futex:
        return sys_futex((uintptr_t)a1, (int)a2, (int)a3, (uintptr_t)a4, (uintptr_t)a5, 0);
    case SYS_getrandom:
        return random_get_bytes((void *)a1, (size_t)a2);
    case SYS_openat:
        return sys_openat((int)a1, (const char *)a2, (int)a3, (mode_t)a4);
    case SYS_mkdirat:
        return sys_mkdirat((int)a1, (const char *)a2, (mode_t)a3);
    case SYS_mknodat:
        return sys_mknodat((int)a1, (const char *)a2, (mode_t)a3, (dev_t)a4);
    case SYS_unlinkat:
        return sys_unlinkat((int)a1, (const char *)a2, (int)a3);
    case SYS_linkat:
        return sys_linkat((int)a1, (const char *)a2, (int)a3, (const char *)a4, (int)a5);
    case SYS_newfstatat:
        return sys_newfstatat((int)a1, (const char *)a2, (struct stat *)a3, (int)a4);
    case SYS_faccessat:
        return sys_faccessat((int)a1, (const char *)a2, (int)a3, (int)a4);
    case SYS_readlinkat:
        return sys_readlinkat((int)a1, (const char *)a2, (char *)a3, (size_t)a4);
    case SYS_fchmodat:
        return sys_fchmodat((int)a1, (const char *)a2, (mode_t)a3, (int)a4);
    case SYS_fchownat:
        return sys_fchownat((int)a1, (const char *)a2, (uid_t)a3, (gid_t)a4, (int)a5);
    case SYS_kqueue:
        return sys_kqueue();
    case SYS_kevent:
        return sys_kevent((int)a1, (const struct kevent *)a2, (int)a3, (struct kevent *)a4, (int)a5,
                          (const struct timespec_kernel *)a6);
    case SYS_sync:
        bflush(NULL);
        return 0;
    case SYS_reboot:
        return sys_reboot((int)a1, (int)a2, (int)a3, (void *)a4);
    case SYS_yield:
        sched_yield();
        return 0;
    case SYS_pipe2:
        return sys_pipe2((int *)a1, (int)a2);
    case SYS_dup3:
        return sys_dup3((int)a1, (int)a2, (int)a3);
    case SYS_epoll_create1:
        return sys_epoll_create1((int)a1);
    case SYS_epoll_ctl:
        return sys_epoll_ctl((int)a1, (int)a2, (int)a3, (struct epoll_event *)a4);
    case SYS_epoll_wait:
        return sys_epoll_wait((int)a1, (struct epoll_event *)a2, (int)a3, (int)a4);
    case SYS_epoll_pwait:
        return sys_epoll_pwait((int)a1, (struct epoll_event *)a2, (int)a3, (int)a4, (const void *)a5, (size_t)a6);
    case SYS_eventfd:
        return sys_eventfd2((unsigned int)a1, 0);
    case SYS_eventfd2:
        return sys_eventfd2((unsigned int)a1, (int)a2);
    case SYS_inotify_init:
        return sys_inotify_init();
    case SYS_inotify_init1:
        return sys_inotify_init1((int)a1);
    case SYS_inotify_add_watch:
        return sys_inotify_add_watch((int)a1, (const char *)a2, (uint32_t)a3);
    case SYS_inotify_rm_watch:
        return sys_inotify_rm_watch((int)a1, (int)a2);
    case SYS_fchdir:
        return sys_fchdir((int)a1);
    case SYS_clock_nanosleep:
        return sys_clock_nanosleep((int)a1, (int)a2, (const struct timespec_kernel *)a3, (struct timespec_kernel *)a4);
    case SYS_signalfd:
        return sys_signalfd((int)a1, (const sigset_t *)a2, (size_t)a3);
    case SYS_signalfd4:
        return sys_signalfd4((int)a1, (const sigset_t *)a2, (size_t)a3, (int)a4);
    case SYS_timerfd_create:
        return sys_timerfd_create((int)a1, (int)a2);
    case SYS_timerfd_settime:
        return sys_timerfd_settime((int)a1, (int)a2, (const struct itimerspec_kernel *)a3, (struct itimerspec_kernel *)a4);
    case SYS_timerfd_gettime:
        return sys_timerfd_gettime((int)a1, (struct itimerspec_kernel *)a2);
    default:
        klog_warn("Syscall: Unknown syscall #%lu called!", sys_no);
        return (uint64_t)-38; /* -ENOSYS, so libc fallbacks can detect it */
    }
}

uint64_t syscall_dispatcher(uint64_t sys_no, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t a6) {
    check_fatal_signals(sys_no);
    uint64_t ret = syscall_dispatch_inner(sys_no, a1, a2, a3, a4, a5, a6);
    check_fatal_signals(sys_no);
    return ret;
}

void syscall_init(void) {
    syscall_arch_init();
    klog_info("POSIX Syscall Dispatcher registered");
}
