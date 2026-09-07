/*
 * SzpontOS - Native DRM/KMS Modesetting API Header (xf86drmMode.h)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _XF86DRMMODE_H
#define _XF86DRMMODE_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _drmModeRes {
    int count_fbs;
    uint32_t *fbs;
    int count_crtcs;
    uint32_t *crtcs;
    int count_connectors;
    uint32_t *connectors;
    int count_encoders;
    uint32_t *encoders;
    uint32_t min_width, max_width;
    uint32_t min_height, max_height;
} drmModeRes, *drmModeResPtr;

typedef struct _drmModeModeInfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
} drmModeModeInfo, *drmModeModeInfoPtr;

typedef struct _drmModeConnector {
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mmWidth, mmHeight;
    uint32_t subpixel;
    int count_modes;
    drmModeModeInfoPtr modes;
    int count_props;
    uint32_t *props;
    uint64_t *prop_values;
    int count_encoders;
    uint32_t *encoders;
} drmModeConnector, *drmModeConnectorPtr;

typedef struct _drmModeEncoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
} drmModeEncoder, *drmModeEncoderPtr;

typedef struct _drmModeCrtc {
    uint32_t crtc_id;
    uint32_t buffer_id;
    uint32_t x, y;
    uint32_t width, height;
    int mode_valid;
    drmModeModeInfo mode;
    int gamma_size;
} drmModeCrtc, *drmModeCrtcPtr;

typedef struct _drmModeClip {
    unsigned short x1;
    unsigned short y1;
    unsigned short x2;
    unsigned short y2;
} drmModeClip, *drmModeClipPtr;

typedef struct _drmModeFB {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
} drmModeFB, *drmModeFBPtr;

typedef struct _drmModePropertyBlob {
    uint32_t id;
    uint32_t length;
    void *data;
} drmModePropertyBlobRes, *drmModePropertyBlobPtr;

#ifndef DRM_PROP_NAME_LEN
#define DRM_PROP_NAME_LEN 32
#endif

typedef struct _drmModeProperty {
    uint32_t prop_id;
    uint32_t flags;
    char name[DRM_PROP_NAME_LEN];
    int count_values;
    uint64_t *values;
    int count_enums;
    struct drm_mode_property_enum *enums;
    int count_blobs;
    uint32_t *blob_ids;
} drmModePropertyRes, *drmModePropertyPtr;

typedef struct _drmModePlane {
    uint32_t count_formats;
    uint32_t *formats;
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t crtc_x, crtc_y;
    uint32_t x, y;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
} drmModePlane, *drmModePlanePtr;

typedef struct _drmModePlaneRes {
    uint32_t count_planes;
    uint32_t *planes;
} drmModePlaneRes, *drmModePlaneResPtr;

drmModeResPtr drmModeGetResources(int fd);
void drmModeFreeResources(drmModeResPtr ptr);

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId);
void drmModeFreeConnector(drmModeConnectorPtr ptr);

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id);
void drmModeFreeEncoder(drmModeEncoderPtr ptr);

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtcId);
void drmModeFreeCrtc(drmModeCrtcPtr ptr);

drmModeFBPtr drmModeGetFB(int fd, uint32_t fb_id);
void drmModeFreeFB(drmModeFBPtr ptr);

drmModePropertyPtr drmModeGetProperty(int fd, uint32_t property_id);
void drmModeFreeProperty(drmModePropertyPtr ptr);

drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id);
void drmModeFreePropertyBlob(drmModePropertyBlobPtr ptr);
int drmModeCreatePropertyBlob(int fd, const void *data, size_t size, uint32_t *id);
int drmModeDestroyPropertyBlob(int fd, uint32_t id);

drmModePlaneResPtr drmModeGetPlaneResources(int fd);
void drmModeFreePlaneResources(drmModePlaneResPtr ptr);

typedef struct _drmModeObjectProperties {
    uint32_t count_props;
    uint32_t *props;
    uint64_t *prop_values;
} drmModeObjectProperties, *drmModeObjectPropertiesPtr;

typedef struct _drmModeLesseeList {
    uint32_t count;
    uint32_t *lessees;
} drmModeLesseeList, *drmModeLesseeListPtr;

drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type);
void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr);
drmModeLesseeListPtr drmModeListLessees(int fd);
int drmModeCreateLease(int fd, const uint32_t *objects, int num_objects, int flags, uint32_t *lessee_id);
int drmModeRevokeLease(int fd, uint32_t lessee_id);

drmModePlanePtr drmModeGetPlane(int fd, uint32_t plane_id);
void drmModeFreePlane(drmModePlanePtr ptr);

int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode);

int drmModeAddFB(int fd, uint32_t width, uint32_t height, uint8_t depth,
                 uint8_t bpp, uint32_t pitch, uint32_t bo_handle,
                 uint32_t *buf_id);

int drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format,
                  const uint32_t bo_handles[4], const uint32_t pitches[4],
                  const uint32_t offsets[4], uint32_t *buf_id, uint32_t flags);

int drmModeRmFB(int fd, uint32_t bufferId);

int drmModeDirtyFB(int fd, uint32_t bufferId, drmModeClipPtr clips, uint32_t num_clips);

/* Dumb Buffer Helpers */
int drmModeCreateDumb(int fd, uint32_t width, uint32_t height, uint32_t bpp,
                      uint32_t flags, uint32_t *handle, uint32_t *pitch, uint64_t *size);

int drmModeMapDumb(int fd, uint32_t handle, uint64_t *offset);

int drmModeDestroyDumb(int fd, uint32_t handle);

static inline int drm_property_type_is(drmModePropertyPtr prop, uint32_t type) {
    if (!prop) return 0;
    if (prop->flags & DRM_MODE_PROP_EXTENDED_TYPE)
        return (prop->flags & DRM_MODE_PROP_EXTENDED_TYPE) == type;
    return (prop->flags & type) == type;
}

int drmModeSetCursor(int fd, uint32_t crtcId, uint32_t bo_handle, uint32_t width, uint32_t height);
int drmModeSetCursor2(int fd, uint32_t crtcId, uint32_t bo_handle, uint32_t width, uint32_t height, int32_t hot_x, int32_t hot_y);
int drmModeMoveCursor(int fd, uint32_t crtcId, int x, int y);
int drmModeCrtcSetGamma(int fd, uint32_t crtcId, uint32_t size, uint16_t *red, uint16_t *green, uint16_t *blue);
int drmModeConnectorSetProperty(int fd, uint32_t connector_id, uint32_t property_id, uint64_t value);
int drmModeObjectSetProperty(int fd, uint32_t object_id, uint32_t object_type, uint32_t property_id, uint64_t value);
int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id, uint32_t flags, void *user_data);

typedef struct _drmModeAtomicReq drmModeAtomicReq, *drmModeAtomicReqPtr;
drmModeAtomicReqPtr drmModeAtomicAlloc(void);
void drmModeAtomicFree(drmModeAtomicReqPtr req);
int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value);
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* _XF86DRMMODE_H */
