/*
 * SzpontOS Libc - Signal Name / List Definitions and Conversion Routines
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

const int sys_nsig = 32;

const char *const sys_signame[32] = {
    [0] = "0",
    [SIGHUP] = "HUP",
    [SIGINT] = "INT",
    [SIGQUIT] = "QUIT",
    [SIGILL] = "ILL",
    [SIGTRAP] = "TRAP",
    [SIGABRT] = "ABRT",
    [SIGBUS] = "BUS",
    [SIGFPE] = "FPE",
    [SIGKILL] = "KILL",
    [SIGUSR1] = "USR1",
    [SIGSEGV] = "SEGV",
    [SIGUSR2] = "USR2",
    [SIGPIPE] = "PIPE",
    [SIGALRM] = "ALRM",
    [SIGTERM] = "TERM",
    [SIGCHLD] = "CHLD",
    [SIGCONT] = "CONT",
    [SIGSTOP] = "STOP",
    [SIGTSTP] = "TSTP",
    [SIGTTIN] = "TTIN",
    [SIGTTOU] = "TTOU",
    [SIGURG] = "URG",
    [SIGXCPU] = "XCPU",
    [SIGXFSZ] = "XFSZ",
    [SIGVTALRM] = "VTALRM",
    [SIGPROF] = "PROF",
    [SIGWINCH] = "WINCH",
    [SIGIO] = "IO",
    [SIGPWR] = "PWR",
    [SIGSYS] = "SYS",
};

const char *const sys_siglist[32] = {
    [0] = "Signal 0",
    [SIGHUP] = "Hangup",
    [SIGINT] = "Interrupt",
    [SIGQUIT] = "Quit",
    [SIGILL] = "Illegal instruction",
    [SIGTRAP] = "Trace/BPT trap",
    [SIGABRT] = "Abort trap",
    [SIGBUS] = "Bus error",
    [SIGFPE] = "Floating point exception",
    [SIGKILL] = "Killed",
    [SIGUSR1] = "User defined signal 1",
    [SIGSEGV] = "Segmentation fault",
    [SIGUSR2] = "User defined signal 2",
    [SIGPIPE] = "Broken pipe",
    [SIGALRM] = "Alarm clock",
    [SIGTERM] = "Terminated",
    [SIGCHLD] = "Child exited",
    [SIGCONT] = "Continued",
    [SIGSTOP] = "Suspended (signal)",
    [SIGTSTP] = "Suspended",
    [SIGTTIN] = "Stopped (tty input)",
    [SIGTTOU] = "Stopped (tty output)",
    [SIGURG] = "Urgent I/O condition",
    [SIGXCPU] = "Cputime limit exceeded",
    [SIGXFSZ] = "Filesize limit exceeded",
    [SIGVTALRM] = "Virtual timer expired",
    [SIGPROF] = "Profiling timer expired",
    [SIGWINCH] = "Window size changes",
    [SIGIO] = "I/O possible",
    [SIGPWR] = "Power failure",
    [SIGSYS] = "Bad system call",
};

int sig2str(int signum, char *str) {
    if (!str)
        return -1;
    if (signum >= 0 && signum < 32 && sys_signame[signum]) {
        strcpy(str, sys_signame[signum]);
        return 0;
    }
    return -1;
}

int str2sig(const char *str, int *pnum) {
    if (!str || !pnum)
        return -1;
    if (str[0] >= '0' && str[0] <= '9') {
        int n = atoi(str);
        if (n >= 0 && n < 32) {
            *pnum = n;
            return 0;
        }
        return -1;
    }
    if (strncasecmp(str, "SIG", 3) == 0)
        str += 3;
    for (int i = 0; i < 32; i++) {
        if (sys_signame[i] && strcasecmp(sys_signame[i], str) == 0) {
            *pnum = i;
            return 0;
        }
    }
    return -1;
}
