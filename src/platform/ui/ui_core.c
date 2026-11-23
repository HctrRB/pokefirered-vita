// Core of the 960x544 native UI on Core 1 (the game thread).
// Ui_Tick runs once per game frame from VBlankIntrWait between two game frames.

#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "platform/vita_extras.h"
#include "platform/vita_controls.h"
#include "../vita/vita_platform.h"
#include "../vita/vita_perf.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "event_object_movement.h"
#include "field_fadetransition.h"
#include "field_screen_effect.h"
#include "field_specials.h"
#include "field_tasks.h"
#include "field_weather.h"
#include "help_system.h"
#include "link.h"
#include "main.h"
#include "menu_helpers.h"
#include "overworld.h"
#include "palette.h"
#include "quest_log.h"
#include "scanline_effect.h"
#include "script.h"
#include "sound.h"
#include "task.h"
#pragma GCC diagnostic pop
#include "constants/rgb.h"
#include "constants/songs.h"

#define REPEAT_DELAY 18U
#define REPEAT_RATE 5U
#define DRAG_THRESHOLD 6

static bool sEnabled = true;
static bool sOpen;
#define UI_NAV_DEPTH 8
static const UiScreen *sStack[UI_NAV_DEPTH];
static int32_t sDepth;
static bool sCloseRequested;
static bool sFromScript;
static void (*sAfterClose)(void);
static UiInput sInput;
static uint32_t sPrevHeld = ~0u;
static uint32_t sRepeatTicks;
static bool sPrevTouch;
static bool sIgnoreTouch;
static uint32_t sCircleTicks;
static int32_t sLastTy;
static float sAvgTickMs;
static bool sHosted;
static uint32_t sRenderMsBits;
static bool sSceneWindow;

static void (*sSceneStart)(void);
static void (*sReopenPrepare)(void);
static const UiScreen *sSavedStack[UI_NAV_DEPTH];
static int32_t sSavedDepth;
static const char *sSceneTitle;

void Ui_Init(void)
{
    sEnabled = true;
    __atomic_store_n(&sOpen, false, __ATOMIC_RELAXED);
    __atomic_store_n(&sSceneWindow, false, __ATOMIC_RELAXED);
    sDepth = 0;
    sSavedDepth = 0;
    UiTheme_Refresh();
    UiAssets_Load();
    // Before the boot snapshot and the game thread: the catalog is immutable once Core 1 runs.
    UiLocale_Load();
    UiDraw_Begin();
    UiDraw_Publish(0.0f);
}

void Ui_Shutdown(void)
{
    UiAssets_Unload();
    __atomic_store_n(&sOpen, false, __ATOMIC_RELAXED);
    __atomic_store_n(&sSceneWindow, false, __ATOMIC_RELAXED);
}

bool Ui_IsEnabled(void)
{
    return sEnabled && UiAssets_Ready();
}

bool Ui_IsClassicGba(void)
{
    return VitaExtras_IsClassicGbaUi();
}

void Ui_SetEnabled(const bool enabled)
{
    sEnabled = enabled;
    if (!enabled && Ui_IsOpen())
        Ui_OnReset();
}

bool Ui_IsOpen(void)
{
    return __atomic_load_n(&sOpen, __ATOMIC_RELAXED);
}

bool Ui_ConsumesInput(void)
{
    return Ui_IsOpen();
}

bool UiCore_SceneInWindow(void)
{
    return __atomic_load_n(&sSceneWindow, __ATOMIC_RELAXED) && gMain.callback2 != CB2_Overworld;
}

float UiCore_LastRenderMs(void)
{
    float ms = 0.0f;
    const uint32_t bits = __atomic_load_n(&sRenderMsBits, __ATOMIC_RELAXED);
    memcpy(&ms, &bits, sizeof(ms));
    return ms;
}

void UiCore_SetLastRenderMs(const float ms)
{
    uint32_t bits = 0U;
    memcpy(&bits, &ms, sizeof(bits));
    __atomic_store_n(&sRenderMsBits, bits, __ATOMIC_RELAXED);
}

float UiCore_AvgTickMs(void)
{
    return sAvgTickMs;
}

static void ReadInput(void)
{
    UiInput *const in = &sInput;
    const uint32_t held = VitaPlatform_ReadButtons();
    const uint32_t prev = (sPrevHeld == ~0u) ? held : sPrevHeld;
    in->held = held;
    in->pressed = held & ~prev;
    in->repeat = in->pressed;
    const uint32_t dpad = VITA_BTN_UP | VITA_BTN_DOWN | VITA_BTN_LEFT | VITA_BTN_RIGHT;
    if (held & dpad)
    {
        if (++sRepeatTicks >= REPEAT_DELAY && (sRepeatTicks - REPEAT_DELAY) % REPEAT_RATE == 0U)
            in->repeat |= held & dpad;
    }
    else
    {
        sRepeatTicks = 0U;
    }
    sPrevHeld = held;

    int32_t tx = 0, ty = 0;
    const bool touch = VitaPlatform_ReadTouch(&tx, &ty);
    if (sIgnoreTouch)
    {
        if (!touch)
            sIgnoreTouch = false;
        in->touching = in->touchDown = in->tap = in->dragging = false;
        in->dragDy = 0;
        sPrevTouch = false;
        return;
    }
    in->touchDown = touch && !sPrevTouch;
    in->tap = false;
    in->dragDy = 0;
    if (in->touchDown)
    {
        in->downX = tx;
        in->downY = ty;
        in->dragging = false;
        sLastTy = ty;
    }
    if (touch)
    {
        if (!in->dragging && (tx - in->downX > DRAG_THRESHOLD || in->downX - tx > DRAG_THRESHOLD
                              || ty - in->downY > DRAG_THRESHOLD || in->downY - ty > DRAG_THRESHOLD))
            in->dragging = true;
        if (in->dragging)
            in->dragDy = ty - sLastTy;
        sLastTy = ty;
        in->tx = tx;
        in->ty = ty;
    }
    else if (sPrevTouch && !in->dragging)
    {
        in->tap = true;
    }
    in->touching = touch;
    sPrevTouch = touch;
}

bool Ui_OwnsFieldStart(void)
{
    if (Ui_IsClassicGba())
        return false;
    return Ui_IsEnabled()
        && !MenuHelpers_IsLinkActive()
        && !InUnionRoom()
        && !IsUpdateLinkStateCBActive()
        && !QL_IS_PLAYBACK_STATE
        && !HelpSystem_IsActiveForUi();
}

static bool CanOpenFromField(void)
{
    return UiGame_InField();
}

static void EnterTop(void)
{
    sPrevHeld = ~0u;
    sPrevTouch = true;
    sIgnoreTouch = true;
    if (sStack[sDepth - 1]->enter)
        sStack[sDepth - 1]->enter();
}

void UiNav_Push(const UiScreen *const screen)
{
    if (sDepth >= UI_NAV_DEPTH)
        return;
    sStack[sDepth++] = screen;
    EnterTop();
}

void UiNav_Replace(const UiScreen *const screen)
{
    if (sDepth == 0)
        return;
    sStack[sDepth - 1] = screen;
    EnterTop();
}

void UiNav_Pop(void)
{
    if (sDepth > 0)
        sDepth--;
    if (sDepth == 0)
    {
        sCloseRequested = true;
    }
    else
    {
        sPrevHeld = ~0u;
        sPrevTouch = true;
        sIgnoreTouch = true;
        if (sStack[sDepth - 1]->resume)
            sStack[sDepth - 1]->resume();
    }
}

void UiNav_Close(void (*const afterClose)(void))
{
    sCloseRequested = true;
    sAfterClose = afterClose;
}

static void Open(const UiScreen *const screen)
{
    if (!sFromScript)
    {
        UiGame_FieldLock();
        Ui_HideZoneSign();
    }
    sDepth = 0;
    sCloseRequested = false;
    sAfterClose = NULL;
    UiKit_ClearToast();
    UiNav_Push(screen);
    __atomic_store_n(&sOpen, true, __ATOMIC_RELAXED);
    PlaySE(SE_WIN_OPEN);
}

void UiCore_OpenFromScript(const UiScreen *const screen, void (*const afterClose)(void))
{
    if (Ui_IsOpen())
        return;
    sFromScript = true;
    Open(screen);
    sAfterClose = afterClose;
}

static void CB2_UiHost(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

bool UiCore_IsHostCallback(void)
{
    return gMain.callback2 == CB2_UiHost;
}

void UiCore_OpenHosted(const UiScreen *const screen, void (*const afterClose)(void))
{
    if (Ui_IsOpen())
        return;
    SetMainCallback2(CB2_UiHost);
    sHosted = true;
    sFromScript = true;
    Open(screen);
    sAfterClose = afterClose;
}

static void Task_FadeToScene(u8 taskId)
{
    if (gTasks[taskId].data[0] == 0)
    {
        StopPokemonLeagueLightingEffectTask();
        FadeScreen(FADE_TO_BLACK, 0);
        gTasks[taskId].data[0] = 1;
        return;
    }
    if (gPaletteFade.active)
        return;
    DestroyTask(taskId);
    void (*const start)(void) = sSceneStart;
    sSceneStart = NULL;
    PlayRainStoppingSoundEffect();
    CleanupOverworldWindowsAndTilemaps();
    ScanlineEffect_Stop();
    ResetTasks();
    UiGame_MarkSceneReturn();
    gMain.state = 0;
    if (start != NULL)
        start();
}

static void FadeToScene(void)
{
    LockPlayerFieldControls();
    FreezeObjectEvents();
    CreateTask(Task_FadeToScene, 80);
}

static void CloseToScene(void (*const start)(void), const bool window, const char *const title)
{
    for (int32_t i = 0; i < sDepth && i < UI_NAV_DEPTH; i++)
        sSavedStack[i] = sStack[i];
    sSavedDepth = (sHosted || gMain.callback2 != CB2_Overworld) ? 0 : sDepth;
    sSceneStart = start;
    __atomic_store_n(&sSceneWindow, window, __ATOMIC_RELAXED);
    sSceneTitle = title;
    UiNav_Close(FadeToScene);
}

void UiCore_CloseToScene(void (*const start)(void))
{
    CloseToScene(start, false, NULL);
}

void UiCore_CloseToSceneInWindow(void (*const start)(void), const char *const title)
{
    CloseToScene(start, true, title);
}

static void RestoreStack(void)
{
    UiGame_FieldLock();
    sFromScript = false;
    sHosted = false;
    sCloseRequested = false;
    sAfterClose = NULL;
    UiKit_ClearToast();
    for (int32_t i = 0; i < sSavedDepth; i++)
        sStack[i] = sSavedStack[i];
    sDepth = sSavedDepth;
    sSavedDepth = 0;
    sPrevHeld = ~0u;
    sPrevTouch = true;
    sIgnoreTouch = true;
    for (int32_t i = 0; i < sDepth; i++)
        if (sStack[i]->resume)
            sStack[i]->resume();
    __atomic_store_n(&sOpen, true, __ATOMIC_RELAXED);
}

bool UiCore_TryRestoreAfterScene(void)
{
    __atomic_store_n(&sSceneWindow, false, __ATOMIC_RELAXED);
    if (sSavedDepth > 0 && Ui_IsEnabled() && !Ui_IsOpen())
    {
        RestoreStack();
        return true;
    }
    sSavedDepth = 0;
    return false;
}

static void Task_ReopenAfterField(u8 taskId)
{
    if (gPaletteFade.active)
        return;
    DestroyTask(taskId);
    __atomic_store_n(&sSceneWindow, false, __ATOMIC_RELAXED);
    if (sSavedDepth > 0 && Ui_IsEnabled() && !Ui_IsOpen())
    {
        RestoreStack();
        if (sReopenPrepare != NULL)
            sReopenPrepare();
    }
    else
    {
        sSavedDepth = 0;
        UiGame_FieldUnlock();
    }
    sReopenPrepare = NULL;
}

static void FieldCB_ReopenAfterField(void)
{
    LockPlayerFieldControls();
    FadeInFromBlack();
    CreateTask(Task_ReopenAfterField, 80);
}

void UiCore_ReopenAfterField(void (*const prepare)(void))
{
    sReopenPrepare = prepare;
    gFieldCallback = FieldCB_ReopenAfterField;
    SetMainCallback2(CB2_ReturnToField);
}

static bool DrawSceneWindow(void)
{
    if (!UiCore_SceneInWindow())
        return false;
    const int32_t x = UI_SCENE_WIN_X, y = UI_SCENE_WIN_Y, w = UI_SCENE_WIN_W, h = UI_SCENE_WIN_H, e = 6;
    UiDraw_Rect(0, 0, UI_SCREEN_W, y - e, UI_COL_FRAME1);
    UiDraw_Rect(0, y + h + e, UI_SCREEN_W, UI_SCREEN_H - y - h - e, UI_COL_FRAME1);
    UiDraw_Rect(0, y - e, x - e, h + 2 * e, UI_COL_FRAME1);
    UiDraw_Rect(x + w + e, y - e, UI_SCREEN_W - x - w - e, h + 2 * e, UI_COL_FRAME1);
    UiDraw_Frame(x - e, y - e, w + 2 * e, h + 2 * e, e, gUiTheme->paperEdge);
    UiDraw_Frame(x - 2, y - 2, w + 4, h + 4, 2, gUiTheme->selEdge);
    if (sSceneTitle != NULL)
    {
        const int32_t tw = UiText_Width(UI_FACE_PS2P, 16, sSceneTitle) + 32;
        UiW_Capsule((UI_SCREEN_W - tw) / 2, y - e - 44, tw, 34);
        UiText_DrawMid(UI_FACE_PS2P, 16, (UI_SCREEN_W - tw) / 2 + 16, y - e - 44, 34, sSceneTitle, UI_COL_TITLE);
    }
    return true;
}

void UiCore_OpenScreen(const UiScreen *const screen)
{
    if (!Ui_IsOpen())
        Open(screen);
}

static void Close(void)
{
    __atomic_store_n(&sOpen, false, __ATOMIC_RELAXED);
    sDepth = 0;
    sCloseRequested = false;
    if (!sFromScript)
        UiGame_FieldUnlock();
    sFromScript = false;
    sHosted = false;
    UiGame_ViewParty();
    UiGame_BattleMenuEnd();
    UiStall_Closed();
    UiDraw_Begin();
    UiDraw_Publish(0.0f);
}

void Ui_OnReset(void)
{
    __atomic_store_n(&sOpen, false, __ATOMIC_RELAXED);
    __atomic_store_n(&sSceneWindow, false, __ATOMIC_RELAXED);
    sDepth = 0;
    sCloseRequested = false;
    sFromScript = false;
    sHosted = false;
    sAfterClose = NULL;
    sSceneStart = NULL;
    sReopenPrepare = NULL;
    sSavedDepth = 0;
    sPrevHeld = ~0u;
    (void)Ui_TakeInjectedKeys();
    UiGame_ViewParty();
    UiGame_BattleMenuEnd();
    UiBattle_Reset();
    UiGame_ResetState();
    UiKit_ClearToast();
    UiSign_Reset();
    UiStall_Closed();
    UiDraw_Begin();
    UiDraw_Publish(0.0f);
}

void Ui_Tick(void)
{
    UiTheme_Refresh();
    UiBattle_Tick();
    if (!Ui_IsOpen())
    {
        if (!Ui_IsEnabled())
            return;

        static bool sOverlayShown;
        UiDraw_Begin();
        UiSpr_BeginTick();
        bool any = DrawSceneWindow();
        any |= UiSign_Draw();
        any |= UiBattle_Active() ? UiBattle_Draw() : UiMirror_Draw();
        if (any || sOverlayShown)
            UiDraw_Publish(0.0f);
        sOverlayShown = any;

        const uint32_t held = VitaPlatform_ReadButtons();
        const uint16_t mappedHeld = VitaControls_MapButtons(held);
        const uint16_t prevMapped = (sPrevHeld == ~0u) ? mappedHeld : VitaControls_MapButtons(sPrevHeld);
        const bool resetPrimer = (mappedHeld & (A_BUTTON | B_BUTTON | SELECT_BUTTON)) == (A_BUTTON | B_BUTTON | SELECT_BUTTON);
        const bool startNew = !resetPrimer && (mappedHeld & START_BUTTON) != 0U && (prevMapped & START_BUTTON) == 0U && sPrevHeld != ~0u;
        const bool menuNew = VitaControls_MenuPressed(held) && !VitaControls_MenuPressed(sPrevHeld) && sPrevHeld != ~0u;
        sPrevHeld = held;

        if (startNew && CanOpenFromField() && Ui_OwnsFieldStart())
            Open(&gUiHubScreen);
        else if (menuNew && CanOpenFromField())
            Open(&gUiOptionsScreen);
        if (!Ui_IsOpen())
            return;
    }

    const double t0 = VitaPerf_NowMs();
    ReadInput();
    if (gMain.callback2 != CB2_Overworld && !(gMain.inBattle && UiGame_InBattleMenu()) && !(sHosted && gMain.callback2 == CB2_UiHost))
    {
        Close();
        return;
    }
    sCircleTicks = (sInput.held & VITA_BTN_CIRCLE) ? sCircleTicks + 1U : 0U;
    if (sCircleTicks == 60U && !sFromScript)
    {
        Close();
        return;
    }
    const UiScreen *const top = sStack[sDepth - 1];
    UiStall_Tick(top, sAvgTickMs);
    if (!top->update(&sInput))
        UiNav_Pop();
    if (sCloseRequested || sDepth == 0)
    {
        void (*const after)(void) = sAfterClose;
        sAfterClose = NULL;
        Close();
        if (after != NULL)
            after();
        return;
    }
    UiDraw_Begin();
    UiSpr_BeginTick();
    UiW_GbaPageReset();
    sStack[sDepth - 1]->build();
    UiW_GbaPageFlush();
    UiKit_DrawToast();
    const float ms = (float)(VitaPerf_NowMs() - t0);
    sAvgTickMs = (sAvgTickMs == 0.0f) ? ms : (sAvgTickMs * 0.9f + ms * 0.1f);
    UiDraw_Publish(ms);
}
