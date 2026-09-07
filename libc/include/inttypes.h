#ifndef _INTTYPES_H
#define _INTTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "ld"
#define PRIdMAX "ld"
#define PRIdPTR "ld"

#define PRIi8 "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIi64 "li"
#define PRIiMAX "li"
#define PRIiPTR "li"

#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "lu"
#define PRIuMAX "lu"
#define PRIuPTR "lu"

#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "lx"
#define PRIxMAX "lx"
#define PRIxPTR "lx"

#define PRIX8 "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 "lX"
#define PRIXMAX "lX"
#define PRIXPTR "lX"

#define PRIo8 "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIo64 "lo"
#define PRIoMAX "lo"
#define PRIoPTR "lo"

#define SCNd8 "hhd"
#define SCNd16 "hd"
#define SCNd32 "d"
#define SCNd64 "ld"
#define SCNdMAX "ld"

#define SCNu8 "hhu"
#define SCNu16 "hu"
#define SCNu32 "u"
#define SCNu64 "lu"
#define SCNuMAX "lu"

#define SCNx8 "hhx"
#define SCNx16 "hx"
#define SCNx32 "x"
#define SCNx64 "lx"

intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
intmax_t imaxabs(intmax_t j);

#ifdef __cplusplus
}
#endif

#endif /* _INTTYPES_H */
