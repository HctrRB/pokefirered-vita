/*
 * voxel_fly.h - The Fly cinematic in the voxel overworld: pure state machine, no SDK.
 *
 * Phase F5a provides clean inactive types/stubs; full Kanto/Sevii 3D Fly lands
 * in Phase F5b.
 *
 * Origin: derived from pokeemerald-vita-3ds / Emerald 3DS (see NOTICE.md).
 */
#ifndef VOXEL_FLY_H
#define VOXEL_FLY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    VOXEL_FLY_IDLE = 0,
    VOXEL_FLY_DIVE,
    VOXEL_FLY_CLIMB,
    VOXEL_FLY_CLOUD,
    VOXEL_FLY_DESCEND,
} VoxelFlyPhase;

typedef struct
{
    VoxelFlyPhase phase;
    uint32_t frame;
    float dirX, dirZ;
    bool gone;
} VoxelFly;

typedef struct
{
    float lift;
    float rise;
    float whiteout;
    float wisps;
    float dirX, dirZ;
    bool oldMap;
    bool noFade;
} VoxelFlyFrame;

void VoxelFly_Begin(VoxelFly *f, float dirX, float dirZ);
void VoxelFly_Cancel(VoxelFly *f);
bool VoxelFly_Active(const VoxelFly *f);
bool VoxelFly_Hovers(const VoxelFly *f);
bool VoxelFly_Busy(const VoxelFly *f);
void VoxelFly_Frame(const VoxelFly *f, VoxelFlyFrame *out);
void VoxelFly_Step(VoxelFly *f, bool fieldShown, bool fieldGone);

#endif /* VOXEL_FLY_H */
