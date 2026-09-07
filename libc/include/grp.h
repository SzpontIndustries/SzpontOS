#ifndef _GRP_H
#define _GRP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

struct group {
    char *gr_name;   /* Group name */
    char *gr_passwd; /* Group password */
    gid_t gr_gid;    /* Group ID */
    char **gr_mem;   /* Member list NULL-terminated */
};

struct group *getgrnam(const char *name);
struct group *getgrgid(gid_t gid);
struct group *getgrent(void);
void setgrent(void);
void endgrent(void);
int initgroups(const char *user, gid_t group);
int setgroups(size_t size, const gid_t *list);

#ifdef __cplusplus
}
#endif

#endif /* _GRP_H */
