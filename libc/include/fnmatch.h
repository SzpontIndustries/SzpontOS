#ifndef _FNMATCH_H
#define _FNMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define FNM_NOMATCH 1
#define FNM_PATHNAME (1 << 0)
#define FNM_NOESCAPE (1 << 1)
#define FNM_PERIOD (1 << 2)
#define FNM_FILE_NAME FNM_PATHNAME
#define FNM_LEADING_DIR (1 << 3)
#define FNM_CASEFOLD (1 << 4)
#define FNM_IGNORECASE FNM_CASEFOLD

int fnmatch(const char *pattern, const char *string, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _FNMATCH_H */
int rpl_fnmatch(const char *pattern, const char *string, int flags);
