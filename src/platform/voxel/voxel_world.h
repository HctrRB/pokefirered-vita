/*
 * Logical map view for the voxel overworld. Derived from the MIT-licensed
 * voxel_world.c/.h of pokeemerald-multiplatform; see NOTICE.md.
 *
 * Adapted for FireRed: u32 metatile attributes, VMap (Xsize/Ysize),
 * 640 primary metatiles / 7 primary palettes, and Kanto layout swaps.
 */
#ifndef CTR_VOXEL_WORLD_H
#define CTR_VOXEL_WORLD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    VOXEL_SHAPE_FLAT = 0,
    VOXEL_SHAPE_DECAL,
    VOXEL_SHAPE_FURNITURE,
    VOXEL_SHAPE_BED,
    VOXEL_SHAPE_TABLE,
    VOXEL_SHAPE_COUNTER,
    VOXEL_SHAPE_SIGN,
    VOXEL_SHAPE_WATER,
    VOXEL_SHAPE_VOID,
    VOXEL_SHAPE_COUNT
} VoxelVisualShape;

#define MAX_VOXEL_MAP_INSTANCES 16
#define VOXEL_WORLD_REACH       56

typedef struct
{
    const void *header;           /* const struct MapHeader *  */
    const void *layout;           /* const struct MapLayout *  */
    const void *primaryTileset;   /* const struct Tileset *    */
    const void *secondaryTileset; /* const struct Tileset *    */
    int32_t mapGroup, mapNum;
    int32_t layoutId;
    int32_t headerLayoutId;
    bool live;
    int32_t originX, originY;
    int32_t width, height;
    bool indoor;
    bool hasConnections;
    int32_t lift;
} VoxelMapInstance;

bool VoxelWorld_IsMapAvailable(void);
bool VoxelWorld_FieldShownLive(void);
bool VoxelWorld_IsBattleMapAvailable(void);

void VoxelWorld_BeginBatch(void);
void VoxelWorld_FreezeMemos(bool frozen);
void VoxelWorld_BuildInstances(void);

uint32_t VoxelWorld_InstanceCount(void);
const VoxelMapInstance *VoxelWorld_Instance(uint32_t index);
const VoxelMapInstance *VoxelWorld_GetInstanceAt(int32_t worldX, int32_t worldY);
bool VoxelWorld_GetInstanceOffset(int32_t mapGroup, int32_t mapNum, int32_t *outOriginX, int32_t *outOriginY);

int32_t VoxelWorld_GetMetatileId(int32_t worldX, int32_t worldY);
uint32_t VoxelWorld_GetCollision(int32_t worldX, int32_t worldY);
uint32_t VoxelWorld_GetMetatileBehavior(int32_t worldX, int32_t worldY);
bool VoxelWorld_IsTallGrassCell(int32_t worldX, int32_t worldY);
bool VoxelWorld_IsAshGrassCell(int32_t worldX, int32_t worldY);
bool VoxelWorld_IsFlowerCell(int32_t worldX, int32_t worldY);
bool VoxelWorld_IsVisibleReflectiveSurface(int32_t worldX, int32_t worldY);
bool VoxelWorld_UnderGroundMask(int32_t worldX, int32_t worldY, uint16_t rows[16]);
bool VoxelWorld_UsesTreeSprites(const VoxelMapInstance *inst);

int32_t VoxelWorld_BorderMetatile(int32_t worldX, int32_t worldY);
bool VoxelWorld_BorderIsWater(int32_t worldX, int32_t worldY);
VoxelVisualShape VoxelWorld_ClassifyTile(int32_t worldX, int32_t worldY);

void VoxelWorld_GetMapDimensions(int32_t *width, int32_t *height);
bool VoxelWorld_MapIsConnected(void);
uint32_t VoxelWorld_ClosedEdges(void);
void VoxelWorld_GetPlayerWorldCoords(float *worldX, float *worldZ);
void VoxelWorld_GetPlayerWorldCoordsLive(float *worldX, float *worldZ);
void VoxelWorld_GetLocation(int32_t *mapGroup, int32_t *mapNum);

typedef enum {
    VOXEL_WEATHER_CLEAR,
    VOXEL_WEATHER_SUN,
    VOXEL_WEATHER_RAIN,
    VOXEL_WEATHER_FOG,
    VOXEL_WEATHER_PARTICLES,
    VOXEL_WEATHER_SHADE,
    VOXEL_WEATHER_SAND,
    VOXEL_WEATHER_ASH
} VoxelWeatherClass;

VoxelWeatherClass VoxelWorld_Weather(void);
float VoxelWorld_WeatherAmount(void);
float VoxelWorld_Lightning(void);
float VoxelWorld_FogDensity(void);
bool VoxelWorld_Underground(void);
bool VoxelWorld_ScreenFade(float *amount, float rgb[3]);

uint32_t VoxelWorld_BlockHash(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
const uint16_t *VoxelWorld_LayoutDrawing(int32_t layoutId, int32_t *width, int32_t *height);
uint32_t VoxelWorld_LiveDigest(void);

const uint16_t *Voxel_ResolveMap(const void *layout);
const uint16_t *Voxel_ResolveMetatiles(const void *tileset);
const uint32_t *Voxel_ResolveAttributes(const void *tileset);
const uint16_t *Voxel_ResolvePalettes(const void *tileset);
uint32_t Voxel_MetatileCount(const void *tileset, uint32_t limit);

void VoxelWorld_SetMaterialView(const int32_t rect[4], int32_t margin);
void VoxelWorld_MarkUsedMetatiles(const void *primaryTileset, const void *secondaryTileset,
                                  uint8_t *used);

typedef struct {
    uint32_t source, written, size, packedSize, remaining, distance;
    uint8_t flags, bits;
    bool initialized, done, ok;
} VoxelTileLoad;

bool Voxel_LoadTilesStep(const void *tileset, uint8_t *dest, uint32_t destSize,
                         VoxelTileLoad *load, uint32_t bytes);

#endif
