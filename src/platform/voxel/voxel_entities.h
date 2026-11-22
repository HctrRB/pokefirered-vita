/*
 * Player and NPC billboards for the voxel overworld.
 *
 * Phase F5b provides full Core 1 snapshot capture, Core 0 512x512 sprite atlas
 * decoding (64-slot dirty bitmask), object/effect billboards, cast shadows,
 * water reflections, 1x1/1x2 doors, follower, and up to 6 visible wilds.
 *
 * Origin: derived from pokeemerald-vita-3ds / pokeemerald-multiplatform /
 * Pokemon-FireRed-3DS (MIT); see NOTICE.md.
 */
#ifndef CTR_VOXEL_ENTITIES_H
#define CTR_VOXEL_ENTITIES_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_camera.h"
#include "voxel_mesh_builder.h"

#define VOXEL_SPRITE_ATLAS_W       512u
#define VOXEL_SPRITE_ATLAS_H       512u
#define VOXEL_SPRITE_SHEET_H       256u
#define VOXEL_SPRITE_SLOT_DIM      64u
#define VOXEL_SPRITE_COLUMNS       (VOXEL_SPRITE_ATLAS_W / VOXEL_SPRITE_SLOT_DIM)
#define VOXEL_SPRITE_SLOTS         32u
#define VOXEL_SPRITE_ATLAS_SLOTS   64u
#define VOXEL_DOOR_SLOT            (VOXEL_SPRITE_SLOTS - 1u)
#define VOXEL_SPRITE_PIXELS        (VOXEL_SPRITE_ATLAS_W * VOXEL_SPRITE_ATLAS_H)
#define VOXEL_FOLLOWER_CELL_X      0u
#define VOXEL_FOLLOWER_CELL_Y      256u
#define VOXEL_FOLLOWER_CELL_DIM    128u
#define VOXEL_FOLLOWER_DIRTY_MASK  ((1ULL << 32) | (1ULL << 33) | (1ULL << 40) | (1ULL << 41))
#define VOXEL_CAST_SHADOW_VERTICES 6u
#define VOXEL_REFLECTION_STRIPS    4u
#define VOXEL_REFLECTION_VERTICES  (6u * VOXEL_REFLECTION_STRIPS)
#define VOXEL_CLOUD_VERTICES       (256u * 6u)

void VoxelEntities_Reset(void);
void VoxelEntities_GetPlayerWorldPos(float *worldX, float *worldZ);
float VoxelEntities_PlayerDeck(void);
float VoxelEntities_PlayerGround(void);
float VoxelEntities_PlayerZShift(void);
void VoxelEntities_GetPlayerCameraTarget(float *worldX, float *worldZ);
int32_t VoxelEntities_PlayerVertexFirst(void);

uint32_t VoxelEntities_Emit(VoxelBuilder *builder, uint16_t *atlas,
                            const VoxelCamera *camera, VoxelBuilder *shadows,
                            VoxelBuilder *reflections);
uint64_t VoxelEntities_TakeDirtySlots(void);
void VoxelEntities_AllowLighting(bool allowed);

#endif
