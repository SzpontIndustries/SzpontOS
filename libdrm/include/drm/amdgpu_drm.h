#ifndef _AMDGPU_DRM_H
#define _AMDGPU_DRM_H
#include <drm/drm.h>
#define DRM_AMDGPU_INFO 0x02
#define DRM_IOCTL_AMDGPU_INFO DRM_IOW(DRM_COMMAND_BASE + DRM_AMDGPU_INFO, struct drm_amdgpu_info)
#define AMDGPU_INFO_SENSOR 0x1a
#define AMDGPU_INFO_DEV_INFO 0x16
#define AMDGPU_INFO_SENSOR_GPU_TEMP 0x1
#define AMDGPU_INFO_SENSOR_GPU_LOAD 0x3
#define AMDGPU_INFO_VRAM_USAGE       0x17
#define AMDGPU_INFO_GTT_USAGE        0x18
#define AMDGPU_INFO_MEMORY           0x19
#define AMDGPU_IDS_FLAGS_FUSION 0x1

struct drm_amdgpu_info {
    uint64_t return_pointer;
    uint32_t return_size;
    uint32_t query;
    union {
        struct { uint32_t type; } query_hw_ip;
    };
};

struct drm_amdgpu_info_device {
    uint32_t device_id;
    uint32_t chip_rev;
    uint32_t external_rev;
    uint32_t pvr_minor;
    uint32_t family;
    uint32_t special_support;
    uint32_t max_engine_clock;
    uint32_t max_memory_clock;
    uint32_t cu_active_number;
    uint32_t cu_ao_mask;
    uint32_t ids_flags;
    uint32_t _pad;
    uint32_t _pad1;
    uint32_t vram_type;
    uint32_t vram_bit_width;
    uint32_t num_vram_cpu_accessible_pages;
    uint32_t num_vram_pages;
    uint32_t num_gtt_pages;
};

struct drm_amdgpu_memory_info {
    struct {
        uint64_t total_heap_size;
        uint64_t usable_heap_size;
        uint64_t heap_usage;
        uint64_t max_allocation;
    } vram, cpu_accessible_vram, gtt;
};
#endif
