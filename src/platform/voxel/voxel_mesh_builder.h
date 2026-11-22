/*
 * CPU geometry for the voxel overworld.
 */
#ifndef CTR_VOXEL_MESH_BUILDER_H
#define CTR_VOXEL_MESH_BUILDER_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_world.h"

typedef struct
{
    float x, y, z;
    float u, v;
    float shade;
} VoxelVertex;

typedef struct
{
    uint32_t pageModel;
    bool lit;
    float x, y, z;
    float w, d, top;
} VoxelStaticDraw;

typedef struct
{
    VoxelVertex *vertices;
    uint32_t capacity;
    uint32_t count;
    uint32_t dropped;
    float originX, originZ;
    const struct VoxelAtlasMap *atlas;
    uint32_t uncovered;
    float lift, shift;
    float base;
    float stretchFrom, stretchTo, stretchAdd;
    bool lighting;
    bool artShaded;
    bool lightingRefine;
    bool rounded;
    bool vertexFace;
    float lightingConstant;
    VoxelStaticDraw *statics;
    uint32_t staticCapacity, staticCount;
    bool opaqueMark;
    uint8_t *triOpaque;
} VoxelBuilder;

void VoxelBuilder_Init(VoxelBuilder *builder, VoxelVertex *storage, uint32_t capacity);
void VoxelBuilder_SetAtlas(VoxelBuilder *builder, const struct VoxelAtlasMap *atlas);
void VoxelBuilder_SetOrigin(VoxelBuilder *builder, int32_t originX, int32_t originZ);
void VoxelBuilder_SetStretch(VoxelBuilder *builder, const VoxelMapInstance *inst);
float VoxelBuilder_StretchZ(const VoxelBuilder *builder, float z);
void VoxelBuilder_Place(const VoxelBuilder *builder, float *x, float *y, float *z);
void VoxelBuilder_Tri(VoxelBuilder *builder, const VoxelVertex *a,
                      const VoxelVertex *b, const VoxelVertex *c);
void VoxelBuilder_Quad(VoxelBuilder *builder, const VoxelVertex *a, const VoxelVertex *b,
                       const VoxelVertex *c, const VoxelVertex *d);

#define VOXEL_WINDOW_MAX         80
#define VOXEL_CHUNK_MARGIN_NORTH 8

void VoxelMesh_BeginWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

void VoxelMesh_Top(VoxelBuilder *b, float wx, float wz, float h, float inset,
                   float u0, float v0, float u1, float v1, float shade);
void VoxelMesh_ReliefGround(VoxelBuilder *b, const VoxelMapInstance *inst, int32_t x, int32_t y,
                            float u0, float v0, float u1, float v1);
bool VoxelMesh_TreeOnRock(const VoxelMapInstance *inst, int32_t x, int32_t y);
void VoxelMesh_South(VoxelBuilder *b, float wx, float wz, float yBottom, float yTop,
                     float inset, float u0, float v0, float u1, float v1);
void VoxelMesh_North(VoxelBuilder *b, float wx, float wz, float yBottom, float yTop,
                     float inset, float u0, float v0, float u1, float v1);
void VoxelMesh_West(VoxelBuilder *b, float wx, float wz, float yBottom, float yTop,
                    float inset, float u0, float v0, float u1, float v1);
void VoxelMesh_East(VoxelBuilder *b, float wx, float wz, float yBottom, float yTop,
                    float inset, float u0, float v0, float u1, float v1);

VoxelVisualShape VoxelMesh_Classify(int32_t x, int32_t y);
bool VoxelMesh_TileUV(VoxelBuilder *b, int32_t x, int32_t y,
                      float *u0, float *v0, float *u1, float *v1);
bool VoxelMesh_MetatileUV(VoxelBuilder *b, int32_t metatileId,
                          float *u0, float *v0, float *u1, float *v1);

void VoxelMesh_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void VoxelMesh_EmitGroundRow(VoxelBuilder *builder, const VoxelMapInstance *inst,
                             int32_t x0, int32_t x1, int32_t y);

int32_t VoxelMesh_DraftSlot(const VoxelMapInstance *inst, const struct VoxelAtlasMap *atlas,
                            int32_t x, int32_t y, uint32_t *uncovered);
void VoxelMesh_DraftCell(VoxelBuilder *b, const VoxelMapInstance *inst, int32_t x, int32_t y, int32_t slot);

void VoxelMesh_EmitBorder(VoxelBuilder *builder, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
bool VoxelMesh_BorderLevel(int32_t x, int32_t y, float *level, float *shift);
void VoxelMesh_SetBorderFollowsEdge(bool on);

#endif
