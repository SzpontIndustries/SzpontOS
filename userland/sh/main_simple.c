#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define MAX_LINE 512
#define MAX_ARGS 64
#define HISTORY_SIZE 32

/* ANSI Color definitions */
#define COLOR_RESET "\033[0m"
#define COLOR_BOLD "\033[1m"
#define COLOR_RED "\033[1;31m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_BLUE "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN "\033[1;36m"
#define COLOR_WHITE "\033[1;37m"
#define COLOR_GRAY "\033[0;90m"

static char g_history[HISTORY_SIZE][MAX_LINE];
static int g_history_count = 0;
static int g_history_pos = 0;

static int parse_and_execute_line(char *line);

static void history_add(const char *cmd) {
    if (!cmd || !*cmd)
        return;
    if (g_history_count > 0 && strcmp(g_history[(g_history_count - 1) % HISTORY_SIZE], cmd) == 0) {
        return; /* Do not duplicate consecutive commands */
    }
    strncpy(g_history[g_history_count % HISTORY_SIZE], cmd, MAX_LINE - 1);
    g_history[g_history_count % HISTORY_SIZE][MAX_LINE - 1] = '\0';
    g_history_count++;
}

static void print_prompt(void) {
    char cwd[128];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/");
    }

    char hostname[64];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "szpontos");
    }

    uid_t euid = geteuid();
    if (euid == 0) {
        printf(COLOR_RED "root" COLOR_RESET "@" COLOR_GREEN "%s" COLOR_RESET ":" COLOR_BLUE "%s" COLOR_RESET "# ",
               hostname, cwd);
    } else {
        struct passwd *pw = getpwuid(euid);
        const char *name = (pw && pw->pw_name) ? pw->pw_name : "user";
        printf(COLOR_CYAN "%s" COLOR_RESET "@" COLOR_GREEN "%s" COLOR_RESET ":" COLOR_BLUE "%s" COLOR_RESET "$ ", name,
               hostname, cwd);
    }
    fflush(stdout);
}

static void cmd_help(void) {
    printf(COLOR_CYAN "SzpontOS Shell (sh) - Built-in Commands:" COLOR_RESET "\n");
    printf("  " COLOR_GREEN "help" COLOR_RESET "            - Display this help message\n");
    printf("  " COLOR_GREEN "echo [args...]" COLOR_RESET "  - Print text to standard output\n");
    printf("  " COLOR_GREEN "pwd" COLOR_RESET "             - Print current working directory\n");
    printf("  " COLOR_GREEN "cd <path>" COLOR_RESET "       - Change current working directory\n");
    printf("  " COLOR_GREEN "exit [code]" COLOR_RESET "     - Exit the shell\n");
    printf("\n" COLOR_YELLOW "External Commands (/bin/):" COLOR_RESET "\n");
    printf("  " COLOR_WHITE "ls, cat, head, tail, wc, grep, find" COLOR_RESET " - Files & text\n");
    printf("  " COLOR_WHITE "uname, hostname, uptime, date, clock, ps, top" COLOR_RESET " - System info\n");
    printf("  " COLOR_WHITE "df, free, mount, dmesg, sysctl" COLOR_RESET "  - Diagnostics\n");
    printf("  " COLOR_WHITE "mkdir, touch, rm, chmod, chown" COLOR_RESET "  - File management\n");
    printf("  " COLOR_WHITE "id, whoami, su, useradd, userdel" COLOR_RESET " - Users & groups\n");
    printf("  " COLOR_WHITE "kill, killall, sleep, clear" COLOR_RESET "     - Process control\n");
    printf("  " COLOR_WHITE "ping, ifconfig, nc, httpd" COLOR_RESET "      - Networking\n");
    printf("  " COLOR_WHITE "git, nano, fastfetch, file, zsh" COLOR_RESET "  - Applications & Version Control\n");
    printf("  " COLOR_WHITE "startx, SzpontX11, xdemo" COLOR_RESET "       - X11 Graphical Desktop & Server\n");
    printf("  " COLOR_WHITE "insmod, rmmod, lsmod, modinfo" COLOR_RESET "  - Kernel modules\n");
    printf("  " COLOR_WHITE "reboot, shutdown, poweroff" COLOR_RESET "     - Power management\n");
    printf("\n" COLOR_YELLOW "Features & Redirection:" COLOR_RESET "\n");
    printf("  " COLOR_WHITE "cmd > file" COLOR_RESET "     - Redirect stdout to file (overwrite)\n");
    printf("  " COLOR_WHITE "cmd >> file" COLOR_RESET "    - Redirect stdout to file (append)\n");
    printf("  " COLOR_WHITE "cmd < file" COLOR_RESET "     - Redirect stdin from file\n");
    printf("  " COLOR_WHITE "cmd1 | cmd2" COLOR_RESET "    - Pipeline connecting stdout to stdin\n");
    printf("  " COLOR_WHITE "cmd1 ; cmd2" COLOR_RESET "    - Sequential command execution\n");
    printf("\n" COLOR_YELLOW "Keyboard Shortcuts & Line Editing:" COLOR_RESET "\n");
    printf("  " COLOR_WHITE "Up/Down Arrows" COLOR_RESET "  - Command history navigation\n");
    printf("  " COLOR_WHITE "Left/Right" COLOR_RESET "      - Cursor movement\n");
    printf("  " COLOR_WHITE "Home / End" COLOR_RESET "      - Jump to beginning / end of line\n");
    printf("  " COLOR_WHITE "Ctrl+C" COLOR_RESET "          - Send SIGINT (cancel current input)\n");
    printf("  " COLOR_WHITE "Ctrl+D" COLOR_RESET "          - Exit shell on empty line / EOF\n");
    printf("  " COLOR_WHITE "Ctrl+L" COLOR_RESET "          - Clear screen and redraw prompt\n");
    printf("  " COLOR_WHITE "Ctrl+U" COLOR_RESET "          - Clear entire input line\n");
}

static void cmd_pwd(void) {
    char buf[256];
    if (getcwd(buf, sizeof(buf))) {
        printf("%s\n", buf);
    } else {
        printf("/\n");
    }
}

static void cmd_cd(const char *path) {
    if (!path || !*path)
        path = "/";
    if (chdir(path) != 0) {
        printf(COLOR_RED "cd: %s: No such directory" COLOR_RESET "\n", path);
    }
}

static void expand_line(const char *src, char *dst, size_t dst_size) {
    size_t d = 0;
    bool in_quote = false;
    char quote_char = 0;

    for (size_t s = 0; src[s] && d < dst_size - 6; s++) {
        char c = src[s];
        if (!in_quote && (c == '\'' || c == '"')) {
            in_quote = true;
            quote_char = c;
            dst[d++] = c;
        } else if (in_quote && c == quote_char) {
            in_quote = false;
            dst[d++] = c;
        } else if (!in_quote && (c == '>' || c == '<' || c == '|' || c == ';')) {
            if (c == '>' && src[s + 1] == '>') {
                dst[d++] = ' ';
                dst[d++] = '>';
                dst[d++] = '>';
                dst[d++] = ' ';
                s++;
            } else {
                dst[d++] = ' ';
                dst[d++] = c;
                dst[d++] = ' ';
            }
        } else {
            dst[d++] = c;
        }
    }
    dst[d] = '\0';
}

static int execute_simple_command(int argc, char *argv[]) {
    if (argc == 0)
        return 0;

    char *infile = NULL;
    char *outfile = NULL;
    bool append_mode = false;
    char *clean_argv[MAX_ARGS];
    int clean_argc = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            outfile = argv[++i];
            append_mode = false;
        } else if (strcmp(argv[i], ">>") == 0 && i + 1 < argc) {
            outfile = argv[++i];
            append_mode = true;
        } else if (strcmp(argv[i], "<") == 0 && i + 1 < argc) {
            infile = argv[++i];
        } else {
            clean_argv[clean_argc++] = argv[i];
        }
    }
    clean_argv[clean_argc] = NULL;

    if (clean_argc == 0)
        return 0;

    /* Built-in commands that affect parent shell state */
    if (strcmp(clean_argv[0], "exit") == 0) {
        int code = (clean_argc > 1) ? atoi(clean_argv[1]) : 0;
        exit(code);
    }
    if (strcmp(clean_argv[0], "cd") == 0) {
        cmd_cd(clean_argc > 1 ? clean_argv[1] : "/");
        return 0;
    }
    if (strcmp(clean_argv[0], "export") == 0) {
        for (int i = 1; i < clean_argc; i++) {
            putenv(strdup(clean_argv[i]));
        }
        return 0;
    }
    if (strchr(clean_argv[0], '=') != NULL && clean_argv[0][0] != '/') {
        putenv(strdup(clean_argv[0]));
        return 0;
    }
    if (strcmp(clean_argv[0], ".") == 0 || strcmp(clean_argv[0], "source") == 0) {
        if (clean_argc > 1) {
            FILE *sf = fopen(clean_argv[1], "r");
            if (sf) {
                char sline[MAX_LINE];
                while (fgets(sline, sizeof(sline), sf)) {
                    size_t slen = strlen(sline);
                    while (slen > 0 && (sline[slen - 1] == '\n' || sline[slen - 1] == '\r')) {
                        sline[--slen] = '\0';
                    }
                    if (slen > 0 && sline[0] != '#') {
                        parse_and_execute_line(sline);
                    }
                }
                fclose(sf);
            }
        }
        return 0;
    }

    /* Redirection setup for builtins and external commands */
    int saved_stdin = -1;
    int saved_stdout = -1;

    if (infile) {
        int fd_in = open(infile, O_RDONLY);
        if (fd_in < 0) {
            perror(infile);
            return 1;
        }
        saved_stdin = dup(STDIN_FILENO);
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    if (outfile) {
        int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
        int fd_out = open(outfile, flags, 0644);
        if (fd_out < 0) {
            perror(outfile);
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            return 1;
        }
        saved_stdout = dup(STDOUT_FILENO);
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    int ret = 0;
    bool bg = false;
    if (clean_argc > 0 && strcmp(clean_argv[clean_argc - 1], "&") == 0) {
        bg = true;
        clean_argv[--clean_argc] = NULL;
    }

    if (clean_argc == 0) {
        return 0;
    }

    if (strcmp(clean_argv[0], "help") == 0) {
        cmd_help();
        ret = 0;
    } else if (strcmp(clean_argv[0], "pwd") == 0) {
        cmd_pwd();
        ret = 0;
    } else if (strcmp(clean_argv[0], "echo") == 0) {
        for (int i = 1; i < clean_argc; i++) {
            printf("%s%s", clean_argv[i], (i == clean_argc - 1) ? "" : " ");
        }
        printf("\n");
        fflush(stdout);
        ret = 0;
    } else {
        /* External program */
        pid_t pid = fork();
        if (pid == 0) {
            /* Child process: restore default SIGINT */
            signal(SIGINT, SIG_DFL);

            char exec_path[256];
            if (clean_argv[0][0] == '/' ||
                (clean_argv[0][0] == '.' && (clean_argv[0][1] == '/' || clean_argv[0][1] == '.'))) {
                strncpy(exec_path, clean_argv[0], sizeof(exec_path) - 1);
                exec_path[sizeof(exec_path) - 1] = '\0';
            } else {
                snprintf(exec_path, sizeof(exec_path), "/bin/%s", clean_argv[0]);
            }

            char *default_envp[] = {
                "PATH=/bin:/usr/bin",
                "TERM=xterm-256color",
                "USER=root",
                "HOME=/root",
                "SHELL=/bin/sh",
                NULL
            };

            execve(exec_path, clean_argv, default_envp);

            /* If not found in /bin, try /usr/bin */
            if (clean_argv[0][0] != '/') {
                snprintf(exec_path, sizeof(exec_path), "/usr/bin/%s", clean_argv[0]);
                execve(exec_path, clean_argv, default_envp);
            }

            fprintf(stderr, COLOR_RED "sh: %s: command not found" COLOR_RESET "\n", clean_argv[0]);
            exit(127);
        } else if (pid > 0) {
            if (bg) {
                printf("[1] %d\n", pid);
                ret = 0;
            } else {
                int status = 0;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    ret = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    ret = 128 + WTERMSIG(status);
                    if (ret == 130) {
                        printf(COLOR_RED "\n[Process interrupted by SIGINT]" COLOR_RESET "\n");
                    }
                }
            }
        } else {
            perror("fork");
            ret = 1;
        }
    }

    /* Restore STDIN / STDOUT if redirected */
    if (saved_stdin >= 0) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    }
    if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }

    return ret;
}

static int execute_pipeline(int argc, char *argv[]) {
    if (argc == 0)
        return 0;

    int pipe_count = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            pipe_count++;
        }
    }

    if (pipe_count == 0) {
        return execute_simple_command(argc, argv);
    }

    int num_cmds = pipe_count + 1;
    char **cmd_argv[16];
    int cmd_argc[16];
    int cmd_idx = 0;

    cmd_argv[cmd_idx] = &argv[0];
    cmd_argc[cmd_idx] = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            argv[i] = NULL;
            cmd_idx++;
            if (cmd_idx >= 16) break;
            cmd_argv[cmd_idx] = &argv[i + 1];
            cmd_argc[cmd_idx] = 0;
        } else {
            cmd_argc[cmd_idx]++;
        }
    }

    int pipefds[16][2];
    for (int i = 0; i < pipe_count; i++) {
        if (pipe(pipefds[i]) < 0) {
            perror("pipe");
            return 1;
        }
    }

    pid_t pids[16];
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }
            if (i < pipe_count) {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < pipe_count; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            int st = execute_simple_command(cmd_argc[i], cmd_argv[i]);
            exit(st);
        }
    }

    for (int j = 0; j < pipe_count; j++) {
        close(pipefds[j][0]);
        close(pipefds[j][1]);
    }

    int last_status = 0;
    for (int i = 0; i < num_cmds; i++) {
        int st = 0;
        waitpid(pids[i], &st, 0);
        if (i == num_cmds - 1) {
            if (WIFEXITED(st))
                last_status = WEXITSTATUS(st);
            else if (WIFSIGNALED(st))
                last_status = 128 + WTERMSIG(st);
        }
    }

    return last_status;
}

static int parse_and_execute_line(char *line) {
    if (!line)
        return 0;

    char expanded[MAX_LINE * 2];
    expand_line(line, expanded, sizeof(expanded));

    char *tokens[MAX_ARGS];
    int token_count = 0;
    char *p = expanded;

    while (*p && token_count < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (!*p)
            break;

        char *token_start;
        if (*p == '\'' || *p == '"') {
            char quote = *p++;
            token_start = p;
            while (*p && *p != quote)
                p++;
            if (*p == quote) {
                *p++ = '\0';
            }
        } else {
            token_start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                p++;
            if (*p) {
                *p++ = '\0';
            }
        }

        tokens[token_count++] = token_start;
    }
    tokens[token_count] = NULL;

    if (token_count == 0)
        return 0;

    int cmd_start = 0;
    int last_status = 0;

    for (int i = 0; i <= token_count; i++) {
        if (i == token_count || strcmp(tokens[i], ";") == 0) {
            tokens[i] = NULL;
            int count = i - cmd_start;
            if (count > 0) {
                last_status = execute_pipeline(count, &tokens[cmd_start]);
            }
            cmd_start = i + 1;
        }
    }

    return last_status;
}

static bool read_input_line(char *line, size_t max_len) {
    int len = 0;
    int cursor = 0;
    line[0] = '\0';
    g_history_pos = g_history_count;

    while (1) {
        int c = getchar();

        /* EOF (Ctrl+D on empty line) */
        if (c == EOF || c == 0x04) {
            if (len == 0) {
                printf("exit\n");
                return false;
            }
            continue;
        }

        /* Ctrl+C (0x03) */
        if (c == 0x03) {
            printf("^C\n");
            line[0] = '\0';
            return true;
        }

        /* Enter / Newline */
        if (c == '\n' || c == '\r') {
            printf("\n");
            line[len] = '\0';
            if (len > 0) {
                history_add(line);
            }
            return true;
        }

        /* Ctrl+L (Clear Screen) */
        if (c == 0x0C) {
            printf("\033[2J\033[H");
            print_prompt();
            printf("%s", line);
            continue;
        }

        /* Ctrl+U (Clear Line) */
        if (c == 0x15) {
            while (cursor > 0) {
                printf("\b \b");
                cursor--;
            }
            len = 0;
            line[0] = '\0';
            continue;
        }

        /* Backspace (0x08 / 127) */
        if (c == '\b' || c == 127) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) {
                    line[i] = line[i + 1];
                }
                len--;
                cursor--;
                line[len] = '\0';

                /* Redraw tail */
                printf("\b");
                for (int i = cursor; i < len; i++) {
                    putchar(line[i]);
                }
                putchar(' ');
                for (int i = cursor; i <= len; i++) {
                    putchar('\b');
                }
            }
            continue;
        }

        /* ANSI Escape Sequence (Arrows, Home, End, Delete) */
        if (c == '\033') {
            int c1 = getchar();
            if (c1 == '[') {
                int c2 = getchar();

                /* Up Arrow (History Previous) */
                if (c2 == 'A') {
                    if (g_history_count > 0 && g_history_pos > 0) {
                        g_history_pos--;
                        const char *hist = g_history[g_history_pos % HISTORY_SIZE];

                        while (cursor > 0) {
                            printf("\b \b");
                            cursor--;
                        }
                        while (len > 0) {
                            printf(" \b");
                            len--;
                        }

                        strncpy(line, hist, max_len - 1);
                        line[max_len - 1] = '\0';
                        len = strlen(line);
                        cursor = len;
                        printf("%s", line);
                    }
                    continue;
                }

                /* Down Arrow (History Next) */
                if (c2 == 'B') {
                    if (g_history_pos < g_history_count) {
                        g_history_pos++;
                        const char *hist =
                            (g_history_pos < g_history_count) ? g_history[g_history_pos % HISTORY_SIZE] : "";

                        while (cursor > 0) {
                            printf("\b \b");
                            cursor--;
                        }
                        while (len > 0) {
                            printf(" \b");
                            len--;
                        }

                        strncpy(line, hist, max_len - 1);
                        line[max_len - 1] = '\0';
                        len = strlen(line);
                        cursor = len;
                        printf("%s", line);
                    }
                    continue;
                }

                /* Right Arrow */
                if (c2 == 'C') {
                    if (cursor < len) {
                        putchar(line[cursor]);
                        cursor++;
                    }
                    continue;
                }

                /* Left Arrow */
                if (c2 == 'D') {
                    if (cursor > 0) {
                        cursor--;
                        putchar('\b');
                    }
                    continue;
                }

                /* Home */
                if (c2 == 'H') {
                    while (cursor > 0) {
                        putchar('\b');
                        cursor--;
                    }
                    continue;
                }

                /* End */
                if (c2 == 'F') {
                    while (cursor < len) {
                        putchar(line[cursor]);
                        cursor++;
                    }
                    continue;
                }

                /* Delete key: \033[3~ */
                if (c2 == '3') {
                    getchar(); /* Consume '~' */
                    if (cursor < len) {
                        for (int i = cursor; i < len - 1; i++) {
                            line[i] = line[i + 1];
                        }
                        len--;
                        line[len] = '\0';

                        for (int i = cursor; i < len; i++) {
                            putchar(line[i]);
                        }
                        putchar(' ');
                        for (int i = cursor; i <= len; i++) {
                            putchar('\b');
                        }
                    }
                    continue;
                }
            }
            continue;
        }

        /* Printable Character Insertion */
        if (c >= 32 && len < (int)max_len - 1) {
            for (int i = len; i > cursor; i--) {
                line[i] = line[i - 1];
            }
            line[cursor] = (char)c;
            len++;
            line[len] = '\0';

            /* Draw inserted character and tail */
            for (int i = cursor; i < len; i++) {
                putchar(line[i]);
            }
            cursor++;
            for (int i = cursor; i < len; i++) {
                putchar('\b');
            }
        }
    }
}

int main(int argc, char *argv[]) {
    /* If invoked as `sh -c "command..."` */
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        char cmd_buf[MAX_LINE * 2] = {0};
        for (int i = 2; i < argc; i++) {
            if (i > 2)
                strncat(cmd_buf, " ", sizeof(cmd_buf) - strlen(cmd_buf) - 1);
            strncat(cmd_buf, argv[i], sizeof(cmd_buf) - strlen(cmd_buf) - 1);
        }
        return parse_and_execute_line(cmd_buf);
    }

    /* If invoked with a script file `sh script.sh` */
    if (argc >= 2 && argv[1][0] != '-') {
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            perror(argv[1]);
            return 1;
        }
        char line[MAX_LINE];
        int status = 0;
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            if (len > 0 && line[0] != '#') {
                status = parse_and_execute_line(line);
            }
        }
        fclose(f);
        return status;
    }

    /* Interactive shell mode */
    signal(SIGINT, SIG_IGN);

    struct termios term;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &term) == 0) {
        term.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &term);
    }

    printf(COLOR_CYAN "SzpontOS Unix Shell (sh) v0.1.0" COLOR_RESET "\n");
    printf(COLOR_GRAY "Type 'help' for built-in commands & keyboard shortcuts." COLOR_RESET "\n\n");

    char line[MAX_LINE];

    while (1) {
        print_prompt();

        if (!read_input_line(line, sizeof(line))) {
            break;
        }

        if (strlen(line) > 0) {
            parse_and_execute_line(line);
        }
    }

    return 0;
}
