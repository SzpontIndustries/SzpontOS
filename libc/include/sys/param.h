#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <limits.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define powerof2(x) ((((x) - 1) & (x)) == 0)

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#ifndef MAXLOGNAME
#define MAXLOGNAME 33
#endif

#ifndef ALIGNBYTES
#define ALIGNBYTES (sizeof(void *) - 1)
#endif

#ifndef ALIGN
#define ALIGN(p) (((unsigned long)(p) + ALIGNBYTES) & ~ALIGNBYTES)
#endif

#endif /* _SYS_PARAM_H */
