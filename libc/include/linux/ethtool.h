#ifndef _LINUX_ETHTOOL_H
#define _LINUX_ETHTOOL_H

#include <stdint.h>
#include <linux/sockios.h>

struct ethtool_cmd {
    uint32_t cmd;
    uint32_t supported;
    uint32_t advertising;
    uint16_t speed;
    uint8_t  duplex;
    uint8_t  port;
    uint8_t  phy_address;
    uint8_t  transceiver;
    uint8_t  autoneg;
    uint8_t  mdio_support;
    uint32_t maxtxpkt;
    uint32_t maxrxpkt;
    uint16_t speed_hi;
    uint8_t  eth_tp_mdix;
    uint8_t  eth_tp_mdix_ctrl;
    uint32_t lp_advertising;
    uint32_t reserved[2];
};

#define ETHTOOL_GSET   0x00000001
#define ETHTOOL_SSET   0x00000002
#define DUPLEX_HALF    0x00
#define DUPLEX_FULL    0x01
#define DUPLEX_UNKNOWN 0xff
#define SPEED_UNKNOWN  -1

#endif /* _LINUX_ETHTOOL_H */
