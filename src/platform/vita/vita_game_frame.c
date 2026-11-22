// The game frame (include/platform/game_frame.h): captured on Core 1, pinned and read on Core 0.
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_main.h"
#include "constants/battle.h"
#include "constants/field_effects.h"
#include "constants/map_types.h"
#include "constants/quest_log.h"
#include "field_effect.h"
#include "field_message_box.h"
#include "help_system.h"
#include "main.h"
#include "palette.h"
#include "platform/game_frame.h"
#include "platform/ui.h"
#include "platform/vita_extras.h"
#include "platform/vita/vita_stage.h"
#include "port_platform.h"
#include "quest_log.h"
#include "save_failed_screen.h"
#include "text.h"
#include "window.h"

extern void CB2_Overworld(void);
extern void CB2_OverworldBasic(void);
extern void BattleMainCB2(void);

extern bool8 BattleTransition_IsActiveForUi(void);
extern bool8 Shop_IsBuyMenuActive(void);
extern bool8 Overworld_IsReturningToField(void);
extern bool8 MapNamePopup_IsActiveForUi(void);

// Group A 816x544 centered GBA-format scene queries (each lives beside its callbacks in Core 1).
extern bool8 FameChecker_IsGbaFormatForUi(void);
extern bool8 TeachyTv_IsGbaFormatForUi(void);
extern bool8 RegionMap_IsGbaFormatForUi(void);
extern bool8 BerryCrush_IsGbaFormatForUi(void);
extern bool8 PokemonJump_IsGbaFormatForUi(void);
extern bool8 DodrioBerryPicking_IsGbaFormatForUi(void);
extern bool8 ClearSaveData_IsGbaFormatForUi(void);
extern bool8 TrainerCard_IsGbaFormatForUi(void);
extern bool8 TradeMenu_IsGbaFormatForUi(void);
extern bool8 UnionRoomChat_IsGbaFormatForUi(void);
extern bool8 MysteryGift_IsGbaFormatForUi(void);
extern bool8 SlotMachine_IsGbaFormatForUi(void);
extern bool8 Diploma_IsGbaFormatForUi(void);
extern bool8 HallOfFame_IsGbaFormatForUi(void);
extern bool8 WirelessComm_IsGbaFormatForUi(void);
extern bool8 BattleRecords_IsGbaFormatForUi(void);
extern bool8 EvolutionScene_IsActiveForUi(void);
extern bool8 EggHatch_IsActiveForUi(void);
extern bool8 Trade_IsAnimActiveForUi(void);

#define CACHE_LINE 32

GameFrame gGameFrameSlots[GAME_FRAME_SLOTS] __attribute__((aligned(CACHE_LINE)));
const GameFrame *gGameFrameCore0 = &gGameFrameSlots[0];
volatile uint32_t sMapMutationLock __attribute__((aligned(CACHE_LINE)));
static int32_t sLatest __attribute__((aligned(CACHE_LINE)));
static uint32_t sMapEpoch __attribute__((aligned(CACHE_LINE)));
static int32_t sPinned __attribute__((aligned(CACHE_LINE))) = -1;

// Latched across transitions between a Group A scene and the 16:9 field/battle.
static bool8 sGbaFormatLatched;

void GameFrame_BeginMapMutation(void)
{
    __atomic_store_n(&sMapMutationLock, 1u, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&sMapEpoch, 1u, __ATOMIC_SEQ_CST);
}

void GameFrame_OnReset(void)
{
    memset(gGameFrameSlots, 0, sizeof(gGameFrameSlots));
    __atomic_store_n(&sLatest, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sMapMutationLock, 0u, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&sMapEpoch, 1u, __ATOMIC_SEQ_CST);
    sGbaFormatLatched = FALSE;
}

static bool8 GbaFormatScene(const bool8 stageActive)
{
    return (Port_SceneShown() && !stageActive)
        || FameChecker_IsGbaFormatForUi()
        || TeachyTv_IsGbaFormatForUi()
        || RegionMap_IsGbaFormatForUi()
        || BerryCrush_IsGbaFormatForUi()
        || PokemonJump_IsGbaFormatForUi()
        || DodrioBerryPicking_IsGbaFormatForUi()
        || ClearSaveData_IsGbaFormatForUi()
        || TrainerCard_IsGbaFormatForUi()
        || TradeMenu_IsGbaFormatForUi()
        || UnionRoomChat_IsGbaFormatForUi()
        || MysteryGift_IsGbaFormatForUi()
        || SlotMachine_IsGbaFormatForUi()
        || Diploma_IsGbaFormatForUi()
        || HallOfFame_IsGbaFormatForUi()
        || WirelessComm_IsGbaFormatForUi()
        || BattleRecords_IsGbaFormatForUi();
}

static bool8 WideContext(const bool8 stageActive)
{
    return gMain.callback2 == CB2_Overworld
        || gMain.callback2 == CB2_OverworldBasic
        || (gMain.inBattle && gMain.callback2 == BattleMainCB2)
        || EvolutionScene_IsActiveForUi()
        || EggHatch_IsActiveForUi()
        || Trade_IsAnimActiveForUi()
        || stageActive;
}

static bool8 GbaFormatLatched(const bool8 stageActive)
{
    if (WideContext(stageActive))
        sGbaFormatLatched = FALSE;
    else if (GbaFormatScene(stageActive))
        sGbaFormatLatched = TRUE;
    return sGbaFormatLatched;
}

#ifndef VOXEL_SUPPORTED
static void CaptureField(GameFrameField *const f)
{
    const bool8 overworldCb = (gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic);

    f->saveLoaded = (gSaveBlock1Ptr != NULL);
    f->overworldCallback = overworldCb;
    f->isOutdoor = gMapHeader.mapLayout != NULL
                && (gMapHeader.mapType == MAP_TYPE_TOWN || gMapHeader.mapType == MAP_TYPE_CITY
                 || gMapHeader.mapType == MAP_TYPE_ROUTE || gMapHeader.mapType == MAP_TYPE_OCEAN_ROUTE);
    f->underground = gMapHeader.mapLayout != NULL && gMapHeader.mapType == MAP_TYPE_UNDERGROUND;
    f->fieldShown = overworldCb;
    f->battleSceneShown = (gMain.inBattle && gMain.callback2 == BattleMainCB2);
    f->questLogPlayback = QL_IS_PLAYBACK_STATE;
    f->helpSystemActive = HelpSystem_IsActiveForUi();
    f->saveFailedActive = SaveFailed_IsActiveForUi();
    if (gSaveBlock1Ptr != NULL)
    {
        f->mapGroup = (s16)gSaveBlock1Ptr->location.mapGroup;
        f->mapNum = (s16)gSaveBlock1Ptr->location.mapNum;
        f->mapLayoutId = gSaveBlock1Ptr->mapLayoutId;
    }
    else
    {
        f->mapGroup = -1;
        f->mapNum = -1;
        f->mapLayoutId = 0;
    }
    f->mapEpoch = __atomic_load_n(&sMapEpoch, __ATOMIC_RELAXED);
    f->callback2 = (const void *)gMain.callback2;
}
#endif

static void CaptureOverlay(GameFrameOverlay *const o, const GameFrameField *const f, const bool8 voxel)
{
    o->battleTransition = BattleTransition_IsActiveForUi();
    o->buyMenu = Shop_IsBuyMenuActive();
    o->returningToField = Overworld_IsReturningToField();
    o->fieldMoveShowMon = FieldEffectActiveListContains(FLDEFF_FIELD_MOVE_SHOW_MON)
                       || FieldEffectActiveListContains(FLDEFF_FIELD_MOVE_SHOW_MON_INIT);
    o->flyAnim = FieldEffectActiveListContains(FLDEFF_FLY_OUT)
              || FieldEffectActiveListContains(FLDEFF_FLY_IN);
    o->messageBox = (GetFieldMessageBoxType() != FIELD_MESSAGE_BOX_HIDDEN);
    o->mapNamePopUp = MapNamePopup_IsActiveForUi();
    o->textPrinter0 = (IsTextPrinterActive(0) != 0);
    o->bg0Window = FALSE;
    for (int32_t w = 1; w < WINDOWS_MAX; w++)
    {
        if (gWindows[w].window.bg == 0 && gWindows[w].tileData != NULL)
        {
            o->bg0Window = TRUE;
            break;
        }
    }
    VitaStage_CaptureFrame(&o->stageActive);
    o->gbaWindow = UiCore_SceneInWindow();
    // Stealth overlays (Help System, Save Failed) and Quest Log playback run without leaving
    // CB2_Overworld, so check them explicitly in addition to the Group A callback latch.
    o->gbaFormat = o->gbaWindow
                || f->helpSystemActive
                || f->saveFailedActive
                || f->questLogPlayback
                || GbaFormatLatched(o->stageActive);
    if (o->stageActive)
        memcpy(o->bgPaletteFaded, gPlttBufferFaded, sizeof(o->bgPaletteFaded));
    if (voxel)
    {
        memcpy(o->objPaletteUnfaded, &gPlttBufferUnfaded[256], sizeof(o->objPaletteUnfaded));
        memcpy(o->objPaletteExtUnfaded, &gPlttBufferUnfaded[PLTT_EXT_ID(0)], sizeof(o->objPaletteExtUnfaded));
    }
}

#ifndef VOXEL_SUPPORTED
static void CaptureBattle(GameFrameBattle *const b)
{
    b->inBattle = gMain.inBattle;
    b->isLink = (gBattleTypeFlags & BATTLE_TYPE_LINK) != 0;
    b->isWild = gMain.inBattle && (gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK)) == 0;
}
#endif

void GameFrame_CaptureAndPublish(void)
{
    const int32_t latest = __atomic_load_n(&sLatest, __ATOMIC_RELAXED);
    const int32_t pinned = __atomic_load_n(&sPinned, __ATOMIC_SEQ_CST);
    int32_t slot = (latest + 1) % GAME_FRAME_SLOTS;

    while (slot == latest || slot == pinned)
        slot = (slot + 1) % GAME_FRAME_SLOTS;

    GameFrame *const frame = &gGameFrameSlots[slot];
    const GameFrame *const previous = &gGameFrameSlots[latest];
    const u32 seq = previous->sequence + 1u;
#ifdef VOXEL_SUPPORTED
    const bool8 voxel = (VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0);
#else
    const bool8 voxel = FALSE;
#endif

    memset(&frame->field, 0, sizeof(frame->field));
    memset(&frame->overlay, 0, sizeof(frame->overlay));
    memset(&frame->battle, 0, sizeof(frame->battle));
    frame->sequence = seq;
    frame->voxel = voxel;
    frame->field.mapEpoch = __atomic_load_n(&sMapEpoch, __ATOMIC_RELAXED);
#ifdef VOXEL_SUPPORTED
    /* Capture field metadata (helpSystemActive, saveFailedActive, questLogPlayback) first so
     * CaptureOverlay sees them when computing gbaFormat. */
    VoxelWorld_CaptureField(&frame->field, &frame->weather, &frame->fade, voxel);
    CaptureOverlay(&frame->overlay, &frame->field, voxel);
    VoxelBattle_Capture(&frame->battle, voxel);
    if (voxel)
        VoxelEntities_Capture(&frame->entities, &previous->entities, frame->overlay.buyMenu);
#else
    memset(&frame->weather, 0, sizeof(frame->weather));
    memset(&frame->fade, 0, sizeof(frame->fade));
    CaptureField(&frame->field);
    CaptureOverlay(&frame->overlay, &frame->field, voxel);
    CaptureBattle(&frame->battle);
#endif

    __atomic_store_n(&sLatest, slot, __ATOMIC_SEQ_CST);
    // Hold Core 0 off the map layout until a frame of the new map is published.
    __atomic_store_n(&sMapMutationLock, 0u, __ATOMIC_RELEASE);
}

const GameFrame *GameFrame_LastPublished(void)
{
    return &gGameFrameSlots[__atomic_load_n(&sLatest, __ATOMIC_RELAXED)];
}

void GameFrame_Pin(void)
{
    int32_t idx = __atomic_load_n(&sLatest, __ATOMIC_SEQ_CST);

    for (;;)
    {
        __atomic_store_n(&sPinned, idx, __ATOMIC_SEQ_CST);
        const int32_t check = __atomic_load_n(&sLatest, __ATOMIC_SEQ_CST);
        if (check == idx)
            break;
        idx = check;
    }
    gGameFrameCore0 = &gGameFrameSlots[idx];
}

void GameFrame_Unpin(void)
{
    __atomic_store_n(&sPinned, -1, __ATOMIC_RELEASE);
}

bool8 GameFrame_MapSettled(void)
{
    if (__atomic_load_n(&sMapMutationLock, __ATOMIC_ACQUIRE) != 0u)
        return FALSE;
    return GameFrame_Get()->field.mapEpoch == __atomic_load_n(&sMapEpoch, __ATOMIC_ACQUIRE);
}
