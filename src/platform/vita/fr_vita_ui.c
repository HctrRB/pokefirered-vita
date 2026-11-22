/*
 * fr_vita_ui.c - the Vita backend of the FireRed 3DS touch UI (see fr_vita_ui.h).
 *
 * Implements what src/platform/fr3ds/fr_bottom_ui.c and fr_extras.c expect from the 3DS
 * backend (3ds_bottom.h, 3ds_input.h, 3ds_log.h, 3ds_platform.h): settings kept in
 * ux0:data/FRVITA/settings.txt, input, the game frame counter, and the canvas the UI draws
 * into, which Core 0 draws over the game with its page colour translucent.
 *
 * Threads: see fr_vita_ui.h. The canvas goes from Core 1 to Core 0 through a copy under a
 * mutex; Core 0 never writes game state.
 *
 * Origin: written for this port. Licence: MIT.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <vitaGL.h>

#include "global.h"
#include "main.h"
#include "palette.h"
#include "port_platform.h"
#include "3ds_bottom.h"
#include "3ds_input.h"
#include "3ds_log.h"
#include "3ds_platform.h"

#include "fr_vita_ui.h"
#include "vita_input.h"
#include "vita_platform.h"
#include "platform/ui.h"
#include "platform/vita_extras.h"

/* ------------------------------------------------------------------------------------------ */
/* Constants                                                                                    */
/* ------------------------------------------------------------------------------------------ */

#define UI_W CTR_BOTTOM_WIDTH
#define UI_H CTR_BOTTOM_HEIGHT
#define UI_PIXELS (UI_W * UI_H)
/* Integer 2x keeps the UI's pixel art and fonts sharp: 640x480, centred on 960x544. */
#define UI_SCALE 2
#define SCREEN_W 960
#define SCREEN_H 544
#define UI_X0 ((SCREEN_W - UI_W * UI_SCALE) / 2)
#define UI_Y0 ((SCREEN_H - UI_H * UI_SCALE) / 2)
/* Alpha (of 15) of the pages' background: the world shows through it. */
#define UI_PAGE_ALPHA 6
#define UI_OPAQUE_ALPHA 15
#define UI_DIM_LEVELS 4

#define SETTINGS_PATH VITA_DATA_DIR "settings.txt"
#define SETTINGS_MAX 64
#define SETTINGS_KEY_MAX 32
#define LOG_LINE_MAX 256

#define SPEED_KEY "speed"
#define SPEED_MIN 1
#define SPEED_MAX 4
#define RUN_KEY "run_always"

/* A frame stamp no frame count reaches soon: "never". */
#define FRAME_NEVER 0xFFFFFFF0u

/* start_menu.c (PORT_BRIDGE): an entry the dock asked for, waiting or running */
bool8 CtrStartMenu_Pending(void);
bool8 CtrStartMenu_Busy(void);

/* fr_bottom_ui.c (PLATFORM_VITA) */
bool CtrBottom_VitaTakesScreen(void);
bool CtrBottom_VitaFieldIdle(void);
u16 CtrBottom_VitaPageColour(void);

/* ------------------------------------------------------------------------------------------ */
/* Log, frame counters, test input                                                              */
/* ------------------------------------------------------------------------------------------ */

void CtrLog_Write(CtrLogCategory category, const char *format, ...)
{
#ifdef VITA_LOGGING
    char line[LOG_LINE_MAX];
    va_list args;

    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    VITA_LOG("[UI %d] %s\n", (int)category, line);
#else
    (void)category;
    (void)format;
#endif
}

static uint32_t sFrames;
static uint32_t sResets;
static uint32_t sSceneFrame = FRAME_NEVER;

uint32_t CtrGame_Frames(void) { return sFrames; }
uint32_t CtrGame_Resets(void) { return sResets; }
void FrVitaUi_FrameDone(void) { sFrames++; }

/* A full-screen scene (evolution, trade, Hall of Fame...) runs over the field. */
void Port_SceneOnTop(void) { sSceneFrame = sFrames; }
bool Port_SceneShown(void) { return sFrames - sSceneFrame <= 1; }

/* The 3DS's scripted test input (CTR_INPUT_SCRIPT): none here. */
bool CtrScript_Touch(u16 *x, u16 *y)
{
    (void)x;
    (void)y;
    return false;
}

bool CtrScript_Stick(s16 *x, s16 *y)
{
    (void)x;
    (void)y;
    return false;
}

/* ------------------------------------------------------------------------------------------ */
/* Input                                                                                        */
/* ------------------------------------------------------------------------------------------ */

static CtrInput sInput;
static bool sUserOpen;          /* the player opened the panel */
static bool sShownToGame;       /* the panel was on screen this frame (Core 1's view) */
static SDL_atomic_t sShown;     /* the same, for Core 0 */
static uint16_t sSwallowed;     /* keys that opened or closed the panel, held until let go */
static bool sReopen;            /* the panel stepped aside for an action of its dock */
static bool sFastForward;
static bool sPrevInBattle;

#define SOFT_RESET_CHORD (CTR_KEY_A | CTR_KEY_B | CTR_KEY_START | CTR_KEY_SELECT)

const CtrInput *CtrInput_Get(void)
{
    return &sInput;
}

/* Vita buttons to the UI's keys: the GBA's ten, plus Square as the 3DS's X. */
static uint16_t MapButtons(const uint32_t buttons)
{
    uint16_t keys = 0;

    if (buttons & VITA_BTN_CROSS)  keys |= CTR_KEY_A;
    if (buttons & VITA_BTN_CIRCLE) keys |= CTR_KEY_B;
    if (buttons & VITA_BTN_SELECT) keys |= CTR_KEY_SELECT;
    if (buttons & VITA_BTN_START)  keys |= CTR_KEY_START;
    if (buttons & VITA_BTN_RIGHT)  keys |= CTR_KEY_RIGHT;
    if (buttons & VITA_BTN_LEFT)   keys |= CTR_KEY_LEFT;
    if (buttons & VITA_BTN_UP)     keys |= CTR_KEY_UP;
    if (buttons & VITA_BTN_DOWN)   keys |= CTR_KEY_DOWN;
    if (buttons & VITA_BTN_R)      keys |= CTR_KEY_R;
    if (buttons & VITA_BTN_L)      keys |= CTR_KEY_L;
    if (buttons & VITA_BTN_SQUARE) keys |= CTR_KEY_X;
    return keys;
}

/* START opens the panel where the 3DS would show it (the field, a battle) and closes it;
 * Circle closes it too. The key is then swallowed until let go, so the game never sees it. */
static void UpdatePanelToggle(const uint16_t down)
{
    const bool takesScreen = CtrBottom_VitaTakesScreen();

    if ((sInput.held & SOFT_RESET_CHORD) == SOFT_RESET_CHORD)
        return;
    if (takesScreen)
        return;
    if (sUserOpen && (down & (CTR_KEY_START | CTR_KEY_B)))
    {
        sUserOpen = false;
        sReopen = false;
        sSwallowed |= down & (CTR_KEY_START | CTR_KEY_B);
    }
    else if (!sUserOpen && (down & CTR_KEY_START) && (CtrBottom_VitaFieldIdle() || gMain.inBattle))
    {
        sUserOpen = true;
        sSwallowed |= CTR_KEY_START;
    }
}

static void ReadTouch(void)
{
    int32_t x = 0;
    int32_t y = 0;
    const bool wasActive = sInput.touchActive;
    bool active = (sUserOpen || CtrBottom_VitaTakesScreen()) && VitaPlatform_ReadTouch(&x, &y);

    if (active)
    {
        if (x < UI_X0 || y < UI_Y0)
        {
            active = false;
        }
        else
        {
            x = (x - UI_X0) / UI_SCALE;
            y = (y - UI_Y0) / UI_SCALE;
            active = x < UI_W && y < UI_H;
        }
    }
    sInput.touchActive = active;
    sInput.touchDown = active && !wasActive;
    sInput.touchUp = !active && wasActive;
    if (active)
    {
        sInput.touchX = (uint16_t)x;
        sInput.touchY = (uint16_t)y;
        sInput.touchNormX = (float)x / (float)UI_W;
        sInput.touchNormY = (float)y / (float)UI_H;
    }
    else
    {
        sInput.touchNormX = 0.0f;
        sInput.touchNormY = 0.0f;
    }
}

/* A dock entry the game runs in the field itself (SAVE's dialog, and every entry until its
 * screen opens) is drawn under the panel and answers the buttons the open panel keeps from the
 * game: the panel steps aside while it runs and comes back in the field afterwards, as the
 * 3DS's bottom screen stays. */
static void StepAsideForDockActions(void)
{
    const bool action = CtrStartMenu_Pending() || CtrStartMenu_Busy();

    if (sUserOpen && action)
    {
        sUserOpen = false;
        sReopen = true;
    }
    else if (sReopen && !action && CtrBottom_VitaFieldIdle())
    {
        sReopen = false;
        sUserOpen = true;
    }
}

void FrVitaUi_ScanInput(void)
{
    const uint32_t buttons = VitaPlatform_ReadButtons();
    VitaInputState snap;
    VitaInput_GetSnapshot(&snap);
    const uint16_t allKeys = MapButtons(buttons);
    const uint32_t nonDpadBtns = buttons & ~(VITA_BTN_UP | VITA_BTN_DOWN | VITA_BTN_LEFT | VITA_BTN_RIGHT);
    const uint16_t physicalNoStick = MapButtons(nonDpadBtns | snap.dpadHeld);
    const uint16_t prevAll = sInput.held | (sInput.physicalHeld & ~CTR_KEY_GAME);

    sInput.physicalHeld = physicalNoStick;
    sInput.physicalDown = allKeys & ~prevAll;
    sInput.physicalUp = prevAll & ~allKeys;
    sInput.held = allKeys & CTR_KEY_GAME;
    sInput.down = sInput.physicalDown & CTR_KEY_GAME;
    sInput.up = sInput.physicalUp & CTR_KEY_GAME;
    sInput.circleX = (int16_t)snap.leftStickX - 128;
    sInput.circleY = (int16_t)(128 - (int16_t)snap.leftStickY);
    sInput.circleDiagonal = 0;
    sFastForward = (buttons & VITA_BTN_TRIANGLE) != 0;

    if (sPrevInBattle && !gMain.inBattle)
    {
        sUserOpen = false;
        sReopen = false;
    }
    sPrevInBattle = gMain.inBattle;
    if (CtrBottom_TakesGameFrame() && !CtrBottom_VitaTakesScreen())
    {
        sUserOpen = false;
        sReopen = false;
    }

    sSwallowed &= allKeys;
    if (Ui_IsEnabled() && (!gMain.inBattle || Ui_IsOpen()))
    {
        sUserOpen = false;
        sReopen = false;
        return;
    }
    UpdatePanelToggle(sInput.physicalDown);
    ReadTouch();
    CtrBottom_Frame();
    StepAsideForDockActions();
}

uint16_t FrVitaUi_GameKeys(void)
{
    uint16_t held;

    if ((sInput.held & SOFT_RESET_CHORD) == SOFT_RESET_CHORD)
        return sInput.held;
    held = sInput.held & ~sSwallowed;
    /* The open panel covers the field: the player does not walk under it. In a battle the
     * buttons still work the battle's own menu. */
    if (sUserOpen && !gMain.inBattle && !CtrBottom_VitaTakesScreen())
        held = 0;
    return CtrBottom_FilterKeys(held) | CtrBottom_InjectedKeys();
}

bool FrVitaUi_FastForwardHeld(void)
{
    return sFastForward;
}

/* ------------------------------------------------------------------------------------------ */
/* Frames                                                                                       */
/* ------------------------------------------------------------------------------------------ */

void FrVitaUi_SyncShown(void)
{
    const bool shown = (!Ui_IsEnabled() || (gMain.inBattle && !Ui_IsOpen()))
                    && (sUserOpen || CtrBottom_VitaTakesScreen());

    sShownToGame = shown;
    SDL_AtomicSet(&sShown, shown ? 1 : 0);
}

/* ------------------------------------------------------------------------------------------ */
/* Canvas (Core 1 -> Core 0)                                                                    */
/* ------------------------------------------------------------------------------------------ */

static SDL_mutex *sCanvasLock;
static uint16_t sShared[UI_PIXELS];      /* the UI's canvas: column-major, bottom to top */
static int32_t sSharedDim = UI_DIM_LEVELS;
static bool sSharedDirty;

static void CopyColumnsWithDim(const uint16_t *canvas, int x0, int x1, int y0, int y1, int dim)
{
    if (canvas == NULL || sCanvasLock == NULL || x0 >= x1 || y0 >= y1)
        return;
    if (x0 < 0) x0 = 0;
    if (x1 > UI_W) x1 = UI_W;
    if (y0 < 0) y0 = 0;
    if (y1 > UI_H) y1 = UI_H;
    /* A column runs bottom to top, so rows [y0, y1) are one run in it. */
    const int first = UI_H - y1;
    const int count = y1 - y0;

    SDL_LockMutex(sCanvasLock);
    for (int x = x0; x < x1; x++)
        memcpy(sShared + x * UI_H + first, canvas + x * UI_H + first, (size_t)count * sizeof(uint16_t));
    sSharedDim = dim < 0 ? 0 : dim > UI_DIM_LEVELS ? UI_DIM_LEVELS : dim;
    sSharedDirty = true;
    SDL_UnlockMutex(sCanvasLock);
}

void CtrBottom_Blit(const uint16_t *canvas, int x0, int x1)
{
    CopyColumnsWithDim(canvas, x0, x1, 0, UI_H, UI_DIM_LEVELS);
}

void CtrBottom_BlitRect(const uint16_t *canvas, int x0, int y0, int x1, int y1)
{
    CopyColumnsWithDim(canvas, x0, x1, y0, y1, UI_DIM_LEVELS);
}

/* A fade: the whole canvas at level/4 of its brightness, applied when it is drawn. */
void CtrBottom_BlitDim(const uint16_t *canvas, int level)
{
    CopyColumnsWithDim(canvas, 0, UI_W, 0, UI_H, level);
}

void FrVitaUi_NoteReset(void)
{
    const uint16_t heldKeys = sInput.held | (sInput.physicalHeld & ~CTR_KEY_GAME);

    sFrames = 0;
    sSceneFrame = FRAME_NEVER;
    memset(&sInput, 0, sizeof(sInput));
    sInput.physicalHeld = heldKeys;
    sUserOpen = false;
    sShownToGame = false;
    SDL_AtomicSet(&sShown, 0);
    sSwallowed = heldKeys;
    sReopen = false;
    sFastForward = false;
    sPrevInBattle = false;
    if (sCanvasLock != NULL)
    {
        SDL_LockMutex(sCanvasLock);
        memset(sShared, 0, sizeof(sShared));
        sSharedDim = UI_DIM_LEVELS;
        sSharedDirty = true;
        SDL_UnlockMutex(sCanvasLock);
    }
    sResets++;
}

/* ------------------------------------------------------------------------------------------ */
/* Drawing (Core 0)                                                                             */
/* ------------------------------------------------------------------------------------------ */

#define UI_TEXTURE_RING 3
static GLuint sTextureRing[UI_TEXTURE_RING];
static uint8_t sTextureIdx;
static uint16_t sTexels[UI_PIXELS];      /* RGBA4444, row-major */
static uint16_t sPageColour;
static float sDim = 1.0f;

void FrVitaUi_Init(void)
{
    sCanvasLock = SDL_CreateMutex();
    CtrBottom_Init();
    sPageColour = CtrBottom_VitaPageColour();
}

void FrVitaUi_InitVideo(void)
{
    glGenTextures(UI_TEXTURE_RING, sTextureRing);
    for (int32_t i = 0; i < UI_TEXTURE_RING; i++)
    {
        glBindTexture(GL_TEXTURE_2D, sTextureRing[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, UI_W, UI_H, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
    }
}

/* RGB565 column-major canvas to RGBA4444 rows; the page colour becomes translucent. Runs
 * under the canvas lock. */
static void ConvertCanvas(void)
{
    for (int x = 0; x < UI_W; x++)
    {
        const uint16_t *column = sShared + x * UI_H;

        for (int y = 0; y < UI_H; y++)
        {
            const uint16_t c = column[UI_H - 1 - y];
            const uint16_t r = (uint16_t)((c >> 11) >> 1);
            const uint16_t g = (uint16_t)(((c >> 5) & 0x3F) >> 2);
            const uint16_t b = (uint16_t)((c & 0x1F) >> 1);
            const uint16_t a = c == sPageColour ? UI_PAGE_ALPHA : UI_OPAQUE_ALPHA;

            sTexels[y * UI_W + x] = (uint16_t)((r << 12) | (g << 8) | (b << 4) | a);
        }
    }
}

static void UploadIfChanged(void)
{
    bool dirty;

    if (sCanvasLock == NULL)
        return;
    SDL_LockMutex(sCanvasLock);
    dirty = sSharedDirty;
    if (dirty)
    {
        ConvertCanvas();
        sDim = (float)sSharedDim / (float)UI_DIM_LEVELS;
        sSharedDirty = false;
    }
    SDL_UnlockMutex(sCanvasLock);
    if (!dirty)
        return;
    sTextureIdx = (uint8_t)((sTextureIdx + 1U) % UI_TEXTURE_RING);
    glBindTexture(GL_TEXTURE_2D, sTextureRing[sTextureIdx]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UI_W, UI_H, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, sTexels);
}

void FrVitaUi_Render(void)
{
    static const float uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    static const float verts[8] = {
        UI_X0, UI_Y0,
        UI_X0 + UI_W * UI_SCALE, UI_Y0,
        UI_X0 + UI_W * UI_SCALE, UI_Y0 + UI_H * UI_SCALE,
        UI_X0, UI_Y0 + UI_H * UI_SCALE,
    };

    if (!SDL_AtomicGet(&sShown))
        return;
    UploadIfChanged();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, sTextureRing[sTextureIdx]);
    glColor4f(sDim, sDim, sDim, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

bool FrVitaUi_IsShown(void)
{
    return SDL_AtomicGet(&sShown) != 0;
}
