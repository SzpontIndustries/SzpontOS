/* BSD fgetln() - read a line without NUL-terminating it.
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Reads a line (including the trailing '\n' when present, embedded NUL bytes
 * preserved) and returns a pointer to an internal static buffer with its
 * length in *lenp. The buffer is overwritten by the next I/O operation on ANY
 * stream, matching historic BSD semantics (cf. FreeBSD fgetln, which likewise
 * uses a single static buffer). Returns NULL with *lenp set to 0 on EOF or
 * error; use feof()/ferror() to distinguish, as with BSD.
 */
#include <stdio.h>
#include <stdlib.h>

char *fgetln(FILE *stream, size_t *lenp) {
    static char *buf = NULL;
    static size_t bufsize = 0;
    size_t pos = 0;
    int c;

    if (!stream) {
        if (lenp)
            *lenp = 0;
        return NULL;
    }

    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= bufsize) {
            size_t nsize = bufsize ? bufsize * 2 : 128;
            char *nbuf = (char *)realloc(buf, nsize);
            if (!nbuf) {
                if (lenp)
                    *lenp = 0;
                return NULL;
            }
            buf = nbuf;
            bufsize = nsize;
        }
        buf[pos++] = (char)c;
        if (c == '\n')
            break;
    }

    if (pos == 0) {
        if (lenp)
            *lenp = 0;
        return NULL;
    }
    if (lenp)
        *lenp = pos;
    return buf;
}
