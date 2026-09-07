#ifndef _UTIME_H
#define _UTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>

struct utimbuf {
    time_t actime;  /* Access time */
    time_t modtime; /* Modification time */
};

int utime(const char *filename, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif /* _UTIME_H */
