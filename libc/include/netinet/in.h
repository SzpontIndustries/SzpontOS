#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/socket.h>

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct in6_addr {
    uint8_t s6_addr[16];
};

struct sockaddr_in6 {
    sa_family_t sin6_family;
    in_port_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

#define INADDR_ANY ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE ((in_addr_t)0xffffffff)

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#define IPPORT_RESERVED 1024
#define IPPORT_USERRESERVED 5000

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_RAW 255

#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36

#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_IF 17
#define IPV6_MULTICAST_LOOP 19
#define IPV6_JOIN_GROUP 20
#define IPV6_LEAVE_GROUP 21

#define IN_CLASSA(a) ((((in_addr_t)(a)) & 0x80000000) == 0)
#define IN_CLASSA_NET 0xff000000
#define IN_CLASSA_NSHIFT 24
#define IN_CLASSA_HOST 0x00ffffff

#define IN_CLASSB(a) ((((in_addr_t)(a)) & 0xc0000000) == 0x80000000)
#define IN_CLASSB_NET 0xffff0000
#define IN_CLASSB_NSHIFT 16
#define IN_CLASSB_HOST 0x0000ffff

#define IN_CLASSC(a) ((((in_addr_t)(a)) & 0xe0000000) == 0xc0000000)
#define IN_CLASSC_NET 0xffffff00
#define IN_CLASSC_NSHIFT 8
#define IN_CLASSC_HOST 0x000000ff

#define IN_CLASSD(a) ((((in_addr_t)(a)) & 0xf0000000) == 0xe0000000)
#define IN_MULTICAST(a) IN_CLASSD(a)

#define IN_LOOPBACKNET 127

#ifndef htons
#define htons(x) __builtin_bswap16((uint16_t)(x))
#define ntohs(x) __builtin_bswap16((uint16_t)(x))
#define htonl(x) __builtin_bswap32((uint32_t)(x))
#define ntohl(x) __builtin_bswap32((uint32_t)(x))
#endif

uint16_t (htons)(uint16_t x);
uint16_t (ntohs)(uint16_t x);
uint32_t (htonl)(uint32_t x);
uint32_t (ntohl)(uint32_t x);


#define IN6_IS_ADDR_UNSPECIFIED(a) \
    (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 && \
     ((const uint32_t *)(a))[2] == 0 && ((const uint32_t *)(a))[3] == 0)

#define IN6_IS_ADDR_LOOPBACK(a) \
    (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 && \
     ((const uint32_t *)(a))[2] == 0 && ((const uint8_t *)(a))[15] == 1)

#define IN6_IS_ADDR_V4MAPPED(a) \
    (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 && \
     ((const uint8_t *)(a))[8] == 0 && ((const uint8_t *)(a))[9] == 0 && \
     ((const uint8_t *)(a))[10] == 0xff && ((const uint8_t *)(a))[11] == 0xff)

#define IN6_IS_ADDR_V4COMPAT(a) \
    (((const uint32_t *)(a))[0] == 0 && ((const uint32_t *)(a))[1] == 0 && \
     ((const uint32_t *)(a))[2] == 0 && !IN6_IS_ADDR_UNSPECIFIED(a) && !IN6_IS_ADDR_LOOPBACK(a))

#define IN6_IS_ADDR_MULTICAST(a) (((const uint8_t *)(a))[0] == 0xff)

#define IN6_IS_ADDR_LINKLOCAL(a) \
    (((const uint8_t *)(a))[0] == 0xfe && (((const uint8_t *)(a))[1] & 0xc0) == 0x80)

#define IN6_IS_ADDR_SITELOCAL(a) \
    (((const uint8_t *)(a))[0] == 0xfe && (((const uint8_t *)(a))[1] & 0xc0) == 0xc0)

extern const struct in6_addr in6addr_any;
#define IN6ADDR_ANY_INIT { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } }

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_IN_H */
