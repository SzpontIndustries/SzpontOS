#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

#define HEAP_ALIGNMENT 16
#define HEAP_MAGIC_ALLOC 0x5A504F4E /* "SZPON" */
#define HEAP_MAGIC_FREE 0x46524545  /* "FREE" */

typedef struct heap_block {
    uint32_t magic;
    uint32_t is_free;
    size_t size; /* Usable data size */
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

#define BLOCK_HEADER_SIZE ALIGN_UP(sizeof(heap_block_t), HEAP_ALIGNMENT)

static heap_block_t *g_heap_head = NULL;
static spinlock_t g_heap_lock = SPINLOCK_INIT;
static size_t g_heap_allocated_bytes = 0;
static size_t g_heap_total_bytes = 0;

static void heap_expand(size_t size) {
    size = ALIGN_UP(size + BLOCK_HEADER_SIZE, PAGE_SIZE);
    size_t pages = size / PAGE_SIZE;

    uintptr_t phys = pmm_alloc_pages(pages);
    if (!phys) {
        /* Out of physical memory: leave the heap as-is. The caller's
         * post-expand retry will find no suitable block and hit the
         * existing "Heap: Out of memory" panic instead of corrupting
         * whatever lives at PHYS_TO_VIRT(0). */
        return;
    }
    void *virt = PHYS_TO_VIRT(phys);

    heap_block_t *new_block = (heap_block_t *)virt;
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->is_free = 1;
    new_block->size = size - BLOCK_HEADER_SIZE;
    new_block->next = NULL;
    new_block->prev = NULL;

    g_heap_total_bytes += size;

    if (!g_heap_head) {
        g_heap_head = new_block;
    } else {
        heap_block_t *curr = g_heap_head;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = new_block;
        new_block->prev = curr;
    }
}

void heap_init(size_t initial_size) {
    spinlock_init(&g_heap_lock);
    heap_expand(initial_size);
    klog_info("Kernel heap initialized (%lu KiB initial pool)", initial_size / 1024);
}

void *kmalloc(size_t size) {
    if (size == 0)
        return NULL;

    size = ALIGN_UP(size, HEAP_ALIGNMENT);

    spinlock_acquire(&g_heap_lock);

    heap_block_t *curr = g_heap_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Check if we can split this block */
            if (curr->size >= size + BLOCK_HEADER_SIZE + HEAP_ALIGNMENT) {
                heap_block_t *split = (heap_block_t *)((uintptr_t)curr + BLOCK_HEADER_SIZE + size);
                split->magic = HEAP_MAGIC_FREE;
                split->is_free = 1;
                split->size = curr->size - size - BLOCK_HEADER_SIZE;
                split->next = curr->next;
                split->prev = curr;

                if (curr->next) {
                    curr->next->prev = split;
                }
                curr->next = split;
                curr->size = size;
            }

            curr->is_free = 0;
            curr->magic = HEAP_MAGIC_ALLOC;
            g_heap_allocated_bytes += curr->size;

            void *res = (void *)((uintptr_t)curr + BLOCK_HEADER_SIZE);
            spinlock_release(&g_heap_lock);
            return res;
        }
        curr = curr->next;
    }

    /* Expand heap if no suitable block found */
    heap_expand(size * 2);

    /* Try again on newly expanded chunk */
    curr = g_heap_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            if (curr->size >= size + BLOCK_HEADER_SIZE + HEAP_ALIGNMENT) {
                heap_block_t *split = (heap_block_t *)((uintptr_t)curr + BLOCK_HEADER_SIZE + size);
                split->magic = HEAP_MAGIC_FREE;
                split->is_free = 1;
                split->size = curr->size - size - BLOCK_HEADER_SIZE;
                split->next = curr->next;
                split->prev = curr;

                if (curr->next) {
                    curr->next->prev = split;
                }
                curr->next = split;
                curr->size = size;
            }

            curr->is_free = 0;
            curr->magic = HEAP_MAGIC_ALLOC;
            g_heap_allocated_bytes += curr->size;

            void *res = (void *)((uintptr_t)curr + BLOCK_HEADER_SIZE);
            spinlock_release(&g_heap_lock);
            return res;
        }
        curr = curr->next;
    }

    spinlock_release(&g_heap_lock);
    panic("Heap: Out of memory during kmalloc(%lu)!", size);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void *kcalloc(size_t num, size_t size) {
    if (num != 0 && size > ((size_t)-1 / num)) {
        /* num * size would overflow */
        return NULL;
    }
    return kzalloc(num * size);
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    spinlock_acquire(&g_heap_lock);

    heap_block_t *block = (heap_block_t *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);
    if (block->magic != HEAP_MAGIC_ALLOC) {
        spinlock_release(&g_heap_lock);
        klog_error("Heap: Corrupted block header or double-free detected at %p (magic=0x%x)!", ptr, block->magic);
        return;
    }

    block->is_free = 1;
    block->magic = HEAP_MAGIC_FREE;
    g_heap_allocated_bytes -= block->size;

    /* Coalesce with next block if adjacent in memory and free */
    if (block->next && block->next->is_free) {
        uintptr_t expected_next = (uintptr_t)block + BLOCK_HEADER_SIZE + block->size;
        if ((uintptr_t)block->next == expected_next) {
            block->size += BLOCK_HEADER_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) {
                block->next->prev = block;
            }
        }
    }

    /* Coalesce with previous block if adjacent in memory and free */
    if (block->prev && block->prev->is_free) {
        uintptr_t expected_curr = (uintptr_t)block->prev + BLOCK_HEADER_SIZE + block->prev->size;
        if ((uintptr_t)block == expected_curr) {
            block->prev->size += BLOCK_HEADER_SIZE + block->size;
            block->prev->next = block->next;
            if (block->next) {
                block->next->prev = block->prev;
            }
        }
    }

    spinlock_release(&g_heap_lock);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr)
        return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_block_t *block = (heap_block_t *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);
    if (block->size >= new_size) {
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

void heap_dump_stats(void) {
    klog_info("Heap stats: %lu KiB total, %lu KiB allocated, %lu KiB free", g_heap_total_bytes / 1024,
              g_heap_allocated_bytes / 1024, (g_heap_total_bytes - g_heap_allocated_bytes) / 1024);
}
