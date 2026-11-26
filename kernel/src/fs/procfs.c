/*
 * SzpontOS - ProcFS (Virtual Process & System Information Filesystem)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <fs/procfs.h>
#include <fs/vfs.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/module.h>
#include <kernel/sysctl.h>
#include <drivers/pci.h>
#include <drivers/xhci.h>
#include <drivers/ehci.h>
#include <arch/x86_64/pit.h>
#include <kernel/smp.h>

#define PROCFS_TYPE_ROOT 0
#define PROCFS_TYPE_PID_DIR 1
#define PROCFS_TYPE_MEMINFO 2
#define PROCFS_TYPE_CPUINFO 3
#define PROCFS_TYPE_VERSION 4
#define PROCFS_TYPE_UPTIME 5
#define PROCFS_TYPE_STAT 6
#define PROCFS_TYPE_LOADAVG 7
#define PROCFS_TYPE_MOUNTS 8
#define PROCFS_TYPE_FILESYSTEMS 9
#define PROCFS_TYPE_CMDLINE 10
#define PROCFS_TYPE_DEVICES 11
#define PROCFS_TYPE_SELF 12
#define PROCFS_TYPE_MODULES 13
#define PROCFS_TYPE_DMESG 14
#define PROCFS_TYPE_SYSCTL 15
#define PROCFS_TYPE_PCI 16
#define PROCFS_TYPE_USB 17

#define PROCFS_PID_STATUS 100
#define PROCFS_PID_CMDLINE 101
#define PROCFS_PID_COMM 102
#define PROCFS_PID_CWD 103
#define PROCFS_PID_EXE 104
#define PROCFS_PID_STAT 105
#define PROCFS_PID_STATM 106
#define PROCFS_PID_MAPS 107
#define PROCFS_PID_ENVIRON 108
#define PROCFS_PID_TASKDIR 109
#define PROCFS_PID_FDDIR 110
#define PROCFS_TID_DIR 111
#define PROCFS_TID_STATUS 112
#define PROCFS_TID_COMM 113
#define PROCFS_TID_STAT 114
#define PROCFS_FD_LINK 115
#define PROCFS_SYS_DIR 116
#define PROCFS_SYS_FILE 117

/* index values for PROCFS_SYS_FILE */
#define PROCFS_SYS_OSTYPE 0
#define PROCFS_SYS_OSRELEASE 1
#define PROCFS_SYS_HOSTNAME 2

typedef struct procfs_file_data {
    uint32_t type;
    pid_t pid;
    tid_t tid; /* TID-dir owner / TID-file owner */
    int index; /* fd number for FD_LINK, sys file id for SYS_FILE, sys dir level for SYS_DIR */
} procfs_file_data_t;

static vfs_ops_t g_procfs_root_ops;
static vfs_ops_t g_procfs_pid_dir_ops;
static vfs_ops_t g_procfs_file_ops;
static vfs_ops_t g_procfs_taskdir_ops;
static vfs_ops_t g_procfs_tiddir_ops;
static vfs_ops_t g_procfs_fddir_ops;
static vfs_ops_t g_procfs_fdlink_ops;
static vfs_ops_t g_procfs_sysdir_ops;
static vfs_node_t *g_procfs_root = NULL;

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
}

/* =========================================================================
 * Content Generators for System-Wide Files
 * ========================================================================= */

static size_t procfs_gen_meminfo(char *buf, size_t max_len) {
    uint64_t total = pmm_get_total_memory() / 1024;
    uint64_t free_k = pmm_get_free_memory() / 1024;
    uint64_t avail = free_k;
    uint64_t buffers = 256;
    uint64_t cached = 0;

    return ksnprintf(buf, max_len,
                     "MemTotal:       %8lu kB\n"
                     "MemFree:        %8lu kB\n"
                     "MemAvailable:   %8lu kB\n"
                     "Buffers:        %8lu kB\n"
                     "Cached:         %8lu kB\n"
                     "SwapCached:            0 kB\n"
                     "Active:         %8lu kB\n"
                     "Inactive:              0 kB\n"
                     "SwapTotal:             0 kB\n"
                     "SwapFree:              0 kB\n"
                     "Dirty:                 0 kB\n"
                     "Writeback:             0 kB\n"
                     "AnonPages:      %8lu kB\n"
                     "Mapped:         %8lu kB\n"
                     "Shmem:                 0 kB\n"
                     "Slab:               8192 kB\n",
                     total, free_k, avail, buffers, cached, (total > free_k ? total - free_k : 0),
                     (total > free_k ? total - free_k : 0), (total > free_k ? total - free_k : 0));
}

static size_t procfs_gen_cpuinfo(char *buf, size_t max_len) {
    uint32_t eax, ebx, ecx, edx;
    char vendor[13] = {0};
    cpuid(0, 0, &eax, (uint32_t *)&vendor[0], (uint32_t *)&vendor[8], (uint32_t *)&vendor[4]);

    char brand[49] = {0};
    uint32_t max_ext = 0;
    cpuid(0x80000000, 0, &max_ext, &ebx, &ecx, &edx);
    if (max_ext >= 0x80000004) {
        cpuid(0x80000002, 0, (uint32_t *)&brand[0], (uint32_t *)&brand[4], (uint32_t *)&brand[8],
              (uint32_t *)&brand[12]);
        cpuid(0x80000003, 0, (uint32_t *)&brand[16], (uint32_t *)&brand[20], (uint32_t *)&brand[24],
              (uint32_t *)&brand[28]);
        cpuid(0x80000004, 0, (uint32_t *)&brand[32], (uint32_t *)&brand[36], (uint32_t *)&brand[40],
              (uint32_t *)&brand[44]);
    } else {
        strcpy(brand, "x86_64 Virtual / Compatible Processor");
    }

    /* CPU Family / Model */
    uint32_t family = 6, model = 1, stepping = 0;
    uint32_t f_edx = 0, f_ecx = 0;
    if (eax >= 1) {
        cpuid(1, 0, &eax, &ebx, &f_ecx, &f_edx);
        stepping = eax & 0xF;
        model = (eax >> 4) & 0xF;
        family = (eax >> 8) & 0xF;
    }

    uint32_t ncpus = smp_get_cpu_count();
    if (ncpus == 0)
        ncpus = 1;

    size_t off = 0;
    for (uint32_t i = 0; i < ncpus && off < max_len; i++) {
        int written = ksnprintf(buf + off, max_len - off,
                                "processor\t: %u\n"
                                "vendor_id\t: %s\n"
                                "cpu family\t: %u\n"
                                "model\t\t: %u\n"
                                "model name\t: %s\n"
                                "stepping\t: %u\n"
                                "cpu MHz\t\t: 2400.000\n"
                                "cache size\t: 4096 KB\n"
                                "fpu\t\t: yes\n"
                                "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx "
                                "fxsr sse sse2 sse3 ssse3 sse4_1 sse4_2 avx\n"
                                "bogomips\t: 4800.00\n"
                                "clflush size\t: 64\n"
                                "address sizes\t: 48 bits physical, 48 bits virtual\n\n",
                                i, vendor, family, model, brand, stepping);
        if (written > 0) {
            off += (size_t)written;
        } else {
            break;
        }
    }
    return off;
}

static size_t procfs_gen_version(char *buf, size_t max_len) {
    return ksnprintf(buf, max_len, "SzpontOS version 0.1.0 (x86_64 Higher-Half) (gcc 13.2.0, Limine 8.x) #1 PREEMPT\n");
}

static size_t procfs_gen_uptime(char *buf, size_t max_len) {
    uint64_t ticks = pit_get_ticks();
    uint64_t freq = pit_get_frequency();
    uint64_t sec = ticks / freq;
    uint64_t csec = (ticks % freq) * 100 / freq;
    return ksnprintf(buf, max_len, "%lu.%02lu %lu.%02lu\n", sec, csec, sec, csec);
}

static size_t procfs_gen_stat(char *buf, size_t max_len) {
    uint64_t user = 0, sys = 0, idle = 0;
    sched_get_cpu_ticks(&user, &sys, &idle);

    uint64_t total = user + sys + idle;
    if (total == 0) {
        idle = 1;
    }

    uint64_t ticks = pit_get_ticks();
    proc_info_t procs[64];
    size_t count = process_get_list(procs, 64);

    return ksnprintf(buf, max_len,
                     "cpu  %lu 0 %lu %lu 0 0 0 0 0 0\n"
                     "cpu0 %lu 0 %lu %lu 0 0 0 0 0 0\n"
                     "intr %lu\n"
                     "ctxt %lu\n"
                     "btime 1700000000\n"
                     "processes %lu\n"
                     "procs_running 1\n"
                     "procs_blocked 0\n",
                     user, sys, idle, user, sys, idle, ticks * 2, ticks, count);
}

static size_t procfs_gen_loadavg(char *buf, size_t max_len) {
    proc_info_t procs[64];
    size_t count = process_get_list(procs, 64);
    return ksnprintf(buf, max_len, "0.01 0.01 0.00 1/%lu 42\n", count);
}

static size_t procfs_gen_mounts(char *buf, size_t max_len) {
    vfs_mount_info_t mounts[32];
    size_t count = vfs_get_mount_list(mounts, 32);
    size_t pos = 0;

    for (size_t i = 0; i < count && pos < max_len; i++) {
        const char *fs_type = "rootfs";
        const char *dev_name = mounts[i].name;

        if (strcmp(mounts[i].path, "/dev") == 0)
            fs_type = "devfs";
        else if (strcmp(mounts[i].path, "/proc") == 0)
            fs_type = "procfs";
        else if (strcmp(mounts[i].path, "/mnt") == 0)
            fs_type = "ext2";

        pos += ksnprintf(buf + pos, max_len - pos, "%s %s %s rw,relatime 0 0\n", dev_name, mounts[i].path, fs_type);
    }
    return pos;
}

static size_t procfs_gen_filesystems(char *buf, size_t max_len) {
    return ksnprintf(buf, max_len,
                     "nodev\trootfs\n"
                     "nodev\tdevfs\n"
                     "nodev\tprocfs\n"
                     "\text2\n");
}

static size_t procfs_gen_cmdline(char *buf, size_t max_len) {
    return ksnprintf(buf, max_len, "BOOT_IMAGE=/boot/szpontos-kernel console=tty0 root=/dev/ram0\n");
}

static size_t procfs_gen_devices(char *buf, size_t max_len) {
    return ksnprintf(buf, max_len,
                     "Character devices:\n"
                     "  1 mem\n"
                     "  4 tty\n"
                     "  5 /dev/tty\n"
                     "  5 /dev/serial\n"
                     "\n"
                     "Block devices:\n"
                     "  8 hda\n");
}

static size_t procfs_gen_self(char *buf, size_t max_len) {
    process_t *proc = sched_get_current_process();
    pid_t pid = proc ? proc->pid : 1;
    return ksnprintf(buf, max_len, "/proc/%d\n", pid);
}

struct procfs_module_ctx {
    char *buf;
    size_t max_len;
    size_t offset;
};

static void procfs_module_formatter(module_t *mod, void *arg) {
    struct procfs_module_ctx *ctx = (struct procfs_module_ctx *)arg;
    if (ctx->offset >= ctx->max_len)
        return;

    const char *state_str = "Live";
    if (mod->state == MODULE_STATE_COMING)
        state_str = "Loading";
    else if (mod->state == MODULE_STATE_GOING)
        state_str = "Unloading";

    ctx->offset += ksnprintf(ctx->buf + ctx->offset, ctx->max_len - ctx->offset, "%s %lu %d - %s 0x%lx\n", mod->name,
                             mod->size, mod->refcnt, state_str, mod->base_addr);
}

static size_t procfs_gen_modules(char *buf, size_t max_len) {
    struct procfs_module_ctx ctx = {buf, max_len, 0};
    module_list_for_each(procfs_module_formatter, &ctx);
    return ctx.offset;
}

static size_t procfs_gen_sysctl(char *buf, size_t max_len) {
    return sysctl_get_all(buf, max_len);
}

static size_t procfs_gen_pci(char *buf, size_t max_len) {
    size_t pos = 0;
    for (pci_device_t *dev = pci_get_device_list(); dev != NULL && pos < max_len; dev = dev->next) {
        pos += ksnprintf(buf + pos, max_len - pos,
                         "%02x:%02x.%x %02x%02x: %04x:%04x (rev 00, prog-if %02x) IRQ %u",
                         dev->bus, dev->slot, dev->func, dev->class_code, dev->subclass,
                         dev->vendor_id, dev->device_id, dev->prog_if, dev->irq);
        for (int i = 0; i < 6 && pos < max_len; i++) {
            if (dev->bar[i] != 0) {
                pos += ksnprintf(buf + pos, max_len - pos, " [BAR%d: 0x%lx (%s)]",
                                 i, (unsigned long)dev->bar[i], dev->bar_is_io[i] ? "I/O" : "MMIO");
            }
        }
        pos += ksnprintf(buf + pos, max_len - pos, "\n");
    }
    return pos;
}

static size_t procfs_gen_usb(char *buf, size_t max_len) {
    size_t pos = xhci_get_device_list_info(buf, max_len);
    if (pos < max_len) {
        pos += ehci_get_device_list_info(buf + pos, max_len - pos);
    }
    if (pos == 0) {
        pos += ksnprintf(buf, max_len, "No active USB host controllers or devices found.\n");
    }
    return pos;
}

/* =========================================================================
 * Content Generators for Per-Process Files (/proc/<pid>/)
 * ========================================================================= */

static size_t procfs_gen_proc_status(process_t *proc, char *buf, size_t max_len) {
    const char *state_str = "R (running)";
    if (proc->status == PROCESS_ZOMBIE)
        state_str = "Z (zombie)";
    else if (proc->status == PROCESS_DEAD)
        state_str = "X (dead)";

    size_t rss_pages = (proc->pagemap) ? vmm_count_user_pages(proc->pagemap) : 0;
    size_t rss_kb = rss_pages * 4;
    size_t vmsize_kb = rss_kb;
    if (proc->brk_current > proc->brk_start) {
        vmsize_kb += (proc->brk_current - proc->brk_start) / 1024;
    }
    if (vmsize_kb < rss_kb + 4096) {
        vmsize_kb = rss_kb + 4096;
    }
    if (proc->status == PROCESS_ZOMBIE) {
        rss_kb = 0;
        vmsize_kb = 0;
    }

    return ksnprintf(buf, max_len,
                     "Name:\t%s\n"
                     "Umask:\t0022\n"
                     "State:\t%s\n"
                     "Tgid:\t%d\n"
                     "Pid:\t%d\n"
                     "PPid:\t%d\n"
                     "Uid:\t%d\t%d\t%d\t%d\n"
                     "Gid:\t%d\t%d\t%d\t%d\n"
                     "FDSize:\t%d\n"
                     "Groups:\t%d\n"
                     "Threads:\t1\n"
                     "VmPeak:\t    %zu kB\n"
                     "VmSize:\t    %zu kB\n"
                     "VmRSS:\t      %zu kB\n",
                     proc->name, state_str, proc->pid, proc->pid, proc->ppid, proc->uid, proc->euid, proc->uid,
                     proc->euid, proc->gid, proc->egid, proc->gid, proc->egid, MAX_FD, proc->gid,
                     vmsize_kb, vmsize_kb, rss_kb);
}

static size_t procfs_gen_proc_cmdline(process_t *proc, char *buf, size_t max_len) {
    return ksnprintf(buf, max_len, "%s\n", proc->name);
}

static size_t procfs_gen_proc_comm(process_t *proc, char *buf, size_t max_len) {
    const char *comm = proc->name;
    const char *slash = strrchr(proc->name, '/');
    if (slash)
        comm = slash + 1;
    return ksnprintf(buf, max_len, "%s\n", comm);
}

static size_t procfs_gen_proc_cwd(process_t *proc, char *buf, size_t max_len) {
    return ksnprintf(buf, max_len, "%s\n", proc->cwd[0] ? proc->cwd : "/");
}

static size_t procfs_gen_proc_exe(process_t *proc, char *buf, size_t max_len) {
    if (!proc || !buf || max_len == 0)
        return 0;
    if (proc->name[0] == '/') {
        return ksnprintf(buf, max_len, "%s\n", proc->name);
    } else {
        return ksnprintf(buf, max_len, "/bin/%s\n", proc->name);
    }
}

static size_t procfs_gen_proc_stat(process_t *proc, char *buf, size_t max_len) {
    char state_char = 'R';
    if (proc->status == PROCESS_ZOMBIE)
        state_char = 'Z';
    else if (proc->status == PROCESS_DEAD)
        state_char = 'X';

    const char *comm = proc->name;
    const char *slash = strrchr(proc->name, '/');
    if (slash)
        comm = slash + 1;

    return ksnprintf(buf, max_len,
                     "%d (%s) %c %d %d %d 0 -1 4194304 0 0 0 0 10 5 0 0 20 0 1 0 100 8388608 16 18446744073709551615 "
                     "4194304 4362240 140737218686976 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0\n",
                     proc->pid, comm, state_char, proc->ppid, proc->pid, proc->pid);
}

static size_t procfs_gen_proc_statm(process_t *proc, char *buf, size_t max_len) {
    size_t rss_pages = (proc->pagemap) ? vmm_count_user_pages(proc->pagemap) : 0;
    return ksnprintf(buf, max_len, "%zu %zu 0 0 0 %zu 0\n", rss_pages, rss_pages, rss_pages);
}

static size_t procfs_gen_proc_maps(process_t *proc, char *buf, size_t max_len) {
    uintptr_t brk_c = proc->brk_current ? proc->brk_current : proc->brk_start + 4096;
    return ksnprintf(buf, max_len,
                     "00400000-00428000 r-xp 00000000 00:00 0 %s\n"
                     "00428000-0042a000 rw-p 00028000 00:00 0 %s\n"
                     "%08lx-%08lx rwxp 00000000 00:00 0 [heap]\n"
                     "7ffff0000000-7ffff0010000 rw-p 00000000 00:00 0 [stack]\n",
                     proc->name, proc->name, proc->brk_start, brk_c);
}

static size_t procfs_gen_proc_environ(process_t *proc, char *buf, size_t max_len) {
    (void)proc;
    return ksnprintf(buf, max_len, "PATH=/bin:/usr/bin:/usr/tbin\nUSER=root\nHOME=/root\nTERM=xterm-256color\n");
}

static const char *procfs_thread_state_str(thread_state_t state) {
    switch (state) {
    case THREAD_RUNNING:
    case THREAD_READY:
        return "R (running)";
    case THREAD_BLOCKED:
    case THREAD_SLEEPING:
        return "S (sleeping)";
    case THREAD_ZOMBIE:
        return "Z (zombie)";
    default:
        return "? (unknown)";
    }
}

static size_t procfs_gen_tid_status(process_t *proc, thread_t *t, char *buf, size_t max_len) {
    const char *name = t->name[0] ? t->name : proc->name;
    return ksnprintf(buf, max_len,
                     "Name:\t%s\n"
                     "State:\t%s\n"
                     "Tgid:\t%d\n"
                     "Pid:\t%d\n"
                     "PPid:\t%d\n"
                     "Uid:\t%d\t%d\t%d\t%d\n"
                     "Gid:\t%d\t%d\t%d\t%d\n",
                     name, procfs_thread_state_str(t->state), proc->pid, t->tid, proc->ppid, proc->uid, proc->euid,
                     proc->uid, proc->euid, proc->gid, proc->egid, proc->gid, proc->egid);
}

static size_t procfs_gen_tid_comm(process_t *proc, thread_t *t, char *buf, size_t max_len) {
    const char *comm = t->name[0] ? t->name : proc->name;
    const char *slash = strrchr(comm, '/');
    if (slash)
        comm = slash + 1;
    return ksnprintf(buf, max_len, "%s\n", comm);
}

static size_t procfs_gen_tid_stat(process_t *proc, thread_t *t, char *buf, size_t max_len) {
    char state_char = 'R';
    if (t->state == THREAD_BLOCKED || t->state == THREAD_SLEEPING)
        state_char = 'S';
    else if (t->state == THREAD_ZOMBIE)
        state_char = 'Z';

    const char *comm = t->name[0] ? t->name : proc->name;
    const char *slash = strrchr(comm, '/');
    if (slash)
        comm = slash + 1;

    return ksnprintf(buf, max_len, "%d (%s) %c %d %d %d 0 -1 4194304 0 0 0 0 0 0 0 0 20 0 1 0 100 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
                     t->tid, comm, state_char, proc->ppid, proc->pid, proc->pgid);
}

static size_t procfs_gen_fd_link(process_t *proc, int fd, char *buf, size_t max_len) {
    if (!proc || fd < 0 || fd >= MAX_FD || !proc->fds[fd] || !proc->fds[fd]->node)
        return 0;
    vfs_node_t *target = proc->fds[fd]->node;
    if (target->flags == VFS_TYPE_SOCKET)
        return ksnprintf(buf, max_len, "socket:[%u]", target->inode);
    if (target->flags == VFS_TYPE_PIPE)
        return ksnprintf(buf, max_len, "pipe:[%u]", target->inode);
    /* Regular nodes only carry a basename; still identifies the target. */
    return ksnprintf(buf, max_len, "/%s", target->name[0] ? target->name : "unknown");
}

static size_t procfs_gen_sys_file(int id, char *buf, size_t max_len) {
    switch (id) {
    case PROCFS_SYS_OSTYPE:
        return ksnprintf(buf, max_len, "SzpontOS\n");
    case PROCFS_SYS_OSRELEASE:
        return ksnprintf(buf, max_len, "0.1.0\n");
    case PROCFS_SYS_HOSTNAME: {
        char hostname[64] = "szpontos";
        vfs_node_t *node = vfs_lookup("/etc/hostname");
        if (node && node->ops && node->ops->read) {
            char file_buf[64];
            ssize_t bytes = node->ops->read(node, 0, sizeof(file_buf) - 1, (uint8_t *)file_buf);
            if (bytes > 0) {
                file_buf[bytes] = '\0';
                while (bytes > 0 && (file_buf[bytes - 1] == '\n' || file_buf[bytes - 1] == '\r' ||
                                     file_buf[bytes - 1] == ' ' || file_buf[bytes - 1] == '\t')) {
                    file_buf[bytes - 1] = '\0';
                    bytes--;
                }
                if (bytes > 0) {
                    strncpy(hostname, file_buf, sizeof(hostname) - 1);
                    hostname[sizeof(hostname) - 1] = '\0';
                }
            }
        }
        return ksnprintf(buf, max_len, "%s\n", hostname);
    }
    default:
        return 0;
    }
}

/* Resolve a pid-dir pid to a process, mapping 0 to the caller. */
static process_t *procfs_resolve_pid(pid_t pid) {
    if (pid == 0)
        return sched_get_current_process();
    return process_get_by_pid(pid);
}

/* =========================================================================
 * VFS Operations: File Read
 * ========================================================================= */

static ssize_t procfs_file_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    if (!node || !buffer || size == 0)
        return 0;

    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    if (!data)
        return 0;

    if (data->type == PROCFS_TYPE_DMESG) {
        return (ssize_t)klog_read_ring((char *)buffer, size, (size_t)offset);
    }

    char tmp_buf[4096];
    memset(tmp_buf, 0, sizeof(tmp_buf));
    size_t content_len = 0;

    switch (data->type) {
    case PROCFS_TYPE_MEMINFO:
        content_len = procfs_gen_meminfo(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_CPUINFO:
        content_len = procfs_gen_cpuinfo(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_VERSION:
        content_len = procfs_gen_version(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_UPTIME:
        content_len = procfs_gen_uptime(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_STAT:
        content_len = procfs_gen_stat(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_LOADAVG:
        content_len = procfs_gen_loadavg(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_MOUNTS:
        content_len = procfs_gen_mounts(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_FILESYSTEMS:
        content_len = procfs_gen_filesystems(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_CMDLINE:
        content_len = procfs_gen_cmdline(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_DEVICES:
        content_len = procfs_gen_devices(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_SELF:
        content_len = procfs_gen_self(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_MODULES:
        content_len = procfs_gen_modules(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_SYSCTL:
        content_len = procfs_gen_sysctl(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_PCI:
        content_len = procfs_gen_pci(tmp_buf, sizeof(tmp_buf));
        break;
    case PROCFS_TYPE_USB:
        content_len = procfs_gen_usb(tmp_buf, sizeof(tmp_buf));
        break;
    default: {
        process_t *proc = procfs_resolve_pid(data->pid);
        if (!proc)
            return 0;

        switch (data->type) {
        case PROCFS_PID_STATUS:
            content_len = procfs_gen_proc_status(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_CMDLINE:
            content_len = procfs_gen_proc_cmdline(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_COMM:
            content_len = procfs_gen_proc_comm(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_CWD:
            content_len = procfs_gen_proc_cwd(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_EXE:
            content_len = procfs_gen_proc_exe(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_STAT:
            content_len = procfs_gen_proc_stat(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_STATM:
            content_len = procfs_gen_proc_statm(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_MAPS:
            content_len = procfs_gen_proc_maps(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_PID_ENVIRON:
            content_len = procfs_gen_proc_environ(proc, tmp_buf, sizeof(tmp_buf));
            break;
        case PROCFS_TID_STATUS:
        case PROCFS_TID_COMM:
        case PROCFS_TID_STAT: {
            thread_t *t = process_get_thread_by_tid(proc, data->tid);
            if (!t)
                return 0;
            if (data->type == PROCFS_TID_STATUS)
                content_len = procfs_gen_tid_status(proc, t, tmp_buf, sizeof(tmp_buf));
            else if (data->type == PROCFS_TID_COMM)
                content_len = procfs_gen_tid_comm(proc, t, tmp_buf, sizeof(tmp_buf));
            else
                content_len = procfs_gen_tid_stat(proc, t, tmp_buf, sizeof(tmp_buf));
            break;
        }
        case PROCFS_SYS_FILE:
            content_len = procfs_gen_sys_file(data->index, tmp_buf, sizeof(tmp_buf));
            break;
        default:
            break;
        }
        break;
    }
    }

    if (offset >= (off_t)content_len) {
        return 0; /* EOF */
    }

    size_t available = content_len - offset;
    size_t to_copy = (size < available) ? size : available;
    memcpy(buffer, tmp_buf + offset, to_copy);
    return (ssize_t)to_copy;
}

static vfs_node_t *procfs_create_node(const char *name, uint32_t type, pid_t pid, tid_t tid, int index,
                                        vfs_ops_t *ops, uint32_t vfs_type, uint32_t perm) {
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node)
        return NULL;

    strncpy(node->name, name, sizeof(node->name) - 1);
    node->flags = vfs_type;
    node->permissions = perm;
    node->uid = 0;
    node->gid = 0;
    node->ops = ops;

    procfs_file_data_t *data = (procfs_file_data_t *)kzalloc(sizeof(procfs_file_data_t));
    if (!data) {
        kfree(node);
        return NULL;
    }
    data->type = type;
    data->pid = pid;
    data->tid = tid;
    data->index = index;
    node->device_data = data;

    return node;
}

static vfs_node_t *procfs_create_file_node(const char *name, uint32_t type, pid_t pid) {
    return procfs_create_node(name, type, pid, 0, 0, &g_procfs_file_ops, VFS_TYPE_FILE, 0444);
}

static ssize_t procfs_fdlink_readlink(vfs_node_t *node, char *buf, size_t bufsiz) {
    if (!node || !buf || bufsiz == 0)
        return -22; /* -EINVAL */
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    if (!data)
        return -2; /* -ENOENT */
    process_t *proc = procfs_resolve_pid(data->pid);
    if (!proc)
        return -2; /* -ENOENT */

    char target[160];
    size_t len = procfs_gen_fd_link(proc, data->index, target, sizeof(target));
    if (len == 0)
        return -2; /* -ENOENT: fd closed since lookup */
    if (len > bufsiz)
        len = bufsiz;
    memcpy(buf, target, len);
    return (ssize_t)len;
}

/* =========================================================================
 * VFS Operations: Directory Readdir & Finddir for /proc/<pid>/
 * ========================================================================= */

static const char *g_proc_pid_files[] = {"status", "cmdline", "comm",  "cwd",  "exe",  "stat", "statm",
                                           "maps",   "environ", "task", "fd"};
#define PROC_PID_FILE_COUNT (sizeof(g_proc_pid_files) / sizeof(g_proc_pid_files[0]))

static bool procfs_pid_entry_is_dir(const char *name) {
    return strcmp(name, "task") == 0 || strcmp(name, "fd") == 0;
}

static struct vfs_dirent *procfs_pid_dir_readdir(vfs_node_t *node, uint32_t index) {
    (void)node;
    static vfs_dirent_t dirent;
    if (index >= PROC_PID_FILE_COUNT)
        return NULL;

    strncpy(dirent.name, g_proc_pid_files[index], sizeof(dirent.name) - 1);
    dirent.inode = index + 100;
    dirent.type = procfs_pid_entry_is_dir(g_proc_pid_files[index]) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
    return &dirent;
}

static struct vfs_node *procfs_pid_dir_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name)
        return NULL;
    procfs_file_data_t *dir_data = (procfs_file_data_t *)node->device_data;
    pid_t pid = dir_data ? dir_data->pid : 0;

    if (strcmp(name, "status") == 0)
        return procfs_create_file_node("status", PROCFS_PID_STATUS, pid);
    if (strcmp(name, "cmdline") == 0)
        return procfs_create_file_node("cmdline", PROCFS_PID_CMDLINE, pid);
    if (strcmp(name, "comm") == 0)
        return procfs_create_file_node("comm", PROCFS_PID_COMM, pid);
    if (strcmp(name, "cwd") == 0)
        return procfs_create_file_node("cwd", PROCFS_PID_CWD, pid);
    if (strcmp(name, "exe") == 0)
        return procfs_create_file_node("exe", PROCFS_PID_EXE, pid);
    if (strcmp(name, "stat") == 0)
        return procfs_create_file_node("stat", PROCFS_PID_STAT, pid);
    if (strcmp(name, "statm") == 0)
        return procfs_create_file_node("statm", PROCFS_PID_STATM, pid);
    if (strcmp(name, "maps") == 0)
        return procfs_create_file_node("maps", PROCFS_PID_MAPS, pid);
    if (strcmp(name, "environ") == 0)
        return procfs_create_file_node("environ", PROCFS_PID_ENVIRON, pid);
    if (strcmp(name, "task") == 0)
        return procfs_create_node("task", PROCFS_PID_TASKDIR, pid, 0, 0, &g_procfs_taskdir_ops,
                                  VFS_TYPE_DIRECTORY, 0555);
    if (strcmp(name, "fd") == 0)
        return procfs_create_node("fd", PROCFS_PID_FDDIR, pid, 0, 0, &g_procfs_fddir_ops, VFS_TYPE_DIRECTORY,
                                  0555);

    return NULL;
}

/* =========================================================================
 * VFS Operations: /proc/<pid>/task/ and /proc/<pid>/task/<tid>/
 * The tree is rebuilt from the process table on every lookup/readdir, so a
 * directory can never describe a thread that has already gone.
 * ========================================================================= */

static struct vfs_dirent *procfs_taskdir_readdir(vfs_node_t *node, uint32_t index) {
    static vfs_dirent_t dirent;
    if (!node)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    process_t *proc = data ? procfs_resolve_pid(data->pid) : NULL;
    if (!proc)
        return NULL;

    tid_t tids[64];
    size_t count = process_get_tid_list(proc, tids, 64);
    if (index >= count)
        return NULL;

    ksnprintf(dirent.name, sizeof(dirent.name), "%d", tids[index]);
    dirent.inode = 2000 + tids[index];
    dirent.type = VFS_TYPE_DIRECTORY;
    return &dirent;
}

static struct vfs_node *procfs_taskdir_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    pid_t pid = data ? data->pid : 0;

    bool is_num = name[0] != '\0';
    for (size_t i = 0; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9') {
            is_num = false;
            break;
        }
    }
    if (!is_num)
        return NULL;

    int tid = 0;
    for (size_t i = 0; name[i]; i++) {
        tid = tid * 10 + (name[i] - '0');
    }

    process_t *proc = procfs_resolve_pid(pid);
    if (!proc || !process_get_thread_by_tid(proc, tid))
        return NULL;
    return procfs_create_node(name, PROCFS_TID_DIR, pid, tid, 0, &g_procfs_tiddir_ops, VFS_TYPE_DIRECTORY, 0555);
}

static const char *g_proc_tid_files[] = {"status", "comm", "stat"};
#define PROC_TID_FILE_COUNT (sizeof(g_proc_tid_files) / sizeof(g_proc_tid_files[0]))

static struct vfs_dirent *procfs_tiddir_readdir(vfs_node_t *node, uint32_t index) {
    (void)node;
    static vfs_dirent_t dirent;
    if (index >= PROC_TID_FILE_COUNT)
        return NULL;

    strncpy(dirent.name, g_proc_tid_files[index], sizeof(dirent.name) - 1);
    dirent.inode = 2100 + index;
    dirent.type = VFS_TYPE_FILE;
    return &dirent;
}

static struct vfs_node *procfs_tiddir_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    if (!data)
        return NULL;

    /* The thread may have exited since the task/ listing; re-validate. */
    process_t *proc = procfs_resolve_pid(data->pid);
    if (!proc || !process_get_thread_by_tid(proc, data->tid))
        return NULL;

    if (strcmp(name, "status") == 0)
        return procfs_create_node("status", PROCFS_TID_STATUS, data->pid, data->tid, 0, &g_procfs_file_ops,
                                  VFS_TYPE_FILE, 0444);
    if (strcmp(name, "comm") == 0)
        return procfs_create_node("comm", PROCFS_TID_COMM, data->pid, data->tid, 0, &g_procfs_file_ops,
                                  VFS_TYPE_FILE, 0444);
    if (strcmp(name, "stat") == 0)
        return procfs_create_node("stat", PROCFS_TID_STAT, data->pid, data->tid, 0, &g_procfs_file_ops,
                                  VFS_TYPE_FILE, 0444);
    return NULL;
}

/* =========================================================================
 * VFS Operations: /proc/<pid>/fd/ (symlink farm over open descriptors)
 * ========================================================================= */

static struct vfs_dirent *procfs_fddir_readdir(vfs_node_t *node, uint32_t index) {
    static vfs_dirent_t dirent;
    if (!node)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    process_t *proc = data ? procfs_resolve_pid(data->pid) : NULL;
    if (!proc)
        return NULL;

    uint32_t seen = 0;
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!proc->fds[fd])
            continue;
        if (seen == index) {
            ksnprintf(dirent.name, sizeof(dirent.name), "%d", fd);
            dirent.inode = 3000 + fd;
            dirent.type = VFS_TYPE_SYMLINK;
            return &dirent;
        }
        seen++;
    }
    return NULL;
}

static struct vfs_node *procfs_fddir_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    pid_t pid = data ? data->pid : 0;

    bool is_num = name[0] != '\0';
    for (size_t i = 0; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9') {
            is_num = false;
            break;
        }
    }
    if (!is_num)
        return NULL;

    int fd = 0;
    for (size_t i = 0; name[i]; i++) {
        fd = fd * 10 + (name[i] - '0');
    }
    if (fd < 0 || fd >= MAX_FD)
        return NULL;

    process_t *proc = procfs_resolve_pid(pid);
    if (!proc || !proc->fds[fd])
        return NULL;
    return procfs_create_node(name, PROCFS_FD_LINK, pid, 0, fd, &g_procfs_fdlink_ops, VFS_TYPE_SYMLINK, 0777);
}

/* =========================================================================
 * VFS Operations: /proc/sys/ (kernel.* parameters Chromium reads at startup)
 * ========================================================================= */

static struct vfs_dirent *procfs_sysdir_readdir(vfs_node_t *node, uint32_t index) {
    static vfs_dirent_t dirent;
    if (!node)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    int level = data ? data->index : 0;

    if (level == 0) {
        if (index != 0)
            return NULL;
        strncpy(dirent.name, "kernel", sizeof(dirent.name) - 1);
        dirent.inode = 4000;
        dirent.type = VFS_TYPE_DIRECTORY;
        return &dirent;
    }

    static const char *kfiles[] = {"ostype", "osrelease", "hostname"};
    if (index >= 3)
        return NULL;
    strncpy(dirent.name, kfiles[index], sizeof(dirent.name) - 1);
    dirent.inode = 4100 + index;
    dirent.type = VFS_TYPE_FILE;
    return &dirent;
}

static struct vfs_node *procfs_sysdir_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name)
        return NULL;
    procfs_file_data_t *data = (procfs_file_data_t *)node->device_data;
    int level = data ? data->index : 0;

    if (level == 0) {
        if (strcmp(name, "kernel") == 0)
            return procfs_create_node("kernel", PROCFS_SYS_DIR, 0, 0, 1, &g_procfs_sysdir_ops,
                                      VFS_TYPE_DIRECTORY, 0555);
        return NULL;
    }

    if (strcmp(name, "ostype") == 0)
        return procfs_create_node("ostype", PROCFS_SYS_FILE, 0, 0, PROCFS_SYS_OSTYPE, &g_procfs_file_ops,
                                  VFS_TYPE_FILE, 0444);
    if (strcmp(name, "osrelease") == 0)
        return procfs_create_node("osrelease", PROCFS_SYS_FILE, 0, 0, PROCFS_SYS_OSRELEASE, &g_procfs_file_ops,
                                  VFS_TYPE_FILE, 0444);
    if (strcmp(name, "hostname") == 0)
        return procfs_create_node("hostname", PROCFS_SYS_FILE, 0, 0, PROCFS_SYS_HOSTNAME, &g_procfs_file_ops,
                                  VFS_TYPE_FILE, 0444);
    return NULL;
}

static vfs_node_t *procfs_create_pid_dir(pid_t pid) {
    vfs_node_t *dir = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!dir)
        return NULL;

    ksnprintf(dir->name, sizeof(dir->name), "%d", pid);
    dir->flags = VFS_TYPE_DIRECTORY;
    dir->permissions = 0555;
    dir->uid = 0;
    dir->gid = 0;
    dir->ops = &g_procfs_pid_dir_ops;

    procfs_file_data_t *data = (procfs_file_data_t *)kzalloc(sizeof(procfs_file_data_t));
    data->type = PROCFS_TYPE_PID_DIR;
    data->pid = pid;
    dir->device_data = data;

    return dir;
}

/* =========================================================================
 * VFS Operations: Directory Readdir & Finddir for /proc/
 * ========================================================================= */

static const struct {
    const char *name;
    uint32_t type;
} g_proc_static_entries[] = {{"meminfo", PROCFS_TYPE_MEMINFO}, {"cpuinfo", PROCFS_TYPE_CPUINFO},
                             {"version", PROCFS_TYPE_VERSION}, {"uptime", PROCFS_TYPE_UPTIME},
                             {"stat", PROCFS_TYPE_STAT},       {"loadavg", PROCFS_TYPE_LOADAVG},
                             {"mounts", PROCFS_TYPE_MOUNTS},   {"filesystems", PROCFS_TYPE_FILESYSTEMS},
                             {"cmdline", PROCFS_TYPE_CMDLINE}, {"devices", PROCFS_TYPE_DEVICES},
                             {"self", PROCFS_TYPE_SELF},       {"modules", PROCFS_TYPE_MODULES},
                             {"dmesg", PROCFS_TYPE_DMESG},     {"kmsg", PROCFS_TYPE_DMESG},
                             {"sysctl", PROCFS_TYPE_SYSCTL},   {"pci", PROCFS_TYPE_PCI},
                             {"usb", PROCFS_TYPE_USB},         {"sys", PROCFS_SYS_DIR}};
#define PROC_STATIC_COUNT (sizeof(g_proc_static_entries) / sizeof(g_proc_static_entries[0]))

static bool procfs_static_is_special(const char *name) {
    return strcmp(name, "self") == 0 || strcmp(name, "sys") == 0;
}

static struct vfs_dirent *procfs_root_readdir(vfs_node_t *node, uint32_t index) {
    (void)node;
    static vfs_dirent_t dirent;

    if (index < PROC_STATIC_COUNT) {
        strncpy(dirent.name, g_proc_static_entries[index].name, sizeof(dirent.name) - 1);
        dirent.inode = index + 1;
        if (strcmp(g_proc_static_entries[index].name, "self") == 0)
            dirent.type = VFS_TYPE_SYMLINK;
        else if (g_proc_static_entries[index].type == PROCFS_SYS_DIR)
            dirent.type = VFS_TYPE_DIRECTORY;
        else
            dirent.type = VFS_TYPE_FILE;
        return &dirent;
    }

    uint32_t pid_idx = index - PROC_STATIC_COUNT;
    proc_info_t procs[64];
    size_t count = process_get_list(procs, 64);

    if (pid_idx < count) {
        ksnprintf(dirent.name, sizeof(dirent.name), "%d", procs[pid_idx].pid);
        dirent.inode = 1000 + procs[pid_idx].pid;
        dirent.type = VFS_TYPE_DIRECTORY;
        return &dirent;
    }

    return NULL;
}

static struct vfs_node *procfs_root_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    if (!name)
        return NULL;

    if (strcmp(name, "self") == 0) {
        process_t *curr = sched_get_current_process();
        pid_t pid = curr ? curr->pid : 1;
        return procfs_create_pid_dir(pid);
    }

    if (strcmp(name, "sys") == 0) {
        return procfs_create_node("sys", PROCFS_SYS_DIR, 0, 0, 0, &g_procfs_sysdir_ops, VFS_TYPE_DIRECTORY,
                                  0555);
    }

    for (size_t i = 0; i < PROC_STATIC_COUNT; i++) {
        if (!procfs_static_is_special(g_proc_static_entries[i].name) &&
            strcmp(name, g_proc_static_entries[i].name) == 0) {
            return procfs_create_file_node(name, g_proc_static_entries[i].type, 0);
        }
    }

    /* Check if name is numeric (PID directory) */
    bool is_num = true;
    for (size_t i = 0; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9') {
            is_num = false;
            break;
        }
    }

    if (is_num && name[0] != '\0') {
        int pid = 0;
        for (size_t i = 0; name[i]; i++) {
            pid = pid * 10 + (name[i] - '0');
        }

        process_t *proc = process_get_by_pid(pid);
        if (proc) {
            return procfs_create_pid_dir(pid);
        }
    }

    return NULL;
}

/* =========================================================================
 * ProcFS Initialization
 * ========================================================================= */

vfs_node_t *procfs_init(void) {
    memset(&g_procfs_root_ops, 0, sizeof(vfs_ops_t));
    g_procfs_root_ops.readdir = procfs_root_readdir;
    g_procfs_root_ops.finddir = procfs_root_finddir;

    memset(&g_procfs_pid_dir_ops, 0, sizeof(vfs_ops_t));
    g_procfs_pid_dir_ops.readdir = procfs_pid_dir_readdir;
    g_procfs_pid_dir_ops.finddir = procfs_pid_dir_finddir;

    memset(&g_procfs_file_ops, 0, sizeof(vfs_ops_t));
    g_procfs_file_ops.read = procfs_file_read;

    memset(&g_procfs_taskdir_ops, 0, sizeof(vfs_ops_t));
    g_procfs_taskdir_ops.readdir = procfs_taskdir_readdir;
    g_procfs_taskdir_ops.finddir = procfs_taskdir_finddir;

    memset(&g_procfs_tiddir_ops, 0, sizeof(vfs_ops_t));
    g_procfs_tiddir_ops.readdir = procfs_tiddir_readdir;
    g_procfs_tiddir_ops.finddir = procfs_tiddir_finddir;

    memset(&g_procfs_fddir_ops, 0, sizeof(vfs_ops_t));
    g_procfs_fddir_ops.readdir = procfs_fddir_readdir;
    g_procfs_fddir_ops.finddir = procfs_fddir_finddir;

    memset(&g_procfs_fdlink_ops, 0, sizeof(vfs_ops_t));
    g_procfs_fdlink_ops.readlink = procfs_fdlink_readlink;

    memset(&g_procfs_sysdir_ops, 0, sizeof(vfs_ops_t));
    g_procfs_sysdir_ops.readdir = procfs_sysdir_readdir;
    g_procfs_sysdir_ops.finddir = procfs_sysdir_finddir;

    g_procfs_root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(g_procfs_root->name, "proc");
    g_procfs_root->flags = VFS_TYPE_DIRECTORY;
    g_procfs_root->permissions = 0555; /* r-xr-xr-x */
    g_procfs_root->uid = 0;
    g_procfs_root->gid = 0;
    g_procfs_root->ops = &g_procfs_root_ops;

    klog_info("ProcFS initialized with root node '/proc'");
    return g_procfs_root;
}
