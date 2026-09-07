/* Host regression test: actual VMM, simulated physical RAM and privileged IO. */
#include <kernel/types.h>
#define SZPONTOS_ARCH_X86_64_IO_H
static void invlpg(uintptr_t v) { (void)v; }
static void write_cr3(uint64_t v) { (void)v; }
static uint64_t read_cr3(void) { return 0; }
static void wrmsr(uint32_t n, uint64_t v) { (void)n; (void)v; }
#include "kernel/src/mm/vmm.c"
extern void *malloc(size_t);
extern void free(void *);
extern int printf(const char *, ...);
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint8_t ram[128 * PAGE_SIZE] __attribute__((aligned(4096)));
static bool used[128];
static int alloc_budget = -1;
uintptr_t pmm_alloc_page(void) {
    if (alloc_budget == 0) return 0;
    for (int i = 1; i < 128; i++) if (!used[i]) {
        if (alloc_budget > 0) alloc_budget--;
        used[i] = true;
        return (uintptr_t)i * PAGE_SIZE;
    }
    return 0;
}
void pmm_free_page(uintptr_t p) { used[p / PAGE_SIZE] = false; }
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void klog(log_level_t level, const char *fmt, ...) { (void)level; (void)fmt; }
static int allocated(void) { int n=0; for (int i=1;i<128;i++) n+=used[i]; return n; }
int main(void) {
    g_hhdm_base = (uintptr_t)ram;
    g_kernel_pagemap.pml4_phys = pmm_alloc_page();
    g_kernel_pagemap.pml4_virt = PHYS_TO_VIRT(g_kernel_pagemap.pml4_phys);
    pagemap_t *map = vmm_create_address_space();
    CHECK(map);
    CHECK(!vmm_user_range(0xffff800000000000ULL, 1));
    CHECK(!vmm_user_range(VMM_USER_END - 1, 2));
    CHECK(!vmm_user_range(4096, (size_t)-1));
    CHECK(!vmm_map_page(map, 0xffffffff80000000ULL, 4096, VMM_FLAG_USER));
    int base = allocated();
    for (int i=0;i<1024;i++) {
        uintptr_t v = 0x600000000000ULL + (uintptr_t)i * 0x200000;
        CHECK(vmm_alloc_user_page(map, v, VMM_FLAG_NO_EXECUTE));
        CHECK(vmm_release_user_page(map, v));
        CHECK(allocated() == base);
    }
    uintptr_t v = 0x400000;
    CHECK(vmm_alloc_user_page(map, v, VMM_FLAG_NO_EXECUTE));
    CHECK(vmm_user_access(map, v, 4096, true));
    CHECK(!vmm_user_access(map, v + 4095, 2, false));
    CHECK(vmm_set_range_flags(map, v, 4096, VMM_FLAG_USER | VMM_FLAG_NO_EXECUTE));
    CHECK(vmm_user_access(map, v, 4096, false));
    CHECK(!vmm_user_access(map, v, 1, true));
    CHECK(!vmm_set_range_flags(map, v, 8192, VMM_FLAG_USER | VMM_FLAG_WRITABLE));
    CHECK(!vmm_user_access(map, v, 1, true));
    pagemap_t *child = vmm_clone_address_space(map);
    CHECK(child);
    CHECK(*user_leaf(child, v) & VMM_FLAG_NO_EXECUTE);
    CHECK(vmm_virt_to_phys(child, v) != vmm_virt_to_phys(map, v));
    vmm_destroy_address_space(child);
    CHECK(vmm_set_range_flags(map, v, 4096, VMM_FLAG_NO_EXECUTE));
    CHECK(!vmm_user_access(map, v, 1, false));
    child = vmm_clone_address_space(map);
    CHECK(child && !vmm_user_access(child, v, 1, false));
    vmm_destroy_address_space(child);
    CHECK(vmm_set_range_flags(map, v, 4096, VMM_FLAG_USER | VMM_FLAG_WRITABLE));
    CHECK(vmm_user_access(map, v, 1, true));
    for (int budget=0;budget<5;budget++) {
        int before = allocated();
        alloc_budget=budget;
        CHECK(vmm_clone_address_space(map) == NULL);
        CHECK(allocated() == before);
    }
    alloc_budget=-1;
    uintptr_t borrowed=pmm_alloc_page();
    CHECK(vmm_map_page(map, v+4096, borrowed, VMM_FLAG_USER | VMM_FLAG_BORROWED));
    CHECK(vmm_release_user_page(map, v+4096));
    CHECK(used[borrowed/PAGE_SIZE]);
    pmm_free_page(borrowed);
    vmm_destroy_address_space(map);
    CHECK(allocated() == 1);
    printf("PASS: VMM ranges, permissions, NX/fork, OOM rollback, owned/borrowed unmap\n");
    return 0;
}
