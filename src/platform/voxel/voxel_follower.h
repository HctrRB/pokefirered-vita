/*
 * Mystery Dungeon follower and visible wild Pokemon cards rendered in the 3D
 * sprite atlas (`followers.bin`). Core 0 reads strictly from GameFrame /
 * VoxelEntitySnapshot without touching Core 1 game state.
 *
 * Origin: derived from Pokemon-FireRed-3DS (3ds_port/src/voxel/voxel_follower.h,
 * MIT) & pokeemerald-vita-3ds; see NOTICE.md.
 */
#ifndef CTR_VOXEL_FOLLOWER_H
#define CTR_VOXEL_FOLLOWER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    float x, z;          /* ground point, world */
    int32_t dx, dy;      /* picture top-left offset from ground point, pixels (y down) */
    int32_t w, h;        /* picture dimensions in the atlas follower cell */
    int32_t foot;        /* row from ground point where feet stand */
} VoxelFollowerCard;

/* Moves and animates the follower for this frame, writing into the follower atlas
 * cell when changed and setting dirty bits in *dirtySlots. False when none drawn. */
bool VoxelFollower_Update(uint16_t *atlas, float yawDegrees, VoxelFollowerCard *card,
                          uint64_t *dirtySlots);

/* Forgets path and cached follower/wild frames (call on reset/scene invalidation). */
void VoxelFollower_Reset(void);

#define VOXEL_WILD_MAX      6u
#define VOXEL_WILD_CELL_DIM 64u

typedef struct
{
    float x, z;          /* ground point, world */
    int32_t dx, dy;      /* picture top-left offset from ground point, source pixels */
    int32_t w, h;        /* picture dimensions in atlas cell, atlas pixels */
    int32_t foot;        /* row (source pixels) where feet stand */
    int32_t scale;       /* source pixels per atlas pixel: 1, or 2 for oversized species */
    uint32_t cellX, cellY;
    int32_t spriteId;    /* GBA party-icon sprite id omitted in 3D when replaced */
} VoxelWildCard;

/* Updates up to VOXEL_WILD_MAX visible wild cards from the pinned entity snapshot. */
uint32_t VoxelWild_Update(uint16_t *atlas, float yawDegrees, float originX, float originZ,
                          VoxelWildCard cards[VOXEL_WILD_MAX], uint64_t *dirtySlots);

#endif /* CTR_VOXEL_FOLLOWER_H */
