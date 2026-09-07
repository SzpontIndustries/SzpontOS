/*
 * SzpontOS - DRM/KMS Kernel Subsystem & Dumb Buffer Allocator
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef SZPONTOS_DRIVERS_DRM_H
#define SZPONTOS_DRIVERS_DRM_H

#include <kernel/types.h>
#include <kernel/spinlock.h>
#include <fs/vfs.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define DRM_MAX_DUMB_BUFFERS 32
#define DRM_MAX_FBS          32
#define DRM_MAX_CONNECTORS   4
#define DRM_MAX_CRTC         2
#define DRM_MAX_ENCODERS     2

typedef struct drm_dumb_bo {
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    size_t size;
    size_t num_pages;
    uintptr_t *phys_pages;
    void *kernel_virt;
    uint64_t mmap_offset;
    int refcount;
    bool allocated;
    bool is_direct_vram;
} drm_dumb_bo_t;

typedef struct drm_fb {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t pixel_format;
    uint32_t bo_handle;
    bool allocated;
} drm_fb_t;

typedef struct drm_crtc_state {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    bool mode_valid;
    struct drm_mode_modeinfo mode;
} drm_crtc_state_t;

typedef struct drm_connector_state {
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connection;
    uint32_t encoder_id;
    uint32_t mm_width;
    uint32_t mm_height;
} drm_connector_state_t;

typedef struct drm_encoder_state {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
} drm_encoder_state_t;

#define DRM_MAX_EVENTS 32
typedef struct drm_event_queue {
    struct drm_event_vblank events[DRM_MAX_EVENTS];
    size_t head;
    size_t tail;
    size_t count;
} drm_event_queue_t;

#define DRM_MAX_SYNCOBJS 64
typedef struct drm_syncobj {
    uint32_t handle;
    bool allocated;
    bool signaled;
} drm_syncobj_t;

void drm_init(void);
int drm_ioctl(uint64_t request, void *argp);
int drm_render_ioctl(uint64_t request, void *argp);
int drm_mmap(void *addr, size_t length, int prot, int flags, off_t offset, void **out_vaddr);
ssize_t drm_read(void *buffer, size_t size);
bool drm_has_events(void);
bool drm_is_dmabuf_node(vfs_node_t *node);

#endif /* SZPONTOS_DRIVERS_DRM_H */
