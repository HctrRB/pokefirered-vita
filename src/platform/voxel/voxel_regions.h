#ifndef CTR_VOXEL_REGIONS_H
#define CTR_VOXEL_REGIONS_H

#include <stdbool.h>
#include <stdint.h>

#define VOXEL_ROLE_FLOOR 0
#define VOXEL_ROLE_WATER 1
#define VOXEL_ROLE_LEDGE 2
#define VOXEL_ROLE_STAIR 3
#define VOXEL_ROLE_WALL  4
#define VOXEL_ROLE_TREE  5
#define VOXEL_ROLE_PROP  6
#define VOXEL_ROLE_SHELF 7
#define VOXEL_ROLE_FENCE 8
#define VOXEL_ROLE_CLIFF 9
#define VOXEL_ROLE_SIGNPOST 10
#define VOXEL_ROLE_COUNT 11

bool VoxelRegions_Init(void);
void VoxelRegions_Shutdown(void);
uint32_t VoxelRegions_RoleAt(uint32_t layoutId, int32_t localX, int32_t localY);

#endif
