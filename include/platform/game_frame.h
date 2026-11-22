#ifndef GUARD_PLATFORM_GAME_FRAME_H
#define GUARD_PLATFORM_GAME_FRAME_H

/*
 * The game frame: everything the other cores may know about the game, as it stood at the end of
 * one game frame (docs/reference/THREADS.md).
 *
 * The game runs on Core 1 alone. At each VBlank, Core 1 captures what Core 0 needs into the next
 * free slot of a 4-slot ring and publishes it atomically. Core 0 pins the latest published frame
 * once per rendered frame (GameFrame_Pin) and reads only that pinned copy, so every pass of a
 * rendered frame sees a consistent game frame and Core 1 never writes the slot Core 0 reads.
 */

#include <stdint.h>
#include "global.h"
#include "palette.h"
#ifdef VOXEL_SUPPORTED
#include "platform/voxel_snapshot.h"
#endif

#define GAME_FRAME_BATTLERS 4
#define GAME_FRAME_SLOTS    4

// The field: which map, and whether the field is what the screen shows.
typedef struct
{
    bool8 saveLoaded;            // gSaveBlock1Ptr is set
    bool8 fieldShown;            // the overworld is on screen
    bool8 overworldCallback;     // CB2_Overworld / CB2_OverworldBasic
    bool8 battleSceneShown;      // in a battle, on BattleMainCB2
    bool8 questLogPlayback;      // QL_IS_PLAYBACK_STATE
    bool8 helpSystemActive;      // HelpSystem_IsActiveForUi
    bool8 saveFailedActive;      // SaveFailed_IsActiveForUi
    bool8 isOutdoor;             // overworld outdoor map type (for Core 0 day/night)
    bool8 underground;           // MAP_TYPE_UNDERGROUND (for Core 0 voxel indoor/outdoor cave)
    s16 mapGroup;                // -1 without a save
    s16 mapNum;
    u16 mapLayoutId;             // gSaveBlock1Ptr->mapLayoutId
    u32 mapEpoch;                // map mutations begun before this frame (GameFrame_BeginMapMutation)
    u32 layoutSwapMask;          // VoxelWorld: bit i set when its layout swap i applies
    float playerTileX;           // the player's map-local tile (object event coordinates)
    float playerTileZ;
    u8 playerFacing;             // DIR_*
    bool8 playerSurfing;
    const void *callback2;       // gMain.callback2, compared, never called
} GameFrameField;

// Weather, as the voxel world shows it.
typedef struct
{
    u8 weatherClass;             // VoxelWeatherClass
    float amount;                // 0..1, the sprites coming in and out
    float lightning;             // 0..1
    float fogDensity;            // 0..1
} GameFrameWeather;

// The screen fade the world follows (VoxelWorld_ScreenFade).
typedef struct
{
    bool8 fading;
    float amount;
    float rgb[3];
} GameFrameFade;

// What the 2D layer over the world shows (Pass_Overlay2D, RenderGameTexture).
typedef struct
{
    bool8 battleTransition;
    bool8 buyMenu;
    bool8 returningToField;
    bool8 fieldMoveShowMon;      // FLDEFF_FIELD_MOVE_SHOW_MON(_INIT)
    bool8 flyAnim;               // FLDEFF_FLY_OUT / FLDEFF_FLY_IN
    bool8 messageBox;            // GetFieldMessageBoxType() != hidden
    bool8 mapNamePopUp;
    bool8 textPrinter0;          // IsTextPrinterActive(0)
    bool8 bg0Window;             // a window other than 0 on BG0 holds tiles
    bool8 stageActive;
    bool8 gbaFormat;
    bool8 gbaWindow;
    u16 bgPaletteFaded[256];     // gPlttBufferFaded's BG half
    u16 objPaletteUnfaded[256];  // gPlttBufferUnfaded's OBJ half
    u16 objPaletteExtUnfaded[PLTT_EXT_COUNT * 16];  // extended sprite palettes, unfaded
} GameFrameOverlay;

// The battle.
typedef struct
{
    bool8 inBattle;              // gMain.inBattle
    bool8 isLink;                // BATTLE_TYPE_LINK
    bool8 isWild;                // not a trainer or link battle
    bool8 critical;              // gCritMultiplier > 1
    bool8 moveBgShown;           // CtrBattleBg_MoveBgShown()
    bool8 introSliding;          // CtrBattleIntro_Sliding()
    u8 moveType;                 // the type of the move being used (TYPE_NORMAL when unknown)
    u16 hp[GAME_FRAME_BATTLERS];
    u16 maxHp[GAME_FRAME_BATTLERS];
    u32 shadowCount;
    struct { float x, y, rx, ry; } shadows[GAME_FRAME_BATTLERS];
} GameFrameBattle;

typedef struct
{
    u32 sequence;                // counts the published frames
    bool8 voxel;                 // captured with the 3D field on: entities, weather, fade, sprite palettes
    GameFrameField field;
    GameFrameWeather weather;
    GameFrameFade fade;
    GameFrameOverlay overlay;
    GameFrameBattle battle;
#ifdef VOXEL_SUPPORTED
    VoxelEntitySnapshot entities;
#endif
} __attribute__((aligned(32))) GameFrame;

// Set by Core 1 at map load start (overworld.c) and released once the new map's frame publishes.
extern volatile uint32_t sMapMutationLock;

// ── Core 1 (the game thread) ─────────────────────────────────────────────────
#ifdef VOXEL_SUPPORTED
void VoxelWorld_CaptureField(GameFrameField *field, GameFrameWeather *weather, GameFrameFade *fade, bool8 voxel);
void VoxelBattle_Capture(GameFrameBattle *battle, bool8 voxel);
#endif
void GameFrame_BeginMapMutation(void);
void GameFrame_CaptureAndPublish(void);
const GameFrame *GameFrame_LastPublished(void);
void GameFrame_OnReset(void);

// ── Core 0 (presentation thread) ─────────────────────────────────────────────
void GameFrame_Pin(void);
void GameFrame_Unpin(void);
bool8 GameFrame_MapSettled(void);

extern GameFrame gGameFrameSlots[GAME_FRAME_SLOTS];
extern const GameFrame *gGameFrameCore0;

// Inline accessor for Core 0 only (tools/core_boundary fails if Core 1 reads gGameFrameCore0).
static inline const GameFrame *GameFrame_Get(void)
{
    return gGameFrameCore0;
}

#endif // GUARD_PLATFORM_GAME_FRAME_H
