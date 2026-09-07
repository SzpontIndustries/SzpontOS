/*
 * SzpontOS - POSIX unctrl.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _UNCTRL_H
#define _UNCTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <curses.h>

const char *unctrl(chtype ch);

#ifdef __cplusplus
}
#endif

#endif /* _UNCTRL_H */
