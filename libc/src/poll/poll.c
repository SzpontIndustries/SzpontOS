/*
 * SzpontOS Libc - POSIX poll() implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <poll.h>
#include <sys/syscall.h>
#include <errno.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    int64_t ret = __syscall3(SYS_poll, (int64_t)fds, (int64_t)nfds, (int64_t)timeout);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask) {
    int timeout_ms = -1;
    if (tmo_p) {
        timeout_ms = (int)(tmo_p->tv_sec * 1000 + tmo_p->tv_nsec / 1000000);
    }
    (void)sigmask;
    return poll(fds, nfds, timeout_ms);
}
