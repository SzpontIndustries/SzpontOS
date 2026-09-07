#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCLD SIGCHLD
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPOLL SIGIO
#define SIGPWR 30
#define SIGSYS 31
#define SIGUNUSED 31

#define NSIG 32
#define _NSIG 64

typedef void (*sighandler_t)(int);
typedef int sig_atomic_t;
#define SIG_ATOMIC_T sig_atomic_t

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t) - 1)

#define SA_NOCLDSTOP 1
#define SA_NOCLDWAIT 2
#define SA_SIGINFO 4
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define SA_NOMASK SA_NODEFER
#define SA_ONESHOT SA_RESETHAND

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SI_USER 0
#define SI_KERNEL 0x80
#define SI_QUEUE -1
#define SI_TIMER -2
#define SI_MESGQ -3
#define SI_ASYNCIO -4
#define SI_SIGIO -5
#define SI_TKILL -6

typedef uint64_t sigset_t;

typedef struct siginfo {
    int si_signo;
    int si_errno;
    int si_code;
    pid_t si_pid;
    uid_t si_uid;
    void *si_addr;
    int si_status;
    long si_band;
    union {
        int sival_int;
        void *sival_ptr;
    } si_value;
} siginfo_t;

struct sigaction {
    union {
        sighandler_t sa_handler;
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

int kill(pid_t pid, int sig);
int raise(int sig);
sighandler_t signal(int signum, sighandler_t handler);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

typedef sighandler_t sig_t;
#define SIG2STR_MAX 32

extern const int sys_nsig;
extern const char *const sys_signame[];
extern const char *const sys_siglist[];

int sig2str(int signum, char *str);
int str2sig(const char *str, int *pnum);

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_H */
