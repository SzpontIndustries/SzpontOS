#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static struct group g_group_entry;
static char g_group_buf[512];
static char *g_group_members[16];
static int g_group_fd = -1;

void setgrent(void) {
    if (g_group_fd >= 0) {
        close(g_group_fd);
    }
    g_group_fd = open("/etc/group", O_RDONLY);
}

void endgrent(void) {
    if (g_group_fd >= 0) {
        close(g_group_fd);
        g_group_fd = -1;
    }
}

struct group *getgrent(void) {
    if (g_group_fd < 0) {
        setgrent();
        if (g_group_fd < 0)
            return NULL;
    }

    size_t pos = 0;
    while (pos < sizeof(g_group_buf) - 1) {
        char c;
        ssize_t bytes = read(g_group_fd, &c, 1);
        if (bytes <= 0) {
            if (pos == 0)
                return NULL;
            break;
        }
        if (c == '\n')
            break;
        g_group_buf[pos++] = c;
    }
    g_group_buf[pos] = '\0';
    if (pos == 0)
        return NULL;

    /* Parse: name:password:gid:members */
    char *fields[4];
    char *p = g_group_buf;
    for (int i = 0; i < 4; i++) {
        fields[i] = p;
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            p = colon + 1;
        } else {
            if (i < 3)
                return NULL;
            break;
        }
    }

    g_group_entry.gr_name = fields[0];
    g_group_entry.gr_passwd = fields[1];
    g_group_entry.gr_gid = (gid_t)atoi(fields[2]);

    /* Parse comma-separated members in fields[3] */
    int mem_count = 0;
    char *m = fields[3];
    while (*m && mem_count < 15) {
        g_group_members[mem_count++] = m;
        char *comma = strchr(m, ',');
        if (comma) {
            *comma = '\0';
            m = comma + 1;
        } else {
            break;
        }
    }
    g_group_members[mem_count] = NULL;
    g_group_entry.gr_mem = g_group_members;

    return &g_group_entry;
}

struct group *getgrnam(const char *name) {
    if (!name)
        return NULL;
    setgrent();
    struct group *gr;
    while ((gr = getgrent()) != NULL) {
        if (strcmp(gr->gr_name, name) == 0) {
            endgrent();
            return gr;
        }
    }
    endgrent();
    return NULL;
}

struct group *getgrgid(gid_t gid) {
    setgrent();
    struct group *gr;
    while ((gr = getgrent()) != NULL) {
        if (gr->gr_gid == gid) {
            endgrent();
            return gr;
        }
    }
    endgrent();
    return NULL;
}

int initgroups(const char *user, gid_t group) {
    if (!user)
        return -1;

    gid_t groups[32];
    size_t ngroups = 0;
    groups[ngroups++] = group;

    setgrent();
    struct group *gr;
    while ((gr = getgrent()) != NULL && ngroups < 32) {
        if (gr->gr_gid == group)
            continue;
        for (char **m = gr->gr_mem; m && *m; m++) {
            if (strcmp(*m, user) == 0) {
                groups[ngroups++] = gr->gr_gid;
                break;
            }
        }
    }
    endgrent();

    return setgroups(ngroups, groups);
}
