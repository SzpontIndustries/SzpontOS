/*
 * FreeBSD-compatible sys/event.h (kqueue / kevent)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_EVENT_H
#define _SYS_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define EVFILT_READ (-1)
#define EVFILT_WRITE (-2)
#define EVFILT_AIO (-3)
#define EVFILT_VNODE (-4)
#define EVFILT_PROC (-5)
#define EVFILT_SIGNAL (-6)
#define EVFILT_TIMER (-7)
#define EVFILT_USER (-11)

#define EV_ADD 0x0001
#define EV_DELETE 0x0002
#define EV_ENABLE 0x0004
#define EV_DISABLE 0x0008
#define EV_ONESHOT 0x0010
#define EV_CLEAR 0x0020
#define EV_RECEIPT 0x0040
#define EV_DISPATCH 0x0080

#define EV_ERROR 0x4000
#define EV_EOF 0x8000

struct kevent {
    uintptr_t ident;
    short filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t data;
    void *udata;
};

#define EV_SET(kevp, a, b, c, d, e, f)                                                                                 \
    do {                                                                                                               \
        (kevp)->ident = (uintptr_t)(a);                                                                                \
        (kevp)->filter = (short)(b);                                                                                   \
        (kevp)->flags = (uint16_t)(c);                                                                                 \
        (kevp)->fflags = (uint32_t)(d);                                                                                \
        (kevp)->data = (int64_t)(e);                                                                                   \
        (kevp)->udata = (void *)(f);                                                                                   \
    } while (0)

int kqueue(void);
int kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents,
           const struct timespec *timeout);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_EVENT_H */
