/*
 * SzpontOS - reboot (System reboot utility)
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Signals PID 1 (init) via SIGINT for graceful shutdown and reboot,
 * or forces immediate hardware reboot if '-f' is passed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/reboot.h>

int main(int argc, char *argv[]) {
    bool force = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        }
    }

    if (!force) {
        /* Signal init (PID 1) for graceful FreeBSD-style shutdown */
        if (kill(1, SIGINT) == 0) {
            return 0;
        }
        /* Fallback if PID 1 is not running or signal failed */
    }

    printf("\033[1;33mForcing immediate hardware reboot...\033[0m\n");
    sync();

    if (reboot(RB_AUTOBOOT) != 0) {
        perror("reboot");
        return 1;
    }

    return 0;
}
