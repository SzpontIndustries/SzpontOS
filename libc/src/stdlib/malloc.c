#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define ALIGNMENT 16
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

#define HEADER_SIZE ALIGN_UP(sizeof(block_header_t), ALIGNMENT)

static block_header_t *g_head = NULL;

void *malloc(size_t size) {
    if (size == 0)
        size = ALIGNMENT;
    if (size > (size_t)INTPTR_MAX - HEADER_SIZE - (ALIGNMENT - 1)) {
        errno = ENOMEM;
        return NULL;
    }
    size = ALIGN_UP(size, ALIGNMENT);

    /* Search free list with best-fit / first-fit with splitting */
    block_header_t *curr = g_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Split block if significantly larger */
            if (curr->size >= size + HEADER_SIZE + ALIGNMENT) {
                block_header_t *split = (block_header_t *)((uintptr_t)curr + HEADER_SIZE + size);
                split->size = curr->size - size - HEADER_SIZE;
                split->is_free = 1;
                split->next = curr->next;
                split->prev = curr;
                if (curr->next)
                    curr->next->prev = split;
                curr->next = split;
                curr->size = size;
            }
            curr->is_free = 0;
            return (void *)((uintptr_t)curr + HEADER_SIZE);
        }
        curr = curr->next;
    }

    /* Request memory from OS via sbrk */
    size_t total_size = size + HEADER_SIZE;
    void *p = sbrk((intptr_t)total_size);
    if (p == (void *)-1) {
        return NULL;
    }

    block_header_t *new_block = (block_header_t *)p;
    new_block->size = size;
    new_block->is_free = 0;
    new_block->next = NULL;
    new_block->prev = NULL;

    if (!g_head) {
        g_head = new_block;
    } else {
        block_header_t *last = g_head;
        while (last->next)
            last = last->next;
        last->next = new_block;
        new_block->prev = last;
    }

    return (void *)((uintptr_t)new_block + HEADER_SIZE);
}

void free(void *ptr) {
    if (!ptr)
        return;
    block_header_t *block = (block_header_t *)((uintptr_t)ptr - HEADER_SIZE);
    block->is_free = 1;

    /* Coalesce next */
    if (block->next && block->next->is_free) {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
    }

    /* Coalesce prev */
    if (block->prev && block->prev->is_free) {
        block->prev->size += HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
    }
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)
        return malloc(size);
    if (size == 0)
        size = ALIGNMENT;

    if (size > (size_t)INTPTR_MAX - HEADER_SIZE - (ALIGNMENT - 1)) {
        errno = ENOMEM;
        return NULL;
    }
    size = ALIGN_UP(size, ALIGNMENT);
    block_header_t *block = (block_header_t *)((uintptr_t)ptr - HEADER_SIZE);
    if (block->size >= size) {
        return ptr;
    }

    /* Try to expand into next free block if available */
    if (block->next && block->next->is_free &&
        (block->size + HEADER_SIZE + block->next->size) >= size) {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (nmemb > 0 && size > (size_t)-1 / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(ptr, nmemb * size);
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb > 0 && size > (size_t)-1 / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total ? total : ALIGNMENT);
    }
    return ptr;
}

size_t malloc_usable_size(void *ptr) {
    if (!ptr)
        return 0;
    block_header_t *block = (block_header_t *)((uintptr_t)ptr - HEADER_SIZE);
    return block->size;
}

extern void __execute_atexit(void);
extern void __cxa_finalize(void *dso);
extern void __libc_fini_array(void);

void exit(int status) {
    __cxa_finalize(NULL);
    __libc_fini_array();
    __execute_atexit();
    _exit(status);
}

void _Exit(int status) {
    _exit(status);
}

void abort(void) {
    _exit(134);
}

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    fprintf(stderr, "Assertion failed: %s (%s: %s: %u)\n", assertion, file, function, line);
    abort();
}

div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

lldiv_t lldiv(long long numer, long long denom) {
    lldiv_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

int atoi(const char *nptr) {
    return (int)atol(nptr);
}

long atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

long long atoll(const char *nptr) {
    return strtoll(nptr, NULL, 10);
}

int abs(int j) {
    return (j < 0) ? -j : j;
}

long labs(long j) {
    return (j < 0) ? -j : j;
}

long long llabs(long long j) {
    return (j < 0) ? -j : j;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s))
        s++;

    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long long acc = 0;
    while (*s) {
        int val;
        if (isdigit((unsigned char)*s))
            val = *s - '0';
        else if (isalpha((unsigned char)*s))
            val = toupper((unsigned char)*s) - 'A' + 10;
        else
            break;

        if (val >= base)
            break;
        acc = acc * base + val;
        s++;
    }

    if (endptr)
        *endptr = (char *)s;
    return acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtoull(nptr, endptr, base);
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base) {
    return (uintmax_t)strtoull(nptr, endptr, base);
}

long long strtoll(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s))
        s++;

    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    unsigned long long acc = strtoull(s, endptr, base);
    return neg ? -(long long)acc : (long long)acc;
}

long strtol(const char *nptr, char **endptr, int base) {
    return (long)strtoll(nptr, endptr, base);
}

intmax_t strtoimax(const char *nptr, char **endptr, int base) {
    return (intmax_t)strtoll(nptr, endptr, base);
}

intmax_t imaxabs(intmax_t j) {
    return j < 0 ? -j : j;
}

double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    while (isspace((unsigned char)*s))
        s++;

    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    double val = 0.0;
    while (isdigit((unsigned char)*s)) {
        val = val * 10.0 + (*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double factor = 0.1;
        while (isdigit((unsigned char)*s)) {
            val += (*s - '0') * factor;
            factor *= 0.1;
            s++;
        }
    }

    if (endptr)
        *endptr = (char *)s;
    return neg ? -val : val;
}

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}

#define MAX_ENV 64
static char *g_env_keys[MAX_ENV];
static char *g_env_vals[MAX_ENV];
static size_t g_env_count = 0;

char *getenv(const char *name) {
    if (!name)
        return NULL;
    size_t len = strlen(name);

    /* 1. Check dynamically set variables via setenv */
    for (size_t i = 0; i < g_env_count; i++) {
        if (strcmp(g_env_keys[i], name) == 0) {
            return g_env_vals[i];
        }
    }

    /* 2. Check environ array passed from execve */
    if (environ) {
        for (char **ep = environ; *ep; ep++) {
            if (strncmp(*ep, name, len) == 0 && (*ep)[len] == '=') {
                return *ep + len + 1;
            }
        }
    }

    /* 3. Fallback defaults */
    if (strcmp(name, "PATH") == 0)
        return "/bin:/usr/bin";
    if (strcmp(name, "HOME") == 0)
        return "/root";
    if (strcmp(name, "USER") == 0)
        return "root";
    if (strcmp(name, "SHELL") == 0)
        return "/bin/sh";
    if (strcmp(name, "TERM") == 0)
        return "xterm-256color";
    if (strcmp(name, "MAGIC") == 0)
        return "/etc/magic:/usr/share/misc/magic";

    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !value)
        return -1;
    for (size_t i = 0; i < g_env_count; i++) {
        if (strcmp(g_env_keys[i], name) == 0) {
            if (!overwrite)
                return 0;
            free(g_env_vals[i]);
            g_env_vals[i] = strdup(value);
            return 0;
        }
    }
    if (g_env_count < MAX_ENV) {
        g_env_keys[g_env_count] = strdup(name);
        g_env_vals[g_env_count] = strdup(value);
        g_env_count++;
        return 0;
    }
    return -1;
}

int unsetenv(const char *name) {
    if (!name)
        return -1;
    for (size_t i = 0; i < g_env_count; i++) {
        if (strcmp(g_env_keys[i], name) == 0) {
            free(g_env_keys[i]);
            free(g_env_vals[i]);
            g_env_keys[i] = g_env_keys[g_env_count - 1];
            g_env_vals[i] = g_env_vals[g_env_count - 1];
            g_env_count--;
            return 0;
        }
    }
    return 0;
}

int putenv(char *string) {
    if (!string)
        return -1;
    char *eq = strchr(string, '=');
    if (!eq)
        return -1;
    *eq = '\0';
    int ret = setenv(string, eq + 1, 1);
    *eq = '=';
    return ret;
}

static const char *g_progname = "szpontos";

const char *getprogname(void) {
    return g_progname ? g_progname : "szpontos";
}

void setprogname(const char *name) {
    g_progname = name;
}

char *mktemp(char *template) {
    if (!template)
        return NULL;
    char *x = strstr(template, "XXXXXX");
    if (x) {
        static unsigned int counter = 1000;
        snprintf(x, 7, "%06u", counter++);
    }
    return template;
}

int mkstemp(char *template) {
    char *name = mktemp(template);
    if (!name)
        return -1;
    return open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
}

char *mkdtemp(char *template) {
    if (!template) {
        errno = EINVAL;
        return NULL;
    }
    char *name = mktemp(template);
    if (!name)
        return NULL;
    if (mkdir(name, 0700) < 0)
        return NULL;
    return name;
}

int mkstemps(char *template, int suffixlen) {
    if (!template || suffixlen < 0)
        return -1;
    size_t len = strlen(template);
    if (len < (size_t)suffixlen + 6)
        return -1;
    char *x = template + len - suffixlen - 6;
    if (strncmp(x, "XXXXXX", 6) != 0)
        return -1;
    static unsigned int counter = 1000;
    snprintf(x, 7, "%06u", counter++);
    return open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
}

char *realpath(const char *path, char *resolved_path) {
    if (!path)
        return NULL;
    char *res = resolved_path ? resolved_path : (char *)malloc(4096);
    if (!res)
        return NULL;

    if (path[0] == '/') {
        strncpy(res, path, 4095);
    } else {
        char cwd[512] = {0};
        getcwd(cwd, sizeof(cwd));
        if (strcmp(cwd, "/") == 0) {
            snprintf(res, 4095, "/%s", path);
        } else {
            snprintf(res, 4095, "%s/%s", cwd, path);
        }
    }
    res[4095] = '\0';
    return res;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb <= 1 || size == 0 || !compar)
        return;

    /* Simple insertion sort / bubble sort for small/medium arrays */
    char *arr = (char *)base;
    char *temp = (char *)malloc(size);
    if (!temp)
        return;

    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - i - 1; j++) {
            char *p1 = arr + j * size;
            char *p2 = arr + (j + 1) * size;
            if (compar(p1, p2) > 0) {
                memcpy(temp, p1, size);
                memcpy(p1, p2, size);
                memcpy(p2, temp, size);
            }
        }
    }
    free(temp);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!key || !base || !compar)
        return NULL;
    size_t l = 0, r = nmemb;
    const char *arr = (const char *)base;

    while (l < r) {
        size_t mid = l + (r - l) / 2;
        const void *elem = arr + mid * size;
        int res = compar(key, elem);
        if (res == 0)
            return (void *)elem;
        if (res < 0)
            r = mid;
        else
            l = mid + 1;
    }
    return NULL;
}

static unsigned long g_rand_next = 1;

int rand(void) {
    g_rand_next = g_rand_next * 1103515245 + 12345;
    return (int)((g_rand_next / 65536) % 2147483648ULL);
}

void srand(unsigned int seed) {
    g_rand_next = seed;
}

long random(void) {
    return (long)rand();
}

void srandom(unsigned int seed) {
    srand(seed);
}
