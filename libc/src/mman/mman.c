/*
 * SzpontOS Libc - Memory Management (mmap, munmap, mprotect)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    int64_t ret = __syscall6(SYS_mmap, (int64_t)addr, (int64_t)length, prot, flags, fd, offset);
    if (ret < 0 || (void *)ret == (void *)-1) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)ret;
}

int munmap(void *addr, size_t length) {
    int64_t ret = __syscall2(SYS_munmap, (int64_t)addr, (int64_t)length);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    int64_t ret = __syscall3(SYS_mprotect, (int64_t)addr, (int64_t)len, prot);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

/* SzpontOS never swaps pages out, so locking memory in place is a no-op. */
int mlock(const void *addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

int munlock(const void *addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

int mlockall(int flags) {
    (void)flags;
    return 0;
}

int munlockall(void) {
    return 0;
}
