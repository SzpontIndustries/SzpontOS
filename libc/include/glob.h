/*
 * SzpontOS - POSIX glob.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _GLOB_H
#define _GLOB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stddef.h>

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
    int gl_flags;
} glob_t;

#define GLOB_ERR (1 << 0)
#define GLOB_MARK (1 << 1)
#define GLOB_NOSORT (1 << 2)
#define GLOB_DOOFS (1 << 3)
#define GLOB_NOCHECK (1 << 4)
#define GLOB_APPEND (1 << 5)
#define GLOB_NOESCAPE (1 << 6)
#define GLOB_PERIOD (1 << 7)
#define GLOB_MAGCHAR (1 << 8)
#define GLOB_ALTDIRFUNC (1 << 9)
#define GLOB_BRACE (1 << 10)
#define GLOB_NOMAGIC (1 << 11)
#define GLOB_TILDE (1 << 12)
#define GLOB_ONLYDIR (1 << 13)
#define GLOB_TILDE_CHECK (1 << 14)

#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3
#define GLOB_NOSYS 4

int glob(const char *pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *pglob);
void globfree(glob_t *pglob);

#ifdef __cplusplus
}
#endif

#endif /* _GLOB_H */
