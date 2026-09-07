/*
 * SzpontOS - POSIX/BSD sha1.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SHA1_H
#define _SHA1_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} SHA1_CTX;

void SHA1Init(SHA1_CTX *context);
void SHA1Update(SHA1_CTX *context, const uint8_t *data, size_t len);
void SHA1Final(uint8_t digest[20], SHA1_CTX *context);

/* OpenSSL compat functions */
typedef SHA1_CTX SHA_CTX;
int SHA1_Init(SHA_CTX *c);
int SHA1_Update(SHA_CTX *c, const void *data, size_t len);
int SHA1_Final(unsigned char *md, SHA_CTX *c);

#ifdef __cplusplus
}
#endif

#endif /* _SHA1_H */
