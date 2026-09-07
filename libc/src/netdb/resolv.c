/*
 * SzpontOS Libc - FreeBSD-Compatible Stub Resolver Subsystem (resolv.c)
 * (C) Copyright by Szpont Industries. All rights reserved.
 * Inspired by FreeBSD libc/net/res_init.c, res_query.c, res_send.c
 */

#include <resolv.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

/* Global resolver state matching FreeBSD _res */
struct __res_state _res = {
    .retrans = RES_TIMEOUT,
    .retry = RES_DFLRETRY,
    .options = 0,
    .nscount = 0,
    .id = 0x4242,
    .defdname = {0},
    .dnsrch = {NULL},
    .initialized = 0
};

static void trim_line(char *str) {
    char *p = str;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != str) memmove(str, p, strlen(p) + 1);

    size_t len = strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len - 1]) || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

static int split_first_token(const char *line, char *tok, size_t tok_sz, char *rest, size_t rest_sz) {
    while (*line && isspace((unsigned char)*line)) line++;
    if (!*line) return 0;

    size_t i = 0;
    while (*line && !isspace((unsigned char)*line) && i + 1 < tok_sz) {
        tok[i++] = *line++;
    }
    tok[i] = '\0';

    while (*line && isspace((unsigned char)*line)) line++;
    i = 0;
    while (*line && i + 1 < rest_sz) {
        rest[i++] = *line++;
    }
    rest[i] = '\0';
    trim_line(rest);
    return 1;
}

int res_init(void) {
    _res.retrans = RES_TIMEOUT;
    _res.retry = RES_DFLRETRY;
    _res.nscount = 0;
    _res.id = 0x1337;
    memset(_res.defdname, 0, sizeof(_res.defdname));
    memset(_res.nsaddr_list, 0, sizeof(_res.nsaddr_list));

    FILE *fp = fopen("/etc/resolv.conf", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            trim_line(line);
            if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
                continue;

            char keyword[32], val[224];
            if (split_first_token(line, keyword, sizeof(keyword), val, sizeof(val))) {
                if (strcmp(keyword, "nameserver") == 0 && _res.nscount < MAXNS) {
                    struct in_addr addr;
                    if (inet_aton(val, &addr)) {
                        _res.nsaddr_list[_res.nscount].sin_family = AF_INET;
                        _res.nsaddr_list[_res.nscount].sin_port = htons(53);
                        _res.nsaddr_list[_res.nscount].sin_addr = addr;
                        _res.nscount++;
                    }
                } else if (strcmp(keyword, "domain") == 0) {
                    strncpy(_res.defdname, val, sizeof(_res.defdname) - 1);
                } else if (strcmp(keyword, "timeout") == 0) {
                    int t = atoi(val);
                    if (t > 0 && t <= RES_MAXRETRANS)
                        _res.retrans = t;
                } else if (strcmp(keyword, "attempts") == 0) {
                    int a = atoi(val);
                    if (a > 0 && a <= RES_MAXRETRY)
                        _res.retry = a;
                }
            }
        }
        fclose(fp);
    }

    /* Fallback default nameservers if none configured in /etc/resolv.conf */
    if (_res.nscount == 0) {
        const char *fallback_ns[] = {"10.0.2.3", "1.1.1.1", "8.8.8.8"};
        for (size_t i = 0; i < 3 && _res.nscount < MAXNS; i++) {
            _res.nsaddr_list[_res.nscount].sin_family = AF_INET;
            _res.nsaddr_list[_res.nscount].sin_port = htons(53);
            inet_aton(fallback_ns[i], &_res.nsaddr_list[_res.nscount].sin_addr);
            _res.nscount++;
        }
    }

    _res.initialized = 1;
    return 0;
}

void res_close(void) {
    _res.initialized = 0;
}

/*
 * DNS RFC 1035 Query Packet Formatter
 */
int res_mkquery(int op, const char *dname, int class, int type,
                const unsigned char *data, int datalen,
                const unsigned char *newrr,
                unsigned char *buf, int buflen) {
    (void)data;
    (void)datalen;
    (void)newrr;

    if (!dname || !buf || buflen < HFIXEDSZ + QFIXEDSZ + 256)
        return -1;

    memset(buf, 0, buflen);

    HEADER *hp = (HEADER *)buf;
    hp->id = htons(++_res.id);
    hp->flags = htons(0x0100 | ((op & 0x0F) << 11)); /* RD = 1, standard query */
    hp->qdcount = htons(1);
    hp->ancount = 0;
    hp->nscount = 0;
    hp->arcount = 0;

    unsigned char *qptr = buf + HFIXEDSZ;
    const char *src = dname;

    while (*src) {
        const char *dot = strchr(src, '.');
        size_t len = dot ? (size_t)(dot - src) : strlen(src);
        if (len > 63 || (qptr + len + 1) >= (buf + buflen))
            return -1;

        *qptr++ = (unsigned char)len;
        memcpy(qptr, src, len);
        qptr += len;

        src += len;
        if (*src == '.') src++;
    }
    *qptr++ = 0; /* Terminating zero length for root label */

    /* QTYPE */
    *qptr++ = (unsigned char)((type >> 8) & 0xFF);
    *qptr++ = (unsigned char)(type & 0xFF);

    /* QCLASS */
    *qptr++ = (unsigned char)((class >> 8) & 0xFF);
    *qptr++ = (unsigned char)(class & 0xFF);

    return (int)(qptr - buf);
}

/*
 * Low-level DNS Packet Transport (UDP port 53 with retry & timeout)
 */
int res_send(const unsigned char *msg, int msglen,
             unsigned char *ans, int anssiz) {
    if (!_res.initialized)
        res_init();

    int timeout_sec = _res.retrans > 0 ? _res.retrans : RES_TIMEOUT;
    int max_retries = _res.retry > 0 ? _res.retry : RES_DFLRETRY;

    for (int retry = 0; retry < max_retries; retry++) {
        for (int ns = 0; ns < _res.nscount; ns++) {
            struct sockaddr_in *dest = &_res.nsaddr_list[ns];

            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0)
                continue;

            ssize_t sent = sendto(sockfd, msg, (size_t)msglen, 0,
                                  (struct sockaddr *)dest, sizeof(struct sockaddr_in));
            if (sent != msglen) {
                close(sockfd);
                continue;
            }

            struct pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            int poll_ret = poll(&pfd, 1, timeout_sec * 1000);
            if (poll_ret > 0 && (pfd.revents & POLLIN)) {
                socklen_t fromlen = sizeof(struct sockaddr_in);
                struct sockaddr_in from;
                ssize_t received = recvfrom(sockfd, ans, (size_t)anssiz, 0,
                                            (struct sockaddr *)&from, &fromlen);
                if (received > HFIXEDSZ) {
                    close(sockfd);
                    return (int)received;
                }
            }
            close(sockfd);
        }
    }

    return -1;
}

int res_query(const char *dname, int class, int type,
              unsigned char *ans, int anssiz) {
    if (!dname || !ans)
        return -1;

    unsigned char query_buf[PACKETSZ];
    int qlen = res_mkquery(QUERY, dname, class, type, NULL, 0, NULL, query_buf, sizeof(query_buf));
    if (qlen <= 0)
        return -1;

    return res_send(query_buf, qlen, ans, anssiz);
}

int res_search(const char *dname, int class, int type,
               unsigned char *ans, int anssiz) {
    return res_query(dname, class, type, ans, anssiz);
}

/*
 * Static Host Resolution (/etc/hosts)
 */
int hosts_lookup(const char *name, uint32_t *out_ip) {
    if (!name || !*name || !out_ip)
        return -1;

    FILE *fp = fopen("/etc/hosts", "r");
    if (!fp)
        return -1;

    char line[512];
    int found = -1;

    while (fgets(line, sizeof(line), fp)) {
        trim_line(line);
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
            continue;

        char ip_str[64];
        char hosts_str[448];
        if (split_first_token(line, ip_str, sizeof(ip_str), hosts_str, sizeof(hosts_str))) {
            struct in_addr in;
            if (!inet_aton(ip_str, &in))
                continue;

            char *token = strtok(hosts_str, " \t");
            while (token) {
                if (strcasecmp(token, name) == 0) {
                    *out_ip = in.s_addr;
                    found = 0;
                    break;
                }
                token = strtok(NULL, " \t");
            }
            if (found == 0)
                break;
        }
    }

    fclose(fp);
    return found;
}

/*
 * Name Service Switch (/etc/nsswitch.conf)
 */
#define NSS_SRC_FILES 1
#define NSS_SRC_DNS   2

int nss_get_hosts_order(int *order, int max_order) {
    if (!order || max_order < 2)
        return 0;

    /* Defaults */
    order[0] = NSS_SRC_FILES;
    order[1] = NSS_SRC_DNS;
    int count = 2;

    FILE *fp = fopen("/etc/nsswitch.conf", "r");
    if (!fp)
        return count;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        trim_line(line);
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
            continue;

        char db[32], sources[224];
        if (split_first_token(line, db, sizeof(db), sources, sizeof(sources))) {
            /* Strip colon from db if present */
            size_t dblen = strlen(db);
            if (dblen > 0 && db[dblen - 1] == ':')
                db[dblen - 1] = '\0';

            if (strcmp(db, "hosts") == 0) {
                count = 0;
                char *tok = strtok(sources, " \t");
                while (tok && count < max_order) {
                    if (strcmp(tok, "files") == 0) {
                        order[count++] = NSS_SRC_FILES;
                    } else if (strcmp(tok, "dns") == 0) {
                        order[count++] = NSS_SRC_DNS;
                    }
                    tok = strtok(NULL, " \t");
                }
                break;
            }
        }
    }

    fclose(fp);
    return count > 0 ? count : 2;
}

int dn_expand(const unsigned char *msg, const unsigned char *eomorig,
              const unsigned char *comp_dn, char *exp_dn, int length) {
    if (!comp_dn || !exp_dn || length <= 0)
        return -1;
    int len = 0;
    const unsigned char *cp = comp_dn;
    while (*cp != 0 && len < length - 1) {
        int label_len = *cp++;
        if ((label_len & 0xc0) == 0xc0) {
            int offset = ((label_len & 0x3f) << 8) | *cp++;
            if (msg && eomorig && (msg + offset < eomorig)) {
                cp = msg + offset;
                continue;
            }
            break;
        }
        for (int i = 0; i < label_len && len < length - 1; i++) {
            exp_dn[len++] = *cp++;
        }
        if (*cp != 0 && len < length - 1)
            exp_dn[len++] = '.';
    }
    exp_dn[len] = '\0';
    return (int)(cp - comp_dn);
}
