#ifndef PLATFORM_UI_H
#define PLATFORM_UI_H

// Native 960x544 PS Vita UI system (Phase F7: Stage 2).
// Logic runs on Core 1 (the game thread), drawing on Core 0. Core 1 emits ready quads
// into a lock-free buffer; Core 0 draws the latest published list over 3D/2D before swap.

#include <stdbool.h>
#include <stdint.h>

// Core 0 at boot before AgbMain starts: loads assets/ui2/ui_atlas.bin.
void Ui_Init(void);
void Ui_Shutdown(void);

bool Ui_IsEnabled(void);
void Ui_SetEnabled(bool enabled);

// Core 1 / Any: true when user selected Classic GBA UI mode in settings.
bool Ui_IsClassicGba(void);

// GBA text window frame (0-9 for FireRed type1..type10) matching current theme.
uint8_t Ui_GbaWindowFrame(void);

// Framed GBA sub-scene geometry (3:2 centered 600x400 window).
#define UI_SCENE_WIN_X 180
#define UI_SCENE_WIN_Y 104
#define UI_SCENE_WIN_W 600
#define UI_SCENE_WIN_H 400
bool UiCore_SceneInWindow(void);

// Core 1, once per game frame from VBlankIntrWait.
void Ui_Tick(void);

// Core 1 during SoftReset(): clears open screens, zone signs and draw lists.
void Ui_OnReset(void);

// Core 1: START in the field opens the native hub (else the game's START menu).
bool Ui_OwnsFieldStart(void);

// True while a native UI screen is open (safe lock-free read from any thread).
bool Ui_IsOpen(void);

// Core 1 (Platform_GetKeyInput): true while native UI consumes pad buttons.
bool Ui_ConsumesInput(void);

// Core 0: renders the latest published UI list over the current framebuffer.
void Ui_Render(int32_t viewWidth, int32_t viewHeight);

// Hosted entrypoints (F7a/F7b/F7c).
bool Ui_OpenOptionsHosted(void (*after)(void));

// Core 1 (map_name_popup.c): Kanto/Sevii zone sign banner with optional floor number.
void Ui_ShowZoneSign(uint16_t mapSec, int8_t floorNum);
void Ui_HideZoneSign(void);

// Core 0: true while mirrored game windows cover the active GBA boxes.
bool Ui_MirrorCovers(void);

// Core 1 (Platform_GetKeyInput): GBA keys injected once by a touch on a mirrored prompt.
uint16_t Ui_TakeInjectedKeys(void);

// Core 0: DISPCNT_BG*_ON mask cleared when native battle HUD owns battle layers (F7b).
uint16_t Ui_BattleDispcntClear(void);

// Battle and Pokédex native entrypoints (F7b-2).
struct Pokemon;
bool Ui_OpenBattleParty(void);
bool Ui_OpenBattleBag(void);
bool Ui_OpenCaughtDexPage(uint16_t species);
bool Ui_OpenBerryChoice(void (*exitCallback)(void));
bool Ui_OpenMart(uint8_t shopTaskId, bool sell, uint8_t martType, const uint16_t *items, uint16_t count);
bool Ui_OpenTownMap(bool wallMap, void (*exitCallback)(void));

// Sub-Fase F7c: ChooseMon, ForgetMove/MoveRelearner, Bill's PC, Player's PC, Naming, EasyChat
enum { UI_CHOOSE_TUTOR = 0, UI_CHOOSE_DAYCARE, UI_CHOOSE_ANY, UI_CHOOSE_MINIGAME, UI_CHOOSE_RELEARNER, UI_CHOOSE_MULTI_BATTLE, UI_CHOOSE_COUNT };
bool Ui_OpenChooseMon(int kind);
bool Ui_OpenForgetMove(struct Pokemon *mons, uint8_t monIndex, uint16_t newMove, void (*callback)(void));
bool Ui_OpenMoveRelearner(void);
bool Ui_OpenPc(uint8_t option);
enum { UI_PC_TAB_ITEMS = 0, UI_PC_TAB_MAIL = 1 };
enum { UI_PC_CENTER = 0, UI_PC_BEDROOM = 1 };
bool Ui_OpenPlayerPc(uint8_t taskId, uint8_t tab, uint8_t kind);
bool Ui_OpenNaming(uint8_t templateNum, uint8_t *destBuffer, uint16_t species, uint16_t gender, uint32_t personality, void (*returnCallback)(void));
bool Ui_OpenEasyChat(uint8_t type, uint16_t *words, void (*exitCallback)(void));

// Core 1: apply persisted GBA options (text speed, battle scene/style, sound, button mode) from settings.txt.
void UiGame_ApplySavedGbaOptions(void);

extern volatile bool gUiVoxelFrame;

#endif // PLATFORM_UI_H
