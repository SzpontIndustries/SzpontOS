/*
 * SzpontOS - POSIX sys/ioctl.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCSBRK 0x5409
#define TCXONC 0x540A
#define TCFLSH 0x540B
#define TIOCSCTTY 0x540E
#define TIOCNOTTY 0x5422
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCLINUX 0x541C
#define TIOCGPTN   0x80045430
#define TIOCSPTLCK 0x40045431
#define TIOCMGET 0x5415
#define TIOCMBIS 0x5416
#define TIOCMBIC 0x5417
#define TIOCMSET 0x5418
#define TIOCSDTR 0x5417
#define TIOCCDTR 0x5418
#define FIONREAD 0x541B

#define TIOCM_LE  0x001
#define TIOCM_DTR 0x002
#define TIOCM_RTS 0x004
#define TIOCM_ST  0x008
#define TIOCM_SR  0x010
#define TIOCM_CTS 0x020
#define TIOCM_CAR 0x040
#define TIOCM_RNG 0x080
#define TIOCM_DSR 0x100
#define TIOCM_CD  TIOCM_CAR
#define TIOCM_RI  TIOCM_RNG

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2

#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U

#define _IOC(dir, type, nr, size) (((dir) << 30) | ((type) << 8) | ((nr) << 0) | ((size) << 16))

#define _IO(type, nr) _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, size) _IOC(_IOC_READ, (type), (nr), sizeof(size))
#define _IOW(type, nr, size) _IOC(_IOC_WRITE, (type), (nr), sizeof(size))
#define _IOWR(type, nr, size) _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(size))

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IOCTL_H */
