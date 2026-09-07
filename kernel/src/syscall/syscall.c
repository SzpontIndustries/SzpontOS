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
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <drivers/block.h>
#include <drivers/rtc.h>
#include <drivers/random.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/pit.h>
#include <arch/x86_64/io.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/sysctl.h>
#include <kernel/kqueue.h>
#include <drivers/power.h>
#include <drivers/tty.h>
#include <drivers/evdev.h>
#include <drivers/pty.h>
#include <drivers/drm.h>

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

static int64_t sys_read(int fd, void *buf, size_t count) {
    if (!buf || count == 0)
        return 0;

    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD)
        return -1;

    /* Validate user write buffer if in userspace */
    if ((uintptr_t)buf <= USER_ADDR_MAX && !verify_user_write((uintptr_t)buf, count))
        return -14; /* -EFAULT */

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
                    process_t *fg = process_get_foreground();
                    if (fg) {
                        process_send_signal(fg, SIGINT);
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
    if (!buf || count == 0)
        return 0;

    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD)
        return -1;

    /* Validate user read buffer if in userspace */
    if ((uintptr_t)buf <= USER_ADDR_MAX && !verify_user_read((uintptr_t)buf, count))
        return -14; /* -EFAULT */

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
                 strncmp(open_node->name, "pts", 3) == 0)) {
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

static int64_t sys_pipe(int *pipefd) {
    if (!pipefd)
        return -1;
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
        return -1;

    pipe_chan_t *p = (pipe_chan_t *)kzalloc(sizeof(pipe_chan_t));
    p->readers = 1;
    p->writers = 1;

    vfs_node_t *rnode = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(rnode->name, "pipe_read");
    rnode->flags = VFS_TYPE_PIPE;
    rnode->device_data = p;
    rnode->ops = &g_pipe_read_ops;

    vfs_node_t *wnode = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(wnode->name, "pipe_write");
    wnode->flags = VFS_TYPE_PIPE;
    wnode->device_data = p;
    wnode->ops = &g_pipe_write_ops;

    file_descriptor_t *f0 = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    f0->node = rnode;
    f0->flags = O_RDONLY;
    f0->refcount = 1;

    file_descriptor_t *f1 = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    f1->node = wnode;
    f1->flags = O_WRONLY;
    f1->refcount = 1;

    proc->fds[fd0] = f0;
    proc->fds[fd1] = f1;

    int kfds[2] = {fd0, fd1};
    if (!put_user_buffer(pipefd, kfds, sizeof(kfds))) {
        proc->fds[fd0] = NULL;
        proc->fds[fd1] = NULL;
        kfree(f0);
        kfree(f1);
        kfree(rnode);
        kfree(wnode);
        kfree(p);
        return -14; /* -EFAULT */
    }
    return 0;
}

static int64_t sys_close(int fd) {
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd])
        return -1;

    file_descriptor_t *f = proc->fds[fd];
    proc->fds[fd] = NULL;
    proc->fd_cloexec[fd] = false;
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

    memcpy(dirp, &out, copy_size);
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
    if (ruid)
        *ruid = proc->uid;
    if (euid)
        *euid = proc->euid;
    if (suid)
        *suid = proc->suid;
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
    if (rgid)
        *rgid = proc->gid;
    if (egid)
        *egid = proc->egid;
    if (sgid)
        *sgid = proc->sgid;
    return 0;
}

static int64_t sys_sysctl_syscall(const char *name, void *oldp, size_t *oldlenp, const void *newp, size_t newlen) {
    return sysctl_byname(name, oldp, oldlenp, newp, newlen);
}

static int64_t sys_syslog_syscall(int type, char *bufp, int len) {
    if (type == 2 || type == 3 || type == 4) {
        if (!bufp || len <= 0)
            return 0;
        return (int64_t)klog_read_ring(bufp, (size_t)len, 0);
    }
    if (type == 9 || type == 10) {
        return (int64_t)klog_get_ring_size();
    }
    return 0;
}

static int64_t sys_chmod(const char *path, mode_t mode) {
    if (!path)
        return -1;
    return vfs_chmod(path, mode);
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
        return -1;
    return vfs_chown(path, uid, gid);
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
        return -1;
    process_t *proc = sched_get_current_process();
    mode_t actual_mode = (mode ? mode : 0777) & ~(proc ? proc->umask : 0022);
    return vfs_mkdir(path, actual_mode);
}

static int64_t sys_access(const char *path, int mode) {
    if (!path)
        return -1;
    return vfs_access(path, mode);
}

static int64_t sys_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -1;
    return vfs_rename(oldpath, newpath);
}

static int64_t sys_rmdir(const char *path) {
    if (!path)
        return -1;
    return vfs_rmdir(path);
}

static int64_t sys_truncate(const char *path, off_t length) {
    if (!path || length < 0)
        return -1;
    return vfs_truncate(path, length);
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
        return -1;
    return vfs_symlink(target, linkpath);
}

static int64_t sys_readlink(const char *path, char *buf, size_t bufsiz) {
    if (!path || !buf || bufsiz == 0)
        return -1;
    return vfs_readlink(path, buf, bufsiz);
}

static int64_t sys_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -22; /* -EINVAL */
    return (int64_t)vfs_link(oldpath, newpath);
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
            return -1;
        *(uintptr_t *)addr = curr->fs_base;
        return 0;
    } else if (code == ARCH_SET_GS) {
        wrmsr(0xC0000101, addr);
        return 0;
    }
    return -1;
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
    child->umask = parent->umask;
    child->ngroups = parent->ngroups;
    memcpy(child->groups, parent->groups, sizeof(child->groups));
    child->blocked_signals = parent->blocked_signals;
    memcpy(child->signal_handlers, parent->signal_handlers, sizeof(child->signal_handlers));
    memcpy(child->sigactions, parent->sigactions, sizeof(child->sigactions));
    child->brk_start = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->mmap_current = parent->mmap_current;
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);

    /* Clone address space */
    if (parent->pagemap) {
        pagemap_t *old_pagemap = child->pagemap;
        child->pagemap = vmm_clone_address_space(parent->pagemap);
        vmm_destroy_address_space(old_pagemap);
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
    extern uint64_t g_current_kernel_stack;
    if (g_current_kernel_stack) {
        uint64_t *rflags_ptr = (uint64_t *)(g_current_kernel_stack - 16);
        *rflags_ptr = (*rflags_ptr & ~0x3000ULL) | ((uint64_t)(level & 3) << 12);
    }
    return 0;
}

static int64_t sys_execve(const char *pathname, char *const argv[], char *const envp[]) {
    UNUSED(envp);
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
    char k_argv[16][128];
    if (argv) {
        while (argv[argc] && argc < 16) {
            strncpy(k_argv[argc], argv[argc], sizeof(k_argv[argc]) - 1);
            k_argv[argc][sizeof(k_argv[argc]) - 1] = '\0';
            argc++;
        }
    }

    pagemap_t *new_map = vmm_create_address_space();
    uintptr_t entry = 0;
    uintptr_t user_stack = 0;
    uintptr_t brk_start = 0x0000000000800000ULL;

    if (elf_load_binary(file, new_map, &entry, &user_stack, &brk_start) != 0) {
        vmm_destroy_address_space(new_map);
        return -1;
    }

    pagemap_t *old_map = proc->pagemap;
    proc->pagemap = new_map;
    proc->brk_start = brk_start;
    proc->brk_current = proc->brk_start;
    proc->mmap_current = 0x0000600000000000ULL;
    strncpy(proc->name, resolved_path, sizeof(proc->name) - 1);
    vmm_switch_address_space(new_map);
    vmm_destroy_address_space(old_map);

    /* Close all FD_CLOEXEC file descriptors */
    for (int i = 0; i < MAX_FD; i++) {
        if (proc->fds[i] && proc->fd_cloexec[i]) {
            sys_close(i);
        }
    }

    /* Setup user stack with argc and argv pointers */
    uintptr_t sp = user_stack;
    uintptr_t argv_ptrs[16];

    for (int i = argc - 1; i >= 0; i--) {
        size_t slen = strlen(k_argv[i]) + 1;
        sp -= slen;
        memcpy((void *)sp, k_argv[i], slen);
        argv_ptrs[i] = sp;
    }

    sp &= ~7ULL;

    /* Push envp NULL */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Push argv NULL terminator */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Push argv pointers */
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        *(uint64_t *)sp = argv_ptrs[i];
    }

    /* Push argc */
    sp -= 8;
    *(uint64_t *)sp = (uint64_t)argc;

    t->user_entry = entry;
    t->user_stack = sp;

    /* Jump directly into new executable in Ring 3 */
    arch_enter_user_mode(entry, sp);
    return 0;
}

static int64_t sys_sysinfo(struct sysinfo *info) {
    if (!info)
        return -1;
    memset(info, 0, sizeof(struct sysinfo));
    uint32_t freq = pit_get_frequency();
    info->uptime = (long)(pit_get_ticks() / (freq ? freq : 1000));
    info->totalram = pmm_get_total_memory();
    info->freeram = pmm_get_free_memory();
    info->bufferram = 64 * 4096;
    info->mem_unit = 1;

    proc_info_t procs[64];
    info->procs = (unsigned short)process_get_list(procs, 64);
    return 0;
}

static int64_t sys_statfs(const char *path, struct statfs *buf) {
    if (!path || !buf)
        return -1;
    memset(buf, 0, sizeof(struct statfs));

    char full_path[256];
    vfs_normalize_path(path, full_path, sizeof(full_path));

    if (strncmp(full_path, "/mnt", 4) == 0) {
        block_device_t *hda = block_device_get("hda");
        buf->f_type = 0xEF53; /* EXT2_SUPER_MAGIC */
        buf->f_bsize = 1024;
        buf->f_blocks = hda ? (hda->sector_count * 512) / 1024 : 32768;
        buf->f_bfree = (buf->f_blocks > 266) ? buf->f_blocks - 266 : 0;
        buf->f_bavail = buf->f_bfree;
        buf->f_files = 8192;
        buf->f_ffree = 8192 - 14;
        buf->f_namelen = 255;
        return 0;
    }

    if (strncmp(full_path, "/dev", 4) == 0) {
        buf->f_type = 0x1373; /* DEVFS_SUPER_MAGIC */
        buf->f_bsize = 512;
        buf->f_blocks = 1024;
        buf->f_bfree = 1024;
        buf->f_bavail = 1024;
        buf->f_files = 64;
        buf->f_ffree = 58;
        buf->f_namelen = 128;
        return 0;
    }

    /* Default root / (RAMFS / Initramfs) */
    buf->f_type = 0x858458F6; /* RAMFS_MAGIC */
    buf->f_bsize = 4096;
    buf->f_blocks = pmm_get_total_memory() / 4096;
    buf->f_bfree = pmm_get_free_memory() / 4096;
    buf->f_bavail = buf->f_bfree;
    buf->f_files = 4096;
    buf->f_ffree = 4000;
    buf->f_namelen = 255;
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
        return -1;
    return (int64_t)process_get_list(buf, max_count);
}

static int64_t sys_unlink(const char *pathname) {
    if (!pathname)
        return -1;
    return (int64_t)vfs_unlink(pathname);
}

static int64_t sys_pread64(int fd, void *buf, size_t count, off_t offset) {
    if (!buf || count == 0 || offset < 0)
        return 0;
    process_t *proc = sched_get_current_process();
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return -1;
    vfs_node_t *node = proc->fds[fd]->node;
    if (!node->ops || !node->ops->read)
        return -1;
    return node->ops->read(node, offset, count, buf);
}

static int64_t sys_pwrite64(int fd, const void *buf, size_t count, off_t offset) {
    if (!buf || count == 0 || offset < 0)
        return 0;
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
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0)
            continue;
        int64_t r = sys_read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0)
            return (total > 0) ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len)
            break;
    }
    return total;
}

static int64_t sys_writev(int fd, const struct iovec_k *iov, int iovcnt) {
    if (!iov || iovcnt <= 0)
        return 0;
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0)
            continue;
        int64_t r = sys_write(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0)
            return (total > 0) ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len)
            break;
    }
    return total;
}

static int64_t sys_mprotect(void *addr, size_t len, int prot) {
    process_t *proc = sched_get_current_process();
    if (!proc || !addr || len == 0)
        return -22; /* -EINVAL */

    /* POSIX: addr must be a multiple of the page size */
    if ((uintptr_t)addr & (PAGE_SIZE - 1))
        return -22; /* -EINVAL */

    /* Check for integer overflow and canonical userspace boundary */
    uintptr_t start = (uintptr_t)addr;
    if (start + len < start || (start + len) > USER_ADDR_MAX)
        return -12; /* -ENOMEM */

    uintptr_t end = ALIGN_UP(start + len, PAGE_SIZE);

    /* POSIX: If any part of the address range is not mapped, fail with ENOMEM */
    for (uintptr_t p = start; p < end; p += PAGE_SIZE) {
        if (vmm_virt_to_phys(proc->pagemap, p) == 0) {
            return -12; /* -ENOMEM */
        }
    }

    /* Compute page table flags */
    uint64_t flags = 0;
    if (prot != 0) { /* Not PROT_NONE */
        flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (prot & 2) /* PROT_WRITE */
            flags |= VMM_FLAG_WRITABLE;
        if (!(prot & 4)) /* PROT_EXEC not set -> NX bit */
            flags |= VMM_FLAG_NO_EXECUTE;
    }

    for (uintptr_t p = start; p < end; p += PAGE_SIZE) {
        uintptr_t phys = vmm_virt_to_phys(proc->pagemap, p) & PHYS_ADDR_MASK;
        if (flags == 0) {
            /* PROT_NONE: unmap page entry so access triggers page fault */
            vmm_unmap_page(proc->pagemap, p);
        } else {
            vmm_map_page(proc->pagemap, p, phys, flags);
        }
    }
    return 0;
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

struct rlimit_k {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

static int64_t sys_getrlimit(int resource, struct rlimit_k *rlim) {
    if (!rlim)
        return -1;
    if (resource == 7) { /* RLIMIT_NOFILE */
        rlim->rlim_cur = MAX_FD;
        rlim->rlim_max = MAX_FD;
    } else if (resource == 3) { /* RLIMIT_STACK */
        rlim->rlim_cur = 8 * 1024 * 1024;
        rlim->rlim_max = 8 * 1024 * 1024;
    } else {
        rlim->rlim_cur = 0x7FFFFFFF;
        rlim->rlim_max = 0x7FFFFFFF;
    }
    return 0;
}

static int64_t sys_setrlimit(int resource, const struct rlimit_k *rlim) {
    (void)resource;
    (void)rlim;
    return 0;
}

static int64_t sys_getrusage(int who, void *usage) {
    (void)who;
    if (usage) {
        memset(usage, 0, 128);
    }
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
    (void)clk_id;
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 1000000;
    }
    return 0;
}

#define AT_FDCWD -100

static void build_at_path(int dirfd, const char *pathname, char *out, size_t out_len) {
    if (!pathname || !out || out_len == 0)
        return;
    if (pathname[0] == '/') {
        strncpy(out, pathname, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    process_t *proc = sched_get_current_process();
    if (dirfd == AT_FDCWD || !proc || dirfd < 0 || dirfd >= MAX_FD || !proc->fds[dirfd]) {
        vfs_resolve_path(pathname, out, out_len);
        return;
    }
    vfs_node_t *dir_node = proc->fds[dirfd]->node;
    if (dir_node && dir_node->flags == VFS_TYPE_DIRECTORY) {
        ksnprintf(out, out_len, "/%s/%s", dir_node->name, pathname);
        char norm[256];
        vfs_normalize_path(out, norm, sizeof(norm));
        strncpy(out, norm, out_len - 1);
        out[out_len - 1] = '\0';
    } else {
        vfs_resolve_path(pathname, out, out_len);
    }
}

static int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    return sys_open(full, flags, mode);
}

static int64_t sys_mkdirat(int dirfd, const char *pathname, mode_t mode) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    return sys_mkdir(full, mode);
}

static int64_t sys_unlinkat(int dirfd, const char *pathname, int flags) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (flags & 0x200) { /* AT_REMOVEDIR */
        return sys_rmdir(full);
    }
    return sys_unlink(full);
}

static int64_t sys_newfstatat(int dirfd, const char *pathname, struct stat *buf, int flags) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    if (flags & 0x100) { /* AT_SYMLINK_NOFOLLOW */
        return sys_lstat(full, buf);
    }
    return sys_stat(full, buf);
}

static int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    (void)flags;
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    return sys_access(full, mode);
}

static int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    return sys_readlink(full, buf, bufsiz);
}

static int64_t sys_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags) {
    (void)flags;
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
    return sys_chmod(full, mode);
}

static int64_t sys_fchownat(int dirfd, const char *pathname, uid_t uid, gid_t gid, int flags) {
    (void)flags;
    char full[256];
    build_at_path(dirfd, pathname, full, sizeof(full));
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
    (void)prot;
    (void)flags;
    process_t *proc = sched_get_current_process();
    if (!proc || length == 0)
        return (void *)-1;

    /* Check if file descriptor has custom device mmap handler (e.g. /dev/dri/card0) */
    if (fd >= 0 && fd < MAX_FD && proc->fds[fd] && proc->fds[fd]->node) {
        vfs_node_t *node = proc->fds[fd]->node;
        if (node->ops && node->ops->mmap) {
            void *out_vaddr = NULL;
            if (node->ops->mmap(node, addr, length, prot, flags, offset, &out_vaddr) == 0) {
                return out_vaddr;
            }
            return (void *)-1;
        }
    }

    if (proc->mmap_current == 0) {
        proc->mmap_current = 0x0000600000000000ULL;
    }

    size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t vaddr = (uintptr_t)addr;
    if (vaddr == 0) {
        vaddr = proc->mmap_current;
        proc->mmap_current += pages * PAGE_SIZE;
    }

    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys = pmm_alloc_page();
        if (!phys) {
            /* Roll back pages already allocated/mapped for this request. */
            for (size_t j = 0; j < i; j++) {
                uintptr_t jvirt = vaddr + j * PAGE_SIZE;
                uintptr_t jphys = vmm_virt_to_phys(proc->pagemap, jvirt);
                vmm_unmap_page(proc->pagemap, jvirt);
                if (jphys)
                    pmm_free_page(jphys);
            }
            return (void *)-1;
        }
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        vmm_map_page(proc->pagemap, vaddr + i * PAGE_SIZE, phys, VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }

    /* If file-backed mapping, read data from file into physical pages */
    if (fd >= 0 && fd < MAX_FD && proc->fds[fd] && proc->fds[fd]->node) {
        vfs_node_t *node = proc->fds[fd]->node;
        if (node->ops && node->ops->read) {
            size_t bytes_left = length;
            if (node->length > (size_t)offset) {
                size_t avail = node->length - (size_t)offset;
                if (bytes_left > avail)
                    bytes_left = avail;
            } else {
                bytes_left = 0;
            }

            off_t cur_offset = offset;
            for (size_t i = 0; i < pages && bytes_left > 0; i++) {
                uintptr_t virt = vaddr + i * PAGE_SIZE;
                uintptr_t phys = vmm_virt_to_phys(proc->pagemap, virt);
                if (!phys)
                    break;
                void *page_buf = (void *)PHYS_TO_VIRT(phys);
                size_t chunk = (bytes_left > PAGE_SIZE) ? PAGE_SIZE : bytes_left;
                ssize_t read_bytes = node->ops->read(node, cur_offset, chunk, page_buf);
                if (read_bytes <= 0)
                    break;
                bytes_left -= read_bytes;
                cur_offset += read_bytes;
            }
        }
    }

    return (void *)vaddr;
}

static int sys_munmap(void *addr, size_t length) {
    process_t *proc = sched_get_current_process();
    if (!proc || !addr || length == 0)
        return -1;

    size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t vaddr = (uintptr_t)addr;
    for (size_t i = 0; i < pages; i++) {
        vmm_unmap_page(proc->pagemap, vaddr + i * PAGE_SIZE);
    }
    return 0;
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

            file_descriptor_t *fdesc = proc->fds[fd];
            vfs_node_t *node = fdesc->node;
            if (!node)
                continue;

            if (node->flags == VFS_TYPE_SOCKET) {
                socket_t *sock = (socket_t *)node->device_data;
                if (sock) {
                    if (sock->domain == AF_UNIX) {
                        if (sock->state == SS_LISTENING) {
                            if ((fds[i].events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                                sock->accept_count > 0) {
                                fds[i].revents |= (fds[i].events & (0x0001 | 0x0040 | 0x0080));
                            }
                            if (sock->state == SS_CLOSED) {
                                fds[i].revents |= (POLLERR | 0x0010 /* POLLHUP */);
                            }
                        } else {
                            if ((fds[i].events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                                (sock->rx_len > 0 || sock->state == SS_CLOSED ||
                                 (sock->state == SS_CONNECTED && (!sock->peer || sock->peer->state == SS_CLOSED)))) {
                                fds[i].revents |= (fds[i].events & (0x0001 | 0x0040 | 0x0080));
                            }
                            if (sock->state == SS_CLOSED ||
                                (sock->state == SS_CONNECTED && (!sock->peer || sock->peer->state == SS_CLOSED))) {
                                fds[i].revents |= 0x0010 /* POLLHUP */;
                            }
                            if ((fds[i].events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */ | 0x0200 /* POLLWRBAND */)) &&
                                sock->state == SS_CONNECTED && sock->peer && sock->peer->state != SS_CLOSED &&
                                sock->peer->rx_len < SOCK_RX_BUF_SIZE) {
                                fds[i].revents |= (fds[i].events & (0x0004 | 0x0100 | 0x0200));
                            }
                        }
                    } else if (sock->type == SOCK_DGRAM) {
                        if ((fds[i].events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                            sock->rx_len > 0) {
                            fds[i].revents |= (fds[i].events & (0x0001 | 0x0040 | 0x0080));
                        }
                        if (fds[i].events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */ | 0x0200 /* POLLWRBAND */)) {
                            fds[i].revents |= (fds[i].events & (0x0004 | 0x0100 | 0x0200));
                        }
                    } else if (sock->type == SOCK_STREAM) {
                        if ((fds[i].events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */ | 0x0080 /* POLLRDBAND */)) &&
                            (sock->rx_len > 0 || sock->accept_count > 0 || sock->state == SS_CLOSED ||
                             sock->tcp_state == TCP_STATE_CLOSE_WAIT || sock->tcp_state == TCP_STATE_CLOSED)) {
                            fds[i].revents |= (fds[i].events & (0x0001 | 0x0040 | 0x0080));
                        }
                        if ((fds[i].events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */ | 0x0200 /* POLLWRBAND */)) &&
                            (sock->state == SS_CONNECTED || sock->tcp_state == TCP_STATE_ESTABLISHED)) {
                            fds[i].revents |= (fds[i].events & (0x0004 | 0x0100 | 0x0200));
                        }
                        if (sock->tcp_state == TCP_STATE_CLOSED && sock->state == SS_CONNECTING) {
                            fds[i].revents |= (POLLERR | POLLHUP);
                        }
                    }
                }
            } else if (strcmp(node->name, "event0") == 0 || strcmp(node->name, "mouse") == 0) {
                if ((fds[i].events & POLLIN) && evdev_mouse_has_events())
                    fds[i].revents |= POLLIN;
            } else if (strcmp(node->name, "event1") == 0) {
                if ((fds[i].events & POLLIN) && evdev_kbd_has_events())
                    fds[i].revents |= POLLIN;
            } else if (strcmp(node->name, "mice") == 0 || strcmp(node->name, "psaux") == 0) {
                if ((fds[i].events & POLLIN) && evdev_mice_has_data())
                    fds[i].revents |= POLLIN;
            } else if (strncmp(node->name, "ptmx", 4) == 0 || strncmp(node->name, "pts", 3) == 0) {
                if ((fds[i].events & POLLIN) && pty_node_has_pollin(node))
                    fds[i].revents |= POLLIN;
                if ((fds[i].events & POLLOUT) && pty_node_has_pollout(node))
                    fds[i].revents |= POLLOUT;
            } else if (node->flags == VFS_TYPE_PIPE && node->device_data) {
                pipe_chan_t *p = (pipe_chan_t *)node->device_data;
                if (fdesc->flags & O_WRONLY) {
                    if ((fds[i].events & (0x0004 /* POLLOUT */ | 0x0100 /* POLLWRNORM */)) &&
                        p->count < PIPE_BUF_SIZE && p->readers > 0) {
                        fds[i].revents |= (fds[i].events & (0x0004 | 0x0100));
                    }
                    if (p->readers <= 0) {
                        fds[i].revents |= (POLLERR | 0x0010 /* POLLHUP */);
                    }
                } else {
                    if ((fds[i].events & (0x0001 /* POLLIN */ | 0x0040 /* POLLRDNORM */)) &&
                        (p->count > 0 || p->writers == 0)) {
                        fds[i].revents |= (fds[i].events & (0x0001 | 0x0040));
                    }
                    if (p->writers == 0) {
                        fds[i].revents |= 0x0010 /* POLLHUP */;
                    }
                }
            } else if (strcmp(node->name, "tty") == 0 || strcmp(node->name, "console") == 0 || strcmp(node->name, "serial") == 0) {
                if ((fds[i].events & POLLIN) && tty_has_input())
                    fds[i].revents |= POLLIN;
                if (fds[i].events & POLLOUT)
                    fds[i].revents |= POLLOUT;
            } else if (strcmp(node->name, "card0") == 0 || strcmp(node->name, "renderD128") == 0 || strncmp(node->name, "dri/", 4) == 0) {
                if ((fds[i].events & POLLIN) && drm_has_events())
                    fds[i].revents |= POLLIN;
                if (fds[i].events & POLLOUT)
                    fds[i].revents |= POLLOUT;
            } else {
                if (fds[i].events & POLLIN)
                    fds[i].revents |= POLLIN;
                if (fds[i].events & POLLOUT)
                    fds[i].revents |= POLLOUT;
            }

            if (fds[i].revents)
                ready++;
        }

        if (ready > 0 || timeout == 0)
            break;
        if (timeout > 0 && (pit_get_ticks() - start_tick) >= timeout_ticks)
            break;
        netif_poll_all();
        thread_sleep(2);
    }
    return ready;
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
    (void)flags;
    char path[64];
    static int memfd_id = 1;
    process_t *proc = sched_get_current_process();
    ksnprintf(path, sizeof(path), "/tmp/.memfd_%d_%d", proc ? proc->pid : 0, memfd_id++);
    return sys_open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
}

uint64_t syscall_dispatcher(uint64_t sys_no, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
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
        return kernel_sys_poll(NULL, 0, 0);
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
        return 0;
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
    case SYS_getprocs:
        return sys_getprocs((proc_info_t *)a1, (size_t)a2);
    case SYS_sleep:
        thread_sleep((uint32_t)a1 * 1000);
        return 0;
    case SYS_iopl:
        return sys_iopl((int)a1);
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
    default:
        klog_warn("Syscall: Unknown syscall #%lu called!", sys_no);
        return (uint64_t)-1;
    }
}

void syscall_init(void) {
    syscall_arch_init();
    klog_info("POSIX Syscall Dispatcher registered");
}
