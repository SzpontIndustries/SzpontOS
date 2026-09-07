#ifndef SZPONTOS_SCHED_PROCESS_H
#define SZPONTOS_SCHED_PROCESS_H

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/signal.h>
#include <mm/vmm.h>
#include <fs/vfs.h>
#include <sched/thread.h>

#define MAX_FD 256
#define NGROUPS_MAX 32

typedef enum { PROCESS_ACTIVE = 0, PROCESS_ZOMBIE = 1, PROCESS_DEAD = 2 } process_status_t;

typedef struct process {
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    pid_t sid;
    bool has_ctty;
    vfs_node_t *ctty;

    uid_t uid;
    gid_t gid;
    uid_t euid;
    gid_t egid;
    uid_t suid;
    gid_t sgid;

    gid_t groups[NGROUPS_MAX];
    int ngroups;

    char name[64];
    process_status_t status;

    pagemap_t *pagemap;

    file_descriptor_t *fds[MAX_FD];
    bool fd_cloexec[MAX_FD];
    char cwd[256];
    mode_t umask;
    uint64_t alarm_ticks;

    uintptr_t brk_start;
    uintptr_t brk_current;
    uintptr_t mmap_current;

    /* Shared Memory Mappings (SYSV SHM / Zero-Copy) */
    struct {
        int shmid;
        uintptr_t vaddr;
        size_t size;
        bool active;
    } shm_mappings[32];

    int exit_code;

    uint32_t pending_signals;
    uint32_t blocked_signals;
    uintptr_t signal_handlers[32];
    struct sigaction sigactions[32];

    list_node_t threads;
    list_node_t proc_list_node;
} process_t;

typedef struct proc_info {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    gid_t gid;
    int state;
    char name[64];
} proc_info_t;

void process_init(void);
process_t *process_create(const char *name);
process_t *process_fork(process_t *parent);
void process_exit(int exit_code);
void process_destroy_unstarted(process_t *proc);
pid_t process_waitpid(pid_t pid, int *status, int options);
process_t *process_get_by_pid(pid_t pid);
int process_send_signal(process_t *proc, int sig);
int process_kill(pid_t pid, int sig);
process_t *process_get_foreground(void);
void process_set_foreground(process_t *proc);
void process_signal_ctty(vfs_node_t *ctty_node, int sig);
void process_check_signals(void);
size_t process_get_list(proc_info_t *buf, size_t max_count);

int process_setpgid(pid_t pid, pid_t pgid);
pid_t process_getpgid(pid_t pid);
pid_t process_setsid(void);
pid_t process_getsid(pid_t pid);

int process_setgroups(size_t size, const gid_t *list);
int process_getgroups(size_t size, gid_t *list);

int process_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int process_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int process_sigpending(sigset_t *set);
void process_close_all_fds(process_t *proc);

#endif /* SZPONTOS_SCHED_PROCESS_H */
