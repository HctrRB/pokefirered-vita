// Kanto NUZLOCKE tracker of the 960x544 native UI (1:1 port of pokeemerald-vita-3ds
// layout backed by FireRed's VitaNuzlocke_* encounter/graveyard tracker).

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/vita_extras.h"
#include "platform/vita_nuzlocke.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "pokemon.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/songs.h"
#include "constants/species.h"

#define LIST_X 28
#define LIST_W 560
#define LIST_Y 106
#define ROW_H  40
#define ROWS   9

static const char *const sTabs[4] = { UI_N("ENCOUNTERS"), UI_N("SQUAD"), UI_N("RULES"), UI_N("SETTINGS") };
static const char *const sRules[4][2] = {
    { UI_N("One encounter per area"), UI_N("Only the first wild POKéMON of each area can be caught.") },
    { UI_N("Fainted = released"), UI_N("A POKéMON that faints counts as fallen and can't be used.") },
    { UI_N("Level cap"), UI_N("The next GYM LEADER's highest level (option LEVEL CAP).") },
    { UI_N("Nicknames"), UI_N("Give every POKéMON a nickname to bond with it.") },
};

static int32_t sTab, sTabX[4], sTabW[4];
static UiList sList[4];
static const VitaExtra *sSettings[8];
static int32_t sSettingCount;
static bool sResetArmed;

static void BuildSettings(void)
{
    sSettingCount = 0;
    const unsigned n = VitaExtras_GetTabExtraCount(VITA_TAB_MODES);
    for (unsigned i = 0; i < n && sSettingCount < 8; i++)
    {
        const VitaExtra *const ex = VitaExtras_GetTabExtra(VITA_TAB_MODES, i);
        if (ex != NULL && ex->group != NULL && strcmp(ex->group, "CHALLENGE") == 0)
            sSettings[sSettingCount++] = ex;
    }
}

static int32_t Count(void)
{
    if (sTab == 0) return (int32_t)VitaNuzlocke_GetRecordCount();
    if (sTab == 1) return UiGame_PartyCount();
    if (sTab == 2) return 4;
    return sSettingCount + 1;
}

static void Enter(void)
{
    BuildSettings();
    sResetArmed = false;
    for (int32_t t = 0; t < 4; t++)
    {
        sTab = t;
        UiList_Reset(&sList[t], Count(), ROWS);
    }
    sTab = 0;
}

static bool Update(const UiInput *const in)
{
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        return false;
    }

    int32_t t = gUiTheme->gbaFrame ? -1 : UiKit_TabHit(in, sTabX, sTabW, 4);
    if (gUiTheme->gbaFrame)
    {
        const int32_t ph = UiW_GbaPagerHit(in, true, 4);
        if (ph != 0)
            t = (sTab + 4 + ph) % 4;
    }
    if (in->pressed & VITA_BTN_L) t = (sTab + 3) % 4;
    if (in->pressed & VITA_BTN_R) t = (sTab + 1) % 4;
    if (t >= 0 && t != sTab)
    {
        sTab = t;
        PlaySE(SE_SELECT);
        UiList_Reset(&sList[sTab], Count(), ROWS);
        return true;
    }

    const int32_t a = UiList_Update(&sList[sTab], in, LIST_X, LIST_Y, LIST_W, ROW_H, false);
    if (sTab == 3)
    {
        const int32_t sel = sList[3].sel;
        const int32_t dir = (in->repeat & VITA_BTN_LEFT) ? -1
                          : (in->repeat & VITA_BTN_RIGHT) ? 1
                          : (a >= 0 ? 1 : 0);
        if (dir != 0 && sel < sSettingCount)
        {
            VitaExtras_Step(sSettings[sel], dir);
            VitaExtras_Save();
            PlaySE(SE_SELECT);
        }
        else if (a == sSettingCount)
        {
            if (!sResetArmed)
            {
                sResetArmed = true;
                PlaySE(SE_SELECT);
                UiKit_Toast(UI_TR("Press X again to reset the tracker."));
            }
            else
            {
                VitaNuzlocke_Reset();
                sResetArmed = false;
                UiList_Reset(&sList[0], Count(), ROWS);
                PlaySE(SE_SUCCESS);
                UiKit_Toast(UI_TR("Tracker reset."));
            }
        }
    }
    return true;
}

static void StateBadge(const int32_t x, const int32_t y, const char *const label, const uint32_t top, const uint32_t bot)
{
    UiDraw_RectV(x, y + 4, 140, 32, top, bot);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 70 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, label) / 2,
                y + 4, label, UI_COL_ON_FRAME);
}

static void Build(void)
{
    char buf[64];
    const uint8_t cap = VitaNuzlocke_GetLevelCap();
    const char *const leader = VitaNuzlocke_GetNextBossName();

    UiKit_HeaderAs(UI_GBA_BG_CARD, UI_SPR_BALL_PREMIER, UI_TR("NUZLOCKE"), sTabs, 4, sTab, sTabX, sTabW, 12);
    snprintf(buf, sizeof(buf), UI_TR("NEXT: %s (CAP LV.%u)"), leader ? leader : "-", (unsigned)cap);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    UiW_Panel(16, 60, 584, 432);
    UiW_PanelHead(16, 60, 584,
                  sTab == 0 ? UI_TR("ENCOUNTERS") : sTab == 1 ? UI_TR("SQUAD VITAL STATUS") : sTab == 2 ? UI_TR("RULES") : UI_TR("SETTINGS"),
                  sTab == 3 ? UI_TR("< > change") : NULL);

    const UiList *const l = &sList[sTab];
    const uint32_t green[2] = { UI_RGBA(110, 219, 151, 255), UI_COL_HP_HIGH };
    const uint32_t red[2]   = { UI_RGBA(240, 120, 104, 255), UI_COL_HP_LOW };

    const int32_t total = Count();
    for (int32_t r = 0; r < ROWS && l->first + r < total; r++)
    {
        const int32_t i = l->first + r;
        const int32_t y = LIST_Y + r * ROW_H;
        const bool on = (i == l->sel);
        const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
        UiW_Row(LIST_X, y, LIST_W - 12, ROW_H, on, i);

        if (sTab == 0)
        {
            const VitaNuzRecord *const rec = VitaNuzlocke_GetRecord((uint32_t)i);
            if (rec != NULL)
            {
                char sp[20];
                UiGame_SpeciesName(rec->species, sp, sizeof(sp));
                UiSpr_MonIcon(rec->species, 0, LIST_X + 24, y + 2, 1);
                snprintf(buf, sizeof(buf), "%s Lv%u", sp, (unsigned)rec->level);
                UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 64, y + 4, buf, ink);
                const bool dead = (rec->status == VITA_NUZ_DEAD);
                StateBadge(LIST_X + LIST_W - 160, y, dead ? UI_TR("FALLEN") : UI_TR("ALIVE"),
                           dead ? red[0] : green[0], dead ? red[1] : green[1]);
            }
        }
        else if (sTab == 1)
        {
            UiMon m;
            if (UiGame_Mon(i, &m))
            {
                UiSpr_MonIcon(m.species, 0, LIST_X + 24, y + 2, 1);
                UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 64, y + 4, m.name, ink);
                if (!m.egg)
                {
                    snprintf(buf, sizeof(buf), UI_TR("Lv%u"), (unsigned)m.level);
                    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 300, y + 4, buf, ink);
                    const bool dead = (m.hp == 0U);
                    StateBadge(LIST_X + LIST_W - 160, y, dead ? UI_TR("FALLEN") : UI_TR("ALIVE"),
                               dead ? red[0] : green[0], dead ? red[1] : green[1]);
                }
            }
        }
        else if (sTab == 2)
        {
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 28, y + 4, UiLocale_Lookup(sRules[i][0]), ink);
        }
        else if (i < sSettingCount)
        {
            const VitaExtra *const ex = sSettings[i];
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 28, y + 4, UiLocale_Lookup(ex->name), ink);
            const uint8_t idx = VitaExtras_ValueIndex(ex);
            const char *const val = (ex->values && idx < ex->count) ? UiLocale_Lookup(ex->values[idx]) : "?";
            const int32_t vw = UiText_Width(UI_FACE_VT323, UI_BODY_PX, val);
            UiW_Capsule(LIST_X + LIST_W - 60 - vw, y + 4, vw + 24, 32);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + LIST_W - 48 - vw, y + 4, val, UI_COL_TITLE);
        }
        else
        {
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 28, y + 4,
                        sResetArmed ? UI_TR("RESET TRACKER? (X)") : UI_TR("RESET TRACKER"),
                        on ? ink : UI_COL_HP_LOW);
        }
    }
    UiW_ScrollBar(588, LIST_Y, ROWS * ROW_H, l->first, ROWS, total);

    UiW_Panel(612, 60, 332, 200);
    if (sTab == 0)
    {
        const VitaNuzRecord *const rec = (total > 0) ? VitaNuzlocke_GetRecord((uint32_t)l->sel) : NULL;
        if (rec != NULL)
        {
            char zone[32], sp[24];
            UiGame_MapSecName(rec->mapSec, zone, sizeof(zone));
            UiGame_SpeciesName(rec->species, sp, sizeof(sp));
            UiText_Draw(UI_FACE_PS2P, 16, 628, 76, zone, UI_COL_INK);
            snprintf(buf, sizeof(buf), "%s (Lv.%u) - %s", sp, (unsigned)rec->level,
                     rec->status == VITA_NUZ_DEAD ? UI_TR("Fallen") : UI_TR("Caught"));
            UiW_TextWrapped(628, 108, 300, 30, buf, UI_COL_INK, 3);
        }
        else
        {
            UiText_Draw(UI_FACE_PS2P, 16, 628, 76, UI_TR("ENCOUNTERS"), UI_COL_INK);
            UiW_TextWrapped(628, 108, 300, 30, UI_TR("No Kanto encounters recorded yet."), UI_COL_INK_SOFT, 3);
        }
    }
    else if (sTab == 1 && UiGame_PartyCount() > 0)
    {
        UiMon m;
        UiGame_Mon(l->sel, &m);
        UiText_Draw(UI_FACE_PS2P, 16, 628, 76, m.name, UI_COL_INK);
        snprintf(buf, sizeof(buf), m.hp == 0U ? UI_TR("Fainted: it can no longer be used.") : UI_TR("Alive. Level cap: %u"),
                 (unsigned)cap);
        UiW_TextWrapped(628, 108, 300, 30, buf, UI_COL_INK, 3);
    }
    else if (sTab == 3)
    {
        UiText_Draw(UI_FACE_PS2P, 16, 628, 76, UI_TR("SETTINGS"), UI_COL_INK);
        UiW_TextWrapped(628, 108, 300, 30,
                        l->sel < sSettingCount ? UI_TR("Left / right or X to change. Saved at once.")
                                               : UI_TR("Clears all encounters and fallen records of this save slot."),
                        UI_COL_INK, 4);
    }
    else if (sTab == 2)
    {
        UiText_Draw(UI_FACE_PS2P, 16, 628, 76, UI_TR("RULE"), UI_COL_INK);
        UiW_TextWrapped(628, 108, 300, 30, UiLocale_Lookup(sRules[l->sel][1]), UI_COL_INK, 4);
    }

    uint32_t alive = 0, fallen = 0;
    const uint32_t recTotal = VitaNuzlocke_GetRecordCount();
    for (uint32_t i = 0; i < recTotal; i++)
    {
        const VitaNuzRecord *const rec = VitaNuzlocke_GetRecord(i);
        if (rec != NULL)
        {
            if (rec->status == VITA_NUZ_DEAD) fallen++;
            else alive++;
        }
    }
    UiW_Panel(612, 272, 332, 140);
    const char *const k[3] = { UI_TR("ALIVE CAUGHT"), UI_TR("FALLEN ALLIES"), UI_TR("KANTO BADGES") };
    const uint32_t v[3] = { alive, fallen, VitaNuzlocke_GetBadgesCount() };
    for (int32_t i = 0; i < 3; i++)
    {
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 628, 284 + i * 38, k[i], UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u", (unsigned)v[i]);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 924 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf),
                    284 + i * 38, buf, UI_COL_INK);
    }

    int32_t fx = 16;
    if (sTab == 3)
        UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Change"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    UiKit_FooterClock();
}

const UiScreen gUiNuzlockeScreen = { Enter, Update, Build, NULL };
