// OPTIONS of the 960x544 native UI (1:1 port of pokeemerald-vita-3ds layout with
// FireRed GBA palette and FireRed settings/controls/cheats).

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "platform/vita_controls.h"
#include "platform/vita_extras.h"
#include "platform/vita_nuzlocke.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "m4a.h"
#include "platform.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/songs.h"

enum { ROW_CORE = 0, ROW_EXTRA, ROW_REMAP, ROW_QUIT };
typedef struct { uint8_t kind; uint16_t sub; const char *group, *label; const VitaExtra *ex; } OptRow;

#define LIST_X 28
#define LIST_W 904
#define LIST_Y 72
#define ROW_H  40
#define ROWS   9
#define GBA_LIST_X 26
#define GBA_LIST_W 892
#define GBA_LIST_Y 104
#define GBA_ROW_H  41
#define GBA_ROWS   8
#define MAX_ROWS   40

static int32_t OptVisRows(void)
{
    return gUiTheme->gbaFrame ? GBA_ROWS : ROWS;
}

static const char *const sTabs[VITA_TAB_COUNT] = {
    UI_N("GENERAL"), UI_N("GRAPHICS"), UI_N("WORLD"), UI_N("BATTLE"), UI_N("MODES"), UI_N("CHEATS")
};
static const char *const sCoreNames[6] = {
    UI_N("TEXT SPEED"), UI_N("BATTLE SCENE"), UI_N("BATTLE STYLE"), UI_N("SOUND OUTPUT"),
    UI_N("MASTER VOLUME"), UI_N("BUTTON MODE")
};
static const uint8_t sDefaultButtons[VITA_ACTION_COUNT] = {
    4U,  // A -> CROSS
    5U,  // B -> CIRCLE
    11U, // SELECT
    10U, // START
    8U,  // L
    9U,  // R
    7U,  // FAST_FORWARD -> TRIANGLE
    6U,  // VITA_MENU -> SQUARE
};

static int32_t sTab;
static UiList sList[VITA_TAB_COUNT];
static OptRow sRows[MAX_ROWS];
static int32_t sCount;
static int32_t sTabX[VITA_TAB_COUNT], sTabW[VITA_TAB_COUNT];
static int32_t sRemap, sRemapWaitFor;
static UiList sRemapList;
static bool sQuit;

static const char *const sCoreHelp[6] = {
    UI_N("How fast the dialogue text is printed."),
    UI_N("Show the move animations in battle."),
    UI_N("SHIFT offers a switch when the foe sends out a POKEMON."),
    UI_N("Mono or stereo sound."),
    UI_N("Volume of music and sound effects."),
    UI_N("HELP bar, L/R pages, or L acting as the A button."),
};

static const struct { const char *key, *help; } sExtraHelp[] = {
    { "ui_mode",        UI_N("NATIVE 16:9 widescreen or authentic original GBA menus.") },
    { "ui_theme",       UI_N("Colour and style of the menus.") },
    { "voxel",          UI_N("Draw the overworld as a 3D voxel diorama.") },
    { "voxel_fps",      UI_N("Frame rate of the 3D world. 60 uses more battery.") },
    { "voxel_hd2d",     UI_N("LOW: sway, relief, particles. MED/HIGH: + bloom, shafts, grade.") },
    { "voxel_pitch",    UI_N("Camera tilt angle in 3D view.") },
    { "voxel_zoom",     UI_N("Camera zoom level in 3D view.") },
    { "voxel_bloom",    UI_N("Glow of bright parts of the world (0 = off). Needs HD-2D MED or HIGH.") },
    { "voxel_sky_bloom",UI_N("Sun and moon sky glow (5 = normal). Needs HD-2D MED or HIGH.") },
    { "voxel_shafts",   UI_N("Sun and moon light shafts (5 = normal). Needs HD-2D MED or HIGH.") },
    { "voxel_blur",     UI_N("Blur the far background. Bloom and light shafts stay on.") },
    { "run_always",     UI_N("Run without holding B; hold B to walk.") },
    { "run_indoors",    UI_N("Allow running inside buildings.") },
    { "follower",       UI_N("The first POKEMON of the party walks behind you.") },
    { "field_hms",      UI_N("Use field moves directly when interacting in the world.") },
    { "repel_prompt",   UI_N("Ask to use another REPEL when one wears off.") },
    { "daynight",       UI_N("Light changes with the clock.") },
    { "clock_speed",    UI_N("How fast the in-game clock runs.") },
    { "visible_wild",   UI_N("Wild POKEMON roam the grass and water; touch one to battle.") },
    { "encounter_rate", UI_N("How often wild POKEMON appear.") },
    { "voxel_battle",   UI_N("Fight on a 3D voxel arena.") },
    { "move_split",     UI_N("Physical or special by move, as in later games.") },
    { "trainer_teams",  UI_N("Use enhanced boss/trainer rosters and AI.") },
    { "exp_share",      UI_N("Every POKEMON in the party gains EXP.") },
    { "exp_catch",      UI_N("Catching a POKEMON gives EXP.") },
    { "trade_evo",      UI_N("Trade evolutions happen at level 40.") },
    { "fast_eggs",      UI_N("EGGS hatch much sooner.") },
    { "reusable_tms",   UI_N("TMs are not used up.") },
    { "nuzlocke",       UI_N("One catch per area; fainted POKEMON are lost.") },
    { "level_cap",      UI_N("Limit levels to the next GYM LEADER's ace.") },
    { "shiny_odds",     UI_N("Chance that a wild POKEMON is shiny.") },
    { "always_catch",   UI_N("Every POKE BALL catches.") },
    { "instant_victory",UI_N("Win active battles immediately when enabled.") },
    { "infinite_money", UI_N("Prevent spending or dropping money below max.") },
    { "money_max",      UI_N("Set your money to $999,999.") },
    { "dex_kanto",      UI_N("Mark every KANTO POKEMON as seen and caught.") },
    { "dex_national_on",UI_N("Unlock the NATIONAL POKEDEX immediately.") },
    { "dex_national",   UI_N("Unlock and complete the full NATIONAL POKEDEX.") },
    { "mega_kit",       UI_N("Receive Key Stone and Mega Stones.") },
    { "give_pocket",    UI_N("Pocket of the item to give.") },
    { "give_item",      UI_N("Item to give.") },
    { "give_count",     UI_N("How many to give.") },
    { "give_now",       UI_N("Put the chosen items in the BAG.") },
};

static const char *RowHelp(const OptRow *const r)
{
    if (r->kind == ROW_REMAP) return UI_TR("Choose the Vita button of each action.");
    if (r->kind == ROW_QUIT)  return UI_TR("Save the settings and quit to the LiveArea.");
    if (r->kind == ROW_CORE)  return UiLocale_Lookup(sCoreHelp[r->sub]);
    for (size_t i = 0; i < sizeof(sExtraHelp) / sizeof(sExtraHelp[0]); i++)
    {
        if (strcmp(sExtraHelp[i].key, r->ex->key) == 0)
            return UiLocale_Lookup(sExtraHelp[i].help);
    }
    return r->ex->count == 0 ? UI_TR("Press X to run it.") : UI_TR("Left / right or X to change the value.");
}

static void BuildRows(void)
{
    sCount = 0;
    if (sTab == VITA_TAB_GENERAL)
    {
        for (uint16_t i = 0; i < 6; i++)
        {
            sRows[sCount++] = (OptRow){
                ROW_CORE, i,
                i < 3 ? UI_TR("GAMEPLAY") : i < 5 ? UI_TR("AUDIO") : UI_TR("INTERFACE"),
                UiLocale_Lookup(sCoreNames[i]), NULL
            };
        }
        sRows[sCount++] = (OptRow){ ROW_REMAP, 0, UI_TR("CONTROLS"), UI_TR("BUTTON REMAPPING"), NULL };
    }
    const unsigned n = VitaExtras_GetTabExtraCount((uint8_t)sTab);
    for (unsigned i = 0; i < n && sCount < MAX_ROWS - 1; i++)
    {
        const VitaExtra *const ex = VitaExtras_GetTabExtra((uint8_t)sTab, i);
        if (ex != NULL)
            sRows[sCount++] = (OptRow){ ROW_EXTRA, (uint16_t)i, UiLocale_Lookup(ex->group), UiLocale_Lookup(ex->name), ex };
    }
    if (sTab == VITA_TAB_MODES)
        sRows[sCount++] = (OptRow){ ROW_QUIT, 0, UI_TR("SYSTEM"), UI_TR("QUIT TO LIVEAREA"), NULL };
    UiList_Reset(&sList[sTab], sCount, OptVisRows());
}

static void CoreValue(const uint16_t id, char *const out, const size_t cap)
{
    static const char *const spd[3]  = { UI_N("SLOW"), UI_N("MID"), UI_N("FAST") };
    static const char *const mode[3] = { UI_N("HELP"), "LR", UI_N("L=A") };
    switch (id)
    {
    case 0: snprintf(out, cap, "%s", UiLocale_Lookup(spd[UiGame_GetTextSpeed() % 3U])); break;
    case 1: snprintf(out, cap, "%s", UiGame_GetBattleScene() ? UI_TR("OFF") : UI_TR("ON")); break;
    case 2: snprintf(out, cap, "%s", UiGame_GetBattleStyle() ? UI_TR("SET") : UI_TR("SHIFT")); break;
    case 3: snprintf(out, cap, "%s", UiGame_GetSoundMode() ? UI_TR("STEREO") : UI_TR("MONO")); break;
    case 4: snprintf(out, cap, "%u%%", (unsigned)Platform_GetSetting(PLATFORM_SETTING_VOLUME) * 10U); break;
    default: snprintf(out, cap, "%s", UiLocale_Lookup(mode[UiGame_GetButtonMode() % 3U])); break;
    }
}

static void StepCore(const uint16_t id, const int32_t dir)
{
    switch (id)
    {
    case 0: UiGame_SetTextSpeed((uint8_t)((3 + UiGame_GetTextSpeed() + dir) % 3)); break;
    case 1: UiGame_SetBattleScene((uint8_t)(UiGame_GetBattleScene() ^ 1U)); break;
    case 2: UiGame_SetBattleStyle((uint8_t)(UiGame_GetBattleStyle() ^ 1U)); break;
    case 3: UiGame_SetSoundMode((uint8_t)(UiGame_GetSoundMode() ^ 1U)); break;
    case 4:
    {
        const int32_t v = (int32_t)Platform_GetSetting(PLATFORM_SETTING_VOLUME) + dir;
        Platform_SetSetting(PLATFORM_SETTING_VOLUME, (u8)(v < 0 ? 10 : v > 10 ? 0 : v));
        break;
    }
    default: UiGame_SetButtonMode((uint8_t)((3 + UiGame_GetButtonMode() + dir) % 3)); break;
    }
}

static void ExtraValue(const VitaExtra *const ex, char *const out, const size_t cap)
{
    if (ex->text != NULL)
    {
        UiGame_Text(ex->text(), out, cap);
    }
    else if (ex->count == 0)
    {
        snprintf(out, cap, UI_TR("EXECUTE"));
    }
    else
    {
        const uint8_t idx = VitaExtras_ValueIndex(ex);
        snprintf(out, cap, "%s", (idx < ex->count && ex->values) ? UiLocale_Lookup(ex->values[idx]) : "?");
    }
}

static void Enter(void)
{
    sTab = VITA_TAB_GENERAL;
    sRemap = 0;
    sQuit = false;
    BuildRows();
}

static void Step(const OptRow *const r, const int32_t dir)
{
    PlaySE(SE_SELECT);
    if (r->kind == ROW_CORE)
    {
        StepCore(r->sub, dir);
    }
    else if (r->kind == ROW_REMAP)
    {
        sRemap = 1;
        UiList_Reset(&sRemapList, VITA_ACTION_COUNT + 1, VITA_ACTION_COUNT + 1);
    }
    else if (r->kind == ROW_QUIT)
    {
        sQuit = true;
    }
    else if (r->ex != NULL)
    {
        if (r->ex->count == 0 && !UiGame_HasSaveData())
        {
            PlaySE(SE_FAILURE);
            UiKit_Toast(UI_TR("Load a save file before using cheats."));
            return;
        }
        VitaExtras_Step(r->ex, dir);
        UiTheme_Refresh();
        if (r->ex->count == 0)
            UiKit_Toast(UI_TR("Applied: %s"), UiLocale_Lookup(r->ex->name));
    }
}

static int32_t CoreChoices(const uint16_t id, const char *const **const out)
{
    static const char *const kSpd[3]   = { UI_N("SLOW"), UI_N("MID"), UI_N("FAST") };
    static const char *const kOnOff[2] = { UI_N("ON"), UI_N("OFF") };
    static const char *const kStyle[2] = { UI_N("SHIFT"), UI_N("SET") };
    static const char *const kSound[2] = { UI_N("MONO"), UI_N("STEREO") };
    static const char *const kMode[3]  = { UI_N("HELP"), "LR", UI_N("L=A") };
    switch (id)
    {
    case 0: *out = kSpd;   return UiGame_GetTextSpeed() % 3U;
    case 1: *out = kOnOff; return UiGame_GetBattleScene() ? 1 : 0;
    case 2: *out = kStyle; return UiGame_GetBattleStyle() ? 1 : 0;
    case 3: *out = kSound; return UiGame_GetSoundMode() ? 1 : 0;
    case 5: *out = kMode;  return UiGame_GetButtonMode() % 3U;
    default: *out = NULL;  return -1;
    }
}

static int32_t CoreChoiceCount(const uint16_t id)
{
    return (id == 0 || id == 5) ? 3 : (id == 4) ? 0 : 2;
}

static int32_t RowChoices(const OptRow *const row, const char *const **const vals, int32_t *const curOut)
{
    if (row->kind == ROW_CORE && row->sub != 4)
    {
        *curOut = CoreChoices(row->sub, vals);
        return CoreChoiceCount(row->sub);
    }
    if (row->kind == ROW_EXTRA && row->ex != NULL && row->ex->text == NULL
        && row->ex->count >= 2 && row->ex->count <= 4 && row->ex->values != NULL)
    {
        *vals = row->ex->values;
        *curOut = (int32_t)VitaExtras_ValueIndex(row->ex);
        return row->ex->count;
    }
    *vals = NULL;
    *curOut = -1;
    return 0;
}

static bool Update(const UiInput *const in)
{
    const bool gba = gUiTheme->gbaFrame;
    if (sQuit)
    {
        if (in->pressed & VITA_BTN_CROSS)
        {
            VitaExtras_Save();
            VitaPlatform_RequestQuit();
        }
        if ((in->pressed & VITA_BTN_CIRCLE) || in->tap)
        {
            PlaySE(SE_SELECT);
            sQuit = false;
        }
        return true;
    }
    if (sRemap == 2)
    {
        if (in->tap)
        {
            PlaySE(SE_SELECT);
            sRemap = 1;
            return true;
        }
        for (uint8_t bit = 4; bit < 16; bit++)
        {
            const uint32_t mask = VitaControls_BitFromSetting(bit);
            if (mask != 0U && (in->pressed & mask) != 0U)
            {
                VitaControls_SetButton((uint8_t)sRemapWaitFor, bit);
                VitaControls_Save();
                VitaExtras_Save();
                PlaySE(SE_SELECT);
                sRemap = 1;
                break;
            }
        }
        return true;
    }
    if (sRemap == 1)
    {
        if ((in->pressed & VITA_BTN_CIRCLE) || (in->tap && !UiHit(in, 200, 90, 560, 360)))
        {
            PlaySE(SE_SELECT);
            sRemap = 0;
            return true;
        }
        if ((in->pressed & VITA_BTN_TRIANGLE) && sRemapList.sel < (int32_t)VITA_ACTION_COUNT)
        {
            VitaControls_SetButton((uint8_t)sRemapList.sel, VITA_BTN_UNBOUND);
            VitaControls_Save();
            VitaExtras_Save();
            PlaySE(SE_SELECT);
            UiKit_Toast(UI_TR("%s has no button now."), VitaControls_ActionName((uint8_t)sRemapList.sel));
            return true;
        }
        const int32_t a = UiList_Update(&sRemapList, in, 220, 150, 520, 36, true);
        if (a == (int32_t)VITA_ACTION_COUNT)
        {
            for (uint8_t i = 0; i < VITA_ACTION_COUNT; i++)
                VitaControls_SetButton(i, sDefaultButtons[i]);
            VitaControls_Save();
            VitaExtras_Save();
            PlaySE(SE_SUCCESS);
            UiKit_Toast(UI_TR("Buttons reset to the defaults."));
        }
        else if (a >= 0)
        {
            PlaySE(SE_SELECT);
            sRemapWaitFor = a;
            sRemap = 2;
        }
        return true;
    }
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        VitaControls_Save();
        VitaExtras_Save();
        return false;
    }
    if (in->pressed & VITA_BTN_START)
    {
        PlaySE(SE_SELECT);
        sQuit = true;
        return true;
    }
    if (in->pressed & VITA_BTN_SELECT)
    {
        PlaySE(SE_SELECT);
        sRemap = 1;
        UiList_Reset(&sRemapList, VITA_ACTION_COUNT + 1, VITA_ACTION_COUNT + 1);
        return true;
    }

    int32_t t = -1;
    if (gba)
    {
        const int32_t ph = UiW_GbaPagerHit(in, true, VITA_TAB_COUNT);
        if (ph != 0)
            t = (sTab + VITA_TAB_COUNT + ph) % VITA_TAB_COUNT;
    }
    else
    {
        t = UiKit_TabHit(in, sTabX, sTabW, VITA_TAB_COUNT);
    }
    if (in->pressed & VITA_BTN_L) t = (sTab + VITA_TAB_COUNT - 1) % VITA_TAB_COUNT;
    if (in->pressed & VITA_BTN_R) t = (sTab + 1) % VITA_TAB_COUNT;
    if (t >= 0 && t != sTab)
    {
        sTab = t;
        PlaySE(SE_SELECT);
        BuildRows();
        return true;
    }

    UiList *const l = &sList[sTab];
    l->rows = OptVisRows();
    const int32_t lx = gba ? GBA_LIST_X : LIST_X;
    const int32_t ly = gba ? GBA_LIST_Y : LIST_Y;
    const int32_t lw = gba ? GBA_LIST_W : LIST_W;
    const int32_t rh = gba ? GBA_ROW_H : ROW_H;
    const int32_t a = UiList_Update(l, in, lx, ly, lw, rh, false);
    if (sCount > 0)
    {
        if (in->repeat & VITA_BTN_LEFT)       Step(&sRows[l->sel], -1);
        else if (in->repeat & VITA_BTN_RIGHT) Step(&sRows[l->sel], 1);
        else if (a >= 0)                      Step(&sRows[a], 1);
    }
    return true;
}

static void DrawModals(void)
{
    if (sRemap)
    {
        UiDraw_Rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, UI_COL_DIM);
        UiW_Window(200, 100, 560, 400, sRemap == 2 ? UI_TR("PRESS A BUTTON") : UI_TR("BUTTON REMAPPING"));
        UiText_Draw(UI_FACE_PS2P, 16, 220, 482, UI_TR("X: change  TRIANGLE: no button"), UI_COL_INK_SOFT);
        for (int32_t i = 0; i <= (int32_t)VITA_ACTION_COUNT; i++)
        {
            const int32_t y = 150 + i * 36;
            const bool on = (i == sRemapList.sel);
            UiW_Row(220, y, 520, 36, on, i);
            const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
            if (i == (int32_t)VITA_ACTION_COUNT)
            {
                UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 248, y + 2, UI_TR("RESET TO DEFAULTS"), ink);
                continue;
            }
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 248, y + 2, VitaControls_ActionName((uint8_t)i), ink);
            const uint8_t bit = VitaControls_GetButton((uint8_t)i);
            const char *const b = (sRemap == 2 && i == sRemapWaitFor) ? UI_TR("PRESS...")
                                : VitaControls_ButtonName(bit);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 720 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, b), y + 2, b, ink);
        }
    }
    if (sQuit)
    {
        UiDraw_Rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, UI_COL_DIM);
        UiW_Window(280, 200, 400, 140, UI_TR("QUIT"));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 300, 250, UI_TR("Quit to the LiveArea?"), UI_COL_INK);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 300, 286, UI_TR("X: quit   O: cancel"), UI_COL_INK_SOFT);
    }
}

static void BuildGba(void)
{
    char buf[64];
    UiW_GbaTopBarPages(UI_GBA_BG_OPTIONS, UI_SPR_START_GEAR, UI_TR("OPTIONS"), sTabs, VITA_TAB_COUNT, sTab);

    int32_t rx = UI_SCREEN_W - 14;
    UiW_GbaHelpBadge(&rx, "O", UI_TR("BACK"), true);
    UiW_GbaHelpBadge(&rx, "SELECT", UI_TR("REMAP"), true);
    UiW_GbaHelpBadge(&rx, "X", UI_TR("CHANGE"), false);

    UiW_GbaWin(14, 60, 932, 384, UI_GBA_WIN_DEFAULT);
    snprintf(buf, sizeof(buf), UI_TR("TAB %d / %d · %s"), (int)sTab + 1, VITA_TAB_COUNT, UiLocale_Lookup(sTabs[sTab]));
    UiW_GbaSectionStrip(26, 70, 908, 28, UI_GBA_TAG_CRIMSON, UI_TR("SYSTEM & GAMEPLAY OPTIONS"), buf);

    UiW_GbaBox(GBA_LIST_X, GBA_LIST_Y, GBA_LIST_W, GBA_ROWS * GBA_ROW_H, 6, UI_RGBA(136, 148, 186, 255),
               UI_RGBA(250, 248, 240, 255), UI_RGBA(244, 244, 234, 255), C_WHITE, 0);
    UiList *const l = &sList[sTab];
    l->rows = GBA_ROWS;
    UiDraw_PushClip(GBA_LIST_X + 2, GBA_LIST_Y + 2, GBA_LIST_W - 4, GBA_ROWS * GBA_ROW_H - 4);
    for (int32_t r = 0; r < GBA_ROWS && l->first + r < sCount; r++)
    {
        const int32_t i = l->first + r;
        const int32_t y = GBA_LIST_Y + r * GBA_ROW_H;
        const OptRow *const row = &sRows[i];
        const bool on = (i == l->sel);
        if (on)
        {
            UiW_GbaBox(GBA_LIST_X + 4, y + 2, GBA_LIST_W - 8, GBA_ROW_H - 4, 4, UI_RGBA(230, 148, 65, 255),
                       UI_RGBA(255, 249, 224, 255), UI_RGBA(255, 242, 186, 255), C_WHITE, 0);
            UiW_Arrow(GBA_LIST_X + 12, y + 14, UI_RGBA(230, 8, 8, 255));
        }
        else
        {
            UiDraw_Rect(GBA_LIST_X + 2, y, GBA_LIST_W - 4, GBA_ROW_H,
                        (i & 1) ? UI_RGBA(242, 242, 236, 255) : UI_RGBA(250, 248, 242, 255));
            if (r < GBA_ROWS - 1)
                UiDraw_Rect(GBA_LIST_X + 2, y + GBA_ROW_H - 2, GBA_LIST_W - 4, 2, UI_RGBA(224, 224, 218, 255));
        }
        UiW_GbaTextMid(UI_FACE_VT323, 28, GBA_LIST_X + 34, y, GBA_ROW_H, row->label,
                       UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT);

        const char *const *vals = NULL;
        int32_t curVal = -1;
        const int32_t vc = RowChoices(row, &vals, &curVal);
        if (vc >= 2)
        {
            int32_t px = GBA_LIST_X + GBA_LIST_W - 14;
            for (int32_t j = vc - 1; j >= 0; j--)
            {
                const int32_t pw = UiText_Width(UI_FACE_VT323, 28, UiLocale_Lookup(vals[j])) + 22;
                px -= pw;
                if (j == curVal)
                {
                    UiW_GbaBox(px, y + 5, pw, 30, 4, UI_RGBA(216, 32, 24, 255),
                               UI_RGBA(255, 240, 238, 255), UI_RGBA(252, 224, 220, 255), C_WHITE, 1);
                    UiW_GbaTextCentred(UI_FACE_VT323, 28, px, y + 5, pw, 30, UiLocale_Lookup(vals[j]),
                                       UI_RGBA(216, 32, 24, 255), UI_RGBA(248, 200, 196, 255));
                }
                else
                {
                    UiW_GbaTextCentred(UI_FACE_VT323, 28, px, y + 5, pw, 30, UiLocale_Lookup(vals[j]),
                                       UI_RGBA(130, 136, 144, 255), UI_GBA_SHAD_LIGHT);
                }
                px -= 8;
            }
        }
        else
        {
            if (row->kind == ROW_CORE) CoreValue(row->sub, buf, sizeof(buf));
            else if (row->kind == ROW_EXTRA) ExtraValue(row->ex, buf, sizeof(buf));
            else snprintf(buf, sizeof(buf), "%s", row->kind == ROW_REMAP ? UI_TR("OPEN") : UI_TR("EXECUTE"));
            const int32_t pw = UiText_Width(UI_FACE_VT323, 28, buf) + 28;
            const int32_t px = GBA_LIST_X + GBA_LIST_W - 18 - pw;
            UiW_GbaBox(px, y + 5, pw, 30, 4, UI_RGBA(216, 32, 24, 255),
                       UI_RGBA(255, 240, 238, 255), UI_RGBA(252, 224, 220, 255), C_WHITE, 1);
            UiW_GbaTextCentred(UI_FACE_VT323, 28, px, y + 5, pw, 30, buf,
                               UI_RGBA(216, 32, 24, 255), UI_RGBA(248, 200, 196, 255));
            if (on && (row->kind == ROW_CORE || (row->kind == ROW_EXTRA && row->ex != NULL && row->ex->count > 1)))
            {
                UiW_GbaArrowH(px - 18, y + 14, false, UI_RGBA(216, 32, 24, 255));
                UiW_GbaArrowH(px + pw + 6, y + 14, true, UI_RGBA(216, 32, 24, 255));
            }
        }
    }
    UiDraw_PopClip();
    UiW_ScrollBar(924, GBA_LIST_Y, GBA_ROWS * GBA_ROW_H, l->first, GBA_ROWS, sCount);

    UiW_GbaMsg(14, 452, 932, 80, false, true);
    if (sCount > 0)
    {
        const OptRow *const sel = &sRows[l->sel];
        UiW_GbaTextWrapped(UI_FACE_VT323, 28, 38, 464, 868, 28, RowHelp(sel),
                           UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT, 2);
    }
    DrawModals();
}

static void Build(void)
{
    if (gUiTheme->gbaFrame)
    {
        BuildGba();
        return;
    }
    char buf[48];
    UiKit_Header(UI_TR("OPTIONS"), sTabs, VITA_TAB_COUNT, sTab, sTabX, sTabW, 12);
    UiW_Panel(16, 60, 928, 376);
    const UiList *const l = &sList[sTab];
    UiDraw_PushClip(LIST_X, LIST_Y, LIST_W, ROWS * ROW_H);
    const char *prevGroup = l->first > 0 ? sRows[l->first - 1].group : NULL;
    for (int32_t r = 0; r < ROWS && l->first + r < sCount; r++)
    {
        const int32_t i = l->first + r;
        const int32_t y = LIST_Y + r * ROW_H;
        const OptRow *const row = &sRows[i];
        const bool on = (i == l->sel);
        UiW_Row(LIST_X, y, LIST_W - 12, ROW_H, on, i);
        const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
        if (row->group && (prevGroup == NULL || strcmp(prevGroup, row->group) != 0))
            UiText_Draw(UI_FACE_PS2P, 16, LIST_X + 28, y + 12, row->group, UI_COL_MONEY);
        prevGroup = row->group;
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 220, y + 4, row->label, ink);
        if (row->kind == ROW_CORE) CoreValue(row->sub, buf, sizeof(buf));
        else if (row->kind == ROW_EXTRA) ExtraValue(row->ex, buf, sizeof(buf));
        else snprintf(buf, sizeof(buf), "%s", row->kind == ROW_REMAP ? UI_TR("OPEN") : UI_TR("EXECUTE"));
        const int32_t vw = UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf);
        const int32_t vx = LIST_X + LIST_W - 60 - vw;
        UiW_Capsule(vx - 12, y + 4, vw + 24, 32);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, vx, y + 4, buf, UI_COL_TITLE);
        if (on && (row->kind == ROW_CORE || (row->kind == ROW_EXTRA && row->ex != NULL && row->ex->count > 1)))
        {
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, vx - 40, y + 4, "<", ink);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + LIST_W - 40, y + 4, ">", ink);
        }
    }
    UiDraw_PopClip();
    UiW_ScrollBar(LIST_X + LIST_W - 9, LIST_Y, ROWS * ROW_H, l->first, ROWS, sCount);
    UiW_PanelDark(16, 444, 928, 48);
    if (sCount > 0)
    {
        const OptRow *const sel = &sRows[l->sel];
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 32, 450, RowHelp(sel), UI_COL_ON_FRAME);
    }

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Change"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Save"));
    UiW_FooterKey(&fx, "SELECT", UI_TR("Remap"));
    UiW_FooterKey(&fx, "START", UI_TR("Quit"));
    UiKit_FooterClock();
    DrawModals();
}

const UiScreen gUiOptionsScreen = { Enter, Update, Build, NULL };

bool Ui_OpenOptions(void)
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    UiCore_OpenScreen(&gUiOptionsScreen);
    return true;
}

bool Ui_OpenOptionsHosted(void (*const after)(void))
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    UiCore_OpenHosted(&gUiOptionsScreen, after);
    return true;
}
