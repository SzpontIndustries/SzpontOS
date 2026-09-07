/*
 * SzpontOS - Safe User ↔ Kernel Memory Copy Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * User addresses are resolved one page at a time through the process pagemap.
 * Each page's physical address is looked up via vmm_virt_to_phys() and then
 * accessed through the HHDM direct map, so:
 *   - An unmapped page returns false/-EFAULT rather than a kernel #PF
 *   - Non-canonical addresses (above USER_ADDR_MAX) are rejected outright
 *   - Integer overflow on (addr + len) is caught before any memory access
 */

#include <mm/usercopy.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

/*
 * Validate that a user address range is within canonical userspace
 * and does not overflow.
 */
static inline bool valid_user_range(uintptr_t addr, size_t len)
{
    if (len == 0)
        return true;

    /* Check for integer overflow */
    if (addr + len < addr)
        return false;

    /* Must be entirely within the canonical user half */
    if (addr > USER_ADDR_MAX || (addr + len - 1) > USER_ADDR_MAX)
        return false;

    return true;
}

/*
 * Core copy routine that resolves user addresses page-by-page through
 * the current process's pagemap.
 *
 * @pagemap:        The pagemap to resolve user addresses against
 * @kernel_buf:     Kernel-side buffer (source or destination)
 * @user_addr:      User virtual address (source or destination)
 * @len:            Number of bytes to transfer
 * @to_user:        true = kernel→user, false = user→kernel
 *
 * Returns true on success, false on any address resolution failure.
 */
static bool do_copy(pagemap_t *pagemap, void *kernel_buf, uintptr_t user_addr,
                    size_t len, bool to_user)
{
    if (len == 0)
        return true;

    if (!kernel_buf || !pagemap)
        return false;

    if (!valid_user_range(user_addr, len))
        return false;

    size_t copied = 0;
    while (copied < len) {
        uintptr_t current_addr = user_addr + copied;

        /* How much of the current page is left? */
        size_t page_offset = current_addr & (VMM_PAGE_SIZE - 1);
        size_t chunk = VMM_PAGE_SIZE - page_offset;
        if (chunk > (len - copied))
            chunk = len - copied;

        /* Resolve user virtual → physical through the pagemap with permissions */
        uintptr_t phys = vmm_user_page_phys(pagemap, current_addr, to_user);
        if (phys == 0) {
            /* Page not mapped or lacks required permissions — cannot proceed */
            return false;
        }

        /* Access through the kernel's HHDM direct map */
        void *hhdm_ptr = (void *)(phys + g_hhdm_base);

        if (to_user) {
            memcpy(hhdm_ptr, (const uint8_t *)kernel_buf + copied, chunk);
        } else {
            memcpy((uint8_t *)kernel_buf + copied, hhdm_ptr, chunk);
        }

        copied += chunk;
    }

    return true;
}

bool copy_from_user(void *dst, uintptr_t user_src, size_t len)
{
    process_t *proc = sched_get_current_process();
    if (!proc || !proc->pagemap)
        return false;

    return do_copy(proc->pagemap, dst, user_src, len, false);
}

bool copy_to_user(uintptr_t user_dst, const void *src, size_t len)
{
    process_t *proc = sched_get_current_process();
    if (!proc || !proc->pagemap)
        return false;

    return do_copy(proc->pagemap, (void *)src, user_dst, len, true);
}

ssize_t copy_string_from_user(char *dst, uintptr_t user_src, size_t max_len)
{
    if (!dst || max_len == 0)
        return -1;

    process_t *proc = sched_get_current_process();
    if (!proc || !proc->pagemap)
        return -1;

    if (user_src > USER_ADDR_MAX)
        return -1;

    pagemap_t *pagemap = proc->pagemap;
    size_t i = 0;

    while (i < max_len - 1) {
        uintptr_t addr = user_src + i;

        /* Check canonical range on each page boundary */
        if (addr > USER_ADDR_MAX)
            return -1;

        /* Resolve the page with read permission check */
        size_t page_offset = addr & (VMM_PAGE_SIZE - 1);
        uintptr_t phys = vmm_user_page_phys(pagemap, addr, false);
        if (phys == 0)
            return -1;

        /* Access through HHDM — copy byte-by-byte until page boundary or NUL */
        const uint8_t *src_page = (const uint8_t *)(phys + g_hhdm_base);
        size_t remaining_in_page = VMM_PAGE_SIZE - page_offset;

        while (remaining_in_page > 0 && i < max_len - 1) {
            char c = (char)*src_page++;
            dst[i] = c;
            if (c == '\0') {
                return (ssize_t)i;
            }
            i++;
            remaining_in_page--;
        }
    }

    /* String too long — NUL-terminate and report failure */
    dst[max_len - 1] = '\0';
    return -1;
}

bool verify_user_read(uintptr_t user_addr, size_t len)
{
    if (!valid_user_range(user_addr, len))
        return false;

    if (len == 0)
        return true;

    process_t *proc = sched_get_current_process();
    if (!proc || !proc->pagemap)
        return false;

    pagemap_t *pagemap = proc->pagemap;

    for (size_t off = 0; off < len; off += VMM_PAGE_SIZE) {
        uintptr_t addr = user_addr + off;
        if (vmm_user_page_phys(pagemap, addr, false) == 0)
            return false;
    }

    /* Also check the page containing the last byte */
    uintptr_t last_byte = user_addr + len - 1;
    if (vmm_user_page_phys(pagemap, last_byte, false) == 0)
        return false;

    return true;
}

bool verify_user_write(uintptr_t user_addr, size_t len)
{
    if (!valid_user_range(user_addr, len))
        return false;

    if (len == 0)
        return true;

    process_t *proc = sched_get_current_process();
    if (!proc || !proc->pagemap)
        return false;

    pagemap_t *pagemap = proc->pagemap;

    for (size_t off = 0; off < len; off += VMM_PAGE_SIZE) {
        uintptr_t addr = user_addr + off;
        if (vmm_user_page_phys(pagemap, addr, true) == 0)
            return false;
    }

    /* Also check the page containing the last byte */
    uintptr_t last_byte = user_addr + len - 1;
    if (vmm_user_page_phys(pagemap, last_byte, true) == 0)
        return false;

    return true;
}
