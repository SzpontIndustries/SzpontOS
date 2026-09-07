#ifndef _LINUX_WIRELESS_H
#define _LINUX_WIRELESS_H

#include <sys/socket.h>
#include <linux/if.h>

#define SIOCGIWNAME       0x8B01
#define SIOCGIWNWID       0x8B03
#define SIOCGIWFREQ       0x8B05
#define SIOCGIWMODE       0x8B07
#define SIOCGIWSENS       0x8B09
#define SIOCGIWRANGE      0x8B0B
#define SIOCGIWPRIV       0x8B0D
#define SIOCGIWSTATS      0x8B0F
#define SIOCGIWSPY        0x8B11
#define SIOCGIWTHRSPY     0x8B13
#define SIOCGIWAP         0x8B15
#define SIOCGIWESSID      0x8B1B
#define SIOCGIWRATE       0x8B21
#define SIOCGIWRTS        0x8B23
#define SIOCGIWFRAG       0x8B25
#define SIOCGIWTXPOW      0x8B27
#define SIOCGIWRETRY      0x8B29
#define SIOCGIWPOWER      0x8B2D
#define SIOCGIWENCODEEXT  0x8B35

#define IW_ESSID_MAX_SIZE 32

struct iw_param {
    int32_t value;
    uint8_t fixed;
    uint8_t disabled;
    uint16_t flags;
};

struct iw_point {
    void *pointer;
    uint16_t length;
    uint16_t flags;
};

struct iw_freq {
    int32_t m;
    int16_t e;
    uint8_t i;
    uint8_t flags;
};

struct iw_quality {
    uint8_t qual;
    uint8_t level;
    uint8_t noise;
    uint8_t updated;
};

struct iwreq_data {
    char name[16];
    struct iw_point essid;
    struct iw_param nwid;
    struct iw_freq freq;
    struct iw_param sens;
    struct iw_param bitrate;
    struct iw_param txpower;
    struct iw_param rts;
    struct iw_param frag;
    struct iw_param mode;
    struct iw_param retry;
    struct iw_point encoding;
    struct iw_param power;
    struct iw_quality qual;
    struct sockaddr ap_addr;
    struct sockaddr addr;
    struct iw_param param;
    struct iw_point data;
};

struct iwreq {
    union {
        char ifrn_name[16];
    } ifr_ifrn;
    struct iwreq_data u;
};
#define ifr_name ifr_ifrn.ifrn_name

#define IW_ENCODE_ALG_NONE      0
#define IW_ENCODE_ALG_WEP       1
#define IW_ENCODE_ALG_TKIP      2
#define IW_ENCODE_ALG_CCMP      3
#define IW_ENCODE_ALG_PMK       4
#define IW_ENCODE_ALG_AES_CMAC  5

struct iw_statistics {
    uint16_t status;
    struct iw_quality qual;
    struct {
        uint32_t invalid_nwid;
        uint32_t invalid_crypt;
        uint32_t invalid_misc;
        uint32_t missed_beacon;
    } discard;
    struct {
        uint32_t retries;
        uint32_t failed;
    } tx;
};

struct iw_encode_ext {
    uint32_t ext_flags;
    uint8_t  tx_seq[8];
    uint8_t  rx_seq[8];
    uint8_t  alg;
    uint8_t  key_len;
    uint16_t reserved;
    uint8_t  key[32];
};

#endif /* _LINUX_WIRELESS_H */
