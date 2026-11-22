#ifndef VOXEL_LAMPS_H
#define VOXEL_LAMPS_H

#include <stdint.h>

#include "voxel_mesh_builder.h"
#include "voxel_world.h"

#define VOXEL_LAMP_CHUNK_LIGHTS 8

uint32_t VoxelLamps_EmitChunk(VoxelBuilder *glass, VoxelBuilder *pool, const VoxelMapInstance *inst,
                              int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              float (*lights)[3], uint32_t lightCapacity);

#endif // VOXEL_LAMPS_H
