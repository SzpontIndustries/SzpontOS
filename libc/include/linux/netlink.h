#ifndef _LINUX_NETLINK_H
#define _LINUX_NETLINK_H

#include <stdint.h>
#include <sys/socket.h>

#define NETLINK_ROUTE 0

struct sockaddr_nl {
    sa_family_t nl_family;
    uint16_t    nl_pad;
    uint32_t    nl_pid;
    uint32_t    nl_groups;
};

struct nlmsghdr {
    uint32_t nlmsg_len;
    uint16_t nlmsg_type;
    uint16_t nlmsg_flags;
    uint32_t nlmsg_seq;
    uint32_t nlmsg_pid;
};

#define NLM_F_REQUEST 0x01
#define NLM_F_MULTI   0x02
#define NLM_F_ACK     0x04
#define NLM_F_ECHO    0x08
#define NLM_F_DUMP_INTR 0x10
#define NLM_F_DUMP_FILTERED 0x20

#define NLM_F_ROOT    0x100
#define NLM_F_MATCH   0x200
#define NLM_F_ATOMIC  0x400
#define NLM_F_DUMP    (NLM_F_ROOT|NLM_F_MATCH)

#define NLMSG_ALIGNTO   4U
#define NLMSG_ALIGN(len) (((len)+NLMSG_ALIGNTO-1) & ~(NLMSG_ALIGNTO-1))
#define NLMSG_HDRLEN     ((int) NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_HDRLEN)
#define NLMSG_SPACE(len)  NLMSG_ALIGN(NLMSG_LENGTH(len))
#define NLMSG_DATA(nlh)   ((void*)(((char*)nlh) + NLMSG_HDRLEN))
#define NLMSG_NEXT(nlh,len) ((len) -= NLMSG_ALIGN((nlh)->nlmsg_len), \
                              (struct nlmsghdr*)(((char*)(nlh)) + NLMSG_ALIGN((nlh)->nlmsg_len)))
#define NLMSG_OK(nlh,len) ((len) >= (int)sizeof(struct nlmsghdr) && \
                           (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
                           (int)(nlh)->nlmsg_len <= (len))
#define NLMSG_PAYLOAD(nlh,len) ((nlh)->nlmsg_len - NLMSG_SPACE((len)))

#define NLMSG_NOOP    0x1
#define NLMSG_ERROR   0x2
#define NLMSG_DONE    0x3
#define NLMSG_OVERRUN 0x4
#define NLMSG_MIN_TYPE 0x10

struct nlattr {
    uint16_t nla_len;
    uint16_t nla_type;
};

#define NLA_F_NESTED        (1 << 15)
#define NLA_F_NET_BYTEORDER (1 << 14)
#define NLA_TYPE_MASK       (~(NLA_F_NESTED | NLA_F_NET_BYTEORDER))

#define NLA_ALIGNTO     4U
#define NLA_ALIGN(len)  (((len)+NLA_ALIGNTO-1) & ~(NLA_ALIGNTO-1))
#define NLA_HDRLEN      ((int) NLA_ALIGN(sizeof(struct nlattr)))
#define NLA_DATA(nla)   ((void*)(((char*)nla) + NLA_HDRLEN))
#define NLA_PAYLOAD(nla) ((int)((nla)->nla_len) - NLA_HDRLEN)

#endif /* _LINUX_NETLINK_H */

struct nlmsgerr {
    int error;
    struct nlmsghdr msg;
};

