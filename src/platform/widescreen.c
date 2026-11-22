// Vita widescreen: which layers of each frame fill the 24 px margins (see platform/widescreen.h).
#include <stdint.h>
#include <string.h>
#include "global.h"
#include "bg.h"
#include "decompress.h"
#include "graphics.h"
#include "main.h"
#include "platform.h"
#include "platform/framedraw.h"
#include "platform/game_frame.h"
#include "platform/vita/vita_stage.h"
#include "scanline_effect.h"

extern u16 *gBGTilemapBuffers1;
extern u16 *gBGTilemapBuffers2;
extern u16 *gBGTilemapBuffers3;

#ifdef WIDESCREEN_SUPPORTED
struct FastDrawWide gFastDrawWide;

static MainCallback sFieldCallback;
static MainCallback sBattleCallback;
static u16 sFillMaps[4][32 * 32];
static u16 *sFieldMaps[4];
static u8 sFieldMapBase[4];
static int32_t sResetPending;

// Battle text box and menus (BG0, 32x64 tile map): original template and stretched 64x64 map.
static u16 sTextboxBase[32 * 64];
static bool8 sTextboxBaseLoaded;
static u16 sTextboxWide[64 * 64];

bool8 Widescreen_IsOn(void)
{
    return TRUE;
}

void Widescreen_SetFieldFrame(void)
{
    // FireRed binds gBGTilemapBuffers2 to BG1 and gBGTilemapBuffers1 to BG2 (src/overworld.c).
    u16 *const maps[4] = {NULL, gBGTilemapBuffers2, gBGTilemapBuffers1, gBGTilemapBuffers3};
    int32_t bg;

    for (bg = 1; bg < 4; bg++)
    {
        __atomic_store_n(&sFieldMaps[bg], maps[bg], __ATOMIC_RELEASE);
        __atomic_store_n(&sFieldMapBase[bg], (u8)GetBgAttribute((u8)bg, BG_ATTR_MAPBASEINDEX), __ATOMIC_RELEASE);
    }
    __atomic_store_n(&sBattleCallback, (MainCallback)NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&sFieldCallback, gMain.callback2, __ATOMIC_RELEASE);
}

void Widescreen_SetBattleFrame(bool8 introSlide)
{
    (void)introSlide;
    __atomic_store_n(&sFieldCallback, (MainCallback)NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&sBattleCallback, gMain.callback2, __ATOMIC_RELEASE);
}

static u16 MostCommonMapEntry(const u16 *const map, const u16 control, const u16 hoffs, const u16 voffs)
{
    static u16 sKeys[1024];
    static u16 sCounts[1024];
    const u32 screenSize = (control >> 14) & 3u;
    const u32 widthMask = (screenSize & 1u) ? 63u : 31u;
    const u32 heightMask = (screenSize & 2u) ? 63u : 31u;
    u16 best = 0;
    u16 bestCount = 0;
    int32_t x, y;

    memset(sCounts, 0, sizeof(sCounts));
    for (y = 0; y < DISPLAY_HEIGHT / 8; y++)
    {
        for (x = 0; x < DISPLAY_WIDTH / 8; x++)
        {
            const u32 tx = ((u32)x + hoffs / 8u) & widthMask;
            const u32 ty = ((u32)y + voffs / 8u) & heightMask;
            u32 idx = (ty & 31u) * 32u + (tx & 31u);
            if (tx & 32u)
                idx += 0x400u;
            if (ty & 32u)
                idx += (screenSize & 1u) ? 0x800u : 0x400u;
            const u16 entry = (((uintptr_t)&map[idx] + sizeof(u16)) <= (uintptr_t)BG_VRAM + 0x10000u) ? map[idx] : 0u;
            u32 slot = (entry * 2654435761u) >> 22;

            while (sCounts[slot] != 0 && sKeys[slot] != entry)
                slot = (slot + 1) & 1023;
            sKeys[slot] = entry;
            if (++sCounts[slot] > bestCount)
            {
                bestCount = sCounts[slot];
                best = entry;
            }
        }
    }
    return best;
}

static u16 *BuildFillMap(const int32_t bg)
{
    const u16 control = *(vu16 *)(REG_ADDR_BG0CNT + bg * 2);
    const u16 *const live = (const u16 *)BG_SCREEN_ADDR((control >> 8) & 0x1F);
    const u16 entry = MostCommonMapEntry(live, control,
                                         *(vu16 *)(REG_ADDR_BG0HOFS + bg * 4),
                                         *(vu16 *)(REG_ADDR_BG0VOFS + bg * 4));
    int32_t i;

    for (i = 0; i < 32 * 32; i++)
        sFillMaps[bg][i] = entry;
    return sFillMaps[bg];
}

static int32_t GetBackTextBg(void)
{
    const int32_t mode = REG_DISPCNT & 7;
    const int32_t numTextBgs = (mode == 0) ? 4 : (mode == 1 ? 2 : 0);
    int32_t bg, back = -1, backPriority = -1;

    for (bg = 0; bg < numTextBgs; bg++)
    {
        const int32_t priority = *(vu16 *)(REG_ADDR_BG0CNT + bg * 2) & 3;

        if (!(REG_DISPCNT & (DISPCNT_BG0_ON << bg)))
            continue;
        if (priority >= backPriority)
        {
            back = bg;
            backPriority = priority;
        }
    }
    return back;
}

static u32 WideMapIndex(int32_t x, int32_t y)
{
    x &= 63;
    y &= 63;
    return (u32)((y & 31) * 32 + (x & 31) + ((x & 32) << 5) + ((y & 32) << 6));
}

static void BuildBattleTextboxMap(const u16 *const live)
{
    int32_t x, y;

    for (y = 0; y < 64; y++)
    {
        const u16 *const liveRow = &live[y * 32];
        const u16 *const baseRow = &sTextboxBase[y * 32];
        const bool8 isBoxRow = (baseRow[0] & 0x3FF) != 0;

        for (x = -3; x < 33; x++)
        {
            u16 entry;

            if (!isBoxRow)
                entry = (x >= 0 && x < 30) ? liveRow[x] : 0;
            else if (x < -1)
                entry = baseRow[x + 3];
            else if (x == -1)
                entry = baseRow[2];
            else if (x < 2)
                entry = (liveRow[x] == baseRow[x]) ? baseRow[2] : liveRow[x];
            else if (x < 28)
                entry = liveRow[x];
            else if (x < 30)
                entry = (liveRow[x] == baseRow[x]) ? baseRow[27] : liveRow[x];
            else if (x == 30)
                entry = baseRow[27];
            else
                entry = baseRow[x - 3];
            sTextboxWide[WideMapIndex(x, y)] = entry;
        }
    }
}

// --- Stage scenes -------------------------------------------------------------------------------
// Adapted from Zallax and Emerald 3DS (CC BY-NC-SA 4.0, see NOTICE.md).
#define STAGE_STILL_FRAMES 32
#define STAGE_STILL_SPAN 4
#define STAGE_SCROLL_HOLD_FRAMES 120
#define STAGE_SCROLL_MASK 511
#define STAGE_MEASURE_INTERVAL 8
#define STAGE_HIDDEN_FIRST_COLUMN 30
#define STAGE_MEASURED_FIRST_COLUMN 28
#define STAGE_MAP_COLUMNS 32
#define STAGE_SCREEN_ROWS (DISPLAY_HEIGHT / 8)

static u32 sStageFrame;
static u32 sStageLayerKey[4];
static u16 sStageScrollHistory[4][STAGE_STILL_FRAMES];
static bool8 sStageScrolls[4];
static u32 sStageMovedFrame[4];
static u16 sStageScrollX[4];
static u16 sStageScrollY[4];
static u32 sStageMeasuredFrame[4];
static bool8 sStagePanorama[4];
static bool8 sStageFade;

static bool8 TileHasPixels(const uintptr_t chars, const u16 entry, const bool8 color256)
{
    const uintptr_t tileBytes = color256 ? 64u : 32u;
    const uintptr_t address = chars + (entry & 0x3FFu) * tileBytes;
    const u32 *const words = (const u32 *)address;
    u32 i;

    if (address + tileBytes > (uintptr_t)BG_VRAM + 0x10000u)
        return FALSE;
    for (i = 0; i < tileBytes / 4; i++)
    {
        if (words[i] != 0)
            return TRUE;
    }
    return FALSE;
}

static bool8 StageLayerIsPanorama(const int32_t bg, const u16 control)
{
    const uintptr_t chars = (uintptr_t)BG_CHAR_ADDR((control >> 2) & 3);
    const u16 *const map = (const u16 *)BG_SCREEN_ADDR((control >> 8) & 0x1F);
    const bool8 color256 = (control & BGCNT_256COLOR) != 0;
    const u32 rowMask = ((control >> 14) & 2) ? 63 : 31;
    u32 inside = 0;
    u32 beyond = 0;
    u32 y, x;

    if ((control >> 14) & 1)
        return TRUE;
    for (y = 0; y < STAGE_SCREEN_ROWS; y++)
    {
        const u32 row = (y + sStageScrollY[bg] / 8) & rowMask;
        const u16 *const rowBase = &map[(row & 31) * 32 + ((row & 32) ? 1024 : 0)];

        for (x = STAGE_MEASURED_FIRST_COLUMN; x < STAGE_MAP_COLUMNS; x++)
        {
            const u32 column = (x + sStageScrollX[bg] / 8) & 31;

            if (TileHasPixels(chars, rowBase[column], color256))
                (x < STAGE_HIDDEN_FIRST_COLUMN) ? inside++ : beyond++;
        }
    }
    return beyond != 0 && beyond * 4 >= inside * 3;
}

static bool8 HofsIsLineDriven(const int32_t bg)
{
    const uintptr_t hofs = (uintptr_t)(REG_ADDR_BG0HOFS + bg * 4);

    return gScanlineEffect.state != 0 && (uintptr_t)gScanlineEffect.dmaDest == hofs;
}

static void TrackStageScroll(const int32_t bg)
{
    const u16 display = REG_DISPCNT;
    const u32 key = *(vu16 *)(REG_ADDR_BG0CNT + bg * 2) | ((u32)(display & 7) << 16)
                  | ((u32)((display >> (8 + bg)) & 1) << 20);
    const bool8 lineDriven = HofsIsLineDriven(bg);
    const u16 scroll = lineDriven ? 0 : (*(vu16 *)(REG_ADDR_BG0HOFS + bg * 4) & STAGE_SCROLL_MASK);
    int32_t low = 0, high = 0;
    u32 i;

    sStageScrollX[bg] = *(vu16 *)(REG_ADDR_BG0HOFS + bg * 4);
    sStageScrollY[bg] = *(vu16 *)(REG_ADDR_BG0VOFS + bg * 4);
    if (key != sStageLayerKey[bg])
    {
        sStageLayerKey[bg] = key;
        sStageScrolls[bg] = FALSE;
        sStageMeasuredFrame[bg] = 0;
        for (i = 0; i < STAGE_STILL_FRAMES; i++)
            sStageScrollHistory[bg][i] = scroll;
    }
    sStageScrollHistory[bg][sStageFrame % STAGE_STILL_FRAMES] = scroll;
    for (i = 0; i < STAGE_STILL_FRAMES; i++)
    {
        const int32_t delta = ((((int32_t)sStageScrollHistory[bg][i] - (int32_t)scroll) + 256) & STAGE_SCROLL_MASK) - 256;

        if (delta < low)
            low = delta;
        if (delta > high)
            high = delta;
    }
    if (lineDriven || high - low > STAGE_STILL_SPAN)
    {
        sStageScrolls[bg] = TRUE;
        sStageMovedFrame[bg] = sStageFrame;
    }
    else if (sStageScrolls[bg] && sStageFrame - sStageMovedFrame[bg] > STAGE_SCROLL_HOLD_FRAMES)
    {
        sStageScrolls[bg] = FALSE;
    }
}

static u8 StageLayerSpan(const int32_t bg, const bool8 sceneScrolls)
{
    const u16 control = *(vu16 *)(REG_ADDR_BG0CNT + bg * 2);

    if (sStageScrolls[bg])
        return WIDE_BG_WIDE;
    if (!sceneScrolls)
        return WIDE_BG_EDGE_TILE;
    if (sStageMeasuredFrame[bg] == 0 || sStageFrame - sStageMeasuredFrame[bg] >= STAGE_MEASURE_INTERVAL)
    {
        sStagePanorama[bg] = StageLayerIsPanorama(bg, control);
        sStageMeasuredFrame[bg] = sStageFrame;
    }
    return sStagePanorama[bg] ? WIDE_BG_WIDE : WIDE_BG_EDGE_TILE;
}

static void PrepareStageFrame(void)
{
    const u16 display = REG_DISPCNT;
    const int32_t mode = display & 7;
    const int32_t textBgs = (mode == 0) ? 4 : (mode == 1 ? 2 : 0);
    bool8 sceneScrolls = FALSE;
    bool8 anyWide = FALSE;
    bool8 anyStill = FALSE;
    int32_t bg;

    sStageFrame++;
    for (bg = 0; bg < textBgs; bg++)
    {
        TrackStageScroll(bg);
        if ((display & (DISPCNT_BG0_ON << bg)) && sStageScrolls[bg])
            sceneScrolls = TRUE;
    }
    for (bg = 0; bg < textBgs; bg++)
    {
        if (!(display & (DISPCNT_BG0_ON << bg)))
            continue;
        gFastDrawWide.bgSpan[bg] = StageLayerSpan(bg, sceneScrolls);
        if (gFastDrawWide.bgSpan[bg] == WIDE_BG_WIDE)
            anyWide = TRUE;
        else
            anyStill = TRUE;
    }
    gFastDrawWide.extendObj = TRUE;
    sStageFade = anyStill && !anyWide;
}

bool8 Widescreen_StageFadesMargins(void)
{
    return sStageFade;
}

void Widescreen_OnReset(void)
{
    int32_t bg;

    __atomic_store_n(&sFieldCallback, (MainCallback)NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&sBattleCallback, (MainCallback)NULL, __ATOMIC_RELEASE);
    for (bg = 0; bg < 4; bg++)
    {
        __atomic_store_n(&sFieldMaps[bg], (u16 *)NULL, __ATOMIC_RELEASE);
        __atomic_store_n(&sFieldMapBase[bg], 0, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&sResetPending, 1, __ATOMIC_RELEASE);
}

void Widescreen_PrepareDraw(void)
{
    if (__atomic_exchange_n(&sResetPending, 0, __ATOMIC_ACQUIRE))
    {
        memset(&gFastDrawWide, 0, sizeof(gFastDrawWide));
        sStageFrame = 0;
        sStageFade = FALSE;
        memset(sStageLayerKey, 0, sizeof(sStageLayerKey));
        memset(sStageScrolls, 0, sizeof(sStageScrolls));
        memset(sStageMovedFrame, 0, sizeof(sStageMovedFrame));
        memset(sStageMeasuredFrame, 0, sizeof(sStageMeasuredFrame));
        memset(sStagePanorama, 0, sizeof(sStagePanorama));
    }

    const GameFrame *const gf = GameFrame_Get();
    const void *const callback2 = (gf != NULL) ? gf->field.callback2 : NULL;
    const MainCallback fieldCb = __atomic_load_n(&sFieldCallback, __ATOMIC_ACQUIRE);
    const MainCallback battleCb = __atomic_load_n(&sBattleCallback, __ATOMIC_ACQUIRE);
    const bool8 fieldFrame = fieldCb != NULL && callback2 == (const void *)fieldCb;
    const bool8 battleFrame = battleCb != NULL && callback2 == (const void *)battleCb;
    int32_t bg;

    sStageFade = FALSE;
    gFastDrawWide.output = Widescreen_IsOn() && gf != NULL && !gf->overlay.gbaFormat;
    gFastDrawWide.extendObj = FALSE;
    for (bg = 0; bg < 4; bg++)
    {
        gFastDrawWide.bgSpan[bg] = WIDE_BG_CENTRED;
        gFastDrawWide.bgMaps[bg] = fieldFrame ? __atomic_load_n(&sFieldMaps[bg], __ATOMIC_ACQUIRE) : NULL;
        gFastDrawWide.bgMapBase[bg] = __atomic_load_n(&sFieldMapBase[bg], __ATOMIC_ACQUIRE);
        gFastDrawWide.bgMapSize[bg] = 1; // 64x32
    }
    if (gFastDrawCentredOnly)
    {
        gFastDrawWide.extend = FALSE;
        return;
    }
    if (gFastDrawWide.output && fieldFrame)
    {
        gFastDrawWide.bgSpan[1] = gFastDrawWide.bgSpan[2] = gFastDrawWide.bgSpan[3] = WIDE_BG_WIDE;
        gFastDrawWide.extendObj = TRUE;
    }
    else if (gFastDrawWide.output && battleFrame)
    {
        gFastDrawWide.bgSpan[3] = WIDE_BG_WIDE;
        gFastDrawWide.bgSpan[1] = ((REG_BG1CNT >> 14) & 1) ? WIDE_BG_WIDE : WIDE_BG_CENTRED;
        gFastDrawWide.bgSpan[2] = WIDE_BG_WIDE;
        gFastDrawWide.extendObj = TRUE;

        // Stretch battle BG0 text box / menus when its 32x64 map is active.
        if (((REG_BG0CNT >> 14) & 3) == 2)
        {
            if (!sTextboxBaseLoaded)
            {
                LZDecompressWram(gBattleInterface_Textbox_Tilemap, sTextboxBase);
                sTextboxBaseLoaded = TRUE;
            }
            BuildBattleTextboxMap((const u16 *)BG_SCREEN_ADDR((REG_BG0CNT >> 8) & 0x1F));
            gFastDrawWide.bgMaps[0] = sTextboxWide;
            gFastDrawWide.bgMapBase[0] = (u8)((REG_BG0CNT >> 8) & 0x1F);
            gFastDrawWide.bgMapSize[0] = 3; // 64x64
            gFastDrawWide.bgSpan[0] = WIDE_BG_WIDE;
        }
    }
    else if (gFastDrawWide.output && VitaStage_IsStageActive())
    {
        PrepareStageFrame();
    }
    else if (gFastDrawWide.output)
    {
        const int32_t back = GetBackTextBg();

        if (back >= 0)
        {
            gFastDrawWide.bgSpan[back] = WIDE_BG_FILL;
            gFastDrawWide.bgFillMaps[back] = BuildFillMap(back);
        }
    }
    gFastDrawWide.extend = gFastDrawWide.extendObj;
    for (bg = 0; bg < 4; bg++)
    {
        if (gFastDrawWide.bgSpan[bg] != WIDE_BG_CENTRED)
            gFastDrawWide.extend = TRUE;
    }
}
#endif
