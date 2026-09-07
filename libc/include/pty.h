/*
 * SzpontOS - POSIX/BSD pty.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _PTY_H
#define _PTY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <termios.h>
#include <sys/ioctl.h>

int openpty(int *amaster, int *aslave, char *name, const struct termios *termp, const struct winsize *winp);
pid_t forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);
int login_tty(int fd);

#ifdef __cplusplus
}
#endif

#endif /* _PTY_H */
