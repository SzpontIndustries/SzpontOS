/*
 * SzpontOS - BSD daemon(3) implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <paths.h>

#ifndef _PATH_DEVNULL
#define _PATH_DEVNULL "/dev/null"
#endif

int daemon(int nochdir, int noclose) {
    switch (fork()) {
    case -1:
        return -1;
    case 0:
        break;
    default:
        _exit(0);
    }

    if (setsid() == -1)
        return -1;

    if (!nochdir)
        (void)chdir("/");

    if (!noclose) {
        int fd = open(_PATH_DEVNULL, O_RDWR, 0);
        if (fd != -1) {
            (void)dup2(fd, STDIN_FILENO);
            (void)dup2(fd, STDOUT_FILENO);
            (void)dup2(fd, STDERR_FILENO);
            if (fd > 2)
                (void)close(fd);
        }
    }
    return 0;
}
