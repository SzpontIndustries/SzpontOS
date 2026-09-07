/*
 * SzpontOS Libc - Berkeley Sockets POSIX Implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <sys/socket.h>
#include <sys/syscall.h>
#include <errno.h>
#include <unistd.h>

int socket(int domain, int type, int protocol) {
    int64_t ret = __syscall3(SYS_socket, domain, type, protocol);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    int64_t ret = __syscall4(SYS_socketpair, domain, type, protocol, (int64_t)sv);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int64_t ret = __syscall3(SYS_bind, sockfd, (int64_t)addr, addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int listen(int sockfd, int backlog) {
    int64_t ret = __syscall2(SYS_listen, sockfd, backlog);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int64_t ret = __syscall3(SYS_accept, sockfd, (int64_t)addr, (int64_t)addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int64_t ret = __syscall3(SYS_connect, sockfd, (int64_t)addr, addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int64_t ret = __syscall3(SYS_getsockname, sockfd, (int64_t)addr, (int64_t)addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int64_t ret = __syscall3(SYS_getpeername, sockfd, (int64_t)addr, (int64_t)addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    return sendto(sockfd, buf, len, flags, NULL, 0);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
               socklen_t addrlen) {
    int64_t ret = __syscall6(SYS_sendto, sockfd, (int64_t)buf, (int64_t)len, flags, (int64_t)dest_addr, addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    int64_t ret =
        __syscall6(SYS_recvfrom, sockfd, (int64_t)buf, (int64_t)len, flags, (int64_t)src_addr, (int64_t)addrlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    int64_t ret = __syscall3(SYS_sendmsg, sockfd, (int64_t)msg, flags);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    int64_t ret = __syscall3(SYS_recvmsg, sockfd, (int64_t)msg, flags);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    int64_t ret = __syscall5(SYS_setsockopt, sockfd, level, optname, (int64_t)optval, optlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    int64_t ret = __syscall5(SYS_getsockopt, sockfd, level, optname, (int64_t)optval, (int64_t)optlen);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int shutdown(int sockfd, int how) {
    int64_t ret = __syscall2(SYS_shutdown, sockfd, how);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

#undef htons
#undef ntohs
#undef htonl
#undef ntohl

uint16_t htons(uint16_t x) {
    return __builtin_bswap16(x);
}

uint16_t ntohs(uint16_t x) {
    return __builtin_bswap16(x);
}

uint32_t htonl(uint32_t x) {
    return __builtin_bswap32(x);
}

uint32_t ntohl(uint32_t x) {
    return __builtin_bswap32(x);
}

