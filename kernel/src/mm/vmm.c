#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <arch/x86_64/io.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

pagemap_t g_kernel_pagemap;
uint64_t g_hhdm_base = 0;
static spinlock_t g_vmm_lock = SPINLOCK_INIT;

static inline size_t pml4_index(uintptr_t v) {
    return (v >> 39) & 0x1FF;
}
static inline size_t pdpt_index(uintptr_t v) {
    return (v >> 30) & 0x1FF;
}
static inline size_t pd_index(uintptr_t v) {
    return (v >> 21) & 0x1FF;
}
static inline size_t pt_index(uintptr_t v) {
    return (v >> 12) & 0x1FF;
}

static page_table_t *get_next_level(page_table_t *current, size_t index, bool allocate, uint64_t flags) {
    uint64_t entry = current->entries[index];

    if (entry & VMM_FLAG_PRESENT) {
        if (entry & (1 << 7)) {
            /* Huge page entry (2 MiB or 1 GiB) - not a next level table pointer */
            return NULL;
        }
        uintptr_t phys = entry & PHYS_ADDR_MASK;
        return (page_table_t *)PHYS_TO_VIRT(phys);
    }

    if (!allocate) {
        return NULL;
    }

    uintptr_t new_table_phys = pmm_alloc_page();
    if (!new_table_phys) {
        return NULL;
    }
    page_table_t *new_table_virt = (page_table_t *)PHYS_TO_VIRT(new_table_phys);
    memset(new_table_virt, 0, sizeof(page_table_t));

    current->entries[index] = new_table_phys | flags | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    return new_table_virt;
}

bool vmm_map_page(pagemap_t *map, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    if (!map || !map->pml4_virt)
        return false;

    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    phys = ALIGN_DOWN(phys, PAGE_SIZE);

    spinlock_acquire(&g_vmm_lock);

    page_table_t *pml4 = map->pml4_virt;
    page_table_t *pdpt = get_next_level(pml4, pml4_index(virt), true, flags & VMM_FLAG_USER);
    if (!pdpt) {
        spinlock_release(&g_vmm_lock);
        return false;
    }

    page_table_t *pd = get_next_level(pdpt, pdpt_index(virt), true, flags & VMM_FLAG_USER);
    if (!pd) {
        spinlock_release(&g_vmm_lock);
        return false;
    }

    page_table_t *pt = get_next_level(pd, pd_index(virt), true, flags & VMM_FLAG_USER);
    if (!pt) {
        spinlock_release(&g_vmm_lock);
        return false;
    }

    pt->entries[pt_index(virt)] = phys | flags | VMM_FLAG_PRESENT;
    invlpg(virt);

    spinlock_release(&g_vmm_lock);
    return true;
}

bool vmm_unmap_page(pagemap_t *map, uintptr_t virt) {
    if (!map || !map->pml4_virt)
        return false;

    virt = ALIGN_DOWN(virt, PAGE_SIZE);

    spinlock_acquire(&g_vmm_lock);

    page_table_t *pml4 = map->pml4_virt;
    page_table_t *pdpt = get_next_level(pml4, pml4_index(virt), false, 0);
    if (!pdpt) {
        spinlock_release(&g_vmm_lock);
        return false;
    }

    page_table_t *pd = get_next_level(pdpt, pdpt_index(virt), false, 0);
    if (!pd) {
        spinlock_release(&g_vmm_lock);
        return false;
    }

    page_table_t *pt = get_next_level(pd, pd_index(virt), false, 0);
    if (!pt) {
        spinlock_release(&g_vmm_lock);
        return false;
    }

    pt->entries[pt_index(virt)] = 0;
    invlpg(virt);

    spinlock_release(&g_vmm_lock);
    return true;
}

uintptr_t vmm_virt_to_phys(pagemap_t *map, uintptr_t virt) {
    if (!map || !map->pml4_virt)
        return 0;

    page_table_t *pml4 = map->pml4_virt;
    page_table_t *pdpt = get_next_level(pml4, pml4_index(virt), false, 0);
    if (!pdpt)
        return 0;

    page_table_t *pd = get_next_level(pdpt, pdpt_index(virt), false, 0);
    if (!pd)
        return 0;

    page_table_t *pt = get_next_level(pd, pd_index(virt), false, 0);
    if (!pt)
        return 0;

    uint64_t entry = pt->entries[pt_index(virt)];
    if (!(entry & VMM_FLAG_PRESENT))
        return 0;

    return (entry & PHYS_ADDR_MASK) | (virt & 0xFFF);
}

pagemap_t *vmm_get_kernel_pagemap(void) {
    return &g_kernel_pagemap;
}

void vmm_switch_address_space(pagemap_t *map) {
    if (!map || map->pml4_phys == 0)
        return;
    write_cr3(map->pml4_phys);
}

bool vmm_alloc_user_page(pagemap_t *map, uintptr_t virt, uint64_t flags) {
    if (vmm_virt_to_phys(map, virt) != 0) {
        return true;
    }

    uintptr_t phys = pmm_alloc_page();
    if (!phys)
        return false;

    void *ptr = PHYS_TO_VIRT(phys);
    memset(ptr, 0, PAGE_SIZE);

    return vmm_map_page(map, virt, phys, flags | VMM_FLAG_USER | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
}

pagemap_t *vmm_create_address_space(void) {
    uintptr_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) {
        klog_error("vmm_create_address_space: pmm_alloc_page failed!");
        return NULL;
    }

    page_table_t *pml4_virt = (page_table_t *)PHYS_TO_VIRT(pml4_phys);
    memset(pml4_virt, 0, sizeof(page_table_t));

    /* Copy Kernel Higher-Half mappings (entries 256..511) */
    for (size_t i = 256; i < 512; i++) {
        pml4_virt->entries[i] = g_kernel_pagemap.pml4_virt->entries[i];
    }

    pagemap_t *map = (pagemap_t *)kmalloc(sizeof(pagemap_t));
    if (!map) {
        klog_error("vmm_create_address_space: kmalloc failed!");
        pmm_free_page(pml4_phys);
        return NULL;
    }
    map->pml4_phys = pml4_phys;
    map->pml4_virt = pml4_virt;
    return map;
}

void vmm_destroy_address_space(pagemap_t *map) {
    if (!map || map == &g_kernel_pagemap)
        return;

    /* Free user space pages (entries 0..255) */
    for (size_t i = 0; i < 256; i++) {
        if (map->pml4_virt->entries[i] & VMM_FLAG_PRESENT) {
            uintptr_t pdpt_phys = map->pml4_virt->entries[i] & PHYS_ADDR_MASK;
            page_table_t *pdpt = (page_table_t *)PHYS_TO_VIRT(pdpt_phys);

            for (size_t j = 0; j < 512; j++) {
                if (pdpt->entries[j] & VMM_FLAG_PRESENT) {
                    uintptr_t pd_phys = pdpt->entries[j] & PHYS_ADDR_MASK;
                    page_table_t *pd = (page_table_t *)PHYS_TO_VIRT(pd_phys);

                    for (size_t k = 0; k < 512; k++) {
                        if (pd->entries[k] & VMM_FLAG_PRESENT) {
                            uintptr_t pt_phys = pd->entries[k] & PHYS_ADDR_MASK;
                            page_table_t *pt = (page_table_t *)PHYS_TO_VIRT(pt_phys);

                            for (size_t l = 0; l < 512; l++) {
                                if (pt->entries[l] & VMM_FLAG_PRESENT) {
                                    uintptr_t page_phys = pt->entries[l] & PHYS_ADDR_MASK;
                                    pmm_free_page(page_phys);
                                }
                            }
                            pmm_free_page(pt_phys);
                        }
                    }
                    pmm_free_page(pd_phys);
                }
            }
            pmm_free_page(pdpt_phys);
        }
    }

    pmm_free_page(map->pml4_phys);
    kfree(map);
}

pagemap_t *vmm_clone_address_space(pagemap_t *src) {
    if (!src)
        return NULL;

    pagemap_t *dst = vmm_create_address_space();
    if (!dst)
        return NULL;

    /* Copy user pages */
    for (size_t i = 0; i < 256; i++) {
        if (src->pml4_virt->entries[i] & VMM_FLAG_PRESENT) {
            uintptr_t pdpt_phys = src->pml4_virt->entries[i] & PHYS_ADDR_MASK;
            page_table_t *pdpt = (page_table_t *)PHYS_TO_VIRT(pdpt_phys);

            for (size_t j = 0; j < 512; j++) {
                if (pdpt->entries[j] & VMM_FLAG_PRESENT) {
                    if (pdpt->entries[j] & (1 << 7)) {
                        continue; /* 1 GiB huge page */
                    }
                    uintptr_t pd_phys = pdpt->entries[j] & PHYS_ADDR_MASK;
                    page_table_t *pd = (page_table_t *)PHYS_TO_VIRT(pd_phys);

                    for (size_t k = 0; k < 512; k++) {
                        if (pd->entries[k] & VMM_FLAG_PRESENT) {
                            if (pd->entries[k] & (1 << 7)) {
                                continue; /* 2 MiB huge page */
                            }
                            uintptr_t pt_phys = pd->entries[k] & PHYS_ADDR_MASK;
                            page_table_t *pt = (page_table_t *)PHYS_TO_VIRT(pt_phys);

                            for (size_t l = 0; l < 512; l++) {
                                if (pt->entries[l] & VMM_FLAG_PRESENT) {
                                    uintptr_t virt = ((uintptr_t)i << 39) | ((uintptr_t)j << 30) |
                                                     ((uintptr_t)k << 21) | ((uintptr_t)l << 12);
                                    uintptr_t src_phys = pt->entries[l] & PHYS_ADDR_MASK;
                                    uint64_t flags = pt->entries[l] & 0xFFF;

                                    uintptr_t dst_phys = pmm_alloc_page();
                                    if (!dst_phys) {
                                        klog_error("VMM: Out of physical memory while cloning address space (fork)!");
                                        continue;
                                    }
                                    memcpy(PHYS_TO_VIRT(dst_phys), PHYS_TO_VIRT(src_phys), PAGE_SIZE);
                                    vmm_map_page(dst, virt, dst_phys, flags);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return dst;
}

bool vmm_set_range_flags(pagemap_t *map, uintptr_t virt, size_t size, uint64_t flags) {
    if (!map || !map->pml4_virt || size == 0)
        return false;

    uintptr_t start = ALIGN_DOWN(virt, PAGE_SIZE);
    uintptr_t end = ALIGN_UP(virt + size, PAGE_SIZE);

    spinlock_acquire(&g_vmm_lock);

    for (uintptr_t v = start; v < end; v += PAGE_SIZE) {
        page_table_t *pml4 = map->pml4_virt;
        page_table_t *pdpt = get_next_level(pml4, pml4_index(v), false, 0);
        if (!pdpt)
            continue;

        page_table_t *pd = get_next_level(pdpt, pdpt_index(v), false, 0);
        if (!pd)
            continue;

        page_table_t *pt = get_next_level(pd, pd_index(v), false, 0);
        if (!pt)
            continue;

        uint64_t entry = pt->entries[pt_index(v)];
        if (entry & VMM_FLAG_PRESENT) {
            uintptr_t phys = entry & PHYS_ADDR_MASK;
            pt->entries[pt_index(v)] = phys | flags | VMM_FLAG_PRESENT;
            invlpg(v);
        }
    }

    spinlock_release(&g_vmm_lock);
    return true;
}

void vmm_init(uint64_t hhdm_offset) {
    g_hhdm_base = hhdm_offset;

    /* Initialize IA32_PAT (MSR 0x277) to enable Write-Combining on PA1 */
    uint64_t pat = 0x0007010600070106ULL;
    wrmsr(0x277, pat);

    /* Get current PML4 from CR3 */
    uintptr_t cr3 = read_cr3() & PHYS_ADDR_MASK;
    g_kernel_pagemap.pml4_phys = cr3;
    g_kernel_pagemap.pml4_virt = (page_table_t *)PHYS_TO_VIRT(cr3);

    klog_info("VMM initialized (PML4 phys: 0x%016lx, HHDM base: 0x%016lx, PAT: WC enabled)", g_kernel_pagemap.pml4_phys,
              g_hhdm_base);
}
