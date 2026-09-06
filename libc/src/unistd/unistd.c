#include <unistd.h>
#include <sched.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <poll.h>
#include <stdbool.h>
#include <pwd.h>
#include <time.h>
#include <errno.h>

int errno = 0;

static char *g_default_environ[] = {"PATH=/bin:/usr/bin",  "USER=root",     "HOME=/root",
                                    "TERM=xterm-256color", "SHELL=/bin/sh", NULL};

char **environ = g_default_environ;

#include <sys/syscall.h>
#include <sys/event.h>

/* External assembly syscall wrappers */
extern int64_t __syscall0(int64_t num);
extern int64_t __syscall1(int64_t num, int64_t a1);
extern int64_t __syscall2(int64_t num, int64_t a1, int64_t a2);
extern int64_t __syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3);
extern int64_t __syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4);
extern int64_t __syscall5(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5);
extern int64_t __syscall6(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6);

static inline int64_t __check_syscall(int64_t ret) {
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return ret;
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)__check_syscall(__syscall3(SYS_read, fd, (int64_t)buf, count));
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)__check_syscall(__syscall3(SYS_write, fd, (int64_t)buf, count));
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    return (ssize_t)__check_syscall(__syscall4(SYS_pread64, fd, (int64_t)buf, count, offset));
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    return (ssize_t)__check_syscall(__syscall4(SYS_pwrite64, fd, (int64_t)buf, count, offset));
}

int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    return (int)__check_syscall(__syscall3(SYS_open, (int64_t)pathname, flags, (int64_t)mode));
}

int close(int fd) {
    return (int)__check_syscall(__syscall1(SYS_close, fd));
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)__check_syscall(__syscall3(SYS_lseek, fd, offset, whence));
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    return (ssize_t)__check_syscall(__syscall3(SYS_readlink, (int64_t)pathname, (int64_t)buf, (int64_t)bufsiz));
}

int dup(int oldfd) {
    return (int)__check_syscall(__syscall1(SYS_dup, oldfd));
}

int dup2(int oldfd, int newfd) {
    return (int)__check_syscall(__syscall2(SYS_dup2, oldfd, newfd));
}

int pipe(int pipefd[2]) {
    return (int)__check_syscall(__syscall1(SYS_pipe, (int64_t)pipefd));
}

int pipe2(int pipefd[2], int flags) {
    (void)flags;
    return pipe(pipefd);
}

int isatty(int fd) {
    return (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO);
}

char *ttyname(int fd) {
    if (isatty(fd)) {
        return "/dev/tty";
    }
    return NULL;
}

int ttyname_r(int fd, char *buf, size_t buflen) {
    if (!buf || buflen < 9) {
        errno = ERANGE;
        return -1;
    }
    if (isatty(fd)) {
        strcpy(buf, "/dev/tty");
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

int access(const char *pathname, int mode) {
    return (int)__check_syscall(__syscall2(SYS_access, (int64_t)pathname, (int64_t)mode));
}

int truncate(const char *path, off_t length) {
    return (int)__check_syscall(__syscall2(SYS_truncate, (int64_t)path, (int64_t)length));
}

int ftruncate(int fd, off_t length) {
    return (int)__check_syscall(__syscall2(SYS_ftruncate, (int64_t)fd, (int64_t)length));
}

pid_t getpid(void) {
    return (pid_t)__syscall0(SYS_getpid);
}

pid_t getppid(void) {
    return (pid_t)__syscall0(SYS_getppid);
}

pid_t gettid(void) {
    return (pid_t)__syscall0(SYS_gettid);
}

int arch_prctl(int code, unsigned long addr) {
    return (int)__syscall2(SYS_arch_prctl, (int64_t)code, (int64_t)addr);
}

uid_t getuid(void) {
    return (uid_t)__syscall0(SYS_getuid);
}

gid_t getgid(void) {
    return (gid_t)__syscall0(SYS_getgid);
}

uid_t geteuid(void) {
    return (uid_t)__syscall0(SYS_geteuid);
}

gid_t getegid(void) {
    return (gid_t)__syscall0(SYS_getegid);
}

int setuid(uid_t uid) {
    return (int)__syscall1(SYS_setuid, (int64_t)uid);
}

int setgid(gid_t gid) {
    return (int)__syscall1(SYS_setgid, (int64_t)gid);
}

int seteuid(uid_t euid) {
    return (int)__syscall1(SYS_seteuid, (int64_t)euid);
}

int setegid(gid_t egid) {
    return (int)__syscall1(SYS_setegid, (int64_t)egid);
}

int chmod(const char *pathname, mode_t mode) {
    return (int)__check_syscall(__syscall2(SYS_chmod, (int64_t)pathname, (int64_t)mode));
}

int fchmod(int fd, mode_t mode) {
    return (int)__check_syscall(__syscall2(SYS_fchmod, fd, (int64_t)mode));
}

int chown(const char *pathname, uid_t owner, gid_t group) {
    return (int)__check_syscall(__syscall3(SYS_chown, (int64_t)pathname, (int64_t)owner, (int64_t)group));
}

int fchown(int fd, uid_t owner, gid_t group) {
    return (int)__check_syscall(__syscall3(SYS_fchown, fd, (int64_t)owner, (int64_t)group));
}

int mkdir(const char *pathname, mode_t mode) {
    return (int)__check_syscall(__syscall2(SYS_mkdir, (int64_t)pathname, (int64_t)mode));
}

int unlink(const char *pathname) {
    return (int)__check_syscall(__syscall1(SYS_unlink, (int64_t)pathname));
}

pid_t fork(void) {
    return (pid_t)__syscall0(SYS_fork);
}

pid_t vfork(void) {
    return fork();
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    return (int)__syscall3(SYS_execve, (int64_t)pathname, (int64_t)argv, (int64_t)envp);
}

int execvp(const char *file, char *const argv[]) {
    if (!file)
        return -1;
    if (strchr(file, '/')) {
        return execve(file, argv, NULL);
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "/bin/%s", file);
    return execve(buf, argv, NULL);
}

int execv(const char *path, char *const argv[]) {
    return execve(path, argv, NULL);
}

int execl(const char *path, const char *arg0, ...) {
    va_list ap;
    va_start(ap, arg0);
    int argc = 1;
    while (va_arg(ap, const char *)) {
        argc++;
    }
    va_end(ap);

    char **argv = (char **)malloc(sizeof(char *) * (argc + 1));
    if (!argv)
        return -1;
    argv[0] = (char *)arg0;

    va_start(ap, arg0);
    for (int i = 1; i < argc; i++) {
        argv[i] = va_arg(ap, char *);
    }
    argv[argc] = NULL;
    va_end(ap);

    int ret = execve(path, argv, NULL);
    free(argv);
    return ret;
}

int execlp(const char *file, const char *arg0, ...) {
    va_list ap;
    va_start(ap, arg0);
    int argc = 1;
    while (va_arg(ap, const char *)) {
        argc++;
    }
    va_end(ap);

    char **argv = (char **)malloc(sizeof(char *) * (argc + 1));
    if (!argv)
        return -1;
    argv[0] = (char *)arg0;

    va_start(ap, arg0);
    for (int i = 1; i < argc; i++) {
        argv[i] = va_arg(ap, char *);
    }
    argv[argc] = NULL;
    va_end(ap);

    int ret = execvp(file, argv);
    free(argv);
    return ret;
}

long fpathconf(int fd, int name) {
    (void)fd;
    switch (name) {
    case _PC_PIPE_BUF:
        return 4096;
    case _PC_PATH_MAX:
        return 4096;
    case _PC_NAME_MAX:
        return 255;
    default:
        return 4096;
    }
}

long pathconf(const char *path, int name) {
    (void)path;
    return fpathconf(-1, name);
}

int usleep(unsigned long usec) {
    struct timespec req;
    req.tv_sec = (time_t)(usec / 1000000UL);
    req.tv_nsec = (long)((usec % 1000000UL) * 1000UL);
    return nanosleep(&req, NULL);
}

void _exit(int status) {
    __syscall1(SYS_exit_group, status);
    while (1) {
    }
}

int brk(void *addr) {
    return (int)__syscall1(SYS_brk, (int64_t)addr);
}

void *sbrk(intptr_t increment) {
    uintptr_t cur = (uintptr_t)__syscall1(SYS_brk, 0);
    if (increment == 0) {
        return (void *)cur;
    }

    uintptr_t new_brk = cur + increment;
    uintptr_t res = (uintptr_t)__syscall1(SYS_brk, (int64_t)new_brk);

    if (res != new_brk) {
        errno = ENOMEM;
        return (void *)-1;
    }

    return (void *)cur;
}

int stat(const char *pathname, struct stat *statbuf) {
    return (int)__check_syscall(__syscall2(SYS_stat, (int64_t)pathname, (int64_t)statbuf));
}

int lstat(const char *pathname, struct stat *statbuf) {
    return stat(pathname, statbuf);
}

int fstat(int fd, struct stat *statbuf) {
    return (int)__check_syscall(__syscall2(SYS_fstat, fd, (int64_t)statbuf));
}

pid_t wait(int *wstatus) {
    return waitpid(-1, wstatus, 0);
}

pid_t waitpid(pid_t pid, int *wstatus, int options) {
    return (pid_t)__check_syscall(__syscall3(SYS_wait4, pid, (int64_t)wstatus, options));
}

int uname(struct utsname *buf) {
    return (int)__check_syscall(__syscall1(SYS_uname, (int64_t)buf));
}

char *getcwd(char *buf, size_t size) {
    int64_t ret = __syscall2(SYS_getcwd, (int64_t)buf, size);
    if (ret <= 0) {
        errno = (ret < 0) ? (int)-ret : ERANGE;
        return NULL;
    }
    return (char *)ret;
}

int chdir(const char *path) {
    return (int)__check_syscall(__syscall1(SYS_chdir, (int64_t)path));
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req, rem;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = 0;
    rem.tv_sec = 0;
    rem.tv_nsec = 0;
    if (nanosleep(&req, &rem) == 0) {
        return 0;
    }
    return (unsigned int)rem.tv_sec;
}

int kill(pid_t pid, int sig) {
    int64_t ret = __syscall2(SYS_kill, pid, sig);
    if (ret < 0) {
        errno = ESRCH;
        return -1;
    }
    return (int)ret;
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    return (int)__syscall4(SYS_rt_sigaction, signum, (int64_t)act, (int64_t)oldact, sizeof(sigset_t));
}

sighandler_t signal(int signum, sighandler_t handler) {
    struct sigaction act, oldact;
    memset(&act, 0, sizeof(act));
    memset(&oldact, 0, sizeof(oldact));
    act.sa_handler = handler;
    if (sigaction(signum, &act, &oldact) < 0) {
        return SIG_ERR;
    }
    return oldact.sa_handler;
}

int sigemptyset(sigset_t *set) {
    if (!set)
        return -1;
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set)
        return -1;
    *set = ~((sigset_t)0);
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64)
        return -1;
    *set |= ((sigset_t)1 << (signum - 1));
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64)
        return -1;
    *set &= ~((sigset_t)1 << (signum - 1));
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64)
        return -1;
    return (*set & ((sigset_t)1 << (signum - 1))) ? 1 : 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    if (oldset)
        *oldset = 0;
    return 0;
}

int sysinfo(struct sysinfo *info) {
    return (int)__syscall1(SYS_sysinfo, (int64_t)info);
}

int get_nprocs(void) {
    return 1;
}

int get_nprocs_conf(void) {
    return 1;
}

long get_phys_pages(void) {
    struct sysinfo s;
    if (sysinfo(&s) == 0 && s.totalram > 0) {
        return (long)(s.totalram / 4096);
    }
    return 65536;
}

long get_avphys_pages(void) {
    struct sysinfo s;
    if (sysinfo(&s) == 0 && s.freeram > 0) {
        return (long)(s.freeram / 4096);
    }
    return 32768;
}

int getprocs(proc_info_t *buf, size_t max_count) {
    return (int)__syscall2(SYS_getprocs, (int64_t)buf, max_count);
}

int getpagesize(void) {
    return 4096;
}

int statfs(const char *path, struct statfs *buf) {
    return (int)__syscall2(SYS_statfs, (int64_t)path, (int64_t)buf);
}

int statvfs(const char *path, struct statvfs *buf) {
    struct statfs s;
    int ret = statfs(path, &s);
    if (ret != 0)
        return ret;

    buf->f_bsize = s.f_bsize;
    buf->f_frsize = s.f_frsize ? s.f_frsize : s.f_bsize;
    buf->f_blocks = s.f_blocks;
    buf->f_bfree = s.f_bfree;
    buf->f_bavail = s.f_bavail;
    buf->f_files = s.f_files;
    buf->f_ffree = s.f_ffree;
    buf->f_favail = s.f_ffree;
    buf->f_fsid = s.f_fsid[0];
    buf->f_flag = s.f_flags;
    buf->f_namemax = s.f_namelen;
    return 0;
}

int rmdir(const char *pathname) {
    return (int)__check_syscall(__syscall1(SYS_rmdir, (int64_t)pathname));
}

mode_t umask(mode_t mask) {
    return (mode_t)__syscall1(SYS_umask, (int64_t)mask);
}

int futimens(int fd, const struct timespec times[2]) {
    (void)fd;
    (void)times;
    return 0;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > 1024) {
        errno = EINVAL;
        return -1;
    }

    struct pollfd pfds[128];
    if (nfds > 128) nfds = 128;

    int poll_count = 0;
    int fd_map[128];

    for (int fd = 0; fd < nfds; fd++) {
        short ev = 0;
        if (readfds && FD_ISSET(fd, readfds)) ev |= POLLIN;
        if (writefds && FD_ISSET(fd, writefds)) ev |= POLLOUT;
        if (exceptfds && FD_ISSET(fd, exceptfds)) ev |= POLLPRI;

        if (ev != 0) {
            pfds[poll_count].fd = fd;
            pfds[poll_count].events = ev;
            pfds[poll_count].revents = 0;
            fd_map[poll_count] = fd;
            poll_count++;
        }
    }

    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);

    if (poll_count == 0) {
        if (timeout) {
            long ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
            if (ms > 0) usleep((unsigned int)(ms * 1000));
        }
        return 0;
    }

    int timeout_ms = -1;
    if (timeout) {
        timeout_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    }

    int ret = poll(pfds, (nfds_t)poll_count, timeout_ms);
    if (ret <= 0) {
        return ret;
    }

    int ready_count = 0;
    for (int i = 0; i < poll_count; i++) {
        int fd = fd_map[i];
        bool ready = false;
        if (readfds && (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
            FD_SET(fd, readfds);
            ready = true;
        }
        if (writefds && (pfds[i].revents & (POLLOUT | POLLERR))) {
            FD_SET(fd, writefds);
            ready = true;
        }
        if (exceptfds && (pfds[i].revents & POLLPRI)) {
            FD_SET(fd, exceptfds);
            ready = true;
        }
        if (ready) ready_count++;
    }

    return ready_count;
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask) {
    (void)sigmask;
    struct timeval tv;
    if (timeout) {
        tv.tv_sec = timeout->tv_sec;
        tv.tv_usec = timeout->tv_nsec / 1000;
        return select(nfds, readfds, writefds, exceptfds, &tv);
    }
    return select(nfds, readfds, writefds, exceptfds, NULL);
}

int gethostname(char *name, size_t len) {
    if (!name || len == 0) {
        errno = EINVAL;
        return -1;
    }

    int fd = open("/etc/hostname", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (bytes > 0) {
            buf[bytes] = '\0';
            while (bytes > 0 && (buf[bytes - 1] == '\n' || buf[bytes - 1] == '\r' || buf[bytes - 1] == ' ' ||
                                 buf[bytes - 1] == '\t')) {
                buf[bytes - 1] = '\0';
                bytes--;
            }
            if (bytes > 0) {
                if ((size_t)bytes >= len) {
                    errno = ENAMETOOLONG;
                    return -1;
                }
                strncpy(name, buf, len);
                name[len - 1] = '\0';
                return 0;
            }
        }
    }

    const char *host = "szpontos";
    size_t host_len = strlen(host);
    if (host_len >= len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(name, host, len);
    name[len - 1] = '\0';
    return 0;
}

int sethostname(const char *name, size_t len) {
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    if (geteuid() != 0) {
        errno = EPERM;
        return -1;
    }
    int fd = open("/etc/hostname", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    ssize_t written = write(fd, name, len);
    write(fd, "\n", 1);
    close(fd);
    if (written < 0)
        return -1;
    return 0;
}

int fcntl(int fd, int cmd, ...) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    va_list ap;
    va_start(ap, cmd);
    unsigned long arg = va_arg(ap, unsigned long);
    va_end(ap);

    return (int)__syscall3(SYS_fcntl, fd, cmd, arg);
}

int getrlimit(int resource, struct rlimit *rlim) {
    return (int)__syscall2(SYS_getrlimit, (int64_t)resource, (int64_t)rlim);
}

int setrlimit(int resource, const struct rlimit *rlim) {
    return (int)__syscall2(SYS_setrlimit, (int64_t)resource, (int64_t)rlim);
}

int getrusage(int who, struct rusage *usage) {
    return (int)__syscall2(SYS_getrusage, (int64_t)who, (int64_t)usage);
}

char *getlogin(void) {
    char *user = getenv("LOGNAME");
    if (user && *user)
        return user;
    user = getenv("USER");
    if (user && *user)
        return user;

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name && pw->pw_name[0]) {
        return pw->pw_name;
    }
    return "root";
}

int getlogin_r(char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) {
        errno = EINVAL;
        return -1;
    }
    const char *login = getlogin();
    if (!login) {
        errno = ENOENT;
        return -1;
    }
    size_t len = strlen(login);
    if (len >= bufsize) {
        errno = ERANGE;
        return -1;
    }
    strcpy(buf, login);
    return 0;
}

static char g_cuserid_buf[32];
char *cuserid(char *s) {
    char *buf = s ? s : g_cuserid_buf;
    struct passwd *pw = getpwuid(geteuid());
    if (pw && pw->pw_name && pw->pw_name[0]) {
        strncpy(buf, pw->pw_name, 31);
        buf[31] = '\0';
        return buf;
    }
    strncpy(buf, "root", 31);
    buf[31] = '\0';
    return buf;
}

clock_t times(struct tms *buf) {
    return (clock_t)__syscall1(SYS_times, (int64_t)buf);
}

pid_t getpgrp(void) {
    return (pid_t)__syscall0(SYS_getpgrp);
}

pid_t getpgid(pid_t pid) {
    return (pid_t)__syscall1(SYS_getpgid, (int64_t)pid);
}

int setpgrp(void) {
    return (int)__syscall2(SYS_setpgid, 0, 0);
}

int setpgid(pid_t pid, pid_t pgid) {
    return (int)__check_syscall(__syscall2(SYS_setpgid, (int64_t)pid, (int64_t)pgid));
}

pid_t setsid(void) {
    return (pid_t)__check_syscall(__syscall0(SYS_setsid));
}

pid_t getsid(pid_t pid) {
    (void)pid;
    return getpid();
}

pid_t tcgetpgrp(int fd) {
    (void)fd;
    return getpgrp();
}

int tcsetpgrp(int fd, pid_t pgrp) {
    (void)fd;
    return setpgid(0, pgrp);
}

int revoke(const char *path) {
    (void)path;
    return 0;
}

int link(const char *oldpath, const char *newpath) {
    return (int)__check_syscall(__syscall2(SYS_link, (int64_t)oldpath, (int64_t)newpath));
}

int symlink(const char *target, const char *linkpath) {
    return (int)__check_syscall(__syscall2(SYS_symlink, (int64_t)target, (int64_t)linkpath));
}

int getgroups(int size, gid_t list[]) {
    return (int)__check_syscall(__syscall2(SYS_getgroups, (int64_t)size, (int64_t)list));
}

int setgroups(size_t size, const gid_t *list) {
    return (int)__check_syscall(__syscall2(SYS_setgroups, (int64_t)size, (int64_t)list));
}

unsigned int alarm(unsigned int seconds) {
    return (unsigned int)__syscall1(SYS_alarm, (int64_t)seconds);
}

int pause(void) {
    sched_yield();
    errno = EINTR;
    return -1;
}

int sigsuspend(const sigset_t *mask) {
    (void)mask;
    pause();
    errno = EINTR;
    return -1;
}

int sigpending(sigset_t *set) {
    return (int)__check_syscall(__syscall1(SYS_rt_sigpending, (int64_t)set));
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    return (int)__check_syscall(__syscall4(SYS_openat, (int64_t)dirfd, (int64_t)pathname, flags, (int64_t)mode));
}

int faccessat(int dirfd, const char *pathname, int mode, int flags) {
    return (int)__check_syscall(__syscall4(SYS_faccessat, (int64_t)dirfd, (int64_t)pathname, (int64_t)mode, (int64_t)flags));
}

int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    return (int)__check_syscall(__syscall4(SYS_newfstatat, (int64_t)dirfd, (int64_t)pathname, (int64_t)statbuf, (int64_t)flags));
}

int unlinkat(int dirfd, const char *pathname, int flags) {
    return (int)__check_syscall(__syscall3(SYS_unlinkat, (int64_t)dirfd, (int64_t)pathname, (int64_t)flags));
}

int mkdirat(int dirfd, const char *pathname, mode_t mode) {
    return (int)__check_syscall(__syscall3(SYS_mkdirat, (int64_t)dirfd, (int64_t)pathname, (int64_t)mode));
}

ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    return (ssize_t)__check_syscall(__syscall4(SYS_readlinkat, (int64_t)dirfd, (int64_t)pathname, (int64_t)buf, (int64_t)bufsiz));
}

int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags) {
    return (int)__check_syscall(__syscall4(SYS_fchmodat, (int64_t)dirfd, (int64_t)pathname, (int64_t)mode, (int64_t)flags));
}

int fchownat(int dirfd, const char *pathname, uid_t uid, gid_t gid, int flags) {
    return (int)__check_syscall(__syscall5(SYS_fchownat, (int64_t)dirfd, (int64_t)pathname, (int64_t)uid, (int64_t)gid, (int64_t)flags));
}

int kqueue(void) {
    return (int)__syscall0(SYS_kqueue);
}

int kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents,
           const struct timespec *timeout) {
    return (int)__syscall6(SYS_kevent, kq, (int64_t)changelist, nchanges, (int64_t)eventlist, nevents,
                           (int64_t)timeout);
}

int reboot(int cmd) {
    return (int)__syscall4(SYS_reboot, 0xfee1dead, 672274793, cmd, 0);
}

void sync(void) {
    __syscall0(SYS_sync);
}

int fsync(int fd) {
    (void)fd;
    return (int)__syscall0(SYS_sync);
}

int flock(int fd, int operation) {
    (void)fd;
    (void)operation;
    return 0;
}

static char g_syslog_ident[64] = "szpontos";
static int g_syslog_option = 0;
static int g_syslog_facility = 1 << 3;
static int g_syslog_mask = 0xff;

void openlog(const char *ident, int option, int facility) {
    if (ident) {
        strncpy(g_syslog_ident, ident, sizeof(g_syslog_ident) - 1);
        g_syslog_ident[sizeof(g_syslog_ident) - 1] = '\0';
    }
    g_syslog_option = option;
    g_syslog_facility = facility;
}

void vsyslog(int priority, const char *format, va_list ap) {
    if (!(g_syslog_mask & (1 << (priority & 0x7))))
        return;
    char msg[1024];
    vsnprintf(msg, sizeof(msg), format, ap);
    if (g_syslog_option & 0x20) { /* LOG_PERROR */
        fprintf(stderr, "%s: %s\n", g_syslog_ident, msg);
    }
}

void syslog(int priority, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}

void closelog(void) {
}

int setlogmask(int mask) {
    int old = g_syslog_mask;
    if (mask != 0)
        g_syslog_mask = mask;
    return old;
}

int eaccess(const char *path, int mode) {
    return access(path, mode);
}

pid_t wait3(int *wstatus, int options, struct rusage *rusage) {
    (void)rusage;
    return waitpid(-1, wstatus, options);
}

pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage) {
    (void)rusage;
    return waitpid(pid, wstatus, options);
}

void *setmode(const char *mode_str) {
    (void)mode_str;
    return NULL;
}

mode_t getmode(const void *set, mode_t mode) {
    (void)set;
    return mode;
}
