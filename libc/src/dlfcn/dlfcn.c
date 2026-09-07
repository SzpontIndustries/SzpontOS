/*
 * SzpontOS Libc - Dynamic Linker & Dynamic Loading (dlfcn)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>

static char g_dlerror_buf[256] = {0};
static int g_has_dlerror = 0;

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} local_elf_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} local_elf_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} local_elf_shdr_t;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} local_elf_sym_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} local_elf_rela_t;

#define ELF64_R_SYM(i)    ((uint64_t)(i) >> 32)
#define ELF64_R_TYPE(i)   ((uint32_t)(i))

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_COPY      5
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

#define PT_LOAD   1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA   4
#define SHT_DYNSYM 11

typedef struct dl_handle {
    char name[128];
    uintptr_t base_addr;
    local_elf_sym_t *symtab;
    size_t sym_count;
    char *strtab;
    size_t str_size;
} dl_handle_t;

#define MAX_DL_HANDLES 32
static dl_handle_t g_dl_handles[MAX_DL_HANDLES];
static size_t g_dl_count = 0;
static uintptr_t g_next_dl_base = 0x0000720000000000ULL;
static int g_main_initialized = 0;

static void set_dlerror(const char *msg) {
    if (msg) {
        strncpy(g_dlerror_buf, msg, sizeof(g_dlerror_buf) - 1);
        g_dlerror_buf[sizeof(g_dlerror_buf) - 1] = '\0';
        g_has_dlerror = 1;
    } else {
        g_has_dlerror = 0;
    }
}

char *dlerror(void) {
    if (!g_has_dlerror)
        return NULL;
    g_has_dlerror = 0;
    return g_dlerror_buf;
}

/* Built-in system symbol map for libc and libdrm */
typedef struct {
    const char *name;
    void *addr;
} builtin_sym_t;

static const builtin_sym_t g_builtin_syms[] = {
    /* POSIX I/O & Process */
    {"open", (void *)open},
    {"close", (void *)close},
    {"read", (void *)read},
    {"write", (void *)write},
    {"lseek", (void *)lseek},
    {"ioctl", (void *)ioctl},
    {"poll", (void *)poll},
    {"dup", (void *)dup},
    {"dup2", (void *)dup2},
    {"mmap", (void *)mmap},
    {"munmap", (void *)munmap},
    {"fork", (void *)fork},
    {"execve", (void *)execve},
    {"exit", (void *)exit},
    {"getpid", (void *)getpid},
    {"sleep", (void *)sleep},
    {"nanosleep", (void *)nanosleep},
    {"clock_gettime", (void *)clock_gettime},
    {"gettimeofday", (void *)gettimeofday},
    {"time", (void *)time},
    {"errno", (void *)&errno},
    {"__errno_location", (void *)&errno},

    /* Memory & String */
    {"malloc", (void *)malloc},
    {"calloc", (void *)calloc},
    {"realloc", (void *)realloc},
    {"free", (void *)free},
    {"strdup", (void *)strdup},
    {"asprintf", (void *)asprintf},
    {"snprintf", (void *)snprintf},
    {"sprintf", (void *)sprintf},
    {"printf", (void *)printf},
    {"vsnprintf", (void *)vsnprintf},
    {"fprintf", (void *)fprintf},
    {"puts", (void *)puts},
    {"putchar", (void *)putchar},
    {"getenv", (void *)getenv},
    {"setenv", (void *)setenv},
    {"atoi", (void *)atoi},
    {"strtol", (void *)strtol},
    {"strtoul", (void *)strtoul},
    {"abort", (void *)abort},
    {"qsort", (void *)qsort},
    {"memcpy", (void *)memcpy},
    {"memmove", (void *)memmove},
    {"memset", (void *)memset},
    {"memcmp", (void *)memcmp},
    {"strlen", (void *)strlen},
    {"strcpy", (void *)strcpy},
    {"strncpy", (void *)strncpy},
    {"strcat", (void *)strcat},
    {"strncat", (void *)strncat},
    {"strcmp", (void *)strcmp},
    {"strncmp", (void *)strncmp},
    {"strcasecmp", (void *)strcasecmp},
    {"strncasecmp", (void *)strncasecmp},
    {"strchr", (void *)strchr},
    {"strrchr", (void *)strrchr},
    {"strstr", (void *)strstr},
    {"strtok", (void *)strtok},
    {"strerror", (void *)strerror},

    /* Dynamic loading */
    {"dlopen", (void *)dlopen},
    {"dlsym", (void *)dlsym},
    {"dlclose", (void *)dlclose},
    {"dlerror", (void *)dlerror},

    {NULL, NULL}
};

static void *find_builtin_symbol(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; g_builtin_syms[i].name != NULL; i++) {
        if (strcmp(g_builtin_syms[i].name, name) == 0) {
            return g_builtin_syms[i].addr;
        }
    }
    return NULL;
}

static void *find_symbol_in_handle(dl_handle_t *h, const char *symbol) {
    if (!h || !h->symtab || !h->strtab)
        return NULL;

    for (size_t i = 0; i < h->sym_count; i++) {
        local_elf_sym_t *sym = &h->symtab[i];
        if (sym->st_name < h->str_size) {
            const char *name = h->strtab + sym->st_name;
            if (strcmp(name, symbol) == 0 && sym->st_shndx != 0) {
                return (void *)(h->base_addr + sym->st_value);
            }
        }
    }
    return NULL;
}

static void init_main_binary_symbols(void) {
    if (g_main_initialized)
        return;
    g_main_initialized = 1;

    int fd = -1;
    char path[256] = {0};

    /* 1. Try reading executable path from /proc/self/exe */
    int pfd = open("/proc/self/exe", O_RDONLY, 0);
    if (pfd >= 0) {
        ssize_t n = read(pfd, path, sizeof(path) - 1);
        close(pfd);
        if (n > 0) {
            path[n] = '\0';
            while (n > 0 && (path[n-1] == '\n' || path[n-1] == '\r' || path[n-1] == ' ')) {
                path[--n] = '\0';
            }
            if (path[0] == '/') {
                fd = open(path, O_RDONLY, 0);
            } else if (path[0] != '\0') {
                char full[256];
                snprintf(full, sizeof(full), "/bin/%s", path);
                fd = open(full, O_RDONLY, 0);
            }
        }
    }

    /* 2. Fallback to common main binaries */
    if (fd < 0) {
        const char *main_paths[] = {"/bin/Xorg", "/bin/xdemo", "/bin/sh", "/bin/init", NULL};
        for (int i = 0; main_paths[i] != NULL; i++) {
            fd = open(main_paths[i], O_RDONLY, 0);
            if (fd >= 0) {
                strncpy(path, main_paths[i], sizeof(path) - 1);
                break;
            }
        }
    }

    if (fd < 0) return;

    local_elf_ehdr_t ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        return;
    }

    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        close(fd);
        return;
    }

    if (ehdr.e_shoff && ehdr.e_shnum) {
        size_t shdr_size = (size_t)ehdr.e_shentsize * ehdr.e_shnum;
        local_elf_shdr_t *shdrs = (local_elf_shdr_t *)malloc(shdr_size);
        if (shdrs) {
            lseek(fd, ehdr.e_shoff, SEEK_SET);
            if (read(fd, shdrs, shdr_size) == (ssize_t)shdr_size) {
                int sym_idx = -1;
                for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
                    if (shdrs[i].sh_type == SHT_SYMTAB) {
                        sym_idx = i;
                        break;
                    }
                    if (shdrs[i].sh_type == SHT_DYNSYM && sym_idx < 0) {
                        sym_idx = i;
                    }
                }

                if (sym_idx >= 0) {
                    uint32_t str_idx = shdrs[sym_idx].sh_link;
                    size_t sym_count = shdrs[sym_idx].sh_size / sizeof(local_elf_sym_t);
                    local_elf_sym_t *symtab = (local_elf_sym_t *)malloc(shdrs[sym_idx].sh_size);
                    if (symtab) {
                        lseek(fd, shdrs[sym_idx].sh_offset, SEEK_SET);
                        read(fd, symtab, shdrs[sym_idx].sh_size);
                    }

                    char *strtab = NULL;
                    size_t str_size = 0;
                    if (str_idx < ehdr.e_shnum) {
                        str_size = shdrs[str_idx].sh_size;
                        strtab = (char *)malloc(str_size);
                        if (strtab) {
                            lseek(fd, shdrs[str_idx].sh_offset, SEEK_SET);
                            read(fd, strtab, str_size);
                        }
                    }

                    if (g_dl_count == 0) {
                        dl_handle_t *h = &g_dl_handles[g_dl_count++];
                        memset(h, 0, sizeof(dl_handle_t));
                        strncpy(h->name, "main", sizeof(h->name) - 1);
                        h->base_addr = 0; /* Fixed virtual address for executable */
                        h->symtab = symtab;
                        h->sym_count = sym_count;
                        h->strtab = strtab;
                        h->str_size = str_size;
                    }
                }
            }
            free(shdrs);
        }
    }
    close(fd);
}

void *dlsym(void *handle, const char *symbol) {
    if (!symbol || !*symbol) {
        set_dlerror("dlsym: empty symbol name");
        return NULL;
    }

    init_main_binary_symbols();

    /* 1. Check built-in system symbols (libc / libdrm) */
    void *built_in = find_builtin_symbol(symbol);
    if (built_in) {
        return built_in;
    }

    if (handle == RTLD_DEFAULT || handle == NULL) {
        /* Search all loaded libraries */
        for (size_t i = 0; i < g_dl_count; i++) {
            void *ptr = find_symbol_in_handle(&g_dl_handles[i], symbol);
            if (ptr) return ptr;
        }
        set_dlerror("dlsym: symbol not found in global scope");
        return NULL;
    }

    dl_handle_t *h = (dl_handle_t *)handle;
    void *ptr = find_symbol_in_handle(h, symbol);
    if (ptr) {
        return ptr;
    }

    /* Fallback: search global scope */
    for (size_t i = 0; i < g_dl_count; i++) {
        ptr = find_symbol_in_handle(&g_dl_handles[i], symbol);
        if (ptr) return ptr;
    }

    set_dlerror("dlsym: symbol not found");
    return NULL;
}

void *dlopen(const char *filename, int flags) {
    (void)flags;

    init_main_binary_symbols();

    if (!filename) {
        /* Return global handle (RTLD_DEFAULT) */
        return RTLD_DEFAULT;
    }

    /* Check if already loaded */
    for (size_t i = 0; i < g_dl_count; i++) {
        if (strcmp(g_dl_handles[i].name, filename) == 0) {
            return &g_dl_handles[i];
        }
    }

    if (g_dl_count >= MAX_DL_HANDLES) {
        set_dlerror("dlopen: maximum shared library handles reached");
        return NULL;
    }

    char path[256];
    int fd = -1;

    if (filename[0] == '/') {
        strncpy(path, filename, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        fd = open(path, O_RDONLY, 0);
    } else {
        const char *search_dirs[] = {
            "/usr/lib/xorg/modules/drivers",
            "/usr/lib/xorg/modules",
            "/lib",
            "/usr/lib",
            NULL
        };

        for (int i = 0; search_dirs[i] != NULL; i++) {
            snprintf(path, sizeof(path), "%s/%s", search_dirs[i], filename);
            fd = open(path, O_RDONLY, 0);
            if (fd >= 0) break;

            snprintf(path, sizeof(path), "%s/%s.so", search_dirs[i], filename);
            fd = open(path, O_RDONLY, 0);
            if (fd >= 0) break;
        }
    }

    if (fd < 0) {
        set_dlerror("dlopen: file not found");
        return NULL;
    }

    local_elf_ehdr_t ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        set_dlerror("dlopen: failed to read ELF header");
        return NULL;
    }

    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        close(fd);
        set_dlerror("dlopen: invalid ELF magic");
        return NULL;
    }

    uintptr_t base_addr = g_next_dl_base;
    g_next_dl_base += 0x0000000010000000ULL; /* 256MB per module */

    /* 1. Map PT_LOAD segments */
    if (ehdr.e_phoff && ehdr.e_phnum) {
        size_t phdr_size = (size_t)ehdr.e_phentsize * ehdr.e_phnum;
        local_elf_phdr_t *phdrs = (local_elf_phdr_t *)malloc(phdr_size);
        if (phdrs) {
            lseek(fd, ehdr.e_phoff, SEEK_SET);
            if (read(fd, phdrs, phdr_size) == (ssize_t)phdr_size) {
                for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
                    if (phdrs[i].p_type == PT_LOAD) {
                        uintptr_t vaddr = base_addr + phdrs[i].p_vaddr;
                        uintptr_t page_vaddr = vaddr & ~0xFFFULL;
                        uintptr_t page_offset = vaddr - page_vaddr;
                        size_t page_len = (phdrs[i].p_memsz + page_offset + 0xFFFULL) & ~0xFFFULL;

                        mmap((void *)page_vaddr, page_len,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

                        lseek(fd, phdrs[i].p_offset, SEEK_SET);
                        read(fd, (void *)vaddr, phdrs[i].p_filesz);
                        if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
                            memset((void *)(vaddr + phdrs[i].p_filesz), 0,
                                   phdrs[i].p_memsz - phdrs[i].p_filesz);
                        }
                    }
                }
            }
            free(phdrs);
        }
    }

    /* 2. Read Section Headers to extract Symbol & String Tables and Relocations */
    local_elf_sym_t *symtab = NULL;
    size_t sym_count = 0;
    char *strtab = NULL;
    size_t str_size = 0;

    local_elf_rela_t *rela_dyn = NULL;
    size_t rela_dyn_count = 0;
    local_elf_rela_t *rela_plt = NULL;
    size_t rela_plt_count = 0;

    if (ehdr.e_shoff && ehdr.e_shnum) {
        size_t shdr_size = (size_t)ehdr.e_shentsize * ehdr.e_shnum;
        local_elf_shdr_t *shdrs = (local_elf_shdr_t *)malloc(shdr_size);
        if (shdrs) {
            lseek(fd, ehdr.e_shoff, SEEK_SET);
            if (read(fd, shdrs, shdr_size) == (ssize_t)shdr_size) {
                int sym_idx = -1;
                char *shstrtab = NULL;
                if (ehdr.e_shstrndx < ehdr.e_shnum) {
                    shstrtab = (char *)malloc(shdrs[ehdr.e_shstrndx].sh_size);
                    if (shstrtab) {
                        lseek(fd, shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
                        read(fd, shstrtab, shdrs[ehdr.e_shstrndx].sh_size);
                    }
                }

                for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
                    if (shdrs[i].sh_type == SHT_DYNSYM) {
                        sym_idx = i;
                    } else if (shdrs[i].sh_type == SHT_SYMTAB && sym_idx < 0) {
                        sym_idx = i;
                    } else if (shdrs[i].sh_type == SHT_RELA) {
                        if (shstrtab && strcmp(shstrtab + shdrs[i].sh_name, ".rela.plt") == 0) {
                            rela_plt_count = shdrs[i].sh_size / sizeof(local_elf_rela_t);
                            rela_plt = (local_elf_rela_t *)malloc(shdrs[i].sh_size);
                            if (rela_plt) {
                                lseek(fd, shdrs[i].sh_offset, SEEK_SET);
                                read(fd, rela_plt, shdrs[i].sh_size);
                            }
                        } else {
                            rela_dyn_count = shdrs[i].sh_size / sizeof(local_elf_rela_t);
                            rela_dyn = (local_elf_rela_t *)malloc(shdrs[i].sh_size);
                            if (rela_dyn) {
                                lseek(fd, shdrs[i].sh_offset, SEEK_SET);
                                read(fd, rela_dyn, shdrs[i].sh_size);
                            }
                        }
                    }
                }

                if (sym_idx >= 0) {
                    uint32_t str_idx = shdrs[sym_idx].sh_link;
                    sym_count = shdrs[sym_idx].sh_size / sizeof(local_elf_sym_t);
                    symtab = (local_elf_sym_t *)malloc(shdrs[sym_idx].sh_size);
                    if (symtab) {
                        lseek(fd, shdrs[sym_idx].sh_offset, SEEK_SET);
                        read(fd, symtab, shdrs[sym_idx].sh_size);
                    }

                    if (str_idx < ehdr.e_shnum) {
                        str_size = shdrs[str_idx].sh_size;
                        strtab = (char *)malloc(str_size);
                        if (strtab) {
                            lseek(fd, shdrs[str_idx].sh_offset, SEEK_SET);
                            read(fd, strtab, str_size);
                        }
                    }
                }
                if (shstrtab) free(shstrtab);
            }
            free(shdrs);
        }
    }

    close(fd);

    dl_handle_t *h = &g_dl_handles[g_dl_count++];
    memset(h, 0, sizeof(dl_handle_t));
    strncpy(h->name, filename, sizeof(h->name) - 1);
    h->base_addr = base_addr;
    h->symtab = symtab;
    h->sym_count = sym_count;
    h->strtab = strtab;
    h->str_size = str_size;

    /* 3. Apply Dynamic Relocations (R_X86_64_RELATIVE, R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT) */
    for (int pass = 0; pass < 2; pass++) {
        local_elf_rela_t *relas = (pass == 0) ? rela_dyn : rela_plt;
        size_t count = (pass == 0) ? rela_dyn_count : rela_plt_count;
        if (!relas) continue;

        for (size_t j = 0; j < count; j++) {
            uint64_t type = ELF64_R_TYPE(relas[j].r_info);
            uint64_t sym_idx = ELF64_R_SYM(relas[j].r_info);
            uint64_t *target = (uint64_t *)(base_addr + relas[j].r_offset);

            if (type == R_X86_64_RELATIVE) {
                *target = (uint64_t)(base_addr + relas[j].r_addend);
            } else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                const char *sym_name = "";
                if (sym_idx < sym_count && symtab && strtab) {
                    if (symtab[sym_idx].st_name < str_size) {
                        sym_name = strtab + symtab[sym_idx].st_name;
                    }
                }

                void *sym_val = NULL;
                if (*sym_name) {
                    sym_val = dlsym(RTLD_DEFAULT, sym_name);
                    if (!sym_val && symtab && symtab[sym_idx].st_shndx != 0) {
                        sym_val = (void *)(base_addr + symtab[sym_idx].st_value);
                    }
                }

                if (sym_val) {
                    if (type == R_X86_64_64) {
                        *target = (uint64_t)((uintptr_t)sym_val + relas[j].r_addend);
                    } else {
                        *target = (uint64_t)(uintptr_t)sym_val;
                    }
                } else if (*target < base_addr && *target != 0) {
                    *target += base_addr;
                }
            }
        }
        free(relas);
    }

    set_dlerror(NULL);
    return h;
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

int dladdr(const void *addr, Dl_info *info) {
    if (!info) return 0;
    init_main_binary_symbols();
    uintptr_t uaddr = (uintptr_t)addr;
    for (size_t i = 0; i < g_dl_count; i++) {
        dl_handle_t *h = &g_dl_handles[i];
        if (h->base_addr <= uaddr) {
            info->dli_fname = h->name;
            info->dli_fbase = (void *)h->base_addr;
            info->dli_sname = NULL;
            info->dli_saddr = NULL;
            if (h->symtab && h->strtab) {
                for (size_t s = 0; s < h->sym_count; s++) {
                    local_elf_sym_t *sym = &h->symtab[s];
                    if (sym->st_shndx != 0 && sym->st_name < h->str_size) {
                        uintptr_t sym_addr = h->base_addr + sym->st_value;
                        if (sym_addr == uaddr || (sym_addr <= uaddr && uaddr < sym_addr + sym->st_size)) {
                            info->dli_sname = h->strtab + sym->st_name;
                            info->dli_saddr = (void *)sym_addr;
                            break;
                        }
                    }
                }
            }
            return 1;
        }
    }
    info->dli_fname = "/lib/libc.so";
    info->dli_fbase = (void *)0;
    info->dli_sname = "unknown";
    info->dli_saddr = (void *)addr;
    return 1;
}

