/*
 * SzpontOS - Native libdrm Syncobj Implementation (syncobj.c)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <xf86drm.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) {
    if (!handle) return -EINVAL;
    struct drm_syncobj_create req;
    memset(&req, 0, sizeof(req));
    req.flags = flags;
    int ret = ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &req);
    if (ret == 0) {
        *handle = req.handle;
    }
    return ret;
}

int drmSyncobjDestroy(int fd, uint32_t handle) {
    struct drm_syncobj_destroy req;
    memset(&req, 0, sizeof(req));
    req.handle = handle;
    return ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &req);
}

int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd) {
    if (!obj_fd) return -EINVAL;
    struct drm_syncobj_handle req;
    memset(&req, 0, sizeof(req));
    req.handle = handle;
    int ret = ioctl(fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &req);
    if (ret == 0) {
        *obj_fd = req.fd;
    }
    return ret;
}

int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle) {
    if (!handle) return -EINVAL;
    struct drm_syncobj_handle req;
    memset(&req, 0, sizeof(req));
    req.fd = obj_fd;
    int ret = ioctl(fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &req);
    if (ret == 0) {
        *handle = req.handle;
    }
    return ret;
}

int drmSyncobjWait(int fd, uint32_t *handles, uint32_t num_handles, int64_t timeout_nsec, uint32_t flags, uint32_t *first_signaled) {
    struct drm_syncobj_wait req;
    memset(&req, 0, sizeof(req));
    req.handles = (uint64_t)(uintptr_t)handles;
    req.count_handles = num_handles;
    req.timeout_nsec = timeout_nsec;
    req.flags = flags;
    int ret = ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &req);
    if (ret == 0 && first_signaled) {
        *first_signaled = req.first_signaled;
    }
    return ret;
}
