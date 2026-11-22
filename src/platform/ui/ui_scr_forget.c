// "Which move should be forgotten?" of the native 960x544 UI, replacing the summary screen's
// SELECT_MOVE mode (ShowSelectMovePokemonSummaryScreen), plus the Move Maniac / Move Relearner
// screen (Ui_OpenMoveRelearner, learn_move.c). Ported 1:1 from pokeemerald-vita-3ds and adapted to
// FireRed v1.1 (no Hoenn contest stats).

#include <stdio.h>
#include <string.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "battle_main.h"
#include "data.h"
#include "event_data.h"
#include "main.h"
#include "menu_helpers.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_summary_screen.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "strings.h"
#include "string_util.h"
#pragma GCC diagnostic pop
#include "constants/moves.h"
#include "constants/songs.h"

extern void SummaryScreen_SetMoveSlotForUi(u8 slot);
extern s32 MoveRelearner_MovesForUi(u16 *moves);
extern bool8 MoveRelearner_TeachForUi(u16 move);
extern void MoveRelearner_ReplaceForUi(u8 slot, u16 move);
extern void MoveRelearner_GiveUpForUi(void);
extern void MoveRelearner_ExitForUi(void);

#define ROW_X 348
#define ROW_W 580
#define ROW_Y 106
#define ROW_H 40
#define NEW_Y (ROW_Y + MAX_MON_MOVES * ROW_H + 30)

typedef struct
{
    uint16_t move, power;
    uint8_t type, accuracy, pp, maxPp;
    char name[16], desc[96];
} ForgetMove;

static struct Pokemon *sMon;
static uint8_t sMonIndex;
static uint16_t sNewMove;
static void (*sCallback)(void);
static ForgetMove sMoves[MAX_MON_MOVES + 1];   // [MAX_MON_MOVES]: the new move
static int32_t sSel;
static uint32_t sFrame;

static void Fill(ForgetMove *m, uint16_t move, int32_t slot)
{
    memset(m, 0, sizeof(*m));
    move = UiGame_SafeMove(move);
    m->move = move;
    if (move == MOVE_NONE)
        return;
    const struct BattleMove *bm = &gBattleMoves[move];
    UiGame_Text(gMoveNames[move], m->name, sizeof(m->name));
    UiGame_Text(gMoveDescriptionPointers[move - 1], m->desc, sizeof(m->desc));
    m->type = bm->type;
    m->power = bm->power;
    m->accuracy = bm->accuracy;
    if (slot < 0)
        m->pp = m->maxPp = bm->pp;
    else
    {
        m->pp = (uint8_t)GetMonData(sMon, MON_DATA_PP1 + slot);
        m->maxPp = (uint8_t)CalculatePPWithBonus(move, (u8)GetMonData(sMon, MON_DATA_PP_BONUSES), (u8)slot);
    }
}

static void Finish(void)
{
    ResetSpriteData();
    FreeAllSpritePalettes();
    if (sCallback != NULL)
        SetMainCallback2(sCallback);
}

bool Ui_OpenForgetMove(struct Pokemon *mons, uint8_t monIndex, uint16_t newMove, void (*callback)(void))
{
    if (!Ui_IsEnabled() || Ui_IsOpen() || mons == NULL || callback == NULL)
        return false;
    sMon = &mons[monIndex];
    sMonIndex = monIndex;
    sNewMove = newMove;
    sCallback = callback;
    SetVBlankHBlankCallbacksToNull();
    ScanlineEffect_Stop();
    ResetPaletteFade();
    ResetSpriteData();
    FreeAllSpritePalettes();
    SummaryScreen_SetMoveSlotForUi(MAX_MON_MOVES);
    UiCore_OpenHosted(&gUiForgetMoveScreen, NULL);
    return true;
}

static bool Learning(void)
{
    return sNewMove != MOVE_NONE;
}

static bool Selectable(int32_t i)
{
    return i == MAX_MON_MOVES ? Learning() : sMoves[i].move != MOVE_NONE;
}

static void Enter(void)
{
    for (int32_t i = 0; i < MAX_MON_MOVES; i++)
        Fill(&sMoves[i], (uint16_t)GetMonData(sMon, MON_DATA_MOVE1 + i), i);
    Fill(&sMoves[MAX_MON_MOVES], sNewMove, -1);
    sSel = 0;
    while (sSel < MAX_MON_MOVES && sMoves[sSel].move == MOVE_NONE)
        sSel++;
}

static void Choose(int32_t slot)
{
    PlaySE(SE_SELECT);
    SummaryScreen_SetMoveSlotForUi((u8)slot);
    UiNav_Close(Finish);
}

static void Step(int32_t dir)
{
    int32_t s = sSel;
    for (int32_t i = 0; i <= MAX_MON_MOVES; i++)
    {
        s += dir;
        if (s > MAX_MON_MOVES) s = 0;
        else if (s < 0) s = MAX_MON_MOVES;
        if (Selectable(s))
            break;
    }
    if (s != sSel)
    {
        sSel = s;
        PlaySE(SE_SELECT);
    }
}

static int32_t RowY(int32_t i)
{
    return i == MAX_MON_MOVES ? NEW_Y : ROW_Y + i * ROW_H;
}

static bool Update(const UiInput *in)
{
    sFrame++;
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        Choose(MAX_MON_MOVES);
        return true;
    }
    if (in->repeat & VITA_BTN_UP) Step(-1);
    if (in->repeat & VITA_BTN_DOWN) Step(1);
    bool activate = (in->pressed & VITA_BTN_CROSS) != 0;
    for (int32_t i = 0; i <= MAX_MON_MOVES; i++)
        if (Selectable(i) && UiHit(in, ROW_X, RowY(i), ROW_W, ROW_H))
        {
            if (i == sSel) activate = true;
            else { sSel = i; PlaySE(SE_SELECT); }
        }
    if (!activate)
        return true;
    if (sSel < MAX_MON_MOVES && sNewMove != MOVE_NONE && IsMoveHm(sMoves[sSel].move) && gMain.inBattle == 0)
    {
        char msg[64];
        PlaySE(SE_FAILURE);
        UiGame_Text(gText_PokeSum_HmMovesCantBeForgotten, msg, sizeof(msg));
        for (char *p = msg; *p; p++)
            if (*p == '\n') *p = ' ';
        UiKit_Toast("%s", msg);
        return true;
    }
    Choose(sSel);
    return true;
}

static void DrawMoveRow(const ForgetMove *m, int32_t y, bool on, int32_t i, bool known)
{
    const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
    char buf[16];
    UiW_Row(ROW_X, y, ROW_W, ROW_H, on, i);
    if (m->move == MOVE_NONE)
    {
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, ROW_X + 104, y + 4, "-", UI_COL_INK_SOFT);
        return;
    }
    UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + m->type), ROW_X + 28, y + 6, 2);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, ROW_X + 104, y + 4, m->name, ink);
    if (known && IsMoveHm(m->move))
        UiW_Chip(ROW_X + ROW_W - 200, y + 7, UI_TR("HM"), false);
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)m->pp, (unsigned)m->maxPp);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, ROW_X + ROW_W - 16 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf), y + 4, buf, ink);
}

static void DrawRow(int32_t i)
{
    DrawMoveRow(&sMoves[i], RowY(i), i == sSel, i, i < MAX_MON_MOVES);
}

static void DrawCompare(int32_t x, int32_t w, const char *tag, uint32_t tagCol, const ForgetMove *m)
{
    char buf[64];
    UiW_Inset(x, 356, w, 136);
    UiText_Draw(UI_FACE_PS2P, 16, x + 14, 368, tag, tagCol);
    if (m->move == MOVE_NONE)
        return;
    char pw[8], ac[8];
    if (m->power > 1) snprintf(pw, sizeof(pw), "%u", (unsigned)m->power); else snprintf(pw, sizeof(pw), "-");
    if (m->accuracy) snprintf(ac, sizeof(ac), "%u%%", (unsigned)m->accuracy); else snprintf(ac, sizeof(ac), "-");
    snprintf(buf, sizeof(buf), "%s · %s", pw, ac);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + w - 14 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf), 358, buf, UI_COL_INK);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 14, 390, m->name, UI_COL_INK);
    UiW_TextWrapped(x + 14, 424, w - 28, 30, m->desc, UI_COL_INK_SOFT, 2);
}

static void DrawMonPanel(const char *name)
{
    char buf[64];
    const uint16_t species = UiGame_SafeSpecies(GetMonData(sMon, MON_DATA_SPECIES));
    UiW_Panel(16, 60, 300, 272);
    UiW_Inset(30, 74, 272, 176);
    UiSpr_MonFront(species, IsMonShiny(sMon), 30 + (272 - 128) / 2, 74 + (176 - 128) / 2, 2);
    snprintf(buf, sizeof(buf), UI_TR("%s Lv%u"), name, (unsigned)GetMonData(sMon, MON_DATA_LEVEL));
    UiText_Draw(UI_FACE_PS2P, 16, 30, 262, buf, UI_COL_INK);
    UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + gSpeciesInfo[species].types[0]), 30, 290, 2);
    if (gSpeciesInfo[species].types[1] != gSpeciesInfo[species].types[0])
        UiDraw_Sprite((uint16_t)(UI_SPR_TYPE_FIRST + gSpeciesInfo[species].types[1]), 102, 290, 2);
    UiW_Inset(16, 344, 300, 148);
}

static void Build(void)
{
    char buf[96], name[24];
    UiKit_HeaderAs(UI_GBA_BG_SUMMARY, UI_SPR_BALL_POKE, Learning() ? UI_TR("LEARN A MOVE") : UI_TR("FORGET A MOVE"), NULL, 0, 0, NULL, NULL, 0);
    {
        u8 nick[POKEMON_NAME_LENGTH + 1];
        GetMonData(sMon, MON_DATA_NICKNAME, nick);
        UiGame_Text(nick, name, sizeof(name));
    }
    if (Learning())
        snprintf(buf, sizeof(buf), UI_TR("%s wants to learn %s"), name, sMoves[MAX_MON_MOVES].name);
    else
        snprintf(buf, sizeof(buf), "%s", name);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    DrawMonPanel(name);
    UiW_TextWrapped(30, 352, 272, 30, Learning() ? UI_TR("Choose a move to forget, or the new one to stop learning it.")
                                                 : UI_TR("Choose the move to forget."), UI_COL_INK, 4);

    UiW_Panel(332, 60, 612, 284);
    UiW_PanelHead(332, 60, 612, UI_TR("MOVES"), UI_TR("PP"));
    for (int32_t i = 0; i < MAX_MON_MOVES; i++)
        DrawRow(i);
    if (Learning())
    {
        UiText_Draw(UI_FACE_PS2P, 16, ROW_X + 12, NEW_Y - 24, UI_TR("NEW MOVE"), UI_RGBA(180, 83, 9, 255));
        DrawRow(MAX_MON_MOVES);
    }

    if (sSel < MAX_MON_MOVES)
        DrawCompare(332, 300, UI_TR("FORGET"), UI_RGBA(181, 53, 43, 255), &sMoves[sSel]);
    else
    {
        UiW_Inset(332, 356, 300, 136);
        UiText_Draw(UI_FACE_PS2P, 16, 346, 368, UI_TR("DON'T LEARN"), UI_COL_INK_SOFT);
        UiW_TextWrapped(346, 396, 272, 30, UI_TR("It keeps the four moves it knows."), UI_COL_INK, 3);
    }
    if (Learning())
        DrawCompare(644, 300, UI_TR("LEARN"), UI_RGBA(47, 158, 88, 255), &sMoves[MAX_MON_MOVES]);

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, sSel < MAX_MON_MOVES ? UI_TR("Forget") : UI_TR("Don't learn"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, Learning() ? UI_TR("Don't learn") : UI_TR("Cancel"));
    UiKit_FooterClock();
}

const UiScreen gUiForgetMoveScreen = { Enter, Update, Build, NULL };

// =====================================================================================
// MOVE MANIAC / MOVE RELEARNER (learn_move.c), hosted.

enum { RL_LIST, RL_ASK_TEACH, RL_ASK_DELETE, RL_ASK_STOP, RL_ASK_GIVE_UP, RL_RESULT };
#define RL_ROWS 5
#define RL_MAX_MOVES 25
static uint8_t sRlPartySlot;
static uint16_t sRlMoves[RL_MAX_MOVES];
static int32_t sRlCount, sRlState;
static UiList sRlList;
static UiMenu sRlAsk;
static ForgetMove sRlInfo;
static char sRlText[160];
static bool sRlBack;
static uint8_t sRlSlot;

static void RlExpand(const u8 *text, uint16_t move, uint16_t forgotten)
{
    GetMonData(sMon, MON_DATA_NICKNAME, gStringVar1);
    StringGet_Nickname(gStringVar1);
    StringCopy(gStringVar2, gMoveNames[UiGame_SafeMove(move)]);
    StringCopy(gStringVar3, gMoveNames[UiGame_SafeMove(forgotten)]);
    StringExpandPlaceholders(gStringVar4, text);
    UiGame_Text(gStringVar4, sRlText, sizeof(sRlText));
    for (char *c = sRlText; *c; c++)
        if (*c == '\n') *c = ' ';
}

static uint16_t RlMove(void)
{
    return sRlList.sel < sRlCount ? sRlMoves[sRlList.sel] : MOVE_NONE;
}

static void RlAsk(int32_t state, const char *title)
{
    sRlState = state;
    UiMenu_Open(&sRlAsk, title, 676, 150, 252);
    UiMenu_Add(&sRlAsk, UI_TR("YES"), 1);
    UiMenu_Add(&sRlAsk, UI_TR("NO"), 0);
}

static void RlExit(void)
{
    MoveRelearner_ExitForUi();
}

static void CB2_RelearnerAfterForget(void);
static void RlStartForget(void)
{
    gSpecialVar_0x8004 = sRlPartySlot;
    ShowSelectMovePokemonSummaryScreen(gPlayerParty, sRlPartySlot, gPlayerPartyCount - 1,
                                       CB2_RelearnerAfterForget, RlMove());
}

static void RlEnter(void)
{
    gSpecialVar_0x8004 = sRlPartySlot;
    sMon = &gPlayerParty[sRlPartySlot];
    sRlCount = MoveRelearner_MovesForUi(sRlMoves);
    sRlAsk.open = false;
    if (!sRlBack)
        UiList_Reset(&sRlList, sRlCount + 1, RL_ROWS);
    sRlState = RL_LIST;
    RlExpand(gText_TeachWhichMoveToMon, MOVE_NONE, MOVE_NONE);
    if (sRlBack)
    {
        sRlBack = false;
        const uint16_t move = RlMove();
        if (sRlSlot < MAX_MON_MOVES)
        {
            const uint16_t old = (uint16_t)GetMonData(sMon, MON_DATA_MOVE1 + sRlSlot);
            gSpecialVar_0x8004 = sRlPartySlot;
            MoveRelearner_ReplaceForUi(sRlSlot, move);
            RlExpand(gText_MonForgotOldMoveAndMonLearnedNewMove, move, old);
            PlayFanfare(MUS_LEVEL_UP);
            sRlState = RL_RESULT;
        }
        else
        {
            RlExpand(gText_StopLearningMove, move, MOVE_NONE);
            RlAsk(RL_ASK_STOP, UI_TR("STOP TEACHING?"));
        }
    }
}

static bool RlResultReady(void)
{
    return IsFanfareTaskInactive();
}

static bool RlUpdate(const UiInput *in)
{
    if (sRlState == RL_RESULT)
    {
        if (RlResultReady() && ((in->pressed & (VITA_BTN_CROSS | VITA_BTN_CIRCLE)) || in->tap))
        {
            PlaySE(SE_SELECT);
            UiNav_Close(RlExit);
        }
        return true;
    }
    if (sRlAsk.open)
    {
        const int32_t id = UiMenu_Update(&sRlAsk, in);
        if (id == UI_MENU_NONE)
            return true;
        const bool yes = id == 1;
        const uint16_t move = RlMove();
        if (sRlState == RL_ASK_TEACH && yes)
        {
            gSpecialVar_0x8004 = sRlPartySlot;
            if (MoveRelearner_TeachForUi(move))
            {
                RlExpand(gText_MonLearnedMove, move, MOVE_NONE);
                PlayFanfare(MUS_LEVEL_UP);
                sRlState = RL_RESULT;
                return true;
            }
            RlExpand(gText_MonIsTryingToLearnMove, move, MOVE_NONE);
            RlAsk(RL_ASK_DELETE, UI_TR("DELETE A MOVE?"));
            return true;
        }
        if (sRlState == RL_ASK_DELETE && yes)
        {
            sRlBack = true;
            sRlSlot = MAX_MON_MOVES;
            UiNav_Close(RlStartForget);
            return true;
        }
        if (sRlState == RL_ASK_DELETE && !yes)
        {
            RlExpand(gText_StopLearningMove, move, MOVE_NONE);
            RlAsk(RL_ASK_STOP, UI_TR("STOP TEACHING?"));
            return true;
        }
        if (sRlState == RL_ASK_STOP && !yes)
        {
            RlExpand(gText_MonIsTryingToLearnMove, move, MOVE_NONE);
            RlAsk(RL_ASK_DELETE, UI_TR("DELETE A MOVE?"));
            return true;
        }
        if ((sRlState == RL_ASK_GIVE_UP || sRlState == RL_ASK_STOP) && yes)
        {
            MoveRelearner_GiveUpForUi();
            UiNav_Close(RlExit);
            return true;
        }
        sRlState = RL_LIST;
        RlExpand(gText_TeachWhichMoveToMon, MOVE_NONE, MOVE_NONE);
        return true;
    }
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        RlExpand(gText_GiveUpTryingToTeachNewMove, MOVE_NONE, MOVE_NONE);
        RlAsk(RL_ASK_GIVE_UP, UI_TR("GIVE UP?"));
        return true;
    }
    const int32_t before = sRlList.sel;
    const int32_t a = UiList_Update(&sRlList, in, ROW_X, ROW_Y, ROW_W, ROW_H, false);
    if (sRlList.sel != before)
        PlaySE(SE_SELECT);
    if (a < 0)
        return true;
    PlaySE(SE_SELECT);
    if (a >= sRlCount)
    {
        RlExpand(gText_GiveUpTryingToTeachNewMove, MOVE_NONE, MOVE_NONE);
        RlAsk(RL_ASK_GIVE_UP, UI_TR("GIVE UP?"));
        return true;
    }
    char title[24], mv[16];
    UiGame_Text(gMoveNames[UiGame_SafeMove(RlMove())], mv, sizeof(mv));
    snprintf(title, sizeof(title), UI_TR("TEACH %s?"), mv);
    RlExpand(gText_TeachMoveQues, RlMove(), MOVE_NONE);
    RlAsk(RL_ASK_TEACH, title);
    return true;
}

static void RlBuild(void)
{
    char name[24];
    {
        u8 nick[POKEMON_NAME_LENGTH + 1];
        GetMonData(sMon, MON_DATA_NICKNAME, nick);
        UiGame_Text(nick, name, sizeof(name));
    }
    UiKit_HeaderAs(UI_GBA_BG_SUMMARY, UI_SPR_BALL_POKE, UI_TR("MOVE MANIAC"), NULL, 0, 0, NULL, NULL, 0);
    UiW_HeaderValue(name, UI_COL_ON_FRAME);
    DrawMonPanel(name);
    UiW_TextWrapped(30, 352, 272, 30, sRlText, UI_COL_INK, 4);

    UiW_Panel(332, 60, 612, 284);
    UiW_PanelHead(332, 60, 612, UI_TR("MOVES"), UI_TR("PP"));
    UiDraw_PushClip(ROW_X, ROW_Y, ROW_W, RL_ROWS * ROW_H);
    for (int32_t r = 0; r < RL_ROWS && sRlList.first + r < sRlCount + 1; r++)
    {
        const int32_t i = sRlList.first + r, y = ROW_Y + r * ROW_H;
        const bool on = i == sRlList.sel;
        if (i == sRlCount)
        {
            UiW_Row(ROW_X, y, ROW_W, ROW_H, on, i);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, ROW_X + 104, y + 4, UI_TR("CANCEL"), on ? UI_COL_SELECT_INK : UI_COL_INK_SOFT);
            continue;
        }
        ForgetMove m;
        Fill(&m, sRlMoves[i], -1);
        DrawMoveRow(&m, y, on, i, false);
    }
    UiDraw_PopClip();
    UiW_ScrollBar(ROW_X + ROW_W - 9, ROW_Y, RL_ROWS * ROW_H, sRlList.first, RL_ROWS, sRlCount + 1);

    Fill(&sRlInfo, RlMove(), -1);
    DrawCompare(332, 612, UI_TR("BATTLE"), UI_RGBA(181, 53, 43, 255), &sRlInfo);

    int32_t fx = 16;
    if (sRlState != RL_RESULT)
    {
        UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Teach"));
        UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Give up"));
    }
    else if (RlResultReady())
        UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("OK"));
    UiKit_FooterClock();
    if (sRlAsk.open)
        UiMenu_Draw(&sRlAsk);
    if (sRlState == RL_RESULT)
    {
        UiDraw_Rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, UI_COL_DIM);
        UiW_Window(200, 200, 560, 144, UI_TR("MOVE MANIAC"));
        UiW_TextWrapped(224, 250, 512, 30, sRlText, UI_COL_INK, 3);
    }
}

static const UiScreen sRelearnerScreen = { RlEnter, RlUpdate, RlBuild, NULL };

bool Ui_OpenMoveRelearner(void)
{
    if (!Ui_IsEnabled() || Ui_IsOpen() || gSpecialVar_0x8004 >= PARTY_SIZE)
        return false;
    sRlPartySlot = (uint8_t)gSpecialVar_0x8004;
    sRlBack = false;
    UiCore_OpenHosted(&sRelearnerScreen, NULL);
    return true;
}

static void CB2_RelearnerAfterForget(void)
{
    sRlSlot = GetMoveSlotToReplace();
    UiCore_OpenHosted(&sRelearnerScreen, NULL);
}
