#ifndef _KERNEL_DRM_DRM_H
#define _KERNEL_DRM_DRM_H

#include <kernel/types.h>

#define DRM_COMMAND_BASE 0x40
#define DRM_COMMAND_END  0xA0
#define DRM_IOCTL_BASE   'd'

#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U
#define _IOC(dir, type, nr, size) (((dir) << 30) | ((type) << 8) | ((nr) << 0) | ((size) << 16))
#define DRM_IO(nr)       _IOC(_IOC_NONE, DRM_IOCTL_BASE, nr, 0)
#define DRM_IOR(nr,type) _IOC(_IOC_READ, DRM_IOCTL_BASE, nr, sizeof(type))
#define DRM_IOW(nr,type) _IOC(_IOC_WRITE, DRM_IOCTL_BASE, nr, sizeof(type))
#define DRM_IOWR(nr,type) _IOC(_IOC_READ | _IOC_WRITE, DRM_IOCTL_BASE, nr, sizeof(type))

struct drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

struct drm_unique {
    size_t unique_len;
    char *unique;
};

struct drm_auth {
    uint32_t magic;
};

#define DRM_CAP_DUMB_BUFFER             0x1
#define DRM_CAP_VBLANK_HIGH_CRTC        0x2
#define DRM_CAP_DUMB_PREFERRED_DEPTH    0x3
#define DRM_CAP_DUMB_PREFER_SHADOW      0x4
#define DRM_CAP_PRIME                   0x5
#define DRM_CAP_TIMESTAMP_MONOTONIC     0x6
#define DRM_CAP_ASYNC_PAGE_FLIP         0x7
#define DRM_CAP_CURSOR_WIDTH            0x8
#define DRM_CAP_CURSOR_HEIGHT           0x9
#define DRM_CAP_ADDFB2_MODIFIERS        0x10
#define DRM_CAP_PAGE_FLIP_TARGET        0x11
#define DRM_CAP_CRTC_IN_VBLANK_EVENT    0x12
#define DRM_CAP_SYNCOBJ                 0x13
#define DRM_CAP_SYNCOBJ_TIMELINE        0x14

#define DRM_PRIME_CAP_IMPORT            0x1
#define DRM_PRIME_CAP_EXPORT            0x2

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_set_client_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_gem_close {
    uint32_t handle;
    uint32_t pad;
};

struct drm_prime_handle {
    uint32_t handle;
    uint32_t flags;
    int32_t fd;
};

#define DRM_SYNCOBJ_CREATE_SIGNALED                    (1 << 0)
#define DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE (1 << 0)
#define DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE (1 << 0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL                (1 << 0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT         (1 << 1)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE          (1 << 2)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE           (1 << 3)

struct drm_syncobj_create {
    uint32_t handle;
    uint32_t flags;
};

struct drm_syncobj_destroy {
    uint32_t handle;
    uint32_t pad;
};

struct drm_syncobj_handle {
    uint32_t handle;
    uint32_t flags;
    int32_t fd;
    uint32_t pad;
};

struct drm_syncobj_wait {
    uint64_t handles;
    int64_t timeout_nsec;
    uint32_t count_handles;
    uint32_t flags;
    uint32_t first_signaled;
    uint32_t pad;
    uint64_t deadline_nsec;
};

#define DRM_EVENT_VBLANK        0x01
#define DRM_EVENT_FLIP_COMPLETE 0x02

struct drm_event {
    uint32_t type;
    uint32_t length;
};

struct drm_event_vblank {
    struct drm_event base;
    uint64_t user_data;
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint32_t sequence;
    uint32_t crtc_id;
};

#define DRM_IOCTL_VERSION               DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE            DRM_IOWR(0x01, struct drm_unique)
#define DRM_IOCTL_GET_MAGIC             DRM_IOWR(0x02, struct drm_auth)
#define DRM_IOCTL_GEM_CLOSE             DRM_IOW(0x09, struct drm_gem_close)
#define DRM_IOCTL_GET_CAP               DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_SET_CLIENT_CAP        DRM_IOW(0x0d, struct drm_set_client_cap)
#define DRM_IOCTL_SET_MASTER            DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER           DRM_IO(0x1f)
#define DRM_IOCTL_AUTH_MAGIC            DRM_IOW(0x11, struct drm_auth)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD    DRM_IOWR(0x2d, struct drm_prime_handle)
#define DRM_IOCTL_PRIME_FD_TO_HANDLE    DRM_IOWR(0x2e, struct drm_prime_handle)
#define DRM_IOCTL_SYNCOBJ_CREATE        DRM_IOWR(0xbf, struct drm_syncobj_create)
#define DRM_IOCTL_SYNCOBJ_DESTROY       DRM_IOWR(0xc0, struct drm_syncobj_destroy)
#define DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD  DRM_IOWR(0xc1, struct drm_syncobj_handle)
#define DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE  DRM_IOWR(0xc2, struct drm_syncobj_handle)
#define DRM_IOCTL_SYNCOBJ_WAIT          DRM_IOWR(0xc3, struct drm_syncobj_wait)

#endif /* _KERNEL_DRM_DRM_H */
