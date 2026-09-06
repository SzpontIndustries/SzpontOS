#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>

void *memcpy(void *dest, const void *src, size_t n) {
    if (!dest || !src || n == 0)
        return dest;
#if defined(__x86_64__)
    void *d = dest;
    const void *s = src;
    __asm__ volatile("mov %2, %%rcx\n\t"
                     "shr $3, %%rcx\n\t"
                     "rep movsq\n\t"
                     "mov %2, %%rcx\n\t"
                     "and $7, %%rcx\n\t"
                     "rep movsb"
                     : "+D"(d), "+S"(s)
                     : "r"(n)
                     : "rcx", "memory");
    return dest;
#else
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    size_t qwords = n >> 3;
    uint64_t *d64 = (uint64_t *)d;
    const uint64_t *s64 = (const uint64_t *)s;
    while (qwords--) {
        *d64++ = *s64++;
    }
    d = (uint8_t *)d64;
    s = (const uint8_t *)s64;
    size_t rem = n & 7;
    while (rem--) {
        *d++ = *s++;
    }
    return dest;
#endif
}

void *memset(void *s, int c, size_t n) {
    if (!s || n == 0)
        return s;
    uint8_t byte = (uint8_t)c;
#if defined(__x86_64__)
    void *d = s;
    uint64_t val64 = 0x0101010101010101ULL * byte;
    __asm__ volatile("mov %2, %%rcx\n\t"
                     "shr $3, %%rcx\n\t"
                     "rep stosq\n\t"
                     "mov %2, %%rcx\n\t"
                     "and $7, %%rcx\n\t"
                     "rep stosb"
                     : "+D"(d)
                     : "a"(val64), "r"(n)
                     : "rcx", "memory");
    return s;
#else
    uint8_t *p = (uint8_t *)s;
    uint64_t val64 = 0x0101010101010101ULL * byte;
    size_t qwords = n >> 3;
    uint64_t *p64 = (uint64_t *)p;
    while (qwords--) {
        *p64++ = val64;
    }
    p = (uint8_t *)p64;
    size_t rem = n & 7;
    while (rem--) {
        *p++ = byte;
    }
    return s;
#endif
}

void *memmove(void *dest, const void *src, size_t n) {
    if (!dest || !src || n == 0 || dest == src)
        return dest;

    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s || d >= (s + n)) {
        return memcpy(dest, src, n);
    }

    /* Backward copy for overlapping regions */
    d += n;
    s += n;
    size_t rem = n & 7;
    while (rem--) {
        *--d = *--s;
    }
    size_t qwords = n >> 3;
    uint64_t *d64 = (uint64_t *)d;
    const uint64_t *s64 = (const uint64_t *)s;
    while (qwords--) {
        *--d64 = *--s64;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == uc)
            return (void *)(p + i);
    }
    return NULL;
}

void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = n; i > 0; i--) {
        if (p[i - 1] == uc)
            return (void *)(p + i - 1);
    }
    return NULL;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) {
        len++;
    }
    return len;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (s && len < maxlen && s[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

int strcoll(const char *s1, const char *s2) {
    return strcmp(s1, s2);
}

size_t strxfrm(char *dest, const char *src, size_t n) {
    size_t len = strlen(src);
    if (n > 0 && dest) {
        strncpy(dest, src, n);
        dest[n - 1] = '\0';
    }
    return len;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (tolower(*(unsigned char *)s1) == tolower(*(unsigned char *)s2))) {
        s1++;
        s2++;
    }
    return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int diff = tolower(*(unsigned char *)(s1 + i)) - tolower(*(unsigned char *)(s2 + i));
        if (diff != 0 || s1[i] == '\0') {
            return diff;
        }
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest + strlen(dest);
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest + strlen(dest);
    size_t i = 0;
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    *d = '\0';
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (*s == (char)c) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    return (last != NULL || *s == (char)c) ? (char *)(last ? last : s) : NULL;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n)
                return (char *)haystack;
        }
    }
    return NULL;
}

char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        if (tolower(*(unsigned char *)haystack) == tolower(*(unsigned char *)needle)) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && tolower(*(unsigned char *)h) == tolower(*(unsigned char *)n)) {
                h++;
                n++;
            }
            if (!*n)
                return (char *)haystack;
        }
    }
    return NULL;
}

char *strdup(const char *s) {
    if (!s)
        return NULL;
    size_t len = strlen(s) + 1;
    char *dup = (char *)malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

char *strndup(const char *s, size_t n) {
    if (!s)
        return NULL;
    size_t len = strnlen(s, n);
    char *dup = (char *)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

size_t strspn(const char *s, const char *accept) {
    size_t count = 0;
    while (*s && strchr(accept, *s)) {
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    while (*s && !strchr(reject, *s)) {
        count++;
        s++;
    }
    return count;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        if (strchr(accept, *s))
            return (char *)s;
        s++;
    }
    return NULL;
}

char *strsep(char **stringp, const char *delim) {
    if (!stringp || !*stringp)
        return NULL;
    char *start = *stringp;
    char *p = strpbrk(start, delim);
    if (p) {
        *p = '\0';
        *stringp = p + 1;
    } else {
        *stringp = NULL;
    }
    return start;
}

static char *g_strtok_ctx = NULL;

char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (!saveptr)
        return NULL;
    char *s = str ? str : *saveptr;
    if (!s)
        return NULL;

    s += strspn(s, delim);
    if (!*s) {
        *saveptr = NULL;
        return NULL;
    }

    char *token = s;
    s = strpbrk(token, delim);
    if (s) {
        *s = '\0';
        *saveptr = s + 1;
    } else {
        *saveptr = NULL;
    }
    return token;
}

char *strtok(char *str, const char *delim) {
    return strtok_r(str, delim, &g_strtok_ctx);
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len = strlen(src);
    if (size > 0) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dst_len = strlen(dst);
    size_t src_len = strlen(src);
    if (dst_len >= size)
        return size + src_len;
    size_t space = size - dst_len - 1;
    size_t copy_len = (src_len > space) ? space : src_len;
    memcpy(dst + dst_len, src, copy_len);
    dst[dst_len + copy_len] = '\0';
    return dst_len + src_len;
}

char *strerror(int errnum) {
    switch (errnum) {
    case 0:
        return "Success";
    case EPERM:
        return "Operation not permitted";
    case ENOENT:
        return "No such file or directory";
    case ESRCH:
        return "No such process";
    case EINTR:
        return "Interrupted system call";
    case EIO:
        return "Input/output error";
    case EBADF:
        return "Bad file descriptor";
    case ENOMEM:
        return "Cannot allocate memory";
    case EACCES:
        return "Permission denied";
    case EEXIST:
        return "File exists";
    case ENOTDIR:
        return "Not a directory";
    case EISDIR:
        return "Is a directory";
    case EINVAL:
        return "Invalid argument";
    case ENOSYS:
        return "Function not implemented";
    default:
        return "Unknown error";
    }
}

char *strsignal(int sig) {
    switch (sig) {
    case 1:
        return "Hangup";
    case 2:
        return "Interrupt";
    case 3:
        return "Quit";
    case 9:
        return "Killed";
    case 11:
        return "Segmentation fault";
    case 15:
        return "Terminated";
    default:
        return "Signal";
    }
}

char *basename(char *path) {
    if (!path || !*path)
        return ".";
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

char *dirname(char *path) {
    if (!path || !*path)
        return ".";
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    char *slash = strrchr(path, '/');
    if (!slash)
        return ".";
    if (slash == path)
        return "/";
    *slash = '\0';
    return path;
}

int isalnum(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int iscntrl(int c) {
    return (c >= 0 && c < 32) || c == 127;
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isgraph(int c) {
    return (c > 32 && c < 127);
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int isprint(int c) {
    return (c >= 32 && c < 127);
}

int ispunct(int c) {
    return isgraph(c) && !isalnum(c);
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int isxdigit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

int toupper(int c) {
    return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c;
}

int isascii(int c) {
    return (c >= 0 && c <= 127);
}

int toascii(int c) {
    return (c & 0x7F);
}

int isblank(int c) {
    return (c == ' ' || c == '\t');
}

void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen) {
    if (!haystack || !needle)
        return NULL;
    if (needlelen == 0)
        return (void *)haystack;
    if (haystacklen < needlelen)
        return NULL;

    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    size_t max = haystacklen - needlelen;

    for (size_t i = 0; i <= max; i++) {
        if (h[i] == n[0] && memcmp(&h[i], n, needlelen) == 0) {
            return (void *)&h[i];
        }
    }
    return NULL;
}

int ffs(int i) {
    return __builtin_ffs(i);
}

int ffsl(long int i) {
    return __builtin_ffsl(i);
}

char *strchrnul(const char *s, int c) {
    char ch = (char)c;
    while (*s && *s != ch)
        s++;
    return (char *)s;
}

void *memccpy(void *dest, const void *src, int c, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char ch = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
        if (s[i] == ch)
            return d + i + 1;
    }
    return NULL;
}

