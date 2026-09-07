#ifndef _SPAWN_H
#define _SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sched.h>
#include <signal.h>

typedef struct {
    short flags;
    pid_t pgroup;
    sigset_t defdefault;
    sigset_t sigmask;
    struct sched_param schedparam;
    int schedpolicy;
} posix_spawnattr_t;

typedef struct {
    int allocated;
    int used;
    void *actions;
} posix_spawn_file_actions_t;

#define POSIX_SPAWN_RESETIDS 0x01
#define POSIX_SPAWN_SETPGROUP 0x02
#define POSIX_SPAWN_SETSIGDEF 0x04
#define POSIX_SPAWN_SETSIGMASK 0x08
#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER 0x20

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp, char *const argv[], char *const envp[]);

int posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp, char *const argv[], char *const envp[]);

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions, int fd, int newfd);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions, int fd);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *file_actions, int fd, const char *path, int oflag,
                                     mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* _SPAWN_H */
