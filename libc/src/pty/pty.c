/*
 * SzpontOS - POSIX & BSD Pseudo-Terminal (PTY) Implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

static char g_ptsname_buf[64];

int posix_openpt(int flags) {
    return open("/dev/ptmx", flags);
}

int grantpt(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int unlockpt(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int ptsname_r(int fd, char *buf, size_t buflen) {
    if (fd < 0 || !buf || buflen == 0) {
        errno = EINVAL;
        return EINVAL;
    }
    int ptn = 0;
    if (ioctl(fd, TIOCGPTN, &ptn) == 0) {
        snprintf(buf, buflen, "/dev/pts%d", ptn);
    } else {
        snprintf(buf, buflen, "/dev/pts0");
    }
    return 0;
}

char *ptsname(int fd) {
    if (ptsname_r(fd, g_ptsname_buf, sizeof(g_ptsname_buf)) != 0) {
        return NULL;
    }
    return g_ptsname_buf;
}

int openpty(int *amaster, int *aslave, char *name, const struct termios *termp, const struct winsize *winp) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return -1;

    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        close(master);
        return -1;
    }

    char *sname = ptsname(master);
    if (!sname) {
        close(master);
        return -1;
    }

    if (name) {
        strcpy(name, sname);
    }

    int slave = open(sname, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        close(master);
        return -1;
    }

    if (termp) {
        tcsetattr(slave, TCSANOW, termp);
    }
    if (winp) {
        ioctl(slave, TIOCSWINSZ, (void *)winp);
    }

    if (amaster)
        *amaster = master;
    if (aslave)
        *aslave = slave;

    return 0;
}

pid_t forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp) {
    int master, slave;
    if (openpty(&master, &slave, name, termp, winp) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        close(slave);
        return -1;
    }

    if (pid == 0) {
        close(master);
        setsid();
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2)
            close(slave);
        return 0;
    }

    close(slave);
    if (amaster)
        *amaster = master;
    return pid;
}

int login_tty(int fd) {
    setsid();
    ioctl(fd, TIOCSCTTY, (char *)NULL);
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2)
        close(fd);
    return 0;
}
