#include <stdio.h>
#include <unistd.h>
#include <sched.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("\n\033[1;36m========================================================\033[0m\n");
    printf("  \033[1;32m[INIT]\033[0m \033[1;37mSzpontOS Init Process (PID 1) started in Ring 3!\033[0m\n");
    printf("  \033[1;32m[INIT]\033[0m Spawning interactive user shell (/bin/sh)...\n");
    printf("\033[1;36m========================================================\033[0m\n\n");

    char *sh_argv[] = {"/bin/sh", NULL};
    char *sh_envp[] = {"PATH=/bin:/usr/bin", "USER=root", "TERM=xterm-256color", "HOME=/root", "SHELL=/bin/sh", NULL};
    execve(sh_argv[0], sh_argv, sh_envp);

    printf("\033[1;31m[INIT] Error: execve %s returned!\033[0m\n", sh_argv[0]);
    while (1) {
        sched_yield();
    }
    return 1;
}
