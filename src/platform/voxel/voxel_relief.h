#ifndef CTR_VOXEL_RELIEF_H
#define CTR_VOXEL_RELIEF_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_world.h"

#define VOXEL_RELIEF_SIDE 5

bool VoxelRelief_Init(void);
void VoxelRelief_Shutdown(void);

float VoxelRelief_Base(const VoxelMapInstance *inst);
int32_t VoxelRelief_SeamStep(const VoxelMapInstance *a, const VoxelMapInstance *b);
const int16_t *VoxelRelief_Cell(const VoxelMapInstance *inst, int32_t x, int32_t y);
bool VoxelRelief_IsDrawn(const VoxelMapInstance *inst);
uint32_t VoxelRelief_Belt(const VoxelMapInstance *inst);
int32_t VoxelRelief_FaceTile(const VoxelMapInstance *inst);

typedef struct
{
    int32_t metatile;
    bool water, hasTop;
    int32_t top;
} VoxelEdgeFill;

bool VoxelRelief_EdgeFill(uint32_t layoutId, uint32_t edge, int32_t along, uint32_t index, VoxelEdgeFill *fill);
bool VoxelRelief_EdgeFillAt(const VoxelMapInstance *ring, int32_t worldX, int32_t worldY, VoxelEdgeFill *fill);
bool VoxelRelief_EdgeFills(uint32_t layoutId, uint32_t i, int32_t *metatile);

bool VoxelRelief_IsSlope(const int16_t *grid);
const int16_t *VoxelRelief_Depth(const VoxelMapInstance *inst, int32_t x, int32_t y);
float VoxelRelief_CellLift(const VoxelMapInstance *inst, int32_t x, int32_t y);
float VoxelRelief_CellShift(const VoxelMapInstance *inst, int32_t x, int32_t y);
float VoxelRelief_LiftAt(float worldX, float worldZ);
float VoxelRelief_ShiftAt(float worldX, float worldZ);

bool VoxelRelief_Stretch(const VoxelMapInstance *inst, float *from, float *to, float *add);
float VoxelRelief_StretchZ(const VoxelMapInstance *inst, float z);

float VoxelRelief_SurfaceAt(float worldX, float worldZ);
float VoxelRelief_SurfaceTop(const VoxelMapInstance *inst, int32_t x, int32_t z, float *bottom);
float VoxelRelief_DrawnTop(const VoxelMapInstance *inst);

#define VOXEL_RELIEF_NO_VARIANT 0xFFFF
#define VOXEL_RELIEF_GOES_ON (2u << 8)
#define VOXEL_RELIEF_GOES_ON_FACE (32u << 8)
#define VOXEL_RELIEF_GOES_ON_SOUTH (64u << 8)
#define VOXEL_RELIEF_NO_FACE 0xFFFE
#define VOXEL_RELIEF_BLOCK_WALLS (4u << 8)
#define VOXEL_RELIEF_THIN_DECK (8u << 8)
#define VOXEL_RELIEF_FACE_TOP (16u << 8)
#define VOXEL_RELIEF_CANOPY (128u << 8)
#define VOXEL_RELIEF_DECK_EDGE (4.0f / 16.0f)

int32_t VoxelRelief_Cut(const VoxelMapInstance *inst, int32_t x, int32_t y, float *foot, int32_t *ground,
                        int32_t *wall, uint32_t *sides);
typedef void (*VoxelReliefCutVisit)(int32_t x, int32_t y, int32_t ground, int32_t wall, void *ctx);
void VoxelRelief_ForEachCut(const VoxelMapInstance *inst, VoxelReliefCutVisit visit, void *ctx);
uint32_t VoxelRelief_CutCount(void);
bool VoxelRelief_CutVariant(uint32_t i, uint32_t *layout, uint32_t *metatile,
                            const uint8_t **rows);

typedef struct
{
    int32_t count;
    int8_t a[4], b[4];
    int16_t c[4];
} VoxelCutDiagonals;

void VoxelRelief_CutDiagonals(const uint8_t *rows, VoxelCutDiagonals *out);
void VoxelRelief_CutClear(const uint8_t *rows, uint16_t clear[16]);
int32_t VoxelRelief_CutFringe(const uint8_t *rows, const uint16_t clear[16], int32_t x, int32_t y);

#endif
