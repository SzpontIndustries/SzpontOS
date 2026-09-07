/*
 * SzpontOS - POSIX/XPG crypt.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _CRYPT_H
#define _CRYPT_H

#ifdef __cplusplus
extern "C" {
#endif

char *crypt(const char *key, const char *salt);

#ifdef __cplusplus
}
#endif

#endif /* _CRYPT_H */
