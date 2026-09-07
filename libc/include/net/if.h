#ifndef _NET_IF_H
#define _NET_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>

#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short ifr_flags;
        int ifr_ifindex;
        int ifr_metric;
        int ifr_mtu;
        char *ifr_data;
    };
};

struct ifconf {
    int ifc_len;
    union {
        char *ifc_buf;
        struct ifreq *ifc_req;
    } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifc_buf
#define ifc_req ifc_ifcu.ifc_req

#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_DEBUG 0x4
#define IFF_LOOPBACK 0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS 0x20
#define IFF_RUNNING 0x40
#define IFF_NOARP 0x80
#define IFF_PROMISC 0x100
#define IFF_ALLMULTI 0x200
#define IFF_MASTER 0x400
#define IFF_SLAVE 0x800
#define IFF_MULTICAST 0x1000
#define IFF_PORTSEL 0x2000
#define IFF_AUTOMEDIA 0x4000
#define IFF_DYNAMIC 0x8000
#define IFF_LOWER_UP 0x10000
#define IFF_DORMANT 0x20000
#define IFF_ECHO 0x40000

#define SIOCGIFNAME 0x8910
#define SIOCSIFLINK 0x8911
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFADDR 0x8915
#define SIOCSIFADDR 0x8916
#define SIOCGIFDSTADDR 0x8917
#define SIOCSIFDSTADDR 0x8918
#define SIOCGIFBRDADDR 0x8919
#define SIOCSIFBRDADDR 0x891a
#define SIOCGIFNETMASK 0x891b
#define SIOCSIFNETMASK 0x891c
#define SIOCGIFMETRIC 0x891d
#define SIOCSIFMETRIC 0x891e
#define SIOCGIFMEM 0x891f
#define SIOCSIFMEM 0x8920
#define SIOCGIFMTU 0x8921
#define SIOCSIFMTU 0x8922
#define SIOCGIFHWADDR 0x8927

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

unsigned int if_nametoindex(const char *ifname);
char *if_indextoname(unsigned int ifindex, char *ifname);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *ptr);

#ifdef __cplusplus
}
#endif

#endif /* _NET_IF_H */
