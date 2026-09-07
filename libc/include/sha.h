/*
 * SzpontOS - POSIX sha.h compatibility header
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SHA_H
#define _SHA_H

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

typedef SHA1_CTX SHA_CTX;

void SHA1Init(SHA1_CTX *context);
void SHA1Update(SHA1_CTX *context, const uint8_t *data, size_t len);
void SHA1Final(uint8_t digest[20], SHA1_CTX *context);

static inline int SHA1_Init(SHA1_CTX *c) {
    SHA1Init(c);
    return 1;
}

static inline int SHA1_Update(SHA1_CTX *c, const void *data, size_t len) {
    SHA1Update(c, (const uint8_t *)data, len);
    return 1;
}

static inline int SHA1_Final(unsigned char *md, SHA1_CTX *c) {
    SHA1Final(md, c);
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* _SHA_H */
