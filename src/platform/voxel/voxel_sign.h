#ifndef CTR_VOXEL_SIGN_H
#define CTR_VOXEL_SIGN_H

#include <stdint.h>

#include "voxel_mesh_builder.h"
#include "voxel_world.h"

#define VOXEL_SIGN_MAX_ROWS 32
typedef struct
{
    uint16_t opaque[VOXEL_SIGN_MAX_ROWS];
    uint8_t width;
    uint8_t height;
    uint8_t headRows;
    float u0, v0;
    float du, dv;
    float headU0, headV0;
} VoxelSignMask;

#define VOXEL_SIGN_PINNED_DEPTH 2

void VoxelSign_Emit(VoxelBuilder *builder, float wx, float wz,
                    float baseY, const VoxelSignMask *mask,
                    uint32_t depthPixels, float shade);
bool VoxelSign_EmitCell(VoxelBuilder *builder, const VoxelMapInstance *inst,
                        int32_t x, int32_t y);
bool VoxelSign_EmitStanding(VoxelBuilder *builder, const VoxelMapInstance *inst, int32_t x, int32_t y);
bool VoxelSign_IsCell(const VoxelMapInstance *inst, int32_t x, int32_t y);
float VoxelSign_CasterTop(const VoxelMapInstance *inst, int32_t x, int32_t y);
bool VoxelSign_Occludes(const VoxelMapInstance *inst, int32_t x, int32_t y,
                        float px, float py, float pz);
bool VoxelSign_HeadGround(const VoxelMapInstance *inst, int32_t x, int32_t y, int32_t *metatile);
bool VoxelSign_Ground(const VoxelMapInstance *inst, int32_t x, int32_t y, int32_t *metatile);
void VoxelSign_Init(void);
void VoxelSign_Shutdown(void);

#define VOXEL_LAMP_POOL_RADIUS 2.25f

bool VoxelSign_IsLantern(const uint16_t head[16]);
void VoxelSign_EmitLampPool(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            float centerX, float centerZ);
uint32_t VoxelSign_EmitNight(VoxelBuilder *glass, VoxelBuilder *ground,
                             const VoxelMapInstance *inst, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             float (*lights)[3], uint32_t lightCapacity);

#endif
