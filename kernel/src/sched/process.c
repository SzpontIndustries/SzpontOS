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
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>
#include <arch/x86_64/gdt.h>

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
    if (!p || p->status != PROCESS_ACTIVE) {
        /* Fall back to current running process if active */
        process_t *curr = sched_get_current_process();
        if (curr && curr->pid > 0 && curr->status == PROCESS_ACTIVE) {
            p = curr;
        } else {
            /* Find latest active user process */
            list_node_t *pos;
            list_for_each(pos, &g_process_list) {
                process_t *item = container_of(pos, process_t, proc_list_node);
                if (item->status == PROCESS_ACTIVE && item->pid > 1) {
                    p = item;
                }
            }
        }
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

    process_t *targets[32];
    size_t count = 0;

    spinlock_acquire(&g_process_lock);
    list_node_t *pos;
    list_for_each(pos, &g_process_list) {
        process_t *item = container_of(pos, process_t, proc_list_node);
        if (item->status == PROCESS_ACTIVE && item->has_ctty && item->ctty == ctty_node) {
            if (count < 32) {
                targets[count++] = item;
            }
        }
    }
    spinlock_release(&g_process_lock);

    for (size_t i = 0; i < count; i++) {
        process_send_signal(targets[i], sig);
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
    proc->blocked_signals = 0;
    for (int i = 0; i < 32; i++) {
        proc->signal_handlers[i] = SIG_DFL;
        memset(&proc->sigactions[i], 0, sizeof(struct sigaction));
    }

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

thread_t *thread_create(process_t *proc, void (*entry_point)(void), bool is_user) {
    UNUSED(is_user);
    if (!proc)
        return NULL;

    spinlock_acquire(&g_process_lock);

    thread_t *t = (thread_t *)kzalloc(sizeof(thread_t));
    t->tid = g_next_tid++;
    t->process = proc;
    t->state = THREAD_READY;
    memcpy(t->fpu_state, g_default_fpu_state, 512);

    /* Allocate 16 KiB kernel stack */
    size_t stack_pages = KERNEL_STACK_SIZE / PAGE_SIZE;
    uintptr_t stack_phys = pmm_alloc_pages(stack_pages);
    if (!stack_phys) {
        klog_error("thread_create: Out of physical memory allocating kernel stack!");
        kfree(t);
        spinlock_release(&g_process_lock);
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

    list_add_tail(&proc->threads, &t->proc_node);
    spinlock_release(&g_process_lock);

    sched_add_thread(t);
    return t;
}

void thread_exit(int exit_code) {
    UNUSED(exit_code);
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return;

    /* Handle CLONE_CHILD_CLEARTID / set_tid_address */
    if (curr->clear_child_tid && curr->process && curr->process->pagemap) {
        if (vmm_virt_to_phys(curr->process->pagemap, curr->clear_child_tid)) {
            *(volatile int *)curr->clear_child_tid = 0;
            futex_wake(curr->clear_child_tid, 1);
        }
        curr->clear_child_tid = 0;
    }

    curr->state = THREAD_ZOMBIE;

    /* If all threads are zombie, exit process */
    process_t *proc = curr->process;
    bool all_dead = true;

    list_node_t *pos;
    list_for_each(pos, &proc->threads) {
        thread_t *t = container_of(pos, thread_t, proc_node);
        if (t->state != THREAD_ZOMBIE) {
            all_dead = false;
            break;
        }
    }

    if (all_dead) {
        process_exit(exit_code);
    }

    sched_yield();
}

void process_close_all_fds(process_t *proc) {
    if (!proc)
        return;
    for (int i = 0; i < MAX_FD; i++) {
        if (proc->fds[i]) {
            file_descriptor_t *f = proc->fds[i];
            proc->fds[i] = NULL;
            proc->fd_cloexec[i] = false;
            fd_release(f);
        }
    }
}

void process_exit(int exit_code) {
    process_t *proc = sched_get_current_process();
    if (!proc)
        return;

    /* Detach all active shared memory mappings */
    shm_process_exit(proc);

    /* Close all open file descriptors so pipe/socket peers receive EOF/HUP immediately */
    process_close_all_fds(proc);

    proc->status = PROCESS_ZOMBIE;
    proc->exit_code = exit_code;

    if (g_foreground_proc == proc) {
        g_foreground_proc = NULL;
    }

    /* Reparent all children of exiting process to PID 1 (init) */
    bool notify_init = false;
    spinlock_acquire(&g_process_lock);
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
    spinlock_release(&g_process_lock);

    if (proc->ppid > 0) {
        process_t *parent = process_get_by_pid(proc->ppid);
        if (parent) {
            process_send_signal(parent, SIGCHLD);
            wait_queue_wake_all(&parent->wait_child);
        }
    }
    if (notify_init && proc->ppid != 1) {
        process_t *init_proc = process_get_by_pid(1);
        if (init_proc) {
            process_send_signal(init_proc, SIGCHLD);
            wait_queue_wake_all(&init_proc->wait_child);
        }
    }

    list_node_t *pos;
    list_for_each(pos, &proc->threads) {
        thread_t *t = container_of(pos, thread_t, proc_node);
        futex_remove_thread(t);
        t->state = THREAD_ZOMBIE;
        sched_remove_thread(t);
    }

    sched_yield();
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
    uintptr_t handler = proc->signal_handlers[sig];

    if (handler == SIG_IGN || (sig == SIGCHLD && handler == SIG_DFL)) {
        proc->pending_signals &= ~(1U << sig);
        spinlock_release(&g_process_lock);
        return 0;
    }

    if (handler == SIG_DFL) {
        /* Terminating signals */
        if (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT || sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV ||
            sig == SIGILL) {
            proc->status = PROCESS_ZOMBIE;
            proc->exit_code = (128 + sig);

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
                thread_t *t = container_of(pos, thread_t, proc_node);
                futex_remove_thread(t);
                t->state = THREAD_ZOMBIE;
            }

            spinlock_release(&g_process_lock);

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

            if (proc == sched_get_current_process()) {
                sched_yield();
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

    spinlock_acquire(&g_process_lock);
    curr->ngroups = (int)size;
    if (size > 0 && list) {
        memcpy(curr->groups, list, size * sizeof(gid_t));
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
    if (list) {
        memcpy(list, curr->groups, count * sizeof(gid_t));
    }
    spinlock_release(&g_process_lock);
    return count;
}

int process_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (sig <= 0 || sig >= 32 || sig == SIGKILL || sig == SIGSTOP)
        return -1;
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    spinlock_acquire(&g_process_lock);
    if (oldact && (uintptr_t)oldact > 0x1000) {
        memcpy(oldact, &curr->sigactions[sig], sizeof(struct sigaction));
        oldact->sa_handler = curr->signal_handlers[sig];
    }
    if (act) {
        if ((uintptr_t)act <= 3) {
            /* Raw sighandler_t value passed (SIG_DFL, SIG_IGN) */
            curr->signal_handlers[sig] = (uintptr_t)act;
            curr->sigactions[sig].sa_handler = (uintptr_t)act;
        } else {
            memcpy(&curr->sigactions[sig], act, sizeof(struct sigaction));
            curr->signal_handlers[sig] = act->sa_handler;
        }
    }
    spinlock_release(&g_process_lock);
    return 0;
}

int process_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    spinlock_acquire(&g_process_lock);
    if (oldset) {
        *oldset = curr->blocked_signals;
    }
    if (set) {
        sigset_t mask = *set & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        if (how == SIG_BLOCK) {
            curr->blocked_signals |= (uint32_t)mask;
        } else if (how == SIG_UNBLOCK) {
            curr->blocked_signals &= ~(uint32_t)mask;
        } else if (how == SIG_SETMASK) {
            curr->blocked_signals = (uint32_t)mask;
        }
    }
    spinlock_release(&g_process_lock);
    return 0;
}

int process_sigpending(sigset_t *set) {
    if (!set)
        return -1;
    process_t *curr = sched_get_current_process();
    if (!curr)
        return -1;

    spinlock_acquire(&g_process_lock);
    *set = curr->pending_signals;
    curr->pending_signals = 0;
    spinlock_release(&g_process_lock);
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
        /* Fall back to foreground process if no group found */
        process_t *fg = process_get_foreground();
        if (fg) {
            return process_send_signal(fg, sig);
        }
        return -1;
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
                        if (status)
                            *status = ((p->exit_code & 0xFF) << 8);

                        if (g_foreground_proc == p) {
                            g_foreground_proc = NULL;
                        }

                        /* Free child resources */
                        process_close_all_fds(p);

                        /* Free child threads and their kernel stacks */
                        list_node_t *tpos, *tnext;
                        list_for_each_safe(tpos, tnext, &p->threads) {
                            thread_t *t = container_of(tpos, thread_t, proc_node);
                            list_remove(&t->proc_node);
                            if (t->kernel_stack_bottom) {
                                pmm_free_pages(VIRT_TO_PHYS(t->kernel_stack_bottom), KERNEL_STACK_SIZE / PAGE_SIZE);
                            }
                            kfree(t);
                        }

                        vmm_destroy_address_space(p->pagemap);
                        list_remove(&p->proc_list_node);
                        kfree(p);

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

        /* Check for pending unblocked interrupting signals (excluding SIGCHLD) */
        uint32_t interrupting = curr->pending_signals & ~(curr->blocked_signals | (1U << SIGCHLD));
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
