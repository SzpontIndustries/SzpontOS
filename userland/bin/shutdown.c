/*
 * SzpontOS - shutdown (System shutdown & poweroff utility)
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Communicates with PID 1 (init) to execute /etc/rc.shutdown, terminate services,
 * and cleanly bring the system down, or performs emergency shutdown if '-f' is passed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/reboot.h>

static void usage(void) {
    fprintf(stderr, "Usage: shutdown [-h | -r | -P | -s] [-f] [now]\n");
    fprintf(stderr, "  -h, -P, --poweroff  Power off / halt the machine (default)\n");
    fprintf(stderr, "  -r, --reboot        Reboot the machine\n");
    fprintf(stderr, "  -s, --single        Transition to single-user mode\n");
    fprintf(stderr, "  -f, --force         Force immediate action without notifying init\n");
}

int main(int argc, char *argv[]) {
    bool do_reboot = false;
    bool do_single = false;
    bool force = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--reboot") == 0) {
            do_reboot = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--poweroff") == 0) {
            do_reboot = false;
            do_single = false;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--single") == 0) {
            do_single = true;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "now") == 0) {
            /* Immediate shutdown */
        }
    }

    if (!force) {
        int sig = SIGUSR2;
        if (do_reboot) {
            sig = SIGINT;
        } else if (do_single) {
            sig = SIGTERM;
        }

        if (kill(1, sig) == 0) {
            return 0;
        }
    }

    if (do_reboot) {
        printf("\033[1;33mThe system is going down for reboot NOW!\033[0m\n");
        sync();
        reboot(RB_AUTOBOOT);
    } else if (do_single) {
        printf("\033[1;33mTransitioning to single-user mode NOW!\033[0m\n");
        kill(1, SIGTERM);
    } else {
        printf("\033[1;31mThe system is going down for poweroff NOW!\033[0m\n");
        sync();
        reboot(RB_POWER_OFF);
    }

    return 0;
}
