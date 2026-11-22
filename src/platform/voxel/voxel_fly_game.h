/*
 * voxel_fly_game.h - Core 1 owner and Core 0 snapshot bridge for the 3D Fly cinematic.
 *
 * Phase F5a provides clean inactive stubs; full 3D Fly warp integration lands
 * in Phase F5b.
 */
#ifndef VOXEL_FLY_GAME_H
#define VOXEL_FLY_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "voxel_fly.h"

typedef struct
{
    bool active;
    uint32_t warpVersion;
    VoxelFlyFrame frame;
} VoxelFlySnapshot;

void VoxelFlyGame_OnWarpLoaded(void);
void VoxelFlyGame_Tick(void);
void VoxelFlyGame_OnReset(void);
const VoxelFlySnapshot *VoxelFly_GetSnapshot(void);
void VoxelFly_NotifyFrameDrawn(uint32_t warpVersion);

#endif /* VOXEL_FLY_GAME_H */
