#ifndef _MNTENT_H
#define _MNTENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <paths.h>

#define MOUNTED _PATH_MOUNTED
#define MNTOPT_RO "ro"
#define MNTOPT_RW "rw"

struct mntent {
    char *mnt_fsname;
    char *mnt_dir;
    char *mnt_type;
    char *mnt_opts;
    int mnt_freq;
    int mnt_passno;
};

FILE *setmntent(const char *filename, const char *type);
struct mntent *getmntent(FILE *stream);
int endmntent(FILE *stream);
char *hasmntopt(const struct mntent *mnt, const char *opt);

#ifdef __cplusplus
}
#endif

#endif /* _MNTENT_H */
