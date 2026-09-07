/*
 * SzpontOS - Transmission Control Protocol (TCP) Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <net/net.h>
#include <net/socket.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

extern socket_t *socket_find_tcp(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port);
extern socket_t *socket_find_tcp_listener(uint32_t local_ip, uint16_t local_port);
extern socket_t *socket_create_child(socket_t *listener, uint32_t remote_ip, uint16_t remote_port);
extern int socket_enqueue_data(socket_t *sock, const void *data, size_t len, uint32_t from_ip, uint16_t from_port);

typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_len;
} tcp_pseudo_header_t;

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip, const tcp_hdr_t *tcp, size_t len) {
    uint32_t sum = 0;

    /* Pseudo-header sum */
    sum += (src_ip & 0xFFFF);
    sum += (src_ip >> 16);
    sum += (dest_ip & 0xFFFF);
    sum += (dest_ip >> 16);
    sum += htons(IP_PROTO_TCP);
    sum += htons((uint16_t)len);

    /* TCP header and payload sum */
    const uint16_t *w = (const uint16_t *)tcp;
    size_t l = len;
    while (l > 1) {
        sum += *w++;
        l -= 2;
    }
    if (l == 1) {
        sum += (uint8_t)(*(const uint8_t *)w);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t res = (uint16_t)(~sum);
    return (res == 0) ? 0xFFFF : res;
}

int tcp_send_segment(uint32_t src_ip, uint16_t src_port, uint32_t dest_ip, uint16_t dest_port, uint32_t seq,
                     uint32_t ack, uint8_t flags, const void *data, size_t len) {
    net_buf_t *buf = net_buf_alloc();
    if (!buf)
        return -1;

    tcp_hdr_t *tcp = (tcp_hdr_t *)buf->data;
    tcp->src_port = htons(src_port);
    tcp->dest_port = htons(dest_port);
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = (sizeof(tcp_hdr_t) / 4) << 4;
    tcp->flags = flags;
    tcp->window_size = htons(65535);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (data && len > 0) {
        memcpy(buf->data + sizeof(tcp_hdr_t), data, len);
    }

    buf->len = sizeof(tcp_hdr_t) + len;
    buf->offset = 0;
    tcp->checksum = tcp_checksum(src_ip, dest_ip, tcp, buf->len);



    return ipv4_output(NULL, dest_ip, IP_PROTO_TCP, buf);
}

void tcp_init(void) {}

void tcp_input(netif_t *netif, net_buf_t *buf) {
    if (!netif || !buf || buf->len < sizeof(tcp_hdr_t)) {
        if (buf)
            net_buf_free(buf);
        return;
    }

    ipv4_hdr_t *ip = (ipv4_hdr_t *)(buf->data + buf->offset - sizeof(ipv4_hdr_t));
    tcp_hdr_t *tcp = (tcp_hdr_t *)(buf->data + buf->offset);

    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dest_port = ntohs(tcp->dest_port);
    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t hlen = (tcp->data_offset >> 4) * 4;
    uint8_t flags = tcp->flags;

    if (hlen < sizeof(tcp_hdr_t) || buf->len < hlen) {
        net_buf_free(buf);
        return;
    }

    const uint8_t *payload = buf->data + buf->offset + hlen;
    size_t payload_len = buf->len - hlen;



    /* 1. Try to find established/connecting socket */
    socket_t *sock = socket_find_tcp(ip->dest_ip, dest_port, ip->src_ip, src_port);

    /* 2. If not found and SYN packet, try to find listening socket */
    if (!sock && (flags & TCP_FLAG_SYN)) {
        socket_t *listener = socket_find_tcp_listener(ip->dest_ip, dest_port);
        if (listener) {
            socket_t *child = socket_create_child(listener, ip->src_ip, src_port);
            if (child) {
                child->rcv_nxt = seq + 1;
                child->snd_nxt = 1000;
                child->tcp_state = TCP_STATE_SYN_RECEIVED;

                /* Send SYN+ACK */
                tcp_send_segment(netif->ip, dest_port, ip->src_ip, src_port, child->snd_nxt, child->rcv_nxt,
                                 TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                child->snd_nxt++;
            }
            net_buf_free(buf);
            return;
        }
    }

    if (!sock) {
        /* Send RST if no socket found for incoming SYN */
        if (!(flags & TCP_FLAG_RST) && (flags & TCP_FLAG_SYN)) {
            tcp_send_segment(netif->ip, dest_port, ip->src_ip, src_port, ack,
                             seq + (payload_len ? (uint32_t)payload_len : 1), TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0);
        }
        net_buf_free(buf);
        return;
    }

    if (flags & TCP_FLAG_RST) {
        sock->tcp_state = TCP_STATE_CLOSED;
        sock->state = SS_CLOSED;
        net_buf_free(buf);
        return;
    }

    if (sock->local_ip == 0 && netif) {
        sock->local_ip = netif->ip;
    }

    /* Process state machine */
    switch (sock->tcp_state) {
    case TCP_STATE_SYN_SENT:
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            sock->rcv_nxt = seq + 1;
            sock->snd_una = ack;
            sock->tcp_state = TCP_STATE_ESTABLISHED;
            sock->state = SS_CONNECTED;

            /* Send ACK */
            tcp_send_segment(sock->local_ip, sock->local_port, sock->remote_ip, sock->remote_port, sock->snd_nxt,
                             sock->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
        }
        break;

    case TCP_STATE_SYN_RECEIVED:
        if (flags & TCP_FLAG_ACK) {
            sock->tcp_state = TCP_STATE_ESTABLISHED;
            sock->state = SS_CONNECTED;
        }
        break;

    case TCP_STATE_ESTABLISHED:
        if (payload_len > 0) {
            socket_enqueue_data(sock, payload, payload_len, ip->src_ip, src_port);
            sock->rcv_nxt += (uint32_t)payload_len;

            /* Send ACK */
            tcp_send_segment(sock->local_ip, sock->local_port, sock->remote_ip, sock->remote_port, sock->snd_nxt,
                             sock->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
        }

        if (flags & TCP_FLAG_FIN) {
            sock->rcv_nxt++;
            sock->tcp_state = TCP_STATE_CLOSE_WAIT;
            sock->state = SS_CLOSED;

            /* Send ACK for FIN */
            tcp_send_segment(sock->local_ip, sock->local_port, sock->remote_ip, sock->remote_port, sock->snd_nxt,
                             sock->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
        }
        break;

    case TCP_STATE_FIN_WAIT_1:
        if (flags & TCP_FLAG_ACK) {
            sock->tcp_state = TCP_STATE_FIN_WAIT_2;
        }
        if (flags & TCP_FLAG_FIN) {
            sock->rcv_nxt++;
            tcp_send_segment(sock->local_ip, sock->local_port, sock->remote_ip, sock->remote_port, sock->snd_nxt,
                             sock->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
            sock->tcp_state = TCP_STATE_TIME_WAIT;
        }
        break;

    case TCP_STATE_FIN_WAIT_2:
        if (flags & TCP_FLAG_FIN) {
            sock->rcv_nxt++;
            tcp_send_segment(sock->local_ip, sock->local_port, sock->remote_ip, sock->remote_port, sock->snd_nxt,
                             sock->rcv_nxt, TCP_FLAG_ACK, NULL, 0);
            sock->tcp_state = TCP_STATE_CLOSED;
            sock->state = SS_CLOSED;
        }
        break;

    case TCP_STATE_LAST_ACK:
        if (flags & TCP_FLAG_ACK) {
            sock->tcp_state = TCP_STATE_CLOSED;
            sock->state = SS_CLOSED;
        }
        break;

    default:
        break;
    }

    net_buf_free(buf);
}
