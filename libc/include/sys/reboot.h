/*
 * POSIX / Unix sys/reboot.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#define RB_AUTOBOOT 0x01234567
#define RB_HALT_SYSTEM 0xcdef0123
#define RB_ENABLE_CAD 0x89abcdef
#define RB_DISABLE_CAD 0x00000000
#define RB_POWER_OFF 0x4321fedc
#define RB_SW_SUSPEND 0xd000fce2
#define RB_KEXEC 0x45584543

#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793

int reboot(int cmd);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_REBOOT_H */
