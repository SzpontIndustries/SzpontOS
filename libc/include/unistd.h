#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#define _PC_LINK_MAX 0
#define _PC_MAX_CANON 1
#define _PC_MAX_INPUT 2
#define _PC_NAME_MAX 3
#define _PC_PATH_MAX 4
#define _PC_PIPE_BUF 5
#define _PC_CHOWN_RESTRICTED 6
#define _PC_NO_TRUNC 7
#define _PC_VDISABLE 8

#define _SC_ARG_MAX 0
#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_NGROUPS_MAX 3
#define _SC_OPEN_MAX 4
#define _SC_STREAM_MAX 5
#define _SC_TZNAME_MAX 6
#define _SC_JOB_CONTROL 7
#define _SC_SAVED_IDS 8
#define _SC_REALTIME_SIGNALS 9
#define _SC_PRIORITY_SCHEDULING 10
#define _SC_TIMERS 11
#define _SC_ASYNCHRONOUS_IO 12
#define _SC_PRIORITIZED_IO 13
#define _SC_SYNCHRONIZED_IO 14
#define _SC_FSYNC 15
#define _SC_MAPPED_FILES 16
#define _SC_MEMLOCK 17
#define _SC_MEMLOCK_RANGE 18
#define _SC_MEMORY_PROTECTION 19
#define _SC_MESSAGE_PASSING 20
#define _SC_SEMAPHORES 21
#define _SC_SHARED_MEMORY_OBJECTS 22
#define _SC_AIO_LISTIO_MAX 23
#define _SC_AIO_MAX 24
#define _SC_AIO_PRIO_DELTA_MAX 25
#define _SC_DELAYTIMER_MAX 26
#define _SC_MQ_OPEN_MAX 27
#define _SC_MQ_PRIO_MAX 28
#define _SC_VERSION 29
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_RTSIG_MAX 31
#define _SC_SEM_NSEMS_MAX 32
#define _SC_SEM_VALUE_MAX 33
#define _SC_SIGQUEUE_MAX 34
#define _SC_TIMER_MAX 35
#define _SC_NPROCESSORS_CONF 83
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PHYS_PAGES 85
#define _SC_AVPHYS_PAGES 86
#define _SC_GETPW_R_SIZE_MAX 70
#define _SC_GETGR_R_SIZE_MAX 71

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
int pipe2(int pipefd[2], int flags);
int isatty(int fd);
char *ttyname(int fd);
int ttyname_r(int fd, char *buf, size_t buflen);
int access(const char *pathname, int mode);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
long fpathconf(int fd, int name);
long pathconf(const char *path, int name);
long sysconf(int name);
int getpagesize(void);

pid_t getpid(void);
pid_t getppid(void);
pid_t gettid(void);
pid_t fork(void);
pid_t vfork(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
int execv(const char *path, char *const argv[]);
int execl(const char *path, const char *arg0, ...);
int execlp(const char *file, const char *arg0, ...);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned long usec);
unsigned int alarm(unsigned int seconds);
int pause(void);
void _exit(int status) __attribute__((noreturn));
int arch_prctl(int code, unsigned long addr);

uid_t getuid(void);
gid_t getgid(void);
uid_t geteuid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t euid);
int setegid(gid_t egid);
char *getlogin(void);
int getlogin_r(char *buf, size_t bufsize);

int chmod(const char *pathname, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int chown(const char *pathname, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int fchownat(int dirfd, const char *pathname, uid_t uid, gid_t gid, int flags);
int mkdir(const char *pathname, mode_t mode);
int mkdirat(int dirfd, const char *pathname, mode_t mode);
int unlink(const char *pathname);
int unlinkat(int dirfd, const char *pathname, int flags);
int rmdir(const char *pathname);
int openat(int dirfd, const char *pathname, int flags, ...);
ssize_t readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);

int brk(void *addr);
void *sbrk(intptr_t increment);

char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);
char *cuserid(char *s);

pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
int setpgrp(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);
pid_t getsid(pid_t pid);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);
int revoke(const char *path);

int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);

int getgroups(int size, gid_t list[]);
int setgroups(size_t size, const gid_t *list);

void sync(void);
int fsync(int fd);

int getentropy(void *buffer, size_t length);
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

extern char **environ;

int64_t syscall(int64_t num, ...);

int eaccess(const char *path, int mode);
void *setmode(const char *mode_str);
mode_t getmode(const void *set, mode_t mode);

#endif /* _UNISTD_H */
