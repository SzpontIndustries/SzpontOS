#ifndef SZPONTOS_SCHED_THREAD_H
#define SZPONTOS_SCHED_THREAD_H

#include <kernel/types.h>
#include <kernel/list.h>

typedef int32_t tid_t;
struct process;

typedef enum {
    THREAD_READY = 0,
    THREAD_RUNNING = 1,
    THREAD_BLOCKED = 2,
    THREAD_SLEEPING = 3,
    THREAD_ZOMBIE = 4
} thread_state_t;

typedef struct thread {
    tid_t tid;
    struct process *process;
    thread_state_t state;

    uintptr_t kernel_stack_bottom; /* 16 KiB allocated stack base */
    uintptr_t kernel_stack_top;    /* Top of kernel stack (TSS.rsp0) */
    uintptr_t rsp;                 /* Current saved kernel RSP during switch */
    uintptr_t user_stack;          /* User mode stack pointer */
    uintptr_t user_entry;          /* User mode entry point (RIP) */
    uintptr_t fs_base;             /* Thread-Local Storage (TLS) FS base */
    uintptr_t clear_child_tid;     /* Pointer to clear and wake on thread exit */

    uint64_t sleep_until_tick;
    uint64_t scheduled_at_ns;      /* Monotonic timestamp (ns) when thread started running */
    uintptr_t futex_uaddr;
    struct process *futex_proc;
    int exit_code;

    uint8_t fpu_state[512] __attribute__((aligned(16)));

    list_node_t sched_node;
    list_node_t proc_node;
    list_node_t futex_node;
} thread_t;

thread_t *thread_create(struct process *proc, void (*entry_point)(void), bool is_user);
void thread_exit(int code);
void thread_sleep(uint32_t ms);

#endif /* SZPONTOS_SCHED_THREAD_H */
