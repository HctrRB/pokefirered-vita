/* General tileset trees and Kanto props/woods: ground shadows, tilted cards, fences, ledges. */
#ifndef CTR_VOXEL_TREE_H
#define CTR_VOXEL_TREE_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_mesh_builder.h"

#define VOXEL_TREE_TEXTURE_DIM  512u   /* texture width */
#define VOXEL_TREE_TEXTURE_H    1024u  /* texture height: props/trees + rooms */
#define VOXEL_TREE_TEXTURE_PATH "voxel/trees.rgba5551"

/* Reserved 16x16 slot in trees.rgba5551 holding FireRed's standing tall grass sprite. */
#define VOXEL_TREE_TALL_GRASS_X 496u
#define VOXEL_TREE_TALL_GRASS_Y 1008u

/* Emerald's one-cell small tree (unused in FireRed General, kept for shared lighting). */
#define VOXEL_TREE_SMALL      4
#define VOXEL_TREE_BASE_LEFT  2
#define VOXEL_TREE_BASE_RIGHT 3
/* Three-cell wood trees: VOXEL_TREE_WOOD + column; VOXEL_TREE_WOOD_FLOOR for wood floor cells. */
#define VOXEL_TREE_WOOD       8
#define VOXEL_TREE_WOOD_FLOOR (VOXEL_TREE_WOOD + 3)

/* Pre-initializes lookup tables on boot before Core 2 workers start. */
void VoxelTree_Init(void);

int32_t VoxelTree_Part(const void *secondaryTileset, int32_t metatileId);
int32_t VoxelTree_Crown(const void *secondaryTileset, int32_t metatileId);
bool VoxelTree_InWood(const void *secondaryTileset, int32_t metatileId);
int32_t VoxelTree_GroundMetatile(const void *secondaryTileset, int32_t metatileId);

void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void VoxelTree_EmitBorder(VoxelBuilder *builder, int32_t x0, int32_t y0, int32_t x1, int32_t y1);

#endif
