/*
 * SzpontOS - Native libdrm Modesetting Implementation (xf86drmMode.c)
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

int drmModeAddFB2(int fd, uint32_t width, uint32_t height,
                  uint32_t pixel_format, const uint32_t bo_handles[4],
                  const uint32_t pitches[4], const uint32_t offsets[4],
                  uint32_t *buf_id, uint32_t flags) {
    if (!buf_id)
        return -EINVAL;

    struct drm_mode_fb_cmd2 cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.width = width;
    cmd.height = height;
    cmd.pixel_format = pixel_format;
    cmd.flags = flags;

    if (bo_handles) memcpy(cmd.handles, bo_handles, sizeof(cmd.handles));
    if (pitches) memcpy(cmd.pitches, pitches, sizeof(cmd.pitches));
    if (offsets) memcpy(cmd.offsets, offsets, sizeof(cmd.offsets));

    int ret = ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &cmd);
    if (ret == 0) {
        *buf_id = cmd.fb_id;
    }
    return ret;
}

int drmModeRmFB(int fd, uint32_t bufferId) {
    return ioctl(fd, DRM_IOCTL_MODE_RMFB, &bufferId);
}

drmModeFBPtr drmModeGetFB(int fd, uint32_t buf) {
    struct drm_mode_fb_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.fb_id = buf;

    if (ioctl(fd, DRM_IOCTL_MODE_GETFB, &cmd) != 0) {
        return NULL;
    }

    drmModeFBPtr fb = (drmModeFBPtr)malloc(sizeof(drmModeFB));
    if (!fb) return NULL;

    fb->fb_id = cmd.fb_id;
    fb->width = cmd.width;
    fb->height = cmd.height;
    fb->pitch = cmd.pitch;
    fb->bpp = cmd.bpp;
    fb->depth = cmd.depth;
    fb->handle = cmd.handle;
    return fb;
}

void drmModeFreeFB(drmModeFBPtr ptr) {
    if (ptr) free(ptr);
}

drmModePropertyPtr drmModeGetProperty(int fd, uint32_t propertyId) {
    struct drm_mode_get_property prop;
    memset(&prop, 0, sizeof(prop));
    prop.prop_id = propertyId;

    if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) != 0) {
        return NULL;
    }

    drmModePropertyPtr p = (drmModePropertyPtr)malloc(sizeof(drmModePropertyRes));
    if (!p) return NULL;
    memset(p, 0, sizeof(drmModePropertyRes));

    p->prop_id = prop.prop_id;
    p->flags = prop.flags;
    strncpy(p->name, prop.name, DRM_PROP_NAME_LEN);
    p->name[DRM_PROP_NAME_LEN - 1] = '\0';
    p->count_values = (int)prop.count_values;
    p->count_enums = (int)prop.count_enum_blobs;

    return p;
}

void drmModeFreeProperty(drmModePropertyPtr ptr) {
    if (!ptr) return;
    if (ptr->values) free(ptr->values);
    if (ptr->enums) free(ptr->enums);
    if (ptr->blob_ids) free(ptr->blob_ids);
    free(ptr);
}

drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id) {
    struct drm_mode_get_blob blob;
    memset(&blob, 0, sizeof(blob));
    blob.blob_id = blob_id;

    if (ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &blob) != 0) {
        return NULL;
    }

    drmModePropertyBlobPtr b = (drmModePropertyBlobPtr)malloc(sizeof(drmModePropertyBlobRes));
    if (!b) return NULL;
    memset(b, 0, sizeof(drmModePropertyBlobRes));

    b->id = blob.blob_id;
    b->length = blob.length;
    if (b->length > 0) {
        b->data = malloc(b->length);
        blob.data = (uint64_t)(uintptr_t)b->data;
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &blob) != 0) {
            free(b->data);
            free(b);
            return NULL;
        }
    }
    return b;
}

void drmModeFreePropertyBlob(drmModePropertyBlobPtr ptr) {
    if (!ptr) return;
    if (ptr->data) free(ptr->data);
    free(ptr);
}

int drmModeCreatePropertyBlob(int fd, const void *data, size_t size, uint32_t *id) {
    (void)fd;
    (void)data;
    (void)size;
    if (id) *id = 0;
    return -ENOSYS;
}

int drmModeDestroyPropertyBlob(int fd, uint32_t id) {
    (void)fd;
    (void)id;
    return -ENOSYS;
}

drmModePlaneResPtr drmModeGetPlaneResources(int fd) {
    (void)fd;
    drmModePlaneResPtr res = (drmModePlaneResPtr)calloc(1, sizeof(drmModePlaneRes));
    return res;
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
    return (drmModeObjectPropertiesPtr)calloc(1, sizeof(drmModeObjectProperties));
}

void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr) {
    if (ptr) {
        if (ptr->props) free(ptr->props);
        if (ptr->prop_values) free(ptr->prop_values);
        free(ptr);
    }
}

int drmModeObjectSetProperty(int fd, uint32_t object_id, uint32_t object_type,
                             uint32_t property_id, uint64_t value) {
    (void)fd;
    (void)object_id;
    (void)object_type;
    (void)property_id;
    (void)value;
    return 0;
}

int drmModeSetCursor(int fd, uint32_t crtcId, uint32_t bo_handle, uint32_t width, uint32_t height) {
    (void)fd;
    (void)crtcId;
    (void)bo_handle;
    (void)width;
    (void)height;
    return 0;
}

int drmModeSetCursor2(int fd, uint32_t crtcId, uint32_t bo_handle, uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y) {
    (void)fd;
    (void)crtcId;
    (void)bo_handle;
    (void)width;
    (void)height;
    (void)hot_x;
    (void)hot_y;
    return 0;
}

int drmModeMoveCursor(int fd, uint32_t crtcId, int x, int y) {
    (void)fd;
    (void)crtcId;
    (void)x;
    (void)y;
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

int drmModeCreateDumb(int fd, uint32_t width, uint32_t height, uint32_t bpp,
                      uint32_t flags, uint32_t *handle, uint32_t *pitch,
                      uint64_t *size) {
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

int drmModeDirtyFB(int fd, uint32_t bufferId, drmModeClipPtr clips, uint32_t num_clips) {
    struct drm_mode_fb_dirty_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.fb_id = bufferId;
    cmd.num_clips = num_clips;
    cmd.clips_ptr = (uint64_t)(uintptr_t)clips;
    return ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &cmd);
}

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id,
                    uint32_t flags, void *user_data) {
    struct drm_mode_crtc_page_flip flip;
    memset(&flip, 0, sizeof(flip));
    flip.crtc_id = crtc_id;
    flip.fb_id = fb_id;
    flip.flags = flags;
    flip.user_data = (uint64_t)(uintptr_t)user_data;
    return ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);
}

drmModeAtomicReqPtr drmModeAtomicAlloc(void) {
    return (drmModeAtomicReqPtr)calloc(1, 64);
}

void drmModeAtomicFree(drmModeAtomicReqPtr req) {
    if (req) free(req);
}

int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id,
                             uint32_t property_id, uint64_t value) {
    (void)req;
    (void)object_id;
    (void)property_id;
    (void)value;
    return 0;
}

int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data) {
    (void)fd;
    (void)req;
    (void)flags;
    (void)user_data;
    return 0;
}

int drmModeCreateLease(int fd, const uint32_t *objects, int num_objects, int flags, uint32_t *lessee_id) {
    (void)fd;
    (void)objects;
    (void)num_objects;
    (void)flags;
    if (lessee_id) *lessee_id = 1;
    return -ENOSYS;
}

int drmModeRevokeLease(int fd, uint32_t lessee_id) {
    (void)fd;
    (void)lessee_id;
    return -ENOSYS;
}
