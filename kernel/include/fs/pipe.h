/*
 * SzpontOS - Kernel Pipe Channel Definitions
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _FS_PIPE_H
#define _FS_PIPE_H

#include <kernel/types.h>

#define PIPE_BUF_SIZE 4096

typedef struct pipe_chan {
    char data[PIPE_BUF_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    int readers;
    int writers;
} pipe_chan_t;

#endif /* _FS_PIPE_H */
