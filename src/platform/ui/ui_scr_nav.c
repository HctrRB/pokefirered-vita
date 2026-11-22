// Town Map, Fly Map & Pokédex Area Overlay for FireRed PS Vita native 960x544 UI (Core 1).
// Covers Kanto + Sevii Islands (4 region maps, 22x15 grid starting at tile (4, 4) inside the
// 26x18 (208x144) atlas sprites starting at tile (2, 2)), dungeon layer inspection,
// wild encounters popup (Grass / Surf / Fish + Rod filter / Rock Smash), and Fly destinations.

#include <stdio.h>
#include <string.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "overworld.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/species.h"

// =====================================================================================
// 4-Region Kanto / Sevii Map Geometry
// =====================================================================================
#define REGION_MAX 4
#define GRID_W 22
#define GRID_H 15
#define SPR_TILES_W 26
#define SPR_TILES_H 18
#define GRID_OFF_TILES 2

// Modern & Pokédex Area: 26*24 x 18*24 = 624x432 (exact 3x GBA scale)
#define MAP_X 16
#define MAP_Y 60
#define MAP_W 624
#define MAP_H 432

// GBA dark-well Town Map & Fly Map: 26*20 x 18*20 = 520x360 centered in (14, 60, 590, 472)
#define GBA_MAP_X 49
#define GBA_MAP_Y 72
#define GBA_MAP_W 520
#define GBA_MAP_H 360

#define POP_ROWS 6

static const uint16_t sRegionSprites[REGION_MAX] = {
    UI_SPR_MAP_KANTO,
    UI_SPR_MAP_SEVII_123,
    UI_SPR_MAP_SEVII_45,
    UI_SPR_MAP_SEVII_67,
};

static const char *const sRegionLabels[REGION_MAX] = {
    UI_N("KANTO"),
    UI_N("SEVII 1-3"),
    UI_N("SEVII 4-5"),
    UI_N("SEVII 6-7"),
};

static int32_t sRegion = 0;
static int32_t sRegionCount = 1;
static int32_t sPlayerRegion = 0;
static int32_t sPlayerX = 0, sPlayerY = 0;
static int32_t sCurX = 4, sCurY = 11;
static bool sZoom = false;
static uint8_t sFlyable[MAPSEC_NONE + 1];
static uint32_t sFrame = 0;

static void FindDefaultCursorForRegion(const int32_t region, const bool fly)
{
    if (region == sPlayerRegion)
    {
        sCurX = sPlayerX;
        sCurY = sPlayerY;
        return;
    }
    for (int32_t y = 0; y < GRID_H; y++)
    {
        for (int32_t x = 0; x < GRID_W; x++)
        {
            const uint16_t s = UiGame_MapSecAt(region, 0, x, y);
            if (fly)
            {
                if (s < MAPSEC_NONE && sFlyable[s])
                {
                    sCurX = x;
                    sCurY = y;
                    return;
                }
            }
            else if (s != MAPSEC_NONE || UiGame_MapSecAt(region, 1, x, y) != MAPSEC_NONE)
            {
                sCurX = x;
                sCurY = y;
                return;
            }
        }
    }
    sCurX = 0;
    sCurY = 0;
}

static void MapEnter(const bool fly)
{
    sRegionCount = UiGame_MapRegionCount();
    if (sRegionCount < 1) sRegionCount = 1;
    if (sRegionCount > REGION_MAX) sRegionCount = REGION_MAX;

    sPlayerRegion = (int32_t)UiGame_PlayerMapPos(&sPlayerX, &sPlayerY);
    if (sPlayerRegion < 0 || sPlayerRegion >= sRegionCount)
        sPlayerRegion = 0;
    if (sPlayerX < 0 || sPlayerX >= GRID_W) sPlayerX = 0;
    if (sPlayerY < 0 || sPlayerY >= GRID_H) sPlayerY = 0;

    sRegion = sPlayerRegion;
    sCurX = sPlayerX;
    sCurY = sPlayerY;
    sZoom = false;

    if (fly)
    {
        for (uint16_t s = 0; s < MAPSEC_NONE; s++)
            sFlyable[s] = UiGame_CanFlyTo(s) ? 1u : 0u;
    }
}

static void MapBounds(int32_t *const x, int32_t *const y, int32_t *const w, int32_t *const h)
{
    if (gUiTheme->gbaFrame) { *x = GBA_MAP_X; *y = GBA_MAP_Y; *w = GBA_MAP_W; *h = GBA_MAP_H; }
    else                    { *x = MAP_X;     *y = MAP_Y;     *w = MAP_W;     *h = MAP_H; }
}

static int32_t CellPx(void)
{
    return gUiTheme->gbaFrame ? (sZoom ? 40 : 20) : (sZoom ? 48 : 24);
}

static void MapOrigin(int32_t *const ox, int32_t *const oy)
{
    if (!sZoom)
    {
        *ox = 0;
        *oy = 0;
        return;
    }
    int32_t mx, my, mw, mh;
    MapBounds(&mx, &my, &mw, &mh);
    const int32_t c = CellPx();
    const int32_t fullW = SPR_TILES_W * c;
    const int32_t fullH = SPR_TILES_H * c;
    int32_t x = (sCurX + GRID_OFF_TILES) * c + c / 2 - mw / 2;
    int32_t y = (sCurY + GRID_OFF_TILES) * c + c / 2 - mh / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > fullW - mw) x = fullW - mw;
    if (y > fullH - mh) y = fullH - mh;
    *ox = x;
    *oy = y;
}

static uint16_t MapSec(void)
{
    return UiGame_MapSecAt(sRegion, 0, sCurX, sCurY);
}

static uint16_t DungeonSec(void)
{
    return UiGame_MapSecAt(sRegion, 1, sCurX, sCurY);
}

static bool MapUpdate(const UiInput *const in)
{
    const int32_t bx = sCurX, by = sCurY;
    if ((in->repeat & VITA_BTN_LEFT) && sCurX > 0) sCurX--;
    if ((in->repeat & VITA_BTN_RIGHT) && sCurX < GRID_W - 1) sCurX++;
    if ((in->repeat & VITA_BTN_UP) && sCurY > 0) sCurY--;
    if ((in->repeat & VITA_BTN_DOWN) && sCurY < GRID_H - 1) sCurY++;

    int32_t mx, my, mw, mh;
    MapBounds(&mx, &my, &mw, &mh);
    if (UiHit(in, mx, my, mw, mh))
    {
        int32_t ox, oy;
        MapOrigin(&ox, &oy);
        const int32_t c = CellPx();
        int32_t cx = (in->tx - mx + ox) / c - GRID_OFF_TILES;
        int32_t cy = (in->ty - my + oy) / c - GRID_OFF_TILES;
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx >= GRID_W) cx = GRID_W - 1;
        if (cy >= GRID_H) cy = GRID_H - 1;
        sCurX = cx;
        sCurY = cy;
    }

    if (sCurX != bx || sCurY != by)
    {
        PlaySE(SE_SELECT);
        return true;
    }
    return false;
}

static void MapDraw(const bool fly, const uint32_t frame)
{
    int32_t mx, my, mw, mh;
    MapBounds(&mx, &my, &mw, &mh);
    UiDraw_Rect(mx - 3, my - 3, mw + 6, mh + 6, gUiTheme->gbaFrame ? UI_RGBA(41, 49, 49, 255) : UI_COL_LINE_DARK);

    int32_t ox, oy;
    MapOrigin(&ox, &oy);
    const int32_t c = CellPx();

    UiDraw_PushClip(mx, my, mw, mh);
    uint16_t su = 0, sv = 0, sw = 0, sh = 0;
    if (UiAssets_Sprite(sRegionSprites[sRegion], &su, &sv, &sw, &sh))
        UiDraw_Quad(mx - ox, my - oy, SPR_TILES_W * c, SPR_TILES_H * c, su, sv, su + sw, sv + sh, 0xFFFFFFFFu);

    const uint16_t sec = MapSec();
    const uint16_t dun = DungeonSec();
    for (int32_t y = 0; y < GRID_H; y++)
    {
        for (int32_t x = 0; x < GRID_W; x++)
        {
            const uint16_t s = UiGame_MapSecAt(sRegion, 0, x, y);
            const uint16_t d = UiGame_MapSecAt(sRegion, 1, x, y);
            const int32_t px = mx + (x + GRID_OFF_TILES) * c - ox;
            const int32_t py = my + (y + GRID_OFF_TILES) * c - oy;

            const bool matchOver = (s != MAPSEC_NONE && s == sec);
            const bool matchDun = (d != MAPSEC_NONE && d == dun);
            if (matchOver || matchDun)
            {
                UiDraw_Rect(px, py, c, c, gUiTheme->gbaFrame ? UI_RGBA(255, 230, 57, 88) : UI_RGBA(246, 207, 87, 90));
                if (gUiTheme->gbaFrame)
                    UiDraw_Frame(px, py, c, c, 1, UI_RGBA(255, 230, 57, 220));
            }
            if (d != MAPSEC_NONE)
            {
                // Distinct orange/cyan dungeon dot so caves/forests/towers are visible at a glance.
                const int32_t dot = c >= 40 ? 8 : 6;
                UiDraw_Rect(px + c - dot - 2, py + c - dot - 2, dot, dot, UI_RGBA(248, 128, 32, 255));
                UiDraw_Frame(px + c - dot - 2, py + c - dot - 2, dot, dot, 1, C_WHITE);
            }
            if (fly && s < MAPSEC_NONE && sFlyable[s])
            {
                if (gUiTheme->gbaFrame)
                {
                    UiDraw_Frame(px + c / 2 - 5, py + c / 2 - 5, 10, 10, 2, C_WHITE);
                    UiDraw_Rect(px + c / 2 - 3, py + c / 2 - 3, 6, 6, UI_RGBA(90, 213, 131, 255));
                }
                else
                {
                    UiDraw_Frame(px + 2, py + 2, c - 4, c - 4, 2, UI_RGBA(63, 183, 107, 255));
                }
            }
        }
    }

    if (sRegion == sPlayerRegion)
    {
        const int32_t hx = mx + (sPlayerX + GRID_OFF_TILES) * c - ox + c / 2 - 16;
        const int32_t hy = my + (sPlayerY + GRID_OFF_TILES) * c - oy + c / 2 - 16;
        UiDraw_Sprite(UiGame_PlayerIsMale() ? UI_SPR_HEAD_RED : UI_SPR_HEAD_LEAF, hx, hy, 2);
    }

    const int32_t curPx = mx + (sCurX + GRID_OFF_TILES) * c - ox;
    const int32_t curPy = my + (sCurY + GRID_OFF_TILES) * c - oy;
    if (gUiTheme->gbaFrame)
    {
        const uint32_t curCol = ((frame / 16) & 1) ? UI_RGBA(230, 8, 8, 255) : C_WHITE;
        UiDraw_Frame(curPx - 1, curPy - 1, c + 2, c + 2, 2, C_WHITE);
        UiDraw_Frame(curPx - 3, curPy - 3, c + 6, c + 6, 3, curCol);
    }
    else
    {
        UiDraw_Frame(curPx, curPy, c, c, 3, ((frame / 16) & 1) ? UI_COL_ON_FRAME : UI_COL_SELECT);
    }
    UiDraw_PopClip();
}

// =====================================================================================
// Town Map (Overworld + Dungeon inspection + Wild Encounters popup)
// =====================================================================================
static const char *const sMethods[UI_WILD_COUNT] = { UI_N("GRASS"), UI_N("SURF"), UI_N("FISH"), UI_N("ROCK") };
static const char *const sRods[4] = { UI_N("ALL"), UI_N("OLD"), UI_N("GOOD"), UI_N("SUPER") };
static const char *const sTimes[3] = { UI_N("MORNING"), UI_N("DAY"), UI_N("NIGHT") };

static int32_t sTabX[REGION_MAX], sTabW[REGION_MAX];
static int32_t sMethod = UI_WILD_GRASS;
static int32_t sRod = 0;
static int32_t sTimePhase = 1;
static bool sUseDungeonLayer = false;
static UiWild sWild[28];
static int32_t sWildN = 0;
static uint16_t sWildSec = MAPSEC_NONE;
static bool sEncOpen = false;
static UiList sEncList;
static bool sWallMode = false;
static bool sWallModeNext = false;

static bool SectionHasAnyWild(const uint16_t sec)
{
    if (sec >= MAPSEC_NONE)
        return false;
    UiWild tmp[4];
    for (int32_t m = 0; m < UI_WILD_COUNT; m++)
    {
        if (UiGame_Wild(sec, m, 0, tmp, 4) > 0)
            return true;
    }
    return false;
}

static void RefreshWild(const bool autoSelectLayer)
{
    const uint16_t over = MapSec();
    const uint16_t dun = DungeonSec();
    if (autoSelectLayer)
    {
        if (dun != MAPSEC_NONE && (!SectionHasAnyWild(over) || sUseDungeonLayer))
            sUseDungeonLayer = SectionHasAnyWild(dun);
        else if (dun == MAPSEC_NONE)
            sUseDungeonLayer = false;
    }
    sWildSec = (sUseDungeonLayer && dun != MAPSEC_NONE) ? dun : over;
    if (sWildSec == MAPSEC_NONE && dun != MAPSEC_NONE)
        sWildSec = dun;

    const int32_t sub = (sMethod == UI_WILD_FISH) ? sRod : (sMethod == UI_WILD_GRASS ? sTimePhase : 0);
    sWildN = UiGame_Wild(sWildSec, sMethod, sub, sWild, 28);
    if (autoSelectLayer && sWildN == 0 && sWildSec != MAPSEC_NONE)
    {
        for (int32_t m = 0; m < UI_WILD_COUNT; m++)
        {
            const int32_t mSub = (m == UI_WILD_GRASS) ? sTimePhase : 0;
            const int32_t count = UiGame_Wild(sWildSec, m, mSub, sWild, 28);
            if (count > 0)
            {
                sMethod = m;
                sWildN = count;
                break;
            }
        }
    }
    UiList_Reset(&sEncList, sWildN, POP_ROWS);
}

static void MapScreenEnter(void)
{
    sWallMode = sWallModeNext;
    sWallModeNext = false;
    MapEnter(false);
    sEncOpen = false;
    sMethod = UI_WILD_GRASS;
    sRod = 0;
    sTimePhase = (int32_t)UiGame_CurrentTimePhase();
    sUseDungeonLayer = false;
    RefreshWild(true);
}

static void FilterLabel(char *const buf, const size_t cap)
{
    if (sMethod == UI_WILD_FISH)
        snprintf(buf, cap, UI_TR("FISH · %s"), sRod ? UiLocale_Lookup(sRods[sRod]) : UI_TR("ALL RODS"));
    else if (sMethod == UI_WILD_GRASS)
        snprintf(buf, cap, "%s · %s", UiLocale_Lookup(sMethods[sMethod]), UiLocale_Lookup(sTimes[sTimePhase % 3]));
    else
        snprintf(buf, cap, "%s", UiLocale_Lookup(sMethods[sMethod]));
}

static void AreaTitle(char *const buf, const size_t cap)
{
    char overName[32], dunName[32];
    const uint16_t over = MapSec();
    const uint16_t dun = DungeonSec();
    UiGame_MapSecName(over, overName, sizeof(overName));
    if (dun != MAPSEC_NONE)
    {
        UiGame_MapSecName(dun, dunName, sizeof(dunName));
        if (over != MAPSEC_NONE && strcmp(overName, dunName) != 0)
            snprintf(buf, cap, "%s / %s", overName, dunName);
        else
            snprintf(buf, cap, "%s", dunName);
    }
    else
    {
        snprintf(buf, cap, "%s", overName);
    }
}

#define POP_X 122
#define POP_Y 48
#define POP_W 716
#define POP_H 448
static int32_t sSubX[2][4], sSubW[2][4];

static void SubTabs(const int32_t row, int32_t x, const int32_t y, const char *const *const labels, const int32_t n, const int32_t cur)
{
    for (int32_t i = 0; i < n; i++)
    {
        const int32_t w = UiText_Width(UI_FACE_VT323, UI_BODY_PX, UiLocale_Lookup(labels[i])) + 24;
        sSubX[row][i] = x;
        sSubW[row][i] = w;
        if (i == cur)
        {
            UiDraw_Rect(x, y, w, 30, gUiTheme->chipEdge);
            UiDraw_RectV(x + 2, y + 2, w - 4, 26, gUiTheme->chipTop, gUiTheme->chipBot);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 12, y - 1, UiLocale_Lookup(labels[i]), gUiTheme->chipInk);
        }
        else
        {
            UiDraw_Rect(x, y, w, 30, gUiTheme->paperShade);
            UiDraw_Rect(x + 2, y + 2, w - 4, 26, UI_COL_PAPER1);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 12, y - 1, UiLocale_Lookup(labels[i]), UI_COL_INK_SOFT);
        }
        x += w + 2;
    }
}

static int32_t SubHit(const UiInput *const in, const int32_t row, const int32_t n, const int32_t y)
{
    for (int32_t i = 0; i < n; i++)
        if (UiHit(in, sSubX[row][i], y, sSubW[row][i], 30))
            return i;
    return -1;
}

static bool UpdateEncPopup(const UiInput *const in)
{
    if ((in->pressed & VITA_BTN_CIRCLE) || (in->tap && !UiHit(in, POP_X, POP_Y, POP_W, POP_H)))
    {
        PlaySE(SE_SELECT);
        sEncOpen = false;
        return true;
    }

    int32_t m = sMethod;
    if (in->pressed & VITA_BTN_TRIANGLE)
        m = (sMethod + 1) % UI_WILD_COUNT;
    const int32_t hm = SubHit(in, 0, UI_WILD_COUNT, POP_Y + 54);
    if (hm >= 0) m = hm;
    if (m != sMethod)
    {
        sMethod = m;
        PlaySE(SE_SELECT);
        RefreshWild(false);
    }

    if (sMethod == UI_WILD_FISH)
    {
        int32_t d = (in->repeat & VITA_BTN_LEFT) ? -1 : (in->repeat & VITA_BTN_RIGHT) ? 1 : 0;
        const int32_t h2 = SubHit(in, 1, 4, POP_Y + 92);
        if (h2 >= 0) { sRod = h2; d = 0; PlaySE(SE_SELECT); RefreshWild(false); }
        if (d != 0)  { sRod = (sRod + d + 4) % 4; PlaySE(SE_SELECT); RefreshWild(false); }
    }
    else if (sMethod == UI_WILD_GRASS)
    {
        int32_t d = (in->repeat & VITA_BTN_LEFT) ? -1 : (in->repeat & VITA_BTN_RIGHT) ? 1 : 0;
        const int32_t h2 = SubHit(in, 1, 3, POP_Y + 92);
        if (h2 >= 0) { sTimePhase = h2; d = 0; PlaySE(SE_SELECT); RefreshWild(false); }
        if (d != 0)  { sTimePhase = (sTimePhase + d + 3) % 3; PlaySE(SE_SELECT); RefreshWild(false); }
    }

    if ((in->pressed & VITA_BTN_SELECT) && MapSec() != MAPSEC_NONE && DungeonSec() != MAPSEC_NONE)
    {
        sUseDungeonLayer = !sUseDungeonLayer;
        PlaySE(SE_SELECT);
        RefreshWild(false);
    }

    UiList_Update(&sEncList, in, POP_X + 16, POP_Y + 134, POP_W - 32, 44, false);
    return true;
}

static void DrawEncPopup(void)
{
    char buf[64], name[48];
    UiDraw_Rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, UI_COL_DIM);
    UiGame_MapSecName(sWildSec, name, sizeof(name));
    UiW_Window(POP_X, POP_Y, POP_W, POP_H, name);
    snprintf(buf, sizeof(buf), UI_TR("%d POKéMON"), (int)sWildN);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, POP_X + POP_W - 20 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf), POP_Y + 4, buf, UI_COL_ON_FRAME);

    SubTabs(0, POP_X + 16, POP_Y + 54, sMethods, UI_WILD_COUNT, sMethod);
    if (sMethod == UI_WILD_FISH)
        SubTabs(1, POP_X + 16, POP_Y + 92, sRods, 4, sRod);
    else if (sMethod == UI_WILD_GRASS)
        SubTabs(1, POP_X + 16, POP_Y + 92, sTimes, 3, sTimePhase);
    else if (MapSec() != MAPSEC_NONE && DungeonSec() != MAPSEC_NONE)
        UiText_Draw(UI_FACE_VT323, 24, POP_X + 20, POP_Y + 96, UI_TR("SELECT: switch Overworld / Dungeon"), UI_COL_INK_SOFT);

    for (int32_t r = 0; r < POP_ROWS && sEncList.first + r < sWildN; r++)
    {
        const int32_t i = sEncList.first + r, y = POP_Y + 134 + r * 44;
        const bool on = i == sEncList.sel;
        const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
        UiW_Row(POP_X + 16, y, POP_W - 32, 44, on, i);
        UiSpr_MonIcon(sWild[i].species, 0, POP_X + 44, y + 4, 1);
        UiGame_SpeciesName(sWild[i].species, name, sizeof(name));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, POP_X + 84, y + 6, name, ink);
        if (sWild[i].rod)
            UiW_Chip(POP_X + 410, y + 10, UiLocale_Lookup(sRods[sWild[i].rod]), false);
        if (sWild[i].minLv == sWild[i].maxLv)
            snprintf(buf, sizeof(buf), UI_TR("Lv%u"), (unsigned)sWild[i].minLv);
        else
            snprintf(buf, sizeof(buf), UI_TR("Lv%u-%u"), (unsigned)sWild[i].minLv, (unsigned)sWild[i].maxLv);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, POP_X + POP_W - 36 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf), y + 6, buf, ink);
    }
    if (sWildN == 0)
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, POP_X + 40, POP_Y + 140, UI_TR("NO WILD DATA FOR THIS FILTER"), UI_COL_INK_SOFT);
    UiW_ScrollBar(POP_X + POP_W - 25, POP_Y + 134, POP_ROWS * 44, sEncList.first, POP_ROWS, sWildN);

    const int32_t fy = POP_Y + POP_H - 46;
    UiDraw_Rect(POP_X + 3, fy, POP_W - 6, 2, gUiTheme->paperShade);
    UiDraw_Rect(POP_X + 3, fy + 2, POP_W - 6, 41, gUiTheme->paper1);
    int32_t fx = POP_X + 20;
    UiW_Glyph(fx, fy + 10, UI_GLYPH_TRIANGLE);
    fx += 30 + UiText_Draw(UI_FACE_PS2P, 16, fx + 30, fy + 15, UI_TR("Method"), UI_COL_INK) + 24;
    if (sMethod == UI_WILD_FISH)
        fx += UiText_Draw(UI_FACE_PS2P, 16, fx, fy + 15, UI_TR("<> Rod"), UI_COL_INK) + 24;
    else if (sMethod == UI_WILD_GRASS)
        fx += UiText_Draw(UI_FACE_PS2P, 16, fx, fy + 15, UI_TR("<> Time"), UI_COL_INK) + 24;
    UiW_Glyph(fx, fy + 10, UI_GLYPH_CIRCLE);
    UiText_Draw(UI_FACE_PS2P, 16, fx + 30, fy + 15, UI_TR("Close"), UI_COL_INK);
}

static bool MapScreenUpdate(const UiInput *const in)
{
    sFrame++;
    if (sEncOpen)
        return UpdateEncPopup(in);

    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        return false;
    }

    const bool gba = gUiTheme->gbaFrame;
    if (sRegionCount > 1)
    {
        int32_t nextReg = -1;
        if (gba)
        {
            const int32_t ph = UiW_GbaPagerHit(in, true, sRegionCount);
            if (ph != 0)
                nextReg = (sRegion + sRegionCount + ph) % sRegionCount;
        }
        else
        {
            nextReg = UiKit_TabHit(in, sTabX, sTabW, sRegionCount);
        }
        if (in->pressed & VITA_BTN_L) nextReg = (sRegion + sRegionCount - 1) % sRegionCount;
        if (in->pressed & VITA_BTN_R) nextReg = (sRegion + 1) % sRegionCount;
        if (nextReg >= 0 && nextReg != sRegion)
        {
            sRegion = nextReg;
            FindDefaultCursorForRegion(sRegion, false);
            PlaySE(SE_SELECT);
            RefreshWild(true);
            return true;
        }
    }

    if (MapUpdate(in))
        RefreshWild(true);

    if (in->pressed & VITA_BTN_SQUARE)
    {
        sZoom = !sZoom;
        PlaySE(SE_SELECT);
    }

    if (!sWallMode)
    {
        if (in->pressed & VITA_BTN_TRIANGLE)
        {
            sMethod = (sMethod + 1) % UI_WILD_COUNT;
            PlaySE(SE_SELECT);
            RefreshWild(false);
        }
        if ((in->pressed & VITA_BTN_SELECT) && MapSec() != MAPSEC_NONE && DungeonSec() != MAPSEC_NONE)
        {
            sUseDungeonLayer = !sUseDungeonLayer;
            PlaySE(SE_SELECT);
            RefreshWild(false);
        }
        const bool hitEnc = gba ? UiHit(in, 612, 220, 334, 312) : UiHit(in, 652, 176, 292, 316);
        if ((in->pressed & VITA_BTN_CROSS) || hitEnc)
        {
            sEncOpen = true;
            PlaySE(SE_SELECT);
            UiList_Reset(&sEncList, sWildN, POP_ROWS);
        }
    }
    else if (in->pressed & VITA_BTN_CROSS)
    {
        sZoom = !sZoom;
        PlaySE(SE_SELECT);
    }

    return true;
}

static void MapScreenBuildGba(void)
{
    char buf[96], secName[48], dunName[48];
    const uint16_t over = MapSec();
    const uint16_t dun = DungeonSec();
    UiGame_MapSecName(over, secName, sizeof(secName));

    if (sRegionCount > 1)
        UiW_GbaTopBarPages(UI_GBA_BG_POKENAV, UI_SPR_START_MAP, UI_TR("TOWN MAP"), sRegionLabels, sRegionCount, sRegion);
    else
        UiW_GbaTopBar(UI_GBA_BG_POKENAV, UI_SPR_START_MAP, UI_TR("TOWN MAP"), 0, 0, false, UiLocale_Lookup(sRegionLabels[0]));

    int32_t rx = UI_SCREEN_W - 14;
    UiW_GbaHelpBadge(&rx, "O", UI_TR("BACK"), true);
    if (!sWallMode)
    {
        UiW_GbaHelpBadge(&rx, "SQ", UI_TR("ZOOM"), false);
        UiW_GbaHelpBadge(&rx, "TRI", UI_TR("METHOD"), false);
        UiW_GbaHelpBadge(&rx, "X", UI_TR("WILD"), false);
    }
    else
    {
        UiW_GbaHelpBadge(&rx, "X/SQ", UI_TR("ZOOM"), false);
    }

    // Left dark-well Kanto/Sevii map card (14, 60, 590, 472)
    UiW_GbaWin(14, 60, 590, 472, UI_GBA_WIN_DARK);
    MapDraw(false, sFrame);

    // Bottom radar strip inside map card (26, 440, 566, 78)
    UiW_Bevel(26, 440, 566, 78, 4, UI_RGBA(16, 24, 28, 255), UI_RGBA(27, 44, 50, 255), UI_RGBA(27, 44, 50, 255),
              2, UI_RGBA(72, 120, 136, 255), UI_RGBA(16, 24, 28, 255), 0);
    AreaTitle(buf, sizeof(buf));
    UiW_GbaText(UI_FACE_PS2P, 16, 38, 452, buf, UI_RGBA(255, 230, 57, 255), UI_GBA_SHAD_DARK);
    snprintf(buf, sizeof(buf), sZoom ? UI_TR("%s · ZOOM x2") : UI_TR("%s · FULL MAP"),
             UiLocale_Lookup(sRegionLabels[sRegion]));
    UiW_GbaText(UI_FACE_VT323, 24, 38, 482, buf, UI_RGBA(115, 255, 172, 255), UI_GBA_SHAD_DARK);

    // Right column (612, 60, 334)
    UiW_GbaWin(612, 60, 334, 152, UI_GBA_WIN_DEFAULT);
    const char *badge = (over < MAPSEC_NONE && UiGame_CanFlyTo(over)) ? UI_TR("FLY OK") : UI_TR("AREA");
    UiW_GbaSectionStrip(624, 72, 310, 26, UI_GBA_TAG_BLUE, over != MAPSEC_NONE ? secName : UI_TR("OCEAN / WILD"), badge);
    if (dun != MAPSEC_NONE)
    {
        UiGame_MapSecName(dun, dunName, sizeof(dunName));
        snprintf(buf, sizeof(buf), UI_TR("Dungeon: %s"), dunName);
        UiW_GbaText(UI_FACE_VT323, 24, 624, 104, buf, UI_RGBA(213, 65, 41, 255), UI_GBA_SHAD_LIGHT);
    }
    else
    {
        UiW_GbaText(UI_FACE_VT323, 24, 624, 104,
                    sRegionCount > 1 ? UI_TR("L / R: switch Kanto & Sevii") : UI_TR("Tap map or D-pad to scan"),
                    UI_RGBA(72, 80, 88, 255), UI_GBA_SHAD_LIGHT);
    }
    UiW_GbaText(UI_FACE_VT323, 24, 624, 134,
                sWallMode ? UI_TR("Wall Town Map · X/SQ to zoom") : UI_TR("Tap below or X for wild list"),
                UI_RGBA(96, 104, 96, 255), UI_GBA_SHAD_LIGHT);

    UiW_GbaWin(612, 220, 334, 312, UI_GBA_WIN_DEFAULT);
    FilterLabel(buf, sizeof(buf));
    UiW_GbaSectionStrip(624, 232, 310, 26, UI_GBA_TAG_TEAL, UI_TR("ENCOUNTERS"), sWallMode ? UI_TR("KANTO") : buf);
    if (sWallMode)
    {
        UiW_GbaTextWrapped(UI_FACE_VT323, 28, 626, 272, 306, 28,
                           UI_TR("Inspecting the Kanto Town Map. Move the cursor over cities, routes and landmarks."),
                           UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT, 5);
    }
    else
    {
        for (int32_t i = 0; i < sWildN && i < 5; i++)
        {
            const int32_t ry = 266 + i * 48;
            const bool hi = (i == 0);
            UiW_GbaBox(624, ry, 310, 44, 4, hi ? UI_RGBA(230, 148, 65, 255) : UI_RGBA(184, 190, 176, 255),
                       hi ? UI_RGBA(255, 246, 205, 255) : UI_RGBA(244, 245, 236, 255),
                       hi ? UI_RGBA(255, 246, 205, 255) : UI_RGBA(244, 245, 236, 255), C_WHITE, 0);
            UiDraw_Sprite(UI_SPR_BALL_POKE, 632, ry + 14, 1);
            UiSpr_MonIcon(sWild[i].species, 0, 652, ry + 6, 1);
            UiGame_SpeciesName(sWild[i].species, secName, sizeof(secName));
            UiW_GbaTextMid(UI_FACE_VT323, 24, 688, ry, 44, secName, UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT);
            if (sWild[i].minLv == sWild[i].maxLv)
                snprintf(buf, sizeof(buf), UI_TR("Lv%u"), (unsigned)sWild[i].minLv);
            else
                snprintf(buf, sizeof(buf), UI_TR("Lv%u-%u"), (unsigned)sWild[i].minLv, (unsigned)sWild[i].maxLv);
            UiW_GbaTextMid(UI_FACE_VT323, 24, 926 - UiText_Width(UI_FACE_VT323, 24, buf), ry, 44, buf, UI_RGBA(213, 65, 41, 255), UI_GBA_SHAD_LIGHT);
        }
        if (sWildN == 0)
            UiW_GbaTextCentred(UI_FACE_VT323, 28, 624, 290, 310, 100, UI_TR("NO WILD POKéMON"), UI_RGBA(112, 120, 112, 255), UI_GBA_SHAD_LIGHT);
    }

    if (sEncOpen)
        DrawEncPopup();
}

static void MapScreenBuild(void)
{
    if (gUiTheme->gbaFrame)
    {
        MapScreenBuildGba();
        return;
    }

    char buf[96], name[48], dunName[48];
    UiKit_Header(UI_TR("TOWN MAP"), sRegionLabels, sRegionCount, sRegion, sTabX, sTabW, 12);
    AreaTitle(buf, sizeof(buf));
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    MapDraw(false, sFrame);

    UiW_Panel(652, 60, 292, 108);
    UiGame_MapSecName(MapSec(), name, sizeof(name));
    UiDraw_PushClip(664, 66, 268, 36);
    UiText_Draw(UI_FACE_PS2P, 16, 668, 76, name, UI_COL_INK);
    UiDraw_PopClip();
    if (DungeonSec() != MAPSEC_NONE)
    {
        UiGame_MapSecName(DungeonSec(), dunName, sizeof(dunName));
        snprintf(buf, sizeof(buf), UI_TR("Dungeon: %s"), dunName);
        UiText_Draw(UI_FACE_VT323, 26, 668, 108, buf, gUiTheme->accent ? gUiTheme->accent : gUiTheme->selEdge);
    }
    else
    {
        UiText_Draw(UI_FACE_VT323, 26, 668, 108,
                    sZoom ? UI_TR("View: x2 Zoom") : UI_TR("View: Full Map"), UI_COL_INK_SOFT);
    }

    UiW_Panel(652, 176, 292, 316);
    FilterLabel(buf, sizeof(buf));
    UiW_PanelHead(652, 176, 292, sWallMode ? UI_TR("TOWN MAP") : buf, NULL);
    if (sWallMode)
    {
        UiW_TextWrapped(668, 224, 260, 30,
                        UI_TR("Wall Town Map. Tap anywhere on the map or use D-Pad to inspect areas."),
                        UI_COL_INK_SOFT, 5);
    }
    else
    {
        for (int32_t i = 0; i < sWildN && i < 6; i++)
        {
            const int32_t y = 222 + i * 44;
            UiW_Row(658, y, 280, 44, false, i);
            UiSpr_MonIcon(sWild[i].species, 0, 664, y + 4, 1);
            UiGame_SpeciesName(sWild[i].species, name, sizeof(name));
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 700, y + 6, name, UI_COL_INK);
            snprintf(buf, sizeof(buf), UI_TR("Lv%u"), (unsigned)sWild[i].minLv);
            UiText_Draw(UI_FACE_VT323, 26, 930 - UiText_Width(UI_FACE_VT323, 26, buf), y + 8, buf, UI_COL_INK_SOFT);
        }
        if (sWildN == 0)
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 668, 232, UI_TR("NO WILD DATA"), UI_COL_INK_SOFT);
    }

    int32_t fx = 16;
    if (!sWallMode)
    {
        UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Encounters"));
        UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
        UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Method"));
        UiW_FooterGlyph(&fx, UI_GLYPH_SQUARE, UI_TR("Zoom"));
    }
    else
    {
        UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Zoom"));
        UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Close"));
    }
    UiKit_FooterClock();

    if (sEncOpen)
        DrawEncPopup();
}

const UiScreen gUiMapScreen = { MapScreenEnter, MapScreenUpdate, MapScreenBuild, NULL };

bool Ui_OpenTownMap(const bool wallMap, void (*const exitCallback)(void))
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    sWallModeNext = wallMap;
    UiCore_OpenHosted(&gUiMapScreen, exitCallback != NULL ? exitCallback : CB2_ReturnToField);
    return true;
}

void UiMap_OpenFromBag(void)
{
    sWallModeNext = false;
    UiNav_Push(&gUiMapScreen);
}

// =====================================================================================
// Fly Map
// =====================================================================================
static uint8_t sFlySlot = 0;
static uint16_t sFlySec = MAPSEC_NONE;
static int32_t sFlyTabX[REGION_MAX], sFlyTabW[REGION_MAX];

void UiFly_Set(const uint8_t partySlot)
{
    sFlySlot = partySlot;
}

static void FlyEnter(void)
{
    MapEnter(true);
}

static void FlyAfter(void)
{
    UiGame_ExecuteFly(sFlySlot, sFlySec);
}

static bool FlyUpdate(const UiInput *const in)
{
    sFrame++;
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        return false;
    }

    const bool gba = gUiTheme->gbaFrame;
    if (sRegionCount > 1)
    {
        int32_t nextReg = -1;
        if (gba)
        {
            const int32_t ph = UiW_GbaPagerHit(in, true, sRegionCount);
            if (ph != 0)
                nextReg = (sRegion + sRegionCount + ph) % sRegionCount;
        }
        else
        {
            nextReg = UiKit_TabHit(in, sFlyTabX, sFlyTabW, sRegionCount);
        }
        if (in->pressed & VITA_BTN_L) nextReg = (sRegion + sRegionCount - 1) % sRegionCount;
        if (in->pressed & VITA_BTN_R) nextReg = (sRegion + 1) % sRegionCount;
        if (nextReg >= 0 && nextReg != sRegion)
        {
            sRegion = nextReg;
            FindDefaultCursorForRegion(sRegion, true);
            PlaySE(SE_SELECT);
            return true;
        }
    }

    if (in->pressed & VITA_BTN_SQUARE)
    {
        sZoom = !sZoom;
        PlaySE(SE_SELECT);
    }

    const int32_t prevX = sCurX, prevY = sCurY;
    MapUpdate(in);
    const bool tapMapSame = (in->tap && sCurX == prevX && sCurY == prevY
                             && UiHit(in, gba ? GBA_MAP_X : MAP_X, gba ? GBA_MAP_Y : MAP_Y, gba ? GBA_MAP_W : MAP_W, gba ? GBA_MAP_H : MAP_H));
    const bool tapPanel = UiHit(in, gba ? 612 : 652, 60, gba ? 334 : 292, gba ? 472 : 432);
    if ((in->pressed & VITA_BTN_CROSS) || tapMapSame || tapPanel)
    {
        const uint16_t sec = MapSec();
        if (sec < MAPSEC_NONE && sFlyable[sec])
        {
            PlaySE(SE_USE_ITEM);
            sFlySec = sec;
            UiNav_Close(FlyAfter);
        }
        else if (in->pressed & VITA_BTN_CROSS)
        {
            PlaySE(SE_FAILURE);
            UiKit_Toast(UI_TR("You can't fly there."));
        }
    }
    return true;
}

static void FlyBuild(void)
{
    int32_t destCount = 0;
    for (uint16_t s = 0; s < MAPSEC_NONE; s++)
        destCount += sFlyable[s];

    char buf[64], name[48];
    const uint16_t sec = MapSec();
    const bool canFly = (sec < MAPSEC_NONE && sFlyable[sec]);
    UiGame_MapSecName(sec, name, sizeof(name));

    if (gUiTheme->gbaFrame)
    {
        if (sRegionCount > 1)
            UiW_GbaTopBarPages(UI_GBA_BG_POKENAV, UI_SPR_START_MAP, UI_TR("FLY"), sRegionLabels, sRegionCount, sRegion);
        else
        {
            snprintf(buf, sizeof(buf), UI_TR("%d DESTINATIONS"), (int)destCount);
            UiW_GbaTopBar(UI_GBA_BG_POKENAV, UI_SPR_START_MAP, UI_TR("FLY"), 1, 0, true, buf);
        }

        int32_t rx = UI_SCREEN_W - 14;
        UiW_GbaHelpBadge(&rx, "O", UI_TR("CANCEL"), true);
        UiW_GbaHelpBadge(&rx, "SQ", UI_TR("ZOOM"), false);
        UiW_GbaHelpBadge(&rx, "X", UI_TR("FLY"), false);

        UiW_GbaWin(14, 60, 590, 472, UI_GBA_WIN_DARK);
        MapDraw(true, sFrame);

        UiW_Bevel(26, 440, 566, 78, 4, UI_RGBA(16, 24, 28, 255), UI_RGBA(27, 44, 50, 255), UI_RGBA(27, 44, 50, 255),
                  2, UI_RGBA(72, 120, 136, 255), UI_RGBA(16, 24, 28, 255), 0);
        UiW_GbaText(UI_FACE_PS2P, 16, 38, 452, name, UI_RGBA(255, 230, 57, 255), UI_GBA_SHAD_DARK);
        UiW_GbaText(UI_FACE_VT323, 24, 38, 482,
                    canFly ? UI_TR("Valid destination - press X or tap to FLY!") : UI_TR("Move cursor to a green square to FLY."),
                    canFly ? UI_RGBA(115, 255, 172, 255) : UI_RGBA(184, 212, 224, 255), UI_GBA_SHAD_DARK);

        UiW_GbaWin(612, 60, 334, 472, UI_GBA_WIN_DEFAULT);
        UiW_GbaSectionStrip(624, 72, 310, 26, UI_GBA_TAG_BLUE, UI_TR("DESTINATION"), canFly ? UI_TR("FLY OK") : name);

        UiMon m;
        UiGame_Mon(sFlySlot, &m);
        UiDraw_Frame(624, 110, 310, 108, 2, UI_RGBA(184, 190, 176, 255));
        UiDraw_Rect(626, 112, 306, 104, UI_RGBA(244, 245, 236, 255));
        UiSpr_MonIcon(m.species, (uint8_t)((sFrame / 8) & 1), 636, 132, 1);
        snprintf(buf, sizeof(buf), UI_TR("%s will use FLY"), m.name);
        UiW_GbaTextWrapped(UI_FACE_VT323, 28, 680, 126, 240, 28, buf, UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT, 3);

        UiW_GbaSectionStrip(624, 234, 310, 26, UI_GBA_TAG_GREEN, UI_TR("MAP GUIDE"), NULL);
        UiW_GbaTextWrapped(UI_FACE_VT323, 26, 632, 272, 294, 26,
                           sRegionCount > 1
                           ? UI_TR("Tap map or D-Pad. L/R switches Kanto & Sevii. Green squares are unlocked FLY targets.")
                           : UI_TR("Tap anywhere on the map or use D-Pad. Green squares are cities and towns unlocked for FLY."),
                           UI_RGBA(72, 80, 72, 255), UI_GBA_SHAD_LIGHT, 5);
        UiW_Button(628, 464, 302, UI_TR("FLY HERE"), canFly ? UI_BTN_GREEN : UI_BTN_AMBER, false);
        return;
    }

    UiKit_Header(UI_TR("FLY"), sRegionLabels, sRegionCount, sRegion, sFlyTabX, sFlyTabW, 12);
    snprintf(buf, sizeof(buf), UI_TR("Destinations: %d"), (int)destCount);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    MapDraw(true, sFrame);

    UiW_Panel(652, 60, 292, 124);
    UiDraw_PushClip(664, 66, 268, 36);
    UiText_Draw(UI_FACE_PS2P, 16, 668, 76, name, UI_COL_INK);
    UiDraw_PopClip();
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 668, 112, canFly ? UI_TR("You can fly here.") : "-",
                canFly ? UI_COL_HP_HIGH : UI_COL_INK_SOFT);

    UiW_Panel(652, 196, 292, 296);
    UiMon m;
    UiGame_Mon(sFlySlot, &m);
    UiSpr_MonIcon(m.species, (uint8_t)((sFrame / 8) & 1), 668, 214, 1);
    snprintf(buf, sizeof(buf), UI_TR("%s will FLY"), m.name);
    UiW_TextWrapped(712, 214, 216, 30, buf, UI_COL_INK, 2);
    UiW_TextWrapped(668, 286, 260, 30, UI_TR("Green frames: towns you can fly to."), UI_COL_INK_SOFT, 4);
    UiW_Button(668, 424, 260, UI_TR("FLY HERE"), canFly ? UI_BTN_GREEN : UI_BTN_AMBER, false);

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Fly here"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Cancel"));
    UiW_FooterGlyph(&fx, UI_GLYPH_SQUARE, UI_TR("Zoom"));
    UiKit_FooterClock();
}

const UiScreen gUiFlyScreen = { FlyEnter, FlyUpdate, FlyBuild, NULL };

// =====================================================================================
// Pokédex AREA Overlay (rendered into 624x432 at (16, 60))
// =====================================================================================
void UiMap_DrawArea(const uint8_t region, const uint8_t *const areas, const uint32_t frame)
{
    const uint8_t reg = region < REGION_MAX ? region : 0;
    const int32_t c = 24;

    UiDraw_Rect(MAP_X - 3, MAP_Y - 3, MAP_W + 6, MAP_H + 6, gUiTheme->gbaFrame ? UI_RGBA(41, 49, 49, 255) : UI_COL_LINE_DARK);
    UiDraw_PushClip(MAP_X, MAP_Y, MAP_W, MAP_H);

    uint16_t su = 0, sv = 0, sw = 0, sh = 0;
    if (UiAssets_Sprite(sRegionSprites[reg], &su, &sv, &sw, &sh))
        UiDraw_Quad(MAP_X, MAP_Y, SPR_TILES_W * c, SPR_TILES_H * c, su, sv, su + sw, sv + sh, 0xFFFFFFFFu);

    const uint32_t pulse = ((frame / 18) & 1) ? 168u : 112u;
    for (int32_t y = 0; y < GRID_H; y++)
    {
        for (int32_t x = 0; x < GRID_W; x++)
        {
            const uint16_t s0 = UiGame_MapSecAt(reg, 0, x, y);
            const uint16_t s1 = UiGame_MapSecAt(reg, 1, x, y);
            const bool hit = (s0 < 256 && areas[s0]) || (s1 < 256 && areas[s1]);
            if (hit)
            {
                const int32_t px = MAP_X + (x + GRID_OFF_TILES) * c;
                const int32_t py = MAP_Y + (y + GRID_OFF_TILES) * c;
                UiDraw_Rect(px, py, c, c, UI_RGBA(224, 58, 58, pulse));
                UiDraw_Frame(px, py, c, c, 2, C_WHITE);
            }
        }
    }

    int32_t hx = 0, hy = 0;
    const uint8_t pReg = UiGame_PlayerMapPos(&hx, &hy);
    if (pReg == reg)
    {
        UiDraw_Sprite(UiGame_PlayerIsMale() ? UI_SPR_HEAD_RED : UI_SPR_HEAD_LEAF,
                      MAP_X + (hx + GRID_OFF_TILES) * c + c / 2 - 16,
                      MAP_Y + (hy + GRID_OFF_TILES) * c + c / 2 - 16, 2);
    }
    UiDraw_PopClip();
}
