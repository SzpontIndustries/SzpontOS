/*
 * SzpontOS - UNIX98 Pseudo-Terminal (PTY/PTS) Subsystem Implementation
 * Inspired by FreeBSD sys/kern/tty_pts.c
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/pty.h>
#include <drivers/tty.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <sched/sched.h>
#include <sched/process.h>
#include <kernel/spinlock.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <mm/usercopy.h>

static inline bool pty_put_user(void *dst, const void *src, size_t len) {
    if (!dst || len == 0) return true;
    if ((uintptr_t)dst <= USER_ADDR_MAX) {
        return copy_to_user((uintptr_t)dst, src, len);
    }
    memcpy(dst, src, len);
    return true;
}

static inline bool pty_get_user(void *dst, const void *src, size_t len) {
    if (!src || len == 0) return true;
    if ((uintptr_t)src <= USER_ADDR_MAX) {
        return copy_from_user(dst, (uintptr_t)src, len);
    }
    memcpy(dst, src, len);
    return true;
}

#define PTY_BUFFER_SIZE 4096

typedef struct pty_pair {
    int index;
    bool allocated;
    bool locked;
    struct winsize ws;
    termios_t term;

    /* Master -> Slave buffer */
    char m2s_buf[PTY_BUFFER_SIZE];
    size_t m2s_head;
    size_t m2s_tail;
    size_t m2s_count;

    /* Slave -> Master buffer */
    char s2m_buf[PTY_BUFFER_SIZE];
    size_t s2m_head;
    size_t s2m_tail;
    size_t s2m_count;

    spinlock_t lock;
    pid_t fg_pgrp;
    vfs_node_t *master_node;
    vfs_node_t *slave_node;
    int slave_open_count;
    bool slave_hung_up;
} pty_pair_t;

static pty_pair_t g_ptys[MAX_PTS];
static spinlock_t g_pty_global_lock = SPINLOCK_INIT;

static vfs_ops_t g_ptmx_ops;
static vfs_ops_t g_pty_master_ops;
static vfs_ops_t g_pty_slave_ops;

/* =========================================================================
 * Master Node Operations
 * ========================================================================= */

static ssize_t pty_master_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    (void)offset;
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (!pty || !buffer || size == 0)
        return 0;

    char *dst = (char *)buffer;
    size_t read_bytes = 0;

    spinlock_acquire(&pty->lock);
    while (read_bytes < size && pty->s2m_count > 0) {
        dst[read_bytes++] = pty->s2m_buf[pty->s2m_tail];
        pty->s2m_tail = (pty->s2m_tail + 1) % PTY_BUFFER_SIZE;
        pty->s2m_count--;
    }
    bool hung_up = pty->slave_hung_up;
    spinlock_release(&pty->lock);

    if (read_bytes == 0) {
        if (hung_up) {
            return -5; /* -EIO when slave closed (Linux standard for pty master EOF) */
        }
        return -11; /* -EAGAIN */
    }

    return (ssize_t)read_bytes;
}

static ssize_t pty_master_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    (void)offset;
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (!pty || !buffer || size == 0)
        return 0;

    const char *src = (const char *)buffer;
    size_t written = 0;

    while (written < size) {
        char c = src[written++];

        /* Signal handling if ISIG is enabled: route strictly to foreground process group */
        if (pty->term.c_lflag & TTY_LFLAG_ISIG) {
            int sig = 0;
            if (c == (char)pty->term.c_cc[TTY_VINTR]) {
                sig = SIGINT;
            } else if (c == (char)pty->term.c_cc[TTY_VQUIT]) {
                sig = SIGQUIT;
            } else if (c == (char)pty->term.c_cc[TTY_VSUSP]) {
                sig = SIGTSTP;
            }

            if (sig != 0) {
                if (pty->fg_pgrp > 1) {
                    process_signal_pgrp(pty->fg_pgrp, sig);
                } else {
                    process_signal_ctty(pty->slave_node, sig);
                }
                continue;
            }
        }

        /* Input processing (c_iflag) */
        if (c == '\r') {
            if (pty->term.c_iflag & TTY_IFLAG_IGNCR) {
                continue;
            }
            if (pty->term.c_iflag & TTY_IFLAG_ICRNL) {
                c = '\n';
            }
        } else if (c == '\n') {
            if (pty->term.c_iflag & TTY_IFLAG_INLCR) {
                c = '\r';
            }
        }

        spinlock_acquire(&pty->lock);
        if (pty->m2s_count < PTY_BUFFER_SIZE) {
            pty->m2s_buf[pty->m2s_head] = c;
            pty->m2s_head = (pty->m2s_head + 1) % PTY_BUFFER_SIZE;
            pty->m2s_count++;
        }
        spinlock_release(&pty->lock);
    }

    return (ssize_t)written;
}

static int pty_master_ioctl(vfs_node_t *node, uint64_t request, uintptr_t arg) {
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (!pty)
        return -22; /* -EINVAL */

    switch (request) {
    case TIOCGPTN:
        if (!arg)
            return -22;
        if (!pty_put_user((void *)arg, &pty->index, sizeof(int)))
            return -14; /* -EFAULT */
        return 0;
    case TIOCSPTLCK: {
        if (!arg)
            return -22;
        int lck = 0;
        if (!pty_get_user(&lck, (const void *)arg, sizeof(int)))
            return -14; /* -EFAULT */
        pty->locked = (lck != 0);
        return 0;
    }
    case TIOCGWINSZ:
        if (!arg)
            return -22;
        if (!pty_put_user((void *)arg, &pty->ws, sizeof(struct winsize)))
            return -14; /* -EFAULT */
        return 0;
    case TIOCSWINSZ:
        if (!arg)
            return -22;
        if (!pty_get_user(&pty->ws, (const void *)arg, sizeof(struct winsize)))
            return -14; /* -EFAULT */
        if (pty->fg_pgrp > 1) {
            process_signal_pgrp(pty->fg_pgrp, SIGWINCH);
        } else {
            process_signal_ctty(pty->slave_node, SIGWINCH);
        }
        return 0;
    case 0x5401: /* TCGETS */
        if (!arg)
            return -22;
        if (!pty_put_user((void *)arg, &pty->term, sizeof(termios_t)))
            return -14; /* -EFAULT */
        return 0;
    case 0x5402: /* TCSETS */
    case 0x5403: /* TCSETSW */
    case 0x5404: /* TCSETSF */
        if (!arg)
            return -22;
        if (!pty_get_user(&pty->term, (const void *)arg, sizeof(termios_t)))
            return -14; /* -EFAULT */
        return 0;
    case 0x540F: /* TIOCGPGRP */ {
        if (!arg)
            return -22;
        pid_t pgrp = pty->fg_pgrp;
        if (pgrp <= 1) {
            process_t *curr = sched_get_current_process();
            pgrp = (curr && curr->pgid > 1) ? curr->pgid : 0;
        }
        if (!pty_put_user((void *)arg, &pgrp, sizeof(pid_t)))
            return -14; /* -EFAULT */
        return 0;
    }
    case 0x5410: /* TIOCSPGRP */ {
        if (!arg)
            return -22;
        pid_t pgrp = 0;
        if (!pty_get_user(&pgrp, (const void *)arg, sizeof(pid_t)))
            return -14; /* -EFAULT */
        if (pgrp > 1) {
            pty->fg_pgrp = pgrp;
        }
        return 0;
    }
    case 0x5421: /* FIONBIO */
        return 0;
    default:
        return 0;
    }
}

/* =========================================================================
 * Slave Node Operations
 * ========================================================================= */

static ssize_t pty_slave_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (!pty || !pty->allocated)
        return 0; /* EOF: master closed/disconnected */

    char *dst = (char *)buffer;
    size_t read_bytes = 0;

    while (read_bytes == 0) {
        spinlock_acquire(&pty->lock);
        if (!pty->allocated) {
            spinlock_release(&pty->lock);
            return 0; /* EOF */
        }
        while (read_bytes < size && pty->m2s_count > 0) {
            dst[read_bytes++] = pty->m2s_buf[pty->m2s_tail];
            pty->m2s_tail = (pty->m2s_tail + 1) % PTY_BUFFER_SIZE;
            pty->m2s_count--;
        }
        spinlock_release(&pty->lock);

        if (read_bytes > 0)
            break;

        process_t *curr = sched_get_current_process();
        thread_t *pty_self = sched_get_current_thread();
        uint32_t pty_pend = curr ? curr->pending_signals : 0;
        uint32_t pty_mask = 0;
        if (pty_self) {
            pty_pend |= pty_self->thread_pending;
            pty_mask = pty_self->blocked_signals;
        }
        if (pty_pend & ~pty_mask) {
            return -4; /* -EINTR */
        }

        /* Block until input arrives from terminal client (e.g. xterm) */
        thread_sleep(1);
    }

    return (ssize_t)read_bytes;
}

static ssize_t pty_slave_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    (void)offset;
    if (!node || !buffer || size == 0)
        return 0;

    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (!pty || !pty->allocated) {
        process_t *curr = sched_get_current_process();
        if (curr) {
            process_send_signal(curr, SIGHUP);
        }
        return -5; /* -EIO: disconnected terminal */
    }

    const char *src = (const char *)buffer;
    size_t written = 0;

    while (written < size) {
        spinlock_acquire(&pty->lock);
        if (!pty->allocated) {
            spinlock_release(&pty->lock);
            process_t *curr = sched_get_current_process();
            if (curr) {
                process_send_signal(curr, SIGHUP);
            }
            return written > 0 ? (ssize_t)written : -5; /* -EIO */
        }

        bool post = (pty->term.c_oflag & TTY_OFLAG_OPOST) != 0;
        bool onlcr = post && ((pty->term.c_oflag & TTY_OFLAG_ONLCR) != 0);
        bool ocrnl = post && ((pty->term.c_oflag & TTY_OFLAG_OCRNL) != 0);

        while (written < size && pty->s2m_count < PTY_BUFFER_SIZE) {
            char c = src[written];
            if (c == '\n' && onlcr) {
                if (pty->s2m_count + 2 > PTY_BUFFER_SIZE) {
                    break;
                }
                pty->s2m_buf[pty->s2m_head] = '\r';
                pty->s2m_head = (pty->s2m_head + 1) % PTY_BUFFER_SIZE;
                pty->s2m_count++;

                pty->s2m_buf[pty->s2m_head] = '\n';
                pty->s2m_head = (pty->s2m_head + 1) % PTY_BUFFER_SIZE;
                pty->s2m_count++;
                written++;
            } else if (c == '\r' && ocrnl) {
                pty->s2m_buf[pty->s2m_head] = '\n';
                pty->s2m_head = (pty->s2m_head + 1) % PTY_BUFFER_SIZE;
                pty->s2m_count++;
                written++;
            } else {
                pty->s2m_buf[pty->s2m_head] = c;
                pty->s2m_head = (pty->s2m_head + 1) % PTY_BUFFER_SIZE;
                pty->s2m_count++;
                written++;
            }
        }
        spinlock_release(&pty->lock);

        if (written == size)
            break;

        process_t *curr = sched_get_current_process();
        thread_t *pty_self = sched_get_current_thread();
        uint32_t pty_pend = curr ? curr->pending_signals : 0;
        uint32_t pty_mask = 0;
        if (pty_self) {
            pty_pend |= pty_self->thread_pending;
            pty_mask = pty_self->blocked_signals;
        }
        if (pty_pend & ~pty_mask) {
            return written > 0 ? (ssize_t)written : -4; /* -EINTR */
        }

        thread_sleep(1);
    }

    return (ssize_t)written;
}

static int pty_slave_ioctl(vfs_node_t *node, uint64_t request, uintptr_t arg) {
    if (!node)
        return -22;
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (!pty || !pty->allocated)
        return -5; /* -EIO */

    switch (request) {
    case TIOCGPTN:
        if (!arg)
            return -22;
        if (!pty_put_user((void *)arg, &pty->index, sizeof(int)))
            return -14;
        return 0;
    case TIOCGWINSZ:
        if (!arg)
            return -22;
        if (!pty_put_user((void *)arg, &pty->ws, sizeof(struct winsize)))
            return -14;
        return 0;
    case TIOCSWINSZ:
        if (!arg)
            return -22;
        if (!pty_get_user(&pty->ws, (const void *)arg, sizeof(struct winsize)))
            return -14;
        if (pty->fg_pgrp > 1) {
            process_signal_pgrp(pty->fg_pgrp, SIGWINCH);
        } else {
            process_signal_ctty(pty->slave_node, SIGWINCH);
        }
        return 0;
    case 0x5401: /* TCGETS */
        if (!arg)
            return -22;
        if (!pty_put_user((void *)arg, &pty->term, sizeof(termios_t)))
            return -14;
        return 0;
    case 0x5402: /* TCSETS */
    case 0x5403: /* TCSETSW */
    case 0x5404: /* TCSETSF */
        if (!arg)
            return -22;
        if (!pty_get_user(&pty->term, (const void *)arg, sizeof(termios_t)))
            return -14;
        return 0;
    case 0x540E: /* TIOCSCTTY */ {
        process_t *curr = sched_get_current_process();
        if (curr) {
            curr->has_ctty = true;
            curr->ctty = node;
            if (pty->fg_pgrp <= 1 && curr->pgid > 1) {
                pty->fg_pgrp = curr->pgid;
            }
        }
        return 0;
    }
    case 0x5422: /* TIOCNOTTY */ {
        process_t *curr = sched_get_current_process();
        if (curr) {
            curr->has_ctty = false;
            curr->ctty = NULL;
        }
        return 0;
    }
    case 0x540F: /* TIOCGPGRP */ {
        if (!arg)
            return -22;
        pid_t pgrp = pty->fg_pgrp;
        if (pgrp <= 1) {
            process_t *curr = sched_get_current_process();
            pgrp = (curr && curr->pgid > 1) ? curr->pgid : 0;
        }
        if (!pty_put_user((void *)arg, &pgrp, sizeof(pid_t)))
            return -14;
        return 0;
    }
    case 0x5410: /* TIOCSPGRP */ {
        if (!arg)
            return -22;
        pid_t pgrp = 0;
        if (!pty_get_user(&pgrp, (const void *)arg, sizeof(pid_t)))
            return -14;
        if (pgrp > 1) {
            pty->fg_pgrp = pgrp;
        }
        return 0;
    }
    case 0x5421: /* FIONBIO */
        return 0;
    default:
        return 0;
    }
}

static int pty_slave_open(vfs_node_t *node, uint32_t flags) {
    (void)flags;
    pty_pair_t *pty = node ? (pty_pair_t *)node->device_data : NULL;
    if (!pty) return -19; /* -ENODEV */
    spinlock_acquire(&pty->lock);
    pty->slave_open_count++;
    pty->slave_hung_up = false;
    spinlock_release(&pty->lock);
    return 0;
}

static int pty_slave_close(vfs_node_t *node) {
    pty_pair_t *pty = node ? (pty_pair_t *)node->device_data : NULL;
    if (!pty) return 0;
    spinlock_acquire(&pty->lock);
    if (pty->slave_open_count > 0) {
        pty->slave_open_count--;
    }
    if (pty->slave_open_count == 0) {
        pty->slave_hung_up = true;
    }
    spinlock_release(&pty->lock);
    return 0;
}

/* =========================================================================
 * /dev/ptmx Multiplexer
 * ========================================================================= */

static int ptmx_open(vfs_node_t *node, uint32_t flags) {
    (void)flags;
    spinlock_acquire(&g_pty_global_lock);

    int idx = -1;
    for (int i = 0; i < MAX_PTS; i++) {
        if (!g_ptys[i].allocated) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        spinlock_release(&g_pty_global_lock);
        return -24; /* -EMFILE */
    }

    pty_pair_t *pty = &g_ptys[idx];
    memset(pty, 0, sizeof(pty_pair_t));
    pty->index = idx;
    pty->allocated = true;
    pty->locked = true;
    pty->ws.ws_col = 80;
    pty->ws.ws_row = 24;
    pty->lock = SPINLOCK_INIT;

    /* Setup standard default POSIX termios flags */
    pty->term.c_iflag = TTY_IFLAG_ICRNL;
    pty->term.c_oflag = TTY_OFLAG_OPOST | TTY_OFLAG_ONLCR;
    pty->term.c_lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON | TTY_LFLAG_ECHO | TTY_LFLAG_ECHOE | TTY_LFLAG_ECHOK;
    pty->term.c_cc[TTY_VINTR] = 0x03;
    pty->term.c_cc[TTY_VQUIT] = 0x1C;
    pty->term.c_cc[TTY_VERASE] = 0x7F;
    pty->term.c_cc[TTY_VKILL] = 0x15;
    pty->term.c_cc[TTY_VEOF] = 0x04;
    pty->term.c_cc[TTY_VSUSP] = 0x1A;
    pty->term.c_cc[TTY_VTIME] = 0;
    pty->term.c_cc[TTY_VMIN] = 1;

    /* Setup dedicated master node for this open handle */
    ksnprintf(node->name, sizeof(node->name), "ptmx_%d", idx);
    node->flags = VFS_TYPE_CHARDEVICE;
    node->permissions = 0666;
    node->ops = &g_pty_master_ops;
    node->device_data = pty;
    pty->master_node = node;

    /* Create slave device in /dev/pts/N and /dev/ptsN */
    char slave_name[32];
    ksnprintf(slave_name, sizeof(slave_name), "pts%d", idx);
    vfs_node_t *slave = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    slave->flags = VFS_TYPE_CHARDEVICE;
    slave->permissions = 0666;
    slave->ops = &g_pty_slave_ops;
    slave->device_data = pty;
    devfs_register_device(slave_name, slave);

    char slave_dir_name[32];
    ksnprintf(slave_dir_name, sizeof(slave_dir_name), "pts/%d", idx);
    devfs_register_device_path(slave_dir_name, slave);
    ksnprintf(slave->name, sizeof(slave->name), "pts%d", idx);

    pty->slave_node = slave;

    spinlock_release(&g_pty_global_lock);
    return 0;
}

static int pty_master_close(vfs_node_t *node) {
    pty_pair_t *pty = node ? (pty_pair_t *)node->device_data : NULL;
    if (pty) {
        int idx = pty->index;
        spinlock_acquire(&pty->lock);
        pty->allocated = false;
        vfs_node_t *slave = pty->slave_node;
        pid_t fg_pgrp = pty->fg_pgrp;
        if (slave) {
            slave->device_data = NULL;
        }
        pty->slave_node = NULL;
        pty->master_node = NULL;
        pty->fg_pgrp = 0;
        spinlock_release(&pty->lock);

        /* Send SIGHUP and SIGCONT to foreground process group */
        if (fg_pgrp > 1) {
            process_kill(-fg_pgrp, SIGHUP);
            process_kill(-fg_pgrp, SIGCONT);
        }

        /* Send SIGHUP to all processes attached to this controlling terminal */
        if (slave) {
            process_signal_ctty(slave, SIGHUP);
        }

        /* Unregister and clean up slave devices in DevFS */
        char slave_dir_name[32];
        ksnprintf(slave_dir_name, sizeof(slave_dir_name), "pts/%d", idx);
        devfs_unregister_device_path(slave_dir_name);

        char slave_name[32];
        ksnprintf(slave_name, sizeof(slave_name), "pts%d", idx);
        devfs_unregister_device(slave_name);
    }
    return 0;
}

static vfs_ops_t g_ptmx_ops = {.open = ptmx_open};

static vfs_ops_t g_pty_master_ops = {
    .read = pty_master_read,
    .write = pty_master_write,
    .ioctl = pty_master_ioctl,
    .close = pty_master_close,
};

static vfs_ops_t g_pty_slave_ops = {
    .open = pty_slave_open,
    .close = pty_slave_close,
    .read = pty_slave_read,
    .write = pty_slave_write,
    .ioctl = pty_slave_ioctl,
};

void pty_init(void) {
    memset(g_ptys, 0, sizeof(g_ptys));
    vfs_node_t *ptmx = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    ptmx->flags = VFS_TYPE_CHARDEVICE;
    ptmx->permissions = 0666;
    ptmx->ops = &g_ptmx_ops;
    devfs_register_device("ptmx", ptmx);
    klog_info("PTY: UNIX98 pseudo-terminal multiplexer /dev/ptmx registered");
}

bool pty_node_has_pollin(vfs_node_t *node) {
    if (!node || !node->device_data)
        return false;
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (strncmp(node->name, "ptmx", 4) == 0) {
        return pty->s2m_count > 0 || pty->slave_hung_up;
    }
    if (strncmp(node->name, "pts", 3) == 0 || (node->ops && node->ops->read == pty_slave_read)) {
        return pty->m2s_count > 0;
    }
    return false;
}

bool pty_node_has_pollout(vfs_node_t *node) {
    if (!node || !node->device_data)
        return true;
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (strncmp(node->name, "ptmx", 4) == 0) {
        return pty->m2s_count < PTY_BUFFER_SIZE;
    }
    if (strncmp(node->name, "pts", 3) == 0 || (node->ops && node->ops->read == pty_slave_read)) {
        return pty->s2m_count < PTY_BUFFER_SIZE;
    }
    return true;
}

bool pty_node_is_hungup(vfs_node_t *node) {
    if (!node || !node->device_data)
        return false;
    pty_pair_t *pty = (pty_pair_t *)node->device_data;
    if (strncmp(node->name, "ptmx", 4) == 0) {
        return pty->slave_hung_up;
    }
    return false;
}

bool pty_is_slave_node(vfs_node_t *node) {
    if (!node)
        return false;
    if (node->ops == &g_pty_slave_ops)
        return true;
    if (strncmp(node->name, "pts", 3) == 0)
        return true;
    return false;
}
