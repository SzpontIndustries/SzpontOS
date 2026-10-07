#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 8

#define ENOSYS 38
#define EINVAL 22

static int g_failures = 0;

static void check(int ok, const char *what, int64_t got) {
    if (ok) {
        printf("[COMPATTEST] PASS: %s\n", what);
    } else {
        printf("[COMPATTEST] FAIL: %s (got %lld)\n", what, (long long)got);
        g_failures++;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
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

    uint64_t mask = 0;
    r = syscall(SYS_sched_getaffinity, 0, sizeof(mask), (int64_t)&mask);
    check(r == (int64_t)sizeof(mask) && (mask & 1), "sched_getaffinity reports CPU 0", r);
    printf("[COMPATTEST] INFO: affinity mask 0x%llx\n", (unsigned long long)mask);
    r = syscall(SYS_sched_getaffinity, 0, 4, (int64_t)&mask);
    check(r == -EINVAL, "sched_getaffinity rejects a short buffer", r);

    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(page != MAP_FAILED, "mmap anonymous page", (int64_t)(intptr_t)page);
    if (page != MAP_FAILED) {
        r = syscall(SYS_madvise, (int64_t)page, 4096, MADV_WILLNEED);
        check(r == 0, "madvise(MADV_WILLNEED)", r);
        r = syscall(SYS_madvise, (int64_t)page, 4096, MADV_FREE);
        check(r == 0, "madvise(MADV_FREE)", r);
        r = syscall(SYS_madvise, (int64_t)page, 4096, MADV_DONTNEED);
        check(r == -EINVAL, "madvise(MADV_DONTNEED) is not claimed as supported", r);
        r = syscall(SYS_madvise, (int64_t)page + 1, 4096, MADV_WILLNEED);
        check(r == -EINVAL, "madvise rejects unaligned addresses", r);
        munmap(page, 4096);
    }

    if (g_failures) {
        printf("[COMPATTEST] %d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("[COMPATTEST] All tests passed\n");
    return 0;
}
