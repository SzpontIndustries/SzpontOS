/*
 * SzpontOS - Internet Protocol Version 4 (IPv4) Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <net/net.h>
#include <sched/sched.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

static uint16_t g_ip_id = 0;

uint16_t ipv4_checksum(const void *data, size_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

void ipv4_init(void) {
    g_ip_id = 1;
}

void ipv4_input(netif_t *netif, net_buf_t *buf) {
    if (!netif || !buf || buf->len < sizeof(ipv4_hdr_t)) {
        if (buf)
            net_buf_free(buf);
        return;
    }

    ipv4_hdr_t *ip = (ipv4_hdr_t *)(buf->data + buf->offset);
    uint8_t version = ip->version_ihl >> 4;
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;

    if (version != 4 || ihl < sizeof(ipv4_hdr_t) || buf->len < ihl) {
        net_buf_free(buf);
        return;
    }

    /* Check if packet is for us (or broadcast) */
    if (ip->dest_ip != netif->ip && ip->dest_ip != 0xFFFFFFFF && !(netif->flags & NETIF_FLAG_LOOPBACK)) {
        net_buf_free(buf);
        return;
    }

    uint16_t total_len = ntohs(ip->total_len);
    if (total_len > buf->len || total_len < ihl) {
        net_buf_free(buf);
        return;
    }

    uint8_t proto = ip->protocol;

    /* Advance buffer past IPv4 header */
    buf->offset += ihl;
    buf->len = total_len - ihl;

    if (proto == IP_PROTO_ICMP) {
        icmp_input(netif, buf);
    } else if (proto == IP_PROTO_UDP) {
        udp_input(netif, buf);
    } else if (proto == IP_PROTO_TCP) {
        tcp_input(netif, buf);
    } else {
        net_buf_free(buf);
    }
}

int ipv4_output(netif_t *netif, uint32_t dest_ip, uint8_t proto, net_buf_t *payload) {
    if (!payload)
        return -1;

    /* If netif not provided, determine by routing */
    if (!netif) {
        /* Loopback check (127.0.0.0/8 or any local interface IP) */
        if ((dest_ip & 0xFF) == 127 || netif_find_by_ip(dest_ip) != NULL) {
            netif = netif_get_loopback();
        } else {
            netif = netif_get_default();
        }
    }

    if (!netif) {
        net_buf_free(payload);
        return -1;
    }

    /* Determine next hop IP */
    uint32_t next_hop_ip = dest_ip;
    if (!(netif->flags & NETIF_FLAG_LOOPBACK)) {
        /* If destination is not in our subnet, route via gateway */
        if ((dest_ip & netif->netmask) != (netif->ip & netif->netmask)) {
            next_hop_ip = netif->gateway;
        }
    }

    /* Prepare Ethernet Destination MAC */
    uint8_t dst_mac[ETH_ALEN];
    if (netif->flags & NETIF_FLAG_LOOPBACK) {
        memset(dst_mac, 0, ETH_ALEN);
    } else {
        if (dest_ip == 0xFFFFFFFF) {
            memset(dst_mac, 0xFF, ETH_ALEN);
        } else if (!arp_lookup(next_hop_ip, dst_mac)) {
            arp_send_request(netif, next_hop_ip);
            for (int r = 0; r < 20; r++) {
                netif_poll_all();
                if (arp_lookup(next_hop_ip, dst_mac)) {
                    break;
                }
                thread_sleep(1);
            }
            if (!arp_lookup(next_hop_ip, dst_mac)) {
                net_buf_free(payload);
                return -1;
            }
        }
    }

    /* Build full packet in a new buffer */
    net_buf_t *tx_buf = net_buf_alloc();
    if (!tx_buf) {
        net_buf_free(payload);
        return -1;
    }

    /* 1. Ethernet Header */
    eth_hdr_t *eth = (eth_hdr_t *)tx_buf->data;
    memcpy(eth->dest, dst_mac, ETH_ALEN);
    memcpy(eth->src, netif->mac, ETH_ALEN);
    eth->type = htons(ETH_P_IP);

    /* 2. IPv4 Header */
    ipv4_hdr_t *ip = (ipv4_hdr_t *)(tx_buf->data + sizeof(eth_hdr_t));
    ip->version_ihl = (4 << 4) | (sizeof(ipv4_hdr_t) / 4);
    ip->tos = 0;
    ip->total_len = htons((uint16_t)(sizeof(ipv4_hdr_t) + payload->len));
    ip->id = htons(g_ip_id++);
    ip->flags_offset = htons(0x4000); /* Don't Fragment */
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src_ip = netif->ip;
    ip->dest_ip = dest_ip;
    ip->checksum = ipv4_checksum(ip, sizeof(ipv4_hdr_t));

    /* 3. Copy Payload */
    memcpy(tx_buf->data + sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t), payload->data + payload->offset, payload->len);

    tx_buf->len = sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + payload->len;
    tx_buf->offset = 0;

    net_buf_free(payload);
    return netif_output(netif, tx_buf);
}
