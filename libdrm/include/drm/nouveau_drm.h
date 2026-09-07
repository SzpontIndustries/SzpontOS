#ifndef _NOUVEAU_DRM_H
#define _NOUVEAU_DRM_H
#include <drm/drm.h>
#define DRM_NOUVEAU_GETPARAM 0x00
#define DRM_IOCTL_NOUVEAU_GETPARAM DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GETPARAM, struct drm_nouveau_getparam)
#define NOUVEAU_GETPARAM_CHIPSET_ID 16
#define NOUVEAU_GETPARAM_FB_SIZE     17
#define NOUVEAU_GETPARAM_AGP_SIZE    18
#define NOUVEAU_GETPARAM_GRAPH_UNITS 19
struct drm_nouveau_getparam {
    uint64_t param;
    uint64_t value;
};
#endif
