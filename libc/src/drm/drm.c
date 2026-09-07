/*
 * SzpontOS - Native DRM/KMS Libc Implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

int drmSetMaster(int fd) {
    return ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
}

int drmDropMaster(int fd) {
    return ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
}

drmModeResPtr drmModeGetResources(int fd) {
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));

    /* 1. Query counts */
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        return NULL;
    }

    drmModeResPtr r = (drmModeResPtr)malloc(sizeof(drmModeRes));
    if (!r)
        return NULL;
    memset(r, 0, sizeof(drmModeRes));

    r->count_fbs = (int)res.count_fbs;
    r->count_crtcs = (int)res.count_crtcs;
    r->count_connectors = (int)res.count_connectors;
    r->count_encoders = (int)res.count_encoders;
    r->min_width = res.min_width;
    r->max_width = res.max_width;
    r->min_height = res.min_height;
    r->max_height = res.max_height;

    if (r->count_fbs > 0) {
        r->fbs = (uint32_t *)malloc(sizeof(uint32_t) * r->count_fbs);
        res.fb_id_ptr = (uint64_t)(uintptr_t)r->fbs;
    }
    if (r->count_crtcs > 0) {
        r->crtcs = (uint32_t *)malloc(sizeof(uint32_t) * r->count_crtcs);
        res.crtc_id_ptr = (uint64_t)(uintptr_t)r->crtcs;
    }
    if (r->count_connectors > 0) {
        r->connectors = (uint32_t *)malloc(sizeof(uint32_t) * r->count_connectors);
        res.connector_id_ptr = (uint64_t)(uintptr_t)r->connectors;
    }
    if (r->count_encoders > 0) {
        r->encoders = (uint32_t *)malloc(sizeof(uint32_t) * r->count_encoders);
        res.encoder_id_ptr = (uint64_t)(uintptr_t)r->encoders;
    }

    /* 2. Retrieve actual IDs */
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        drmModeFreeResources(r);
        return NULL;
    }

    return r;
}

void drmModeFreeResources(drmModeResPtr ptr) {
    if (!ptr)
        return;
    if (ptr->fbs) free(ptr->fbs);
    if (ptr->crtcs) free(ptr->crtcs);
    if (ptr->connectors) free(ptr->connectors);
    if (ptr->encoders) free(ptr->encoders);
    free(ptr);
}

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId) {
    struct drm_mode_get_connector conn;
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = connectorId;

    if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
        return NULL;
    }

    drmModeConnectorPtr c = (drmModeConnectorPtr)malloc(sizeof(drmModeConnector));
    if (!c)
        return NULL;
    memset(c, 0, sizeof(drmModeConnector));

    c->connector_id = conn.connector_id;
    c->encoder_id = conn.encoder_id;
    c->connector_type = conn.connector_type;
    c->connector_type_id = conn.connector_type_id;
    c->connection = conn.connection;
    c->mmWidth = conn.mm_width;
    c->mmHeight = conn.mm_height;
    c->subpixel = conn.subpixel;
    c->count_modes = (int)conn.count_modes;
    c->count_encoders = (int)conn.count_encoders;

    if (c->count_modes > 0) {
        c->modes = (drmModeModeInfoPtr)malloc(sizeof(drmModeModeInfo) * c->count_modes);
        conn.modes_ptr = (uint64_t)(uintptr_t)c->modes;
    }
    if (c->count_encoders > 0) {
        c->encoders = (uint32_t *)malloc(sizeof(uint32_t) * c->count_encoders);
        conn.encoders_ptr = (uint64_t)(uintptr_t)c->encoders;
    }

    if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
        drmModeFreeConnector(c);
        return NULL;
    }

    return c;
}

void drmModeFreeConnector(drmModeConnectorPtr ptr) {
    if (!ptr)
        return;
    if (ptr->modes) free(ptr->modes);
    if (ptr->encoders) free(ptr->encoders);
    if (ptr->props) free(ptr->props);
    if (ptr->prop_values) free(ptr->prop_values);
    free(ptr);
}

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id) {
    struct drm_mode_get_encoder enc;
    memset(&enc, 0, sizeof(enc));
    enc.encoder_id = encoder_id;

    if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0) {
        return NULL;
    }

    drmModeEncoderPtr e = (drmModeEncoderPtr)malloc(sizeof(drmModeEncoder));
    if (!e)
        return NULL;

    e->encoder_id = enc.encoder_id;
    e->encoder_type = enc.encoder_type;
    e->crtc_id = enc.crtc_id;
    e->possible_crtcs = enc.possible_crtcs;
    e->possible_clones = enc.possible_clones;
    return e;
}

void drmModeFreeEncoder(drmModeEncoderPtr ptr) {
    if (ptr) free(ptr);
}

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtcId) {
    struct drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = crtcId;

    if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) != 0) {
        return NULL;
    }

    drmModeCrtcPtr c = (drmModeCrtcPtr)malloc(sizeof(drmModeCrtc));
    if (!c)
        return NULL;

    c->crtc_id = crtc.crtc_id;
    c->buffer_id = crtc.fb_id;
    c->x = crtc.x;
    c->y = crtc.y;
    c->mode_valid = crtc.mode_valid;
    c->gamma_size = (int)crtc.gamma_size;
    memcpy(&c->mode, &crtc.mode, sizeof(drmModeModeInfo));
    c->width = crtc.mode.hdisplay;
    c->height = crtc.mode.vdisplay;

    return c;
}

void drmModeFreeCrtc(drmModeCrtcPtr ptr) {
    if (ptr) free(ptr);
}

int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode) {
    struct drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof(crtc));

    crtc.crtc_id = crtcId;
    crtc.fb_id = bufferId;
    crtc.x = x;
    crtc.y = y;
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)connectors;
    crtc.count_connectors = (uint32_t)count;

    if (mode) {
        memcpy(&crtc.mode, mode, sizeof(struct drm_mode_modeinfo));
        crtc.mode_valid = 1;
    }

    return ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
}

int drmModeAddFB(int fd, uint32_t width, uint32_t height, uint8_t depth,
                 uint8_t bpp, uint32_t pitch, uint32_t bo_handle,
                 uint32_t *buf_id) {
    if (!buf_id)
        return -EINVAL;

    struct drm_mode_fb_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.width = width;
    cmd.height = height;
    cmd.pitch = pitch;
    cmd.bpp = bpp;
    cmd.depth = depth;
    cmd.handle = bo_handle;

    int ret = ioctl(fd, DRM_IOCTL_MODE_ADDFB, &cmd);
    if (ret == 0) {
        *buf_id = cmd.fb_id;
    }
    return ret;
}

int drmModeRmFB(int fd, uint32_t bufferId) {
    uint32_t fb_id = bufferId;
    return ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb_id);
}

int drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format,
                  const uint32_t bo_handles[4], const uint32_t pitches[4],
                  const uint32_t offsets[4], uint32_t *buf_id, uint32_t flags) {
    if (!buf_id)
        return -EINVAL;

    struct drm_mode_fb_cmd2 req;
    memset(&req, 0, sizeof(req));
    req.width = width;
    req.height = height;
    req.pixel_format = pixel_format;
    req.flags = flags;
    if (bo_handles) memcpy(req.handles, bo_handles, sizeof(req.handles));
    if (pitches) memcpy(req.pitches, pitches, sizeof(req.pitches));
    if (offsets) memcpy(req.offsets, offsets, sizeof(req.offsets));

    int ret = ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &req);
    if (ret == 0) {
        *buf_id = req.fb_id;
    }
    return ret;
}

int drmModeDirtyFB(int fd, uint32_t bufferId, drmModeClipPtr clips, uint32_t num_clips) {
    struct drm_mode_fb_dirty_cmd dirty;
    memset(&dirty, 0, sizeof(dirty));
    dirty.fb_id = bufferId;
    dirty.num_clips = num_clips;
    dirty.clips_ptr = (uint64_t)(uintptr_t)clips;

    return ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}

int drmModeCreateDumb(int fd, uint32_t width, uint32_t height, uint32_t bpp,
                      uint32_t flags, uint32_t *handle, uint32_t *pitch, uint64_t *size) {
    if (!handle || !pitch || !size)
        return -EINVAL;

    struct drm_mode_create_dumb req;
    memset(&req, 0, sizeof(req));
    req.width = width;
    req.height = height;
    req.bpp = bpp;
    req.flags = flags;

    int ret = ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &req);
    if (ret == 0) {
        *handle = req.handle;
        *pitch = req.pitch;
        *size = req.size;
    }
    return ret;
}

int drmModeMapDumb(int fd, uint32_t handle, uint64_t *offset) {
    if (!offset)
        return -EINVAL;

    struct drm_mode_map_dumb req;
    memset(&req, 0, sizeof(req));
    req.handle = handle;

    int ret = ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &req);
    if (ret == 0) {
        *offset = req.offset;
    }
    return ret;
}

int drmModeDestroyDumb(int fd, uint32_t handle) {
    struct drm_mode_destroy_dumb req;
    memset(&req, 0, sizeof(req));
    req.handle = handle;
    return ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &req);
}

drmModeFBPtr drmModeGetFB(int fd, uint32_t fb_id) {
    struct drm_mode_fb_cmd fb;
    memset(&fb, 0, sizeof(fb));
    fb.fb_id = fb_id;
    if (ioctl(fd, DRM_IOCTL_MODE_GETFB, &fb) != 0)
        return NULL;

    drmModeFBPtr r = (drmModeFBPtr)malloc(sizeof(drmModeFB));
    if (!r)
        return NULL;
    r->fb_id = fb.fb_id;
    r->width = fb.width;
    r->height = fb.height;
    r->pitch = fb.pitch;
    r->bpp = fb.bpp;
    r->depth = fb.depth;
    r->handle = fb.handle;
    return r;
}

void drmModeFreeFB(drmModeFBPtr ptr) {
    if (ptr) free(ptr);
}

drmModePropertyPtr drmModeGetProperty(int fd, uint32_t property_id) {
    struct drm_mode_get_property prop;
    memset(&prop, 0, sizeof(prop));
    prop.prop_id = property_id;
    if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) != 0)
        return NULL;

    drmModePropertyPtr r = (drmModePropertyPtr)malloc(sizeof(drmModePropertyRes));
    if (!r)
        return NULL;
    memset(r, 0, sizeof(drmModePropertyRes));
    r->prop_id = prop.prop_id;
    r->flags = prop.flags;
    strncpy(r->name, prop.name, sizeof(r->name) - 1);
    return r;
}

void drmModeFreeProperty(drmModePropertyPtr ptr) {
    if (ptr) free(ptr);
}

drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id) {
    struct drm_mode_get_blob blob;
    memset(&blob, 0, sizeof(blob));
    blob.blob_id = blob_id;
    if (ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &blob) != 0)
        return NULL;

    drmModePropertyBlobPtr r = (drmModePropertyBlobPtr)malloc(sizeof(drmModePropertyBlobRes));
    if (!r)
        return NULL;
    r->id = blob.blob_id;
    r->length = blob.length;
    r->data = NULL;
    if (blob.length > 0) {
        r->data = malloc(blob.length);
        blob.data = (uint64_t)(uintptr_t)r->data;
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &blob) != 0) {
            free(r->data);
            free(r);
            return NULL;
        }
    }
    return r;
}

void drmModeFreePropertyBlob(drmModePropertyBlobPtr ptr) {
    if (ptr) {
        if (ptr->data) free(ptr->data);
        free(ptr);
    }
}

int drmModeCreatePropertyBlob(int fd, const void *data, size_t size, uint32_t *id) {
    (void)fd; (void)data; (void)size;
    if (id) *id = 1;
    return 0;
}

int drmModeDestroyPropertyBlob(int fd, uint32_t id) {
    (void)fd; (void)id;
    return 0;
}

drmModePlaneResPtr drmModeGetPlaneResources(int fd) {
    drmModePlaneResPtr r = (drmModePlaneResPtr)malloc(sizeof(drmModePlaneRes));
    if (!r) return NULL;
    r->count_planes = 0;
    r->planes = NULL;
    return r;
}

void drmModeFreePlaneResources(drmModePlaneResPtr ptr) {
    if (ptr) {
        if (ptr->planes) free(ptr->planes);
        free(ptr);
    }
}

drmModePlanePtr drmModeGetPlane(int fd, uint32_t plane_id) {
    (void)fd;
    (void)plane_id;
    return NULL;
}

void drmModeFreePlane(drmModePlanePtr ptr) {
    if (ptr) free(ptr);
}

drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type) {
    (void)fd;
    (void)object_id;
    (void)object_type;
    drmModeObjectPropertiesPtr p = (drmModeObjectPropertiesPtr)malloc(sizeof(drmModeObjectProperties));
    if (!p) return NULL;
    p->count_props = 0;
    p->props = NULL;
    p->prop_values = NULL;
    return p;
}

void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr) {
    if (ptr) {
        if (ptr->props) free(ptr->props);
        if (ptr->prop_values) free(ptr->prop_values);
        free(ptr);
    }
}

drmModeLesseeListPtr drmModeListLessees(int fd) {
    (void)fd;
    drmModeLesseeListPtr l = (drmModeLesseeListPtr)malloc(sizeof(drmModeLesseeList));
    if (!l) return NULL;
    l->count = 0;
    l->lessees = NULL;
    return l;
}

int drmModeCreateLease(int fd, const uint32_t *objects, int num_objects, int flags, uint32_t *lessee_id) {
    (void)fd;
    (void)objects;
    (void)num_objects;
    (void)flags;
    if (lessee_id) *lessee_id = 0;
    return -ENOSYS;
}

int drmModeRevokeLease(int fd, uint32_t lessee_id) {
    (void)fd;
    (void)lessee_id;
    return -ENOSYS;
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

drmVersionPtr drmGetVersion(int fd) {
    struct drm_version ver;
    memset(&ver, 0, sizeof(ver));
    char name[64] = "szpontos-drm";
    char date[64] = "20260830";
    char desc[64] = "SzpontOS Kernel DRM/KMS Driver";
    ver.name = name;
    ver.name_len = sizeof(name);
    ver.date = date;
    ver.date_len = sizeof(date);
    ver.desc = desc;
    ver.desc_len = sizeof(desc);
    ioctl(fd, DRM_IOCTL_VERSION, &ver);

    drmVersionPtr v = (drmVersionPtr)malloc(sizeof(drmVersion));
    if (!v) return NULL;
    v->version_major = ver.version_major ? ver.version_major : 1;
    v->version_minor = ver.version_minor ? ver.version_minor : 0;
    v->version_patchlevel = ver.version_patchlevel;
    v->name = strdup(name);
    v->date = strdup(date);
    v->desc = strdup(desc);
    return v;
}

void drmFreeVersion(drmVersionPtr v) {
    if (v) {
        if (v->name) free(v->name);
        if (v->date) free(v->date);
        if (v->desc) free(v->desc);
        free(v);
    }
}

int drmSetInterfaceVersion(int fd, drmSetVersionPtr version) {
    (void)fd;
    (void)version;
    return 0;
}

char *drmGetBusid(int fd) {
    (void)fd;
    return strdup("pci:0000:00:01.0");
}

void drmFreeBusid(char *busid) {
    if (busid) free(busid);
}

int drmSetClientCap(int fd, uint64_t capability, uint64_t value) {
    struct drm_set_client_cap cap;
    cap.capability = capability;
    cap.value = value;
    return ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);
}

int drmModeSetCursor(int fd, uint32_t crtcId, uint32_t bo_handle, uint32_t width, uint32_t height) {
    (void)fd;
    (void)crtcId;
    (void)bo_handle;
    (void)width;
    (void)height;
    return 0;
}

int drmModeCrtcSetGamma(int fd, uint32_t crtcId, uint32_t size, uint16_t *red, uint16_t *green, uint16_t *blue) {
    (void)fd;
    (void)crtcId;
    (void)size;
    (void)red;
    (void)green;
    (void)blue;
    return 0;
}

int drmModeConnectorSetProperty(int fd, uint32_t connector_id, uint32_t property_id, uint64_t value) {
    (void)fd;
    (void)connector_id;
    (void)property_id;
    (void)value;
    return 0;
}

int drmModeSetCursor2(int fd, uint32_t crtcId, uint32_t bo_handle, uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y) {
    (void)fd; (void)crtcId; (void)bo_handle; (void)width; (void)height; (void)hot_x; (void)hot_y;
    return 0;
}

int drmModeMoveCursor(int fd, uint32_t crtcId, int x, int y) {
    (void)fd; (void)crtcId; (void)x; (void)y;
    return 0;
}

int drmModeObjectSetProperty(int fd, uint32_t object_id, uint32_t object_type, uint32_t property_id, uint64_t value) {
    (void)fd; (void)object_id; (void)object_type; (void)property_id; (void)value;
    return 0;
}

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void *user_data) {
    struct drm_mode_crtc_page_flip flip;
    memset(&flip, 0, sizeof(flip));
    flip.crtc_id = crtc_id;
    flip.fb_id = fb_id;
    flip.flags = flags;
    flip.user_data = (uint64_t)(uintptr_t)user_data;
    return ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);
}

struct _drmModeAtomicReq {
    int count;
};

drmModeAtomicReqPtr drmModeAtomicAlloc(void) {
    drmModeAtomicReqPtr req = (drmModeAtomicReqPtr)malloc(sizeof(struct _drmModeAtomicReq));
    if (req) req->count = 0;
    return req;
}

void drmModeAtomicFree(drmModeAtomicReqPtr req) {
    if (req) free(req);
}

int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value) {
    (void)req; (void)object_id; (void)property_id; (void)value;
    return 0;
}

int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data) {
    (void)fd; (void)req; (void)flags; (void)user_data;
    return -EINVAL;
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
    if (!handle) return -EINVAL;
    struct drm_prime_handle req;
    req.handle = 0;
    req.flags = 0;
    req.fd = prime_fd;
    int ret = ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &req);
    if (ret == 0) {
        *handle = req.handle;
    }
    return ret;
}

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    if (!prime_fd) return -EINVAL;
    struct drm_prime_handle req;
    req.handle = handle;
    req.flags = flags;
    req.fd = -1;
    int ret = ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req);
    if (ret == 0) {
        *prime_fd = req.fd;
    }
    return ret;
}

int drmWaitVBlank(int fd, drmVBlankPtr vbl) {
    (void)fd;
    if (vbl) {
        vbl->reply.sequence = 1;
        vbl->reply.tval_sec = 0;
        vbl->reply.tval_usec = 0;
    }
    return 0;
}

int drmCrtcGetSequence(int fd, uint32_t crtcId, uint64_t *sequence, uint64_t *ns) {
    (void)fd; (void)crtcId;
    if (sequence) *sequence = 1;
    if (ns) *ns = 0;
    return 0;
}

int drmCrtcQueueSequence(int fd, uint32_t crtcId, uint32_t flags, uint64_t sequence, uint64_t *sequence_queued, uint64_t user_data) {
    (void)fd; (void)crtcId; (void)flags; (void)sequence; (void)user_data;
    if (sequence_queued) *sequence_queued = sequence;
    return 0;
}

void osPciInit(void) {
    /* SzpontOS initializes PCI buses during kernel boot */
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

