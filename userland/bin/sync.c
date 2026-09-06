/*
 * SzpontOS - sync utility (/bin/sync)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <unistd.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    sync();
    return 0;
}
