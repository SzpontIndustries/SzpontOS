#ifndef SZPONTOS_MM_VMM_H
#define SZPONTOS_MM_VMM_H

#include <kernel/types.h>

#define VMM_FLAG_PRESENT (1ULL << 0)
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER (1ULL << 2)
#define VMM_FLAG_WRITE_THROUGH (1ULL << 3)
#define VMM_FLAG_WRITE_COMBINING (1ULL << 3) /* PA1 in configured PAT MSR */
#define VMM_FLAG_CACHE_DISABLE (1ULL << 4)
#define VMM_FLAG_NO_EXECUTE (1ULL << 63)

#define VMM_PAGE_SIZE 4096UL
#define PHYS_ADDR_MASK 0x000FFFFFFFFFF000ULL

typedef struct {
    uint64_t entries[512];
} page_table_t;

typedef struct {
    page_table_t *pml4_virt;
    uintptr_t pml4_phys;
} pagemap_t;

extern pagemap_t g_kernel_pagemap;
extern uint64_t g_hhdm_base;

#define PHYS_TO_VIRT(p) ((void *)((uintptr_t)(p) + g_hhdm_base))
#define VIRT_TO_PHYS(v) ((uintptr_t)(v) - g_hhdm_base)

void vmm_init(uint64_t hhdm_offset);
pagemap_t *vmm_get_kernel_pagemap(void);
pagemap_t *vmm_create_address_space(void);
void vmm_destroy_address_space(pagemap_t *map);
void vmm_switch_address_space(pagemap_t *map);

bool vmm_map_page(pagemap_t *map, uintptr_t virt, uintptr_t phys, uint64_t flags);
bool vmm_unmap_page(pagemap_t *map, uintptr_t virt);
uintptr_t vmm_virt_to_phys(pagemap_t *map, uintptr_t virt);
uintptr_t vmm_user_page_phys(pagemap_t *map, uintptr_t virt, bool write);
bool vmm_alloc_user_page(pagemap_t *map, uintptr_t virt, uint64_t flags);
pagemap_t *vmm_clone_address_space(pagemap_t *src);
bool vmm_set_range_flags(pagemap_t *map, uintptr_t virt, size_t size, uint64_t flags);

#endif /* SZPONTOS_MM_VMM_H */
