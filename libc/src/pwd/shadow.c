/*
 * SzpontOS - shadow.h and getspnam implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static struct spwd g_spwd_entry;
static char g_spwd_buf[512];
static int g_spwd_fd = -1;

void setspent(void) {
    if (g_spwd_fd >= 0) {
        close(g_spwd_fd);
    }
    g_spwd_fd = open(_PATH_SHADOW, O_RDONLY);
}

void endspent(void) {
    if (g_spwd_fd >= 0) {
        close(g_spwd_fd);
        g_spwd_fd = -1;
    }
}

struct spwd *getspent(void) {
    if (g_spwd_fd < 0) {
        setspent();
        if (g_spwd_fd < 0)
            return NULL;
    }

    size_t pos = 0;
    while (pos < sizeof(g_spwd_buf) - 1) {
        char c;
        ssize_t bytes = read(g_spwd_fd, &c, 1);
        if (bytes <= 0) {
            if (pos == 0)
                return NULL;
            break;
        }
        if (c == '\n')
            break;
        g_spwd_buf[pos++] = c;
    }
    g_spwd_buf[pos] = '\0';
    if (pos == 0)
        return NULL;

    /* Parse: name:password:lstchg:min:max:warn:inact:expire:flag */
    char *fields[9];
    char *p = g_spwd_buf;
    for (int i = 0; i < 9; i++) {
        fields[i] = p;
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            p = colon + 1;
        } else {
            if (i < 1)
                return NULL; /* Malformed */
            /* Fill rest with empty strings */
            for (int j = i + 1; j < 9; j++)
                fields[j] = "";
            break;
        }
    }

    g_spwd_entry.sp_namp   = fields[0];
    g_spwd_entry.sp_pwdp   = fields[1];
    g_spwd_entry.sp_lstchg = fields[2][0] ? atol(fields[2]) : -1;
    g_spwd_entry.sp_min    = fields[3][0] ? atol(fields[3]) : -1;
    g_spwd_entry.sp_max    = fields[4][0] ? atol(fields[4]) : -1;
    g_spwd_entry.sp_warn   = fields[5][0] ? atol(fields[5]) : -1;
    g_spwd_entry.sp_inact  = fields[6][0] ? atol(fields[6]) : -1;
    g_spwd_entry.sp_expire = fields[7][0] ? atol(fields[7]) : -1;
    g_spwd_entry.sp_flag   = fields[8][0] ? (unsigned long)atol(fields[8]) : 0;

    return &g_spwd_entry;
}

struct spwd *getspnam(const char *name) {
    if (!name)
        return NULL;
    setspent();
    struct spwd *sp;
    while ((sp = getspent()) != NULL) {
        if (strcmp(sp->sp_namp, name) == 0) {
            endspent();
            return sp;
        }
    }
    endspent();
    return NULL;
}
