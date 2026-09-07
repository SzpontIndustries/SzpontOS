/*
 * SzpontOS - Kernel Shared Memory Subsystem (SYSV IPC SHM & Zero-Copy)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <mm/shm.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <drivers/rtc.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>

static spinlock_t g_shm_lock = SPINLOCK_INIT;
static shm_segment_t g_shm_segments[MAX_SHM_SEGMENTS];
static int g_next_shmid = 1;

void shm_init(void) {
    spinlock_acquire(&g_shm_lock);
    memset(g_shm_segments, 0, sizeof(g_shm_segments));
    spinlock_release(&g_shm_lock);
    klog_info("SHM: Kernel Shared Memory Subsystem initialized (max %d segments)", MAX_SHM_SEGMENTS);
}

static shm_segment_t *shm_find_by_id(int shmid) {
    if (shmid <= 0)
        return NULL;
    for (size_t i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (g_shm_segments[i].allocated && g_shm_segments[i].shmid == shmid) {
            return &g_shm_segments[i];
        }
    }
    return NULL;
}

static shm_segment_t *shm_find_by_key(key_t key) {
    if (key == IPC_PRIVATE)
        return NULL;
    for (size_t i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (g_shm_segments[i].allocated && !g_shm_segments[i].marked_rmid && g_shm_segments[i].key == key) {
            return &g_shm_segments[i];
        }
    }
    return NULL;
}

int shm_get(key_t key, size_t size, int shmflg, int *out_shmid) {
    if (!out_shmid)
        return -22; /* EINVAL */

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    spinlock_acquire(&g_shm_lock);

    shm_segment_t *seg = NULL;
    if (key != IPC_PRIVATE) {
        seg = shm_find_by_key(key);
        if (seg) {
            if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
                spinlock_release(&g_shm_lock);
                return -17; /* EEXIST */
            }
            if (size > seg->size) {
                spinlock_release(&g_shm_lock);
                return -22; /* EINVAL */
            }
            *out_shmid = seg->shmid;
            spinlock_release(&g_shm_lock);
            return 0;
        }
    }

    if (key != IPC_PRIVATE && !(shmflg & IPC_CREAT)) {
        spinlock_release(&g_shm_lock);
        return -2; /* ENOENT */
    }

    if (size == 0 || size > VMM_USER_END - PAGE_SIZE) {
        spinlock_release(&g_shm_lock);
        return -22; /* EINVAL */
    }

    /* Allocate new SHM segment slot */
    for (size_t i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (!g_shm_segments[i].allocated) {
            seg = &g_shm_segments[i];
            break;
        }
    }

    if (!seg) {
        spinlock_release(&g_shm_lock);
        return -28; /* ENOSPC */
    }

    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t *pages = (uintptr_t *)kmalloc(sizeof(uintptr_t) * num_pages);
    if (!pages) {
        spinlock_release(&g_shm_lock);
        return -12; /* ENOMEM */
    }

    for (size_t i = 0; i < num_pages; i++) {
        uintptr_t phys = pmm_alloc_page();
        if (!phys) {
            for (size_t j = 0; j < i; j++) {
                pmm_free_page(pages[j]);
            }
            kfree(pages);
            spinlock_release(&g_shm_lock);
            return -12; /* ENOMEM */
        }
        memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        pages[i] = phys;
    }

    seg->shmid = g_next_shmid++;
    seg->key = key;
    seg->size = size;
    seg->num_pages = num_pages;
    seg->phys_pages = pages;
    seg->attach_count = 0;
    seg->creator_pid = proc->pid;
    seg->last_pid = proc->pid;
    seg->ctime = (time_t)rtc_get_current_epoch();
    seg->atime = 0;
    seg->dtime = 0;
    seg->mode = (mode_t)(shmflg & 0777);
    seg->uid = proc->uid;
    seg->gid = proc->gid;
    seg->allocated = true;
    seg->marked_rmid = false;

    *out_shmid = seg->shmid;
    spinlock_release(&g_shm_lock);
    return 0;
}

void *shm_at(int shmid, const void *shmaddr, int shmflg, process_t *proc) {
    (void)shmflg;
    if (!proc || shmid <= 0)
        return (void *)-22;

    spinlock_acquire(&g_shm_lock);
    shm_segment_t *seg = shm_find_by_id(shmid);
    if (!seg || !seg->allocated) {
        spinlock_release(&g_shm_lock);
        return (void *)-22; /* EINVAL */
    }

    /* Find empty mapping slot in process */
    int map_idx = -1;
    for (int i = 0; i < MAX_PROC_SHM; i++) {
        if (!proc->shm_mappings[i].active) {
            map_idx = i;
            break;
        }
    }

    if (map_idx == -1) {
        spinlock_release(&g_shm_lock);
        return (void *)-24; /* EMFILE */
    }

    if (proc->mmap_current == 0) {
        proc->mmap_current = 0x0000600000000000ULL;
    }

    uintptr_t vaddr = (uintptr_t)shmaddr;
    if (vaddr == 0) {
        vaddr = proc->mmap_current;
    }
    if ((vaddr & (PAGE_SIZE - 1)) || !vmm_user_range(vaddr, seg->num_pages * PAGE_SIZE)) {
        spinlock_release(&g_shm_lock);
        return (void *)-22;
    }
    for (size_t i = 0; i < seg->num_pages; i++) {
        if (vmm_virt_to_phys(proc->pagemap, vaddr + i * PAGE_SIZE)) {
            spinlock_release(&g_shm_lock);
            return (void *)-22;
        }
    }

    /* Map physical frames directly into process address space */
    for (size_t i = 0; i < seg->num_pages; i++) {
        if (!vmm_map_page(proc->pagemap, vaddr + i * PAGE_SIZE, seg->phys_pages[i],
                          VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_BORROWED)) {
            for (size_t j = 0; j < i; j++)
                vmm_release_user_page(proc->pagemap, vaddr + j * PAGE_SIZE);
            spinlock_release(&g_shm_lock);
            return (void *)-12;
        }
    }

    if (vaddr + seg->num_pages * PAGE_SIZE > proc->mmap_current)
        proc->mmap_current = vaddr + seg->num_pages * PAGE_SIZE;
    proc->shm_mappings[map_idx].shmid = seg->shmid;
    proc->shm_mappings[map_idx].vaddr = vaddr;
    proc->shm_mappings[map_idx].size = seg->size;
    proc->shm_mappings[map_idx].active = true;

    seg->attach_count++;
    seg->atime = (time_t)rtc_get_current_epoch();
    seg->last_pid = proc->pid;

    spinlock_release(&g_shm_lock);
    return (void *)vaddr;
}

int shm_dt(const void *shmaddr, process_t *proc) {
    if (!proc || !shmaddr)
        return -22;

    uintptr_t vaddr = (uintptr_t)shmaddr;
    spinlock_acquire(&g_shm_lock);

    int map_idx = -1;
    for (int i = 0; i < MAX_PROC_SHM; i++) {
        if (proc->shm_mappings[i].active && proc->shm_mappings[i].vaddr == vaddr) {
            map_idx = i;
            break;
        }
    }

    if (map_idx == -1) {
        spinlock_release(&g_shm_lock);
        return -22; /* EINVAL */
    }

    int shmid = proc->shm_mappings[map_idx].shmid;
    shm_segment_t *seg = shm_find_by_id(shmid);

    /* Unmap pages from process */
    if (seg) {
        for (size_t i = 0; i < seg->num_pages; i++) {
            uintptr_t virt = vaddr + i * PAGE_SIZE;
            if (vmm_virt_to_phys(proc->pagemap, virt) == seg->phys_pages[i])
                vmm_release_user_page(proc->pagemap, virt);
        }
    }

    proc->shm_mappings[map_idx].active = false;

    if (seg) {
        seg->attach_count--;
        seg->dtime = (time_t)rtc_get_current_epoch();
        seg->last_pid = proc->pid;

        if (seg->attach_count <= 0 && seg->marked_rmid) {
            /* Free segment memory */
            if (seg->phys_pages) {
                for (size_t i = 0; i < seg->num_pages; i++) {
                    if (seg->phys_pages[i])
                        pmm_free_page(seg->phys_pages[i]);
                }
                kfree(seg->phys_pages);
            }
            memset(seg, 0, sizeof(shm_segment_t));
        }
    }

    spinlock_release(&g_shm_lock);
    return 0;
}

int shm_ctl(int shmid, int cmd, struct shmid_ds *buf, process_t *proc) {
    if (shmid <= 0 || !proc)
        return -22;

    spinlock_acquire(&g_shm_lock);
    shm_segment_t *seg = shm_find_by_id(shmid);
    if (!seg || !seg->allocated) {
        spinlock_release(&g_shm_lock);
        return -22; /* EINVAL */
    }

    switch (cmd) {
    case IPC_RMID:
        seg->marked_rmid = true;
        if (seg->attach_count <= 0) {
            if (seg->phys_pages) {
                for (size_t i = 0; i < seg->num_pages; i++) {
                    if (seg->phys_pages[i])
                        pmm_free_page(seg->phys_pages[i]);
                }
                kfree(seg->phys_pages);
            }
            memset(seg, 0, sizeof(shm_segment_t));
        }
        spinlock_release(&g_shm_lock);
        return 0;

    case IPC_STAT:
        if (!buf) {
            spinlock_release(&g_shm_lock);
            return -14; /* EFAULT */
        }
        memset(buf, 0, sizeof(struct shmid_ds));
        buf->shm_perm.key = seg->key;
        buf->shm_perm.uid = seg->uid;
        buf->shm_perm.gid = seg->gid;
        buf->shm_perm.cuid = seg->uid;
        buf->shm_perm.cgid = seg->gid;
        buf->shm_perm.mode = seg->mode;
        buf->shm_segsz = seg->size;
        buf->shm_atime = seg->atime;
        buf->shm_dtime = seg->dtime;
        buf->shm_ctime = seg->ctime;
        buf->shm_cpid = seg->creator_pid;
        buf->shm_lpid = seg->last_pid;
        buf->shm_nattch = (unsigned long)seg->attach_count;
        spinlock_release(&g_shm_lock);
        return 0;

    case IPC_SET:
        if (!buf) {
            spinlock_release(&g_shm_lock);
            return -14; /* EFAULT */
        }
        seg->uid = buf->shm_perm.uid;
        seg->gid = buf->shm_perm.gid;
        seg->mode = buf->shm_perm.mode & 0777;
        seg->ctime = (time_t)rtc_get_current_epoch();
        spinlock_release(&g_shm_lock);
        return 0;

    default:
        spinlock_release(&g_shm_lock);
        return -22; /* EINVAL */
    }
}

void shm_process_exit(process_t *proc) {
    if (!proc)
        return;
    for (int i = 0; i < MAX_PROC_SHM; i++) {
        if (proc->shm_mappings[i].active) {
            shm_dt((const void *)proc->shm_mappings[i].vaddr, proc);
        }
    }
}
