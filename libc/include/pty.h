/*
 * SzpontOS - POSIX/BSD pty.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _PTY_H
#define _PTY_H

#include <sys/types.h>
#include <termios.h>
#include <sys/ioctl.h>

int openpty(int *amaster, int *aslave, char *name, const struct termios *termp, const struct winsize *winp);
pid_t forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);
int login_tty(int fd);

#endif /* _PTY_H */
