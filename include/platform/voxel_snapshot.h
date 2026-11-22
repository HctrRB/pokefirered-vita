#ifndef GUARD_VOXEL_SNAPSHOT_H
#define GUARD_VOXEL_SNAPSHOT_H

#include "global.h"
#include "global.fieldmap.h"
#include "sprite.h"

#ifndef OAM_MATRIX_COUNT
#define OAM_MATRIX_COUNT 32
#endif

#define VOXEL_SNAPSHOT_DOORS 2
#define VOXEL_SNAPSHOT_WILDS 6

typedef struct
{
    bool8 active;
    u8 progress;
    u8 spriteId;
    s8 dx;
    s8 dy;
    u16 nationalSpecies;
    s16 x;
    s16 y;
} VoxelWildSnapshot;

// The entities of one game frame, part of the game frame (include/platform/game_frame.h):
// captured on Core 1 (VoxelEntities_Capture), drawn by Core 0 from this copy alone.
typedef struct
{
    struct ObjectEvent objectEvents[OBJECT_EVENTS_COUNT];
    struct ObjectEventGraphicsInfo graphicsInfo[OBJECT_EVENTS_COUNT];
    struct Sprite sprites[MAX_SPRITES];
    u8 extPalettes[MAX_SPRITES];        // GetSpriteExtPalette, 0xFF without one
    u16 tileTags[MAX_SPRITES];          // GetSpriteTileTagByTileStart of affine sprites, else 0xFFFF
    u64 visibleWild;                    // bit n: sprite n is a visible wild Pokemon
    s16 wildX16[MAX_SPRITES];           // map-local 1/16-tile coords of visible wild sprites
    s16 wildY16[MAX_SPRITES];
    u64 weatherSprites;                 // bit n: sprite n is drawn by the weather (CtrSprite_IsVoxelWeather)
    u64 cloudSprites;                   // bit n: sprite n is a cloud (CtrSprite_IsVoxelCloud)
    u64 healSprites;                    // bit n: sprite n is a ball of the Pokecenter healing machine
    u16 healPalette[16];                // faded palette of healing machine balls
    s16 spriteCoordOffsetX;             // gSpriteCoordOffsetX/Y: takes screen sprites to objects' frame
    s16 spriteCoordOffsetY;
    struct OamMatrix oamMatrices[OAM_MATRIX_COUNT];
    u8 playerObjectEventId;             // gPlayerAvatar.objectEventId
    u8 doorCount;
    u8 doorWidth;
    u8 doorHeight;
    s16 doorX[VOXEL_SNAPSHOT_DOORS];
    s16 doorY[VOXEL_SNAPSHOT_DOORS];
    const u8 *doorTiles;                // ROM
    const u8 *doorPalettes;             // ROM
    bool8 playerFreeActive;
    s16 playerFreeDx;
    s16 playerFreeDy;
    float playerWorldX;
    float playerWorldZ;
    int16_t mapGroup;
    int16_t mapNum;
    u8 mapType;                         // gMapHeader.mapType (MAP_TYPE_*)
    bool8 followerEnabled;              // CtrPlayer_Follower() & not surfing/biking/invisible/QL
    u8 playerFacing;                    // player object facingDirection
    u16 followerSpecies;                // national dex of lead non-egg non-fainted party mon
    bool8 wildSpritesAllowed;           // !inBattle && !QL_IS_PLAYBACK_STATE
    VoxelWildSnapshot wilds[VOXEL_SNAPSHOT_WILDS];
    uint32_t frameNum;
} VoxelEntitySnapshot;

// Core 1: fills the entities of the game frame being captured. `keepPrevious`: the shop buy
// menu is open, so the field behind it keeps its prior entity snapshot.
void VoxelEntities_Capture(VoxelEntitySnapshot *snap, const VoxelEntitySnapshot *previous, bool8 keepPrevious);
// Core 0: the entities of the pinned game frame.
const VoxelEntitySnapshot *VoxelEntities_GetVisibleSnapshot(void);

#ifdef VOXEL_SUPPORTED
void VoxelFlyGame_OnWarpLoaded(void);
#endif

#endif // GUARD_VOXEL_SNAPSHOT_H
