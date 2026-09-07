#ifndef SZPONTOS_KERNEL_SIGNAL_H
#define SZPONTOS_KERNEL_SIGNAL_H

#include <kernel/types.h>

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGWINCH 28

#define NSIG 32

#define SIG_DFL ((uintptr_t)0)
#define SIG_IGN ((uintptr_t)1)
#define SIG_ERR ((uintptr_t)-1)

/* sigprocmask flags */
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* sigaction flags */
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO 0x00000004
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define SA_RESTORER 0x04000000

typedef uint64_t sigset_t;

typedef void (*sighandler_t)(int);

struct sigaction {
    uintptr_t sa_handler;
    uint64_t sa_flags;
    uintptr_t sa_restorer;
    sigset_t sa_mask;
};

#endif /* SZPONTOS_KERNEL_SIGNAL_H */
