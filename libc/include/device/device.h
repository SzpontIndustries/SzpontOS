#ifndef _DEVICE_DEVICE_H
#define _DEVICE_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mach.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

typedef mach_port_t device_t;
typedef mach_port_t memory_object_t;
typedef int dev_mode_t;
#define D_READ 1
#define D_WRITE 2

static inline int get_privileged_ports(mach_port_t *host, mach_port_t *device) {
    if (host) *host = 0;
    if (device) *device = 0;
    return 0;
}

static inline int device_open(mach_port_t master, dev_mode_t mode, const char *name, mach_port_t *dev) {
    (void)master;
    int flags = (mode & D_WRITE) ? O_RDWR : O_RDONLY;
    *dev = open(name ? name : "/dev/mem", flags);
    return (*dev < 0) ? errno : 0;
}

static inline int file_name_lookup(const char *name, int flags, int mode) {
    (void)flags;
    (void)mode;
    return open(name, O_RDWR);
}

static inline int device_map(int dev, int prot, off_t offset, size_t size, mach_port_t *pager, int unk) {
    (void)dev; (void)prot; (void)offset; (void)size; (void)pager; (void)unk;
    return 0;
}

static inline int device_close(int dev) {
    return close(dev);
}

static inline int vm_map(mach_port_t task, vm_address_t *dest, size_t size, vm_address_t mask, int anywhere, mach_port_t pager, off_t offset, int copy, int prot, int max_prot, int inherit) {
    (void)task; (void)mask; (void)anywhere; (void)pager; (void)copy; (void)max_prot; (void)inherit;
    void *ptr = mmap(NULL, size, prot, MAP_SHARED | MAP_ANONYMOUS, -1, offset);
    if (ptr == MAP_FAILED) return errno;
    *dest = (vm_address_t)ptr;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _DEVICE_DEVICE_H */
