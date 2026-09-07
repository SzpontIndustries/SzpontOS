#ifndef _LINUX_IF_ADDR_H
#define _LINUX_IF_ADDR_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define IFA_F_TEMPORARY       0x01
#define IFA_F_NODAD           0x02
#define IFA_F_OPTIMISTIC      0x04
#define IFA_F_DADFAILED       0x08
#define IFA_F_HOMEADDRESS     0x10
#define IFA_F_DEPRECATED      0x20
#define IFA_F_TENTATIVE       0x40
#define IFA_F_PERMANENT       0x80
#define IFA_F_MANAGETEMPADDR  0x100
#define IFA_F_NOPREFIXROUTE   0x200
#define IFA_F_MCAUTOJOIN      0x400
#define IFA_F_STABLE_PRIVACY  0x800

#endif /* _LINUX_IF_ADDR_H */
