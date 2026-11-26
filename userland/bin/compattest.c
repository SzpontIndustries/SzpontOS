#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <sys/inotify.h>
#include <sys/eventfd.h>

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif
#ifndef MFD_HUGETLB
#define MFD_HUGETLB 0x0004U
#endif

#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#define PR_SET_DUMPABLE 3
#define PR_GET_DUMPABLE 4
#define PR_GET_SECCOMP 21
#define PR_SET_SECCOMP 22
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 8

#ifndef SYS_clone3
#define SYS_clone3 435
#endif
#ifndef SYS_rseq
#define SYS_rseq 334
#endif
#ifndef SYS_set_robust_list
#define SYS_set_robust_list 273
#endif
#ifndef SYS_get_robust_list
#define SYS_get_robust_list 274
#endif
#ifndef SYS_seccomp
#define SYS_seccomp 317
#endif
#ifndef SYS_sched_getaffinity
#define SYS_sched_getaffinity 204
#endif
#ifndef SYS_prctl
#define SYS_prctl 157
#endif
#ifndef SYS_madvise
#define SYS_madvise 28
#endif
#ifndef SYS_memfd_create
#define SYS_memfd_create 319
#endif
#ifndef SYS_exit
#define SYS_exit 60
#endif
#ifndef SYS_vfork
#define SYS_vfork 58
#endif
#ifndef SYS_tgkill
#define SYS_tgkill 234
#endif
#ifndef SYS_futex
#define SYS_futex 202
#endif
#ifndef SYS_gettid
#define SYS_gettid 186
#endif

#define ENOSYS 38
#define EINVAL 22
#define ESRCH 3
#define EFAULT 14
#define EPERM 1

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_SYSVSEM 0x00040000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID 0x01000000
#define CLONE_NEWNS 0x00020000

struct clone3_args {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
};

/* Raw clone3 with child-entry dispatch (mirrors __clone in syscall.asm).
 * Parent returns child tid, child never returns: it pops fn/arg from the
 * child stack, calls fn(arg), then exits the thread via SYS_exit.
 * Error returns the negative errno as-is. */
__asm__(
    ".text\n"
    ".global clone3_raw_asm\n"
    "clone3_raw_asm:\n"
    "    mov $435, %eax\n"
    "    syscall\n"
    "    test %rax, %rax\n"
    "    jz 1f\n"
    "    ret\n"
    "1:\n"
    "    pop %rdi\n"
    "    pop %rsi\n"
    "    mov %rdi, %rax\n"
    "    mov %rsi, %rdi\n"
    "    call *%rax\n"
    "    mov %rax, %rdi\n"
    "    mov $60, %eax\n"
    "    syscall\n"
    "    hlt\n");

extern long clone3_raw_asm(struct clone3_args *args, size_t size);

struct rseq_test {
    uint32_t cpu_id_start;
    uint32_t cpu_id;
    uint64_t rseq_cs;
    uint32_t flags;
    uint8_t pad[12];
} __attribute__((aligned(32)));

struct robust_head_test {
    void *list;
    long futex_offset;
    void *list_op_pending;
};

static int g_failures = 0;

static void check(int ok, const char *what, int64_t got) {
    if (ok) {
        printf("[COMPATTEST] PASS: %s\n", what);
    } else {
        printf("[COMPATTEST] FAIL: %s (got %lld)\n", what, (long long)got);
        g_failures++;
    }
}

static volatile int g_clone3_child_ran = 0;

static volatile int g_mask_step = 0;
static volatile int g_mask_worker_saw_usr1 = -1;

static void *mask_worker(void *arg) {
    (void)arg;
    int spins = 0;
    while ((g_mask_step != 1) && spins < 500000) {
        if ((spins & 0xFF) == 0)
            sched_yield();
        else
            __asm__ volatile("pause");
        spins++;
    }
    /* Main has blocked SIGUSR1 by now; our mask must be independent. */
    sigset_t w0;
    memset(&w0, 0, sizeof(w0));
    if (sigprocmask(SIG_BLOCK, NULL, &w0) == 0)
        g_mask_worker_saw_usr1 = sigismember(&w0, SIGUSR1) ? 1 : 0;
    else
        g_mask_worker_saw_usr1 = -2;
    sigset_t blk;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR2);
    sigprocmask(SIG_BLOCK, &blk, NULL);
    g_mask_step = 2;
    return NULL;
}

static int clone3_child_fn(void *arg) {
    (void)arg;
    g_clone3_child_ran = 1;
    return 0;
}

static void *churn_worker(void *arg) {
    (void)arg;
    return NULL;
}

static volatile int g_task_hold = 0;

static void *task_hold_worker(void *arg) {
    (void)arg;
    /* Stay alive until main has observed us in task/. */
    while (!g_task_hold)
        __asm__ volatile("pause");
    return NULL;
}

/* Robust-death fixtures: single node owned by the exiting worker. */
static struct {
    void *list;
    long futex_offset;
    void *list_op_pending;
} g_rhead;
static struct {
    void *next;
    int32_t futex;
} g_rnode;

/* Robust-death worker for the raw-clone variant (no pthread/join/heap in
 * the picture): registers the shared head, publishes ownership, returns;
 * the clone3_raw_asm trampoline exits the thread via SYS_exit. */
static int raw_robust_fn(void *arg) {
    (void)arg;
    if (syscall(SYS_set_robust_list, (int64_t)&g_rhead, sizeof(g_rhead)) != 0)
        return 1;
    g_rnode.futex = (int32_t)syscall(SYS_gettid);
    return 0;
}

static volatile int g_tkill_worker_ready = 0;
static volatile int g_tkill_worker_done = 0;
static volatile int g_tkill_worker_saw_sig = 0;

static void *tkill_worker(void *arg) {
    (void)arg;
    g_tkill_worker_ready = 1;
    int spins = 0;
    while ((!g_tkill_worker_done) && spins < 500000) {
        if ((spins & 0xFF) == 0)
            sched_yield();
        else
            __asm__ volatile("pause");
        spins++;
    }
    sigset_t p;
    memset(&p, 0, sizeof(p));
    if (sigpending(&p) == 0 && sigismember(&p, SIGUSR2))
        g_tkill_worker_saw_sig = 1;
    g_tkill_worker_done = 2; /* observed */
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    /* Stream live under slow emulation: block-buffered output otherwise hides
     * completed lines while later spin-waits burn CPU with nothing printed. */
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[COMPATTEST] Starting Linux compatibility syscall tests...\n");

    int64_t r = syscall(999);
    check(r == -ENOSYS, "unknown syscall returns -ENOSYS", r);

    r = syscall(SYS_prctl, PR_SET_NAME, (int64_t)"compat-worker-thread", 0, 0, 0);
    check(r == 0, "prctl(PR_SET_NAME)", r);

    char name[16];
    memset(name, 'X', sizeof(name));
    r = syscall(SYS_prctl, PR_GET_NAME, (int64_t)name, 0, 0, 0);
    check(r == 0 && strcmp(name, "compat-worker-t") == 0, "prctl(PR_GET_NAME) truncates to 15 chars", r);

    r = syscall(SYS_prctl, PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
    check(r == 0, "no_new_privs is initially clear", r);
    r = syscall(SYS_prctl, PR_SET_NO_NEW_PRIVS, 2, 0, 0, 0);
    check(r == -EINVAL, "prctl(PR_SET_NO_NEW_PRIVS, 2) is rejected", r);
    r = syscall(SYS_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    check(r == 0, "prctl(PR_SET_NO_NEW_PRIVS, 1)", r);

    pid_t child = fork();
    if (child == 0) {
        _exit(syscall(SYS_prctl, PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1 ? 0 : 1);
    }
    int status = -1;
    waitpid(child, &status, 0);
    check(child > 0 && status == 0, "no_new_privs is inherited by fork()", status);

    r = syscall(SYS_prctl, 0x7fff, 0, 0, 0, 0);
    check(r == -EINVAL, "unknown prctl option returns -EINVAL", r);

    /* PR_SET/GET_PDEATHSIG */
    r = syscall(SYS_prctl, PR_SET_PDEATHSIG, 0, 0, 0, 0);
    check(r == 0, "prctl(PR_SET_PDEATHSIG, 0) clears", r);
    int pdeath = -1;
    r = syscall(SYS_prctl, PR_GET_PDEATHSIG, (int64_t)&pdeath, 0, 0, 0);
    check(r == 0 && pdeath == 0, "prctl(PR_GET_PDEATHSIG) reads 0", r);
    r = syscall(SYS_prctl, PR_SET_PDEATHSIG, 15, 0, 0, 0);
    check(r == 0, "prctl(PR_SET_PDEATHSIG, SIGTERM)", r);
    pdeath = -1;
    r = syscall(SYS_prctl, PR_GET_PDEATHSIG, (int64_t)&pdeath, 0, 0, 0);
    check(r == 0 && pdeath == 15, "prctl(PR_GET_PDEATHSIG) reads SIGTERM", r);
    r = syscall(SYS_prctl, PR_SET_PDEATHSIG, 65, 0, 0, 0);
    check(r == -EINVAL, "prctl(PR_SET_PDEATHSIG, 65) is rejected", r);
    r = syscall(SYS_prctl, PR_SET_PDEATHSIG, 0, 0, 0, 0);
    check(r == 0, "prctl(PR_SET_PDEATHSIG) restored to 0", r);
    r = syscall(SYS_prctl, PR_SET_DUMPABLE, 1, 0, 0, 0);
    check(r == 0, "prctl(PR_SET_DUMPABLE, 1) accepted", r);
    r = syscall(SYS_prctl, PR_GET_DUMPABLE, 0, 0, 0, 0);
    check(r == 1, "prctl(PR_GET_DUMPABLE) returns 1", r);
    r = syscall(SYS_prctl, PR_SET_SECCOMP, 0, 0, 0, 0);
    check(r == -EINVAL, "prctl(PR_SET_SECCOMP) reports -EINVAL (no seccomp)", r);

    /* seccomp(2): must be EINVAL, never ENOSYS, so Gecko detects absence. */
    r = syscall(SYS_seccomp, 0, 0, 0);
    check(r == -EINVAL, "seccomp returns -EINVAL (unsupported)", r);

    /* robust futex lists */
    {
        struct robust_head_test head;
        memset(&head, 0, sizeof(head));
        r = syscall(SYS_set_robust_list, (int64_t)&head, sizeof(head));
        check(r == 0, "set_robust_list(head, 24)", r);
        uintptr_t got_head = 0;
        size_t got_len = 0;
        r = syscall(SYS_get_robust_list, 0, (int64_t)&got_head, (int64_t)&got_len);
        check(r == 0 && got_head == (uintptr_t)&head && got_len == sizeof(head),
              "get_robust_list returns registration", r);
        r = syscall(SYS_set_robust_list, (int64_t)&head, 1);
        check(r == -EINVAL, "set_robust_list rejects bad len", r);
        r = syscall(SYS_set_robust_list, 0, 0);
        check(r == 0, "set_robust_list(NULL) unregisters", r);
        got_head = 1;
        got_len = 1;
        r = syscall(SYS_get_robust_list, 0, (int64_t)&got_head, (int64_t)&got_len);
        check(r == 0 && got_head == 0 && got_len == 0, "get_robust_list empty after unregister", r);
        r = syscall(SYS_get_robust_list, 999999, (int64_t)&got_head, (int64_t)&got_len);
        check(r == -ESRCH, "get_robust_list rejects unknown pid", r);
    }

    /* rseq(2) */
    {
        static struct rseq_test rs __attribute__((aligned(32)));
        memset(&rs, 0xFF, sizeof(rs));
        r = syscall(SYS_rseq, (int64_t)&rs, sizeof(rs), 0, 0);
        check(r == 0, "rseq(register, len=32)", r);
        check(rs.cpu_id < 64 && rs.cpu_id_start < 64, "rseq publishes cpu_id", (int64_t)rs.cpu_id);
        printf("[COMPATTEST] INFO: rseq cpu_id=%u\n", rs.cpu_id);
        r = syscall(SYS_rseq, (int64_t)&rs, 8, 0, 0);
        check(r == -EINVAL, "rseq rejects short len", r);
        r = syscall(SYS_rseq, (int64_t)&rs, sizeof(rs), 2, 0);
        check(r == -EINVAL, "rseq rejects unknown flags", r);
        r = syscall(SYS_rseq, (int64_t)&rs, sizeof(rs), 1, 0);
        check(r == 0, "rseq(unregister)", r);
        r = syscall(SYS_rseq, (int64_t)&rs, sizeof(rs), 1, 0);
        check(r == 0, "rseq(unregister) is idempotent", r);
    }

    /* clone3(2) validation */
    {
        struct clone3_args bad;
        memset(&bad, 0, sizeof(bad));
        r = syscall(SYS_clone3, (int64_t)&bad, 0);
        check(r == -EINVAL, "clone3 rejects size < 64", r);
        memset(&bad, 0, sizeof(bad));
        bad.flags = (uint64_t)CLONE_NEWNS;
        r = syscall(SYS_clone3, (int64_t)&bad, sizeof(bad));
        check(r == -EINVAL, "clone3 rejects namespaces", r);
        memset(&bad, 0, sizeof(bad));
        bad.pidfd = 1;
        r = syscall(SYS_clone3, (int64_t)&bad, sizeof(bad));
        check(r == -EINVAL, "clone3 rejects pidfd", r);
        memset(&bad, 0, sizeof(bad));
        bad.flags = CLONE_THREAD;
        bad.exit_signal = 17;
        r = syscall(SYS_clone3, (int64_t)&bad, sizeof(bad));
        check(r == -EINVAL, "clone3 rejects CLONE_THREAD with exit_signal", r);
    }

    /* clone3(2) fork-like: new process, same stack discipline as fork(). */
    {
        struct clone3_args fa;
        memset(&fa, 0, sizeof(fa));
        fa.flags = 0;
        fa.exit_signal = 17; /* SIGCHLD */
        pid_t c3 = (pid_t)syscall(SYS_clone3, (int64_t)&fa, sizeof(fa));
        if (c3 == 0) {
            _exit(42);
        }
        int st3 = -1;
        waitpid(c3, &st3, 0);
        /* Kernel encodes status Linux-style (exit_code << 8). */
        check(c3 > 0 && ((st3 >> 8) & 0xFF) == 42, "clone3 fork-like creates a process", (int64_t)st3);
    }

    /* clone3(2) thread-like: shares address space, reports tids. */
    {
        size_t ssize = 64 * 1024;
        void *stack = mmap(NULL, ssize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        check(stack != MAP_FAILED, "clone3 thread stack mmap", (int64_t)(intptr_t)stack);
        if (stack != MAP_FAILED) {
            char *base = (char *)stack;
            char *top = base + ssize - 16;
            top = (char *)((uintptr_t)top & ~15ULL);
            *(void **)top = (void *)clone3_child_fn;
            *(void **)(top + 8) = NULL;
            struct clone3_args ta;
            memset(&ta, 0, sizeof(ta));
            int ptid = 0, ctid = 0;
            ta.flags = (uint64_t)(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
                                  CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID);
            ta.parent_tid = (uint64_t)(uintptr_t)&ptid;
            ta.child_tid = (uint64_t)(uintptr_t)&ctid;
            ta.exit_signal = 0;
            ta.stack = (uint64_t)(uintptr_t)base;
            ta.stack_size = (uint64_t)(top - base);
            g_clone3_child_ran = 0;
            long tid = clone3_raw_asm(&ta, sizeof(ta));
            check(tid > 0, "clone3 thread-like returns tid", (int64_t)tid);
            if (tid > 0) {
                check(ptid == (int)tid, "clone3 writes parent_tid", (int64_t)ptid);
                int spins = 0;
                while ((!g_clone3_child_ran) && spins < 500000) {
                    if ((spins & 0xFF) == 0)
                        sched_yield();
                    else
                        __asm__ volatile("pause");
                    spins++;
                }
                check(g_clone3_child_ran == 1, "clone3 child ran on its stack", g_clone3_child_ran);
            }
            munmap(stack, ssize);
        }
    }

    uint64_t mask = 0;
    r = syscall(SYS_sched_getaffinity, 0, sizeof(mask), (int64_t)&mask);
    check(r == (int64_t)sizeof(mask) && (mask & 1), "sched_getaffinity reports CPU 0", r);
    printf("[COMPATTEST] INFO: affinity mask 0x%llx\n", (unsigned long long)mask);
    r = syscall(SYS_sched_getaffinity, 0, 4, (int64_t)&mask);
    check(r == -EINVAL, "sched_getaffinity rejects a short buffer", r);
    {
        uint64_t big[16];
        memset(big, 0xA5, sizeof(big));
        r = syscall(SYS_sched_getaffinity, 0, sizeof(big), (int64_t)big);
        int tail_zero = 1;
        for (size_t i = 1; i < 16; i++) {
            if (big[i] != 0) {
                tail_zero = 0;
                break;
            }
        }
        check(r == (int64_t)sizeof(mask) && (big[0] & 1) && tail_zero,
              "sched_getaffinity accepts len=128 and zero-fills tail", r);
    }

    /* memfd_create with Firefox IPC flags + seals */
    {
        int fd = memfd_create("mozilla-ipc", MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
        check(fd >= 0, "memfd_create(NOEXEC_SEAL|ALLOW_SEALING)", (int64_t)fd);
        if (fd >= 0) {
            r = fcntl(fd, F_GET_SEALS);
            check(r == 0, "memfd seals initially empty", r);
            r = fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW);
            check(r == 0, "memfd F_ADD_SEALS(SHRINK|GROW)", r);
            r = fcntl(fd, F_GET_SEALS);
            check(r == (F_SEAL_SHRINK | F_SEAL_GROW), "memfd F_GET_SEALS returns mask", r);
            const char *msg = "hello-ipc";
            ssize_t w = write(fd, msg, strlen(msg));
            check(w == (ssize_t)strlen(msg), "memfd write", (int64_t)w);
            off_t off = lseek(fd, 0, SEEK_SET);
            check(off == 0, "memfd lseek(0)", (int64_t)off);
            char buf[32];
            memset(buf, 0, sizeof(buf));
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            check(n == (ssize_t)strlen(msg) && strcmp(buf, msg) == 0, "memfd read back", (int64_t)n);
            close(fd);
        }
        int fd2 = memfd_create("no-seal", MFD_CLOEXEC);
        check(fd2 >= 0, "memfd_create without ALLOW_SEALING", (int64_t)fd2);
        if (fd2 >= 0) {
            r = fcntl(fd2, F_ADD_SEALS, F_SEAL_GROW);
            check(r == -1, "memfd F_ADD_SEALS without ALLOW_SEALING fails", r);
            close(fd2);
        }
        r = syscall(SYS_memfd_create, (int64_t) "bad", 0x8000U);
        check(r == -EINVAL, "memfd_create rejects unknown flags", r);
        r = fcntl(999, F_GET_SEALS);
        check(r == -1, "F_GET_SEALS on bad fd fails", r);
    }

    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(page != MAP_FAILED, "mmap anonymous page", (int64_t)(intptr_t)page);
    if (page != MAP_FAILED) {
        r = syscall(SYS_madvise, (int64_t)page, 4096, MADV_WILLNEED);
        check(r == 0, "madvise(MADV_WILLNEED)", r);
        r = syscall(SYS_madvise, (int64_t)page, 4096, MADV_FREE);
        check(r == 0, "madvise(MADV_FREE)", r);
        memset(page, 0xAB, 4096);
        r = syscall(SYS_madvise, (int64_t)page, 4096, MADV_DONTNEED);
        int all_zero = 1;
        if (r == 0) {
            volatile unsigned char *p = (volatile unsigned char *)page;
            for (size_t i = 0; i < 4096; i++) {
                if (p[i] != 0) {
                    all_zero = 0;
                    break;
                }
            }
        }
        check(r == 0 && all_zero, "madvise(MADV_DONTNEED) zeroes anonymous page", r);
        r = syscall(SYS_madvise, (int64_t)page + 1, 4096, MADV_WILLNEED);
        check(r == -EINVAL, "madvise rejects unaligned addresses", r);
        munmap(page, 4096);
    }

    /* P0: vfork blocks the parent until the child exits. */
    {
        int p[2];
        int pr = pipe(p);
        check(pr == 0, "vfork pipe created", (int64_t)pr);
        if (pr == 0) {
            int fl = fcntl(p[0], F_GETFL);
            fcntl(p[0], F_SETFL, fl | O_NONBLOCK);
            pid_t v = vfork();
            if (v == 0) {
                write(p[1], "x", 1);
                _exit(0);
            }
            char c = 0;
            ssize_t n = read(p[0], &c, 1);
            check(v > 0 && n == 1 && c == 'x', "vfork child ran before parent resumed", (int64_t)n);
            close(p[0]);
            close(p[1]);
            int vst = -1;
            pid_t w = waitpid(v, &vst, 0);
            check(w == v && vst == 0, "vfork child reaped with status 0", (int64_t)vst);
        }
    }

    /* P0: clone publication gate churn (rapid create/join must not hang). */
    {
        int i, ok = 1;
        for (i = 0; i < 20; i++) {
            pthread_t th;
            if (pthread_create(&th, NULL, churn_worker, NULL) != 0) {
                ok = 0;
                break;
            }
            if (pthread_join(th, NULL) != 0) {
                ok = 0;
                break;
            }
        }
        check(ok, "clone churn 20x create/join", (int64_t)ok);
    }

    /* P0: robust mutex death wakes the waiter with OWNER_DIED. */
    {
        memset(&g_rhead, 0, sizeof(g_rhead));
        memset(&g_rnode, 0, sizeof(g_rnode));
        g_rhead.list = &g_rnode;
        /* futex word sits after the next pointer: offset must be exact,
         * otherwise cleanup reads the link field and never matches. */
        g_rhead.futex_offset = (long)((uintptr_t)&g_rnode.futex - (uintptr_t)&g_rnode);
        g_rhead.list_op_pending = NULL;
        g_rnode.next = &g_rhead;
        g_rnode.futex = 0;
        /* Raw clone3 thread (not pthread): sidesteps join/free entirely so a
         * stall here can only come from the kernel robust/futex path. */
        size_t rssize = 64 * 1024;
        void *rstack = mmap(NULL, rssize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        check(rstack != MAP_FAILED, "robust worker stack mmap", (int64_t)(intptr_t)rstack);
        if (rstack != MAP_FAILED) {
            char *base = (char *)rstack;
            char *top = base + rssize - 16;
            top = (char *)((uintptr_t)top & ~15ULL);
            *(void **)top = (void *)raw_robust_fn;
            *(void **)(top + 8) = NULL;
            struct clone3_args ra;
            memset(&ra, 0, sizeof(ra));
            ra.flags = (uint64_t)(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
                                  CLONE_SYSVSEM);
            ra.exit_signal = 0;
            ra.stack = (uint64_t)(uintptr_t)base;
            ra.stack_size = (uint64_t)(top - base);
            g_rnode.futex = 0;
            long rtid = clone3_raw_asm(&ra, sizeof(ra));
            check(rtid > 0, "robust worker created", rtid);
            if (rtid > 0) {
                int spins = 0;
                while ((g_rnode.futex == 0) && spins < 500000) {
                    if ((spins & 0xFF) == 0)
                        sched_yield();
                    else
                        __asm__ volatile("pause");
                    spins++;
                }
                int32_t expect = g_rnode.futex;
                check(expect != 0, "robust worker published futex owner", (int64_t)expect);
                if (expect != 0) {
                    /* Only wait if the worker has not already marked OWNER_DIED
                     * before we sampled g_rnode.futex (otherwise *uaddr == expect
                     * and futex_wait would sleep the full 5s timeout). */
                    if (!(expect & 0x40000000)) {
                        struct timespec to;
                        to.tv_sec = 5;
                        to.tv_nsec = 0;
                        int64_t wr = syscall(SYS_futex, (int64_t)&g_rnode.futex, 0, (int64_t)expect,
                                             (int64_t)&to);
                        printf("[COMPATTEST] INFO: futex_wait returned %lld word 0x%x\n", (long long)wr,
                               (unsigned)g_rnode.futex);
                    }
                    int32_t got = g_rnode.futex;
                    check((got & 0x40000000) && ((got & 0x3FFFFFFF) == (expect & 0x3FFFFFFF)),
                          "robust death marks OWNER_DIED", (int64_t)got);
                }
            }
            munmap(rstack, rssize);
        }
    }

    /* Procfs task/fd/sys trees + O_RDWR anonymous fds. */
    {
        /* /proc/self/task lists at least the calling thread. */
        DIR *td = opendir("/proc/self/task");
        check(td != NULL, "opendir(/proc/self/task)", (int64_t)(intptr_t)td);
        int task_count = 0, self_found = 0;
        long mytid = syscall(SYS_gettid);
        if (td) {
            struct dirent *d;
            while ((d = readdir(td)) != NULL) {
                if (d->d_name[0] >= '0' && d->d_name[0] <= '9') {
                    task_count++;
                    if (atoi(d->d_name) == (int)mytid)
                        self_found = 1;
                }
            }
            closedir(td);
        }
        check(task_count >= 1 && self_found, "task/ contains the calling thread", (int64_t)task_count);

        /* Per-thread status file. */
        char tpath[64];
        snprintf(tpath, sizeof(tpath), "/proc/self/task/%ld/status", mytid);
        FILE *tf = fopen(tpath, "r");
        check(tf != NULL, "open(task/tid/status)", (int64_t)(intptr_t)tf);
        if (tf) {
            char line[128] = {0};
            size_t n = fread(line, 1, sizeof(line) - 1, tf);
            fclose(tf);
            check(n > 0 && strstr(line, "Tgid") != NULL, "task status has Tgid", (int64_t)n);
        }

        /* A second thread shows up in task/ while alive. */
        {
            pthread_t th2;
            g_task_hold = 0;
            int cr2 = pthread_create(&th2, NULL, task_hold_worker, NULL);
            check(cr2 == 0, "task-count worker created", (int64_t)cr2);
            if (cr2 == 0) {
                int spins = 0, two = 0;
                while (spins < 500000 && !two) {
                    DIR *td2 = opendir("/proc/self/task");
                    if (td2) {
                        int c = 0;
                        struct dirent *d;
                        while ((d = readdir(td2)) != NULL) {
                            if (d->d_name[0] >= '0' && d->d_name[0] <= '9')
                                c++;
                        }
                        closedir(td2);
                        if (c >= 2)
                            two = 1;
                    }
                    if ((spins & 0x3FF) == 0)
                        sched_yield();
                    spins++;
                }
                g_task_hold = 1;
                check(two, "task/ lists the second thread", 0);
                pthread_join(th2, NULL);
            }
        }

        /* /proc/self/fd enumerates descriptors; fd readlinks resolve. */
        DIR *fd = opendir("/proc/self/fd");
        check(fd != NULL, "opendir(/proc/self/fd)", (int64_t)(intptr_t)fd);
        int fd_count = 0;
        if (fd) {
            struct dirent *d;
            while ((d = readdir(fd)) != NULL) {
                if (d->d_name[0] >= '0' && d->d_name[0] <= '9')
                    fd_count++;
            }
            closedir(fd);
        }
        check(fd_count >= 3, "fd/ lists stdio descriptors", (int64_t)fd_count);
        {
            char link[128];
            ssize_t ll = readlink("/proc/self/fd/1", link, sizeof(link) - 1);
            check(ll > 0, "readlink(/proc/self/fd/1)", (int64_t)ll);
        }

        /* /proc/sys/kernel parameters. */
        FILE *sf = fopen("/proc/sys/kernel/ostype", "r");
        check(sf != NULL, "open(/proc/sys/kernel/ostype)", (int64_t)(intptr_t)sf);
        if (sf) {
            char obuf[64] = {0};
            size_t n = fread(obuf, 1, sizeof(obuf) - 1, sf);
            fclose(sf);
            check(n > 0 && strncmp(obuf, "SzpontOS", 8) == 0, "ostype reads SzpontOS", (int64_t)n);
        }
        sf = fopen("/proc/sys/kernel/hostname", "r");
        check(sf != NULL, "open(/proc/sys/kernel/hostname)", (int64_t)(intptr_t)sf);
        if (sf)
            fclose(sf);

        /* Anonymous descriptors must be O_RDWR like Linux. */
        {
            int tfd = timerfd_create(0, 0);
            check(tfd >= 0, "timerfd created", (int64_t)tfd);
            if (tfd >= 0) {
                int fl = fcntl(tfd, F_GETFL);
                check((fl & 3) == O_RDWR, "timerfd is O_RDWR", (int64_t)fl);
                close(tfd);
            }
            sigset_t sm;
            sigemptyset(&sm);
            int sfd = signalfd(-1, &sm, 0);
            check(sfd >= 0, "signalfd created", (int64_t)sfd);
            if (sfd >= 0) {
                int fl = fcntl(sfd, F_GETFL);
                check((fl & 3) == O_RDWR, "signalfd is O_RDWR", (int64_t)fl);
                close(sfd);
            }
            int ifd = inotify_init1(0);
            check(ifd >= 0, "inotify created", (int64_t)ifd);
            if (ifd >= 0) {
                int fl = fcntl(ifd, F_GETFL);
                check((fl & 3) == O_RDWR, "inotify is O_RDWR", (int64_t)fl);
                close(ifd);
            }
            int efd = eventfd(0, 0);
            check(efd >= 0, "eventfd created", (int64_t)efd);
            if (efd >= 0) {
                int fl = fcntl(efd, F_GETFL);
                check((fl & 3) == O_RDWR, "eventfd is O_RDWR", (int64_t)fl);
                close(efd);
            }
        }
    }

    /* P0: thread-directed signals stay private; sigpending never consumes.
     * Runs last: pending bits intentionally remain set until process exit. */
    {
        long mytid = syscall(SYS_gettid);
        check(mytid > 0, "gettid works", mytid);
        r = syscall(SYS_tgkill, (int64_t)getpid(), (int64_t)999999, 0);
        check(r == -ESRCH, "tgkill rejects unknown tid", r);
        r = syscall(SYS_tgkill, (int64_t)getpid(), mytid, 0);
        check(r == 0, "tgkill null-signal finds self", r);
        r = syscall(SYS_tkill, mytid, SIGUSR1);
        check(r == 0, "tkill(SIGUSR1) to self", r);
        sigset_t p1, p2;
        memset(&p1, 0, sizeof(p1));
        memset(&p2, 0, sizeof(p2));
        int s1 = sigpending(&p1);
        int s2 = sigpending(&p2);
        check(s1 == 0 && s2 == 0 && sigismember(&p1, SIGUSR1) && sigismember(&p2, SIGUSR1),
              "sigpending reports private bit without consuming", (int64_t)s1);

        g_tkill_worker_ready = 0;
        g_tkill_worker_done = 0;
        g_tkill_worker_saw_sig = 0;
        pthread_t wth;
        int wr = pthread_create(&wth, NULL, tkill_worker, NULL);
        check(wr == 0, "tkill worker created", (int64_t)wr);
        if (wr == 0) {
            int spins = 0;
            while ((!g_tkill_worker_ready) && spins < 500000) {
                if ((spins & 0xFF) == 0)
                    sched_yield();
                else
                    __asm__ volatile("pause");
                spins++;
            }
            int kr = pthread_kill(wth, SIGUSR2);
            check(kr == 0, "pthread_kill(worker, SIGUSR2)", (int64_t)kr);
            g_tkill_worker_done = 1;
            spins = 0;
            while ((g_tkill_worker_done != 2) && spins < 500000) {
                if ((spins & 0xFF) == 0)
                    sched_yield();
                else
                    __asm__ volatile("pause");
                spins++;
            }
            check(g_tkill_worker_saw_sig == 1, "thread-directed signal visible only to target", g_tkill_worker_saw_sig);
            sigset_t pm;
            memset(&pm, 0, sizeof(pm));
            sigpending(&pm);
            check(!sigismember(&pm, SIGUSR2), "sender sees no thread-directed bit", 0);
            pthread_join(wth, NULL);
            int ke = pthread_kill((pthread_t)999999, 0);
            check(ke != 0, "pthread_kill rejects unknown thread", (int64_t)ke);
        }

        /* Masks are per-thread: changes must not leak across threads. */
        g_mask_step = 0;
        g_mask_worker_saw_usr1 = -1;
        pthread_t mth;
        int mr = pthread_create(&mth, NULL, mask_worker, NULL);
        check(mr == 0, "mask worker created", (int64_t)mr);
        if (mr == 0) {
            sigset_t blk1;
            sigemptyset(&blk1);
            sigaddset(&blk1, SIGUSR1);
            int br = sigprocmask(SIG_BLOCK, &blk1, NULL);
            check(br == 0, "main blocks SIGUSR1", (int64_t)br);
            g_mask_step = 1;
            int spins = 0;
            while ((g_mask_step != 2) && spins < 500000) {
                if ((spins & 0xFF) == 0)
                    sched_yield();
                else
                    __asm__ volatile("pause");
                spins++;
            }
            check(g_mask_worker_saw_usr1 == 0, "worker mask independent of main", g_mask_worker_saw_usr1);
            sigset_t mown;
            memset(&mown, 0, sizeof(mown));
            int qr = sigprocmask(SIG_BLOCK, NULL, &mown);
            check(qr == 0 && sigismember(&mown, SIGUSR1) && !sigismember(&mown, SIGUSR2),
                  "main mask has only USR1", (int64_t)qr);
            pthread_join(mth, NULL);
        }
        int badhow = sigprocmask(99, NULL, NULL);
        check(badhow == -1, "sigprocmask rejects bad how", (int64_t)badhow);
    }

    if (g_failures) {
        printf("[COMPATTEST] %d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("[COMPATTEST] All tests passed\n");
    return 0;
}
