/*
 * SzpontOS - Full-featured Init Process (PID 1) & Session Supervisor
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Inspired by 4.4BSD & FreeBSD init(8), NOTES, and pathnames.h.
 * Features:
 *   - Finite State Machine (Single-User, Runcom, Read-TTYs, Multi-User, Clean-TTYs, Catatonia, Death)
 *   - POSIX Zombie & Orphan Reaper (adopts and collects all orphaned background processes)
 *   - Session Supervisor with Anti-Thrashing Hysteresis
 *   - Dynamic Terminal Configuration via /etc/ttys
 *   - BSD RC Subsystem Orchestration (/etc/rc, /etc/rc.shutdown)
 *   - Signal Handling (SIGHUP, SIGTERM, SIGINT, SIGUSR1, SIGUSR2, SIGTSTP, SIGCHLD)
 *   - Client Mode (telinit / init [0|1|6|q|c])
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/reboot.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_WHITE   "\033[1;37m"

#define PATH_CONSOLE  "/dev/console"
#define PATH_TTYS     "/etc/ttys"
#define PATH_RC       "/etc/rc"
#define PATH_SHUTDOWN "/etc/rc.shutdown"
#define PATH_BSHELL   "/bin/sh"

/* Anti-thrashing timing constants (from FreeBSD init) */
#define GETTY_SPACING 5  /* Minimum seconds a session must run before exit */
#define GETTY_NSPACE  4  /* Max rapid crashes allowed before throttling */
#define GETTY_SLEEP   15 /* Seconds to sleep when throttling */

#define SE_OFF        0x0
#define SE_ON         0x1
#define SE_IFEXISTS   0x2
#define SE_SECURE     0x4

typedef struct session {
    char device[64];
    char command[128];
    char type[32];
    int flags;
    pid_t pid;
    time_t started;
    int respawn_count;
    struct session *next;
} session_t;

typedef enum {
    STATE_SINGLE_USER,
    STATE_RUNCOM,
    STATE_READ_TTYS,
    STATE_MULTI_USER,
    STATE_CLEAN_TTYS,
    STATE_CATATONIA,
    STATE_DEATH
} init_state_t;

static session_t *g_sessions = NULL;
static volatile sig_atomic_t g_requested_signal = 0;
static unsigned int g_reboot_cmd = RB_AUTOBOOT;
static bool g_fastboot = false;

static void sig_handler(int sig) {
    g_requested_signal = sig;
}

static void free_sessions(void) {
    session_t *curr = g_sessions;
    while (curr) {
        session_t *next = curr->next;
        free(curr);
        curr = next;
    }
    g_sessions = NULL;
}

static session_t *find_session_by_pid(pid_t pid) {
    for (session_t *s = g_sessions; s != NULL; s = s->next) {
        if (s->pid == pid) {
            return s;
        }
    }
    return NULL;
}

static pid_t start_session(session_t *sp) {
    char devpath[128];
    if (sp->device[0] == '/') {
        strncpy(devpath, sp->device, sizeof(devpath) - 1);
    } else {
        snprintf(devpath, sizeof(devpath), "/dev/%s", sp->device);
    }

    if (sp->flags & SE_IFEXISTS) {
        struct stat st;
        if (stat(devpath, &st) != 0) {
            return 0; /* Device does not exist; skip */
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf(COLOR_RED "[INIT] fork() failed for %s: %s" COLOR_RESET "\n", sp->device, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* In child process */
        setsid();

        int fd = open(devpath, O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) {
                close(fd);
            }
        }

        /* Reset signal handlers */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        char term_env[64];
        snprintf(term_env, sizeof(term_env), "TERM=%s", sp->type[0] ? sp->type : "xterm-256color");

        char *envp[] = {
            "PATH=/bin:/usr/bin",
            "USER=root",
            "HOME=/root",
            "SHELL=/bin/sh",
            "ENV=/etc/shrc",
            term_env,
            NULL
        };

        /* Parse command into argv array */
        char cmd_copy[128];
        strncpy(cmd_copy, sp->command, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';

        char *argv[16];
        int argc = 0;
        char *token = strtok(cmd_copy, " \t");
        while (token && argc < 15) {
            argv[argc++] = token;
            token = strtok(NULL, " \t");
        }
        argv[argc] = NULL;

        char exec_bin[128];
        if (argc == 0) {
            strncpy(exec_bin, PATH_BSHELL, sizeof(exec_bin) - 1);
            argv[0] = "-sh";
            argv[1] = NULL;
        } else {
            strncpy(exec_bin, argv[0], sizeof(exec_bin) - 1);
            if (strcmp(argv[0], PATH_BSHELL) == 0 || strcmp(argv[0], "/bin/sh") == 0) {
                argv[0] = "-sh";
            }
        }
        exec_bin[sizeof(exec_bin) - 1] = '\0';

        execve(exec_bin, argv, envp);

        /* Fallback if execve failed */
        char *fallback_argv[] = {PATH_BSHELL, NULL};
        execve(fallback_argv[0], fallback_argv, envp);

        _exit(127);
    }

    sp->pid = pid;
    sp->started = time(NULL);
    printf(COLOR_GREEN "[INIT] Started session '%s' on %s (PID %d)" COLOR_RESET "\n", sp->command, sp->device, pid);
    return pid;
}

static void parse_ttys(void) {
    FILE *f = fopen(PATH_TTYS, "r");
    if (!f) {
        printf(COLOR_YELLOW "[INIT] Warning: %s not found. Using default console configuration." COLOR_RESET "\n", PATH_TTYS);
        session_t *s1 = (session_t *)calloc(1, sizeof(session_t));
        strcpy(s1->device, "console");
        strcpy(s1->command, PATH_BSHELL);
        strcpy(s1->type, "xterm-256color");
        s1->flags = SE_ON | SE_SECURE;
        s1->next = g_sessions;
        g_sessions = s1;

        session_t *s2 = (session_t *)calloc(1, sizeof(session_t));
        strcpy(s2->device, "serial");
        strcpy(s2->command, PATH_BSHELL);
        strcpy(s2->type, "vt100");
        s2->flags = SE_ON | SE_IFEXISTS | SE_SECURE;
        s2->next = g_sessions;
        g_sessions = s2;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') {
            continue;
        }

        char dev[64] = {0};
        char cmd[128] = {0};
        char type[32] = {0};
        char status[32] = {0};

        /* Read device */
        int n = 0;
        while (*p && *p != ' ' && *p != '\t' && n < 63) {
            dev[n++] = *p++;
        }
        dev[n] = '\0';
        while (*p == ' ' || *p == '\t') p++;

        /* Read command (possibly in quotes) */
        n = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && n < 127) {
                cmd[n++] = *p++;
            }
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && n < 127) {
                cmd[n++] = *p++;
            }
        }
        cmd[n] = '\0';
        while (*p == ' ' || *p == '\t') p++;

        /* Read type */
        n = 0;
        while (*p && *p != ' ' && *p != '\t' && n < 31) {
            type[n++] = *p++;
        }
        type[n] = '\0';
        while (*p == ' ' || *p == '\t') p++;

        /* Read status */
        n = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && n < 31) {
            status[n++] = *p++;
        }
        status[n] = '\0';

        if (dev[0] == '\0' || cmd[0] == '\0') {
            continue;
        }

        int flags = SE_OFF;
        if (strcmp(status, "on") == 0) {
            flags |= SE_ON;
        } else if (strcmp(status, "onifexists") == 0) {
            flags |= (SE_ON | SE_IFEXISTS);
        } else if (strcmp(status, "onifconsole") == 0) {
            flags |= (SE_ON | SE_IFEXISTS);
        }

        /* Check for 'secure' keyword in rest of line */
        if (strstr(p, "secure") != NULL) {
            flags |= SE_SECURE;
        }

        session_t *s = (session_t *)calloc(1, sizeof(session_t));
        strncpy(s->device, dev, sizeof(s->device) - 1);
        strncpy(s->command, cmd, sizeof(s->command) - 1);
        strncpy(s->type, type[0] ? type : "xterm-256color", sizeof(s->type) - 1);
        s->flags = flags;
        s->next = g_sessions;
        g_sessions = s;
    }
    fclose(f);
}

/* ========================================================================= */
/* State Machine Implementations                                             */
/* ========================================================================= */

static init_state_t state_single_user(void) {
    printf("\n" COLOR_YELLOW "========================================================" COLOR_RESET "\n");
    printf("  " COLOR_BOLD COLOR_RED "[INIT] ENTERING SINGLE-USER MAINTENANCE MODE" COLOR_RESET "\n");
    printf("  " COLOR_WHITE "Type 'exit' to boot into multi-user mode." COLOR_RESET "\n");
    printf(COLOR_YELLOW "========================================================" COLOR_RESET "\n\n");

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int fd = open(PATH_CONSOLE, O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        char *argv[] = {PATH_BSHELL, NULL};
        char *envp[] = {
            "PATH=/bin:/usr/bin",
            "USER=root",
            "HOME=/root",
            "SHELL=/bin/sh",
            "TERM=xterm-256color",
            NULL
        };
        execve(argv[0], argv, envp);
        _exit(1);
    }

    if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) != pid) {
            if (g_requested_signal == SIGINT || g_requested_signal == SIGUSR2 ||
                g_requested_signal == SIGUSR1 || g_requested_signal == SIGTERM) {
                return STATE_DEATH;
            }
        }
    }

    printf(COLOR_CYAN "[INIT] Exited single-user mode. Resuming standard startup..." COLOR_RESET "\n");
    return STATE_RUNCOM;
}

static init_state_t state_runcom(void) {
    printf(COLOR_CYAN "[INIT] Starting system initialization script (%s)..." COLOR_RESET "\n", PATH_RC);

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int fd = open(PATH_CONSOLE, O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        char *argv[] = {PATH_BSHELL, PATH_RC, g_fastboot ? "fastboot" : "autoboot", NULL};
        char *envp[] = {
            "PATH=/bin:/usr/bin",
            "USER=root",
            "HOME=/root",
            "SHELL=/bin/sh",
            "TERM=xterm-256color",
            NULL
        };
        execve(argv[0], argv, envp);
        _exit(127);
    }

    if (pid < 0) {
        printf(COLOR_RED "[INIT] Failed to fork %s: %s" COLOR_RESET "\n", PATH_RC, strerror(errno));
        return STATE_SINGLE_USER;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) != pid) {
        if (g_requested_signal == SIGINT || g_requested_signal == SIGUSR2 ||
            g_requested_signal == SIGUSR1 || g_requested_signal == SIGTERM) {
            return STATE_DEATH;
        }
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf(COLOR_GREEN "[INIT] System initialization (%s) completed successfully." COLOR_RESET "\n", PATH_RC);
        return STATE_READ_TTYS;
    } else {
        printf(COLOR_RED "[INIT] %s exited with status %d! Dropping to single-user mode." COLOR_RESET "\n",
               PATH_RC, WEXITSTATUS(status));
        return STATE_SINGLE_USER;
    }
}

static init_state_t state_read_ttys(void) {
    printf(COLOR_CYAN "[INIT] Loading terminal sessions configuration..." COLOR_RESET "\n");
    free_sessions();
    parse_ttys();

    for (session_t *s = g_sessions; s != NULL; s = s->next) {
        if ((s->flags & SE_ON) && s->pid == 0) {
            start_session(s);
        }
    }

    return STATE_MULTI_USER;
}

static init_state_t state_multi_user(void) {
    while (1) {
        /* Check pending signals via sigpending() and signal handler */
        sigset_t pending = 0;
        sigpending(&pending);
        if (g_requested_signal != 0) {
            pending |= (1U << g_requested_signal);
            g_requested_signal = 0;
        }

        if (pending != 0) {
            if (pending & (1U << SIGHUP)) {
                printf(COLOR_CYAN "[INIT] Received SIGHUP: Reloading /etc/ttys configuration..." COLOR_RESET "\n");
                return STATE_CLEAN_TTYS;
            } else if (pending & (1U << SIGINT)) {
                printf(COLOR_YELLOW "[INIT] Received SIGINT: Initiating system reboot..." COLOR_RESET "\n");
                g_reboot_cmd = RB_AUTOBOOT;
                return STATE_DEATH;
            } else if (pending & (1U << SIGUSR2)) {
                printf(COLOR_YELLOW "[INIT] Received SIGUSR2: Initiating system poweroff..." COLOR_RESET "\n");
                g_reboot_cmd = RB_POWER_OFF;
                return STATE_DEATH;
            } else if (pending & (1U << SIGUSR1)) {
                printf(COLOR_YELLOW "[INIT] Received SIGUSR1: Initiating system halt..." COLOR_RESET "\n");
                g_reboot_cmd = RB_HALT_SYSTEM;
                return STATE_DEATH;
            } else if (pending & (1U << SIGTERM)) {
                printf(COLOR_YELLOW "[INIT] Received SIGTERM: Transitioning to single-user mode..." COLOR_RESET "\n");
                g_reboot_cmd = 0;
                return STATE_DEATH;
            } else if (pending & (1U << SIGTSTP)) {
                printf(COLOR_YELLOW "[INIT] Received SIGTSTP: Entering catatonic mode (sessions paused)..." COLOR_RESET "\n");
                return STATE_CATATONIA;
            }
        }

        /* Wait for any child process or orphan */
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid > 0) {
            session_t *sp = find_session_by_pid(pid);
            if (sp) {
                sp->pid = 0;
                time_t now = time(NULL);

                if (now - sp->started < GETTY_SPACING) {
                    sp->respawn_count++;
                } else {
                    sp->respawn_count = 0;
                }

                if (sp->respawn_count >= GETTY_NSPACE) {
                    printf(COLOR_RED "[INIT] Alert: Session on %s respawning too rapidly! Throttling for %d seconds." COLOR_RESET "\n",
                           sp->device, GETTY_SLEEP);
                    sleep(GETTY_SLEEP);
                    sp->respawn_count = 0;
                }

                if (sp->flags & SE_ON) {
                    printf(COLOR_YELLOW "[INIT] Session on %s (PID %d) terminated. Respawning..." COLOR_RESET "\n",
                           sp->device, pid);
                    start_session(sp);
                }
            }
            /* Orphaned child reaped successfully! */
        }

        /* Non-blocking drain for any additional zombies */
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            session_t *s = find_session_by_pid(pid);
            if (s) {
                s->pid = 0;
                if (s->flags & SE_ON) {
                    start_session(s);
                }
            }
        }
    }
}

static init_state_t state_clean_ttys(void) {
    /* Reload configuration and update active sessions */
    parse_ttys();

    for (session_t *s = g_sessions; s != NULL; s = s->next) {
        if ((s->flags & SE_ON) && s->pid == 0) {
            start_session(s);
        } else if (!(s->flags & SE_ON) && s->pid > 0) {
            kill(s->pid, SIGHUP);
        }
    }

    return STATE_MULTI_USER;
}

static init_state_t state_catatonia(void) {
    while (1) {
        pause();
        if (g_requested_signal == SIGHUP) {
            g_requested_signal = 0;
            return STATE_CLEAN_TTYS;
        } else if (g_requested_signal == SIGINT || g_requested_signal == SIGUSR2 ||
                   g_requested_signal == SIGUSR1 || g_requested_signal == SIGTERM) {
            return STATE_DEATH;
        }
    }
}

static init_state_t state_death(void) {
    printf("\n" COLOR_YELLOW "========================================================" COLOR_RESET "\n");
    printf("  " COLOR_BOLD COLOR_RED "[INIT] SHUTTING DOWN SYSTEM..." COLOR_RESET "\n");
    printf(COLOR_YELLOW "========================================================" COLOR_RESET "\n\n");

    /* Ignore signals during shutdown sequence to prevent re-entrancy or state alteration */
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    /* Step 1: Run /etc/rc.shutdown if present */
    struct stat st;
    if (stat(PATH_SHUTDOWN, &st) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            char *argv[] = {PATH_BSHELL, PATH_SHUTDOWN, NULL};
            char *envp[] = {"PATH=/bin:/usr/bin", "USER=root", "HOME=/root", "TERM=xterm-256color", NULL};
            execve(argv[0], argv, envp);
            _exit(0);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }

    /* Step 2: Revoke managed terminal sessions */
    for (session_t *s = g_sessions; s != NULL; s = s->next) {
        if (s->pid > 0) {
            kill(s->pid, SIGHUP);
        }
    }

    /* Step 3: Broadcast SIGTERM to all processes */
    printf(COLOR_YELLOW "[INIT] Sending SIGTERM to all active processes..." COLOR_RESET "\n");
    kill(-1, SIGTERM);

    /* Grace period: wait up to 1.5 seconds for processes to terminate */
    for (int i = 0; i < 6; i++) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {}
        usleep(250000);
    }

    /* Step 4: Broadcast SIGKILL to all surviving processes */
    printf(COLOR_RED "[INIT] Sending SIGKILL to remaining processes..." COLOR_RESET "\n");
    kill(-1, SIGKILL);
    for (int i = 0; i < 2; i++) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {}
        usleep(100000);
    }

    /* Step 5: Sync filesystems to disk */
    printf(COLOR_CYAN "[INIT] Flushing filesystem buffers..." COLOR_RESET "\n");
    sync();

    /* If SIGTERM requested transition to single user */
    if (g_reboot_cmd == 0) {
        return STATE_SINGLE_USER;
    }

    /* Step 6: Hardware reboot / poweroff */
    if (g_reboot_cmd == RB_POWER_OFF) {
        printf(COLOR_BOLD COLOR_GREEN "[INIT] Power down machine." COLOR_RESET "\n");
    } else if (g_reboot_cmd == RB_HALT_SYSTEM) {
        printf(COLOR_BOLD COLOR_GREEN "[INIT] System halted." COLOR_RESET "\n");
    } else {
        printf(COLOR_BOLD COLOR_GREEN "[INIT] Rebooting machine." COLOR_RESET "\n");
    }

    reboot(g_reboot_cmd);

    /* Loop forever if reboot syscall returns */
    while (1) {
        pause();
    }
}

/* ========================================================================= */
/* Client Mode (telinit / init [0|1|6|q|c])                                  */
/* ========================================================================= */

static int run_client(int argc, char *argv[]) {
    if (getuid() != 0) {
        fprintf(stderr, "init: Permission denied (root required)\n");
        return 1;
    }

    if (argc < 2) {
        fprintf(stderr, "Usage: init {0|1|6|q|c|poweroff|reboot|single|reload}\n");
        return 1;
    }

    const char *arg = argv[1];
    int sig = 0;

    if (strcmp(arg, "0") == 0 || strcmp(arg, "poweroff") == 0) {
        sig = SIGUSR2;
    } else if (strcmp(arg, "6") == 0 || strcmp(arg, "reboot") == 0) {
        sig = SIGINT;
    } else if (strcmp(arg, "1") == 0 || strcmp(arg, "s") == 0 || strcmp(arg, "single") == 0) {
        sig = SIGTERM;
    } else if (strcmp(arg, "q") == 0 || strcmp(arg, "reload") == 0) {
        sig = SIGHUP;
    } else if (strcmp(arg, "c") == 0 || strcmp(arg, "pause") == 0) {
        sig = SIGTSTP;
    } else {
        fprintf(stderr, "init: unknown runlevel / directive '%s'\n", arg);
        return 1;
    }

    if (kill(1, sig) != 0) {
        perror("init: failed to signal PID 1");
        return 1;
    }

    return 0;
}

/* ========================================================================= */
/* Entry Point (main)                                                        */
/* ========================================================================= */

int main(int argc, char *argv[]) {
    /* If executed by user or shell (non-PID-1) */
    if (getpid() != 1) {
        return run_client(argc, argv);
    }

    printf("\n" COLOR_CYAN "========================================================" COLOR_RESET "\n");
    printf("  " COLOR_GREEN "[INIT]" COLOR_RESET " " COLOR_WHITE "SzpontOS System Init Process (PID 1) Started" COLOR_RESET "\n");
    printf("  " COLOR_GREEN "[INIT]" COLOR_RESET " " COLOR_BOLD "Architecture: FreeBSD FSM & POSIX Session Supervisor" COLOR_RESET "\n");
    printf(COLOR_CYAN "========================================================" COLOR_RESET "\n\n");

    /* Create initial session */
    setsid();

    /* Configure signal handlers for PID 1 */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore job control signals in PID 1 */
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    /* Determine initial state */
    init_state_t state = STATE_RUNCOM;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            state = STATE_SINGLE_USER;
        } else if (strcmp(argv[i], "-f") == 0) {
            g_fastboot = true;
        }
    }

    /* Start State Machine Loop */
    while (1) {
        switch (state) {
        case STATE_SINGLE_USER:
            state = state_single_user();
            break;
        case STATE_RUNCOM:
            state = state_runcom();
            break;
        case STATE_READ_TTYS:
            state = state_read_ttys();
            break;
        case STATE_MULTI_USER:
            state = state_multi_user();
            break;
        case STATE_CLEAN_TTYS:
            state = state_clean_ttys();
            break;
        case STATE_CATATONIA:
            state = state_catatonia();
            break;
        case STATE_DEATH:
            state = state_death();
            break;
        }
    }

    return 0;
}
