#ifndef _LINUX_RTNETLINK_H
#define _LINUX_RTNETLINK_H

#include <linux/netlink.h>

#define RTM_NEWLINK 16
#define RTM_DELLINK 17
#define RTM_GETLINK 18
#define RTM_NEWADDR 20
#define RTM_DELADDR 21
#define RTM_GETADDR 22
#define RTM_NEWROUTE 24
#define RTM_DELROUTE 25
#define RTM_GETROUTE 26

struct rtattr {
    unsigned short rta_len;
    unsigned short rta_type;
};

#define RTA_ALIGNTO 4U
#define RTA_ALIGN(len) (((len)+RTA_ALIGNTO-1) & ~(RTA_ALIGNTO-1))
#define RTA_OK(rta,len) ((len) >= (int)sizeof(struct rtattr) && \
                         (rta)->rta_len >= sizeof(struct rtattr) && \
                         (int)(rta)->rta_len <= (len))
#define RTA_NEXT(rta,attrlen) ((attrlen) -= RTA_ALIGN((rta)->rta_len), \
                               (struct rtattr*)(((char*)(rta)) + RTA_ALIGN((rta)->rta_len)))
#define RTA_LENGTH(len) (RTA_ALIGN(sizeof(struct rtattr)) + (len))
#define RTA_SPACE(len)  RTA_ALIGN(RTA_LENGTH(len))
#define RTA_DATA(rta)   ((void*)(((char*)(rta)) + RTA_LENGTH(0)))
#define RTA_PAYLOAD(rta) ((int)((rta)->rta_len) - RTA_LENGTH(0))

struct rtmsg {
    unsigned char rtm_family;
    unsigned char rtm_dst_len;
    unsigned char rtm_src_len;
    unsigned char rtm_tos;
    unsigned char rtm_table;
    unsigned char rtm_protocol;
    unsigned char rtm_scope;
    unsigned char rtm_type;
    unsigned int  rtm_flags;
};

#define RTM_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct rtmsg))))
#define RTM_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct rtmsg))

#define RT_TABLE_UNSPEC 0
#define RT_TABLE_COMPAT 252
#define RT_TABLE_DEFAULT 253
#define RT_TABLE_MAIN 254
#define RT_TABLE_LOCAL 255

#define RTPROT_UNSPEC   0
#define RTPROT_REDIRECT 1
#define RTPROT_KERNEL   2
#define RTPROT_BOOT     3
#define RTPROT_STATIC   4

#define RT_SCOPE_UNIVERSE 0
#define RT_SCOPE_SITE     200
#define RT_SCOPE_LINK     253
#define RT_SCOPE_HOST     254
#define RT_SCOPE_NOWHERE  255

#define RTN_UNSPEC      0
#define RTN_UNICAST     1
#define RTN_LOCAL       2
#define RTN_BROADCAST   3
#define RTN_ANYCAST     4
#define RTN_MULTICAST   5
#define RTN_BLACKHOLE   6
#define RTN_UNREACHABLE 7
#define RTN_PROHIBIT    8
#define RTN_THROW       9
#define RTN_NAT         10
#define RTN_XRESOLVE    11

struct ifinfomsg {
    unsigned char  ifi_family;
    unsigned char  __ifi_pad;
    unsigned short ifi_type;
    int            ifi_index;
    unsigned int   ifi_flags;
    unsigned int   ifi_change;
};

struct ifaddrmsg {
    unsigned char ifa_family;
    unsigned char ifa_prefixlen;
    unsigned char ifa_flags;
    unsigned char ifa_scope;
    uint32_t      ifa_index;
};

enum rtattr_type_t {
    IFLA_UNSPEC,
    IFLA_ADDRESS,
    IFLA_BROADCAST,
    IFLA_IFNAME,
    IFLA_MTU,
    IFLA_LINK,
    IFLA_QDISC,
    IFLA_STATS,
    IFLA_COST,
    IFLA_PRIORITY,
    IFLA_MASTER,
    IFLA_WIRELESS,
    IFLA_PROTINFO,
    IFLA_TXQLEN,
    IFLA_MAP,
    IFLA_WEIGHT,
    IFLA_OPERSTATE,
    IFLA_LINKMODE,
    IFLA_LINKINFO,
    IFLA_NET_NS_PID,
    IFLA_IFALIAS,
    IFLA_NUM_VF,
    IFLA_VFINFO_LIST,
    IFLA_STATS64,
    IFLA_VF_PORTS,
    IFLA_PORT_SELF,
    IFLA_AF_SPEC,
    IFLA_GROUP,
    IFLA_NET_NS_FD,
    IFLA_EXT_MASK,
    IFLA_PROMISCUITY,
    IFLA_NUM_TX_QUEUES,
    IFLA_NUM_RX_QUEUES,
    IFLA_CARRIER,
    IFLA_PHYS_PORT_ID,
    IFLA_CARRIER_CHANGES,
    IFLA_PHYS_SWITCH_ID,
    IFLA_LINK_NETNSID,
    IFLA_PHYS_PORT_NAME,
    IFLA_PROTO_DOWN,
    IFLA_GSO_MAX_SEGS,
    IFLA_GSO_MAX_SIZE,
    IFLA_PAD,
    IFLA_XDP,
    IFLA_EVENT,
    IFLA_NEW_NETNSID,
    IFLA_IF_NETNSID,
    IFLA_CARRIER_UP_COUNT,
    IFLA_CARRIER_DOWN_COUNT,
    IFLA_NEW_IFINDEX,
    IFLA_MIN_MTU,
    IFLA_MAX_MTU,
    __IFLA_MAX
};

enum {
    IFA_UNSPEC,
    IFA_ADDRESS,
    IFA_LOCAL,
    IFA_LABEL,
    IFA_BROADCAST,
    IFA_ANYCAST,
    IFA_CACHEINFO,
    IFA_MULTICAST,
    IFA_FLAGS,
    __IFA_MAX
};

enum rt_attr_type_e {
    RTA_UNSPEC,
    RTA_DST,
    RTA_SRC,
    RTA_IIF,
    RTA_OIF,
    RTA_GATEWAY,
    RTA_PRIORITY,
    RTA_PREFSRC,
    RTA_METRICS,
    RTA_MULTIPATH,
    RTA_PROTOINFO,
    RTA_FLOW,
    RTA_CACHEINFO,
    RTA_SESSION,
    RTA_MP_ALGO,
    RTA_TABLE,
    RTA_MARK,
    RTA_MFC_STATS,
    RTA_VIA,
    RTA_NEWDST,
    RTA_PREF,
    RTA_ENCAP_TYPE,
    RTA_ENCAP,
    RTA_EXPIRES,
    RTA_PAD,
    RTA_UID,
    RTA_TTL_PROPAGATE,
    RTA_IP_PROTO,
    RTA_SPORT,
    RTA_DPORT,
    RTA_NH_ID,
    __RTA_MAX
};

#endif /* _LINUX_RTNETLINK_H */
