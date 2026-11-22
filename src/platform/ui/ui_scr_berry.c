// Berry choice of Berry Crush (1:1 port of pokeemerald-vita-3ds ui_scr_berry.c for FireRed):
// hosted over the screen InitBerryPouch(BERRYPOUCH_FROMBERRYCRUSH, ...) fades from; sets
// gSpecialVar_ItemId and switches to the machine's savedCallback when a berry is chosen.

#include <stdio.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "item_menu.h"
#include "main.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/songs.h"

#define LIST_X 28
#define LIST_Y 118
#define LIST_W 352
#define ROW_H  40
#define ROWS   9

static void (*sExit)(void);
static UiBagRow sRows[64];
static int32_t sCount;
static UiList sList;

static void Finish(void)
{
    if (sExit != NULL)
        SetMainCallback2(sExit);
}

bool Ui_OpenBerryChoice(void (*const exitCallback)(void))
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    sCount = UiGame_PocketItems(UI_POCKET_BERRY_POUCH, sRows, 64);
    if (sCount == 0)
        return false;
    sExit = exitCallback;
    UiCore_OpenHosted(&gUiBerryScreen, NULL);
    return true;
}

static void Enter(void)
{
    UiList_Reset(&sList, sCount, ROWS);
}

static bool Update(const UiInput *const in)
{
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_FAILURE);
        UiKit_Toast(UI_TR("Choose a BERRY to put in."));
        return true;
    }
    const int32_t a = UiList_Update(&sList, in, LIST_X, LIST_Y, LIST_W, ROW_H, true);
    if (a < 0 && !UiHit(in, 420, 436, 240, 40))
        return true;
    const int32_t i = a >= 0 ? a : sList.sel;
    PlaySE(SE_SELECT);
    gSpecialVar_ItemId = sRows[i].item;
    UiNav_Close(Finish);
    return true;
}

static void Build(void)
{
    static const char *const tab[1] = { UI_N("CHOOSE A BERRY") };
    int32_t tx[1], tw[1];
    char buf[64];
    UiKit_HeaderAs(UI_GBA_BG_BAG, UI_SPR_START_BAG, UI_TR("BERRIES"), tab, 1, 0, tx, tw, 12);
    snprintf(buf, sizeof(buf), UI_TR("%d kinds"), (int)sCount);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);

    UiW_Panel(16, 60, 380, 432);
    UiW_PanelHead(16, 60, 380, UI_TR("BERRIES"), NULL);
    for (int32_t r = 0; r < ROWS && sList.first + r < sCount; r++)
    {
        const int32_t i = sList.first + r, y = LIST_Y + r * ROW_H;
        const bool on = i == sList.sel;
        UiW_Row(LIST_X, y, LIST_W, ROW_H, on, i);
        UiSpr_ItemIcon(sRows[i].item, LIST_X + 30, y + 8, 1);
        UiGame_ItemName(sRows[i].item, buf, sizeof(buf));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 64, y + 2, buf, on ? UI_COL_SELECT_INK : UI_COL_INK);
        snprintf(buf, sizeof(buf), "x%u", (unsigned)sRows[i].qty);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + LIST_W - 12 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf),
                    y + 2, buf, on ? UI_COL_SELECT_INK : UI_COL_INK_SOFT);
    }
    UiW_ScrollBar(LIST_X + LIST_W + 2, LIST_Y, ROWS * ROW_H, sList.first, ROWS, sCount);

    UiW_Panel(408, 60, 536, 432);
    const uint16_t item = sRows[sList.sel].item;
    UiW_Inset(428, 80, 112, 112);
    UiSpr_ItemIcon(item, 448, 100, 3);
    UiGame_ItemName(item, buf, sizeof(buf));
    UiText_Draw(UI_FACE_PS2P, 16, 560, 96, buf, UI_COL_INK);
    snprintf(buf, sizeof(buf), UI_TR("In bag: x%u"), (unsigned)sRows[sList.sel].qty);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 560, 124, buf, UI_COL_INK_SOFT);
    char desc[160];
    UiGame_ItemDescription(item, desc, sizeof(desc));
    UiW_Inset(428, 208, 496, 140);
    UiW_TextWrapped(444, 216, 468, 30, desc, UI_COL_INK, 4);
    UiW_TextWrapped(428, 364, 496, 30,
                    UI_TR("The BERRY goes into the machine; it is taken from the BAG when the crush starts."),
                    UI_COL_INK_SOFT, 2);
    UiW_Button(420, 436, 240, UI_TR("PUT IN"), UI_BTN_GREEN, false);

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Put in"));
    UiKit_FooterClock();
}

const UiScreen gUiBerryScreen = { Enter, Update, Build, NULL };
