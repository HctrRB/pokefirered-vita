/*
 * Logical map view for the voxel overworld. Derived from the MIT-licensed
 * src/platform/voxel/voxel_world.c of pokeemerald-multiplatform (see NOTICE.md)
 * and adapted for Pokémon FireRed (32-bit attributes, VMap, 640/7 split, Kanto swaps).
 */

#include <stdint.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "overworld.h"
#include "event_data.h"
#include "constants/layouts.h"
#include "fieldmap.h"
#include "decompress.h"
#include "metatile_behavior.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "field_camera.h"
#include "field_weather.h"
#include "help_system.h"
#include "palette.h"
#include "quest_log.h"
#include "save_failed_screen.h"
#include "constants/field_weather.h"
#include "constants/map_types.h"
#include "constants/maps.h"
#include "constants/metatile_behaviors.h"
#include "constants/quest_log.h"
#include "constants/weather.h"
#include "port_platform.h"

#include "voxel_world.h"
#include "voxel_regions.h"
#include "platform/voxel_snapshot.h"
#include "platform/game_frame.h"
#include "voxel_tree.h"
#include "voxel_atlas.h"
#include "voxel_relief.h"
#include "voxel_building.h"
#include "voxel_room.h"
#include "3ds_video.h"

extern const struct Tileset gTileset_General;
extern const struct Tileset gTileset_Building;

/* FireRed: the behaviour is the low 9 bits of a 32-bit attribute; layer type is bits 29-30. */
#define UNPACK_BEHAVIOR(attr)   ((u8)((attr) & 0x1FFu))
#define UNPACK_LAYER_TYPE(attr) (((attr) >> 29) & 3u)

const u16 *MapGridGetCameraBorderAt(int32_t x, int32_t y, u8 *index);
u32 GetMapClosedEdges(void);

static VoxelMapInstance sInstances[MAX_VOXEL_MAP_INSTANCES];
static uint32_t sInstanceCount;

#define VOXEL_MEMO_SLOTS (MAX_VOXEL_MAP_INSTANCES + 4)

struct VoxelMemo
{
    struct { const void *key, *value; } entry[VOXEL_MEMO_SLOTS];
    uint32_t count;
};

static struct VoxelMemo sAttributeMemo, sMapMemo, sBorderMemo;
static volatile bool sMemosFrozen;

void VoxelWorld_BeginBatch(void)
{
    sAttributeMemo.count = 0;
    sMapMemo.count = 0;
    sBorderMemo.count = 0;
}

static bool MemoGet(const struct VoxelMemo *memo, const void *key, const void **out)
{
    for (uint32_t i = 0; i < memo->count; ++i)
        if (memo->entry[i].key == key)
        {
            *out = memo->entry[i].value;
            return true;
        }
    return false;
}

void VoxelWorld_FreezeMemos(bool frozen)
{
    sMemosFrozen = frozen;
}

static void MemoPut(struct VoxelMemo *memo, const void *key, const void *value)
{
    if (sMemosFrozen || memo->count >= VOXEL_MEMO_SLOTS)
        return;
    memo->entry[memo->count].key = key;
    memo->entry[memo->count].value = value;
    __sync_synchronize();
    ++memo->count;
}

/* ── Payload resolvers ──────────────────────────────────────────────────── */

const uint16_t *Voxel_ResolveMap(const void *layoutPtr)
{
    const struct MapLayout *layout = layoutPtr;
    const void *resolved;

    if (layout == NULL || layout->map == NULL)
        return NULL;
    if (MemoGet(&sMapMemo, layout->map, &resolved))
        return resolved;
    resolved = Port_ResolveAssetPointer(layout->map);
    MemoPut(&sMapMemo, layout->map, resolved);
    return resolved;
}

const uint16_t *Voxel_ResolveMetatiles(const void *tilesetPtr)
{
    const struct Tileset *tileset = tilesetPtr;

    if (tileset == NULL || tileset->metatiles == NULL)
        return NULL;
    return Port_ResolveAssetPointer(tileset->metatiles);
}

const uint32_t *Voxel_ResolveAttributes(const void *tilesetPtr)
{
    const struct Tileset *tileset = tilesetPtr;
    const void *resolved;

    if (tileset == NULL || tileset->metatileAttributes == NULL)
        return NULL;
    if (MemoGet(&sAttributeMemo, tileset->metatileAttributes, &resolved))
        return resolved;
    resolved = Port_ResolveAssetPointer(tileset->metatileAttributes);
    MemoPut(&sAttributeMemo, tileset->metatileAttributes, resolved);
    return resolved;
}

const uint16_t *Voxel_ResolvePalettes(const void *tilesetPtr)
{
    const struct Tileset *tileset = tilesetPtr;

    if (tileset == NULL || tileset->palettes == NULL)
        return NULL;
    return (const uint16_t *)Port_ResolveAssetPointer(tileset->palettes);
}

uint32_t Voxel_MetatileCount(const void *tilesetPtr, uint32_t limit)
{
    const struct Tileset *tileset = tilesetPtr;
    u32 bytes;

    if (tileset == NULL || tileset->metatiles == NULL)
        return 0;
    bytes = Port_GetAssetSizeExact(tileset->metatiles);
    if (bytes == 0)
        return limit;
    bytes /= NUM_TILES_PER_METATILE * sizeof(u16);
    return bytes < limit ? (uint32_t)bytes : limit;
}

/* ── Availability ───────────────────────────────────────────────────────── */

bool Voxel_LoadTilesStep(const void *tilesetPtr, uint8_t *dest, uint32_t destSize,
                         VoxelTileLoad *load, uint32_t bytes)
{
    const struct Tileset *tileset = tilesetPtr;
    const uint8_t *src;
    if (load->done) return true;
    if (!tileset || !tileset->tiles || !dest) goto failed;
    src = Port_ResolveAssetPointer(tileset->tiles);
    if (!src) goto failed;
    if (!load->initialized)
    {
        load->packedSize = Port_GetAssetSizeExact(tileset->tiles);
        if (!load->packedSize) goto failed;
        if (tileset->isCompressed)
        {
            if (load->packedSize < 4 || src[0] != 0x10) goto failed;
            load->size = (uint32_t)src[1] | ((uint32_t)src[2] << 8) | ((uint32_t)src[3] << 16);
            load->source = 4;
        }
        else
        {
            const uint32_t tileCount = tileset->isSecondary
                                     ? (uint32_t)(NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY)
                                     : (uint32_t)NUM_TILES_IN_PRIMARY;
            load->size = tileCount * 32u;
        }
        if (!load->size || load->size > destSize) goto failed;
        load->initialized = true;
    }
    if (load->size > destSize) goto failed;
    if (!tileset->isCompressed)
    {
        uint32_t count = load->size - load->written;
        if (count > bytes) count = bytes;
        memcpy(dest + load->written, src + load->written, count);
        load->written += count;
    }
    else while (bytes && load->written < load->size)
    {
        if (!load->remaining)
        {
            if (!load->bits)
            {
                if (load->source >= load->packedSize) goto failed;
                load->flags = src[load->source++];
                load->bits = 8;
            }
            bool match = (load->flags & 0x80) != 0;
            load->flags <<= 1;
            --load->bits;
            if (match)
            {
                if (load->packedSize - load->source < 2) goto failed;
                uint32_t a = src[load->source++], b = src[load->source++];
                load->remaining = (a >> 4) + 3;
                load->distance = ((a & 15) << 8) + b + 1;
                if (load->distance > load->written) goto failed;
            }
            else
            {
                if (load->source >= load->packedSize) goto failed;
                dest[load->written++] = src[load->source++];
                --bytes;
                continue;
            }
        }
        dest[load->written] = dest[load->written - load->distance];
        ++load->written;
        --load->remaining;
        --bytes;
    }
    if (load->written == load->size) load->done = load->ok = true;
    return load->done;
failed:
    load->done = true;
    load->ok = false;
    return true;
}

/* Core 1: whether the 3D overworld field is currently on screen. */
bool VoxelWorld_FieldShownLive(void)
{
    extern bool8 Shop_IsBuyMenuActive(void);
    extern bool8 Overworld_IsReturningToField(void);

    if (gMapHeader.mapLayout == NULL)
        return false;
    if (gSaveBlock1Ptr == NULL)
        return false;
    if (VMap.map == NULL)
        return false;
    if (Port_SceneShown() || QL_IS_PLAYBACK_STATE || HelpSystem_IsActiveForUi()
     || SaveFailed_IsActiveForUi())
        return false;
    return gMain.callback2 == CB2_Overworld
        || gMain.callback2 == CB2_OverworldBasic
        || Shop_IsBuyMenuActive()
        || Overworld_IsReturningToField();
}

static bool BattleSceneShownLive(void)
{
    extern void BattleMainCB2(void);
    return gMain.inBattle
        && gMain.callback2 == BattleMainCB2
        && gMapHeader.mapLayout != NULL
        && gSaveBlock1Ptr != NULL
        && VMap.map != NULL;
}

bool VoxelWorld_IsMapAvailable(void)
{
    return GameFrame_Get()->field.fieldShown;
}

bool VoxelWorld_IsBattleMapAvailable(void)
{
    return GameFrame_Get()->field.battleSceneShown;
}

/* ── Instances ──────────────────────────────────────────────────────────── */

static bool SeafoamB3fStopped(void)
{
    return FlagGet(FLAG_STOPPED_SEAFOAM_B3F_CURRENT) != 0;
}

static bool SeafoamB4fStopped(void)
{
    return FlagGet(FLAG_STOPPED_SEAFOAM_B4F_CURRENT) != 0;
}

static bool DunsparceTunnelDugOut(void)
{
    return IsNationalPokedexEnabled() != 0;
}

static const struct
{
    u8 mapGroup, mapNum;
    u16 layoutId;
} sLayoutSwaps[] = {
    { MAP_GROUP(MAP_SEAFOAM_ISLANDS_B3F),           MAP_NUM(MAP_SEAFOAM_ISLANDS_B3F),           LAYOUT_SEAFOAM_ISLANDS_B3F_CURRENT_STOPPED },
    { MAP_GROUP(MAP_SEAFOAM_ISLANDS_B4F),           MAP_NUM(MAP_SEAFOAM_ISLANDS_B4F),           LAYOUT_SEAFOAM_ISLANDS_B4F_CURRENT_STOPPED },
    { MAP_GROUP(MAP_THREE_ISLAND_DUNSPARCE_TUNNEL), MAP_NUM(MAP_THREE_ISLAND_DUNSPARCE_TUNNEL), LAYOUT_THREE_ISLAND_DUNSPARCE_TUNNEL_DUG_OUT },
};

static bool (*const sLayoutSwapApplies[])(void) = {
    SeafoamB3fStopped,
    SeafoamB4fStopped,
    DunsparceTunnelDugOut,
};

_Static_assert(ARRAY_COUNT(sLayoutSwaps) == ARRAY_COUNT(sLayoutSwapApplies), "one predicate per layout swap");
_Static_assert(ARRAY_COUNT(sLayoutSwaps) <= 32, "the swaps fit layoutSwapMask");

static u16 NeighbourLayoutId(const struct MapHeader *header, int32_t mapGroup, int32_t mapNum)
{
    for (uint32_t i = 0; i < ARRAY_COUNT(sLayoutSwaps); ++i)
        if (sLayoutSwaps[i].mapGroup == mapGroup && sLayoutSwaps[i].mapNum == mapNum
         && (GameFrame_Get()->field.layoutSwapMask & (1u << i))
         && Port_GetMapLayoutById(sLayoutSwaps[i].layoutId) != NULL)
            return sLayoutSwaps[i].layoutId;
    return header->mapLayoutId;
}

static void FillInstance(VoxelMapInstance *inst, const struct MapHeader *header, u16 layoutId,
                         int32_t mapGroup, int32_t mapNum, int32_t originX, int32_t originY)
{
    const struct MapLayout *layout = header == &gMapHeader || layoutId == header->mapLayoutId
                                   ? header->mapLayout : Port_GetMapLayoutById(layoutId);

    if (layout == NULL)
    {
        layout = header->mapLayout;
        layoutId = header->mapLayoutId;
    }
    inst->header = header;
    inst->layout = layout;
    inst->primaryTileset = layout->primaryTileset;
    inst->secondaryTileset = layout->secondaryTileset;
    inst->layoutId = layoutId;
    inst->headerLayoutId = header->mapLayoutId;
    inst->live = false;
    inst->mapGroup = mapGroup;
    inst->mapNum = mapNum;
    inst->originX = originX;
    inst->originY = originY;
    inst->width = layout->width;
    inst->height = layout->height;
    inst->indoor = (header->mapType == MAP_TYPE_INDOOR);
    inst->hasConnections = (header->connections != NULL && header->connections->count > 0);
    inst->lift = 0;
}

static bool ConnectionOrigin(const struct MapConnection *conn, const struct MapHeader *neighbour,
                             const VoxelMapInstance *from, int32_t *originX, int32_t *originY)
{
    switch (conn->direction)
    {
    case CONNECTION_NORTH:
        *originX = from->originX + conn->offset;
        *originY = from->originY - neighbour->mapLayout->height;
        return true;
    case CONNECTION_SOUTH:
        *originX = from->originX + conn->offset;
        *originY = from->originY + from->height;
        return true;
    case CONNECTION_WEST:
        *originX = from->originX - neighbour->mapLayout->width;
        *originY = from->originY + conn->offset;
        return true;
    case CONNECTION_EAST:
        *originX = from->originX + from->width;
        *originY = from->originY + conn->offset;
        return true;
    default:
        return false;
    }
}

#define VOXEL_PLACED_MAX 32
#define VOXEL_WALK_REACH (2 * VOXEL_WORLD_REACH)

static VoxelMapInstance sPlaced[VOXEL_PLACED_MAX];
static uint32_t sPlacedCount;

static int32_t RectGap(const VoxelMapInstance *a, int32_t x, int32_t y, int32_t width, int32_t height)
{
    int32_t dx = x >= a->originX + a->width ? x - (a->originX + a->width)
           : a->originX >= x + width ? a->originX - (x + width) : 0;
    int32_t dy = y >= a->originY + a->height ? y - (a->originY + a->height)
           : a->originY >= y + height ? a->originY - (y + height) : 0;

    return dx > dy ? dx : dy;
}

static void AddConnections(const VoxelMapInstance *from, bool near, bool keep)
{
    const struct MapHeader *header = from->header;
    const struct MapConnection *conn;

    if (header == NULL || header->connections == NULL)
        return;
    conn = header->connections->connections;
    for (s32 i = 0; i < header->connections->count; ++i, ++conn)
    {
        const struct MapHeader *neighbour;
        int32_t originX, originY, width, height, gap;
        bool clash = false;
        VoxelMapInstance *placed;

        if (sPlacedCount >= VOXEL_PLACED_MAX)
            return;
        if (conn->direction == CONNECTION_DIVE || conn->direction == CONNECTION_EMERGE)
            continue;
        neighbour = GetMapHeaderFromConnection(conn);
        if (neighbour == NULL || neighbour->mapLayout == NULL
         || !ConnectionOrigin(conn, neighbour, from, &originX, &originY))
            continue;
        width = neighbour->mapLayout->width;
        height = neighbour->mapLayout->height;
        gap = RectGap(&sPlaced[0], originX, originY, width, height);
        if (!near && !keep && gap > VOXEL_WALK_REACH)
            continue;
        for (uint32_t k = 0; !near && k < sPlacedCount && !clash; ++k)
        {
            const VoxelMapInstance *other = &sPlaced[k];

            clash = (other->mapGroup == conn->mapGroup && other->mapNum == conn->mapNum)
                 || (originX < other->originX + other->width
                     && originX + width > other->originX
                     && originY < other->originY + other->height
                     && originY + height > other->originY);
        }
        if (clash)
            continue;
        placed = &sPlaced[sPlacedCount++];
        FillInstance(placed, neighbour, NeighbourLayoutId(neighbour, conn->mapGroup, conn->mapNum),
                     conn->mapGroup, conn->mapNum, originX, originY);
        placed->lift = from->lift + VoxelRelief_SeamStep(from, placed);
        if ((near || keep || gap <= VOXEL_WORLD_REACH) && sInstanceCount < MAX_VOXEL_MAP_INSTANCES)
            sInstances[sInstanceCount++] = *placed;
    }
}

static bool VariantFits(const VoxelMapInstance *inst, uint32_t layoutId, uint32_t metatile)
{
    const struct MapLayout *layout = Port_GetMapLayoutById((u16)layoutId);

    if (layout == NULL)
        return false;
    return metatile < NUM_METATILES_IN_PRIMARY
         ? layout->primaryTileset == inst->primaryTileset
         : layout->secondaryTileset == inst->secondaryTileset;
}

#define LIFT_MEMORY 32
#define LIFT_FORGET (8 * 16)

static struct
{
    bool used;
    s16 mapGroup, mapNum;
    s16 lift;
} sLiftMemory[LIFT_MEMORY];
static uint32_t sLiftMemoryNext;

static int32_t RememberedLift(int32_t mapGroup, int32_t mapNum, bool *found)
{
    for (uint32_t i = 0; i < LIFT_MEMORY; ++i)
        if (sLiftMemory[i].used && sLiftMemory[i].mapGroup == mapGroup
         && sLiftMemory[i].mapNum == mapNum)
        {
            *found = true;
            return sLiftMemory[i].lift;
        }
    *found = false;
    return 0;
}

static void RememberLift(const VoxelMapInstance *inst)
{
    uint32_t slot = sLiftMemoryNext;

    for (uint32_t i = 0; i < LIFT_MEMORY; ++i)
        if (sLiftMemory[i].used && sLiftMemory[i].mapGroup == inst->mapGroup
         && sLiftMemory[i].mapNum == inst->mapNum)
        {
            sLiftMemory[i].lift = (s16)inst->lift;
            return;
        }
    if (inst->lift == 0)
        return;
    sLiftMemory[slot].used = true;
    sLiftMemory[slot].mapGroup = (s16)inst->mapGroup;
    sLiftMemory[slot].mapNum = (s16)inst->mapNum;
    sLiftMemory[slot].lift = (s16)inst->lift;
    sLiftMemoryNext = (slot + 1u) % LIFT_MEMORY;
}

static int32_t CurrentLift(int32_t mapGroup, int32_t mapNum)
{
    bool found;
    int32_t lift;

    for (uint32_t i = 0; i < sInstanceCount; ++i)
        if (sInstances[i].mapGroup == mapGroup && sInstances[i].mapNum == mapNum)
            return sInstances[i].lift;
    lift = RememberedLift(mapGroup, mapNum, &found);
    return lift > LIFT_FORGET || lift < -LIFT_FORGET ? 0 : lift;
}

void VoxelWorld_BuildInstances(void)
{
    if (!GameFrame_MapSettled())
        return;
    const GameFrameField *const field = &GameFrame_Get()->field;
    if (!field->saveLoaded)
        return;
    uint32_t near;
    int32_t lift = CurrentLift(field->mapGroup, field->mapNum);

    VoxelBuildings_SetVariantFits(VariantFits);
    VoxelBuildings_SetDrawings(VoxelWorld_LayoutDrawing);
    sInstanceCount = 0;
    sPlacedCount = 0;
    if (gMapHeader.mapLayout == NULL)
        return;

    FillInstance(&sPlaced[0], &gMapHeader, field->mapLayoutId,
                 field->mapGroup, field->mapNum, 0, 0);
    sPlaced[0].layoutId = field->mapLayoutId;
    sPlaced[0].lift = sPlaced[0].indoor ? 0 : lift;
    sInstances[0] = sPlaced[0];
    sInstances[0].live = true;
    sPlacedCount = sInstanceCount = 1;
    AddConnections(&sPlaced[0], true, true);
    near = sPlacedCount;
    for (uint32_t i = 1; i < sPlacedCount; ++i)
        AddConnections(&sPlaced[i], false, i < near);
    for (uint32_t i = 0; i < sInstanceCount; ++i)
        RememberLift(&sInstances[i]);
}

uint32_t VoxelWorld_InstanceCount(void)
{
    return sInstanceCount;
}

const VoxelMapInstance *VoxelWorld_Instance(uint32_t index)
{
    return index < sInstanceCount ? &sInstances[index] : NULL;
}

const VoxelMapInstance *VoxelWorld_GetInstanceAt(int32_t worldX, int32_t worldY)
{
    if (sInstanceCount > 0
     && worldX >= 0 && worldX < sInstances[0].width
     && worldY >= 0 && worldY < sInstances[0].height)
        return &sInstances[0];

    for (uint32_t i = 1; i < sInstanceCount; ++i)
    {
        const VoxelMapInstance *inst = &sInstances[i];
        int32_t localX = worldX - inst->originX;
        int32_t localY = worldY - inst->originY;

        if (localX >= 0 && localX < inst->width && localY >= 0 && localY < inst->height)
            return inst;
    }
    return NULL;
}

bool VoxelWorld_GetInstanceOffset(int32_t mapGroup, int32_t mapNum, int32_t *outOriginX, int32_t *outOriginY)
{
    if (outOriginX != NULL) *outOriginX = 0;
    if (outOriginY != NULL) *outOriginY = 0;

    if (sInstanceCount == 0)
        return false;

    if (sInstances[0].mapGroup == mapGroup && sInstances[0].mapNum == mapNum)
        return true;

    for (uint32_t i = 1; i < sInstanceCount; ++i)
    {
        if (sInstances[i].mapGroup == mapGroup && sInstances[i].mapNum == mapNum)
        {
            if (outOriginX != NULL) *outOriginX = sInstances[i].originX;
            if (outOriginY != NULL) *outOriginY = sInstances[i].originY;
            return true;
        }
    }
    return false;
}

/* ── Block lookups ──────────────────────────────────────────────────────── */

static u16 GetRawBlock(int32_t worldX, int32_t worldY, const VoxelMapInstance **outInst)
{
    const VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(worldX, worldY);
    int32_t localX, localY;

    if (outInst != NULL)
        *outInst = inst;
    if (inst == NULL)
        return 0;

    localX = worldX - inst->originX;
    localY = worldY - inst->originY;

    if (inst == &sInstances[0])
    {
        int32_t bx = localX + MAP_OFFSET;
        int32_t by = localY + MAP_OFFSET;

        if (VMap.map != NULL && bx >= 0 && bx < VMap.Xsize && by >= 0 && by < VMap.Ysize)
            return VMap.map[bx + VMap.Xsize * by];
        return 0;
    }
    else
    {
        const uint16_t *map = Voxel_ResolveMap(inst->layout);

        if (map == NULL)
            return 0;
        return map[localY * inst->width + localX];
    }
}

int32_t VoxelWorld_GetMetatileId(int32_t worldX, int32_t worldY)
{
    return GetRawBlock(worldX, worldY, NULL) & MAPGRID_METATILE_ID_MASK;
}

uint32_t VoxelWorld_GetCollision(int32_t worldX, int32_t worldY)
{
    return (GetRawBlock(worldX, worldY, NULL) & MAPGRID_COLLISION_MASK) >> MAPGRID_COLLISION_SHIFT;
}

bool VoxelWorld_IsTallGrassCell(int32_t worldX, int32_t worldY)
{
    const u8 beh = (u8)VoxelWorld_GetMetatileBehavior(worldX, worldY);
    return MetatileBehavior_IsTallGrass(beh) != 0;
}

bool VoxelWorld_IsAshGrassCell(int32_t worldX, int32_t worldY)
{
    (void)worldX;
    (void)worldY;
    return false;
}

#define FLOWER_TILE_FIRST 508
#define FLOWER_TILE_LAST  511

bool VoxelWorld_IsFlowerCell(int32_t worldX, int32_t worldY)
{
    static uint8_t sFlower[NUM_METATILES_IN_PRIMARY / 8];
    static int sFlowerState;
    uint8_t local[NUM_METATILES_IN_PRIMARY / 8];
    const uint8_t *bits = sFlower;
    const VoxelMapInstance *inst = NULL;
    u16 block = GetRawBlock(worldX, worldY, &inst);
    uint32_t id = block & MAPGRID_METATILE_ID_MASK;

    if (inst == NULL || inst->primaryTileset != &gTileset_General)
        return false;
    if (id >= NUM_METATILES_IN_PRIMARY)
    {
        const struct Tileset *const sec = (const struct Tileset *)inst->secondaryTileset;
        if (sec == NULL || sec->metatiles == NULL)
            return false;
        const uint32_t secIdx = id - NUM_METATILES_IN_PRIMARY;
        if (secIdx >= (NUM_METATILES_TOTAL - NUM_METATILES_IN_PRIMARY))
            return false;
        const u16 *const smt = sec->metatiles + secIdx * 8u;
        for (uint32_t t = 0; t < 8u; ++t)
        {
            const uint16_t tile = smt[t] & 0x3FFu;
            if (tile >= FLOWER_TILE_FIRST && tile <= FLOWER_TILE_LAST)
                return true;
        }
        return false;
    }
    if (__atomic_load_n(&sFlowerState, __ATOMIC_ACQUIRE) != 2)
    {
        const u16 *mt = gTileset_General.metatiles;
        if (mt == NULL)
            return false;
        memset(local, 0, sizeof(local));
        for (uint32_t m = 0; m < NUM_METATILES_IN_PRIMARY; ++m)
        {
            for (uint32_t t = 0; t < 8u; ++t)
            {
                uint16_t tile = mt[m * 8u + t] & 0x3FFu;
                if (tile >= FLOWER_TILE_FIRST && tile <= FLOWER_TILE_LAST)
                {
                    local[m >> 3] |= (uint8_t)(1u << (m & 7u));
                    break;
                }
            }
        }
        int expected = 0;
        if (__atomic_compare_exchange_n(&sFlowerState, &expected, 1, false,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            memcpy(sFlower, local, sizeof(sFlower));
            __atomic_store_n(&sFlowerState, 2, __ATOMIC_RELEASE);
        }
        else
        {
            bits = local;
        }
    }
    return (bits[id >> 3] & (uint8_t)(1u << (id & 7u))) != 0u;
}

static u32 GetMetatileAttribute(const VoxelMapInstance *inst, int32_t metatileId)
{
    const uint32_t *attributes;
    uint32_t index;

    if (inst == NULL || metatileId < 0)
        return 0;
    if (metatileId < NUM_METATILES_IN_PRIMARY)
    {
        attributes = Voxel_ResolveAttributes(inst->primaryTileset);
        index = (uint32_t)metatileId;
    }
    else
    {
        attributes = Voxel_ResolveAttributes(inst->secondaryTileset);
        index = (uint32_t)metatileId - NUM_METATILES_IN_PRIMARY;
    }
    return attributes != NULL ? attributes[index] : 0;
}

uint32_t VoxelWorld_GetMetatileBehavior(int32_t worldX, int32_t worldY)
{
    const VoxelMapInstance *inst = NULL;
    u16 block = GetRawBlock(worldX, worldY, &inst);

    return UNPACK_BEHAVIOR(GetMetatileAttribute(inst, block & MAPGRID_METATILE_ID_MASK));
}

bool VoxelWorld_IsVisibleReflectiveSurface(int32_t worldX, int32_t worldY)
{
    const VoxelMapInstance *inst = NULL;
    u16 block = GetRawBlock(worldX, worldY, &inst);
    uint32_t metatileId, behavior;

    if (inst == NULL)
        return false;
    metatileId = block & MAPGRID_METATILE_ID_MASK;
    behavior = UNPACK_BEHAVIOR(GetMetatileAttribute(inst, (int32_t)metatileId));
    return MetatileBehavior_IsReflective(behavior) || MetatileBehavior_IsIce(behavior);
}

#define UNDER_GROUND_CACHE 64u

bool VoxelWorld_UnderGroundMask(int32_t worldX, int32_t worldY, uint16_t rows[16])
{
    static struct
    {
        const void *primary, *secondary;
        uint16_t metatileId;
        uint16_t rows[16];
    } sCache[UNDER_GROUND_CACHE];
    const VoxelMapInstance *inst = NULL;
    u16 block = GetRawBlock(worldX, worldY, &inst);
    const uint8_t *tiles = CtrVideo_GetBgVram();
    const uint16_t *entries;
    uint32_t metatileId, localIdx, slot;

    if (inst == NULL || tiles == NULL || sInstanceCount == 0
     || inst->primaryTileset != sInstances[0].primaryTileset
     || inst->secondaryTileset != sInstances[0].secondaryTileset)
        return false;
    metatileId = block & MAPGRID_METATILE_ID_MASK;
    slot = metatileId % UNDER_GROUND_CACHE;
    if (sCache[slot].primary == inst->primaryTileset && sCache[slot].secondary == inst->secondaryTileset
     && sCache[slot].metatileId == metatileId)
    {
        memcpy(rows, sCache[slot].rows, sizeof(sCache[slot].rows));
        return true;
    }
    if (metatileId < NUM_METATILES_IN_PRIMARY)
    {
        entries = Voxel_ResolveMetatiles(inst->primaryTileset);
        localIdx = metatileId;
    }
    else
    {
        entries = Voxel_ResolveMetatiles(inst->secondaryTileset);
        localIdx = metatileId - NUM_METATILES_IN_PRIMARY;
    }
    if (entries == NULL)
        return false;
    entries += localIdx * NUM_TILES_PER_METATILE;
    memset(rows, 0, 16 * sizeof(rows[0]));
    if (UNPACK_LAYER_TYPE(GetMetatileAttribute(inst, (int32_t)metatileId)) != METATILE_LAYER_TYPE_NORMAL)
    {
        for (uint32_t q = 0; q < 4; ++q)
        {
            uint16_t entry = entries[4 + q];
            const uint8_t *tile = tiles + (entry & 0x3FFu) * 32u;

            for (uint32_t y = 0; y < 8; ++y)
                for (uint32_t x = 0; x < 8; ++x)
                {
                    uint32_t sx = (entry & 0x400u) ? 7u - x : x;
                    uint32_t sy = (entry & 0x800u) ? 7u - y : y;
                    uint8_t packed = tile[sy * 4u + sx / 2u];

                    if (((sx & 1u) ? packed >> 4 : packed & 0xFu) == 0u)
                        rows[(q >> 1) * 8u + y] |= (uint16_t)(1u << ((q & 1u) * 8u + x));
                }
        }
    }
    sCache[slot].primary = inst->primaryTileset;
    sCache[slot].secondary = inst->secondaryTileset;
    sCache[slot].metatileId = (uint16_t)metatileId;
    memcpy(sCache[slot].rows, rows, sizeof(sCache[slot].rows));
    return true;
}

bool VoxelWorld_UsesTreeSprites(const VoxelMapInstance *inst)
{
    return inst != NULL && !inst->indoor && inst->primaryTileset == &gTileset_General;
}

/* ── Classification ─────────────────────────────────────────────────────── */

static VoxelVisualShape BehaviorShape(u8 behavior)
{
    if (MetatileBehavior_IsSurfable(behavior)
     || MetatileBehavior_IsPuddle(behavior)
     || MetatileBehavior_IsShallowFlowingWater(behavior)
     || behavior == MB_HOT_SPRINGS)
        return VOXEL_SHAPE_WATER;
    if (MetatileBehavior_IsCounter(behavior))
        return VOXEL_SHAPE_COUNTER;
    if (MetatileBehavior_IsPC(behavior) || behavior == MB_TELEVISION)
        return VOXEL_SHAPE_FURNITURE;
    return VOXEL_SHAPE_COUNT;
}

VoxelVisualShape VoxelWorld_ClassifyTile(int32_t worldX, int32_t worldY)
{
    const VoxelMapInstance *inst = NULL;
    u16 block = GetRawBlock(worldX, worldY, &inst);
    int32_t metatileId;
    VoxelVisualShape announced;

    if (inst == NULL)
        return VOXEL_SHAPE_VOID;

    metatileId = block & MAPGRID_METATILE_ID_MASK;
    if (VoxelWorld_UsesTreeSprites(inst))
    {
        int32_t ground = VoxelTree_GroundMetatile(inst->secondaryTileset, metatileId);
        if (VoxelTree_Part(inst->secondaryTileset, metatileId) >= 0 || ground != metatileId)
        {
            int32_t attrId = (ground >= (int32_t)VOXEL_BARE_FIRST)
                           ? (ground - (int32_t)VOXEL_BARE_FIRST) : ground;
            if (VoxelTree_Part(inst->secondaryTileset, metatileId) < 0
             && BehaviorShape(UNPACK_BEHAVIOR(GetMetatileAttribute(inst, attrId))) == VOXEL_SHAPE_WATER)
                return VOXEL_SHAPE_WATER;
            return VOXEL_SHAPE_FLAT;
        }
    }
    if (VoxelRegions_RoleAt((uint32_t)inst->layoutId,
                            worldX - inst->originX, worldY - inst->originY)
        == VOXEL_ROLE_SIGNPOST)
        return VOXEL_SHAPE_SIGN;

    announced = BehaviorShape(UNPACK_BEHAVIOR(GetMetatileAttribute(inst, metatileId)));
    if (announced != VOXEL_SHAPE_COUNT)
        return announced;

    if (inst->indoor)
    {
        if (VoxelAtlas_IsVoid(inst, metatileId))
            return VOXEL_SHAPE_VOID;
    }

    return VOXEL_SHAPE_FLAT;
}

/* ── Queries used by the camera and the renderer ────────────────────────── */

void VoxelWorld_GetMapDimensions(int32_t *width, int32_t *height)
{
    if (sInstanceCount > 0u)
    {
        if (width != NULL) *width = sInstances[0].width;
        if (height != NULL) *height = sInstances[0].height;
        return;
    }
    if (GameFrame_MapSettled() && gMapHeader.mapLayout != NULL)
    {
        if (width != NULL) *width = gMapHeader.mapLayout->width;
        if (height != NULL) *height = gMapHeader.mapLayout->height;
    }
    else
    {
        if (width != NULL) *width = 0;
        if (height != NULL) *height = 0;
    }
}

static void PlayerTile(float *x, float *z)
{
    const struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];

    *x = (float)(player->currentCoords.x - MAP_OFFSET);
    *z = (float)(player->currentCoords.y - MAP_OFFSET);
}

void VoxelWorld_GetPlayerWorldCoordsLive(float *worldX, float *worldZ)
{
    float x, z;

    PlayerTile(&x, &z);
    if (sInstanceCount > 0)
    {
        x += (float)sInstances[0].originX;
        z += (float)sInstances[0].originY;
    }
    if (worldX != NULL) *worldX = x;
    if (worldZ != NULL) *worldZ = z;
}

void VoxelWorld_GetPlayerWorldCoords(float *worldX, float *worldZ)
{
    float x = GameFrame_Get()->field.playerTileX;
    float z = GameFrame_Get()->field.playerTileZ;

    if (sInstanceCount > 0)
    {
        x += (float)sInstances[0].originX;
        z += (float)sInstances[0].originY;
    }
    if (worldX != NULL) *worldX = x;
    if (worldZ != NULL) *worldZ = z;
}

static VoxelWeatherClass WeatherLive(void)
{
    switch (GetCurrentWeather())
    {
    case WEATHER_SUNNY:
    case WEATHER_DROUGHT:
        return VOXEL_WEATHER_SUN;
    case WEATHER_RAIN:
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR:
        return VOXEL_WEATHER_RAIN;
    case WEATHER_FOG_HORIZONTAL:
    case WEATHER_FOG_DIAGONAL:
    case WEATHER_UNDERWATER:
    case WEATHER_UNDERWATER_BUBBLES:
        return VOXEL_WEATHER_FOG;
    case WEATHER_SNOW:
        return VOXEL_WEATHER_PARTICLES;
    case WEATHER_VOLCANIC_ASH:
        return VOXEL_WEATHER_ASH;
    case WEATHER_SANDSTORM:
        return VOXEL_WEATHER_SAND;
    case WEATHER_SHADE:
    case WEATHER_SUNNY_CLOUDS:
        return VOXEL_WEATHER_SHADE;
    }
    return VOXEL_WEATHER_CLEAR;
}

static float WeatherAmountLive(void)
{
    float amount;

    if (gWeatherPtr == NULL)
        return 0.0f;
    switch (GetCurrentWeather())
    {
    case WEATHER_RAIN:
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR:
        return (float)gWeatherPtr->curRainSpriteIndex / MAX_RAIN_SPRITES;
    case WEATHER_SNOW:
        if (gWeatherPtr->targetSnowflakeSpriteCount == 0)
            return 0.0f;
        amount = (float)gWeatherPtr->snowflakeSpriteCount / (float)gWeatherPtr->targetSnowflakeSpriteCount;
        return amount > 1.0f ? 1.0f : amount;
    case WEATHER_VOLCANIC_ASH:
        if (!gWeatherPtr->ashSpritesCreated)
            return 0.0f;
        break;
    case WEATHER_SANDSTORM:
        if (!gWeatherPtr->sandstormSpritesCreated)
            return 0.0f;
        break;
    default:
        return 0.0f;
    }
    amount = (float)gWeatherPtr->currBlendEVA / 16.0f;
    return amount > 1.0f ? 1.0f : amount;
}

static float LightningLive(void)
{
    if (gWeatherPtr == NULL || GetCurrentWeather() != WEATHER_RAIN_THUNDERSTORM
     || gWeatherPtr->gammaIndex <= 3)
        return 0.0f;
    return gWeatherPtr->gammaIndex >= 19 ? 1.0f : (float)(gWeatherPtr->gammaIndex - 3) / 16.0f;
}

static float FogDensityLive(void)
{
    float density;

    if (gWeatherPtr == NULL || (!gWeatherPtr->fogHSpritesCreated && !gWeatherPtr->fogDSpritesCreated))
        return 0.0f;
    density = (float)gWeatherPtr->currBlendEVA / 12.0f;
    return density > 1.0f ? 1.0f : density;
}

VoxelWeatherClass VoxelWorld_Weather(void)
{
    return (VoxelWeatherClass)GameFrame_Get()->weather.weatherClass;
}

float VoxelWorld_WeatherAmount(void)
{
    return GameFrame_Get()->weather.amount;
}

float VoxelWorld_Lightning(void)
{
    return GameFrame_Get()->weather.lightning;
}

float VoxelWorld_FogDensity(void)
{
    return GameFrame_Get()->weather.fogDensity;
}

bool VoxelWorld_Underground(void)
{
    return GameFrame_Get()->field.underground != 0;
}

bool VoxelWorld_MapIsConnected(void)
{
    return sInstanceCount > 0u ? sInstances[0].hasConnections : false;
}

uint32_t VoxelWorld_ClosedEdges(void)
{
    return GetMapClosedEdges();
}

static float FadeChannel(uint32_t color, uint32_t shift)
{
    return (float)((color >> shift) & 31);
}

static void FadeFit(const u16 *shown, const u16 *unfaded, uint32_t count,
                    float *amount, float rgb[3])
{
    float n = (float)count, cov = 0.0f, var = 0.0f;
    float su[3] = {0}, sf[3] = {0}, suu[3] = {0}, suf[3] = {0};

    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t c = 0; c < 3; ++c)
        {
            float u = FadeChannel(unfaded[i], c * 5), f = FadeChannel(shown[i], c * 5);

            su[c] += u;
            sf[c] += f;
            suu[c] += u * u;
            suf[c] += u * f;
        }
    for (uint32_t c = 0; c < 3; ++c)
    {
        cov += n * suf[c] - su[c] * sf[c];
        var += n * suu[c] - su[c] * su[c];
    }
    float keep = var > 1.0f ? cov / var : 0.0f;

    if (keep < 0.0f) keep = 0.0f;
    if (keep > 1.0f) keep = 1.0f;
    *amount = 1.0f - keep;
    for (uint32_t c = 0; c < 3; ++c)
    {
        float offset = (sf[c] - keep * su[c]) / n;
        float value = *amount > 0.01f ? offset / *amount : 0.0f;

        if (value < 0.0f) value = 0.0f;
        if (value > 31.0f) value = 31.0f;
        rgb[c] = value / 31.0f;
    }
}

static bool ScreenFadeLive(float *amount, float rgb[3])
{
    bool fading;
    const u16 *shown = (const u16 *)PLTT, *unfaded = gPlttBufferUnfaded;
    uint32_t count = NUM_PALS_TOTAL * 16u;

    *amount = 0.0f;
    rgb[0] = rgb[1] = rgb[2] = 0.0f;

    extern bool8 Overworld_IsReturningToField(void);
    if (Overworld_IsReturningToField() && !gPaletteFade.active)
    {
        *amount = 1.0f;
        return true;
    }

    fading = gPaletteFade.active || gPaletteFade.y != 0
          || (gWeatherPtr != NULL && gWeatherPtr->palProcessingState == WEATHER_PAL_STATE_SCREEN_FADING_IN)
          || (gWeatherPtr != NULL && gWeatherPtr->palProcessingState == WEATHER_PAL_STATE_SCREEN_FADING_OUT);

    if (gMain.inBattle)
    {
        shown += BG_PLTT_ID(2);
        unfaded += BG_PLTT_ID(2);
        count = 3u * 16u;
        fading = true;
    }
    if (!fading || memcmp(shown, unfaded, count * sizeof(u16)) == 0)
        return false;
    FadeFit(shown, unfaded, count, amount, rgb);
    if (*amount < 1.0f / 32.0f)
        *amount = 0.0f;
    return *amount > 0.0f;
}

bool VoxelWorld_ScreenFade(float *amount, float rgb[3])
{
    const GameFrameFade *const fade = &GameFrame_Get()->fade;

    *amount = fade->amount;
    rgb[0] = fade->rgb[0];
    rgb[1] = fade->rgb[1];
    rgb[2] = fade->rgb[2];
    return fade->fading;
}

void VoxelWorld_GetLocation(int32_t *mapGroup, int32_t *mapNum)
{
    const GameFrameField *const field = &GameFrame_Get()->field;

    if (mapGroup != NULL) *mapGroup = field->mapGroup;
    if (mapNum != NULL) *mapNum = field->mapNum;
}

/* Core 1: captures FireRed field state into the published GameFrame slot. */
void VoxelWorld_CaptureField(GameFrameField *field, GameFrameWeather *weather, GameFrameFade *fade, bool8 voxel)
{
    field->saveLoaded = (gSaveBlock1Ptr != NULL);
    field->fieldShown = VoxelWorld_FieldShownLive();
    field->overworldCallback = gMapHeader.mapLayout != NULL && gSaveBlock1Ptr != NULL
                            && (gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic);
    field->isOutdoor = gMapHeader.mapLayout != NULL && IsMapTypeOutdoors(gMapHeader.mapType);
    field->underground = gMapHeader.mapLayout != NULL && gMapHeader.mapType == MAP_TYPE_UNDERGROUND;
    field->battleSceneShown = BattleSceneShownLive();
    field->questLogPlayback = QL_IS_PLAYBACK_STATE;
    field->helpSystemActive = HelpSystem_IsActiveForUi();
    field->saveFailedActive = SaveFailed_IsActiveForUi();
    field->mapGroup = gSaveBlock1Ptr != NULL ? (s16)gSaveBlock1Ptr->location.mapGroup : -1;
    field->mapNum = gSaveBlock1Ptr != NULL ? (s16)gSaveBlock1Ptr->location.mapNum : -1;
    field->mapLayoutId = gSaveBlock1Ptr != NULL ? gSaveBlock1Ptr->mapLayoutId : 0;
    field->layoutSwapMask = 0;
    if (voxel && gSaveBlock1Ptr != NULL)
    {
        for (uint32_t i = 0; i < ARRAY_COUNT(sLayoutSwaps); ++i)
            if (sLayoutSwapApplies[i] == NULL || sLayoutSwapApplies[i]())
                field->layoutSwapMask |= 1u << i;
    }
    PlayerTile(&field->playerTileX, &field->playerTileZ);
    field->playerFacing = gObjectEvents[gPlayerAvatar.objectEventId].facingDirection;
    field->playerSurfing = TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) != 0;
    field->callback2 = (const void *)gMain.callback2;

    if (!voxel)
    {
        memset(weather, 0, sizeof(*weather));
        memset(fade, 0, sizeof(*fade));
        return;
    }
    weather->weatherClass = (u8)WeatherLive();
    weather->amount = WeatherAmountLive();
    weather->lightning = LightningLive();
    weather->fogDensity = FogDensityLive();

    fade->fading = ScreenFadeLive(&fade->amount, fade->rgb);
}

static int32_t sMaterialView[4], sMaterialMargin;
static bool sMaterialViewSet;

void VoxelWorld_SetMaterialView(const int32_t rect[4], int32_t margin)
{
    sMaterialViewSet = rect != NULL;
    if (rect != NULL) memcpy(sMaterialView, rect, sizeof(sMaterialView));
    sMaterialMargin = margin;
}

static int32_t EdgeFillAt(int32_t worldX, int32_t worldY)
{
    VoxelEdgeFill fill;

    if (sInstanceCount == 0 || !VoxelRelief_EdgeFillAt(&sInstances[0], worldX, worldY, &fill))
        return -1;
    return fill.metatile;
}

static void MarkEdgeFills(const VoxelMapInstance *inst, uint8_t *used)
{
    int32_t metatile;

    for (uint32_t i = 0; VoxelRelief_EdgeFills((uint32_t)inst->layoutId, i, &metatile); ++i)
        if (metatile >= 0 && metatile < NUM_METATILES_TOTAL)
            used[metatile] = 2;
}

typedef struct
{
    const VoxelMapInstance *inst;
    int32_t x0, y0, x1, y1;
    uint8_t *used;
} CutMark;

static void MarkCut(int32_t x, int32_t y, int32_t ground, int32_t wall, void *ctx)
{
    const CutMark *mark = ctx;
    const VoxelMapInstance *inst = mark->inst;
    uint32_t priority;

    if (x < mark->x0 || y < mark->y0 || x >= mark->x1 || y >= mark->y1)
        return;
    priority = !sMaterialViewSet ||
        (x + inst->originX >= sMaterialView[0] && x + inst->originX < sMaterialView[2]
      && y + inst->originY >= sMaterialView[1] && y + inst->originY < sMaterialView[3]) ? 2u : 1u;
    if (ground >= 0 && ground < (int32_t)VOXEL_METATILE_IDS && mark->used[ground] < priority)
        mark->used[ground] = (uint8_t)priority;
    if (wall >= 0 && wall < (int32_t)VOXEL_METATILE_IDS && mark->used[wall] < priority)
        mark->used[wall] = (uint8_t)priority;
}

#define VOXEL_LAYOUT_USED_CACHE_SLOTS 32u

typedef struct {
    int32_t layoutId;
    uint32_t stamp;
    uint8_t used[VOXEL_METATILE_IDS];
} VoxelLayoutUsedCacheEntry;

static VoxelLayoutUsedCacheEntry sLayoutUsedCache[VOXEL_LAYOUT_USED_CACHE_SLOTS];
static uint32_t sLayoutUsedCacheStamp = 0u;

static const uint8_t *GetCachedLayoutUsedMask(const VoxelMapInstance *inst)
{
    if (inst == NULL || inst->layout == NULL || inst->layoutId <= 0)
        return NULL;

    ++sLayoutUsedCacheStamp;
    uint32_t victim = 0u;
    uint32_t oldest = UINT32_MAX;
    for (uint32_t s = 0u; s < VOXEL_LAYOUT_USED_CACHE_SLOTS; ++s)
    {
        if (sLayoutUsedCache[s].layoutId == inst->layoutId)
        {
            sLayoutUsedCache[s].stamp = sLayoutUsedCacheStamp;
            return sLayoutUsedCache[s].used;
        }
        if (sLayoutUsedCache[s].layoutId == 0)
        {
            victim = s;
            oldest = 0u;
            break;
        }
        if (sLayoutUsedCache[s].stamp < oldest)
        {
            oldest = sLayoutUsedCache[s].stamp;
            victim = s;
        }
    }

    VoxelLayoutUsedCacheEntry *const entry = &sLayoutUsedCache[victim];
    memset(entry->used, 0, sizeof(entry->used));
    entry->layoutId = inst->layoutId;
    entry->stamp = sLayoutUsedCacheStamp;

    MarkEdgeFills(inst, entry->used);
    const uint16_t *const map = Voxel_ResolveMap(inst->layout);
    if (map != NULL)
    {
        const int32_t w = inst->width;
        const int32_t h = inst->height;
        for (int32_t y = 0; y < h; ++y)
        {
            const uint16_t *const row = map + (size_t)y * (size_t)w;
            for (int32_t x = 0; x < w; ++x)
            {
                const uint32_t m = row[x] & MAPGRID_METATILE_ID_MASK;
                if (entry->used[m] < 2u)
                    entry->used[m] = 2u;

                int32_t ground = -1;
                if (VoxelBuildings_CellAt(inst, x + inst->originX, y + inst->originY, &ground, NULL))
                {
                    if (ground >= 0 && ground < (int32_t)VOXEL_METATILE_IDS && entry->used[ground] < 2u)
                        entry->used[ground] = 2u;
                }
            }
        }
    }
    if (inst->indoor)
    {
        for (int32_t y = 0; y < inst->height; ++y)
            for (int32_t x = 0; x < inst->width; ++x)
            {
                int32_t ground = VoxelRoom_Ground(inst, inst->originX + x, inst->originY + y);
                int32_t art = VoxelRoom_AtlasArt(inst, inst->originX + x, inst->originY + y);
                if (ground >= 0 && ground < (int32_t)VOXEL_METATILE_IDS)
                    entry->used[ground] = 2u;
                if (art >= 0 && art < (int32_t)VOXEL_METATILE_IDS)
                    entry->used[art] = 2u;
            }
    }
    {
        CutMark mark = {inst, 0, 0, inst->width, inst->height, entry->used};
        VoxelRelief_ForEachCut(inst, MarkCut, &mark);
    }
    return entry->used;
}

void VoxelWorld_MarkUsedMetatiles(const void *primaryTileset, const void *secondaryTileset,
                                  uint8_t *used)
{
    for (uint32_t i = 0; i < sInstanceCount; ++i)
    {
        const VoxelMapInstance *inst = &sInstances[i];
        const struct MapLayout *layout = inst->layout;

        if (inst->primaryTileset != primaryTileset
         || inst->secondaryTileset != secondaryTileset)
            continue;

        if (!sMaterialViewSet)
        {
            const uint8_t *const cached = GetCachedLayoutUsedMask(inst);
            if (cached != NULL)
            {
                for (uint32_t m = 0u; m < VOXEL_METATILE_IDS; ++m)
                {
                    if (cached[m] > used[m])
                        used[m] = cached[m];
                }
                if (i == 0 && VMap.map != NULL)
                {
                    const int32_t stride = VMap.Xsize;
                    const int32_t maxBy = VMap.Ysize;
                    for (int32_t y = 0; y < inst->height; ++y)
                    {
                        const int32_t by = y + MAP_OFFSET;
                        if (by < 0 || by >= maxBy)
                            continue;
                        const uint16_t *const row = VMap.map + (size_t)by * (size_t)stride;
                        for (int32_t x = 0; x < inst->width; ++x)
                        {
                            const int32_t bx = x + MAP_OFFSET;
                            if (bx < 0 || bx >= stride)
                                continue;
                            const uint32_t m = row[bx] & MAPGRID_METATILE_ID_MASK;
                            if (used[m] < 2u)
                                used[m] = 2u;
                        }
                    }
                }
                continue;
            }
        }

        int32_t x0 = 0, y0 = 0, x1 = inst->width, y1 = inst->height;
        if (sMaterialViewSet)
        {
            if (x0 < sMaterialView[0] - sMaterialMargin - inst->originX)
                x0 = sMaterialView[0] - sMaterialMargin - inst->originX;
            if (y0 < sMaterialView[1] - sMaterialMargin - inst->originY)
                y0 = sMaterialView[1] - sMaterialMargin - inst->originY;
            if (x1 > sMaterialView[2] + sMaterialMargin - inst->originX)
                x1 = sMaterialView[2] + sMaterialMargin - inst->originX;
            if (y1 > sMaterialView[3] + sMaterialMargin - inst->originY)
                y1 = sMaterialView[3] + sMaterialMargin - inst->originY;
        }
        MarkEdgeFills(inst, used);
        const uint16_t *map = i == 0 ? VMap.map : Voxel_ResolveMap(layout);
        if (map != NULL)
            for (int32_t y = y0; y < y1; ++y)
                for (int32_t x = x0; x < x1; ++x)
                {
                    int32_t bx = i == 0 ? x + MAP_OFFSET : x;
                    int32_t by = i == 0 ? y + MAP_OFFSET : y;
                    int32_t stride = i == 0 ? VMap.Xsize : inst->width;
                    if (i == 0 && (bx < 0 || by < 0 || bx >= stride || by >= VMap.Ysize))
                        continue;
                    uint32_t m = map[by * stride + bx] & MAPGRID_METATILE_ID_MASK;
                    uint32_t priority = !sMaterialViewSet ||
                        (x + inst->originX >= sMaterialView[0] && x + inst->originX < sMaterialView[2]
                      && y + inst->originY >= sMaterialView[1] && y + inst->originY < sMaterialView[3]) ? 2u : 1u;
                    if (used[m] < priority) used[m] = (uint8_t)priority;

                    int32_t ground = -1;
                    if (VoxelBuildings_CellAt(inst, x + inst->originX, y + inst->originY, &ground, NULL))
                    {
                        if (ground >= 0 && ground < (int32_t)VOXEL_METATILE_IDS && used[ground] < priority)
                            used[ground] = (uint8_t)priority;
                        if (ground >= (int32_t)VOXEL_METATILE_REAL)
                        {
                            uint32_t own = (uint32_t)VoxelWorld_GetMetatileId(x + inst->originX, y + inst->originY);
                            if (own < (uint32_t)VOXEL_METATILE_REAL && used[own] < priority)
                                used[own] = (uint8_t)priority;
                        }
                    }
                    if (inst->indoor)
                    {
                        int32_t rGround = VoxelRoom_Ground(inst, x + inst->originX, y + inst->originY);
                        int32_t rArt = VoxelRoom_AtlasArt(inst, x + inst->originX, y + inst->originY);
                        if (rGround >= 0 && rGround < (int32_t)VOXEL_METATILE_IDS && used[rGround] < priority)
                            used[rGround] = (uint8_t)priority;
                        if (rArt >= 0 && rArt < (int32_t)VOXEL_METATILE_IDS && used[rArt] < priority)
                            used[rArt] = (uint8_t)priority;
                    }
                }
        {
            CutMark mark = {inst, x0, y0, x1, y1, used};

            VoxelRelief_ForEachCut(inst, MarkCut, &mark);
        }
    }
    for (uint32_t i = 0; i < sInstanceCount; ++i)
    {
        const VoxelMapInstance *inst = &sInstances[i];
        const struct MapLayout *layout = inst->layout;
        const uint16_t *border;
        bool secondary = false;

        if (inst->primaryTileset != primaryTileset || layout == NULL || layout->border == NULL)
            continue;
        border = Port_ResolveAssetPointer(layout->border);
        if (border == NULL)
            continue;
        const uint32_t bw = (layout->borderWidth > 0) ? (uint32_t)layout->borderWidth : 2u;
        const uint32_t bh = (layout->borderHeight > 0) ? (uint32_t)layout->borderHeight : 2u;
        uint32_t borderCount = bw * bh;
        if (borderCount > 16u)
            borderCount = 16u;
        for (uint32_t t = 0; t < borderCount; ++t)
            secondary |= (border[t] & MAPGRID_METATILE_ID_MASK) >= NUM_METATILES_IN_PRIMARY;
        if (secondary && inst->secondaryTileset != secondaryTileset)
            continue;
        for (uint32_t t = 0; t < borderCount; ++t)
            used[border[t] & MAPGRID_METATILE_ID_MASK] = 2;
    }
    if (primaryTileset == &gTileset_General)
    {
        for (int32_t m = 0; m < NUM_METATILES_TOTAL; ++m)
        {
            int32_t ground = VoxelTree_GroundMetatile(secondaryTileset, m);
            if (ground >= 0 && ground < (int32_t)VOXEL_METATILE_IDS && used[m] > used[ground])
                used[ground] = used[m];
        }
    }
}

const uint16_t *VoxelWorld_LayoutDrawing(int32_t layoutId, int32_t *width, int32_t *height)
{
    const struct MapLayout *layout;

    if (layoutId <= 0 || layoutId > 0xFFFF)
        return NULL;
    layout = Port_GetMapLayoutById((u16)layoutId);
    if (layout == NULL || layout->width <= 0 || layout->height <= 0)
        return NULL;
    *width = layout->width;
    *height = layout->height;
    return Voxel_ResolveMap(layout);
}

uint32_t VoxelWorld_LiveDigest(void)
{
    const u16 *map = VMap.map;
    uint32_t hash = 2166136261u;
    uint32_t area, i;

    if (map == NULL || VMap.Xsize <= 0 || VMap.Ysize <= 0)
        return 0;
    area = (uint32_t)VMap.Xsize * (uint32_t)VMap.Ysize;
    hash = (hash ^ area) * 16777619u;
    hash = (hash ^ (uint32_t)(uintptr_t)map) * 16777619u;
    for (i = 0; i + 1 < area; i += 2)
        hash = (hash ^ ((uint32_t)map[i] | ((uint32_t)map[i + 1] << 16))) * 16777619u;
    if (i < area)
        hash = (hash ^ map[i]) * 16777619u;
    return hash;
}

uint32_t VoxelWorld_BlockHash(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t hash = 2166136261u;
    uint16_t row[80];

    if (x1 > x0 && x1 - x0 <= (int32_t)(sizeof(row) / sizeof(row[0])))
    {
        for (int32_t y = y0; y < y1; ++y)
        {
            memset(row, 0, (size_t)(x1 - x0) * sizeof(row[0]));
            for (uint32_t i = sInstanceCount; i-- > 0;)
            {
                const VoxelMapInstance *inst = &sInstances[i];
                const uint16_t *map;
                int32_t left = x0 > inst->originX ? x0 : inst->originX;
                int32_t right = x1 < inst->originX + inst->width ? x1 : inst->originX + inst->width;
                if (left >= right || y < inst->originY || y >= inst->originY + inst->height)
                    continue;
                memset(row + left - x0, 0, (size_t)(right - left) * sizeof(row[0]));
                if (i == 0)
                {
                    int32_t by = y - inst->originY + MAP_OFFSET;
                    if (VMap.map == NULL || by < 0 || by >= VMap.Ysize)
                        continue;
                    if (left < inst->originX - MAP_OFFSET) left = inst->originX - MAP_OFFSET;
                    if (right > inst->originX + VMap.Xsize - MAP_OFFSET)
                        right = inst->originX + VMap.Xsize - MAP_OFFSET;
                    if (left >= right) continue;
                    map = VMap.map + by * VMap.Xsize + left - inst->originX + MAP_OFFSET;
                }
                else
                {
                    map = Voxel_ResolveMap(inst->layout);
                    if (map == NULL) continue;
                    map += (y - inst->originY) * inst->width + left - inst->originX;
                }
                memcpy(row + left - x0, map, (size_t)(right - left) * sizeof(row[0]));
            }
            for (int32_t x = 0; x < x1 - x0; ++x)
                hash = (hash ^ row[x]) * 16777619u;
        }
        return hash;
    }

    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x)
        {
            hash ^= GetRawBlock(x, y, NULL);
            hash *= 16777619u;
        }
    return hash;
}

int32_t VoxelWorld_BorderMetatile(int32_t worldX, int32_t worldY)
{
    const uint16_t *border;
    const void *source, *resolved;
    u8 index;
    int32_t fill;

    if (sInstanceCount == 0 || sInstances[0].layout == NULL)
        return -1;
    fill = EdgeFillAt(worldX, worldY);
    if (fill >= 0)
        return fill;
    source = MapGridGetCameraBorderAt(worldX - sInstances[0].originX,
                                      worldY - sInstances[0].originY, &index);
    if (source == NULL)
        return -1;
    if (!MemoGet(&sBorderMemo, source, &resolved))
    {
        resolved = Port_ResolveAssetPointer(source);
        MemoPut(&sBorderMemo, source, resolved);
    }
    border = resolved;
    if (border == NULL)
        return -1;
    return (int32_t)(border[index] & MAPGRID_METATILE_ID_MASK);
}

bool VoxelWorld_BorderIsWater(int32_t worldX, int32_t worldY)
{
    int32_t metatileId = VoxelWorld_BorderMetatile(worldX, worldY);

    return metatileId >= 0 && sInstanceCount != 0
        && BehaviorShape(UNPACK_BEHAVIOR(GetMetatileAttribute(&sInstances[0], metatileId)))
           == VOXEL_SHAPE_WATER;
}

const u16 *MapGridGetCameraBorderAt(int32_t x, int32_t y, u8 *index)
{
    const struct MapLayout *const layout = gMapHeader.mapLayout;

    if (layout == NULL || layout->border == NULL)
        return NULL;
    if (index != NULL)
    {
        const int32_t bw = (layout->borderWidth > 0) ? (int32_t)layout->borderWidth : 2;
        const int32_t bh = (layout->borderHeight > 0) ? (int32_t)layout->borderHeight : 2;
        int32_t xprime = (x % bw + bw) % bw;
        int32_t yprime = (y % bh + bh) % bh;
        *index = (u8)(xprime + yprime * bw);
    }
    return layout->border;
}

u32 GetMapClosedEdges(void)
{
    return 0;
}
