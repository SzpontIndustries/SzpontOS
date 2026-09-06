/*
 * SzpontOS - poweroff (System poweroff utility)
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Signals PID 1 (init) via SIGUSR2 for graceful shutdown and ACPI poweroff,
 * or forces immediate hardware poweroff if '-f' is passed.
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
        /* Signal init (PID 1) for graceful FreeBSD-style poweroff */
        if (kill(1, SIGUSR2) == 0) {
            return 0;
        }
        /* Fallback if PID 1 is not running or signal failed */
    }

    printf("\033[1;31mForcing immediate hardware poweroff...\033[0m\n");
    sync();

    if (reboot(RB_POWER_OFF) != 0) {
        perror("poweroff");
        return 1;
    }

    return 0;
}
