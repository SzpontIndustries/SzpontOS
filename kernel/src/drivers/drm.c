/*
 * SzpontOS - DRM/KMS Kernel Driver Implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/drm.h>
#include <drivers/framebuffer.h>
#include <drivers/rtc.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/usercopy.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>

static spinlock_t g_drm_lock = SPINLOCK_INIT;
static drm_dumb_bo_t g_dumb_buffers[DRM_MAX_DUMB_BUFFERS];
static drm_fb_t g_framebuffers[DRM_MAX_FBS];

static drm_syncobj_t g_syncobjs[DRM_MAX_SYNCOBJS];
static uint32_t g_next_syncobj_handle = 1;

static drm_event_queue_t g_drm_events;
static uint32_t g_vblank_sequence = 1;

static vfs_ops_t g_dmabuf_ops;
static vfs_ops_t g_syncfile_ops;

static drm_crtc_state_t g_crtc;
static drm_connector_state_t g_connector;
static drm_encoder_state_t g_encoder;
static struct drm_mode_modeinfo g_active_mode;

static pid_t g_drm_master_pid = 0;
static bool g_drm_initialized = false;

static uint32_t g_next_bo_handle = 1;
static uint32_t g_next_fb_id = 100;

static void drm_setup_default_mode(void) {
    size_t width = fb_get_width();
    size_t height = fb_get_height();

    if (width == 0 || height == 0) {
        width = 1280;
        height = 960;
    }

    memset(&g_active_mode, 0, sizeof(struct drm_mode_modeinfo));
    g_active_mode.clock = 60000;
    g_active_mode.hdisplay = (uint16_t)width;
    g_active_mode.hsync_start = (uint16_t)(width + 16);
    g_active_mode.hsync_end = (uint16_t)(width + 32);
    g_active_mode.htotal = (uint16_t)(width + 48);
    g_active_mode.vdisplay = (uint16_t)height;
    g_active_mode.vsync_start = (uint16_t)(height + 1);
    g_active_mode.vsync_end = (uint16_t)(height + 3);
    g_active_mode.vtotal = (uint16_t)(height + 5);
    g_active_mode.vrefresh = 60;
    g_active_mode.type = DRM_MODE_TYPE_PREFERRED;
    ksnprintf(g_active_mode.name, sizeof(g_active_mode.name), "%lux%lu", width, height);

    g_connector.connector_id = 1;
    g_connector.connector_type = DRM_MODE_CONNECTOR_VIRTUAL;
    g_connector.connection = DRM_MODE_CONNECTED;
    g_connector.encoder_id = 2;
    g_connector.mm_width = (uint32_t)(width * 254 / 960);
    g_connector.mm_height = (uint32_t)(height * 254 / 960);

    g_encoder.encoder_id = 2;
    g_encoder.encoder_type = 1; /* DRM_MODE_ENCODER_NONE / DAC */
    g_encoder.crtc_id = 3;
    g_encoder.possible_crtcs = 1;

    g_crtc.crtc_id = 3;
    g_crtc.fb_id = 0;
    g_crtc.x = 0;
    g_crtc.y = 0;
    g_crtc.mode_valid = true;
    memcpy(&g_crtc.mode, &g_active_mode, sizeof(struct drm_mode_modeinfo));
}

void drm_init(void) {
    spinlock_acquire(&g_drm_lock);
    memset(g_dumb_buffers, 0, sizeof(g_dumb_buffers));
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    memset(g_syncobjs, 0, sizeof(g_syncobjs));
    memset(&g_drm_events, 0, sizeof(g_drm_events));

    drm_setup_default_mode();
    g_drm_initialized = true;
    spinlock_release(&g_drm_lock);

    klog_info("DRM/KMS: Initialized (Mode: %s, Connector ID: %u, CRTC ID: %u)",
              g_active_mode.name, g_connector.connector_id, g_crtc.crtc_id);
}

static drm_dumb_bo_t *drm_find_bo(uint32_t handle) {
    if (handle == 0)
        return NULL;
    for (size_t i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        if (g_dumb_buffers[i].allocated && g_dumb_buffers[i].handle == handle) {
            return &g_dumb_buffers[i];
        }
    }
    return NULL;
}

static drm_fb_t *drm_find_fb(uint32_t fb_id) {
    if (fb_id == 0)
        return NULL;
    for (size_t i = 0; i < DRM_MAX_FBS; i++) {
        if (g_framebuffers[i].allocated && g_framebuffers[i].fb_id == fb_id) {
            return &g_framebuffers[i];
        }
    }
    return NULL;
}

static int drm_ioctl_version(struct drm_version *ver) {
    if (!ver)
        return -22; /* EINVAL */

    ver->version_major = 1;
    ver->version_minor = 0;
    ver->version_patchlevel = 0;

    const char *name = "szpont-drm";
    const char *date = "20260830";
    const char *desc = "SzpontOS Kernel Mode Setting & DRM Driver";

    if (ver->name && ver->name_len > 0) {
        size_t n = strlen(name);
        if (n >= ver->name_len)
            n = ver->name_len - 1;
        memcpy(ver->name, name, n);
        ver->name[n] = '\0';
    }
    ver->name_len = strlen(name);

    if (ver->date && ver->date_len > 0) {
        size_t n = strlen(date);
        if (n >= ver->date_len)
            n = ver->date_len - 1;
        memcpy(ver->date, date, n);
        ver->date[n] = '\0';
    }
    ver->date_len = strlen(date);

    if (ver->desc && ver->desc_len > 0) {
        size_t n = strlen(desc);
        if (n >= ver->desc_len)
            n = ver->desc_len - 1;
        memcpy(ver->desc, desc, n);
        ver->desc[n] = '\0';
    }
    ver->desc_len = strlen(desc);

    return 0;
}

static int drm_ioctl_get_cap(struct drm_get_cap *cap) {
    if (!cap)
        return -22;

    switch (cap->capability) {
    case DRM_CAP_DUMB_BUFFER:
        cap->value = 1;
        return 0;
    case DRM_CAP_VBLANK_HIGH_CRTC:
        cap->value = 1;
        return 0;
    case DRM_CAP_DUMB_PREFERRED_DEPTH:
        cap->value = 32;
        return 0;
    case DRM_CAP_DUMB_PREFER_SHADOW:
        cap->value = 1;
        return 0;
    case DRM_CAP_PRIME:
        cap->value = DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT;
        return 0;
    case DRM_CAP_TIMESTAMP_MONOTONIC:
        cap->value = 1;
        return 0;
    case DRM_CAP_ASYNC_PAGE_FLIP:
        cap->value = 1;
        return 0;
    case DRM_CAP_CURSOR_WIDTH:
        cap->value = 64;
        return 0;
    case DRM_CAP_CURSOR_HEIGHT:
        cap->value = 64;
        return 0;
    case DRM_CAP_ADDFB2_MODIFIERS:
        cap->value = 1;
        return 0;
    case DRM_CAP_PAGE_FLIP_TARGET:
        cap->value = 1;
        return 0;
    case DRM_CAP_CRTC_IN_VBLANK_EVENT:
        cap->value = 1;
        return 0;
    case DRM_CAP_SYNCOBJ:
        cap->value = 1;
        return 0;
    case DRM_CAP_SYNCOBJ_TIMELINE:
        cap->value = 0;
        return 0;
    default:
        cap->value = 0;
        return -22;
    }
}

static int drm_ioctl_get_resources(struct drm_mode_card_res *res) {
    if (!res)
        return -22;

    res->min_width = 320;
    res->max_width = 3840;
    res->min_height = 200;
    res->max_height = 2160;

    res->count_connectors = 1;
    res->count_encoders = 1;
    res->count_crtcs = 1;

    /* Count allocated framebuffers */
    uint32_t count_fbs = 0;
    for (size_t i = 0; i < DRM_MAX_FBS; i++) {
        if (g_framebuffers[i].allocated)
            count_fbs++;
    }
    res->count_fbs = count_fbs;

    if (res->connector_id_ptr) {
        uint32_t *conn_ptr = (uint32_t *)(uintptr_t)res->connector_id_ptr;
        conn_ptr[0] = g_connector.connector_id;
    }
    if (res->encoder_id_ptr) {
        uint32_t *enc_ptr = (uint32_t *)(uintptr_t)res->encoder_id_ptr;
        enc_ptr[0] = g_encoder.encoder_id;
    }
    if (res->crtc_id_ptr) {
        uint32_t *crtc_ptr = (uint32_t *)(uintptr_t)res->crtc_id_ptr;
        crtc_ptr[0] = g_crtc.crtc_id;
    }
    if (res->fb_id_ptr && count_fbs > 0) {
        uint32_t *fb_ptr = (uint32_t *)(uintptr_t)res->fb_id_ptr;
        size_t idx = 0;
        for (size_t i = 0; i < DRM_MAX_FBS; i++) {
            if (g_framebuffers[i].allocated) {
                fb_ptr[idx++] = g_framebuffers[i].fb_id;
            }
        }
    }

    return 0;
}

static int drm_ioctl_get_connector(struct drm_mode_get_connector *conn) {
    if (!conn)
        return -22;

    conn->connector_id = g_connector.connector_id;
    conn->connector_type = g_connector.connector_type;
    conn->connector_type_id = 1;
    conn->connection = g_connector.connection;
    conn->mm_width = g_connector.mm_width;
    conn->mm_height = g_connector.mm_height;
    conn->subpixel = 0;

    conn->count_encoders = 1;
    conn->count_modes = 1;
    conn->count_props = 0;
    conn->encoder_id = g_connector.encoder_id;

    if (conn->encoders_ptr) {
        uint32_t *enc_ptr = (uint32_t *)(uintptr_t)conn->encoders_ptr;
        enc_ptr[0] = g_encoder.encoder_id;
    }

    if (conn->modes_ptr && conn->count_modes > 0) {
        struct drm_mode_modeinfo *modes = (struct drm_mode_modeinfo *)(uintptr_t)conn->modes_ptr;
        memcpy(&modes[0], &g_active_mode, sizeof(struct drm_mode_modeinfo));
    }

    return 0;
}

static int drm_ioctl_get_encoder(struct drm_mode_get_encoder *enc) {
    if (!enc)
        return -22;

    enc->encoder_id = g_encoder.encoder_id;
    enc->encoder_type = g_encoder.encoder_type;
    enc->crtc_id = g_encoder.crtc_id;
    enc->possible_crtcs = g_encoder.possible_crtcs;
    enc->possible_clones = 0;
    return 0;
}

static int drm_ioctl_get_crtc(struct drm_mode_crtc *crtc) {
    if (!crtc)
        return -22;

    crtc->crtc_id = g_crtc.crtc_id;
    crtc->fb_id = g_crtc.fb_id;
    crtc->x = g_crtc.x;
    crtc->y = g_crtc.y;
    crtc->gamma_size = 0;
    crtc->mode_valid = g_crtc.mode_valid ? 1 : 0;
    memcpy(&crtc->mode, &g_crtc.mode, sizeof(struct drm_mode_modeinfo));
    return 0;
}

static int drm_ioctl_set_crtc(struct drm_mode_crtc *crtc) {
    if (!crtc)
        return -22;

    spinlock_acquire(&g_drm_lock);

    g_crtc.fb_id = crtc->fb_id;
    g_crtc.x = crtc->x;
    g_crtc.y = crtc->y;

    if (crtc->mode_valid) {
        memcpy(&g_crtc.mode, &crtc->mode, sizeof(struct drm_mode_modeinfo));
        memcpy(&g_active_mode, &crtc->mode, sizeof(struct drm_mode_modeinfo));
        g_crtc.mode_valid = true;
    }

    /* Activate graphics mode and flush FB */
    fb_set_graphics_mode(true);

    drm_fb_t *fb = drm_find_fb(g_crtc.fb_id);
    if (fb) {
        drm_dumb_bo_t *bo = drm_find_bo(fb->bo_handle);
        if (bo && bo->kernel_virt) {
            fb_blit_from_buffer((const uint32_t *)bo->kernel_virt, bo->pitch / 4, 0, 0, bo->width, bo->height);
        }
    }

    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_create_dumb(struct drm_mode_create_dumb *req) {
    if (!req || req->width == 0 || req->height == 0)
        return -22;

    if (req->bpp == 0)
        req->bpp = 32;

    uint32_t pitch = ALIGN_UP(req->width * ((req->bpp + 7) / 8), 64);
    size_t size = ALIGN_UP((size_t)pitch * req->height, PAGE_SIZE);
    size_t num_pages = size / PAGE_SIZE;

    spinlock_acquire(&g_drm_lock);

    drm_dumb_bo_t *bo = NULL;
    for (size_t i = 0; i < DRM_MAX_DUMB_BUFFERS; i++) {
        if (!g_dumb_buffers[i].allocated) {
            bo = &g_dumb_buffers[i];
            break;
        }
    }

    if (!bo) {
        spinlock_release(&g_drm_lock);
        return -12; /* ENOMEM */
    }

    struct limine_framebuffer *lfb = fb_get_limine();
    bool is_direct = false;
    uintptr_t phys = 0;
    void *virt = NULL;

    if (lfb && lfb->address && req->width == fb_get_width() && req->height == fb_get_height()) {
        uintptr_t fb_virt = (uintptr_t)lfb->address;
        phys = VIRT_TO_PHYS(fb_virt);
        virt = (void *)fb_virt;
        pitch = (uint32_t)lfb->pitch;
        size = ALIGN_UP((size_t)pitch * req->height, PAGE_SIZE);
        num_pages = size / PAGE_SIZE;
        is_direct = true;
    } else {
        phys = pmm_alloc_pages(num_pages);
        if (!phys) {
            spinlock_release(&g_drm_lock);
            return -12; /* ENOMEM */
        }
        virt = (void *)PHYS_TO_VIRT(phys);
        memset(virt, 0, size);
    }

    bo->handle = g_next_bo_handle++;
    bo->width = req->width;
    bo->height = req->height;
    bo->bpp = req->bpp;
    bo->pitch = pitch;
    bo->size = size;
    bo->num_pages = num_pages;
    bo->phys_pages = (uintptr_t *)kmalloc(sizeof(uintptr_t) * num_pages);
    for (size_t i = 0; i < num_pages; i++) {
        bo->phys_pages[i] = phys + i * PAGE_SIZE;
    }
    bo->kernel_virt = virt;
    bo->mmap_offset = ((uint64_t)bo->handle) << 12;
    bo->refcount = 1;
    bo->allocated = true;
    bo->is_direct_vram = is_direct;

    req->handle = bo->handle;
    req->pitch = pitch;
    req->size = size;

    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_map_dumb(struct drm_mode_map_dumb *req) {
    if (!req || req->handle == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(req->handle);
    if (!bo) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    req->offset = bo->mmap_offset;
    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_bo_unref_locked(drm_dumb_bo_t *bo) {
    if (!bo || !bo->allocated)
        return -22;

    bo->refcount--;
    if (bo->refcount <= 0) {
        if (bo->phys_pages) {
            if (!bo->is_direct_vram && bo->phys_pages[0]) {
                pmm_free_pages(bo->phys_pages[0], bo->num_pages);
            }
            kfree(bo->phys_pages);
        }
        memset(bo, 0, sizeof(drm_dumb_bo_t));
    }
    return 0;
}

static int drm_ioctl_destroy_dumb(struct drm_mode_destroy_dumb *req) {
    if (!req || req->handle == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(req->handle);
    if (!bo) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    int ret = drm_bo_unref_locked(bo);
    spinlock_release(&g_drm_lock);
    return ret;
}

static int drm_ioctl_gem_close(struct drm_gem_close *req) {
    if (!req || req->handle == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(req->handle);
    if (!bo) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    int ret = drm_bo_unref_locked(bo);
    spinlock_release(&g_drm_lock);
    return ret;
}

static int drm_ioctl_add_fb(struct drm_mode_fb_cmd *cmd) {
    if (!cmd || cmd->handle == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(cmd->handle);
    if (!bo) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    drm_fb_t *fb = NULL;
    for (size_t i = 0; i < DRM_MAX_FBS; i++) {
        if (!g_framebuffers[i].allocated) {
            fb = &g_framebuffers[i];
            break;
        }
    }

    if (!fb) {
        spinlock_release(&g_drm_lock);
        return -12;
    }

    fb->fb_id = g_next_fb_id++;
    fb->width = cmd->width;
    fb->height = cmd->height;
    fb->pitch = cmd->pitch;
    fb->bpp = cmd->bpp;
    fb->depth = cmd->depth;
    fb->pixel_format = 0;
    fb->bo_handle = cmd->handle;
    fb->allocated = true;

    cmd->fb_id = fb->fb_id;
    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_add_fb2(struct drm_mode_fb_cmd2 *cmd) {
    if (!cmd || cmd->handles[0] == 0 || cmd->width == 0 || cmd->height == 0)
        return -22;

    uint32_t handle = cmd->handles[0];
    uint32_t pitch = cmd->pitches[0];
    uint32_t pixel_format = cmd->pixel_format;

    uint32_t bpp = 32;
    uint32_t depth = 24;
    if (pixel_format == DRM_FORMAT_RGB565 || pixel_format == DRM_FORMAT_BGR565) {
        bpp = 16;
        depth = 16;
    } else if (pixel_format == DRM_FORMAT_ARGB8888 || pixel_format == DRM_FORMAT_ABGR8888) {
        bpp = 32;
        depth = 32;
    } else if (pixel_format == DRM_FORMAT_XRGB8888 || pixel_format == DRM_FORMAT_XBGR8888 || pixel_format == 0) {
        bpp = 32;
        depth = 24;
    }

    if (pitch == 0) {
        pitch = ALIGN_UP(cmd->width * ((bpp + 7) / 8), 64);
    }

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(handle);
    if (!bo) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    drm_fb_t *fb = NULL;
    for (size_t i = 0; i < DRM_MAX_FBS; i++) {
        if (!g_framebuffers[i].allocated) {
            fb = &g_framebuffers[i];
            break;
        }
    }

    if (!fb) {
        spinlock_release(&g_drm_lock);
        return -12;
    }

    fb->fb_id = g_next_fb_id++;
    fb->width = cmd->width;
    fb->height = cmd->height;
    fb->pitch = pitch;
    fb->bpp = bpp;
    fb->depth = depth;
    fb->pixel_format = pixel_format;
    fb->bo_handle = handle;
    fb->allocated = true;

    cmd->fb_id = fb->fb_id;
    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_rm_fb(uint32_t *fb_id_ptr) {
    if (!fb_id_ptr || *fb_id_ptr == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_fb_t *fb = drm_find_fb(*fb_id_ptr);
    if (!fb) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    if (g_crtc.fb_id == fb->fb_id) {
        g_crtc.fb_id = 0;
    }
    memset(fb, 0, sizeof(drm_fb_t));

    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_dirty_fb(struct drm_mode_fb_dirty_cmd *dirty) {
    if (!dirty || dirty->fb_id == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_fb_t *fb = drm_find_fb(dirty->fb_id);
    if (!fb) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    drm_dumb_bo_t *bo = drm_find_bo(fb->bo_handle);
    if (!bo || !bo->kernel_virt) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    if (!bo->is_direct_vram) {
        if (dirty->num_clips == 0 || dirty->clips_ptr == 0) {
            /* Blit full buffer to screen */
            fb_blit_from_buffer((const uint32_t *)bo->kernel_virt, bo->pitch / 4, 0, 0, bo->width, bo->height);
        } else {
            /* Blit clipped damage rectangles */
            struct drm_clip_rect *clips = (struct drm_clip_rect *)(uintptr_t)dirty->clips_ptr;
            for (uint32_t i = 0; i < dirty->num_clips; i++) {
                size_t x = clips[i].x1;
                size_t y = clips[i].y1;
                size_t w = (clips[i].x2 > clips[i].x1) ? (clips[i].x2 - clips[i].x1) : 0;
                size_t h = (clips[i].y2 > clips[i].y1) ? (clips[i].y2 - clips[i].y1) : 0;
                if (w > 0 && h > 0) {
                    fb_blit_from_buffer((const uint32_t *)bo->kernel_virt, bo->pitch / 4, x, y, w, h);
                }
            }
        }
    }

    spinlock_release(&g_drm_lock);
    return 0;
}

static void drm_queue_flip_event(uint32_t crtc_id, uint64_t user_data) {
    if (g_drm_events.count >= DRM_MAX_EVENTS) {
        g_drm_events.head = (g_drm_events.head + 1) % DRM_MAX_EVENTS;
        g_drm_events.count--;
    }

    uint64_t now_ns = rtc_get_monotonic_ns();
    uint32_t sec = (uint32_t)(now_ns / 1000000000ULL);
    uint32_t usec = (uint32_t)((now_ns % 1000000000ULL) / 1000);

    struct drm_event_vblank *ev = &g_drm_events.events[g_drm_events.tail];
    ev->base.type = DRM_EVENT_FLIP_COMPLETE;
    ev->base.length = sizeof(struct drm_event_vblank);
    ev->user_data = user_data;
    ev->tv_sec = sec;
    ev->tv_usec = usec;
    ev->sequence = g_vblank_sequence++;
    ev->crtc_id = crtc_id;

    g_drm_events.tail = (g_drm_events.tail + 1) % DRM_MAX_EVENTS;
    g_drm_events.count++;
}

static int drm_ioctl_page_flip(struct drm_mode_crtc_page_flip *flip) {
    if (!flip || flip->crtc_id == 0 || flip->fb_id == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);

    drm_fb_t *fb = drm_find_fb(flip->fb_id);
    if (!fb) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    drm_dumb_bo_t *bo = drm_find_bo(fb->bo_handle);
    if (!bo || !bo->kernel_virt) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    /* Atomically update CRTC framebuffer scanout ID */
    g_crtc.fb_id = flip->fb_id;

    /* If not directly scanning VRAM hardware, blit the back buffer to screen */
    if (!bo->is_direct_vram) {
        fb_blit_from_buffer((const uint32_t *)bo->kernel_virt, bo->pitch / 4, 0, 0, bo->width, bo->height);
    }

    if (flip->flags & DRM_MODE_PAGE_FLIP_EVENT) {
        drm_queue_flip_event(flip->crtc_id, flip->user_data);
    }

    spinlock_release(&g_drm_lock);
    return 0;
}

ssize_t drm_read(void *buffer, size_t size) {
    if (!buffer || size < sizeof(struct drm_event_vblank))
        return -22;

    spinlock_acquire(&g_drm_lock);
    if (g_drm_events.count == 0) {
        spinlock_release(&g_drm_lock);
        return -11; /* EAGAIN */
    }

    struct drm_event_vblank ev = g_drm_events.events[g_drm_events.head];
    g_drm_events.head = (g_drm_events.head + 1) % DRM_MAX_EVENTS;
    g_drm_events.count--;
    spinlock_release(&g_drm_lock);

    if (!copy_to_user((uintptr_t)buffer, &ev, sizeof(struct drm_event_vblank)))
        return -14;

    return (ssize_t)sizeof(struct drm_event_vblank);
}

bool drm_has_events(void) {
    return g_drm_events.count > 0;
}

/* ==============================================================================
 * PRIME Subsystem (dma-buf Buffer Sharing)
 * ============================================================================== */

static int drm_mmap_bo_locked(drm_dumb_bo_t *bo, void *addr, size_t length, int prot, int flags, void **out_vaddr) {
    (void)prot;
    (void)flags;

    process_t *proc = sched_get_current_process();
    if (!proc || !out_vaddr || length == 0 || length > VMM_USER_END - PAGE_SIZE)
        return -22;

    if (proc->mmap_current == 0) {
        proc->mmap_current = 0x0000600000000000ULL;
    }

    size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > bo->num_pages)
        return -22;

    uintptr_t vaddr = (uintptr_t)addr;
    if (vaddr == 0) {
        vaddr = proc->mmap_current;
    }
    if ((vaddr & (PAGE_SIZE - 1)) || !vmm_user_range(vaddr, pages * PAGE_SIZE)) {
        return -22;
    }

    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys = bo->phys_pages[i];
        vmm_release_user_page(proc->pagemap, vaddr + i * PAGE_SIZE);
        if (!vmm_map_page(proc->pagemap, vaddr + i * PAGE_SIZE, phys,
                          VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_BORROWED)) {
            for (size_t j = 0; j < i; j++)
                vmm_release_user_page(proc->pagemap, vaddr + j * PAGE_SIZE);
            return -12;
        }
    }

    if (vaddr + pages * PAGE_SIZE > proc->mmap_current)
        proc->mmap_current = vaddr + pages * PAGE_SIZE;

    *out_vaddr = (void *)vaddr;
    return 0;
}

static int dmabuf_mmap(vfs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset, void **out_vaddr) {
    (void)offset;
    if (!node || !node->device_data)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = (drm_dumb_bo_t *)node->device_data;
    if (!bo || !bo->allocated) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    int ret = drm_mmap_bo_locked(bo, addr, length, prot, flags, out_vaddr);
    spinlock_release(&g_drm_lock);
    return ret;
}

static int dmabuf_close(vfs_node_t *node) {
    if (!node || !node->device_data)
        return 0;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = (drm_dumb_bo_t *)node->device_data;
    drm_bo_unref_locked(bo);
    spinlock_release(&g_drm_lock);

    kfree(node);
    return 0;
}

static vfs_ops_t g_dmabuf_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = dmabuf_close,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL,
    .chmod = NULL,
    .chown = NULL,
    .unlink = NULL,
    .ioctl = NULL,
    .rename = NULL,
    .rmdir = NULL,
    .truncate = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .link = NULL,
    .access = NULL,
    .mmap = dmabuf_mmap,
};

bool drm_is_dmabuf_node(vfs_node_t *node) {
    return node && node->ops == &g_dmabuf_ops;
}

static int drm_ioctl_prime_handle_to_fd(struct drm_prime_handle *req) {
    if (!req || req->handle == 0)
        return -22;

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(req->handle);
    if (!bo || !bo->allocated) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        spinlock_release(&g_drm_lock);
        return -24; /* EMFILE */
    }

    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(node->name, "dmabuf");
    node->flags = VFS_TYPE_CHARDEVICE;
    node->permissions = 0666;
    node->ops = &g_dmabuf_ops;
    node->device_data = bo;
    node->length = bo->size;

    file_descriptor_t *fdesc = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    fdesc->node = node;
    fdesc->flags = O_RDWR | (req->flags & 0x80000);
    fdesc->refcount = 1;

    proc->fds[fd] = fdesc;
    proc->fd_cloexec[fd] = (req->flags & 0x80000) ? true : false;

    bo->refcount++;
    req->fd = fd;

    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_prime_fd_to_handle(struct drm_prime_handle *req) {
    if (!req || req->fd < 0)
        return -22;

    process_t *proc = sched_get_current_process();
    if (!proc || req->fd >= MAX_FD || !proc->fds[req->fd])
        return -9; /* EBADF */

    file_descriptor_t *fdesc = proc->fds[req->fd];
    if (!fdesc->node || fdesc->node->ops != &g_dmabuf_ops || !fdesc->node->device_data)
        return -22; /* EINVAL */

    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = (drm_dumb_bo_t *)fdesc->node->device_data;
    if (!bo || !bo->allocated) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    bo->refcount++;
    req->handle = bo->handle;
    spinlock_release(&g_drm_lock);
    return 0;
}

/* ==============================================================================
 * Syncobj Subsystem
 * ============================================================================== */

static drm_syncobj_t *drm_find_syncobj(uint32_t handle) {
    if (handle == 0)
        return NULL;
    for (size_t i = 0; i < DRM_MAX_SYNCOBJS; i++) {
        if (g_syncobjs[i].allocated && g_syncobjs[i].handle == handle)
            return &g_syncobjs[i];
    }
    return NULL;
}

static int drm_ioctl_syncobj_create(struct drm_syncobj_create *req) {
    if (!req)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_syncobj_t *so = NULL;
    for (size_t i = 0; i < DRM_MAX_SYNCOBJS; i++) {
        if (!g_syncobjs[i].allocated) {
            so = &g_syncobjs[i];
            break;
        }
    }
    if (!so) {
        spinlock_release(&g_drm_lock);
        return -12; /* ENOMEM */
    }

    so->handle = g_next_syncobj_handle++;
    so->allocated = true;
    so->signaled = (req->flags & DRM_SYNCOBJ_CREATE_SIGNALED) ? true : false;

    req->handle = so->handle;
    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_syncobj_destroy(struct drm_syncobj_destroy *req) {
    if (!req || req->handle == 0)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_syncobj_t *so = drm_find_syncobj(req->handle);
    if (!so) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    memset(so, 0, sizeof(drm_syncobj_t));
    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_syncobj_wait(struct drm_syncobj_wait *req) {
    if (!req || req->handles == 0 || req->count_handles == 0 || req->count_handles > 64)
        return -22;

    uint32_t handles[64];
    if (!copy_from_user(handles, (uintptr_t)req->handles, sizeof(uint32_t) * req->count_handles))
        return -14; /* EFAULT */

    bool wait_all = (req->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0;
    uint64_t start_ns = rtc_get_monotonic_ns();
    uint64_t timeout_ns = (req->timeout_nsec > 0) ? (uint64_t)req->timeout_nsec : 0;

    while (1) {
        spinlock_acquire(&g_drm_lock);
        uint32_t ready_count = 0;
        uint32_t first_ready = 0;

        for (uint32_t i = 0; i < req->count_handles; i++) {
            drm_syncobj_t *so = drm_find_syncobj(handles[i]);
            if (!so) {
                spinlock_release(&g_drm_lock);
                return -22;
            }
            if (so->signaled) {
                if (ready_count == 0)
                    first_ready = i;
                ready_count++;
            }
        }

        bool satisfied = wait_all ? (ready_count == req->count_handles) : (ready_count > 0);
        if (satisfied) {
            req->first_signaled = first_ready;
            spinlock_release(&g_drm_lock);
            return 0;
        }

        spinlock_release(&g_drm_lock);

        uint64_t now_ns = rtc_get_monotonic_ns();
        if (timeout_ns == 0 || now_ns - start_ns >= timeout_ns) {
            return -62; /* ETIME */
        }

        thread_sleep(1);
    }
}

static int syncfile_close(vfs_node_t *node) {
    if (node) {
        kfree(node);
    }
    return 0;
}

static vfs_ops_t g_syncfile_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = syncfile_close,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL,
    .chmod = NULL,
    .chown = NULL,
    .unlink = NULL,
    .ioctl = NULL,
    .rename = NULL,
    .rmdir = NULL,
    .truncate = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .link = NULL,
    .access = NULL,
    .mmap = NULL,
};

static int drm_ioctl_syncobj_handle_to_fd(struct drm_syncobj_handle *req) {
    if (!req || req->handle == 0)
        return -22;

    process_t *proc = sched_get_current_process();
    if (!proc)
        return -1;

    spinlock_acquire(&g_drm_lock);
    drm_syncobj_t *so = drm_find_syncobj(req->handle);
    if (!so) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        spinlock_release(&g_drm_lock);
        return -24;
    }

    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(node->name, "sync_file");
    node->flags = VFS_TYPE_CHARDEVICE;
    node->permissions = 0666;
    node->ops = &g_syncfile_ops;
    node->device_data = so;

    file_descriptor_t *fdesc = (file_descriptor_t *)kzalloc(sizeof(file_descriptor_t));
    fdesc->node = node;
    fdesc->flags = O_RDWR | (req->flags & 0x80000);
    fdesc->refcount = 1;

    proc->fds[fd] = fdesc;
    req->fd = fd;

    spinlock_release(&g_drm_lock);
    return 0;
}

static int drm_ioctl_syncobj_fd_to_handle(struct drm_syncobj_handle *req) {
    if (!req || req->fd < 0)
        return -22;

    process_t *proc = sched_get_current_process();
    if (!proc || req->fd >= MAX_FD || !proc->fds[req->fd])
        return -9;

    file_descriptor_t *fdesc = proc->fds[req->fd];
    if (!fdesc->node || fdesc->node->ops != &g_syncfile_ops || !fdesc->node->device_data)
        return -22;

    spinlock_acquire(&g_drm_lock);
    drm_syncobj_t *so = (drm_syncobj_t *)fdesc->node->device_data;
    if (!so || !so->allocated) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    req->handle = so->handle;
    spinlock_release(&g_drm_lock);
    return 0;
}

/* ==============================================================================
 * DRM Ioctl Dispatchers
 * ============================================================================== */

int drm_ioctl(uint64_t request, void *argp) {
    if (!g_drm_initialized) {
        drm_init();
    }

    switch (request) {
    case DRM_IOCTL_VERSION:
        return drm_ioctl_version((struct drm_version *)argp);

    case DRM_IOCTL_GET_CAP:
        return drm_ioctl_get_cap((struct drm_get_cap *)argp);

    case DRM_IOCTL_SET_CLIENT_CAP:
        return 0;

    case DRM_IOCTL_SET_MASTER:
        g_drm_master_pid = sched_get_current_process() ? sched_get_current_process()->pid : 0;
        fb_set_graphics_mode(true);
        return 0;

    case DRM_IOCTL_DROP_MASTER:
        g_drm_master_pid = 0;
        fb_set_graphics_mode(false);
        return 0;

    case DRM_IOCTL_MODE_GETRESOURCES:
        return drm_ioctl_get_resources((struct drm_mode_card_res *)argp);

    case DRM_IOCTL_MODE_GETCONNECTOR:
        return drm_ioctl_get_connector((struct drm_mode_get_connector *)argp);

    case DRM_IOCTL_MODE_GETENCODER:
        return drm_ioctl_get_encoder((struct drm_mode_get_encoder *)argp);

    case DRM_IOCTL_MODE_GETCRTC:
        return drm_ioctl_get_crtc((struct drm_mode_crtc *)argp);

    case DRM_IOCTL_MODE_SETCRTC:
        return drm_ioctl_set_crtc((struct drm_mode_crtc *)argp);

    case DRM_IOCTL_MODE_CREATE_DUMB:
        return drm_ioctl_create_dumb((struct drm_mode_create_dumb *)argp);

    case DRM_IOCTL_MODE_MAP_DUMB:
        return drm_ioctl_map_dumb((struct drm_mode_map_dumb *)argp);

    case DRM_IOCTL_MODE_DESTROY_DUMB:
        return drm_ioctl_destroy_dumb((struct drm_mode_destroy_dumb *)argp);

    case DRM_IOCTL_GEM_CLOSE:
        return drm_ioctl_gem_close((struct drm_gem_close *)argp);

    case DRM_IOCTL_MODE_ADDFB:
        return drm_ioctl_add_fb((struct drm_mode_fb_cmd *)argp);

    case DRM_IOCTL_MODE_ADDFB2:
        return drm_ioctl_add_fb2((struct drm_mode_fb_cmd2 *)argp);

    case DRM_IOCTL_MODE_RMFB:
        return drm_ioctl_rm_fb((uint32_t *)argp);

    case DRM_IOCTL_MODE_PAGE_FLIP:
        return drm_ioctl_page_flip((struct drm_mode_crtc_page_flip *)argp);

    case DRM_IOCTL_MODE_DIRTYFB:
        return drm_ioctl_dirty_fb((struct drm_mode_fb_dirty_cmd *)argp);

    case DRM_IOCTL_PRIME_HANDLE_TO_FD:
        return drm_ioctl_prime_handle_to_fd((struct drm_prime_handle *)argp);

    case DRM_IOCTL_PRIME_FD_TO_HANDLE:
        return drm_ioctl_prime_fd_to_handle((struct drm_prime_handle *)argp);

    case DRM_IOCTL_SYNCOBJ_CREATE:
        return drm_ioctl_syncobj_create((struct drm_syncobj_create *)argp);

    case DRM_IOCTL_SYNCOBJ_DESTROY:
        return drm_ioctl_syncobj_destroy((struct drm_syncobj_destroy *)argp);

    case DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD:
        return drm_ioctl_syncobj_handle_to_fd((struct drm_syncobj_handle *)argp);

    case DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE:
        return drm_ioctl_syncobj_fd_to_handle((struct drm_syncobj_handle *)argp);

    case DRM_IOCTL_SYNCOBJ_WAIT:
        return drm_ioctl_syncobj_wait((struct drm_syncobj_wait *)argp);

    default:
        klog_warn("DRM: Unsupported ioctl 0x%lx", request);
        return -22; /* EINVAL */
    }
}

int drm_render_ioctl(uint64_t request, void *argp) {
    if (!g_drm_initialized) {
        drm_init();
    }

    switch (request) {
    case DRM_IOCTL_VERSION:
        return drm_ioctl_version((struct drm_version *)argp);

    case DRM_IOCTL_GET_CAP:
        return drm_ioctl_get_cap((struct drm_get_cap *)argp);

    case DRM_IOCTL_SET_CLIENT_CAP:
        return 0;

    case DRM_IOCTL_MODE_CREATE_DUMB:
        return drm_ioctl_create_dumb((struct drm_mode_create_dumb *)argp);

    case DRM_IOCTL_MODE_MAP_DUMB:
        return drm_ioctl_map_dumb((struct drm_mode_map_dumb *)argp);

    case DRM_IOCTL_MODE_DESTROY_DUMB:
        return drm_ioctl_destroy_dumb((struct drm_mode_destroy_dumb *)argp);

    case DRM_IOCTL_GEM_CLOSE:
        return drm_ioctl_gem_close((struct drm_gem_close *)argp);

    case DRM_IOCTL_PRIME_HANDLE_TO_FD:
        return drm_ioctl_prime_handle_to_fd((struct drm_prime_handle *)argp);

    case DRM_IOCTL_PRIME_FD_TO_HANDLE:
        return drm_ioctl_prime_fd_to_handle((struct drm_prime_handle *)argp);

    case DRM_IOCTL_SYNCOBJ_CREATE:
        return drm_ioctl_syncobj_create((struct drm_syncobj_create *)argp);

    case DRM_IOCTL_SYNCOBJ_DESTROY:
        return drm_ioctl_syncobj_destroy((struct drm_syncobj_destroy *)argp);

    case DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD:
        return drm_ioctl_syncobj_handle_to_fd((struct drm_syncobj_handle *)argp);

    case DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE:
        return drm_ioctl_syncobj_fd_to_handle((struct drm_syncobj_handle *)argp);

    case DRM_IOCTL_SYNCOBJ_WAIT:
        return drm_ioctl_syncobj_wait((struct drm_syncobj_wait *)argp);

    /* Render nodes explicitly forbid mode setting and master ioctls */
    case DRM_IOCTL_SET_MASTER:
    case DRM_IOCTL_DROP_MASTER:
    case DRM_IOCTL_MODE_SETCRTC:
    case DRM_IOCTL_MODE_PAGE_FLIP:
        return -13; /* EACCES */

    default:
        return drm_ioctl(request, argp);
    }
}

int drm_mmap(void *addr, size_t length, int prot, int flags, off_t offset, void **out_vaddr) {
    if (!out_vaddr || length == 0)
        return -22;

    uint32_t handle = (uint32_t)(offset >> 12);
    spinlock_acquire(&g_drm_lock);
    drm_dumb_bo_t *bo = drm_find_bo(handle);
    if (!bo || !bo->allocated) {
        spinlock_release(&g_drm_lock);
        return -22;
    }

    int ret = drm_mmap_bo_locked(bo, addr, length, prot, flags, out_vaddr);
    spinlock_release(&g_drm_lock);
    return ret;
}
