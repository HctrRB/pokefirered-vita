#ifndef GUARD_VOXEL_RENDERER_H
#define GUARD_VOXEL_RENDERER_H

#if defined(VOXEL_SUPPORTED) || defined(PLATFORM_VITA)

#include <stdbool.h>
#include <stdint.h>

bool VoxelRenderer_Init(void);
bool VoxelRenderer_RenderFrame(void);
void VoxelRenderer_SwapBuffers(void);
void VoxelRenderer_OnReset(void);
void VoxelRenderer_NotifyTilesetAnimWrite(const void *dest, uint32_t bytes);
void VoxelRenderer_DrawFlyCloudCover(void);

#endif // VOXEL_SUPPORTED || PLATFORM_VITA
#endif // GUARD_VOXEL_RENDERER_H
