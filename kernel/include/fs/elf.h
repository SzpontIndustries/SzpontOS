#ifndef SZPONTOS_FS_ELF_H
#define SZPONTOS_FS_ELF_H

#include <kernel/types.h>
#include <fs/vfs.h>
#include <sched/process.h>

#define ELF_MAGIC 0x464C457FU /* "\x7FELF" */
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_X86_64 62
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define ET_CORE 4

#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6
#define PT_TLS 7

#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Dynamic Array Tags */
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_SYMBOLIC 16
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_DEBUG 21
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_BIND_NOW 24
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH 29
#define DT_FLAGS 30

/* x86_64 Relocation Types */
#define R_X86_64_NONE 0
#define R_X86_64_64 1        /* Direct 64 bit: S + A */
#define R_X86_64_PC32 2      /* PC relative 32 bit signed: S + A - P */
#define R_X86_64_GOT32 3     /* 32 bit GOT entry: G + A */
#define R_X86_64_PLT32 4     /* 32 bit PLT address: L + A - P */
#define R_X86_64_COPY 5      /* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT 6  /* Create GOT entry: S */
#define R_X86_64_JUMP_SLOT 7 /* Create PLT entry: S */
#define R_X86_64_RELATIVE 8  /* Adjust by program base: B + A */
#define R_X86_64_GOTPCREL 9  /* 32 bit signed pc relative offset to GOT: G + GOT + A - P */
#define R_X86_64_32 10       /* Direct 32 bit zero extended: S + A */
#define R_X86_64_32S 11      /* Direct 32 bit sign extended: S + A */

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xFFFFFFFFL)

/* Symbol binding & type */
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

/* Auxiliary vector entries */
#define AT_NULL 0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_NOTELF 10
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_EXECFN 31

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
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

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
} Elf64_Shdr;

typedef struct {
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

typedef struct {
    uint64_t a_type;
    union {
        uint64_t a_val;
        void *a_ptr;
    } a_un;
} Elf64_Auxv;

typedef struct elf_loaded_so {
    char name[64];
    uintptr_t base_vaddr;
    uintptr_t mem_size;
    uintptr_t dyn_vaddr;
    Elf64_Sym *symtab;
    size_t sym_count;
    char *strtab;
    size_t str_size;
    uint32_t *hashtab;
    uint32_t nbucket;
    uint32_t nchain;
    uint32_t *buckets;
    uint32_t *chains;
    Elf64_Rela *rela;
    size_t rela_count;
    Elf64_Rela *jmprel;
    size_t jmprel_count;
    uintptr_t *pltgot;
    uintptr_t init_func;
    uintptr_t fini_func;
    uintptr_t init_array;
    size_t init_array_sz;
} elf_loaded_so_t;

int elf_load_binary(vfs_node_t *file, pagemap_t *map, uintptr_t *out_entry, uintptr_t *out_user_stack,
                    uintptr_t *out_brk_start);
process_t *elf_spawn(const char *path, const char *name);

#endif /* SZPONTOS_FS_ELF_H */
