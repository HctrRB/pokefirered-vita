#ifndef CTR_VOXEL_BUILDING_H
#define CTR_VOXEL_BUILDING_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_mesh_builder.h"
#include "voxel_world.h"

bool VoxelBuildings_Init(void);
void VoxelBuildings_Shutdown(void);

void VoxelBuildings_SetDrawings(const uint16_t *(*drawing)(int32_t layoutId, int32_t *width, int32_t *height));
void VoxelBuildings_WorldChanged(void);
void VoxelBuildings_PrepareWorld(void);
int32_t VoxelBuildings_LayoutOf(const VoxelMapInstance *inst);
uint32_t VoxelBuildings_LiveSignature(const VoxelMapInstance *inst);

int32_t VoxelBuildings_PageOf(const VoxelMapInstance *inst);
bool VoxelBuildings_PageSize(uint32_t page, uint32_t *width, uint32_t *height,
                             uint32_t *texHeight);
bool VoxelBuildings_ReadPage(uint32_t page, uint32_t first, uint32_t count, uint16_t *dest);
bool VoxelBuildings_ReadPageWorker(uint32_t page, uint32_t first, uint32_t count, uint16_t *dest);
void VoxelBuildings_CloseWorkerFile(void);
bool VoxelBuildings_PageHasGlow(uint32_t page);
bool VoxelBuildings_ReadGlow(uint32_t page, uint32_t first, uint32_t count, uint8_t *dest);
bool VoxelBuildings_ReadGlowWorker(uint32_t page, uint32_t first, uint32_t count, uint8_t *dest);

uint32_t VoxelBuildings_AnimRunCount(void);
bool VoxelBuildings_AnimRun(uint32_t run, uint32_t *page, int32_t *layout, uint32_t *offset,
                            uint32_t *bytes);
bool VoxelBuildings_AnimRunDirty(uint32_t run, const uint8_t dirty[128]);
void VoxelBuildings_ComposeAnimRun(uint32_t run, const uint8_t *liveTiles, uint16_t *dest);

float VoxelBuildings_MaxTop(void);
float VoxelBuildings_LayoutTop(const VoxelMapInstance *inst);

bool VoxelBuildings_CellAt(const VoxelMapInstance *inst, int32_t x, int32_t y,
                           int32_t *groundMetatile, float *top);
bool VoxelBuildings_BareVariant(const VoxelMapInstance *inst, int32_t x, int32_t y, int32_t *metatile);
bool VoxelBuildings_DeckAt(const VoxelMapInstance *inst, float worldX, float worldZ,
                           uint32_t elevation, float *lift);
const uint16_t *VoxelBuildings_Footprint(const VoxelMapInstance *inst, int32_t x, int32_t y);
bool VoxelBuildings_Variant(uint32_t i, uint32_t *layout, uint32_t *metatile, uint32_t *quarters);
void VoxelBuildings_SetVariantFits(bool (*fits)(const VoxelMapInstance *inst, uint32_t layout,
                                                uint32_t metatile));
void VoxelBuildings_MapChanged(void);
bool VoxelBuildings_FrontAt(const VoxelMapInstance *inst, int32_t x, int32_t y,
                            float x0, float x1, float height, float *z);
bool VoxelBuildings_StaticModel(uint32_t pageModel, const VoxelVertex **vertices,
                                uint32_t *count, float *ox, float *oy, float *su, float *sv);
bool VoxelBuildings_DoorWall(const VoxelMapInstance *inst, int32_t x, int32_t y, float *z);
void VoxelBuildings_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                                 int32_t x0, int32_t y0, int32_t x1, int32_t y1);

typedef struct
{
    uint32_t placement, part;
    uint32_t vertex;
} VoxelBuildingCursor;

bool VoxelBuildings_EmitSome(VoxelBuilder *builder, const VoxelMapInstance *inst,
                             int32_t x0, int32_t y0, int32_t x1, int32_t y1, VoxelBuildingCursor *cursor,
                             uint32_t triangles);

#endif
