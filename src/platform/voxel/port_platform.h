#ifndef GUARD_PORT_PLATFORM_H
#define GUARD_PORT_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const struct MapLayout *const gMapLayouts[];

bool Port_SceneShown(void);
#ifdef PLATFORM_VITA
bool Port_IsLinkedData(const void *ptr);
#endif

#define FIRERED_LAYOUT_COUNT 383u

static inline const void *Port_ResolveAssetPointer(const void *ptr)
{
    return ptr;
}

static inline u32 Port_GetAssetSizeExact(const void *ptr)
{
    (void)ptr;
    /* Upper bound covering compressed assets and 640-tile uncompressed primary tilesets (20480 B). */
    return 0x10000u;
}

static inline const struct MapLayout *Port_GetMapLayoutById(u16 layoutId)
{
    if (layoutId >= 1u && layoutId <= FIRERED_LAYOUT_COUNT)
        return gMapLayouts[layoutId - 1u];
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif // GUARD_PORT_PLATFORM_H
