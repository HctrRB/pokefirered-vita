// Native 960x544 Pokédex for Pokémon FireRed PS Vita (Core 1).
// Modes: KANTO / NATIONAL / HABITATS (9 FireRed categories).
// Orders: NUMERICAL, A TO Z, HEAVIEST, LIGHTEST, TALLEST, SMALLEST.
// Filters: ALL, CAUGHT, SEEN.
// Detail: INFO (types, height/weight, habitat, footprint, entry), AREA (4-region Kanto/Sevii map),
// CRY (waveform analyzer), SIZE (silhouette comparison with Red / Leaf), plus Caught Page popup.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "event_data.h"
#include "pokedex.h"
#include "pokedex_screen.h"
#include "pokemon.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/pokedex.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "constants/trainers.h"

#define LIST_X 424
#define LIST_W 512
#define LIST_Y 106
#define ROW_H 40
#define ROWS 9
#define GBA_ROWS 8

static int32_t DexRows(void)
{
    return gUiTheme->gbaFrame ? GBA_ROWS : ROWS;
}

enum { MODE_KANTO = 0, MODE_NATIONAL = 1, MODE_HABITAT = 2, MODE_COUNT = 3 };
enum { ORDER_NUMBER = 0, ORDER_AZ, ORDER_HEAVIEST, ORDER_LIGHTEST, ORDER_TALLEST, ORDER_SMALLEST, ORDER_COUNT };
enum { FILTER_ALL = 0, FILTER_CAUGHT, FILTER_SEEN, FILTER_COUNT };

static const char *const sModeTabs[MODE_COUNT] = { UI_N("KANTO"), UI_N("NATIONAL"), UI_N("HABITATS") };
static const char *const sGbaModeTabs[MODE_COUNT] = { UI_N("KANTO DEX"), UI_N("NATIONAL DEX"), UI_N("HABITATS") };
static const char *const sOrderNames[ORDER_COUNT] = {
    UI_N("NUMERICAL"), UI_N("A TO Z"), UI_N("HEAVIEST"), UI_N("LIGHTEST"), UI_N("TALLEST"), UI_N("SMALLEST")
};
static const char *const sFilterNames[FILTER_COUNT] = {
    UI_N("ALL"), UI_N("CAUGHT"), UI_N("SEEN")
};
static const char *const sHabitatNames[UI_HABITAT_COUNT] = {
    UI_N("GRASSLAND"),
    UI_N("FOREST"),
    UI_N("WATER'S EDGE"),
    UI_N("SEA"),
    UI_N("CAVE"),
    UI_N("MOUNTAIN"),
    UI_N("ROUGH TERRAIN"),
    UI_N("URBAN"),
    UI_N("RARE"),
};

static int32_t sMode = MODE_KANTO;
static int32_t sOrder = ORDER_NUMBER;
static int32_t sFilter = FILTER_ALL;
static int32_t sHabitat = 0;
static UiList sList;
static uint16_t sNat[NATIONAL_DEX_COUNT];
static int32_t sCount = 0;
static int32_t sTabX[MODE_COUNT], sTabW[MODE_COUNT];
static UiMenu sMenu;
static uint32_t sFrame = 0;
static uint16_t sDetailNat = 1;
static bool sCaughtPage = false;
static int32_t sListCryTicks = 0;

static bool Seen(const uint16_t nat)
{
    return nat >= 1 && nat <= NATIONAL_DEX_COUNT && GetSetPokedexFlag(nat, FLAG_GET_SEEN) != 0;
}

static bool Caught(const uint16_t nat)
{
    return nat >= 1 && nat <= NATIONAL_DEX_COUNT && GetSetPokedexFlag(nat, FLAG_GET_CAUGHT) != 0;
}

static void Name(const uint16_t nat, char *const out, const size_t cap)
{
    u8 buf[POKEMON_NAME_LENGTH + 1];
    GetSpeciesName(buf, NationalPokedexNumToSpecies(nat));
    UiGame_Text(buf, out, cap);
}

typedef struct { uint16_t nat; char name[16]; } SortKey;
static SortKey sSort[NATIONAL_DEX_COUNT];

static int32_t Compare(const uint16_t a, const uint16_t b)
{
    switch (sOrder)
    {
    case ORDER_HEAVIEST: return (int32_t)gPokedexEntries[b].weight - (int32_t)gPokedexEntries[a].weight;
    case ORDER_LIGHTEST: return (int32_t)gPokedexEntries[a].weight - (int32_t)gPokedexEntries[b].weight;
    case ORDER_TALLEST:  return (int32_t)gPokedexEntries[b].height - (int32_t)gPokedexEntries[a].height;
    case ORDER_SMALLEST: return (int32_t)gPokedexEntries[a].height - (int32_t)gPokedexEntries[b].height;
    default: return 0;
    }
}

static int SortCmp(const void *const pa, const void *const pb)
{
    const SortKey *const a = (const SortKey *)pa;
    const SortKey *const b = (const SortKey *)pb;
    const int32_t c = (sOrder == ORDER_AZ) ? strcmp(a->name, b->name) : Compare(a->nat, b->nat);
    return c != 0 ? (int)c : ((int)a->nat - (int)b->nat);
}

static bool PassesFilter(const uint16_t nat)
{
    if (sFilter == FILTER_CAUGHT && !Caught(nat))
        return false;
    if (sFilter == FILTER_SEEN && !Seen(nat))
        return false;
    if (sOrder != ORDER_NUMBER && !Caught(nat))
        return false;
    return true;
}

static void BuildList(void)
{
    sCount = 0;
    if (sMode == MODE_HABITAT)
    {
        uint16_t spList[NATIONAL_DEX_COUNT];
        const int32_t n = UiGame_HabitatSpecies((uint8_t)sHabitat, spList, NATIONAL_DEX_COUNT);
        for (int32_t i = 0; i < n && sCount < NATIONAL_DEX_COUNT; i++)
        {
            const uint16_t nat = SpeciesToNationalPokedexNum(spList[i]);
            if (nat == 0 || nat > NATIONAL_DEX_COUNT)
                continue;
            if (!PassesFilter(nat))
                continue;
            sNat[sCount++] = nat;
        }
    }
    else
    {
        const int32_t maxDex = (sMode == MODE_KANTO || !IsNationalPokedexViewEnabled())
                             ? KANTO_DEX_COUNT : NATIONAL_DEX_COUNT;
        for (int32_t i = 1; i <= maxDex; i++)
        {
            const uint16_t nat = (uint16_t)i;
            if (!PassesFilter(nat))
                continue;
            sNat[sCount++] = nat;
        }
    }

    if (sOrder != ORDER_NUMBER && sCount > 1)
    {
        for (int32_t i = 0; i < sCount; i++)
        {
            sSort[i].nat = sNat[i];
            if (sOrder == ORDER_AZ)
                Name(sNat[i], sSort[i].name, sizeof(sSort[i].name));
        }
        qsort(sSort, (size_t)sCount, sizeof(sSort[0]), SortCmp);
        for (int32_t i = 0; i < sCount; i++)
            sNat[i] = sSort[i].nat;
    }
    UiList_Reset(&sList, sCount, DexRows());
}

static int32_t ListNumber(const int32_t i)
{
    return (int32_t)sNat[i];
}

static void HeightWeight(const uint16_t nat, char *const h, const size_t hc, char *const w, const size_t wc)
{
#ifdef UI_METRIC_UNITS
    const uint16_t dm = gPokedexEntries[nat].height, hg = gPokedexEntries[nat].weight;
    snprintf(h, hc, UI_TR("%u.%u m"), (unsigned)(dm / 10), (unsigned)(dm % 10));
    snprintf(w, wc, UI_TR("%u.%u kg"), (unsigned)(hg / 10), (unsigned)(hg % 10));
#else
    const uint32_t inches = (uint32_t)gPokedexEntries[nat].height * 10000u / 254u;
    const uint32_t in = (inches + 5u) / 10u;
    snprintf(h, hc, "%u'%02u\"", (unsigned)(in / 12u), (unsigned)(in % 12u));
    const uint32_t lbs10 = (uint32_t)gPokedexEntries[nat].weight * 100000u / 4536u;
    snprintf(w, wc, UI_TR("%u.%u lbs"), (unsigned)((lbs10 + 5u) / 100u), (unsigned)(((lbs10 + 5u) / 10u) % 10u));
#endif
}

static void Types(const uint16_t species, const int32_t x, const int32_t y)
{
    const uint8_t t0 = gSpeciesInfo[species].types[0], t1 = gSpeciesInfo[species].types[1];
    UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + t0), x, y, 2);
    if (t1 != t0)
        UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + t1), x + 70, y, 2);
}

static void Kv(const int32_t x, const int32_t y, const int32_t w, const char *const k)
{
    UiW_Inset(x, y, w, 56);
    UiText_Draw(UI_FACE_PS2P, 16, x + 10, y + 6, k, UI_COL_INK_SOFT);
}

static void Well(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    UiW_Inset(x, y, w, h);
}

static void HabitatText(const uint16_t species, const bool seen, char *const out, const size_t cap)
{
    if (!seen)
    {
        snprintf(out, cap, "?????");
        return;
    }
    const uint8_t hab = UiGame_SpeciesHabitat(species);
    if (hab < UI_HABITAT_COUNT)
        snprintf(out, cap, "%s", UiLocale_Lookup(sHabitatNames[hab]));
    else
        snprintf(out, cap, "-");
}

static void BuildCard(void)
{
    UiW_Panel(16, 60, 400, 432);
    if (sCount == 0)
        return;
    const uint16_t nat = sNat[sList.sel], species = NationalPokedexNumToSpecies(nat);
    const bool seen = Seen(nat), own = Caught(nat);
    char buf[240], name[24];
    snprintf(buf, sizeof(buf), UI_TR("No.%03d"), (int)ListNumber(sList.sel));
    Name(nat, name, sizeof(name));
    UiW_PanelHead(16, 60, 400, buf, seen ? name : "-----");
    Well(32, 108, 156, 156);
    if (seen)
    {
        if (own) UiSpr_MonFront(species, false, 46, 122, 2);
        else     UiSpr_MonFrontTint(species, 46, 122, 2, UI_RGBA(0, 0, 0, 255));
    }
    Kv(200, 108, 200, UI_TR("SPECIES"));
    if (seen) UiGame_Text(gPokedexEntries[nat].categoryName, buf, sizeof(buf));
    else      snprintf(buf, sizeof(buf), "?????");
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 210, 130, buf, UI_COL_INK);

    Kv(200, 166, 200, UI_TR("TYPE"));
    if (seen) Types(species, 210, 188);
    else      UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 210, 188, "???", UI_COL_INK);

    Kv(200, 224, 200, UI_TR("HABITAT"));
    HabitatText(species, seen, buf, sizeof(buf));
    if (own) UiDraw_Sprite(UI_SPR_BALL_POKE, 210, 254, 1);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, own ? 232 : 210, 246, buf, UI_COL_INK);

    char h[16], w[16];
    if (own) HeightWeight(nat, h, sizeof(h), w, sizeof(w));
    else { snprintf(h, sizeof(h), "???"); snprintf(w, sizeof(w), "???"); }
    Kv(32, 290, 140, UI_TR("HEIGHT"));
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 42, 312, h, UI_COL_INK);
    Kv(176, 290, 152, UI_TR("WEIGHT"));
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 186, 312, w, UI_COL_INK);
    Well(336, 290, 64, 56);
    if (own) UiSpr_Footprint(species, 344, 294, 3, UI_COL_INK);
    Well(32, 360, 368, 120);
    if (own) UiGame_Text(gPokedexEntries[nat].description, buf, sizeof(buf));
    else     snprintf(buf, sizeof(buf), seen ? UI_TR("Catch it to see its POKéDEX entry.") : UI_TR("Not seen yet."));
    UiW_TextWrapped(44, 364, 344, 30, buf, UI_COL_INK, 3);
}

static void Enter(void)
{
    sMenu.open = false;
    sMode = MODE_KANTO;
    sOrder = ORDER_NUMBER;
    sFilter = FILTER_ALL;
    sHabitat = 0;
    BuildList();
}

static void Resume(void)
{
    BuildList();
}

static void DrawDexCardBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const uint32_t bg)
{
    UiW_Bevel(x, y, w, h, 5, UI_RGBA(40, 56, 64, 255), bg, bg, 3, UI_RGBA(96, 96, 112, 255), UI_RGBA(96, 96, 112, 255), 3);
}

static void DrawDexDescBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const char *const desc, const uint32_t frame)
{
    UiW_GbaBox(x, y, w, h, 5, UI_RGBA(40, 56, 64, 255), UI_RGBA(248, 248, 248, 255), UI_RGBA(248, 248, 248, 255), UI_RGBA(216, 216, 216, 255), 3);
    UiDraw_Rect(x + 6, y + h - 5, w - 12, 3, UI_RGBA(184, 184, 184, 255));
    // Left & right crimson/amber FireRed Pokédex rails
    UiDraw_Rect(x + 3, y + 6, 5, h - 12, UI_RGBA(213, 65, 41, 255));
    UiDraw_Rect(x + 8, y + 6, 5, h - 12, UI_RGBA(248, 160, 48, 255));
    UiDraw_Rect(x + 13, y + 6, 4, h - 12, UI_RGBA(255, 224, 96, 255));
    UiDraw_Rect(x + 17, y + 6, 2, h - 12, UI_RGBA(40, 56, 64, 255));

    UiDraw_Rect(x + w - 19, y + 6, 2, h - 12, UI_RGBA(40, 56, 64, 255));
    UiDraw_Rect(x + w - 17, y + 6, 4, h - 12, UI_RGBA(255, 224, 96, 255));
    UiDraw_Rect(x + w - 13, y + 6, 5, h - 12, UI_RGBA(248, 160, 48, 255));
    UiDraw_Rect(x + w - 8, y + 6, 5, h - 12, UI_RGBA(213, 65, 41, 255));

    UiW_GbaTextWrapped(UI_FACE_VT323, 28, x + 28, y + 14, w - 58, 28, desc, UI_RGBA(40, 56, 64, 255), UI_RGBA(216, 216, 216, 255), 5);
    UiW_GbaArrowV(x + w - 34, y + h - 18 + (int32_t)((frame / 16) & 1) * 2, true, UI_RGBA(230, 8, 8, 255));
}

static bool Update(const UiInput *const in)
{
    sFrame++;
    if (sListCryTicks > 0)
        sListCryTicks--;
    const bool gba = gUiTheme->gbaFrame;
    if (sMenu.open)
    {
        const int32_t id = UiMenu_Update(&sMenu, in);
        if (id >= 0)
        {
            sOrder = id;
            BuildList();
            UiKit_Toast(UI_TR("Order: %s"), UiLocale_Lookup(sOrderNames[sOrder]));
        }
        return true;
    }
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        return false;
    }

    int32_t t = -1;
    if (gba)
    {
        const int32_t ph = UiW_GbaPagerHit(in, true, MODE_COUNT);
        if (ph != 0)
            t = (sMode + MODE_COUNT + ph) % MODE_COUNT;
    }
    else
    {
        t = UiKit_TabHit(in, sTabX, sTabW, MODE_COUNT);
    }
    if (in->pressed & VITA_BTN_L) t = (sMode + MODE_COUNT - 1) % MODE_COUNT;
    if (in->pressed & VITA_BTN_R) t = (sMode + 1) % MODE_COUNT;
    if (t >= 0 && t != sMode)
    {
        if (t == MODE_NATIONAL && !IsNationalPokedexViewEnabled())
        {
            PlaySE(SE_FAILURE);
            UiKit_Toast(UI_TR("NATIONAL DEX is not unlocked yet."));
            return true;
        }
        sMode = t;
        PlaySE(SE_SELECT);
        BuildList();
        return true;
    }

    if (in->pressed & VITA_BTN_SQUARE)
    {
        sFilter = (sFilter + 1) % FILTER_COUNT;
        PlaySE(SE_SELECT);
        BuildList();
        UiKit_Toast(UI_TR("Filter: %s"), UiLocale_Lookup(sFilterNames[sFilter]));
        return true;
    }

    if ((in->pressed & VITA_BTN_SELECT) || (gba && UiHit(in, 26, 222, 478, 34)))
    {
        PlaySE(SE_SELECT);
        UiMenu_Open(&sMenu, UI_TR("ORDER"), 600, 120, 300);
        for (int32_t i = 0; i < ORDER_COUNT; i++)
            UiMenu_Add(&sMenu, UiLocale_Lookup(sOrderNames[i]), i);
        return true;
    }

    const bool playCryHit = gba && UiHit(in, 94, 292, 112, 42);
    if (((in->pressed & VITA_BTN_TRIANGLE) || playCryHit) && sCount > 0 && Seen(sNat[sList.sel]))
    {
        PlayCry_Normal(NationalPokedexNumToSpecies(sNat[sList.sel]), 0);
        sListCryTicks = 48;
    }

    // In HABITATS mode, LEFT/RIGHT or tapping the list header cycles through the 9 FireRed habitats.
    const bool habTap = (sMode == MODE_HABITAT)
                      && (gba ? UiHit(in, 544, 70, 390, 36) : UiHit(in, 416, 60, 528, 42));
    if (sMode == MODE_HABITAT && ((in->repeat & (VITA_BTN_LEFT | VITA_BTN_RIGHT)) || habTap))
    {
        const int32_t dir = (in->repeat & VITA_BTN_LEFT) ? -1 : 1;
        sHabitat = (sHabitat + dir + UI_HABITAT_COUNT) % UI_HABITAT_COUNT;
        PlaySE(SE_SELECT);
        BuildList();
        return true;
    }
    if (sCount > 0 && (in->repeat & (VITA_BTN_LEFT | VITA_BTN_RIGHT)))
    {
        const int32_t vis = DexRows();
        sList.sel += (in->repeat & VITA_BTN_RIGHT) ? vis : -vis;
        if (sList.sel < 0) sList.sel = 0;
        if (sList.sel >= sCount) sList.sel = sCount - 1;
        sList.first = sList.sel - sList.sel % vis;
        UiList_Reset(&sList, sCount, vis);
        PlaySE(SE_SELECT);
    }

    const int32_t lx = gba ? 544 : LIST_X;
    const int32_t ly = gba ? 114 : LIST_Y;
    const int32_t lw = gba ? 374 : LIST_W;
    const int32_t rh = gba ? 45  : ROW_H;
    const int32_t a = UiList_Update(&sList, in, lx, ly, lw, rh, false);
    if (a >= 0)
    {
        if (Caught(sNat[a]))
        {
            PlaySE(SE_SELECT);
            sDetailNat = sNat[a];
            sCaughtPage = false;
            UiNav_Push(&gUiDexDetailScreen);
        }
        else
        {
            PlaySE(SE_FAILURE);
            UiKit_Toast(UI_TR("Catch it to see its entry."));
        }
    }
    return true;
}

static void BuildGba(void)
{
    char buf[240], name[24], seenBuf[48];
    const uint16_t seenTotal = (sMode == MODE_KANTO) ? GetKantoPokedexCount(FLAG_GET_SEEN) : GetNationalPokedexCount(FLAG_GET_SEEN);
    const uint16_t ownTotal  = (sMode == MODE_KANTO) ? GetKantoPokedexCount(FLAG_GET_CAUGHT) : GetNationalPokedexCount(FLAG_GET_CAUGHT);
    snprintf(seenBuf, sizeof(seenBuf), UI_TR("SEEN %03u · OWN %03u"), (unsigned)seenTotal, (unsigned)ownTotal);

    UiW_GbaTopBarPages(UI_GBA_BG_POKEDEX, UI_SPR_START_DEX, UI_TR("POKéDEX"), sGbaModeTabs, MODE_COUNT, sMode);

    int32_t rx = UI_SCREEN_W - 14;
    UiW_GbaHelpBadge(&rx, "O", UI_TR("BACK"), true);

    DrawDexCardBox(14, 60, 504, 210, UI_RGBA(248, 248, 248, 255));
    UiW_GbaBox(26, 70, 140, 140, 4, UI_RGBA(96, 96, 112, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(184, 184, 184, 255), 0);
    for (int32_t g = 16; g < 134; g += 16)
    {
        UiDraw_Rect(29 + g, 73, 1, 134, UI_RGBA(40, 56, 64, 36));
        UiDraw_Rect(29, 73 + g, 134, 1, UI_RGBA(40, 56, 64, 36));
    }

    const bool has = (sCount > 0);
    const uint16_t nat = has ? sNat[sList.sel] : 1;
    const uint16_t species = NationalPokedexNumToSpecies(nat);
    const bool seen = has && Seen(nat), own = has && Caught(nat);
    if (has && seen)
    {
        if (own) UiSpr_MonFront(species, false, 32, 76, 2);
        else     UiSpr_MonFrontTint(species, 32, 76, 2, UI_RGBA(0, 0, 0, 255));
    }

    UiW_GbaBox(178, 70, 326, 62, 4, UI_RGBA(160, 160, 160, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), C_WHITE, 0);
    if (has)
    {
        if (own) UiDraw_Sprite(UI_SPR_BALL_POKE, 188, 79, 1);
        Name(nat, name, sizeof(name));
        snprintf(buf, sizeof(buf), UI_TR("No.%03d  %s"), (int)ListNumber(sList.sel), seen ? name : "-----");
        UiW_GbaText(UI_FACE_VT323, 28, own ? 210 : 188, 74, buf, UI_RGBA(40, 56, 64, 255), C_WHITE);
        if (seen)
        {
            const uint8_t t0 = gSpeciesInfo[species].types[0], t1 = gSpeciesInfo[species].types[1];
            if (t1 != t0)
            {
                UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + t0), 430, 80, 1);
                UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + t1), 466, 80, 1);
            }
            else
            {
                UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + t0), 466, 80, 1);
            }
            char cat[32];
            UiGame_Text(gPokedexEntries[nat].categoryName, cat, sizeof(cat));
            snprintf(buf, sizeof(buf), UI_TR("%s POKéMON"), cat);
        }
        else
        {
            snprintf(buf, sizeof(buf), UI_TR("????? POKéMON"));
        }
        UiW_GbaText(UI_FACE_VT323, 24, 188, 102, buf, UI_RGBA(72, 80, 88, 255), C_WHITE);
    }

    char h[16], w[16];
    if (own) HeightWeight(nat, h, sizeof(h), w, sizeof(w));
    else { snprintf(h, sizeof(h), "???"); snprintf(w, sizeof(w), "???"); }
    UiW_GbaText(UI_FACE_VT323, 28, 178, 142, UI_TR("HT"), UI_RGBA(40, 56, 64, 255), UI_RGBA(216, 216, 216, 255));
    UiW_GbaText(UI_FACE_VT323, 28, 430 - UiText_Width(UI_FACE_VT323, 28, h), 142, h, UI_RGBA(40, 56, 64, 255), UI_RGBA(216, 216, 216, 255));
    UiDraw_Rect(178, 171, 252, 3, UI_RGBA(184, 184, 184, 255));
    UiW_GbaText(UI_FACE_VT323, 28, 178, 178, UI_TR("WT"), UI_RGBA(40, 56, 64, 255), UI_RGBA(216, 216, 216, 255));
    UiW_GbaText(UI_FACE_VT323, 28, 430 - UiText_Width(UI_FACE_VT323, 28, w), 178, w, UI_RGBA(40, 56, 64, 255), UI_RGBA(216, 216, 216, 255));
    UiDraw_Rect(178, 207, 252, 3, UI_RGBA(184, 184, 184, 255));

    UiW_GbaBox(444, 146, 60, 60, 30, UI_RGBA(160, 160, 160, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(184, 184, 184, 255), 0);
    if (own)
        UiSpr_Footprint(species, 458, 160, 2, UI_RGBA(40, 56, 64, 255));

    char habBuf[32];
    HabitatText(species, seen, habBuf, sizeof(habBuf));
    UiW_GbaBox(26, 222, 478, 34, 4, UI_RGBA(96, 96, 112, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), C_WHITE, 0);
    snprintf(buf, sizeof(buf), UI_TR("HABITAT: %s · %s · %s"),
             habBuf, UiLocale_Lookup(sOrderNames[sOrder]), UiLocale_Lookup(sFilterNames[sFilter]));
    UiW_GbaTextMid(UI_FACE_VT323, 24, 38, 222, 34, buf, UI_RGBA(40, 56, 64, 255), C_WHITE);

    DrawDexCardBox(14, 278, 504, 70, UI_RGBA(216, 216, 216, 255));
    UiW_GbaText(UI_FACE_VT323, 24, 28, 290, UI_TR("CRY"), UI_RGBA(40, 56, 64, 255), C_WHITE);
    UiW_GbaText(UI_FACE_VT323, 24, 28, 310, UI_TR("METER"), UI_RGBA(40, 56, 64, 255), C_WHITE);

    UiW_GbaBox(94, 292, 112, 42, 4, UI_RGBA(16, 21, 24, 255), UI_RGBA(40, 56, 64, 255), UI_RGBA(40, 56, 64, 255), UI_RGBA(80, 100, 112, 255), 0);
    UiW_GbaKeyCap(102, 301, 24, "TRI", false);
    UiW_GbaArrowH(142, 307, true, UI_RGBA(106, 255, 255, 255));
    UiW_GbaTextMid(UI_FACE_VT323, 24, 158, 294, 38, UI_TR("PLAY"), UI_RGBA(248, 248, 248, 255), UI_GBA_SHAD_DARK);

    UiW_GbaBox(216, 290, 288, 46, 4, UI_RGBA(96, 96, 112, 255), UI_RGBA(0, 0, 40, 255), UI_RGBA(0, 0, 40, 255), 0, 0);
    static const uint8_t kWave[16] = { 20, 42, 74, 92, 82, 58, 88, 96, 66, 44, 72, 90, 56, 36, 60, 26 };
    for (int32_t b = 0; b < 16; b++)
    {
        int32_t pct = (has && seen) ? kWave[b] : 10;
        if (sListCryTicks > 0)
            pct = (int32_t)(((b * 37 + sFrame * 13 + nat) % 85) + 15);
        else if (!own)
            pct = 12;
        const int32_t bh = 32 * pct / 100;
        UiDraw_RectV(226 + b * 17, 329 - bh, 12, bh, UI_RGBA(248, 232, 0, 255), UI_RGBA(48, 208, 72, 255));
    }

    if (own) UiGame_Text(gPokedexEntries[nat].description, buf, sizeof(buf));
    else     snprintf(buf, sizeof(buf), seen ? UI_TR("Catch it to record full POKéDEX data and area habitat.") : UI_TR("No data recorded yet."));
    DrawDexDescBox(14, 356, 504, 176, buf, sFrame);

    DrawDexCardBox(532, 60, 414, 424, UI_RGBA(248, 248, 248, 255));
    UiW_GbaBox(544, 70, 390, 36, 4, UI_RGBA(16, 21, 24, 255), UI_RGBA(40, 56, 64, 255), UI_RGBA(40, 56, 64, 255), 0, 0);
    if (sMode == MODE_HABITAT)
    {
        snprintf(buf, sizeof(buf), "< %s (%d/9) >", UiLocale_Lookup(sHabitatNames[sHabitat]), (int)(sHabitat + 1));
        UiW_GbaTextMid(UI_FACE_VT323, 26, 554, 70, 36, buf, UI_RGBA(248, 232, 0, 255), UI_GBA_SHAD_DARK);
    }
    else
    {
        UiW_GbaTextMid(UI_FACE_VT323, 28, 554, 70, 36,
                       sMode == MODE_KANTO ? UI_TR("KANTO POKéDEX") : UI_TR("NATIONAL DEX"),
                       UI_RGBA(248, 232, 0, 255), UI_GBA_SHAD_DARK);
    }
    UiW_GbaTextMid(UI_FACE_VT323, 24, 926 - UiText_Width(UI_FACE_VT323, 24, seenBuf), 70, 36, seenBuf, UI_RGBA(48, 208, 72, 255), UI_GBA_SHAD_DARK);

    UiDraw_PushClip(544, 114, 374, GBA_ROWS * 45);
    for (int32_t r = 0; r < GBA_ROWS && sList.first + r < sCount; r++)
    {
        const int32_t i = sList.first + r;
        const int32_t ry = 114 + r * 45;
        const uint16_t rowNat = sNat[i];
        const bool rOn = (i == sList.sel), rSeen = Seen(rowNat), rOwn = Caught(rowNat);
        if (rOn)
        {
            UiW_GbaBox(544, ry + 1, 374, 42, 4, UI_RGBA(40, 56, 64, 255),
                       UI_RGBA(255, 238, 0, 255), UI_RGBA(248, 184, 0, 255), C_WHITE, 0);
            UiW_GbaArrowH(553, ry + 17, true, C_WHITE);
            UiW_GbaArrowH(552, ry + 16, true, UI_RGBA(230, 8, 8, 255));
        }
        else if (r < GBA_ROWS - 1)
        {
            UiDraw_Rect(544, ry + 43, 374, 2, UI_RGBA(224, 224, 216, 255));
        }

        snprintf(buf, sizeof(buf), UI_TR("No.%03d"), (int)ListNumber(i));
        UiW_GbaTextMid(UI_FACE_VT323, 24, 570, ry, 44, buf,
                       rOn ? UI_RGBA(40, 56, 64, 255) : UI_RGBA(72, 80, 88, 255),
                       rOn ? C_WHITE : UI_RGBA(216, 216, 216, 255));
        if (rOwn)
            UiDraw_Sprite(UI_SPR_BALL_POKE, 640, ry + 14, 1);
        if (rSeen)
            UiSpr_MonIcon(NationalPokedexNumToSpecies(rowNat), 0, 662, ry + 6, 1);
        if (rSeen)
            Name(rowNat, name, sizeof(name));
        else
            snprintf(name, sizeof(name), "-----");
        UiW_GbaTextMid(UI_FACE_VT323, 28, 704, ry, 44, name,
                       rOwn ? UI_RGBA(49, 49, 49, 255) : UI_RGBA(112, 120, 128, 255),
                       rOn ? C_WHITE : UI_RGBA(216, 216, 216, 255));
    }
    UiDraw_PopClip();
    UiW_ScrollBar(924, 114, GBA_ROWS * 45, sList.first, GBA_ROWS, sCount);

    // Compact action legend bar below the Pokémon list panel (keeps the GBA top bar uncluttered).
    DrawDexCardBox(532, 490, 414, 42, UI_RGBA(40, 56, 64, 255));
    int32_t lx = 936;
    UiW_GbaHelpBadgeY(&lx, 498, "SELECT", UI_TR("ORDER"), true);
    UiW_GbaHelpBadgeY(&lx, 498, "SQ", UI_TR("FILTER"), false);
    UiW_GbaHelpBadgeY(&lx, 498, "TRI", UI_TR("CRY"), false);
    UiW_GbaHelpBadgeY(&lx, 498, "X", UI_TR("DATA"), false);

    if (sMenu.open)
        UiMenu_Draw(&sMenu);
}

static void Build(void)
{
    if (gUiTheme->gbaFrame)
    {
        BuildGba();
        return;
    }

    char buf[64], headRight[64];
    UiKit_Header(UI_TR("POKéDEX"), sModeTabs, MODE_COUNT, sMode, sTabX, sTabW, 12);
    const uint16_t seenTotal = (sMode == MODE_KANTO) ? GetKantoPokedexCount(FLAG_GET_SEEN) : GetNationalPokedexCount(FLAG_GET_SEEN);
    const uint16_t ownTotal  = (sMode == MODE_KANTO) ? GetKantoPokedexCount(FLAG_GET_CAUGHT) : GetNationalPokedexCount(FLAG_GET_CAUGHT);
    snprintf(buf, sizeof(buf), UI_TR("SEEN %u · OWN %u"), (unsigned)seenTotal, (unsigned)ownTotal);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    BuildCard();
    UiW_Panel(416, 60, 528, 432);
    if (sMode == MODE_HABITAT)
        snprintf(buf, sizeof(buf), "< %s (%d/9) >", UiLocale_Lookup(sHabitatNames[sHabitat]), (int)(sHabitat + 1));
    else
        snprintf(buf, sizeof(buf), "%s", sMode == MODE_KANTO ? UI_TR("KANTO LIST") : UI_TR("NATIONAL LIST"));
    snprintf(headRight, sizeof(headRight), "%s · %s",
             UiLocale_Lookup(sOrderNames[sOrder]), UiLocale_Lookup(sFilterNames[sFilter]));
    UiW_PanelHead(416, 60, 528, buf, headRight);

    UiDraw_PushClip(LIST_X, LIST_Y, LIST_W, ROWS * ROW_H);
    for (int32_t r = 0; r < ROWS && sList.first + r < sCount; r++)
    {
        const int32_t i = sList.first + r, y = LIST_Y + r * ROW_H;
        const uint16_t nat = sNat[i];
        const bool on = (i == sList.sel), seen = Seen(nat), own = Caught(nat);
        UiW_Row(LIST_X, y, LIST_W - 12, ROW_H, on, i);
        const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
        snprintf(buf, sizeof(buf), "%03d", (int)ListNumber(i));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 28, y + 4, buf, on ? ink : UI_COL_INK_SOFT);
        if (own)  UiDraw_Sprite(UI_SPR_BALL_POKE, LIST_X + 84, y + 12, 1);
        if (seen) UiSpr_MonIcon(NationalPokedexNumToSpecies(nat), 0, LIST_X + 106, y + 2, 1);
        if (seen) Name(nat, buf, sizeof(buf));
        else      snprintf(buf, sizeof(buf), "-----");
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 146, y + 4, buf, own ? ink : UI_COL_INK_SOFT);
    }
    UiDraw_PopClip();
    UiW_ScrollBar(LIST_X + LIST_W - 9, LIST_Y, ROWS * ROW_H, sList.first, ROWS, sCount);

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Details"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Cry"));
    UiW_FooterGlyph(&fx, UI_GLYPH_SQUARE, UI_TR("Filter"));
    UiW_FooterKey(&fx, "SELECT", UI_TR("Order"));
    UiKit_FooterClock();
    if (sMenu.open)
        UiMenu_Draw(&sMenu);
}

const UiScreen gUiDexScreen = { Enter, Update, Build, Resume };

// =====================================================================================
// Detail Screen: INFO / AREA / CRY / SIZE & Caught Pokédex Popup
// =====================================================================================
static const char *const sDetTabs[4] = { UI_N("INFO"), UI_N("AREA"), UI_N("CRY"), UI_N("SIZE") };
static const char *const sAreaRegionNames[4] = {
    UI_N("KANTO"), UI_N("SEVII 1-3"), UI_N("SEVII 4-5"), UI_N("SEVII 6-7")
};
static int32_t sDetTab = 0, sDetTabX[4], sDetTabW[4], sCryTicks = 0;
static uint8_t sAreas[256];
static int32_t sAreaCount = 0;
static uint16_t sAreaFor = 0;
static uint8_t sAreaRegion = 0;
static bool sRegionHasArea[4];

static void RefreshAreaData(const uint16_t species)
{
    sAreaCount = UiGame_SpeciesAreas(species, sAreas, 256);
    sAreaFor = species;
    const int32_t regCount = UiGame_MapRegionCount();
    int32_t firstMatch = -1;
    for (int32_t r = 0; r < 4; r++)
    {
        sRegionHasArea[r] = false;
        if (r >= regCount)
            continue;
        for (int32_t y = 0; y < 15 && !sRegionHasArea[r]; y++)
        {
            for (int32_t x = 0; x < 22; x++)
            {
                const uint16_t s0 = UiGame_MapSecAt(r, 0, x, y);
                const uint16_t s1 = UiGame_MapSecAt(r, 1, x, y);
                if ((s0 < 256 && sAreas[s0]) || (s1 < 256 && sAreas[s1]))
                {
                    sRegionHasArea[r] = true;
                    if (firstMatch < 0)
                        firstMatch = r;
                    break;
                }
            }
        }
    }
    if (firstMatch >= 0)
    {
        sAreaRegion = (uint8_t)firstMatch;
    }
    else
    {
        int32_t px = 0, py = 0;
        const uint8_t pReg = UiGame_PlayerMapPos(&px, &py);
        sAreaRegion = (pReg < (uint8_t)regCount) ? pReg : 0;
    }
}

static void DetEnter(void)
{
    sDetTab = 0;
    sAreaFor = 0;
}

static void StepCaught(const int32_t d)
{
    if (sCount <= 0)
        return;
    for (int32_t k = 1; k < sCount; k++)
    {
        const int32_t i = (sList.sel + d * k + sCount * 4) % sCount;
        if (Caught(sNat[i]))
        {
            sList.sel = i;
            sDetailNat = sNat[i];
            PlaySE(SE_SELECT);
            return;
        }
    }
}

static void Cry(void)
{
    PlayCry_Normal(NationalPokedexNumToSpecies(sDetailNat), 0);
    sCryTicks = 60;
}

static bool DetUpdate(const UiInput *const in)
{
    if (sCryTicks > 0)
        sCryTicks--;

    if (sCaughtPage && (in->pressed & (VITA_BTN_CIRCLE | (sDetTab != 2 ? VITA_BTN_CROSS : 0))))
    {
        PlaySE(SE_SELECT);
        sCaughtPage = false;
        UiNav_Close(NULL);
        return true;
    }
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        return false;
    }

    int32_t t = -1;
    if (gUiTheme->gbaFrame)
    {
        const int32_t ph = UiW_GbaPagerHit(in, true, 4);
        if (ph != 0)
            t = (sDetTab + 4 + ph) % 4;
    }
    else
    {
        t = UiKit_TabHit(in, sDetTabX, sDetTabW, 4);
    }
    if (in->pressed & VITA_BTN_L) t = (sDetTab + 3) % 4;
    if (in->pressed & VITA_BTN_R) t = (sDetTab + 1) % 4;
    if (t >= 0 && t != sDetTab)
    {
        sDetTab = t;
        PlaySE(SE_SELECT);
        if (t == 2)
            Cry();
    }

    if (sDetTab == 1)
    {
        const int32_t regCount = UiGame_MapRegionCount();
        if (regCount > 1)
        {
            if ((in->repeat & (VITA_BTN_LEFT | VITA_BTN_RIGHT))
                || (in->pressed & VITA_BTN_SELECT)
                || UiHit(in, 652, 60, 292, 432))
            {
                const int32_t dir = (in->repeat & VITA_BTN_LEFT) ? -1 : 1;
                sAreaRegion = (uint8_t)((sAreaRegion + dir + regCount) % regCount);
                PlaySE(SE_SELECT);
            }
        }
    }

    if ((in->pressed & VITA_BTN_TRIANGLE) || (sDetTab == 2 && (in->pressed & VITA_BTN_CROSS)))
        Cry();
    if (!sCaughtPage && (in->repeat & VITA_BTN_UP))   StepCaught(-1);
    if (!sCaughtPage && (in->repeat & VITA_BTN_DOWN)) StepCaught(1);
    return true;
}

static void DetBuildGba(void)
{
    const uint16_t nat = sDetailNat, species = NationalPokedexNumToSpecies(nat);
    char buf[240], name[24];
    Name(nat, name, sizeof(name));
    UiW_GbaTopBarPages(UI_GBA_BG_POKEDEX, UI_SPR_START_DEX, UI_TR("POKéDEX"), sDetTabs, 4, sDetTab);

    int32_t rx = UI_SCREEN_W - 14;
    UiW_GbaHelpBadge(&rx, "O", sCaughtPage ? UI_TR("CLOSE") : UI_TR("BACK"), true);
    UiW_GbaHelpBadge(&rx, "TRI", UI_TR("CRY"), false);
    if (sDetTab == 1 && UiGame_MapRegionCount() > 1)
        UiW_GbaHelpBadge(&rx, "<>", UI_TR("MAP"), false);
    if (!sCaughtPage)
        UiW_GbaHelpBadge(&rx, "UD", UI_TR("STEP"), false);

    if (sDetTab == 1)
    {
        if (sAreaFor != species)
            RefreshAreaData(species);
        static uint32_t sAreaFrame;
        UiMap_DrawArea(sAreaRegion, sAreas, ++sAreaFrame);

        DrawDexCardBox(652, 60, 294, 472, UI_RGBA(248, 248, 248, 255));
        UiW_GbaSectionStrip(664, 72, 270, 28, UI_GBA_TAG_GREEN, name, UiLocale_Lookup(sAreaRegionNames[sAreaRegion]));
        snprintf(buf, sizeof(buf), UI_TR("No.%03u · %s"), (unsigned)nat, UiLocale_Lookup(sAreaRegionNames[sAreaRegion]));
        UiW_GbaText(UI_FACE_PS2P, 16, 666, 110, buf, UI_RGBA(72, 80, 88, 255), C_WHITE);
        UiW_GbaTextWrapped(UI_FACE_VT323, 28, 666, 146, 266, 28,
                           sAreaCount
                           ? (sRegionHasArea[sAreaRegion]
                              ? UI_TR("Red zones show where this wild POKéMON appears.")
                              : UI_TR("Found in another region. Press LEFT/RIGHT to switch map."))
                           : UI_TR("AREA UNKNOWN"),
                           sAreaCount ? UI_RGBA(40, 56, 64, 255) : UI_RGBA(112, 120, 128, 255), C_WHITE, 5);

        const int32_t regCount = UiGame_MapRegionCount();
        for (int32_t r = 0; r < regCount && r < 4; r++)
        {
            const int32_t py = 310 + r * 44;
            const bool active = (r == (int32_t)sAreaRegion);
            UiW_GbaBox(666, py, 266, 38, 4,
                       active ? UI_RGBA(213, 65, 41, 255) : UI_RGBA(160, 160, 160, 255),
                       active ? UI_RGBA(255, 236, 214, 255) : UI_RGBA(232, 232, 226, 255),
                       active ? UI_RGBA(255, 236, 214, 255) : UI_RGBA(232, 232, 226, 255), C_WHITE, 0);
            UiW_GbaTextMid(UI_FACE_VT323, 24, 680, py, 38, UiLocale_Lookup(sAreaRegionNames[r]),
                           UI_RGBA(40, 56, 64, 255), C_WHITE);
            UiW_GbaTextMid(UI_FACE_VT323, 24, 868, py, 38, sRegionHasArea[r] ? UI_TR("HABITAT") : "-",
                           sRegionHasArea[r] ? UI_RGBA(213, 65, 41, 255) : UI_RGBA(130, 136, 142, 255), C_WHITE);
        }
        return;
    }

    DrawDexCardBox(14, 60, 932, 472, UI_RGBA(248, 248, 248, 255));
    UiW_GbaBox(32, 78, 232, 232, 4, UI_RGBA(96, 96, 112, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(184, 184, 184, 255), 0);
    for (int32_t g = 16; g < 226; g += 16)
    {
        UiDraw_Rect(35 + g, 81, 1, 226, UI_RGBA(40, 56, 64, 36));
        UiDraw_Rect(35, 81 + g, 226, 1, UI_RGBA(40, 56, 64, 36));
    }
    UiSpr_MonFront(species, false, 52, 98, 3);

    if (sDetTab == 0)
    {
        UiW_GbaBox(280, 78, 354, 64, 4, UI_RGBA(160, 160, 160, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), C_WHITE, 0);
        snprintf(buf, sizeof(buf), UI_TR("No.%03u  %s"), (unsigned)nat, name);
        UiW_GbaText(UI_FACE_PS2P, 16, 294, 88, buf, UI_RGBA(40, 56, 64, 255), C_WHITE);
        char cat[32], hab[32];
        UiGame_Text(gPokedexEntries[nat].categoryName, cat, sizeof(cat));
        snprintf(buf, sizeof(buf), UI_TR("%s POKéMON"), cat);
        UiW_GbaText(UI_FACE_VT323, 28, 294, 110, buf, UI_RGBA(72, 80, 88, 255), C_WHITE);

        HabitatText(species, true, hab, sizeof(hab));
        UiW_GbaSectionStrip(280, 150, 354, 26, UI_GBA_TAG_GREEN, UI_TR("TYPE & HABITAT"), hab);
        Types(species, 290, 184);

        char h[16], w[16];
        HeightWeight(nat, h, sizeof(h), w, sizeof(w));
        UiW_GbaSectionStrip(280, 226, 354, 26, UI_GBA_TAG_TEAL, UI_TR("SIZE & WEIGHT"), NULL);
        snprintf(buf, sizeof(buf), UI_TR("HT %s   ·   WT %s"), h, w);
        UiW_GbaText(UI_FACE_VT323, 32, 290, 260, buf, UI_RGBA(40, 56, 64, 255), C_WHITE);

        UiW_GbaBox(654, 78, 272, 232, 4, UI_RGBA(160, 160, 160, 255), UI_RGBA(236, 236, 230, 255), UI_RGBA(236, 236, 230, 255), C_WHITE, 0);
        UiW_GbaSectionStrip(664, 88, 252, 26, UI_GBA_TAG_SLATE, UI_TR("FOOTPRINT"), NULL);
        UiW_GbaBox(742, 136, 96, 96, 48, UI_RGBA(160, 160, 160, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(216, 216, 216, 255), UI_RGBA(184, 184, 184, 255), 0);
        UiSpr_Footprint(species, 766, 160, 3, UI_RGBA(40, 56, 64, 255));

        UiGame_Text(gPokedexEntries[nat].description, buf, sizeof(buf));
        DrawDexDescBox(32, 326, 896, 188, buf, sFrame);
    }
    else if (sDetTab == 2)
    {
        UiW_GbaSectionStrip(280, 78, 648, 28, UI_GBA_TAG_BLUE, UI_TR("POKéMON CRY ANALYZER"), name);
        UiW_GbaBox(280, 114, 648, 196, 4, UI_RGBA(96, 96, 112, 255), UI_RGBA(0, 0, 40, 255), UI_RGBA(0, 0, 40, 255), 0, 0);
        for (int32_t x = 0; x < 42; x++)
        {
            const int32_t phase = (int32_t)((x * 37 + nat * 13) % 100);
            const int32_t amp = sCryTicks > 0 ? 12 + phase * sCryTicks * 3 / 60 : 8;
            const int32_t bh = amp > 166 ? 166 : amp;
            UiDraw_RectV(300 + x * 15, 212 - bh / 2, 10, bh, UI_RGBA(248, 232, 0, 255), UI_RGBA(48, 208, 72, 255));
        }
        DrawDexDescBox(32, 326, 896, 188, UI_TR("Press X or TRIANGLE to play this POKéMON's cry again."), sFrame);
    }
    else
    {
        UiW_GbaSectionStrip(280, 78, 648, 28, UI_GBA_TAG_GOLD, UI_TR("HEIGHT COMPARISON"), NULL);
        UiW_GbaBox(280, 114, 648, 398, 4, UI_RGBA(160, 160, 160, 255), UI_RGBA(232, 236, 230, 255), UI_RGBA(232, 236, 230, 255), C_WHITE, 0);
        const int32_t ps = gPokedexEntries[nat].pokemonScale ? gPokedexEntries[nat].pokemonScale : 256;
        const int32_t ts = gPokedexEntries[nat].trainerScale ? gPokedexEntries[nat].trainerScale : 256;
        int32_t ms = 192 * 256 / ps, tsz = 192 * 256 / ts;
        if (ms > 340) ms = 340;
        if (tsz > 340) tsz = 340;
        UiSpr_TrainerSized(UiGame_PlayerIsMale() ? TRAINER_PIC_RED : TRAINER_PIC_LEAF, 420 - tsz / 2, 480 - tsz, tsz, tsz);
        UiSpr_MonFrontSized(species, 760 - ms / 2, 480 - ms, ms, ms);
        UiGame_PlayerName(name, sizeof(name));
        snprintf(buf, sizeof(buf), UI_TR("Scale comparison with %s"), name);
        UiW_GbaText(UI_FACE_VT323, 28, 300, 126, buf, UI_RGBA(72, 80, 88, 255), C_WHITE);
    }
}

static void DetBuild(void)
{
    if (gUiTheme->gbaFrame)
    {
        DetBuildGba();
        return;
    }
    const uint16_t nat = sDetailNat, species = NationalPokedexNumToSpecies(nat);
    char buf[240], name[24];
    UiKit_Header(UI_TR("POKéDEX"), sDetTabs, 4, sDetTab, sDetTabX, sDetTabW, 12);
    Name(nat, name, sizeof(name));
    snprintf(buf, sizeof(buf), UI_TR("No.%03u %s"), (unsigned)nat, name);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    if (sDetTab == 1)
    {
        if (sAreaFor != species)
            RefreshAreaData(species);
        static uint32_t sAreaFrame;
        UiMap_DrawArea(sAreaRegion, sAreas, ++sAreaFrame);

        UiW_Panel(652, 60, 292, 432);
        UiW_PanelHead(652, 60, 292, name, UiLocale_Lookup(sAreaRegionNames[sAreaRegion]));
        UiW_TextWrapped(668, 112, 260, 30,
                        sAreaCount
                        ? (sRegionHasArea[sAreaRegion]
                           ? UI_TR("Red areas: where it lives in the wild.")
                           : UI_TR("Lives in another region. Press LEFT/RIGHT to switch map."))
                        : UI_TR("AREA UNKNOWN"),
                        sAreaCount ? UI_COL_INK : UI_COL_INK_SOFT, 4);

        const int32_t regCount = UiGame_MapRegionCount();
        for (int32_t r = 0; r < regCount && r < 4; r++)
        {
            const int32_t ry = 256 + r * 48;
            UiW_Row(664, ry, 268, 44, r == (int32_t)sAreaRegion, r);
            const uint32_t ink = (r == (int32_t)sAreaRegion) ? UI_COL_SELECT_INK : UI_COL_INK;
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 680, ry + 6, UiLocale_Lookup(sAreaRegionNames[r]), ink);
            if (sRegionHasArea[r])
                UiW_Chip(840, ry + 10, UI_TR("WILD"), true);
        }

        int32_t fx = 16;
        UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, sCaughtPage ? UI_TR("Close") : UI_TR("Back"));
        UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Cry"));
        if (regCount > 1)
            UiW_FooterKey(&fx, "<>", UI_TR("Region"));
        UiKit_FooterClock();
        return;
    }

    UiW_Panel(16, 60, 928, 432);
    Well(32, 76, 232, 232);
    UiSpr_MonFront(species, false, 52, 96, 3);
    if (sDetTab == 0)
    {
        Kv(280, 76, 352, UI_TR("NAME"));
        UiText_Draw(UI_FACE_PS2P, 16, 290, 104, name, UI_COL_INK);
        Kv(280, 136, 352, UI_TR("SPECIES"));
        char cat[32];
        UiGame_Text(gPokedexEntries[nat].categoryName, cat, sizeof(cat));
        snprintf(buf, sizeof(buf), UI_TR("%s POKéMON"), cat);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 290, 158, buf, UI_COL_INK);

        Kv(280, 196, 352, UI_TR("TYPE"));
        Types(species, 290, 218);

        char h[16], w[16];
        HeightWeight(nat, h, sizeof(h), w, sizeof(w));
        Kv(280, 256, 352, UI_TR("HEIGHT · WEIGHT"));
        snprintf(buf, sizeof(buf), "%s · %s", h, w);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 290, 278, buf, UI_COL_INK);

        Kv(648, 76, 280, UI_TR("FOOTPRINT"));
        Well(840, 82, 80, 44);
        UiSpr_Footprint(species, 856, 86, 2, UI_COL_INK);

        Kv(648, 136, 280, UI_TR("HABITAT"));
        HabitatText(species, true, buf, sizeof(buf));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 658, 158, buf, UI_COL_INK);

        Well(32, 324, 896, 156);
        UiGame_Text(gPokedexEntries[nat].description, buf, sizeof(buf));
        UiW_TextWrapped(48, 332, 864, 30, buf, UI_COL_INK, 4);
    }
    else if (sDetTab == 2)
    {
        UiW_PanelDark(280, 76, 648, 232);
        for (int32_t x = 0; x < 48; x++)
        {
            const int32_t phase = (int32_t)((x * 37 + nat * 13) % 100);
            const int32_t amp = sCryTicks > 0 ? 10 + phase * sCryTicks * 3 / 60 : 6;
            const int32_t h = amp > 200 ? 200 : amp;
            UiDraw_RectV(300 + x * 13, 192 - h / 2, 9, h, UI_RGBA(110, 219, 151, 255), UI_RGBA(47, 158, 88, 255));
        }
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 300, 330, UI_TR("X or triangle: play the cry again."), UI_COL_INK_SOFT);
    }
    else
    {
        Well(280, 76, 648, 404);
        const int32_t ps = gPokedexEntries[nat].pokemonScale ? gPokedexEntries[nat].pokemonScale : 256;
        const int32_t ts = gPokedexEntries[nat].trainerScale ? gPokedexEntries[nat].trainerScale : 256;
        int32_t ms = 192 * 256 / ps, tsz = 192 * 256 / ts;
        if (ms > 360) ms = 360;
        if (tsz > 360) tsz = 360;
        UiSpr_TrainerSized(UiGame_PlayerIsMale() ? TRAINER_PIC_RED : TRAINER_PIC_LEAF, 400 - tsz / 2, 460 - tsz, tsz, tsz);
        UiSpr_MonFrontSized(species, 760 - ms / 2, 460 - ms, ms, ms);
        UiGame_PlayerName(name, sizeof(name));
        snprintf(buf, sizeof(buf), UI_TR("Size compared with %s"), name);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 296, 84, buf, UI_COL_INK_SOFT);
    }

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, sCaughtPage ? UI_TR("Close") : UI_TR("Back"));
    UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Cry"));
    if (!sCaughtPage)
        UiW_FooterKey(&fx, UI_TR("UP/DOWN"), UI_TR("Previous / next"));
    UiKit_FooterClock();
}

const UiScreen gUiDexDetailScreen = { DetEnter, DetUpdate, DetBuild, NULL };

bool Ui_OpenCaughtDexPage(const uint16_t nationalNum)
{
    if (!Ui_IsEnabled() || Ui_IsOpen() || nationalNum == 0 || nationalNum > NATIONAL_DEX_COUNT)
        return false;
    sDetailNat = nationalNum;
    sCaughtPage = true;
    if (!UiGame_OpenBattleMenu(&gUiDexDetailScreen))
    {
        sCaughtPage = false;
        return false;
    }
    PlayCry_Normal(NationalPokedexNumToSpecies(nationalNum), 0);
    sCryTicks = 60;
    return true;
}
