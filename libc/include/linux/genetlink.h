#ifndef _LINUX_GENETLINK_H
#define _LINUX_GENETLINK_H

#include <linux/netlink.h>

#define NETLINK_GENERIC 16
#define GENL_ID_CTRL 0x10

struct genlmsghdr {
    uint8_t  cmd;
    uint8_t  version;
    uint16_t reserved;
};

#define GENL_HDRLEN ((int) NLMSG_ALIGN(sizeof(struct genlmsghdr)))

#define CTRL_CMD_UNSPEC       0
#define CTRL_CMD_NEWFAMILY    1
#define CTRL_CMD_DELFAMILY    2
#define CTRL_CMD_GETFAMILY    3

#define CTRL_ATTR_UNSPEC      0
#define CTRL_ATTR_FAMILY_ID   1
#define CTRL_ATTR_FAMILY_NAME 2

#endif /* _LINUX_GENETLINK_H */
