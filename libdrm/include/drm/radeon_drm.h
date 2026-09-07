#ifndef _RADEON_DRM_H
#define _RADEON_DRM_H
#include <drm/drm.h>
#define DRM_RADEON_INFO 0x1c
#define DRM_RADEON_GEM_INFO 0x1c
#define DRM_IOCTL_RADEON_INFO DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_INFO, struct drm_radeon_info)
#define DRM_IOCTL_RADEON_GEM_INFO DRM_IOWR(DRM_COMMAND_BASE + DRM_RADEON_GEM_INFO, struct drm_radeon_gem_info)
#define RADEON_INFO_ACTIVE_CU_COUNT 0x07
#define RADEON_INFO_CURRENT_GPU_TEMP 0x08
#define RADEON_INFO_MAX_SCLK 0x09
#define RADEON_INFO_VRAM_USAGE 0x0a
#define RADEON_INFO_GTT_USAGE 0x0b
struct drm_radeon_info {
    uint32_t request;
    uint32_t pad;
    uint64_t value;
};
struct drm_radeon_gem_info {
    uint64_t gart_size;
    uint64_t vram_size;
    uint64_t vram_visible;
};
#endif
