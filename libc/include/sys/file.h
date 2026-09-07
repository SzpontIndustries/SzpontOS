#ifndef _SYS_FILE_H
#define _SYS_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <fcntl.h>

#define LOCK_SH 1 /* Shared lock */
#define LOCK_EX 2 /* Exclusive lock */
#define LOCK_NB 4 /* Don't block when locking */
#define LOCK_UN 8 /* Unlock */

int flock(int fd, int operation);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FILE_H */
