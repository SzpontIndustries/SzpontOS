/*
 * SzpontOS - Native DRM/KMS Userland API Header (xf86drm.h)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _XF86DRM_H
#define _XF86DRM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <drm/drm.h>

#ifndef DRM_CLOEXEC
#define DRM_CLOEXEC O_CLOEXEC
#endif
#ifndef DRM_RDWR
#define DRM_RDWR O_RDWR
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _drmEventContext {
    int version;
    void (*vblank_handler)(int fd, unsigned int sequence, unsigned int tv_sec,
                           unsigned int tv_usec, void *user_data);
    void (*page_flip_handler)(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void *user_data);
    void (*page_flip_handler2)(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, unsigned int crtc_id, void *user_data);
    void (*sequence_handler)(int fd, uint64_t sequence, uint64_t ns, uint64_t user_data);
} drmEventContext, *drmEventContextPtr;

typedef struct _drmVersion {
    int version_major;
    int version_minor;
    int version_patchlevel;
    char *name;
    char *date;
    char *desc;
} drmVersion, *drmVersionPtr;

enum {
    DRM_NODE_PRIMARY = 0,
    DRM_NODE_CONTROL = 1,
    DRM_NODE_RENDER  = 2,
    DRM_NODE_MAX     = 3,
};

#define DRM_BUS_PCI      0
#define DRM_BUS_USB      1
#define DRM_BUS_PLATFORM 2

typedef struct _drmPciBusInfo {
    uint16_t domain;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
} drmPciBusInfo, *drmPciBusInfoPtr;

typedef struct _drmPciDeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subvendor_id;
    uint16_t subdevice_id;
    uint8_t revision;
} drmPciDeviceInfo, *drmPciDeviceInfoPtr;

typedef struct _drmDevice {
    char **nodes;
    int available_nodes;
    int bustype;
    union {
        drmPciBusInfo pci;
    } businfo;
    union {
        drmPciDeviceInfo pci;
    } deviceinfo;
} drmDevice, *drmDevicePtr;

int drmOpen(const char *name, const char *busid);
int drmClose(int fd);
int drmIoctl(int fd, unsigned long request, void *arg);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmSetMaster(int fd);
int drmDropMaster(int fd);
int drmHandleEvent(int fd, drmEventContextPtr evctx);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr v);
int drmSetInterfaceVersion(int fd, drmSetVersionPtr version);
char *drmGetBusid(int fd);
void drmFreeBusid(char *busid);
int drmSetClientCap(int fd, uint64_t capability, uint64_t value);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmWaitVBlank(int fd, drmVBlankPtr vbl);
int drmCrtcGetSequence(int fd, uint32_t crtcId, uint64_t *sequence, uint64_t *ns);
int drmCrtcQueueSequence(int fd, uint32_t crtcId, uint32_t flags, uint64_t sequence, uint64_t *sequence_queued, uint64_t user_data);

int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device);
int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices);
void drmFreeDevice(drmDevicePtr *device);
void drmFreeDevices(drmDevicePtr devices[], int count);
int drmGetNodeTypeFromFd(int fd);
char *drmGetRenderDeviceNameFromFd(int fd);

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd);
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle);
int drmSyncobjWait(int fd, uint32_t *handles, uint32_t num_handles, int64_t timeout_nsec, uint32_t flags, uint32_t *first_signaled);

#ifdef __cplusplus
}
#endif

#endif /* _XF86DRM_H */
