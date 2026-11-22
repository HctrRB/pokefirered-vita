/*
 * Player, NPC, field effect, 1x1/1x2 door, follower, and visible wild
 * billboards/shadows/reflections for the Kanto 3D overworld.
 *
 * Threads: VoxelEntities_Capture runs on Core 1 and reads live game globals into
 * VoxelEntitySnapshot; everything else runs on Core 0 from GameFrame_Get() alone.
 *
 * Origin: derived from pokeemerald-vita-3ds / pokeemerald-multiplatform /
 * Pokemon-FireRed-3DS (MIT); see NOTICE.md.
 */

/* Before global.h, which redefines abs() as a macro over stdlib's prototype. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "constants/event_object_movement.h"
#include "constants/field_effects.h"
#include "constants/map_types.h"
#include "constants/species.h"
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "gba/io_reg.h"
#include "main.h"
#include "palette.h"
#include "pokemon.h"
#include "quest_log.h"
#include "sprite.h"
#include "port_platform.h"

#include "3ds_log.h"
#include "3ds_video.h"
#include "../vita/vita_perf.h"
#include "platform/game_frame.h"
#include "platform/voxel_snapshot.h"
#include "voxel_building.h"
#include "voxel_entities.h"
#include "voxel_follower.h"
#include "voxel_grade.h"
#include "voxel_lighting.h"
#include "voxel_relief.h"
#include "voxel_room.h"
#include "voxel_sprite_slots.h"
#include "voxel_tree.h"
#include "voxel_world.h"

#define VOXEL_PIXELS_PER_TILE  16.0f
#define VOXEL_SPRITE_MAX_BYTES (VOXEL_SPRITE_SLOT_DIM * VOXEL_SPRITE_SLOT_DIM)
#define VOXEL_CARD_CLEAR       (1.0f / VOXEL_PIXELS_PER_TILE)
#define VOXEL_MODEL_PUSH_MAX   0.5f
#define VOXEL_MODEL_PUSH_H     0.25f
#define VOXEL_DECAL_LIFT       0.025f
#define VOXEL_DOOR_DEPTH       0.03f
#define VOXEL_DOOR_DUSK        0.5f
#define VOXEL_DOOR_DARK        0.35f
#define VOXEL_DOOR_ROWS        32u
#define VOXEL_DOORS            VOXEL_SNAPSHOT_DOORS
#define VOXEL_CAST_SHADOW_ALPHA 0.36f
#define VOXEL_CAST_SHADOW_LIFT  0.03f
#define VOXEL_CAST_SHADOW_LIT   0.70f
#define VOXEL_CLOUD_PERIOD_X   32.0f
#define VOXEL_CLOUD_PERIOD_Z   16.0f
#define VOXEL_EFFECTS_MAX      VOXEL_SPRITE_SLOTS
#define VOXEL_EFFECT_DEPTH     0.04f
#define VOXEL_SURF_SEAT        0.04f
#define VOXEL_SURF_BODY        0.12f
/* FireRed's voxel_room.c builds the counter (8 px) + healing machine (10 px) = 18 px (1.125 tiles). */
#define VOXEL_HEAL_BALL_LIFT   1.16f
#define VOXEL_EFFECT_RANGE     (24.0f + 2.0f)

/* Core 1 game queries */
bool8 PortDoor_Get(s16 *x, s16 *y, u8 *width, u8 *height, const u8 **tiles, const u8 **palettes);
bool8 PortDoor_GetTwin(s16 *x, s16 *y);
bool8 VisibleWild_View(u32 i, u16 *species, s16 *x, s16 *y, s8 *dx, s8 *dy, u8 *progress, u8 *spriteId);
bool8 VisibleWild_IsSprite(u8 spriteId);
bool8 VisibleWild_SpriteTile(u8 spriteId, s16 *x16, s16 *y16);
bool8 CtrPlayer_Follower(void);

/* Step durations matching sStepTimes[] in src/event_object_movement.c. */
static const int32_t sStepFrames[] = { 16, 8, 6, 4, 2 };

typedef struct
{
    bool valid;
    u16 tileNum, paletteNum;
    u8 extPalette;
    u8 shape, size;
    bool flipX, flipY, color256;
    int32_t width, height;
    int32_t atlasWidth;
    u32 sourceBytes;
    u8 source[VOXEL_SPRITE_MAX_BYTES];
    u16 palette[256];
    u32 paletteBytes;
    int32_t drawnWidth, drawnHeight;
    int32_t footPad;
    const struct SpriteFrameImage *footPadImages;
    bool footPadExact;
} VoxelSpriteSlot;

static VoxelSpriteSlot sSlots[VOXEL_SPRITE_SLOTS];
static u8 sSlotEffect[VOXEL_SPRITE_SLOTS];
static uint64_t sDirtySlots;
static int32_t sPlayerVertexFirst = -1;
static bool sDoorStale = true;
static float sLay;

static bool sLightingAllowed = true;
static float sLastShade[OBJECT_EVENTS_COUNT];
static bool sLastShadeKnown[OBJECT_EVENTS_COUNT];

/* ── OAM & movement helpers ──────────────────────────────────────────────── */

static void GetSpriteDimensions(u8 shape, u8 size, int32_t *w, int32_t *h)
{
    static const u8 square[4][2] = { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } };
    static const u8 wide[4][2]   = { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } };
    static const u8 tall[4][2]   = { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } };
    const u8 (*table)[2];

    if (shape == ST_OAM_SQUARE) table = square;
    else if (shape == ST_OAM_H_RECTANGLE) table = wide;
    else if (shape == ST_OAM_V_RECTANGLE) table = tall;
    else { *w = 16; *h = 32; return; }
    *w = table[size & 3u][0];
    *h = table[size & 3u][1];
}

static bool IsJumpAction(u8 action)
{
    return (action >= MOVEMENT_ACTION_JUMP_2_DOWN && action <= MOVEMENT_ACTION_JUMP_2_RIGHT)
        || (action >= MOVEMENT_ACTION_JUMP_SPECIAL_DOWN && action <= MOVEMENT_ACTION_JUMP_SPECIAL_RIGHT)
        || (action >= MOVEMENT_ACTION_JUMP_DOWN && action <= MOVEMENT_ACTION_JUMP_IN_PLACE_RIGHT_LEFT);
}

static bool IsJumping(const struct ObjectEvent *obj, const struct Sprite *sprite)
{
    return (obj->singleMovementActive || obj->heldMovementActive)
        && IsJumpAction(obj->movementActionId) && sprite->data[2] == 1;
}

static float GetJumpProgress(const struct ObjectEvent *obj, const struct Sprite *sprite)
{
    float pixels = (float)(u16)sprite->data[6];

    if (obj->movementActionId >= MOVEMENT_ACTION_JUMP_SPECIAL_DOWN
     && obj->movementActionId <= MOVEMENT_ACTION_JUMP_SPECIAL_RIGHT)
        pixels *= 0.5f;
    if (pixels >= VOXEL_PIXELS_PER_TILE)
        pixels -= VOXEL_PIXELS_PER_TILE;
    return pixels < VOXEL_PIXELS_PER_TILE ? pixels / VOXEL_PIXELS_PER_TILE : 1.0f;
}

static float GetMovementProgress(const struct ObjectEvent *obj, const struct Sprite *sprite)
{
    if (!obj->singleMovementActive && !obj->heldMovementActive)
        return 1.0f;
    if (IsJumpAction(obj->movementActionId))
        return IsJumping(obj, sprite) ? GetJumpProgress(obj, sprite) : 1.0f;

    const int32_t speed = (int32_t)(u16)sprite->data[4];
    const int32_t timer = (int32_t)(u16)sprite->data[5];
    if (speed < 0 || speed >= (int32_t)ARRAY_COUNT(sStepFrames))
        return 1.0f;
    const int32_t stepLen = sStepFrames[speed];
    if (stepLen <= 0)
        return 1.0f;

    float t = (float)timer / (float)stepLen;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

static void GetObjectWorldPos(const VoxelEntitySnapshot *snap, const struct ObjectEvent *obj,
                              const struct Sprite *sprite, float *outX, float *outZ)
{
    if (snap != NULL && obj->isPlayer && snap->playerFreeActive)
    {
        *outX = (float)(obj->currentCoords.x - MAP_OFFSET) + (float)snap->playerFreeDx / VOXEL_PIXELS_PER_TILE;
        *outZ = (float)(obj->currentCoords.y - MAP_OFFSET) + (float)snap->playerFreeDy / VOXEL_PIXELS_PER_TILE;
        return;
    }
    const float prevX = (float)(obj->previousCoords.x - MAP_OFFSET);
    const float prevZ = (float)(obj->previousCoords.y - MAP_OFFSET);
    const float curX = (float)(obj->currentCoords.x - MAP_OFFSET);
    const float curZ = (float)(obj->currentCoords.y - MAP_OFFSET);
    const float t = GetMovementProgress(obj, sprite);

    *outX = prevX + (curX - prevX) * t;
    *outZ = prevZ + (curZ - prevZ) * t;
}

static float ObjectDeck(const struct ObjectEvent *obj, float worldX, float worldZ)
{
    const float cx = worldX + 0.5f;
    const float cz = worldZ + 0.5f;
    float lift = 0.0f;
    const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt((int32_t)floorf(cx), (int32_t)floorf(cz));

    return VoxelBuildings_DeckAt(inst, cx, cz, obj->currentElevation, &lift) ? lift : 0.0f;
}

static inline void ApplySnapshotMapOffset(const VoxelEntitySnapshot *const snap,
                                          float *const worldX, float *const worldZ)
{
    if (snap == NULL || snap->mapGroup < 0 || snap->mapNum < 0)
        return;
    int32_t offX = 0, offY = 0;
    if (VoxelWorld_GetInstanceOffset(snap->mapGroup, snap->mapNum, &offX, &offY))
    {
        *worldX += (float)offX;
        *worldZ += (float)offY;
    }
}

static inline const struct Sprite *SnapSprite(const VoxelEntitySnapshot *snap, const struct ObjectEvent *obj)
{
    return obj->spriteId < MAX_SPRITES ? &snap->sprites[obj->spriteId] : NULL;
}

static inline const struct ObjectEventGraphicsInfo *SnapGraphicsInfo(const VoxelEntitySnapshot *snap,
                                                                     const struct ObjectEvent *obj)
{
    return &snap->graphicsInfo[obj - snap->objectEvents];
}

static inline uint32_t SnapSpriteId(const VoxelEntitySnapshot *snap, const struct Sprite *sprite)
{
    return (uint32_t)(sprite - snap->sprites);
}

/* ── Core 1: frame entity capture ────────────────────────────────────────── */

static u16 CaptureLeadNationalSpecies(void) __attribute__((unused));
static u16 CaptureLeadNationalSpecies(void)
{
    for (int32_t i = 0; i < PARTY_SIZE; ++i)
    {
        const u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES_OR_EGG, NULL);
        if (species == SPECIES_NONE || species == SPECIES_EGG)
            continue;
        if (GetMonData(&gPlayerParty[i], MON_DATA_HP, NULL) == 0u)
            continue;
        return SpeciesToNationalPokedexNum(species);
    }
    return 0u;
}

void VoxelEntities_Capture(VoxelEntitySnapshot *snap, const VoxelEntitySnapshot *previous, bool8 keepPrevious)
{
    if (snap == NULL)
        return;
    if (keepPrevious && previous != NULL)
    {
        if (snap != previous)
            memcpy(snap, previous, sizeof(*snap));
        return;
    }

    memset(snap, 0, sizeof(*snap));
    memcpy(snap->objectEvents, gObjectEvents, sizeof(snap->objectEvents));
    memcpy(snap->sprites, gSprites, sizeof(snap->sprites));
    memcpy(snap->oamMatrices, gOamMatrices, sizeof(snap->oamMatrices));
    snap->spriteCoordOffsetX = gSpriteCoordOffsetX;
    snap->spriteCoordOffsetY = gSpriteCoordOffsetY;

    for (uint32_t id = 0u; id < MAX_SPRITES; ++id)
    {
        const struct Sprite *const sprite = &gSprites[id];
        snap->extPalettes[id] = sprite->inUse ? GetSpriteExtPalette(sprite) : 0xFFu;
        snap->tileTags[id] = (sprite->inUse && (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
                           ? GetSpriteTileTagByTileStart(sprite->sheetTileStart) : 0xFFFFu;
        if (sprite->inUse && VisibleWild_IsSprite((u8)id)
         && VisibleWild_SpriteTile((u8)id, &snap->wildX16[id], &snap->wildY16[id]))
            snap->visibleWild |= (1ULL << id);
        if (sprite->inUse && CtrSprite_IsVoxelWeather(sprite))
            snap->weatherSprites |= (1ULL << id);
        if (sprite->inUse && CtrSprite_IsVoxelCloud(sprite))
            snap->cloudSprites |= (1ULL << id);
        if (sprite->inUse && FieldEffect_IsHealMachineBall(sprite))
        {
            if (snap->healSprites == 0ULL)
                memcpy(snap->healPalette, &gPlttBufferFaded[OBJ_PLTT_ID(sprite->oam.paletteNum)], sizeof(snap->healPalette));
            snap->healSprites |= (1ULL << id);
        }
    }

    for (uint32_t i = 0u; i < OBJECT_EVENTS_COUNT; ++i)
    {
        if (gObjectEvents[i].active)
        {
            const struct ObjectEventGraphicsInfo *const info = GetObjectEventGraphicsInfo(gObjectEvents[i].graphicsId);
            if (info != NULL)
                snap->graphicsInfo[i] = *info;
        }
    }

    snap->playerObjectEventId = (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
                              ? gPlayerAvatar.objectEventId : 0u;

    {
        s16 x = 0, y = 0;
        u8 width = 0, height = 0;
        const u8 *tiles = NULL, *palettes = NULL;

        if (gSaveBlock1Ptr != NULL && PortDoor_Get(&x, &y, &width, &height, &tiles, &palettes))
        {
            snap->doorCount = 1u;
            snap->doorWidth = width;
            snap->doorHeight = height;
            snap->doorTiles = tiles;
            snap->doorPalettes = palettes;
            snap->doorX[0] = x;
            snap->doorY[0] = y;
            if (PortDoor_GetTwin(&x, &y))
            {
                snap->doorCount = 2u;
                snap->doorX[1] = x;
                snap->doorY[1] = y;
            }
        }
    }

    s16 freeDx = 0, freeDy = 0;
    snap->playerFreeActive = PlayerGetFreeOffset(&freeDx, &freeDy);
    snap->playerFreeDx = freeDx;
    snap->playerFreeDy = freeDy;

    const struct ObjectEvent *const pObj = &gObjectEvents[snap->playerObjectEventId];
    float px = 0.0f, pz = 0.0f;
    if (pObj->active && pObj->spriteId < MAX_SPRITES)
    {
        if (pObj->isPlayer && snap->playerFreeActive)
        {
            px = (float)(pObj->currentCoords.x - MAP_OFFSET) + (float)freeDx / VOXEL_PIXELS_PER_TILE;
            pz = (float)(pObj->currentCoords.y - MAP_OFFSET) + (float)freeDy / VOXEL_PIXELS_PER_TILE;
        }
        else
        {
            GetObjectWorldPos(NULL, pObj, &gSprites[pObj->spriteId], &px, &pz);
        }
    }
    else
    {
        VoxelWorld_GetPlayerWorldCoordsLive(&px, &pz);
    }
    snap->playerWorldX = px;
    snap->playerWorldZ = pz;
    snap->playerFacing = pObj->facingDirection;
    snap->mapGroup = (gSaveBlock1Ptr != NULL) ? (int16_t)gSaveBlock1Ptr->location.mapGroup : (int16_t)-1;
    snap->mapNum   = (gSaveBlock1Ptr != NULL) ? (int16_t)gSaveBlock1Ptr->location.mapNum   : (int16_t)-1;
    snap->mapType  = gMapHeader.mapType;

    /* Follower Pokemon is now a native HGSS ObjectEvent (src/follower_mon.c) rendered in 2D and 3D
     * through the standard object-event billboard pipeline with shadows, reflections, and decks. */
    snap->followerEnabled = FALSE;
    snap->followerSpecies = 0u;
    snap->wildSpritesAllowed = FALSE;
    if (snap->wildSpritesAllowed)
    {
        for (uint32_t i = 0u; i < VOXEL_SNAPSHOT_WILDS; ++i)
        {
            u16 species = 0u;
            s16 wx = 0, wy = 0;
            s8 wdx = 0, wdy = 0;
            u8 wprog = 0u, wsid = 0u;
            if (VisibleWild_View(i, &species, &wx, &wy, &wdx, &wdy, &wprog, &wsid))
            {
                snap->wilds[i].active = TRUE;
                snap->wilds[i].nationalSpecies = SpeciesToNationalPokedexNum(species);
                snap->wilds[i].x = wx;
                snap->wilds[i].y = wy;
                snap->wilds[i].dx = wdx;
                snap->wilds[i].dy = wdy;
                snap->wilds[i].progress = wprog;
                snap->wilds[i].spriteId = wsid;
            }
        }
    }
    snap->frameNum = (previous != NULL) ? (previous->frameNum + 1u) : 1u;
}

/* ── Core 0: accessors ───────────────────────────────────────────────────── */

const VoxelEntitySnapshot *VoxelEntities_GetVisibleSnapshot(void)
{
    return &GameFrame_Get()->entities;
}

int32_t VoxelEntities_PlayerVertexFirst(void)
{
    return sPlayerVertexFirst;
}

void VoxelEntities_Reset(void)
{
    sDoorStale = true;
    memset(sLastShadeKnown, 0, sizeof(sLastShadeKnown));
    for (uint32_t i = 0u; i < VOXEL_SPRITE_SLOTS; ++i)
    {
        sSlots[i].valid = false;
        sSlotEffect[i] = 0u;
    }
    VoxelFollower_Reset();
}

void VoxelEntities_AllowLighting(bool allowed)
{
    sLightingAllowed = allowed;
}

uint64_t VoxelEntities_TakeDirtySlots(void)
{
    const uint64_t dirty = sDirtySlots;
    sDirtySlots = 0ULL;
    return dirty;
}

float VoxelEntities_PlayerDeck(void)
{
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    const struct ObjectEvent *const obj = &snap->objectEvents[snap->playerObjectEventId];
    float x = 0.0f, z = 0.0f;

    if (!obj->active || obj->spriteId >= MAX_SPRITES)
        return 0.0f;
    GetObjectWorldPos(snap, obj, SnapSprite(snap, obj), &x, &z);
    ApplySnapshotMapOffset(snap, &x, &z);
    return ObjectDeck(obj, x, z);
}

float VoxelEntities_PlayerZShift(void)
{
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    const struct ObjectEvent *const obj = &snap->objectEvents[snap->playerObjectEventId];
    if (!obj->active || obj->spriteId >= MAX_SPRITES)
        return 0.0f;

    float x = snap->playerWorldX;
    float z = snap->playerWorldZ;
    ApplySnapshotMapOffset(snap, &x, &z);
    return VoxelRelief_ShiftAt(x + 0.5f, z + 0.5f) + ObjectDeck(obj, x, z);
}

float VoxelEntities_PlayerGround(void)
{
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    const struct ObjectEvent *const obj = &snap->objectEvents[snap->playerObjectEventId];
    float x = 0.0f, z = 0.0f;

    if (!obj->active || obj->spriteId >= MAX_SPRITES)
    {
        VoxelWorld_GetPlayerWorldCoords(&x, &z);
    }
    else
    {
        GetObjectWorldPos(snap, obj, SnapSprite(snap, obj), &x, &z);
        ApplySnapshotMapOffset(snap, &x, &z);
    }
    return VoxelRelief_LiftAt(x + 0.5f, z + 0.5f) + VoxelEntities_PlayerDeck();
}

void VoxelEntities_GetPlayerWorldPos(float *worldX, float *worldZ)
{
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    float px = (snap != NULL) ? snap->playerWorldX : 0.0f;
    float pz = (snap != NULL) ? snap->playerWorldZ : 0.0f;

    ApplySnapshotMapOffset(snap, &px, &pz);
    if (worldX != NULL) *worldX = px;
    if (worldZ != NULL) *worldZ = pz;
}

void VoxelEntities_GetPlayerCameraTarget(float *worldX, float *worldZ)
{
    float px = 0.0f, pz = 0.0f;

    VoxelEntities_GetPlayerWorldPos(&px, &pz);
    pz += VoxelEntities_PlayerZShift();
    if (worldX != NULL) *worldX = px;
    if (worldZ != NULL) *worldZ = pz;
}

/* ── Core 0: sprite atlas decoding ───────────────────────────────────────── */

static u32 GatherSource(const struct Sprite *sprite, int32_t w, int32_t h, bool color256, u8 *dest)
{
    const u8 *const vram = VRAM_ + 0x10000;
    const bool mapping1d = (REG_DISPCNT & DISPCNT_OBJ_1D_MAP) != 0;
    const uint32_t bytesPerTile = color256 ? 64u : 32u;
    u32 written = 0u;

    for (int32_t ty = 0; ty < h / 8; ++ty)
    {
        for (int32_t tx = 0; tx < w / 8; ++tx)
        {
            const uint32_t tile = CtrVideo_ObjTile(sprite->oam.tileNum, (uint32_t)tx, (uint32_t)ty,
                                                   (uint32_t)w, color256, mapping1d);
            uint32_t offset = tile * 32u;
            if (offset + bytesPerTile > 0x8000u)
                offset = 0u;
            memcpy(dest + written, vram + offset, bytesPerTile);
            written += bytesPerTile;
        }
    }
    return written;
}

static uint16_t *AtlasBlock(uint16_t *atlas, uint32_t bx, uint32_t by)
{
    return atlas + (by * (VOXEL_SPRITE_ATLAS_W / 8u) + bx) * 64u;
}

static void DecodeSlot(VoxelSpriteSlot *slot, uint32_t index, uint16_t *atlas)
{
    static uint8_t morton[64];
    static bool mortonReady;
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t bytesPerTile = slot->color256 ? 64u : 32u;
    const uint32_t colours = slot->color256 ? 256u : 16u;
    const int32_t tilesX = slot->width / 8, tilesY = slot->height / 8;
    const int32_t clearW = slot->drawnWidth > slot->atlasWidth ? slot->drawnWidth : slot->atlasWidth;
    const int32_t clearH = slot->drawnHeight > slot->height ? slot->drawnHeight : slot->height;
    uint16_t graded[256];

    if (!mortonReady)
    {
        for (uint32_t i = 0u; i < 64u; ++i)
            morton[i] = (uint8_t)CtrVideo_Texel(i & 7u, i / 8u, 8u);
        mortonReady = true;
    }
    for (int32_t by = 0; by < (clearH + 7) / 8; ++by)
        for (int32_t bx = 0; bx < (clearW + 7) / 8; ++bx)
            memset(AtlasBlock(atlas, baseX / 8u + (uint32_t)bx, baseY / 8u + (uint32_t)by), 0,
                   64u * sizeof(uint16_t));
    slot->drawnWidth = slot->atlasWidth;
    slot->drawnHeight = slot->height;
    for (uint32_t c = 1u; c < colours; ++c)
        graded[c] = VoxelGrade_RGBA5551(slot->palette[c]);

    if (slot->atlasWidth != slot->width)
    {
        for (int32_t y = 0; y < slot->height; ++y)
        {
            const uint32_t sy = (uint32_t)(slot->flipY ? slot->height - 1 - y : y);
            for (int32_t x = 0; x < slot->atlasWidth; ++x)
            {
                const uint32_t sx = VoxelSlots_SourceColumn((uint32_t)x, (uint32_t)slot->atlasWidth,
                                                            (uint32_t)slot->width, slot->flipX);
                const uint32_t colorIdx = VoxelSlots_TilePixel(slot->source, (uint32_t)slot->width,
                                                               sx, sy, slot->color256);
                if (colorIdx != 0u)
                {
                    atlas[CtrVideo_Texel(baseX + (uint32_t)x, baseY + (uint32_t)y,
                                         VOXEL_SPRITE_ATLAS_W)] = graded[colorIdx];
                }
            }
        }
        return;
    }

    for (int32_t ty = 0; ty < tilesY; ++ty)
    {
        for (int32_t tx = 0; tx < tilesX; ++tx)
        {
            const u8 *const tile = slot->source + (uint32_t)(ty * tilesX + tx) * bytesPerTile;
            const uint32_t ox = (uint32_t)(slot->flipX ? tilesX - 1 - tx : tx);
            const uint32_t oy = (uint32_t)(slot->flipY ? tilesY - 1 - ty : ty);
            uint16_t *const block = AtlasBlock(atlas, baseX / 8u + ox, baseY / 8u + oy);
            const uint32_t fx = slot->flipX ? 7u : 0u;
            const uint32_t fy = slot->flipY ? 7u : 0u;

            for (uint32_t py = 0u; py < 8u; ++py)
            {
                const uint8_t *const row = morton + ((py ^ fy) * 8u);
                for (uint32_t px = 0u; px < 8u; ++px)
                {
                    const uint32_t index8 = py * 8u + px;
                    const uint32_t colorIdx = slot->color256
                        ? tile[index8]
                        : ((tile[index8 / 2u] >> ((px & 1u) * 4u)) & 0xFu);
                    if (colorIdx != 0u)
                        block[row[px ^ fx]] = graded[colorIdx];
                }
            }
        }
    }
}

static int32_t FootPadOf(const u8 *tiles, int32_t width, int32_t height)
{
    const int32_t tilesX = width / 8;

    for (int32_t y = height - 1; y >= 0; --y)
    {
        for (int32_t tx = 0; tx < tilesX; ++tx)
        {
            const u8 *const row = tiles + (u32)((y / 8) * tilesX + tx) * TILE_SIZE_4BPP + (y % 8) * 4;
            if (row[0] | row[1] | row[2] | row[3])
                return height - 1 - y;
        }
    }
    return 0;
}

static int32_t StandingFootPad(const struct ObjectEventGraphicsInfo *info,
                               const VoxelSpriteSlot *slot, bool *exact)
{
    const struct SpriteFrameImage *const frame = &info->images[0];
    const int32_t tilesX = info->width / 8, tilesY = info->height / 8;
    const u32 size = frame->size;
#ifdef PORT_BRIDGE
    const u8 *const tiles = (frame->data != NULL && Port_IsLinkedData(frame->data))
                          ? ((const u8 *)frame->data + frame->offset)
                          : (const u8 *)frame->data;
#else
    const u8 *const tiles = (const u8 *)frame->data;
#endif

    *exact = (tiles != NULL);
    if (tilesX <= 0 || tilesY <= 0)
        return 0;
    if (tiles == NULL)
    {
        if (slot->color256 || slot->width != info->width || slot->height != info->height
         || slot->sourceBytes < (u32)(tilesX * tilesY) * TILE_SIZE_4BPP)
            return 0;
        return FootPadOf(slot->source, info->width, info->height);
    }
    if (size < (u32)(tilesX * tilesY) * TILE_SIZE_4BPP)
        return 0;
    return FootPadOf(tiles, info->width, info->height);
}

static void PictureSize(const struct Sprite *sprite, const struct ObjectEvent *obj, int32_t *w, int32_t *h)
{
    GetSpriteDimensions(sprite->oam.shape, sprite->oam.size, w, h);
    if (obj == NULL || sprite->subspriteTables == NULL)
        return;
    const struct ObjectEventGraphicsInfo *const info = SnapGraphicsInfo(VoxelEntities_GetVisibleSnapshot(), obj);
    if ((info->width > *w || info->height > *h)
     && VoxelSlots_Fits((uint32_t)info->width, (uint32_t)info->height, sprite->oam.bpp != 0,
                        VOXEL_SPRITE_SLOT_DIM, VOXEL_SPRITE_MAX_BYTES))
    {
        *w = info->width;
        *h = info->height;
    }
}

static bool RefreshSlot(uint32_t index, const struct Sprite *sprite, const struct ObjectEvent *obj,
                        u8 extPalette, const u16 *overridePalette, uint16_t *atlas)
{
    VoxelSpriteSlot *const slot = &sSlots[index];
    static u8 source[VOXEL_SPRITE_MAX_BYTES];
    static u16 palette[256];
    int32_t w = 0, h = 0;
    const bool color256 = sprite->oam.bpp != 0;
    const bool flipX = sprite->oam.affineMode == ST_OAM_AFFINE_OFF && (sprite->oam.matrixNum & 0x08u) != 0;
    const bool flipY = sprite->oam.affineMode == ST_OAM_AFFINE_OFF && (sprite->oam.matrixNum & 0x10u) != 0;

    if (color256)
        extPalette = 0xFFu;

    PictureSize(sprite, obj, &w, &h);
    if (!VoxelSlots_Fits((uint32_t)w, (uint32_t)h, color256, VOXEL_SPRITE_SLOT_DIM,
                         VOXEL_SPRITE_MAX_BYTES))
        return false;

    const u32 sourceBytes = GatherSource(sprite, w, h, color256, source);
    const u32 paletteBytes = color256 ? 256u * 2u : 16u * 2u;
    const GameFrameOverlay *const ov = &GameFrame_Get()->overlay;
    if (overridePalette != NULL && !color256)
        memcpy(palette, overridePalette, paletteBytes);
    else if (extPalette != 0xFFu && extPalette < PLTT_EXT_COUNT)
        memcpy(palette, &ov->objPaletteExtUnfaded[extPalette * 16u], paletteBytes);
    else if (extPalette != 0xFFu)
        return false;
    else
        memcpy(palette, &ov->objPaletteUnfaded[color256 ? 0u : sprite->oam.paletteNum * 16u], paletteBytes);

    const bool changed = !slot->valid
                      || slot->width != w || slot->height != h
                      || slot->color256 != color256
                      || slot->flipX != flipX || slot->flipY != flipY
                      || slot->tileNum != sprite->oam.tileNum
                      || slot->paletteNum != sprite->oam.paletteNum
                      || slot->extPalette != extPalette
                      || slot->sourceBytes != sourceBytes
                      || memcmp(slot->source, source, sourceBytes) != 0
                      || memcmp(slot->palette, palette, paletteBytes) != 0;
    if (!changed)
        return false;

    slot->valid = true;
    slot->width = w;
    slot->height = h;
    slot->atlasWidth = (int32_t)VoxelSlots_AtlasWidth((uint32_t)w, VOXEL_SPRITE_SLOT_DIM);
    slot->color256 = color256;
    slot->flipX = flipX;
    slot->flipY = flipY;
    slot->tileNum = sprite->oam.tileNum;
    slot->paletteNum = sprite->oam.paletteNum;
    slot->extPalette = extPalette;
    slot->shape = sprite->oam.shape;
    slot->size = sprite->oam.size;
    slot->sourceBytes = sourceBytes;
    slot->paletteBytes = paletteBytes;
    memcpy(slot->source, source, sourceBytes);
    memcpy(slot->palette, palette, paletteBytes);
    VitaPerf_SubBegin(VITA_PERF_T_ENT_DECODE);
    DecodeSlot(slot, index, atlas);
    VitaPerf_SubEnd(VITA_PERF_T_ENT_DECODE);
    return true;
}

/* ── Core 0: billboards, decals, shadows, reflections ────────────────────── */

static int32_t VisibleRows(const VoxelSpriteSlot *slot)
{
    const int32_t pad = slot->footPad < slot->height ? slot->footPad : slot->height - 1;
    return slot->height - pad;
}

static float CardPush(float cx, float cz, float halfW, float height)
{
    const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt((int32_t)floorf(cx), (int32_t)floorf(cz));
    float worst = -1.0f;
    const int32_t x0 = (int32_t)floorf(cx - halfW), x1 = (int32_t)floorf(cx + halfW);
    const int32_t y0 = (int32_t)floorf(cz - height), y1 = (int32_t)floorf(cz);
    bool relief = false;

    for (int32_t y = y0; y <= y1 && !relief; ++y)
        for (int32_t x = x0; x <= x1 && !relief; ++x)
            relief = VoxelRelief_IsSlope(VoxelRelief_Cell(VoxelWorld_GetInstanceAt(x, y), x, y));
    if (inst == NULL || !relief)
        return 0.0f;
    const float feet = VoxelRelief_LiftAt(cx, cz);
    for (int32_t i = -1; i <= 1; ++i)
    {
        const float x = cx + (float)i * halfW * 0.75f;
        for (float dv = VOXEL_CARD_CLEAR; dv <= height; dv += 2.0f * VOXEL_CARD_CLEAR)
        {
            const float over = VoxelRelief_LiftAt(x, cz - dv) - feet - dv;
            if (over > worst)
                worst = over;
        }
    }
    return worst > -VOXEL_CARD_CLEAR ? worst + VOXEL_CARD_CLEAR : 0.0f;
}

static float ModelPush(float cx, float cz, float halfW)
{
    const int32_t y = (int32_t)floorf(cz);
    const float left = cx - halfW + VOXEL_CARD_CLEAR;
    const float right = cx + halfW - VOXEL_CARD_CLEAR;
    const int32_t x0 = (int32_t)floorf(left), x1 = (int32_t)floorf(right);
    float push = 0.0f;

    for (int32_t x = x0; x <= x1; ++x)
    {
        const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt(x, y);
        float front = 0.0f, need = 0.0f;
        if (inst == NULL)
            continue;
        const bool faced = VoxelBuildings_FrontAt(inst, x, y, left, right, VOXEL_MODEL_PUSH_H, &front)
                        && VoxelSlots_ModelFacePush(front, cz, VOXEL_CARD_CLEAR, VOXEL_MODEL_PUSH_MAX, &need);
        if (!faced && VoxelBuildings_CellAt(inst, x, y, NULL, NULL))
            need = VOXEL_CARD_CLEAR;
        if (need > push)
            push = need;
    }
    return push;
}

static float SpriteScale(const struct Sprite *sprite)
{
    if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        return 1.0f;
    const s16 a = (s16)VoxelEntities_GetVisibleSnapshot()->oamMatrices[sprite->oam.matrixNum & (OAM_MATRIX_COUNT - 1u)].a;
    return a > 0 ? 256.0f / (float)a : 1.0f;
}

static void EmitBillboard(VoxelBuilder *builder, const VoxelSpriteSlot *slot, uint32_t index,
                          float cx, float cz, float offX, float offZ, float rise, float deck,
                          float rightX, float rightZ, float stretch, float shade, float scale, bool inDoor)
{
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float u0 = (float)baseX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)((int32_t)baseX + slot->atlasWidth) / (float)VOXEL_SPRITE_ATLAS_W;
    const int32_t rows = VisibleRows(slot);
    const float v0 = 1.0f - (float)baseY / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - (float)((int32_t)baseY + rows) / (float)VOXEL_SPRITE_ATLAS_H;
    const float halfW = (float)slot->width / VOXEL_PIXELS_PER_TILE * 0.5f * scale;
    const float height = (float)rows / VOXEL_PIXELS_PER_TILE * stretch * scale;
    float lift = VoxelRelief_LiftAt(cx, cz) + rise + deck;
    const float shift = VoxelRelief_ShiftAt(cx, cz) + deck;
    float push = (deck > 0.0f || inDoor) ? 0.0f : CardPush(cx, cz, halfW, height);
    const float model = (deck > 0.0f || inDoor) ? 0.0f : ModelPush(cx, cz, halfW);

    if (model > push)
        push = model;
    const float along = push > 0.0f ? push / sqrtf(rightX * rightX + rightZ * rightZ) : 0.0f;
    const float px = cx + offX - rightZ * along;
    const float pz = cz + offZ + shift + rightX * along;

    lift += push;
    const float ax = px - rightX * halfW, az = pz - rightZ * halfW;
    const float bx = px + rightX * halfW, bz = pz + rightZ * halfW;
    const float angle = sLay * (90.0f - VOXEL_SWOOP_PITCH) * (3.14159265358979323846f / 180.0f);
    const float pitchRad = VOXEL_SWOOP_PITCH * (3.14159265358979323846f / 180.0f);
    const float squat = 1.0f + sLay * (1.0f / (cosf(pitchRad) * (VOXEL_SWOOP_HEIGHT + 1.0f)) - 1.0f);
    const float topY = height * squat * cosf(angle);
    const float topZ = -height * squat * sinf(angle);

    VoxelBuilder_Quad(builder,
        &(VoxelVertex){ax, lift,        az,        u0, v1, shade},
        &(VoxelVertex){bx, lift,        bz,        u1, v1, shade},
        &(VoxelVertex){bx, lift + topY, bz + topZ, u1, v0, shade},
        &(VoxelVertex){ax, lift + topY, az + topZ, u0, v0, shade});
}

static void EmitDecal(VoxelBuilder *builder, const VoxelSpriteSlot *slot, uint32_t index,
                      float cx, float cz, float lift, float shade)
{
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float u0 = (float)baseX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)((int32_t)baseX + slot->atlasWidth) / (float)VOXEL_SPRITE_ATLAS_W;
    const float v0 = 1.0f - (float)baseY / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - (float)((int32_t)baseY + slot->height) / (float)VOXEL_SPRITE_ATLAS_H;
    const float halfW = (float)slot->width / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float halfH = (float)slot->height / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float y = VoxelRelief_LiftAt(cx, cz) + VOXEL_DECAL_LIFT + lift;
    const float z = cz + VoxelRelief_ShiftAt(cx, cz);

    VoxelBuilder_Quad(builder,
        &(VoxelVertex){cx - halfW, y, z + halfH, u0, v1, shade},
        &(VoxelVertex){cx + halfW, y, z + halfH, u1, v1, shade},
        &(VoxelVertex){cx + halfW, y, z - halfH, u1, v0, shade},
        &(VoxelVertex){cx - halfW, y, z - halfH, u0, v0, shade});
}

/* ── Doors (1x1 and 1x2) ─────────────────────────────────────────────────── */

typedef struct
{
    bool on, upright;
    int32_t x, y, width, height;
    float z;
} VoxelDoor;

static void FindDoorWall(VoxelDoor *door, const VoxelMapInstance *inst, uint32_t which)
{
    static int32_t lastLayout[VOXEL_DOORS] = { -1, -1 };
    static int32_t lastX[VOXEL_DOORS], lastY[VOXEL_DOORS];
    static bool lastUpright[VOXEL_DOORS];
    static float lastZ[VOXEL_DOORS];

    if (lastLayout[which] != inst->layoutId || lastX[which] != door->x || lastY[which] != door->y)
    {
        lastLayout[which] = inst->layoutId;
        lastX[which] = door->x;
        lastY[which] = door->y;
        lastZ[which] = (float)door->y + 1.0f;
        lastUpright[which] = VoxelBuildings_CellAt(inst, door->x, door->y, NULL, NULL);
        if (lastUpright[which])
            VoxelBuildings_DoorWall(inst, door->x, door->y, &lastZ[which]);
        else if (inst->indoor && VoxelRoom_Ground(inst, door->x, door->y) != VOXEL_ROOM_NONE)
            lastUpright[which] = true;
    }
    door->upright = lastUpright[which];
    door->z = lastZ[which];
}

static bool FindDoors(VoxelDoor doors[VOXEL_DOORS], uint16_t *atlas, int32_t snapOffX, int32_t snapOffY)
{
    static const u8 *lastTiles, *lastPalettes;
    static const uint16_t *lastPrimary, *lastSecondary;
    static int32_t lastWidth, lastHeight;
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    const uint32_t baseX = (VOXEL_DOOR_SLOT % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (VOXEL_DOOR_SLOT / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;

    for (uint32_t d = 0u; d < VOXEL_DOORS; ++d)
        doors[d].on = false;
    if (VoxelWorld_InstanceCount() == 0u || snap->doorCount == 0u)
        return false;

    const int32_t width = snap->doorWidth > 0u ? (int32_t)snap->doorWidth : 1;
    const int32_t height = snap->doorHeight > 1u ? 2 : 1;
    const u8 *const tiles = snap->doorTiles;
    const u8 *const palettes = snap->doorPalettes;
    const VoxelMapInstance *const inst = VoxelWorld_Instance(0u);
    const uint16_t *const primary = Voxel_ResolvePalettes(inst->primaryTileset);
    const uint16_t *const secondary = Voxel_ResolvePalettes(inst->secondaryTileset);
    if (tiles == NULL || palettes == NULL || primary == NULL || secondary == NULL)
        return false;

    for (uint32_t d = 0u; d < VOXEL_DOORS; ++d)
    {
        if (d >= snap->doorCount || d >= VOXEL_SNAPSHOT_DOORS)
            break;
        doors[d].on = true;
        doors[d].x = snap->doorX[d] - MAP_OFFSET + inst->originX + snapOffX;
        doors[d].y = snap->doorY[d] - MAP_OFFSET + inst->originY + snapOffY;
        doors[d].width = width;
        doors[d].height = height;
        FindDoorWall(&doors[d], inst, d);
    }

    if (!sDoorStale && tiles == lastTiles && palettes == lastPalettes
     && primary == lastPrimary && secondary == lastSecondary
     && width == lastWidth && height == lastHeight)
        return false;
    sDoorStale = false;
    lastTiles = tiles;
    lastPalettes = palettes;
    lastPrimary = primary;
    lastSecondary = secondary;
    lastWidth = width;
    lastHeight = height;

    const uint32_t rowCount = (uint32_t)height * 16u;
    for (uint32_t py = 0u; py < VOXEL_DOOR_ROWS; ++py)
    {
        for (uint32_t px = 0u; px < 32u; ++px)
        {
            uint16_t texel = 0u;
            if (py < rowCount && px < (uint32_t)width * 16u)
            {
                uint32_t palette = 0u;
                const uint32_t index = VoxelSlots_DoorPixel(tiles, palettes, px, py, &palette);
                const uint16_t bgr15 = (index == 0u) ? primary[0]
                                     : (palette < NUM_PALS_IN_PRIMARY ? primary[palette * 16u + index]
                                        : secondary[palette * 16u + index]);
                texel = VoxelGrade_RGBA5551(bgr15);
            }
            atlas[CtrVideo_Texel(baseX + px, baseY + py, VOXEL_SPRITE_ATLAS_W)] = texel;
        }
    }
    return true;
}

static float DoorLight(const VoxelDoor doors[VOXEL_DOORS], float cx, float cz)
{
    float light = 1.0f;

    for (uint32_t d = 0u; d < VOXEL_DOORS; ++d)
    {
        const VoxelDoor *const door = &doors[d];
        if (!door->on || cx < (float)door->x || cx >= (float)(door->x + door->width)
         || cz < (float)door->y || cz >= door->z + VOXEL_DOOR_DEPTH + VOXEL_DOOR_DUSK)
            continue;
        if (cz <= door->z + VOXEL_DOOR_DEPTH)
            return 0.0f;
        float t = (cz - door->z - VOXEL_DOOR_DEPTH) / VOXEL_DOOR_DUSK;
        t = VOXEL_DOOR_DARK + (1.0f - VOXEL_DOOR_DARK) * t;
        if (t < light)
            light = t;
    }
    return light;
}

static float DoorShade(float nx, float ny, float nz)
{
#if CTR_VOXEL_LIGHTING
    if (!VoxelWorld_Instance(0u)->indoor)
        return VoxelLighting_Face(nx, ny, nz);
#endif
    (void)nx; (void)ny; (void)nz;
    return 1.0f;
}

static void EmitDoorRows(VoxelBuilder *builder, const VoxelDoor *door, float wall,
                         float from, float to, float shade)
{
    const uint32_t baseX = (VOXEL_DOOR_SLOT % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (VOXEL_DOOR_SLOT / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float u0 = (float)baseX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)(baseX + (uint32_t)door->width * 16u) / (float)VOXEL_SPRITE_ATLAS_W;
    const float top = (float)door->y - (door->height > 1 ? 1.0f : 0.0f);
    const float v0 = 1.0f - ((float)baseY + (from - top) * 16.0f) / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - ((float)baseY + (to - top) * 16.0f) / (float)VOXEL_SPRITE_ATLAS_H;
    const float x0 = (float)door->x, x1 = (float)(door->x + door->width);
    const float cx = (float)door->x + (float)door->width * 0.5f;
    float lift = VoxelRelief_LiftAt(cx, (float)door->y + 0.5f);
    const float shift = VoxelRelief_ShiftAt(cx, (float)door->y + 0.5f);
    float h0 = 0.0f, h1 = 0.0f, z0 = 0.0f, z1 = 0.0f;

    VoxelSlots_DoorRow(from, wall, &h0, &z0);
    VoxelSlots_DoorRow(to, wall, &h1, &z1);
    if (h0 > 0.0f)
    {
        z0 += VOXEL_DOOR_DEPTH;
        z1 += VOXEL_DOOR_DEPTH;
    }
    else
    {
        lift += VOXEL_DECAL_LIFT + 0.01f;
    }
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x0, lift + h1, z1 + shift, u0, v1, shade},
        &(VoxelVertex){x1, lift + h1, z1 + shift, u1, v1, shade},
        &(VoxelVertex){x1, lift + h0, z0 + shift, u1, v0, shade},
        &(VoxelVertex){x0, lift + h0, z0 + shift, u0, v0, shade});
}

static void EmitDoor(VoxelBuilder *builder, const VoxelDoor *door)
{
    if (!door->on)
        return;
    const float top = (float)door->y - (door->height > 1 ? 1.0f : 0.0f);
    const float bottom = (float)door->y + 1.0f;
    const float wall = door->upright ? door->z : top;
    const float split = wall < top ? top : (wall > bottom ? bottom : wall);

    if (split > top)
        EmitDoorRows(builder, door, wall, top, split, DoorShade(0.0f, 0.0f, 1.0f));
    if (split < bottom)
        EmitDoorRows(builder, door, wall, split, bottom, DoorShade(0.0f, 1.0f, 0.0f));
}

/* ── Shadows, reflections, clouds, Mystery Dungeon cards ─────────────────── */

static void EmitCastShadow(VoxelBuilder *shadows, const VoxelSpriteSlot *slot, uint32_t index,
                           float worldX, float worldZ, float rightX, float rightZ, float stretch,
                           float light, float lift, float deck)
{
    if (gVoxelSun.strength == 0.0f)
        return;
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float u0 = (float)baseX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)((int32_t)baseX + slot->atlasWidth) / (float)VOXEL_SPRITE_ATLAS_W;
    const int32_t rows = VisibleRows(slot);
    const float v0 = 1.0f - (float)baseY / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - (float)((int32_t)baseY + rows) / (float)VOXEL_SPRITE_ATLAS_H;
    const float halfW = (float)slot->width / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float height = (float)rows / VOXEL_PIXELS_PER_TILE * stretch;
    const float cx = worldX + 0.5f + VOXEL_SUN_DX * lift;
    const float cz = worldZ + 0.5f + VOXEL_SUN_DZ * lift;
    const float ax = cx - rightX * halfW, az = cz - rightZ * halfW;
    const float bx = cx + rightX * halfW, bz = cz + rightZ * halfW;
    const float sx = VOXEL_SUN_DX * height, sz = VOXEL_SUN_DZ * height;
    const float ya = VoxelRelief_LiftAt(ax, az) + VOXEL_CAST_SHADOW_LIFT + deck;
    const float yb = VoxelRelief_LiftAt(bx, bz) + VOXEL_CAST_SHADOW_LIFT + deck;
    const float yc = VoxelRelief_LiftAt(bx + sx, bz + sz) + VOXEL_CAST_SHADOW_LIFT + deck;
    const float yd = VoxelRelief_LiftAt(ax + sx, az + sz) + VOXEL_CAST_SHADOW_LIFT + deck;
    const float za = VoxelRelief_ShiftAt(ax, az) + deck, zb = VoxelRelief_ShiftAt(bx, bz) + deck;
    const float zc = VoxelRelief_ShiftAt(bx + sx, bz + sz) + deck;
    const float zd = VoxelRelief_ShiftAt(ax + sx, az + sz) + deck;
    float strength = (light - VOXEL_CAST_SHADOW_LIT) / (1.0f - VOXEL_CAST_SHADOW_LIT);

    if (strength <= 0.0f)
        return;
    if (strength > 1.0f)
        strength = 1.0f;
    strength *= VOXEL_CAST_SHADOW_ALPHA * gVoxelSun.strength;
    VoxelBuilder_Quad(shadows,
        &(VoxelVertex){ax,      ya, az + za,      u0, v1, strength},
        &(VoxelVertex){bx,      yb, bz + zb,      u1, v1, strength},
        &(VoxelVertex){bx + sx, yc, bz + sz + zc, u1, v0, strength},
        &(VoxelVertex){ax + sx, yd, az + sz + zd, u0, v0, strength});
}

static void EmitDecalShadow(VoxelBuilder *shadows, const VoxelSpriteSlot *slot, uint32_t index,
                            float cx, float cz, float height, float light)
{
    if (gVoxelSun.strength == 0.0f)
        return;
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float u0 = (float)baseX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)((int32_t)baseX + slot->atlasWidth) / (float)VOXEL_SPRITE_ATLAS_W;
    const float v0 = 1.0f - (float)baseY / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - (float)((int32_t)baseY + slot->height) / (float)VOXEL_SPRITE_ATLAS_H;
    const float halfW = (float)slot->width / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float halfH = (float)slot->height / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float x = cx + VOXEL_SUN_DX * height, z0 = cz + VOXEL_SUN_DZ * height;
    const float y = VoxelRelief_LiftAt(x, z0) + VOXEL_CAST_SHADOW_LIFT;
    const float z = z0 + VoxelRelief_ShiftAt(x, z0);
    float strength = (light - VOXEL_CAST_SHADOW_LIT) / (1.0f - VOXEL_CAST_SHADOW_LIT);

    if (strength <= 0.0f)
        return;
    if (strength > 1.0f)
        strength = 1.0f;
    strength *= VOXEL_CAST_SHADOW_ALPHA * gVoxelSun.strength;
    VoxelBuilder_Quad(shadows,
        &(VoxelVertex){x - halfW, y, z + halfH, u0, v1, strength},
        &(VoxelVertex){x + halfW, y, z + halfH, u1, v1, strength},
        &(VoxelVertex){x + halfW, y, z - halfH, u1, v0, strength},
        &(VoxelVertex){x - halfW, y, z - halfH, u0, v0, strength});
}

static bool IsReflectiveFootprint(float ax, float az, float bx, float bz, float length)
{
    const float minX = fminf(ax, bx), maxX = fmaxf(ax, bx);
    const float minZ = fminf(az, bz), maxZ = fmaxf(az, bz) + length;
    const int32_t firstX = (int32_t)floorf(minX + 0.0001f);
    const int32_t lastX = (int32_t)floorf(maxX - 0.0001f);
    const int32_t firstZ = (int32_t)floorf(minZ + 0.0001f);
    const int32_t lastZ = (int32_t)floorf(maxZ - 0.0001f);

    for (int32_t z = firstZ; z <= lastZ; ++z)
        for (int32_t x = firstX; x <= lastX; ++x)
            if (!VoxelWorld_IsVisibleReflectiveSurface(x, z))
                return false;
    return true;
}

static void EmitReflection(VoxelBuilder *reflections, const VoxelSpriteSlot *slot,
                           uint32_t index, float worldX, float worldZ,
                           float rightX, float rightZ, float stretch)
{
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float u0 = (float)baseX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)((int32_t)baseX + slot->atlasWidth) / (float)VOXEL_SPRITE_ATLAS_W;
    const float v0 = 1.0f - (float)baseY / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - (float)((int32_t)baseY + slot->height) / (float)VOXEL_SPRITE_ATLAS_H;
    const float halfW = (float)slot->width / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float cardLength = (float)slot->height / VOXEL_PIXELS_PER_TILE * stretch;
    const float fullLength = cardLength > 1.75f ? 1.75f : cardLength;
    float lengths[VOXEL_REFLECTION_STRIPS];
    const float cx = worldX + 0.5f, nearZ = worldZ + 1.0f;
    const float shift = VoxelRelief_ShiftAt(cx, nearZ + 0.5f);
    const float y = VoxelRelief_LiftAt(cx, nearZ + 0.5f) + 0.035f;
    const uint32_t strips = VOXEL_REFLECTION_STRIPS;

    for (uint32_t s = 0u; s < strips; ++s)
    {
        const float a = halfW * (2.0f * (float)s / (float)strips - 1.0f);
        const float b = halfW * (2.0f * (float)(s + 1u) / (float)strips - 1.0f);
        const float ax = cx + rightX * a, az = nearZ + rightZ * a;
        const float bx = cx + rightX * b, bz = nearZ + rightZ * b;

        lengths[s] = 0.0f;
        for (float next = 0.25f; next <= fullLength + 0.0001f; next += 0.25f)
        {
            if (!IsReflectiveFootprint(ax, az, bx, bz, next))
                break;
            lengths[s] = next;
        }
    }
    for (uint32_t s = 0u, e; s < strips; s = e)
    {
        const float length = lengths[s];
        for (e = s + 1u; e < strips && lengths[e] == length; ++e)
            ;
        if (length < 0.25f)
            continue;
        const float t0 = (float)s / (float)strips, t1 = (float)e / (float)strips;
        const float a = halfW * (2.0f * t0 - 1.0f), b = halfW * (2.0f * t1 - 1.0f);
        const float ax = cx + rightX * a, az = nearZ + rightZ * a + shift;
        const float bx = cx + rightX * b, bz = nearZ + rightZ * b + shift;
        const float ua = u0 + (u1 - u0) * t0, ub = u0 + (u1 - u0) * t1;
        const float vFar = v1 + (v0 - v1) * (length / cardLength);

        VoxelBuilder_Quad(reflections,
            &(VoxelVertex){ax, y, az, ua, v1, 1.0f},
            &(VoxelVertex){bx, y, bz, ub, v1, 1.0f},
            &(VoxelVertex){bx, y, bz + length, ub, vFar, 1.0f},
            &(VoxelVertex){ax, y, az + length, ua, vFar, 1.0f});
    }
}

static void EmitCloudPiece(VoxelBuilder *reflections, const VoxelSpriteSlot *slot, uint32_t index,
                           float left, float top, float x0, float z0, float x1, float z1,
                           float y, float shift)
{
    const uint32_t baseX = (index % VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const uint32_t baseY = (index / VOXEL_SPRITE_COLUMNS) * VOXEL_SPRITE_SLOT_DIM;
    const float width = (float)slot->width / VOXEL_PIXELS_PER_TILE;
    const float height = (float)slot->height / VOXEL_PIXELS_PER_TILE;

    x0 = fmaxf(x0, left);
    x1 = fminf(x1, left + width);
    z0 = fmaxf(z0, top);
    z1 = fminf(z1, top + height);
    if (x1 <= x0 || z1 <= z0)
        return;
    const float u0 = ((float)baseX + (float)slot->atlasWidth * (x0 - left) / width) / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = ((float)baseX + (float)slot->atlasWidth * (x1 - left) / width) / (float)VOXEL_SPRITE_ATLAS_W;
    const float v0 = 1.0f - ((float)baseY + (float)slot->height * (z0 - top) / height) / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - ((float)baseY + (float)slot->height * (z1 - top) / height) / (float)VOXEL_SPRITE_ATLAS_H;

    VoxelBuilder_Quad(reflections,
        &(VoxelVertex){x0, y, z0 + shift, u0, v0, 1.0f},
        &(VoxelVertex){x1, y, z0 + shift, u1, v0, 1.0f},
        &(VoxelVertex){x1, y, z1 + shift, u1, v1, 1.0f},
        &(VoxelVertex){x0, y, z1 + shift, u0, v1, 1.0f});
}

static void EmitCloud(VoxelBuilder *reflections, const VoxelSpriteSlot *slot, uint32_t index,
                      float left, float top)
{
    const float right = left + (float)slot->width / VOXEL_PIXELS_PER_TILE;
    const float bottom = top + (float)slot->height / VOXEL_PIXELS_PER_TILE;

    for (int32_t z = (int32_t)floorf(top); z < (int32_t)ceilf(bottom); ++z)
        for (int32_t x = (int32_t)floorf(left); x < (int32_t)ceilf(right); ++x)
        {
            uint16_t rows[16];
            if (!VoxelWorld_IsVisibleReflectiveSurface(x, z))
                continue;
            const float y = VoxelRelief_LiftAt((float)x + 0.5f, (float)z + 0.5f) + 0.035f;
            const float shift = VoxelRelief_ShiftAt((float)x + 0.5f, (float)z + 0.5f);
            if (!VoxelWorld_UnderGroundMask(x, z, rows))
            {
                EmitCloudPiece(reflections, slot, index, left, top,
                               (float)x, (float)z, (float)x + 1.0f, (float)z + 1.0f, y, shift);
                continue;
            }
            for (uint32_t r0 = 0u, r1; r0 < 16u; r0 = r1)
            {
                const uint16_t mask = rows[r0];
                for (r1 = r0 + 1u; r1 < 16u && rows[r1] == mask; ++r1)
                    ;
                for (uint32_t a = 0u, b; a < 16u; a = b)
                {
                    if (!(mask & (1u << a)))
                    {
                        b = a + 1u;
                        continue;
                    }
                    for (b = a + 1u; b < 16u && (mask & (1u << b)); ++b)
                        ;
                    EmitCloudPiece(reflections, slot, index, left, top,
                                   (float)x + (float)a / 16.0f, (float)z + (float)r0 / 16.0f,
                                   (float)x + (float)b / 16.0f, (float)z + (float)r1 / 16.0f, y, shift);
                }
            }
        }
}

static void EmitClouds(VoxelBuilder *reflections, const VoxelSpriteSlot *slot, uint32_t index,
                       const VoxelCamera *camera, float left, float top)
{
    const float width = (float)slot->width / VOXEL_PIXELS_PER_TILE;
    const float height = (float)slot->height / VOXEL_PIXELS_PER_TILE;
    const float minX = camera->targetX - 16.0f, maxX = camera->targetX + 16.0f;
    const float minZ = camera->targetZ - 20.0f, maxZ = camera->targetZ + 10.0f;

    left -= VOXEL_CLOUD_PERIOD_X * floorf((left + width - minX) / VOXEL_CLOUD_PERIOD_X);
    top -= VOXEL_CLOUD_PERIOD_Z * floorf((top + height - minZ) / VOXEL_CLOUD_PERIOD_Z);
    for (float z = top; z < maxZ; z += VOXEL_CLOUD_PERIOD_Z)
        for (float x = left; x < maxX; x += VOXEL_CLOUD_PERIOD_X)
            EmitCloud(reflections, slot, index, x, z);
}

static void EmitMysteryDungeonCard(VoxelBuilder *builder, float x, float z, int32_t dx, int32_t dy,
                                   int32_t w, int32_t h, int32_t foot, int32_t scale,
                                   uint32_t cellX, uint32_t cellY, float rightX, float rightZ,
                                   float stretch, const VoxelDoor doors[VOXEL_DOORS])
{
    const float cx = x + 0.5f, cz = z + 0.5f;
    const float u0 = (float)cellX / (float)VOXEL_SPRITE_ATLAS_W;
    const float u1 = (float)(cellX + (uint32_t)w) / (float)VOXEL_SPRITE_ATLAS_W;
    const float v0 = 1.0f - (float)cellY / (float)VOXEL_SPRITE_ATLAS_H;
    const float v1 = 1.0f - (float)(cellY + (uint32_t)h) / (float)VOXEL_SPRITE_ATLAS_H;
    const float halfW = (float)(w * scale) / VOXEL_PIXELS_PER_TILE * 0.5f;
    const float centre = ((float)dx + (float)(w * scale) * 0.5f) / VOXEL_PIXELS_PER_TILE;
    const float height = (float)(h * scale) / VOXEL_PIXELS_PER_TILE * stretch;
    const float below = (float)(dy + h * scale - foot) / VOXEL_PIXELS_PER_TILE * stretch;
    const float lift = VoxelRelief_LiftAt(cx, cz), shift = VoxelRelief_ShiftAt(cx, cz);
    float push = CardPush(cx, cz, halfW, height);
    const float model = ModelPush(cx, cz, halfW);
    float shade = 1.0f;
    const float dusk = DoorLight(doors, cx, cz);

    if (dusk <= 0.0f || w <= 0 || h <= 0)
        return;
    if (model > push)
        push = model;
#if CTR_VOXEL_LIGHTING
    {
        const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt((int32_t)floorf(cx), (int32_t)floorf(cz));
        if (sLightingAllowed && inst != NULL && !inst->indoor)
            shade = VoxelLighting_SampleAlone(cx, lift + 0.75f, cz + shift);
    }
#endif
    const float along = push > 0.0f ? push / sqrtf(rightX * rightX + rightZ * rightZ) : 0.0f;
    const float px = cx + rightX * centre - rightZ * along;
    const float pz = cz + rightZ * centre + shift + rightX * along;
    const float bottom = lift + push - below;

    VoxelBuilder_Quad(builder,
        &(VoxelVertex){px - rightX * halfW, bottom,          pz - rightZ * halfW, u0, v1, shade * dusk},
        &(VoxelVertex){px + rightX * halfW, bottom,          pz + rightZ * halfW, u1, v1, shade * dusk},
        &(VoxelVertex){px + rightX * halfW, bottom + height, pz + rightZ * halfW, u1, v0, shade * dusk},
        &(VoxelVertex){px - rightX * halfW, bottom + height, pz - rightZ * halfW, u0, v0, shade * dusk});
}

static void EmitFollower(VoxelBuilder *builder, const VoxelFollowerCard *f,
                         float rightX, float rightZ, float stretch, const VoxelDoor doors[VOXEL_DOORS])
{
    EmitMysteryDungeonCard(builder, f->x, f->z, f->dx, f->dy, f->w, f->h, f->foot, 1,
                           VOXEL_FOLLOWER_CELL_X, VOXEL_FOLLOWER_CELL_Y, rightX, rightZ, stretch, doors);
}

/* ── Field effects & main Core 0 emit ────────────────────────────────────── */

typedef struct
{
    bool drawn;
    float worldX, worldZ;
    int32_t screenX;
    int32_t top, base, feet;
    const struct Sprite *sprite;
    float rise, deck, standZ, shade;
    bool outdoor;
} VoxelObjectCard;

typedef struct
{
    const struct Sprite *sprite;
    uint32_t slot;
    int32_t owner;
    bool front, decal, tile, surf, cloud, heal;
    int32_t shiftX, shiftY;
} VoxelEffectCard;

static bool IsTemplate(const struct SpriteTemplate *templatePtr, uint32_t first, uint32_t last)
{
    for (uint32_t i = first; i <= last; ++i)
        if (templatePtr == gFieldEffectObjectTemplatePointers[i])
            return true;
    return false;
}

static bool IsTileEffect(const struct SpriteTemplate *templatePtr)
{
    return templatePtr == gFieldEffectObjectTemplatePointers[FLDEFFOBJ_TALL_GRASS];
}

static bool IsDecal(const struct SpriteTemplate *templatePtr)
{
    return templatePtr == gFieldEffectObjectTemplatePointers[FLDEFFOBJ_RIPPLE]
        || templatePtr == gFieldEffectObjectTemplatePointers[FLDEFFOBJ_SAND_FOOTPRINTS]
        || templatePtr == gFieldEffectObjectTemplatePointers[FLDEFFOBJ_DEEP_SAND_FOOTPRINTS]
        || templatePtr == gFieldEffectObjectTemplatePointers[FLDEFFOBJ_BIKE_TIRE_TRACKS];
}

static bool SnapIsVisibleWild(const VoxelEntitySnapshot *snap, uint32_t id)
{
    return id < MAX_SPRITES && (((snap->visibleWild >> id) & 1ULL) != 0ULL);
}

static u8 EffectPriority(const VoxelEntitySnapshot *snap, const struct Sprite *sprite, uint32_t id)
{
    if (SnapIsVisibleWild(snap, id))
        return VOXEL_SLOT_PRIORITY_WILD;
    if (IsTileEffect(sprite->template))
        return VOXEL_SLOT_PRIORITY_DECOR;
    return VOXEL_SLOT_PRIORITY_EFFECT;
}

static bool DrawnOver(const struct Sprite *sprite, const struct Sprite *object)
{
    if (sprite->oam.priority != object->oam.priority)
        return sprite->oam.priority < object->oam.priority;
    return sprite->subpriority <= object->subpriority;
}

static int32_t SpriteHeight(const struct Sprite *sprite)
{
    int32_t w = 0, h = 0;
    GetSpriteDimensions(sprite->oam.shape, sprite->oam.size, &w, &h);
    return h;
}

static int32_t EffectOwner(const struct Sprite *sprite, const VoxelObjectCard *objects)
{
    const int32_t x = sprite->x + sprite->x2;
    const int32_t y = sprite->y + sprite->y2 + sprite->centerToCornerVecY + SpriteHeight(sprite) / 2;
    int32_t best = -1, bestDistance = 0x7fff;

    for (uint32_t i = 0u; i < OBJECT_EVENTS_COUNT; ++i)
    {
        const VoxelObjectCard *const object = &objects[i];
        if (!object->drawn || abs(x - object->screenX) > 8)
            continue;
        const int32_t y2 = object->sprite->y2;
        if (y < object->top + y2 || y > object->base + y2)
            continue;
        const int32_t distance = abs(y - (object->top + object->base) / 2 - y2);
        if (distance < bestDistance)
        {
            best = (int32_t)i;
            bestDistance = distance;
        }
    }
    return best;
}

uint32_t VoxelEntities_Emit(VoxelBuilder *builder, uint16_t *atlas, const VoxelCamera *camera,
                            VoxelBuilder *shadows, VoxelBuilder *reflections)
{
    const float yawRad = camera->yaw * (3.14159265358979323846f / 180.0f);
    float narrow = sqrtf(cosf(camera->pitch * (3.14159265358979323846f / 180.0f)));
    sLay = camera->lay;
    narrow = 1.0f + (narrow - 1.0f) * (1.0f - sLay);
    const float stretch = 1.0f / narrow;
    const float rightX = cosf(yawRad) * narrow, rightZ = -sinf(yawRad) * narrow;
    const float towardX = sinf(yawRad), towardZ = cosf(yawRad);
    const float pixel = stretch / VOXEL_PIXELS_PER_TILE;

    static VoxelObjectCard objects[OBJECT_EVENTS_COUNT];
    static VoxelEffectCard effects[VOXEL_EFFECTS_MAX];
    static VoxelEffectCard candidates[MAX_SPRITES];
    static VoxelSlotRequest requests[MAX_SPRITES];
    static bool objectSprite[MAX_SPRITES];
    static u16 objectTiles[OBJECT_EVENTS_COUNT];
    static VoxelWildCard wild[VOXEL_WILD_MAX];

    bool claimed[VOXEL_SPRITE_SLOTS] = { false };
    bool reset[VOXEL_SPRITE_SLOTS] = { false };
    uint32_t objectTileCount = 0u, effectCount = 0u, candidateCount = 0u, wildCount = 0u, updates = 0u;
    VoxelDoor doors[VOXEL_DOORS];
    sPlayerVertexFirst = -1;

    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    const bool buyMenu = GameFrame_Get()->overlay.buyMenu;
    int32_t snapOffX = 0, snapOffY = 0;
    if (snap != NULL && snap->mapGroup >= 0 && snap->mapNum >= 0)
        VoxelWorld_GetInstanceOffset(snap->mapGroup, snap->mapNum, &snapOffX, &snapOffY);

    memset(objectSprite, 0, sizeof(objectSprite));
    for (uint32_t i = 0u; i < OBJECT_EVENTS_COUNT; ++i)
    {
        const struct ObjectEvent *const obj = &snap->objectEvents[i];
        VoxelObjectCard *const card = &objects[i];
        float worldX = 0.0f, worldZ = 0.0f;
        float shade = 1.0f;
        bool outdoor = false;

        card->drawn = false;
        if (!obj->active)
            continue;
        claimed[i] = true;
        if (sSlotEffect[i] != 0u)
        {
            sSlotEffect[i] = 0u;
            sSlots[i].valid = false;
            sSlots[i].footPadImages = NULL;
        }
        if (obj->spriteId >= MAX_SPRITES)
            continue;
        const struct Sprite *const sprite = SnapSprite(snap, obj);
        objectSprite[obj->spriteId] = true;
        objectTiles[objectTileCount++] = sprite->oam.tileNum;
        if (obj->invisible || !sprite->coordOffsetEnabled)
            continue;

        GetObjectWorldPos(snap, obj, sprite, &worldX, &worldZ);
        worldX += (float)snapOffX;
        worldZ += (float)snapOffY;
        if (fabsf(worldX - camera->targetX) > 24.0f
         || (camera->targetZ - worldZ) > 42.0f
         || (worldZ - camera->targetZ) > 16.0f)
            continue;
        if (!buyMenu && RefreshSlot(i, sprite, obj, snap->extPalettes[obj->spriteId], NULL, atlas))
        {
            ++updates;
            sDirtySlots |= (1ULL << i);
        }
        if (!sSlots[i].valid)
            continue;
        {
            const struct ObjectEventGraphicsInfo *const info = SnapGraphicsInfo(snap, obj);
            const bool changed = (sSlots[i].footPadImages != info->images);
            if (changed || !sSlots[i].footPadExact)
            {
                bool exact = true;
                const int32_t pad = (info->images != NULL) ? StandingFootPad(info, &sSlots[i], &exact) : 0;
                sSlots[i].footPadImages = info->images;
                if (changed || exact)
                    sSlots[i].footPad = pad;
                sSlots[i].footPadExact = exact;
            }
        }
        card->deck = ObjectDeck(obj, worldX, worldZ);
        {
            const float cx = worldX + 0.5f, cz = worldZ + 0.5f;
            const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt((int32_t)floorf(cx), (int32_t)floorf(cz));
            outdoor = (inst != NULL && !inst->indoor);
#if CTR_VOXEL_LIGHTING
            if (outdoor)
            {
                if (sLightingAllowed)
                {
                    shade = VoxelLighting_SampleAlone(cx, VoxelRelief_LiftAt(cx, cz) + 0.75f + card->deck,
                                                      cz + VoxelRelief_ShiftAt(cx, cz) + card->deck);
                    sLastShade[i] = shade;
                    sLastShadeKnown[i] = true;
                }
                else if (sLastShadeKnown[i])
                {
                    shade = sLastShade[i];
                }
            }
#endif
        }
        card->drawn = true;
        card->worldX = worldX;
        card->worldZ = worldZ;
        card->screenX = sprite->x + sprite->x2;
        card->top = sprite->y + sprite->centerToCornerVecY - (sSlots[i].height - SpriteHeight(sprite)) / 2;
        card->base = card->top + sSlots[i].height;
        card->feet = card->base - (sSlots[i].height - VisibleRows(&sSlots[i]));
        card->sprite = sprite;
        card->rise = 0.0f;
        card->standZ = 0.0f;
        if (i != snap->playerObjectEventId && sSlots[i].height <= 16)
        {
            const int32_t tx = (int32_t)floorf(worldX + 0.5f);
            const int32_t tz = (int32_t)floorf(worldZ + 0.5f);
            const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt(tx, tz);
            float standRise = 0.0f, standOffZ = 0.0f;
            if (inst != NULL && VoxelRoom_StandOn(inst, tx, tz, &standRise, &standOffZ))
            {
                card->rise = standRise;
                card->standZ = standOffZ;
            }
        }
        card->shade = shade;
        card->outdoor = outdoor;
    }

    if (!buyMenu && VoxelWorld_InstanceCount() > 0u)
    {
        const VoxelMapInstance *const inst0 = VoxelWorld_Instance(0u);
        wildCount = VoxelWild_Update(atlas, camera->yaw, (float)(inst0->originX + snapOffX),
                                     (float)(inst0->originY + snapOffY), wild, &sDirtySlots);
        for (uint32_t w = 0u; w < wildCount; ++w)
            if (wild[w].spriteId >= 0 && wild[w].spriteId < MAX_SPRITES)
                objectSprite[wild[w].spriteId] = true;
    }

    if (!buyMenu)
    {
        for (uint32_t id = 0u; id < MAX_SPRITES; ++id)
        {
            const struct Sprite *const sprite = &snap->sprites[id];
            const bool heal = sprite->inUse && !sprite->coordOffsetEnabled && (((snap->healSprites >> id) & 1ULL) != 0ULL);

            if (!sprite->inUse || sprite->invisible || (!sprite->coordOffsetEnabled && !heal) || objectSprite[id])
                continue;
            const bool cloud = (((snap->cloudSprites >> id) & 1ULL) != 0ULL);
            if (IsTemplate(sprite->template, FLDEFFOBJ_SHADOW_S, FLDEFFOBJ_SHADOW_XL)
             || ((((snap->weatherSprites >> id) & 1ULL) != 0ULL) && !cloud))
                continue;
            uint32_t t = 0u;
            for (; t < objectTileCount && objectTiles[t] != sprite->oam.tileNum; ++t)
                ;
            if (t < objectTileCount)
                continue;

            int32_t w = 0, h = 0;
            GetSpriteDimensions(sprite->oam.shape, sprite->oam.size, &w, &h);
            if (w > (int32_t)VOXEL_SPRITE_SLOT_DIM || h > (int32_t)VOXEL_SPRITE_SLOT_DIM)
                continue;

            VoxelEffectCard *const effect = &candidates[candidateCount];
            VoxelSlotRequest *const request = &requests[candidateCount];
            effect->sprite = sprite;
            effect->slot = 0u;
            effect->surf = (sprite->template == gFieldEffectObjectTemplatePointers[FLDEFFOBJ_SURF_BLOB]);
            effect->cloud = cloud;
            effect->decal = IsDecal(sprite->template) || cloud;
            effect->tile = IsTileEffect(sprite->template);
            effect->heal = heal;
            effect->shiftX = heal ? -snap->spriteCoordOffsetX : 0;
            effect->shiftY = heal ? -snap->spriteCoordOffsetY : 0;
            effect->owner = (effect->decal || effect->tile || effect->heal || SnapIsVisibleWild(snap, id))
                          ? -1 : EffectOwner(sprite, objects);
            effect->front = (effect->owner < 0) || DrawnOver(sprite, objects[effect->owner].sprite);

            bool inRange = false;
            const VoxelObjectCard *const reference = snap->playerObjectEventId < OBJECT_EVENTS_COUNT
                                                   ? &objects[snap->playerObjectEventId] : NULL;
            if (SnapIsVisibleWild(snap, id))
            {
                const float cx = (float)snap->wildX16[id] / VOXEL_PIXELS_PER_TILE + (float)snapOffX + 0.5f;
                const float cz = (float)snap->wildY16[id] / VOXEL_PIXELS_PER_TILE + (float)snapOffY + 0.5f;
                inRange = fabsf(cx - camera->targetX) <= 24.0f
                       && (camera->targetZ - cz) <= 42.0f
                       && (cz - camera->targetZ) <= 16.0f;
            }
            else if (effect->tile)
            {
                float cx = (float)(sprite->data[1] - MAP_OFFSET + snapOffX) + 0.5f;
                float cz = (float)(sprite->data[2] - MAP_OFFSET + snapOffY) + 0.5f;
                if (VoxelWorld_InstanceCount() > 0u)
                {
                    cx += (float)VoxelWorld_Instance(0u)->originX;
                    cz += (float)VoxelWorld_Instance(0u)->originY;
                }
                inRange = fabsf(cx - camera->targetX) <= VOXEL_EFFECT_RANGE
                       && fabsf(cz - camera->targetZ) <= VOXEL_EFFECT_RANGE;
            }
            else if (effect->owner >= 0 && !effect->surf)
            {
                inRange = true;
            }
            else if (reference != NULL && reference->drawn)
            {
                const int32_t base = sprite->y + effect->shiftY + sprite->centerToCornerVecY + h;
                const float cx = reference->worldX + 0.5f
                               + (float)(sprite->x + effect->shiftX + sprite->x2 - reference->screenX) / VOXEL_PIXELS_PER_TILE;
                const float cz = reference->worldZ + 0.5f + (float)(base - reference->base) / VOXEL_PIXELS_PER_TILE;
                inRange = fabsf(cx - camera->targetX) <= VOXEL_EFFECT_RANGE
                       && fabsf(cz - camera->targetZ) <= VOXEL_EFFECT_RANGE;
            }
            if (cloud)
                inRange = true;
            request->id = (uint8_t)id;
            request->priority = EffectPriority(snap, sprite, id);
            request->inRange = inRange;
            request->slot = -1;
            ++candidateCount;
        }

        const uint32_t dropped = VoxelSlots_Assign(requests, candidateCount, sSlotEffect, claimed, reset,
                                                   VOXEL_DOOR_SLOT, OBJECT_EVENTS_COUNT);
        if (dropped > 0u)
        {
            static uint32_t sLastFull;
            if (sLastFull == 0u || snap->frameNum - sLastFull >= 600u)
            {
                sLastFull = snap->frameNum != 0u ? snap->frameNum : 1u;
                CtrLog_Write(CTR_LOG_VIDEO, "VOXEL sprite slots full: %u dropped", dropped);
            }
        }
        for (uint32_t s = 0u; s < VOXEL_SPRITE_SLOTS; ++s)
        {
            if (!reset[s])
                continue;
            sSlots[s].valid = false;
            sSlots[s].footPad = 0;
            sSlots[s].footPadImages = NULL;
        }
        for (uint32_t c = 0u; c < candidateCount && effectCount < VOXEL_EFFECTS_MAX; ++c)
        {
            if (requests[c].slot < 0)
                continue;
            VoxelEffectCard *const effect = &effects[effectCount];
            *effect = candidates[c];
            effect->slot = (uint32_t)requests[c].slot;
            if (effect->surf)
            {
                if (effect->owner >= 0 && !effect->front && VOXEL_SURF_SEAT > objects[effect->owner].rise)
                    objects[effect->owner].rise = VOXEL_SURF_SEAT;
                effect->decal = true;
                effect->owner = -1;
                effect->front = true;
            }
            ++effectCount;
        }
        for (uint32_t s = 0u; s < VOXEL_SPRITE_SLOTS; ++s)
        {
            if (claimed[s])
                continue;
            sSlotEffect[s] = 0u;
            sSlots[s].valid = false;
        }
    }

    if (FindDoors(doors, atlas, snapOffX, snapOffY))
    {
        ++updates;
        sDirtySlots |= (1ULL << VOXEL_DOOR_SLOT);
    }
    for (uint32_t d = 0u; d < VOXEL_DOORS; ++d)
        EmitDoor(builder, &doors[d]);

    for (uint32_t i = 0u; i < OBJECT_EVENTS_COUNT; ++i)
    {
        const VoxelObjectCard *const card = &objects[i];
        if (!card->drawn)
            continue;
        const float dusk = DoorLight(doors, card->worldX + 0.5f, card->worldZ + 0.5f);
        if (dusk <= 0.0f)
            continue;
        float rise = card->rise;
        if (card->rise > 0.0f || IsJumping(&snap->objectEvents[i], card->sprite))
            rise -= (float)card->sprite->y2 * pixel;

        if (shadows != NULL && card->outdoor)
            EmitCastShadow(shadows, &sSlots[i], i, card->worldX, card->worldZ, rightX, rightZ,
                           stretch, card->shade, card->rise > 0.0f ? rise : 0.0f, card->deck);
        if (reflections != NULL && snap->objectEvents[i].hasReflection)
            EmitReflection(reflections, &sSlots[i], i, card->worldX, card->worldZ, rightX, rightZ, stretch);

        const uint32_t first = builder->count;
        EmitBillboard(builder, &sSlots[i], i, card->worldX + 0.5f, card->worldZ + 0.5f,
                      0.0f, card->standZ, rise, card->deck, rightX, rightZ, stretch,
                      card->shade * dusk, 1.0f, dusk < 1.0f);
        if (i == snap->playerObjectEventId && builder->count == first + 6u)
            sPlayerVertexFirst = (int32_t)first;
    }

    if (snap->playerObjectEventId < OBJECT_EVENTS_COUNT && objects[snap->playerObjectEventId].drawn)
    {
        VoxelFollowerCard follower;
        if (VoxelFollower_Update(atlas, camera->yaw, &follower, &sDirtySlots))
            EmitFollower(builder, &follower, rightX, rightZ, stretch, doors);
    }

    for (uint32_t w = 0u; w < wildCount; ++w)
    {
        const VoxelWildCard *const c = &wild[w];
        if (fabsf(c->x - camera->targetX) > 24.0f
         || (camera->targetZ - c->z) > 42.0f
         || (c->z - camera->targetZ) > 16.0f)
            continue;
        EmitMysteryDungeonCard(builder, c->x, c->z, c->dx, c->dy, c->w, c->h, c->foot, c->scale,
                               c->cellX, c->cellY, rightX, rightZ, stretch, doors);
    }

    const bool isOceanMap = (snap->mapType == MAP_TYPE_OCEAN_ROUTE || snap->mapType == MAP_TYPE_UNDERWATER);
    for (uint32_t e = 0u; e < effectCount; ++e)
    {
        const VoxelEffectCard *const effect = &effects[e];
        const struct Sprite *const sprite = effect->sprite;
        const u16 *const overridePal = effect->heal ? snap->healPalette : NULL;
        if (RefreshSlot(effect->slot, sprite, NULL, snap->extPalettes[SnapSpriteId(snap, sprite)], overridePal, atlas))
        {
            ++updates;
            sDirtySlots |= (1ULL << effect->slot);
        }
        if (!sSlots[effect->slot].valid)
            continue;

        const int32_t h = sSlots[effect->slot].height;
        const int32_t x = sprite->x + effect->shiftX + sprite->x2;
        const int32_t base = sprite->y + effect->shiftY + sprite->centerToCornerVecY + h;

        if (effect->cloud)
        {
            const VoxelObjectCard *const ref = snap->playerObjectEventId < OBJECT_EVENTS_COUNT
                                             ? &objects[snap->playerObjectEventId] : NULL;
            if (!isOceanMap && reflections != NULL && ref != NULL && ref->drawn)
            {
                EmitClouds(reflections, &sSlots[effect->slot], effect->slot, camera,
                           ref->worldX + 0.5f + (float)(x - ref->screenX) / VOXEL_PIXELS_PER_TILE
                           - (float)sSlots[effect->slot].width / VOXEL_PIXELS_PER_TILE * 0.5f,
                           ref->worldZ + 0.5f + (float)(base - h - (ref->base - 8)) / VOXEL_PIXELS_PER_TILE);
            }
            continue;
        }
        if (effect->tile)
        {
            float cx = (float)(sprite->data[1] - MAP_OFFSET + snapOffX) + 0.5f;
            float cz = (float)(sprite->data[2] - MAP_OFFSET + snapOffY) + 0.5f;
            float shade = 1.0f;
            if (snap->playerObjectEventId < OBJECT_EVENTS_COUNT && objects[snap->playerObjectEventId].drawn)
                shade = objects[snap->playerObjectEventId].shade;
            if (VoxelWorld_InstanceCount() > 0u)
            {
                cx += (float)VoxelWorld_Instance(0u)->originX;
                cz += (float)VoxelWorld_Instance(0u)->originY;
            }
            if (fabsf(cx - camera->targetX) > 24.0f || fabsf(cz - camera->targetZ) > 24.0f)
                continue;
            EmitDecal(builder, &sSlots[effect->slot], effect->slot, cx, cz, 0.0f, shade);
            continue;
        }
        if (effect->owner >= 0)
        {
            const VoxelObjectCard *const object = &objects[effect->owner];
            const float depth = effect->front ? VOXEL_EFFECT_DEPTH : -VOXEL_EFFECT_DEPTH;
            const float along = (float)(x - object->screenX) / VOXEL_PIXELS_PER_TILE;
            const float rise = object->rise + (float)(object->feet - base - sprite->y2) * pixel;

            EmitBillboard(builder, &sSlots[effect->slot], effect->slot,
                          object->worldX + 0.5f, object->worldZ + 0.5f,
                          rightX * along + towardX * depth, rightZ * along + towardZ * depth,
                          rise, object->deck, rightX, rightZ, stretch, object->shade, SpriteScale(sprite), false);
            continue;
        }

        const uint32_t spriteId = SnapSpriteId(snap, sprite);
        if (SnapIsVisibleWild(snap, spriteId))
        {
            const float cx = (float)snap->wildX16[spriteId] / VOXEL_PIXELS_PER_TILE + (float)snapOffX + 0.5f;
            const float cz = (float)snap->wildY16[spriteId] / VOXEL_PIXELS_PER_TILE + (float)snapOffY + 0.5f;
            if (fabsf(cx - camera->targetX) > 24.0f
             || (camera->targetZ - cz) > 42.0f
             || (cz - camera->targetZ) > 16.0f)
                continue;
            float shade = 1.0f;
#if CTR_VOXEL_LIGHTING
            {
                const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt((int32_t)floorf(cx), (int32_t)floorf(cz));
                if (sLightingAllowed && inst != NULL && !inst->indoor)
                    shade = VoxelLighting_SampleAlone(cx, VoxelRelief_LiftAt(cx, cz) + 0.75f,
                                                      cz + VoxelRelief_ShiftAt(cx, cz));
            }
#endif
            EmitBillboard(builder, &sSlots[effect->slot], effect->slot, cx, cz, 0.0f, 0.0f,
                          0.0f, 0.0f, rightX, rightZ, stretch, shade, SpriteScale(sprite), false);
            continue;
        }

        const VoxelObjectCard *const ref = snap->playerObjectEventId < OBJECT_EVENTS_COUNT
                                         ? &objects[snap->playerObjectEventId] : NULL;
        if (ref == NULL || !ref->drawn)
            continue;
        const float cx = ref->worldX + 0.5f + (float)(x - ref->screenX) / VOXEL_PIXELS_PER_TILE;
        if (effect->decal)
        {
            float cz = ref->worldZ + 0.5f + (float)(base - h / 2 - (ref->base - 8)) / VOXEL_PIXELS_PER_TILE;
            if (fabsf(cx - camera->targetX) > 24.0f || fabsf(cz - camera->targetZ) > 24.0f)
                continue;
            if (effect->surf)
            {
                const float lift = sprite->y2 < 0 ? -(float)sprite->y2 * pixel : 0.0f;
                cz -= 8.0f / VOXEL_PIXELS_PER_TILE;
                EmitDecal(builder, &sSlots[effect->slot], effect->slot, cx, cz, lift, ref->shade);
                if (shadows != NULL && ref->outdoor)
                    EmitDecalShadow(shadows, &sSlots[effect->slot], effect->slot, cx, cz,
                                    lift + VOXEL_SURF_BODY, ref->shade);
                continue;
            }
            EmitDecal(builder, &sSlots[effect->slot], effect->slot, cx, cz, 0.0f, ref->shade);
            continue;
        }
        float cz = ref->worldZ + 0.5f + (float)(base - ref->base) / VOXEL_PIXELS_PER_TILE;
        if (effect->heal)
            cz += VOXEL_HEAL_BALL_LIFT;
        if (fabsf(cx - camera->targetX) > 24.0f || fabsf(cz - camera->targetZ) > 24.0f)
            continue;
        EmitBillboard(builder, &sSlots[effect->slot], effect->slot, cx, cz, 0.0f, 0.0f,
                      -(float)sprite->y2 * pixel + (effect->heal ? VOXEL_HEAL_BALL_LIFT : 0.0f),
                      ref->deck, rightX, rightZ, stretch, ref->shade, SpriteScale(sprite), false);
    }
    return updates;
}
