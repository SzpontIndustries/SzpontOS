#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <netinet/in.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

int inet_aton(const char *cp, struct in_addr *pin);
in_addr_t inet_addr(const char *cp);
char *inet_ntoa(struct in_addr in);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifndef htons
#define htons(x) __builtin_bswap16(x)
#define ntohs(x) __builtin_bswap16(x)
#define htonl(x) __builtin_bswap32(x)
#define ntohl(x) __builtin_bswap32(x)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H */
