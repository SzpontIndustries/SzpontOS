#ifndef SZPONTOS_NET_SOCKET_H
#define SZPONTOS_NET_SOCKET_H

#include <kernel/types.h>
#include <kernel/spinlock.h>
#include <net/net.h>
#include <fs/vfs.h>

/* Address Families */
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_LOCAL 1
#define AF_INET 2
#define AF_INET6 10

/* Socket Types */
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3

/* Protocol constants */
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* Socket States */
typedef enum {
    SS_UNCONNECTED = 0,
    SS_BIND,
    SS_LISTENING,
    SS_CONNECTING,
    SS_CONNECTED,
    SS_DISCONNECTING,
    SS_CLOSED
} sock_state_t;

/* TCP States */
typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

/* Standard POSIX sockaddr structures for Kernel */
struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct sockaddr_un {
    uint16_t sun_family;
    char sun_path[108];
};

#define SOCK_RX_BUF_SIZE 65536
#define SOCK_TX_BUF_SIZE 65536

typedef struct socket {
    int domain;
    int type;
    int protocol;
    sock_state_t state;
    tcp_state_t tcp_state;

    /* Addressing */
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    /* Unix Domain socket path */
    char unix_path[108];
    struct socket *peer;

    /* TCP sequence and acknowledgment */
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_wnd;
    uint32_t rcv_nxt;
    uint32_t rcv_wnd;

    /* Buffers */
    uint8_t rx_buf[SOCK_RX_BUF_SIZE];
    size_t rx_head;
    size_t rx_tail;
    size_t rx_len;

    /* Listen backlog */
    int backlog;
    struct socket *accept_queue[16];
    size_t accept_count;

    /* SCM_RIGHTS file descriptor passing for AF_UNIX */
    file_descriptor_t *passed_fds[8];
    size_t passed_fd_count;

    spinlock_t lock;
    vfs_node_t *vfs_node;
    struct socket *next;
} socket_t;

void socket_subsystem_init(void);
socket_t *socket_create(int domain, int type, int protocol);
void socket_destroy(socket_t *sock);
socket_t *socket_find_udp(uint32_t local_ip, uint16_t local_port);
socket_t *socket_find_tcp(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port);
socket_t *socket_find_icmp(uint32_t local_ip);
int socket_enqueue_data(socket_t *sock, const void *data, size_t len, uint32_t from_ip, uint16_t from_port);

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct msghdr {
    void *msg_name;
    uint32_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
    /* unsigned char cmsg_data[]; */
};

#define SOL_SOCKET 1
#define SCM_RIGHTS 1
#define UNIX_MAX_PASSED_FDS 8

#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_DATA(cmsg) ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)   (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))

/* Syscall implementations */
int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const struct sockaddr *addr, uint32_t addrlen);
int sys_connect(int fd, const struct sockaddr *addr, uint32_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, struct sockaddr *addr, uint32_t *addrlen);
ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, uint32_t addrlen);
ssize_t sys_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src_addr, uint32_t *addrlen);
ssize_t sys_sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t sys_recvmsg(int fd, struct msghdr *msg, int flags);
int sys_shutdown(int fd, int how);
int sys_getsockname(int fd, struct sockaddr *addr, uint32_t *addrlen);
int sys_getpeername(int fd, struct sockaddr *addr, uint32_t *addrlen);
int sys_setsockopt(int fd, int level, int optname, const void *optval, uint32_t optlen);
int sys_getsockopt(int fd, int level, int optname, void *optval, uint32_t *optlen);
int sys_socketpair(int domain, int type, int protocol, int sv[2]);

#endif /* SZPONTOS_NET_SOCKET_H */
