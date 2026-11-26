#include <sched/sched.h>
#include <sched/process.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/pit.h>
#include <arch/x86_64/io.h>
#include <mm/vmm.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>
#include <kernel/smp.h>
#include <net/net.h>
#include <drivers/rtc.h>
#include <fs/timerfd.h>

static list_node_t g_ready_queue = LIST_HEAD_INIT(g_ready_queue);
static list_node_t g_sleeping_queue = LIST_HEAD_INIT(g_sleeping_queue);
static spinlock_t g_sched_lock = SPINLOCK_INIT;
static volatile bool g_sched_started = false;

static thread_t *g_idle_threads[SMP_MAX_CPUS] = {0};
static process_t *g_idle_proc = NULL;

extern void arch_switch_context(uintptr_t *old_rsp, uintptr_t new_rsp, void *old_fpu, const void *new_fpu,
                                volatile int32_t *running_cpu);

/* Publish the new CPU id into the next thread's rseq area (glibc ≥ 2.35).
 * Uses an explicit pagemap lookup because copy_to_user() resolves against
 * the *current* process, while next may belong to another process. The
 * 32-byte-aligned struct never crosses a page boundary for its first 8
 * bytes, so a single page lookup covers cpu_id_start/cpu_id. Failures are
 * silently ignored: the thread will observe a stale id and retry. */
static void rseq_update_cpu(thread_t *next, uint32_t cpu_id) {
    if (!next || next->rseq_ptr == 0 || !next->process || !next->process->pagemap)
        return;
    uintptr_t phys = vmm_user_page_phys(next->process->pagemap, next->rseq_ptr, true);
    if (!phys)
        return;
    volatile uint32_t *u = (volatile uint32_t *)PHYS_TO_VIRT(phys);
    u[0] = cpu_id;
    u[1] = cpu_id;
}

static void idle_thread_func(void) {
    while (1) {
        __asm__ volatile("sti; hlt; cli");
    }
}

void sched_init_cpu(uint32_t cpu_id) {
    if (cpu_id >= SMP_MAX_CPUS)
        return;

    if (!g_idle_proc) {
        g_idle_proc = process_create("idle");
    }

    if (!g_idle_threads[cpu_id]) {
        thread_t *idle_t = thread_create(g_idle_proc, idle_thread_func, false);
        if (!idle_t) {
            panic("sched_init_cpu: Failed to allocate idle thread for CPU #%u", cpu_id);
        }

        uint64_t flags;
        spinlock_acquire_irqsave(&g_sched_lock, &flags);
        list_remove(&idle_t->sched_node); /* Not in normal ready queue */
        idle_t->running_cpu = -1;
        g_idle_threads[cpu_id] = idle_t;

        cpu_t *cpu = smp_get_cpu(cpu_id);
        if (cpu) {
            cpu->idle_thread = idle_t;
            if (!cpu->current_thread) {
                cpu->current_thread = idle_t;
            }
        }
        spinlock_release_irqrestore(&g_sched_lock, flags);
    }
}

void sched_init(void) {
    spinlock_init(&g_sched_lock);
    list_init(&g_ready_queue);
    list_init(&g_sleeping_queue);

    sched_init_cpu(0);

    klog_info("Preemptive Scheduler initialized (Multi-Core Round-Robin)");
}

bool sched_is_started(void) {
    return g_sched_started;
}

static void sched_yield_locked(uint64_t flags);

void sched_add_thread(thread_t *thread) {
    if (!thread)
        return;
    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    if (thread->state != THREAD_ZOMBIE) {
        list_remove(&thread->sched_node);
        thread->state = THREAD_READY;
        list_add_tail(&g_ready_queue, &thread->sched_node);
    }
    spinlock_release_irqrestore(&g_sched_lock, flags);
}

void sched_remove_thread(thread_t *thread) {
    if (!thread)
        return;
    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    thread->state = THREAD_ZOMBIE;
    list_remove(&thread->sched_node);
    spinlock_release_irqrestore(&g_sched_lock, flags);
}

thread_t *sched_get_current_thread(void) {
    cpu_t *cpu = smp_current_cpu();
    if (cpu && cpu->current_thread) {
        return cpu->current_thread;
    }
    return g_idle_threads[0];
}

process_t *sched_get_current_process(void) {
    thread_t *curr = sched_get_current_thread();
    return curr ? curr->process : NULL;
}

static bool sched_handle_zombie_or_exit(thread_t *curr, process_t *proc) {
    if (curr->state == THREAD_ZOMBIE || (proc && proc->status == PROCESS_ZOMBIE)) {
        if (proc) {
            process_exit(proc->exit_code ? proc->exit_code : 128);
        }
        return true;
    }
    return false;
}

void sched_block_current_thread(void) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return;
    process_t *proc = curr->process;
    if (sched_handle_zombie_or_exit(curr, proc))
        return;
    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    if (curr->state == THREAD_ZOMBIE || (proc && proc->status == PROCESS_ZOMBIE)) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        if (proc) {
            process_exit(proc->exit_code ? proc->exit_code : 128);
        }
        return;
    }
    list_remove(&curr->sched_node);
    curr->state = THREAD_BLOCKED;
    sched_yield_locked(flags);
    sched_handle_zombie_or_exit(curr, proc);
}

void sched_block_on_waitq(volatile bool *woken_flag) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return;
    process_t *proc = curr->process;
    if (sched_handle_zombie_or_exit(curr, proc))
        return;
    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    if (curr->state == THREAD_ZOMBIE || (proc && proc->status == PROCESS_ZOMBIE)) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        if (proc) {
            process_exit(proc->exit_code ? proc->exit_code : 128);
        }
        return;
    }
    if (woken_flag && __atomic_load_n(woken_flag, __ATOMIC_ACQUIRE)) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        return;
    }
    list_remove(&curr->sched_node);
    curr->state = THREAD_BLOCKED;
    sched_yield_locked(flags);
    sched_handle_zombie_or_exit(curr, proc);
}

bool sched_block_on_futex(volatile uintptr_t *uaddr_slot, uint64_t wakeup_tick, bool timed) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return false;
    process_t *proc = curr->process;
    if (sched_handle_zombie_or_exit(curr, proc))
        return false;
    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    if (curr->state == THREAD_ZOMBIE || (proc && proc->status == PROCESS_ZOMBIE)) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        if (proc) {
            process_exit(proc->exit_code ? proc->exit_code : 128);
        }
        return false;
    }
    if (!uaddr_slot || __atomic_load_n(uaddr_slot, __ATOMIC_ACQUIRE) == 0) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        return false;
    }
    if (timed && pit_get_ticks() >= wakeup_tick) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        return false;
    }
    list_remove(&curr->sched_node);
    if (timed) {
        curr->state = THREAD_SLEEPING;
        curr->sleep_until_tick = wakeup_tick;
        list_add_tail(&g_sleeping_queue, &curr->sched_node);
    } else {
        curr->state = THREAD_BLOCKED;
    }
    sched_yield_locked(flags);
    sched_handle_zombie_or_exit(curr, proc);
    return true;
}

void sched_unblock_thread(thread_t *thread) {
    if (!thread)
        return;
    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    if (thread->state == THREAD_BLOCKED || thread->state == THREAD_SLEEPING) {
        list_remove(&thread->sched_node);
        thread->state = THREAD_READY;
        list_add_tail(&g_ready_queue, &thread->sched_node);
    }
    spinlock_release_irqrestore(&g_sched_lock, flags);
}

static void sched_yield_locked(uint64_t flags) {
    if (!g_sched_started) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        return;
    }

    cpu_t *cpu = smp_current_cpu();
    uint32_t cpu_id = cpu ? cpu->cpu_id : 0;
    thread_t *idle = g_idle_threads[cpu_id] ? g_idle_threads[cpu_id] : g_idle_threads[0];

    thread_t *prev = cpu ? cpu->current_thread : NULL;
    if (prev && prev != idle && prev->state == THREAD_RUNNING) {
        list_remove(&prev->sched_node);
        prev->state = THREAD_READY;
        list_add_tail(&g_ready_queue, &prev->sched_node);
    }

    /* Wake sleeping threads */
    uint64_t ticks = pit_get_ticks();
    list_node_t *pos, *n;
    list_for_each_safe(pos, n, &g_sleeping_queue) {
        thread_t *t = container_of(pos, thread_t, sched_node);
        if (t->state == THREAD_ZOMBIE || (t->process && t->process->status == PROCESS_ZOMBIE)) {
            list_remove(&t->sched_node);
            t->state = THREAD_ZOMBIE;
            continue;
        }
        if (ticks >= t->sleep_until_tick) {
            list_remove(&t->sched_node);
            t->state = THREAD_READY;
            list_add_tail(&g_ready_queue, &t->sched_node);
        }
    }

    /* Pick next thread from ready queue */
    thread_t *next = NULL;
    list_for_each_safe(pos, n, &g_ready_queue) {
        thread_t *cand = container_of(pos, thread_t, sched_node);
        if (cand->state == THREAD_READY) {
            if (cand->process && cand->process->status == PROCESS_ZOMBIE) {
                list_remove(pos);
                cand->state = THREAD_ZOMBIE;
                continue;
            }
            int32_t rc = __atomic_load_n(&cand->running_cpu, __ATOMIC_ACQUIRE);
            if (rc == -1 || rc == (int32_t)cpu_id) {
                list_remove(pos);
                next = cand;
                break;
            }
        }
    }

    if (!next) {
        next = idle;
    }

    next->state = THREAD_RUNNING;
    next->running_cpu = (int32_t)cpu_id;
    if (cpu) {
        cpu->current_thread = next;
    }

    /* Update TSS kernel stack for next thread */
    gdt_set_cpu_kernel_stack(cpu_id, next->kernel_stack_top);

    /* Update TLS FS_BASE (MSR 0xC0000100) */
    wrmsr(0xC0000100, next->fs_base);

    /* Switch address space if necessary */
    if (next->process && next->process->pagemap) {
        vmm_switch_address_space(next->process->pagemap);
    }

    rseq_update_cpu(next, cpu_id);

    if (prev != next) {
        /* CPU Time Accounting */
        uint64_t now_ns = rtc_get_monotonic_ns();
        if (prev && prev->process) {
            uint64_t started = prev->scheduled_at_ns;
            prev->scheduled_at_ns = 0;
            if (started > 0 && now_ns > started) {
                uint64_t span = now_ns - started;
                __atomic_fetch_add(&prev->process->cpu_time_ns, span, __ATOMIC_RELAXED);
            }
        }
        if (next) {
            next->scheduled_at_ns = now_ns;
        }

        static uintptr_t boot_rsps[SMP_MAX_CPUS] = {0};
        uintptr_t *old_rsp_ptr = prev ? &prev->rsp : &boot_rsps[cpu_id];
        void *old_fpu = prev ? prev->fpu_state : NULL;
        const void *new_fpu = next ? next->fpu_state : NULL;
        volatile int32_t *prev_running_flag = (prev && prev != idle) ? &prev->running_cpu : NULL;

        spinlock_release(&g_sched_lock);
        arch_switch_context(old_rsp_ptr, next->rsp, old_fpu, new_fpu, prev_running_flag);
        spinlock_irqrestore(flags);
    } else {
        spinlock_release_irqrestore(&g_sched_lock, flags);
    }
}

void sched_yield(void) {
    if (!g_sched_started)
        return;

    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    sched_yield_locked(flags);
}

void thread_sleep(uint32_t ms) {
    thread_t *curr = sched_get_current_thread();
    if (!curr)
        return;

    process_t *proc = curr->process;
    if (sched_handle_zombie_or_exit(curr, proc))
        return;

    if (proc && ((proc->pending_signals | curr->thread_pending) & ~curr->blocked_signals)) {
        return;
    }

    if (ms == 0) {
        sched_yield();
        return;
    }

    uint32_t freq = pit_get_frequency();
    uint64_t ticks_to_sleep = ((uint64_t)ms * freq + 999) / 1000;
    if (ticks_to_sleep == 0)
        ticks_to_sleep = 1;

    uint64_t flags;
    spinlock_acquire_irqsave(&g_sched_lock, &flags);
    if (curr->state == THREAD_ZOMBIE || (proc && proc->status == PROCESS_ZOMBIE)) {
        spinlock_release_irqrestore(&g_sched_lock, flags);
        if (proc) {
            process_exit(proc->exit_code ? proc->exit_code : 128);
        }
        return;
    }
    list_remove(&curr->sched_node);
    curr->state = THREAD_SLEEPING;
    curr->sleep_until_tick = pit_get_ticks() + ticks_to_sleep;
    list_add_tail(&g_sleeping_queue, &curr->sched_node);
    sched_yield_locked(flags);

    sched_handle_zombie_or_exit(curr, proc);
}

static volatile uint64_t g_stat_user_ticks = 0;
static volatile uint64_t g_stat_sys_ticks = 0;
static volatile uint64_t g_stat_idle_ticks = 0;

void sched_get_cpu_ticks(uint64_t *user_ticks, uint64_t *sys_ticks, uint64_t *idle_ticks) {
    if (user_ticks) *user_ticks = g_stat_user_ticks;
    if (sys_ticks) *sys_ticks = g_stat_sys_ticks;
    if (idle_ticks) *idle_ticks = g_stat_idle_ticks;
}

void sched_tick(void) {
    cpu_t *cpu = smp_current_cpu();
    uint32_t cid = cpu ? cpu->cpu_id : 0;
    thread_t *curr = cpu ? cpu->current_thread : NULL;
    thread_t *idle = (cid < SMP_MAX_CPUS) ? g_idle_threads[cid] : g_idle_threads[0];

    if (!curr || curr == idle || (curr->process && curr->process == g_idle_proc)) {
        __atomic_fetch_add(&g_stat_idle_ticks, 1, __ATOMIC_RELAXED);
    } else if (curr->process && curr->process->pagemap && curr->process->pagemap != &g_kernel_pagemap) {
        __atomic_fetch_add(&g_stat_user_ticks, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_add(&g_stat_sys_ticks, 1, __ATOMIC_RELAXED);
    }

    if (smp_is_bsp()) {
        netif_poll_all();
        timerfd_tick();
    }
    if (!g_sched_started)
        return;
    sched_yield();
}

void sched_start(void) {
    uint32_t ncpus = smp_get_cpu_count();
    for (uint32_t i = 0; i < ncpus; i++) {
        sched_init_cpu(i);
    }
    g_sched_started = true;
    sched_yield();
}

void sched_ap_start(void) {
    cpu_t *cpu = smp_current_cpu();
    uint32_t cid = cpu ? cpu->cpu_id : 1;
    sched_init_cpu(cid);
    sched_yield();
}
