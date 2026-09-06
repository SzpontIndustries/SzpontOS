#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8

#define WIFEXITED(status) (((status) & 0x7F) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status) (((signed char)(((status) & 0x7F) + 1) >> 1) > 0)
#define WTERMSIG(status) ((status) & 0x7F)
#define WIFSTOPPED(status) (((status) & 0xFF) == 0x7F)
#define WSTOPSIG(status) WEXITSTATUS(status)
#define WCOREDUMP(status) (((status) & 0x80) != 0)
#define WIFCONTINUED(status) (((status) & 0xFFFF) == 0xFFFF)

struct rusage;

pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
pid_t wait3(int *wstatus, int options, struct rusage *rusage);
pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);

#endif /* _SYS_WAIT_H */
