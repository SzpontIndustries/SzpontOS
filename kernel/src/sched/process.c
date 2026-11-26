#include <sched/process.h>
#include <sched/sched.h>
#include <sched/futex.h>
#include <kernel/signal.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/shm.h>
#include <fs/vfs.h>
#include <fs/pipe.h>
#include <fs/signalfd.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>
#include <arch/x86_64/gdt.h>
#include <mm/usercopy.h>

#define KERNEL_STACK_SIZE (16 * 1024)

static pid_t g_next_pid = 0;
static tid_t g_next_tid = 0;
static list_node_t g_process_list = LIST_HEAD_INIT(g_process_list);
static spinlock_t g_process_lock = SPINLOCK_INIT;
static process_t *g_foreground_proc = NULL;

extern void arch_thread_trampoline(void);

void process_init(void) {
    spinlock_init(&g_process_lock);
    list_init(&g_process_list);
    g_foreground_proc = NULL;
    g_next_pid = 0;
    g_next_tid = 0;
}

process_t *process_get_foreground(void) {
    spinlock_acquire(&g_process_lock);
    process_t *p = g_foreground_proc;
    if (p && p->status != PROCESS_ACTIVE) {
        p = NULL;
        g_foreground_proc = NULL;
    }
    spinlock_release(&g_process_lock);
    return p;
}

void process_set_foreground(process_t *proc) {
    spinlock_acquire(&g_process_lock);
    g_foreground_proc = proc;
    spinlock_release(&g_process_lock);
}

void process_signal_ctty(vfs_node_t *ctty_node, int sig) {
    if (!ctty_node || sig < 0 || sig >= 32)
        return;

    process_t *targets[128];
    size_t count = 0;

    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &g_process_list) {
        process_t *item = container_of(pos, process_t, proc_list_node);
        if (item->status == PROCESS_ACTIVE && item->has_ctty && item->ctty == ctty_node) {
            if (count < 128) {
                targets[count++] = item;
            }
        }
    }
    spinlock_release(&g_process_lock);

    for (size_t i = 0; i < count; i++) {
        process_send_signal(targets[i], sig);
    }
}

void process_signal_pgrp(pid_t pgid, int sig) {
    if (pgid <= 1 || sig <= 0 || sig >= 32)
        return;

    pid_t targets[128];
    int target_count = 0;

    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &g_process_list) {
        process_t *p = container_of(pos, process_t, proc_list_node);
        if (p->status == PROCESS_ACTIVE && p->pgid == pgid) {
            if (target_count < 128) {
                targets[target_count++] = p->pid;
            }
        }
    }
    spinlock_release(&g_process_lock);

    for (int i = 0; i < target_count; i++) {
        process_t *p = process_get_by_pid(targets[i]);
        if (p && p->status == PROCESS_ACTIVE) {
            process_send_signal(p, sig);
        }
    }
}

process_t *process_create(const char *name) {
    spinlock_acquire(&g_process_lock);

    process_t *proc = (process_t *)kzalloc(sizeof(process_t));
    if (!proc) {
        spinlock_release(&g_process_lock);
        return NULL;
    }
    proc->pid = g_next_pid++;
    proc->ppid = 0;
    proc->pgid = proc->pid;
    proc->sid = proc->pid;
    proc->uid = 0;
    proc->gid = 0;
    proc->euid = 0;
    proc->egid = 0;
    proc->suid = 0;
    proc->sgid = 0;
    proc->ngroups = 0;
    proc->priority = 0;
    strncpy(proc->name, name ? name : "process", sizeof(proc->name) - 1);
    proc->status = PROCESS_ACTIVE;
    proc->pagemap = vmm_create_address_space();
    if (!proc->pagemap) {
        kfree(proc);
        spinlock_release(&g_process_lock);
        return NULL;
    }
    proc->brk_start = 0x0000000000800000;
    proc->brk_current = proc->brk_start;
    proc->mmap_current = 0x0000600000000000ULL;
    strcpy(proc->cwd, "/");
    proc->umask = 0022;
    proc->alarm_ticks = 0;
    proc->cpu_time_ns = 0;
    wait_queue_init(&proc->wait_child);

    proc->pending_signals = 0;
    /* Per-thread masks start empty; threads inherit from their creator. */
    for (int i = 0; i < 32; i++) {
        proc->signal_handlers[i] = SIG_DFL;
        memset(&proc->sigactions[i], 0, sizeof(struct sigaction));
    }

    /* Initialize standard default resource limits */
    for (int i = 0; i < RLIM_NLIMITS; i++) {
        proc->rlimits[i].rlim_cur = RLIM_INFINITY;
        proc->rlimits[i].rlim_max = RLIM_INFINITY;
    }
    proc->rlimits[RLIMIT_STACK].rlim_cur = 8 * 1024 * 1024;
    proc->rlimits[RLIMIT_STACK].rlim_max = 8 * 1024 * 1024;
    proc->rlimits[RLIMIT_NOFILE].rlim_cur = MAX_FD;
    proc->rlimits[RLIMIT_NOFILE].rlim_max = MAX_FD;
    proc->rlimits[RLIMIT_CORE].rlim_cur = 0;
    proc->rlimits[RLIMIT_CORE].rlim_max = 0;
    proc->rlimits[RLIMIT_NPROC].rlim_cur = 1024;
    proc->rlimits[RLIMIT_NPROC].rlim_max = 1024;
    proc->rlimits[RLIMIT_MEMLOCK].rlim_cur = 64 * 1024;
    proc->rlimits[RLIMIT_MEMLOCK].rlim_max = 64 * 1024;

    list_init(&proc->threads);
    list_add_tail(&g_process_list, &proc->proc_list_node);

    /* Open default standard streams if VFS is initialized */
    vfs_node_t *tty = vfs_lookup("/dev/tty");
    proc->has_ctty = true;
    proc->ctty = tty;
    if (tty) {
        for (int i = 0; i < 3; i++) {
            proc->fds[i] = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
            proc->fds[i]->node = tty;
            proc->fds[i]->flags = (i == 0) ? O_RDONLY : O_WRONLY;
            proc->fds[i]->refcount = 1;
        }
    }

    spinlock_release(&g_process_lock);
    return proc;
}

/* Tears down a process that was created via process_create() but never
 * successfully reached thread_create() (e.g. ELF loading failed part-way
 * through, possibly due to physical memory exhaustion). Reclaims the
 * address space (and any partially-mapped pages within it), closes any
 * standard streams opened by process_create(), removes it from the
 * global process list, and frees the process_t itself. */
void process_destroy_unstarted(process_t *proc) {
    if (!proc)
        return;

    spinlock_acquire(&g_process_lock);
    list_remove(&proc->proc_list_node);
    spinlock_release(&g_process_lock);

    for (int i = 0; i < 3; i++) {
        if (proc->fds[i]) {
            kfree(proc->fds[i]);
            proc->fds[i] = NULL;
        }
    }

    if (proc->pagemap) {
        vmm_destroy_address_space(proc->pagemap);
    }

    kfree(proc);
}

tid_t process_alloc_tid(void) {
    return __atomic_fetch_add(&g_next_tid, 1, __ATOMIC_RELAXED);
}

bool process_attach_thread(process_t *proc, thread_t *t) {
    if (!proc || !t)
        return false;
    spinlock_acquire(&g_process_lock);
    if (proc->status != PROCESS_ACTIVE) {
        spinlock_release(&g_process_lock);
        return false;
    }
    list_add_tail(&proc->threads, &t->proc_node);
    spinlock_release(&g_process_lock);
    return true;
}

bool process_is_main_thread(process_t *proc, thread_t *t) {
    if (!proc || !t)
        return false;
    spinlock_acquire(&g_process_lock);
    bool main_t = (proc->threads.next == &t->proc_node);
    spinlock_release(&g_process_lock);
    return main_t;
}

thread_t *thread_create(process_t *proc, void (*entry_point)(void), bool is_user) {
    UNUSED(is_user);
    if (!proc)
        return NULL;

    thread_t *t = (thread_t *)kzalloc(sizeof(thread_t));
    if (!t)
        return NULL;
    t->tid = process_alloc_tid();
    t->process = proc;
    t->state = THREAD_READY;
    t->running_cpu = -1;
    list_init(&t->sched_node);
    list_init(&t->proc_node);
    list_init(&t->futex_node);
    memcpy(t->fpu_state, g_default_fpu_state, 512);

    /* Allocate 16 KiB kernel stack */
    size_t stack_pages = KERNEL_STACK_SIZE / PAGE_SIZE;
    uintptr_t stack_phys = pmm_alloc_pages(stack_pages);
    if (!stack_phys) {
        klog_error("thread_create: Out of physical memory allocating kernel stack!");
        kfree(t);
        return NULL;
    }
    t->kernel_stack_bottom = (uintptr_t)PHYS_TO_VIRT(stack_phys);
    t->kernel_stack_top = t->kernel_stack_bottom + KERNEL_STACK_SIZE;

    /* Initialize stack frame for arch_switch_context */
    uint64_t *sp = (uint64_t *)t->kernel_stack_top;

    /* arch_thread_trampoline expects entry_point pushed on stack */
    *(--sp) = (uint64_t)entry_point;
    *(--sp) = (uint64_t)arch_thread_trampoline; /* Return address for ret in switch_context */

    /* Callee-saved registers popped by arch_switch_context (pop order: rflags, r15, r14, r13, r12, rbp, rbx) */
    *(--sp) = 0;     /* RBX */
    *(--sp) = 0;     /* RBP */
    *(--sp) = 0;     /* R12 */
    *(--sp) = 0;     /* R13 */
    *(--sp) = 0;     /* R14 */
    *(--sp) = 0;      /* R15 */
    *(--sp) = 0x3202; /* RFLAGS (IOPL=3) */

    t->rsp = (uintptr_t)sp;

    spinlock_acquire(&g_process_lock);
    list_add_tail(&proc->threads, &t->proc_node);
    spinlock_release(&g_process_lock);

    sched_add_thread(t);
    return t;
}

/* Best-effort robust futex cleanup for an exiting thread (set_robust_list).
 * For every robust mutex owned by tid, set FUTEX_OWNER_DIED and wake one
 * waiter so pthread_mutex_lock in another thread returns EOWNERDEAD instead
 * of hanging. Corrupted lists terminate the walk silently; exit must never
 * fail because of user memory. */
static void robust_cleanup_pagemap(thread_t *curr, pagemap_t *map) {
    if (!curr || curr->robust_head == 0 || !map)
        return;
    struct {
        uint64_t list;
        int64_t futex_offset;
        uint64_t list_op_pending;
    } head;
    if (!copy_from_user_pagemap(map, &head, curr->robust_head, sizeof(head)))
        return;
    /* The head itself lives in user memory that may vanish under us; snapshot. */
    uint64_t pending = head.list_op_pending;
    uint64_t node_addr = head.list;
    int64_t futex_offset = head.futex_offset;
    /* futex_offset is normally small (e.g. 0/4/8); reject absurd values that
     * would turn the walk into arbitrary memory writes. */
    if (futex_offset < 0 || futex_offset > 2048)
        return;

    for (int iter = 0; iter < 1025; iter++) {
        uint64_t cur = 0;
        if (iter == 0 && pending != 0) {
            cur = pending;
        } else {
            /* Advance first, process second: a single-node list whose next
             * points back at the head must still be processed. */
            if (node_addr == 0 || node_addr == curr->robust_head)
                break;
            cur = node_addr;
            uint64_t next = 0;
            node_addr = copy_from_user_pagemap(map, &next, cur, sizeof(next)) ? next : 0;
        }
        if (cur == 0 || cur > USER_ADDR_MAX)
            continue;
        uintptr_t futex_addr = cur + (uintptr_t)futex_offset;
        if (futex_addr > USER_ADDR_MAX)
            continue;
        int32_t val = 0;
        if (!copy_from_user_pagemap(map, &val, futex_addr, sizeof(val)))
            continue;
        uint32_t owner = (uint32_t)val & 0x3FFFFFFFU;
        if (owner != (uint32_t)curr->tid)
            continue;
        if (val & 0x40000000)
            continue; /* Already marked OWNER_DIED */
        int32_t marked = val | 0x40000000;
        if (!copy_to_user_pagemap(map, futex_addr, &marked, sizeof(marked)))
            continue;
        if (curr->process)
            futex_wake_proc(curr->process, futex_addr, 1);
    }
}

/* Release the user-visible thread exit state (robust futexes + TID word)
 * using an explicit address space. Siblings retired from process_exit share
 * the dying process's pagemap, which may differ from the current one when a
 * kill is delivered from another process. Failures are silent: teardown must
 * never fail because of user memory. */
static void teardown_thread_user(thread_t *t, pagemap_t *map) {
    if (!t || !map)
        return;
    process_t *owner = t->process;
    robust_cleanup_pagemap(t, map);
    if (t->clear_child_tid) {
        int32_t zero = 0;
        if (copy_to_user_pagemap(map, t->clear_child_tid, &zero, sizeof(zero)) && owner)
            futex_wake_proc(owner, t->clear_child_tid, 1);
        t->clear_child_tid = 0;
    }
    t->rseq_ptr = 0;
    t->rseq_len = 0;
}

void thread_exit(int exit_code) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return;

    pagemap_t *map = curr->process ? curr->process->pagemap : NULL;
    teardown_thread_user(curr, map);
    futex_remove_thread(curr);

    /* Retire this thread and check whether any live sibling remains under
     * g_process_lock so concurrent sys_clone / thread_exit / procfs scans
     * observe consistent state. */
    process_t *proc = curr->process;
    bool all_dead = true;
    spinlock_acquire(&g_process_lock);
    curr->state = THREAD_ZOMBIE;
    if (proc) {
        list_node_t *pos;
        list_for_each(pos, &proc->threads) {
            thread_t *t = container_of(pos, thread_t, proc_node);
            if (t->state != THREAD_ZOMBIE) {
                all_dead = false;
                break;
            }
        }
    }
    spinlock_release(&g_process_lock);

    if (all_dead && proc) {
        process_exit(exit_code);
    }

    for (;;) {
        sched_yield();
    }
}

void process_close_all_fds(process_t *proc) {
    if (!proc)
        return;
    for (int i = 0; i < MAX_FD; i++) {
        if (proc->fds[i]) {
            file_descriptor_t *f = proc->fds[i];
            proc->fds[i] = NULL;
            proc->fd_cloexec[i] = false;
            if ((uintptr_t)f >= 0xffff800000000000ULL) {
                fd_release(f);
            }
        }
    }
}

void process_exit(int exit_code) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return;

    /* Claim teardown first so concurrent kills/forks observe the zombie. */
    proc->status = PROCESS_ZOMBIE;
    proc->exit_code = exit_code;
    proc->vfork_done = true; /* Release a vfork parent blocked in sys_vfork. */

    if (g_foreground_proc == proc) {
        g_foreground_proc = NULL;
    }

    /* Reparent all children of exiting process to PID 1 (init) */
    bool notify_init = false;
    struct {
        pid_t pid;
        int sig;
    } pdeath[128];
    int pdeath_count = 0;
    spinlock_acquire(&g_process_lock);
    list_node_t *cpos;
    list_for_each(cpos, &g_process_list) {
        process_t *child = container_of(cpos, process_t, proc_list_node);
        if (child->ppid == proc->pid) {
            /* Capture PR_SET_PDEATHSIG before reparenting: a child thread
             * that asked to be signaled on parent death must be notified. */
            int sig = 0;
            list_node_t *tpos;
            list_for_each(tpos, &child->threads) {
                thread_t *t = container_of(tpos, thread_t, proc_node);
                if (t->state != THREAD_ZOMBIE && t->pdeathsig > 0 && t->pdeathsig < 32) {
                    sig = t->pdeathsig;
                    break;
                }
            }
            if (sig != 0 && pdeath_count < 128) {
                pdeath[pdeath_count].pid = child->pid;
                pdeath[pdeath_count].sig = sig;
                pdeath_count++;
            }
            child->ppid = 1;
            if (child->status == PROCESS_ZOMBIE) {
                notify_init = true;
            }
        }
    }
    spinlock_release(&g_process_lock);
    for (int i = 0; i < pdeath_count; i++) {
        process_t *child = process_get_by_pid(pdeath[i].pid);
        if (child && child->status == PROCESS_ACTIVE)
            process_send_signal(child, pdeath[i].sig);
    }

    if (proc->ppid > 0) {
        process_t *parent = process_get_by_pid(proc->ppid);
        if (parent) {
            uintptr_t handler = parent->signal_handlers[SIGCHLD];
            uint64_t sa_flags = parent->sigactions[SIGCHLD].sa_flags;
            if (handler == SIG_IGN || (sa_flags & SA_NOCLDWAIT)) {
                /* Parent ignores SIGCHLD or requested SA_NOCLDWAIT: reparent to init (PID 1) so it gets reaped immediately */
                proc->ppid = 1;
                notify_init = true;
            } else {
                process_send_signal(parent, SIGCHLD);
                wait_queue_wake_all(&parent->wait_child);
            }
        }
    }
    if (notify_init) {
        process_t *init_proc = process_get_by_pid(1);
        if (init_proc) {
            process_send_signal(init_proc, SIGCHLD);
            wait_queue_wake_all(&init_proc->wait_child);
        }
    }

    list_node_t *pos;
    list_for_each(pos, &proc->threads) {
        if (!pos) break;
        thread_t *t = container_of(pos, thread_t, proc_node);
        /* Retire siblings before releasing user-visible resources: clear
         * robust futexes and TID words while the address space is alive,
         * then drop futex waits and deschedule. */
        futex_remove_thread(t);
        teardown_thread_user(t, proc->pagemap);
        t->state = THREAD_ZOMBIE;
        sched_remove_thread(t);
    }

    /* Release shared memory and file descriptors after threads are retired
     * so pipe/socket peers observe EOF/HUP exactly once. */
    shm_process_exit(proc);
    process_close_all_fds(proc);

    for (;;) {
        sched_yield();
    }
}

int process_send_signal(process_t *proc, int sig) {
    if (!proc || sig < 0 || sig >= 32)
        return -1;
    if (sig == 0)
        return 0; /* Null signal: check process existence */

    spinlock_acquire(&g_process_lock);
    if (proc->status != PROCESS_ACTIVE) {
        spinlock_release(&g_process_lock);
        return -1;
    }

    proc->pending_signals |= (1U << sig);
    signalfd_notify(proc, sig);
    uintptr_t handler = proc->signal_handlers[sig];

    if (handler == SIG_IGN ||
        ((sig == SIGCHLD || sig == SIGWINCH || sig == SIGURG) && handler == SIG_DFL)) {
        proc->pending_signals &= ~(1U << sig);
        spinlock_release(&g_process_lock);
        return 0;
    }

    if ((sig != SIGKILL && sig != SIGSTOP)) {
        /* Masks are per-thread: deliver unless every live thread blocks it.
         * A fully-blocked signal stays pending for sigwait/signalfd. */
        bool all_blocked = true;
        list_node_t *tpos;
        list_for_each(tpos, &proc->threads) {
            thread_t *t = container_of(tpos, thread_t, proc_node);
            if (t->state != THREAD_ZOMBIE && !(t->blocked_signals & (1U << sig))) {
                all_blocked = false;
                break;
            }
        }
        if (all_blocked) {
            spinlock_release(&g_process_lock);
            return 0;
        }
    }

    if (handler == SIG_DFL || (handler > 3 && (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV || sig == SIGILL))) {
        /* Terminating signals */
        if (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV ||
            sig == SIGILL) {
            proc->term_sig = sig;
            if (proc == sched_get_current_process()) {
                spinlock_release(&g_process_lock);
                process_exit(128 + sig);
                return 0;
            }

            proc->status = PROCESS_ZOMBIE;
            proc->exit_code = (128 + sig);
            proc->vfork_done = true;

            if (g_foreground_proc == proc) {
                g_foreground_proc = NULL;
            }

            /* Reparent children to PID 1 */
            bool notify_init = false;
            list_node_t *cpos;
            list_for_each(cpos, &g_process_list) {
                process_t *child = container_of(cpos, process_t, proc_list_node);
                if (child->ppid == proc->pid) {
                    child->ppid = 1;
                    if (child->status == PROCESS_ZOMBIE) {
                        notify_init = true;
                    }
                }
            }

            pid_t ppid = proc->ppid;

            list_node_t *pos;
            list_for_each(pos, &proc->threads) {
                if (!pos) break;
                thread_t *t = container_of(pos, thread_t, proc_node);
                futex_remove_thread(t);
                teardown_thread_user(t, proc->pagemap);
                t->state = THREAD_ZOMBIE;
                sched_remove_thread(t);
            }

            spinlock_release(&g_process_lock);

            /* Immediately detach shared memory and close all open FDs so pipes/sockets/PTYs disconnect.
             * Done after releasing g_process_lock to avoid recursive deadlock when closing PTY master / pipes */
            shm_process_exit(proc);
            process_close_all_fds(proc);

            if (ppid > 0) {
                process_t *parent = process_get_by_pid(ppid);
                if (parent) {
                    process_send_signal(parent, SIGCHLD);
                    wait_queue_wake_all(&parent->wait_child);
                }
            }
            if (notify_init && ppid != 1) {
                process_t *init_proc = process_get_by_pid(1);
                if (init_proc) {
                    process_send_signal(init_proc, SIGCHLD);
                    wait_queue_wake_all(&init_proc->wait_child);
                }
            }

            return 0;
        }
    }

    wait_queue_wake_all(&proc->wait_child);
    spinlock_release(&g_process_lock);
    return 0;
}

int process_setpgid(pid_t pid, pid_t pgid) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    process_t *target = (pid == 0 || pid == curr->pid) ? curr : process_get_by_pid(pid);
    if (!target)
        return -1; /* ESRCH */

    if (pgid < 0)
        return -1; /* EINVAL */
    if (pgid == 0)
        pgid = target->pid;

    spinlock_acquire(&g_process_lock);
    target->pgid = pgid;
    spinlock_release(&g_process_lock);
    return 0;
}

pid_t process_getpgid(pid_t pid) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    process_t *target = (pid == 0 || pid == curr->pid) ? curr : process_get_by_pid(pid);
    if (!target)
        return -1;
    return target->pgid;
}

pid_t process_setsid(void) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    spinlock_acquire(&g_process_lock);
    /* Cannot become session leader if already process group leader */
    if (curr->pgid == curr->pid) {
        spinlock_release(&g_process_lock);
        return -1; /* EPERM */
    }

    curr->sid = curr->pid;
    curr->pgid = curr->pid;
    curr->has_ctty = false;
    curr->ctty = NULL;
    spinlock_release(&g_process_lock);
    return curr->sid;
}

pid_t process_getsid(pid_t pid) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    process_t *target = (pid == 0 || pid == curr->pid) ? curr : process_get_by_pid(pid);
    if (!target)
        return -1;
    return target->sid;
}

int process_setgroups(size_t size, const gid_t *list) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;
    if (curr->euid != 0)
        return -1; /* EPERM */
    if (size > NGROUPS_MAX)
        return -1; /* EINVAL */

    gid_t kgroups[NGROUPS_MAX];
    if (size > 0 && list) {
        if ((uintptr_t)list <= USER_ADDR_MAX) {
            if (!copy_from_user(kgroups, (uintptr_t)list, size * sizeof(gid_t)))
                return -14; /* -EFAULT */
        } else {
            memcpy(kgroups, list, size * sizeof(gid_t));
        }
    }

    spinlock_acquire(&g_process_lock);
    curr->ngroups = (int)size;
    if (size > 0 && list) {
        memcpy(curr->groups, kgroups, size * sizeof(gid_t));
    }
    spinlock_release(&g_process_lock);
    return 0;
}

int process_getgroups(size_t size, gid_t *list) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    spinlock_acquire(&g_process_lock);
    int count = curr->ngroups;
    if (size == 0) {
        spinlock_release(&g_process_lock);
        return count;
    }
    if ((int)size < count) {
        spinlock_release(&g_process_lock);
        return -1; /* EINVAL */
    }
    gid_t kgroups[NGROUPS_MAX];
    memcpy(kgroups, curr->groups, count * sizeof(gid_t));
    spinlock_release(&g_process_lock);

    if (list) {
        if ((uintptr_t)list <= USER_ADDR_MAX) {
            if (!copy_to_user((uintptr_t)list, kgroups, count * sizeof(gid_t)))
                return -14; /* -EFAULT */
        } else {
            memcpy(list, kgroups, count * sizeof(gid_t));
        }
    }
    return count;
}

int process_getpriority(int which, id_t who, int *out_prio) {
    if (!out_prio)
        return -22; /* -EINVAL */

    process_t *curr = sched_get_current_process();
    if (!curr)
        return -3; /* -ESRCH */

    spinlock_acquire(&g_process_lock);

    int best_prio = 20;
    bool found = false;

    if (which == PRIO_PROCESS) {
        pid_t target_pid = (who == 0) ? curr->pid : (pid_t)who;
        list_node_t *pos;
        list_for_each(pos, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->pid == target_pid && p->status != PROCESS_DEAD) {
                best_prio = p->priority;
                found = true;
                break;
            }
        }
    } else if (which == PRIO_PGRP) {
        pid_t target_pgid = (who == 0) ? curr->pgid : (pid_t)who;
        list_node_t *pos;
        list_for_each(pos, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->pgid == target_pgid && p->status != PROCESS_DEAD) {
                if (p->priority < best_prio)
                    best_prio = p->priority;
                found = true;
            }
        }
    } else if (which == PRIO_USER) {
        uid_t target_uid = (who == 0) ? curr->uid : (uid_t)who;
        list_node_t *pos;
        list_for_each(pos, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->uid == target_uid && p->status != PROCESS_DEAD) {
                if (p->priority < best_prio)
                    best_prio = p->priority;
                found = true;
            }
        }
    } else {
        spinlock_release(&g_process_lock);
        return -22; /* -EINVAL */
    }

    spinlock_release(&g_process_lock);

    if (!found)
        return -3; /* -ESRCH */

    *out_prio = best_prio;
    return 0;
}

int process_setpriority(int which, id_t who, int prio) {
    if (prio < -20) prio = -20;
    if (prio > 19) prio = 19;

    process_t *curr = sched_get_current_process();
    if (!curr)
        return -3; /* -ESRCH */

    /* Lowering nice value (raising priority) requires superuser */
    if (prio < 0 && curr->euid != 0)
        return -13; /* -EACCES */

    spinlock_acquire(&g_process_lock);

    bool found = false;
    bool perm_denied = false;

    if (which == PRIO_PROCESS) {
        pid_t target_pid = (who == 0) ? curr->pid : (pid_t)who;
        list_node_t *pos;
        list_for_each(pos, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->pid == target_pid && p->status != PROCESS_DEAD) {
                found = true;
                if (curr->euid == 0 || curr->euid == p->uid || curr->uid == p->uid) {
                    p->priority = prio;
                } else {
                    perm_denied = true;
                }
                break;
            }
        }
    } else if (which == PRIO_PGRP) {
        pid_t target_pgid = (who == 0) ? curr->pgid : (pid_t)who;
        list_node_t *pos;
        list_for_each(pos, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->pgid == target_pgid && p->status != PROCESS_DEAD) {
                found = true;
                if (curr->euid == 0 || curr->euid == p->uid || curr->uid == p->uid) {
                    p->priority = prio;
                } else {
                    perm_denied = true;
                }
            }
        }
    } else if (which == PRIO_USER) {
        uid_t target_uid = (who == 0) ? curr->uid : (uid_t)who;
        list_node_t *pos;
        list_for_each(pos, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->uid == target_uid && p->status != PROCESS_DEAD) {
                found = true;
                if (curr->euid == 0 || curr->euid == p->uid || curr->uid == p->uid) {
                    p->priority = prio;
                } else {
                    perm_denied = true;
                }
            }
        }
    } else {
        spinlock_release(&g_process_lock);
        return -22; /* -EINVAL */
    }

    spinlock_release(&g_process_lock);

    if (!found)
        return -3; /* -ESRCH */
    if (perm_denied)
        return -1; /* -EPERM */

    return 0;
}

int process_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (sig <= 0 || sig >= 32 || sig == SIGKILL || sig == SIGSTOP)
        return -1;
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    struct sigaction kact;
    bool has_kact = false;
    if (act) {
        if ((uintptr_t)act <= 3) {
            memset(&kact, 0, sizeof(kact));
            kact.sa_handler = (uintptr_t)act;
            has_kact = true;
        } else if ((uintptr_t)act <= USER_ADDR_MAX) {
            if (!copy_from_user(&kact, (uintptr_t)act, sizeof(struct sigaction)))
                return -14; /* -EFAULT */
            has_kact = true;
        } else {
            memcpy(&kact, act, sizeof(struct sigaction));
            has_kact = true;
        }
    }

    struct sigaction koldact;
    bool has_oldact = false;

    spinlock_acquire(&g_process_lock);
    if (oldact && (uintptr_t)oldact > 0x1000) {
        memcpy(&koldact, &curr->sigactions[sig], sizeof(struct sigaction));
        koldact.sa_handler = curr->signal_handlers[sig];
        has_oldact = true;
    }
    if (has_kact) {
        if ((uintptr_t)kact.sa_handler <= 3) {
            curr->signal_handlers[sig] = (uintptr_t)kact.sa_handler;
            curr->sigactions[sig].sa_handler = (uintptr_t)kact.sa_handler;
        } else {
            memcpy(&curr->sigactions[sig], &kact, sizeof(struct sigaction));
            curr->signal_handlers[sig] = kact.sa_handler;
        }
    }
    spinlock_release(&g_process_lock);

    if (has_oldact) {
        if ((uintptr_t)oldact <= USER_ADDR_MAX) {
            if (!copy_to_user((uintptr_t)oldact, &koldact, sizeof(struct sigaction)))
                return -14; /* -EFAULT */
        } else {
            memcpy(oldact, &koldact, sizeof(struct sigaction));
        }
    }
    return 0;
}

int process_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    process_t *curr = sched_get_current_process();
    thread_t *self = sched_get_current_thread();
    if (!curr || !self)
        return -1;

    sigset_t kset = 0;
    bool has_set = false;
    if (set) {
        if ((uintptr_t)set <= USER_ADDR_MAX) {
            if (!copy_from_user(&kset, (uintptr_t)set, sizeof(sigset_t)))
                return -14; /* -EFAULT */
        } else {
            kset = *set;
        }
        has_set = true;
    }

    /* POSIX masks are per-thread; fork/exec inherit the calling thread's. */
    sigset_t koldset = 0;
    spinlock_acquire(&g_process_lock);
    koldset = self->blocked_signals;
    if (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK) {
        spinlock_release(&g_process_lock);
        return -22; /* -EINVAL: bad 'how' */
    }
    if (has_set) {
        sigset_t mask = kset & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        if (how == SIG_BLOCK) {
            self->blocked_signals |= (uint32_t)mask;
        } else if (how == SIG_UNBLOCK) {
            self->blocked_signals &= ~(uint32_t)mask;
        } else if (how == SIG_SETMASK) {
            self->blocked_signals = (uint32_t)mask;
        }
    }
    spinlock_release(&g_process_lock);

    if (oldset) {
        if ((uintptr_t)oldset <= USER_ADDR_MAX) {
            if (!copy_to_user((uintptr_t)oldset, &koldset, sizeof(sigset_t)))
                return -14; /* -EFAULT */
        } else {
            *oldset = koldset;
        }
    }
    return 0;
}

int process_sigpending(sigset_t *set) {
    if (!set)
        return -1;
    process_t *curr = sched_get_current_process();
    thread_t *t = sched_get_current_thread();
    if (!curr)
        return -1;

    /* sigpending reports but never consumes: the union of process-directed
     * and calling-thread-private pending signals. */
    sigset_t kpending = 0;
    spinlock_acquire(&g_process_lock);
    kpending = curr->pending_signals;
    if (t)
        kpending |= t->thread_pending;
    spinlock_release(&g_process_lock);

    if ((uintptr_t)set <= USER_ADDR_MAX) {
        if (!copy_to_user((uintptr_t)set, &kpending, sizeof(sigset_t)))
            return -14; /* -EFAULT */
    } else {
        *set = kpending;
    }
    return 0;
}

int process_tkill(tid_t tid, int sig) {
    if (sig < 0 || sig >= 32)
        return -1;
    process_t *curr = sched_get_current_process();
    thread_t *self = sched_get_current_thread();
    if (!curr || !self)
        return -1;
    if (sig == 0) {
        /* Null signal: existence check for the thread. */
        bool found = false;
        list_node_t *pos;
        spinlock_acquire(&g_process_lock);
        list_for_each(pos, &curr->threads) {
            thread_t *t = container_of(pos, thread_t, proc_node);
            if (t->tid == tid) {
                found = true;
                break;
            }
        }
        spinlock_release(&g_process_lock);
        return found ? 0 : -3; /* -ESRCH */
    }

    thread_t *target = NULL;
    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &curr->threads) {
        thread_t *t = container_of(pos, thread_t, proc_node);
        if (t->tid == tid) {
            target = t;
            break;
        }
    }
    if (!target || target->state == THREAD_ZOMBIE) {
        spinlock_release(&g_process_lock);
        return -3; /* -ESRCH */
    }
    target->thread_pending |= (1U << sig);
    uintptr_t handler = curr->signal_handlers[sig];
    bool fatal = (handler == SIG_DFL || sig == SIGKILL) &&
                 (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM ||
                  sig == SIGSEGV || sig == SIGILL);
    spinlock_release(&g_process_lock);

    if (fatal) {
        /* A fatal thread-directed signal terminates the whole thread group. */
        return process_send_signal(curr, sig);
    }
    if (handler == SIG_IGN || ((sig == SIGCHLD || sig == SIGWINCH || sig == SIGURG) && handler == SIG_DFL)) {
        spinlock_acquire(&g_process_lock);
        if (target->state != THREAD_ZOMBIE)
            target->thread_pending &= ~(1U << sig);
        spinlock_release(&g_process_lock);
        return 0;
    }
    wait_queue_wake_all(&curr->wait_child);
    return 0;
}

int process_tgkill(pid_t tgid, tid_t tid, int sig) {
    if (sig < 0 || sig >= 32)
        return -1;
    process_t *target_proc = (tgid == 0) ? sched_get_current_process() : process_get_by_pid(tgid);
    if (!target_proc)
        return -3; /* -ESRCH */
    if (sig == 0) {
        bool found = false;
        spinlock_acquire(&g_process_lock);
        list_node_t *pos;
        list_for_each(pos, &target_proc->threads) {
            thread_t *t = container_of(pos, thread_t, proc_node);
            if (t->tid == tid) {
                found = true;
                break;
            }
        }
        spinlock_release(&g_process_lock);
        return found ? 0 : -3; /* -ESRCH */
    }
    /* Route to the thread; fatal signals terminate its whole group. */
    thread_t *target = NULL;
    spinlock_acquire(&g_process_lock);
    if (target_proc->status == PROCESS_ACTIVE) {
        list_node_t *pos;
        list_for_each(pos, &target_proc->threads) {
            thread_t *t = container_of(pos, thread_t, proc_node);
            if (t->tid == tid) {
                target = t;
                break;
            }
        }
    }
    if (!target || target->state == THREAD_ZOMBIE) {
        spinlock_release(&g_process_lock);
        return -3; /* -ESRCH */
    }
    target->thread_pending |= (1U << sig);
    uintptr_t handler = target_proc->signal_handlers[sig];
    bool fatal = (handler == SIG_DFL || sig == SIGKILL) &&
                 (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM ||
                  sig == SIGSEGV || sig == SIGILL);
    spinlock_release(&g_process_lock);
    if (fatal)
        return process_send_signal(target_proc, sig);
    /* Wake waiters so a thread blocked in waitpid re-evaluates EINTR. */
    wait_queue_wake_all(&target_proc->wait_child);
    return 0;
}

int process_kill(pid_t pid, int sig) {
    if (sig < 0 || sig >= 32)
        return -1;
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    if (pid > 0) {
        process_t *p = process_get_by_pid(pid);
        if (!p)
            return -1; /* ESRCH */
        return process_send_signal(p, sig);
    }

    /* Process Group or Broadcast Killing */
    pid_t targets[128];
    int target_count = 0;

    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &g_process_list) {
        process_t *p = container_of(pos, process_t, proc_list_node);
        if (p->status != PROCESS_ACTIVE)
            continue;

        bool target = false;
        if (pid == 0 && p->pgid == curr->pgid) {
            target = true;
        } else if (pid == -1 && p->pid > 1 && p != curr) {
            target = true;
        } else if (pid < -1 && p->pgid == -pid) {
            target = true;
        }

        if (target && target_count < 128) {
            targets[target_count++] = p->pid;
        }
    }
    spinlock_release(&g_process_lock);

    int sent_count = 0;
    for (int i = 0; i < target_count; i++) {
        process_t *p = process_get_by_pid(targets[i]);
        if (p && p->status == PROCESS_ACTIVE) {
            process_send_signal(p, sig);
            sent_count++;
        }
    }

    if (sent_count == 0 && pid == 0) {
        /* Fall back to current process if no other group members */
        return process_send_signal(curr, sig);
    }

    if (pid == -1) {
        return 0; /* POSIX broadcast kill succeeds even if no other active processes remain */
    }

    return (sent_count > 0) ? 0 : -1;
}

pid_t process_waitpid(pid_t pid, int *status, int options) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    while (1) {
        bool has_children = false;
        spinlock_acquire(&g_process_lock);

        list_node_t *pos, *n;
        list_for_each_safe(pos, n, &g_process_list) {
            process_t *p = container_of(pos, process_t, proc_list_node);
            if (p->ppid == curr->pid) {
                bool pid_match = false;
                if (pid > 0) {
                    pid_match = (p->pid == pid);
                } else if (pid == -1) {
                    pid_match = true;
                } else if (pid == 0) {
                    pid_match = (p->pgid == curr->pgid);
                } else { /* pid < -1 */
                    pid_match = (p->pgid == -pid);
                }

                if (pid_match) {
                    has_children = true;
                    if (p->status == PROCESS_ACTIVE) {
                        if (pid > 0) {
                            g_foreground_proc = p;
                        }
                    } else if (p->status == PROCESS_ZOMBIE) {
                        pid_t found_pid = p->pid;
                        if (status) {
                            if (p->term_sig > 0) {
                                *status = (p->term_sig & 0x7F);
                            } else {
                                *status = ((p->exit_code & 0xFF) << 8);
                            }
                        }

                        if (g_foreground_proc == p) {
                            g_foreground_proc = NULL;
                        }

                        /* Remove child from process list */
                        list_remove(&p->proc_list_node);

                        /* Check if any other zombie children remain; if not, clear SIGCHLD */
                        bool other_zombies = false;
                        list_node_t *chk;
                        list_for_each(chk, &g_process_list) {
                            process_t *other = container_of(chk, process_t, proc_list_node);
                            if (other->ppid == curr->pid && other->status == PROCESS_ZOMBIE) {
                                other_zombies = true;
                                break;
                            }
                        }
                        if (!other_zombies) {
                            curr->pending_signals &= ~(1U << SIGCHLD);
                        }

                        spinlock_release(&g_process_lock);

                        /* Free child resources outside g_process_lock to prevent lock recursion */
                        process_close_all_fds(p);

                        /* Free child threads and their kernel stacks */
                        list_node_t *tpos, *tnext;
                        list_for_each_safe(tpos, tnext, &p->threads) {
                            thread_t *t = container_of(tpos, thread_t, proc_node);
                            list_remove(&t->proc_node);
                            sched_remove_thread(t);
                            while (__atomic_load_n(&t->running_cpu, __ATOMIC_ACQUIRE) != -1) {
                                __builtin_ia32_pause();
                            }
                            if (t->kernel_stack_bottom) {
                                pmm_free_pages(VIRT_TO_PHYS(t->kernel_stack_bottom), KERNEL_STACK_SIZE / PAGE_SIZE);
                            }
                            kfree(t);
                        }

                        if (p->pagemap) {
                            vmm_destroy_address_space(p->pagemap);
                            p->pagemap = NULL;
                        }
                        kfree(p);

                        return found_pid;
                    }
                }
            }
        }

        if (!has_children) {
            curr->pending_signals &= ~(1U << SIGCHLD);
            spinlock_release(&g_process_lock);
            return -10; /* -ECHILD */
        }

        /* Check for pending unblocked interrupting signals (excluding SIGCHLD).
         * Includes the waiting thread's private signals (tkill/tgkill) and
         * filters by its own mask. */
        thread_t *waiter = sched_get_current_thread();
        uint32_t all_pending = curr->pending_signals;
        uint32_t waiter_mask = 0;
        if (waiter) {
            all_pending |= waiter->thread_pending;
            waiter_mask = waiter->blocked_signals;
        }
        uint32_t interrupting = all_pending & ~(waiter_mask | (1U << SIGCHLD));
        if (interrupting != 0) {
            spinlock_release(&g_process_lock);
            return -4; /* -EINTR */
        }

        spinlock_release(&g_process_lock);
        if (options & 1) { /* WNOHANG */
            return 0;
        }

        /* Event-driven blocking wait: deschedules until a child changes state */
        wait_queue_wait(&curr->wait_child);
    }
}

process_t *process_get_by_pid(pid_t pid) {
    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &g_process_list) {
        process_t *p = container_of(pos, process_t, proc_list_node);
        if (p->pid == pid) {
            spinlock_release(&g_process_lock);
            return p;
        }
    }
    spinlock_release(&g_process_lock);
    return NULL;
}

thread_t *process_get_thread_by_tid(process_t *proc, tid_t tid) {
    if (!proc)
        return NULL;
    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &proc->threads) {
        thread_t *t = container_of(pos, thread_t, proc_node);
        if (t->tid == tid) {
            spinlock_release(&g_process_lock);
            return t;
        }
    }
    spinlock_release(&g_process_lock);
    return NULL;
}

size_t process_get_tid_list(process_t *proc, tid_t *buf, size_t max_count) {
    if (!proc || !buf || max_count == 0)
        return 0;
    spinlock_acquire(&g_process_lock);
    size_t count = 0;
    list_node_t *pos;
    list_for_each(pos, &proc->threads) {
        if (count >= max_count)
            break;
        thread_t *t = container_of(pos, thread_t, proc_node);
        if (t->state != THREAD_ZOMBIE) {
            buf[count++] = t->tid;
        }
    }
    spinlock_release(&g_process_lock);
    return count;
}

size_t process_get_list(proc_info_t *buf, size_t max_count) {
    if (!buf || max_count == 0)
        return 0;
    spinlock_acquire(&g_process_lock);
    size_t count = 0;
    list_node_t *pos;
    list_for_each(pos, &g_process_list) {
        if (count >= max_count)
            break;
        process_t *p = container_of(pos, process_t, proc_list_node);
        buf[count].pid = p->pid;
        buf[count].ppid = p->ppid;
        buf[count].uid = p->uid;
        buf[count].gid = p->gid;
        buf[count].state = (int)p->status;
        strncpy(buf[count].name, p->name, sizeof(buf[count].name) - 1);
        buf[count].name[sizeof(buf[count].name) - 1] = '\0';
        count++;
    }
    spinlock_release(&g_process_lock);
    return count;
}
