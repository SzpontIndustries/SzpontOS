/*
 * SzpontOS - Native libdrm Core Implementation (xf86drm.c)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

int drmIoctl(int fd, unsigned long request, void *arg) {
    return ioctl(fd, request, arg);
}

int drmOpen(const char *name, const char *busid) {
    (void)name;
    (void)busid;

    /* Try /dev/dri/card0 first, fallback to /dev/card0 */
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        fd = open("/dev/card0", O_RDWR);
    }
    return fd;
}

int drmClose(int fd) {
    return close(fd);
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    if (!value)
        return -EINVAL;

    struct drm_get_cap cap;
    memset(&cap, 0, sizeof(cap));
    cap.capability = capability;

    int ret = ioctl(fd, DRM_IOCTL_GET_CAP, &cap);
    if (ret == 0) {
        *value = cap.value;
    }
    return ret;
}

int drmSetClientCap(int fd, uint64_t capability, uint64_t value) {
    struct drm_set_client_cap cap;
    memset(&cap, 0, sizeof(cap));
    cap.capability = capability;
    cap.value = value;
    return ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);
}

int drmSetMaster(int fd) {
    return ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
}

int drmDropMaster(int fd) {
    return ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
}

drmVersionPtr drmGetVersion(int fd) {
    struct drm_version ver;
    memset(&ver, 0, sizeof(ver));

    /* First pass: retrieve name/date/desc lengths */
    if (ioctl(fd, DRM_IOCTL_VERSION, &ver) != 0) {
        return NULL;
    }

    drmVersionPtr v = (drmVersionPtr)malloc(sizeof(drmVersion));
    if (!v)
        return NULL;
    memset(v, 0, sizeof(drmVersion));

    v->version_major = ver.version_major;
    v->version_minor = ver.version_minor;
    v->version_patchlevel = ver.version_patchlevel;

    if (ver.name_len > 0) {
        v->name = (char *)malloc(ver.name_len + 1);
        ver.name = v->name;
    }
    if (ver.date_len > 0) {
        v->date = (char *)malloc(ver.date_len + 1);
        ver.date = v->date;
    }
    if (ver.desc_len > 0) {
        v->desc = (char *)malloc(ver.desc_len + 1);
        ver.desc = v->desc;
    }

    /* Second pass: fetch string data */
    if (ioctl(fd, DRM_IOCTL_VERSION, &ver) != 0) {
        drmFreeVersion(v);
        return NULL;
    }

    if (v->name) v->name[ver.name_len] = '\0';
    if (v->date) v->date[ver.date_len] = '\0';
    if (v->desc) v->desc[ver.desc_len] = '\0';

    return v;
}

void drmFreeVersion(drmVersionPtr v) {
    if (!v)
        return;
    if (v->name) free(v->name);
    if (v->date) free(v->date);
    if (v->desc) free(v->desc);
    free(v);
}

int drmSetInterfaceVersion(int fd, drmSetVersionPtr version) {
    if (!version)
        return -EINVAL;
    return ioctl(fd, DRM_IOCTL_SET_VERSION, version);
}

char *drmGetBusid(int fd) {
    struct drm_unique u;
    memset(&u, 0, sizeof(u));

    if (ioctl(fd, DRM_IOCTL_GET_UNIQUE, &u) != 0) {
        return NULL;
    }

    char *busid = (char *)malloc(u.unique_len + 1);
    if (!busid)
        return NULL;

    u.unique = busid;
    if (ioctl(fd, DRM_IOCTL_GET_UNIQUE, &u) != 0) {
        free(busid);
        return NULL;
    }

    busid[u.unique_len] = '\0';
    return busid;
}

void drmFreeBusid(char *busid) {
    if (busid)
        free(busid);
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
    if (!handle)
        return -EINVAL;

    struct drm_prime_handle req;
    memset(&req, 0, sizeof(req));
    req.fd = prime_fd;

    int ret = ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &req);
    if (ret == 0) {
        *handle = req.handle;
    }
    return ret;
}

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    if (!prime_fd)
        return -EINVAL;

    struct drm_prime_handle req;
    memset(&req, 0, sizeof(req));
    req.handle = handle;
    req.flags = flags;

    int ret = ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req);
    if (ret == 0) {
        *prime_fd = req.fd;
    }
    return ret;
}

int drmWaitVBlank(int fd, drmVBlankPtr vbl) {
    if (!vbl)
        return -EINVAL;
    return ioctl(fd, DRM_IOCTL_WAIT_VBLANK, vbl);
}

int drmCrtcGetSequence(int fd, uint32_t crtcId, uint64_t *sequence, uint64_t *ns) {
    (void)crtcId;
    drmVBlank vbl;
    memset(&vbl, 0, sizeof(vbl));
    vbl.request.type = DRM_VBLANK_RELATIVE;
    vbl.request.sequence = 0;

    int ret = ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
    if (ret == 0) {
        if (sequence) *sequence = vbl.reply.sequence;
        if (ns) *ns = (uint64_t)vbl.reply.tval_sec * 1000000000ULL + (uint64_t)vbl.reply.tval_usec * 1000ULL;
    }
    return ret;
}

int drmCrtcQueueSequence(int fd, uint32_t crtcId, uint32_t flags, uint64_t sequence,
                         uint64_t *sequence_queued, uint64_t user_data) {
    (void)flags;
    (void)sequence_queued;
    struct drm_mode_crtc_page_flip flip;
    memset(&flip, 0, sizeof(flip));
    flip.crtc_id = crtcId;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    flip.user_data = user_data;
    return ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);
}

int drmHandleEvent(int fd, drmEventContextPtr evctx) {
    if (!evctx)
        return -EINVAL;

    char buf[sizeof(struct drm_event_vblank) + 32];
    ssize_t len = read(fd, buf, sizeof(buf));
    if (len <= 0)
        return (len < 0) ? -errno : 0;

    struct drm_event *base = (struct drm_event *)buf;
    if (base->type == DRM_EVENT_VBLANK && evctx->version >= 1 && evctx->vblank_handler) {
        struct drm_event_vblank *ev = (struct drm_event_vblank *)buf;
        evctx->vblank_handler(fd, ev->sequence, ev->tv_sec, ev->tv_usec, (void *)(uintptr_t)ev->user_data);
    } else if (base->type == DRM_EVENT_FLIP_COMPLETE && evctx->version >= 2 && evctx->page_flip_handler) {
        struct drm_event_vblank *ev = (struct drm_event_vblank *)buf;
        evctx->page_flip_handler(fd, ev->sequence, ev->tv_sec, ev->tv_usec, (void *)(uintptr_t)ev->user_data);
    }
    return 0;
}

int drmGetNodeTypeFromFd(int fd) {
    (void)fd;
    return DRM_NODE_PRIMARY;
}

char *drmGetRenderDeviceNameFromFd(int fd) {
    (void)fd;
    return strdup("/dev/dri/renderD128");
}

int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device) {
    (void)fd;
    (void)flags;
    if (!device) return -EINVAL;

    drmDevicePtr dev = (drmDevicePtr)calloc(1, sizeof(drmDevice));
    if (!dev) return -ENOMEM;

    dev->nodes = (char **)calloc(DRM_NODE_MAX, sizeof(char *));
    if (!dev->nodes) {
        free(dev);
        return -ENOMEM;
    }

    dev->nodes[DRM_NODE_PRIMARY] = strdup("/dev/dri/card0");
    dev->nodes[DRM_NODE_RENDER] = strdup("/dev/dri/renderD128");
    dev->available_nodes = (1 << DRM_NODE_PRIMARY) | (1 << DRM_NODE_RENDER);
    dev->bustype = DRM_BUS_PCI;

    dev->businfo.pci.domain = 0;
    dev->businfo.pci.bus = 0;
    dev->businfo.pci.dev = 1;
    dev->businfo.pci.func = 0;

    dev->deviceinfo.pci.vendor_id = 0x1234;
    dev->deviceinfo.pci.device_id = 0x1111;

    *device = dev;
    return 0;
}

int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices) {
    if (!devices || max_devices <= 0)
        return 0;

    drmDevicePtr dev = NULL;
    if (drmGetDevice2(-1, flags, &dev) == 0) {
        devices[0] = dev;
        return 1;
    }
    return 0;
}

void drmFreeDevice(drmDevicePtr *device) {
    if (!device || !*device) return;
    drmDevicePtr dev = *device;
    if (dev->nodes) {
        for (int i = 0; i < DRM_NODE_MAX; i++) {
            if (dev->nodes[i]) free(dev->nodes[i]);
        }
        free(dev->nodes);
    }
    free(dev);
    *device = NULL;
}

void drmFreeDevices(drmDevicePtr devices[], int count) {
    if (!devices) return;
    for (int i = 0; i < count; i++) {
        drmDevicePtr dev = devices[i];
        if (dev) {
            drmFreeDevice(&dev);
            devices[i] = NULL;
        }
    }
}
