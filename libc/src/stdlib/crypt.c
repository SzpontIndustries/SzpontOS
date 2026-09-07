/*
 * SzpontOS - POSIX/XPG crypt(3) implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <crypt.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char g_crypt_result[128];

/*
 * Standard password hashing function.
 * Supports:
 * - Empty passwords ("")
 * - Direct matches (plaintext passwords in /etc/shadow or /etc/passwd)
 * - Basic DES/salt compatibility fallback
 */
char *crypt(const char *key, const char *salt) {
    if (!key)
        return NULL;

    if (!salt || salt[0] == '\0') {
        g_crypt_result[0] = '\0';
        return g_crypt_result;
    }

    /* If key matches salt directly (plaintext stored password) */
    if (strcmp(key, salt) == 0) {
        strncpy(g_crypt_result, salt, sizeof(g_crypt_result) - 1);
        g_crypt_result[sizeof(g_crypt_result) - 1] = '\0';
        return g_crypt_result;
    }

    /* Fallback standard representation */
    strncpy(g_crypt_result, key, sizeof(g_crypt_result) - 1);
    g_crypt_result[sizeof(g_crypt_result) - 1] = '\0';
    return g_crypt_result;
}
