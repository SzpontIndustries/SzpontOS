/*
 * SzpontOS - POSIX/BSD netinet/ip.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _NETINET_IP_H
#define _NETINET_IP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <netinet/in.h>
#include <endian.h>

#define IPVERSION 4

struct ip {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned char ip_hl:4,
                  ip_v:4;
#else
    unsigned char ip_v:4,
                  ip_hl:4;
#endif
    unsigned char  ip_tos;
    unsigned short ip_len;
    unsigned short ip_id;
    unsigned short ip_off;
#define IP_RF 0x8000
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff
    unsigned char  ip_ttl;
    unsigned char  ip_p;
    unsigned short ip_sum;
    struct in_addr ip_src, ip_dst;
} __attribute__((packed));

#define IP_MAXPACKET 65535

#define IPTOS_LOWDELAY     0x10
#define IPTOS_THROUGHPUT   0x08
#define IPTOS_RELIABILITY  0x04
#define IPTOS_MINCOST      0x02

#define IPTOS_PREC_NETCONTROL       0xe0
#define IPTOS_PREC_INTERNETCONTROL  0xc0
#define IPTOS_PREC_CRITIC_ECP       0xa0
#define IPTOS_PREC_FLASHOVERRIDE    0x80
#define IPTOS_PREC_FLASH            0x60
#define IPTOS_PREC_IMMEDIATE        0x40
#define IPTOS_PREC_PRIORITY         0x20
#define IPTOS_PREC_ROUTINE          0x00

#define IPTOS_DSCP_CS0 0x00
#define IPTOS_DSCP_CS1 0x20
#define IPTOS_DSCP_CS2 0x40
#define IPTOS_DSCP_CS3 0x60
#define IPTOS_DSCP_CS4 0x80
#define IPTOS_DSCP_CS5 0xa0
#define IPTOS_DSCP_CS6 0xc0
#define IPTOS_DSCP_CS7 0xe0

#define IPTOS_DSCP_AF11 0x28
#define IPTOS_DSCP_AF12 0x30
#define IPTOS_DSCP_AF13 0x38
#define IPTOS_DSCP_AF21 0x48
#define IPTOS_DSCP_AF22 0x50
#define IPTOS_DSCP_AF23 0x58
#define IPTOS_DSCP_AF31 0x68
#define IPTOS_DSCP_AF32 0x70
#define IPTOS_DSCP_AF33 0x78
#define IPTOS_DSCP_AF41 0x88
#define IPTOS_DSCP_AF42 0x90
#define IPTOS_DSCP_AF43 0x98
#define IPTOS_DSCP_EF   0xb8

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_IP_H */
