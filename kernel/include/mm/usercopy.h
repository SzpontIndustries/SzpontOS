/*
 * SzpontOS - Safe User ↔ Kernel Memory Copy Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 *
 * Provides checked memory transfers between userspace and kernel space.
 * User virtual addresses are resolved through the process pagemap one page
 * at a time and copied through the kernel's HHDM physical direct map, so
 * malformed pointers return false / -EFAULT instead of taking a kernel-mode
 * page fault.
 *
 * Inspired by Vinix OS usercopy module.
 */

#ifndef SZPONTOS_MM_USERCOPY_H
#define SZPONTOS_MM_USERCOPY_H

#include <kernel/types.h>
#include <mm/vmm.h>

/*
 * Canonical userspace address boundary on x86_64.
 * Addresses above this belong to the kernel higher-half.
 */
#define USER_ADDR_MAX 0x00007FFFFFFFFFFFULL

/*
 * copy_from_user - Copy data from userspace to kernel buffer.
 *
 * @dst:      Kernel destination buffer (must be valid kernel pointer)
 * @user_src: Userspace virtual address to read from
 * @len:      Number of bytes to copy
 *
 * Returns true on success, false if the user address is invalid or unmapped.
 * On failure, partial data may have been written to @dst.
 */
bool copy_from_user(void *dst, uintptr_t user_src, size_t len);

/*
 * copy_to_user - Copy data from kernel buffer to userspace.
 *
 * @user_dst: Userspace virtual address to write to
 * @src:      Kernel source buffer (must be valid kernel pointer)
 * @len:      Number of bytes to copy
 *
 * Returns true on success, false if the user address is invalid or unmapped.
 * The target page must be mapped writable; otherwise this returns false.
 */
bool copy_to_user(uintptr_t user_dst, const void *src, size_t len);

/*
 * copy_string_from_user - Copy a NUL-terminated string from userspace.
 *
 * @dst:      Kernel destination buffer
 * @user_src: Userspace virtual address of the string
 * @max_len:  Maximum number of bytes to copy (including NUL terminator)
 *
 * Returns the length of the copied string (excluding NUL) on success,
 * or -1 if the address is invalid or the string exceeds max_len.
 */
ssize_t copy_string_from_user(char *dst, uintptr_t user_src, size_t max_len);

/*
 * verify_user_read - Check that a user address range is readable.
 *
 * @user_addr: Start of the user virtual address range
 * @len:       Length of the range in bytes
 *
 * Returns true if every page in [user_addr, user_addr+len) is mapped
 * and within the canonical userspace range.
 */
bool verify_user_read(uintptr_t user_addr, size_t len);

/*
 * verify_user_write - Check that a user address range is writable.
 *
 * @user_addr: Start of the user virtual address range
 * @len:       Length of the range in bytes
 *
 * Returns true if every page in [user_addr, user_addr+len) is mapped
 * writable and within the canonical userspace range.
 */
bool verify_user_write(uintptr_t user_addr, size_t len);

#endif /* SZPONTOS_MM_USERCOPY_H */
