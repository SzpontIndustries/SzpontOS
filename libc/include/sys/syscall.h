#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_lstat 6
#define SYS_poll 7
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_mprotect 10
#define SYS_munmap 11
#define SYS_brk 12
#define SYS_rt_sigaction 13
#define SYS_rt_sigprocmask 14
#define SYS_ioctl 16
#define SYS_pread64 17
#define SYS_pwrite64 18
#define SYS_readv 19
#define SYS_writev 20
#define SYS_access 21
#define SYS_pipe 22
#define SYS_select 23
#define SYS_yield 24
#define SYS_shmget 29
#define SYS_shmat 30
#define SYS_shmctl 31
#define SYS_dup 32
#define SYS_dup2 33
#define SYS_pause 34
#define SYS_nanosleep 35
#define SYS_alarm 37
#define SYS_getpid 39
#define SYS_socket 41
#define SYS_connect 42
#define SYS_accept 43
#define SYS_sendto 44
#define SYS_recvfrom 45
#define SYS_sendmsg 46
#define SYS_recvmsg 47
#define SYS_shutdown 48
#define SYS_bind 49
#define SYS_listen 50
#define SYS_getsockname 51
#define SYS_getpeername 52
#define SYS_socketpair 53
#define SYS_setsockopt 54
#define SYS_getsockopt 55
#define SYS_clone 56
#define SYS_fork 57
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_kill 62
#define SYS_uname 63
#define SYS_shmdt 67
#define SYS_fcntl 72
#define SYS_truncate 76
#define SYS_ftruncate 77
#define SYS_getdents 78
#define SYS_getcwd 79
#define SYS_chdir 80
#define SYS_rename 82
#define SYS_mkdir 83
#define SYS_rmdir 84
#define SYS_creat 85
#define SYS_link 86
#define SYS_unlink 87
#define SYS_symlink 88
#define SYS_readlink 89
#define SYS_chmod 90
#define SYS_fchmod 91
#define SYS_chown 92
#define SYS_fchown 93
#define SYS_umask 95
#define SYS_gettimeofday 96
#define SYS_getrlimit 97
#define SYS_getrusage 98
#define SYS_sysinfo 99
#define SYS_times 100
#define SYS_sleep 101
#define SYS_getuid 102
#define SYS_syslog 103
#define SYS_getgid 104
#define SYS_setuid 105
#define SYS_setgid 106
#define SYS_geteuid 107
#define SYS_getegid 108
#define SYS_setpgid 109
#define SYS_getppid 110
#define SYS_getpgrp 111
#define SYS_setsid 112
#define SYS_setreuid 113
#define SYS_setregid 114
#define SYS_getgroups 115
#define SYS_setgroups 116
#define SYS_setresuid 117
#define SYS_getresuid 118
#define SYS_setresgid 119
#define SYS_getresgid 120
#define SYS_getpgid 121
#define SYS_getsid 124
#define SYS_seteuid 125
#define SYS_setegid 126
#define SYS_rt_sigpending 127
#define SYS_statfs 137
#define SYS_fstatfs 138
#define SYS_sysctl 156
#define SYS_arch_prctl 158
#define SYS_setrlimit 160
#define SYS_sync 162
#define SYS_reboot 169
#define SYS_iopl 172
#define SYS_init_module 175
#define SYS_delete_module 176
#define SYS_getprocs 178
#define SYS_gettid 186
#define SYS_tkill 200
#define SYS_time 201
#define SYS_futex 202
#define SYS_set_tid_address 218
#define SYS_clock_settime 227
#define SYS_clock_gettime 228
#define SYS_clock_getres 229
#define SYS_exit_group 231
#define SYS_openat 257
#define SYS_mkdirat 258
#define SYS_fchownat 260
#define SYS_newfstatat 262
#define SYS_unlinkat 263
#define SYS_readlinkat 267
#define SYS_fchmodat 268
#define SYS_faccessat 269
#define SYS_getrandom 318
#define SYS_memfd_create 319
#define SYS_kqueue 362
#define SYS_kevent 363

int64_t syscall(int64_t num, ...);
int64_t __syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6);
int64_t __syscall0(int64_t num);
int64_t __syscall1(int64_t num, int64_t a1);
int64_t __syscall2(int64_t num, int64_t a1, int64_t a2);
int64_t __syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3);
int64_t __syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4);
int64_t __syscall5(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5);
int64_t __syscall6(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSCALL_H */
