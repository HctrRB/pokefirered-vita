// BAG of the 960x544 native UI (1:1 port of pokeemerald-vita-3ds adapted to FireRed v1.1):
// 5 FireRed pockets (Items, Key Items, Poké Balls, TMs & HMs, Berries), 4-frame bag art with
// TM Case / Berry Pouch overlay, sorting, GIVE mode from Party, and Battle Bag mode.

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "platform/vita_extras.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "item.h"
#include "mail_data.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/items.h"
#include "constants/songs.h"

#define LIST_X     340
#define LIST_W     596
#define LIST_Y     106
#define ROW_H      40
#define ROWS       9
#define GBA_LIST_X 358
#define GBA_LIST_W 562
#define GBA_LIST_Y 108
#define GBA_ROW_H  38
#define GBA_ROWS   8
#define MAX_ROWS   64

enum { ACT_USE = 0, ACT_CHECK, ACT_GIVE, ACT_TOSS, ACT_REGISTER, ACT_CANCEL, ACT_COUNT };
static const char *const sActNames[ACT_COUNT] = {
    UI_N("USE"), UI_N("CHECK"), UI_N("GIVE"), UI_N("TOSS"), UI_N("REGISTER"), UI_N("CANCEL")
};
static const char *const sPocketTabs[UI_POCKET_COUNT] = {
    UI_N("ITEMS"), UI_N("KEY ITEMS"), UI_N("POKé BALLS"), UI_N("TMs & HMs"), UI_N("BERRIES")
};
static const char *const sPocketShort[UI_POCKET_COUNT] = {
    UI_N("ITEMS"), UI_N("KEY"), UI_N("BALLS"), UI_N("TMs"), UI_N("BERRIES")
};
static const char *const sPocketCat[UI_POCKET_COUNT] = {
    UI_N("ITEM"), UI_N("KEY ITEM"), UI_N("BALL"), UI_N("TM / HM"), UI_N("BERRY")
};
static const char *const sSortNames[UI_SORT_COUNT] = {
    UI_N("NUMBER"), UI_N("A TO Z"), UI_N("MOST"), UI_N("LEAST")
};

static int32_t sPocket = UI_POCKET_ITEMS;
static UiList sList[UI_POCKET_COUNT];
static UiBagRow sRows[MAX_ROWS];
static int32_t sCount;
static int32_t sGiveTo = -1;
static int32_t sSort;
static UiMenu sMenu;
static UiQty sQty;
static int32_t sTabX[UI_POCKET_COUNT], sTabW[UI_POCKET_COUNT];
static int32_t sActs[ACT_COUNT], sActCount, sActX[ACT_COUNT], sActW;

static int32_t BagVisRows(void)
{
    return gUiTheme->gbaFrame ? GBA_ROWS : ROWS;
}

void UiBag_SetGiveTo(const int32_t partySlot)
{
    sGiveTo = partySlot;
}

static void Refresh(void)
{
    sCount = UiGame_PocketItems(sPocket, sRows, MAX_ROWS);
    UiList_Reset(&sList[sPocket], sCount + 1, BagVisRows());
}

static uint16_t CurItem(void)
{
    const int32_t i = sList[sPocket].sel;
    return i < sCount ? sRows[i].item : ITEM_NONE;
}

static bool IsHmItem(const uint16_t item)
{
    return item >= ITEM_HM01 && item <= ITEM_HM08;
}

static void FormatRowLabel(const uint16_t item, char *const out, const size_t cap)
{
    char itemName[24];
    UiGame_ItemName(item, itemName, sizeof(itemName));
    if (sPocket == UI_POCKET_TM_CASE)
    {
        char moveName[24];
        UiGame_MoveName(UiGame_ItemMove(item), moveName, sizeof(moveName));
        snprintf(out, cap, "%s %s", itemName, moveName);
    }
    else
    {
        snprintf(out, cap, "%s", itemName);
    }
}

static void FormatQtyLabel(const uint16_t item, const uint16_t qty, char *const out, const size_t cap)
{
    if (sPocket == UI_POCKET_KEY_ITEMS)
    {
        out[0] = '\0';
        return;
    }
    if (sPocket == UI_POCKET_TM_CASE)
    {
        if (IsHmItem(item))
            snprintf(out, cap, UI_TR("HM"));
        else if (VitaExtras_GetFast(FAST_EXTRA_INFINITE_TMS) != 0)
            snprintf(out, cap, UI_TR("INF"));
        else
            snprintf(out, cap, "x%2u", (unsigned)qty);
        return;
    }
    snprintf(out, cap, "x%2u", (unsigned)qty);
}

static void DrawBagArt(const int32_t x, const int32_t y)
{
    // FireRed atlas stores 4 bag frames per gender: 0=closed, 1=Items, 2=Key Items, 3=Poké Balls.
    const uint16_t bagBase = UiGame_PlayerIsMale() ? UI_SPR_BAG_MALE_FIRST : UI_SPR_BAG_FEMALE_FIRST;
    if (sPocket == UI_POCKET_ITEMS)
    {
        UiDraw_Sprite((uint16_t)(bagBase + 1), x, y, 2);
    }
    else if (sPocket == UI_POCKET_KEY_ITEMS)
    {
        UiDraw_Sprite((uint16_t)(bagBase + 2), x, y, 2);
    }
    else if (sPocket == UI_POCKET_POKE_BALLS)
    {
        UiDraw_Sprite((uint16_t)(bagBase + 3), x, y, 2);
    }
    else
    {
        UiDraw_Sprite(bagBase, x, y, 2);
        UiSpr_ItemIcon(sPocket == UI_POCKET_TM_CASE ? ITEM_TM_CASE : ITEM_BERRY_POUCH, x + 76, y + 76, 2);
    }
}

static void AddAct(const int32_t act)
{
    if (sActCount < ACT_COUNT)
        sActs[sActCount++] = act;
}

static void BuildActs(const uint16_t item)
{
    sActCount = 0;
    if (item == ITEM_NONE)
        return;
    if (UiGame_InBattleMenu())
    {
        if (UiGame_BattleItemUsable(item))
            AddAct(ACT_USE);
        return;
    }
    if (sGiveTo >= 0)
    {
        if (UiGame_CanGive(item))
            AddAct(ACT_GIVE);
        return;
    }
    if (sPocket != UI_POCKET_POKE_BALLS)
        AddAct(ItemIsMail(item) ? ACT_CHECK : ACT_USE);
    if (UiGame_CanGive(item))
        AddAct(ACT_GIVE);
    if (UiGame_CanToss(item))
        AddAct(ACT_TOSS);
    if (UiGame_CanRegister(item))
        AddAct(ACT_REGISTER);
}

static void Enter(void)
{
    sPocket = UI_POCKET_ITEMS;
    sMenu.open = false;
    sQty.open = false;
    Refresh();
}

static void Resume(void)
{
    Refresh();
}

static void DoAction(const int32_t act)
{
    const uint16_t item = CurItem();
    if (item == ITEM_NONE)
        return;
    char name[24], msg[96];
    UiGame_ItemName(item, name, sizeof(name));

    switch (act)
    {
    case ACT_USE:
    {
        if (!UiGame_InBattleMenu() && item == ITEM_TM_CASE)
        {
            sPocket = UI_POCKET_TM_CASE;
            PlaySE(SE_SELECT);
            Refresh();
            break;
        }
        if (item == ITEM_BERRY_POUCH)
        {
            sPocket = UI_POCKET_BERRY_POUCH;
            PlaySE(SE_SELECT);
            Refresh();
            break;
        }
        if (item == ITEM_TOWN_MAP && !UiGame_InBattleMenu())
        {
            PlaySE(SE_SELECT);
            UiMap_OpenFromBag();
            break;
        }
        if (UiGame_InBattleMenu())
        {
            const UiBattleUse b = UiGame_BattleUse(item, msg, sizeof(msg));
            if (b == UI_BUSE_CLOSE)
                UiNav_Close(NULL);
            else if (b == UI_BUSE_TARGET)
            {
                UiTarget_Set(UI_TARGET_BATTLE, item);
                UiNav_Push(&gUiTargetScreen);
            }
            else
            {
                PlaySE(SE_FAILURE);
                UiKit_Toast("%s", msg);
            }
            break;
        }
        void (*after)(void) = NULL;
        const UiUseResult r = UiGame_UseFromBag(item, msg, sizeof(msg), &after);
        if (r == UI_USE_CLOSE)
        {
            UiNav_Close(after);
        }
        else if (r == UI_USE_TARGET)
        {
            UiTarget_Set(UiGame_ItemIsTmHm(item) ? UI_TARGET_TM : UI_TARGET_USE, item);
            UiNav_Push(&gUiTargetScreen);
        }
        else
        {
            PlaySE(r == UI_USE_DONE ? SE_USE_ITEM : SE_FAILURE);
            UiKit_Toast("%s", msg);
            Refresh();
        }
        break;
    }
    case ACT_CHECK:
    {
        struct Mail blank;
        memset(&blank, 0, sizeof(blank));
        for (int32_t i = 0; i < MAIL_WORDS_COUNT; i++)
            blank.words[i] = 0xFFFF;
        blank.itemId = item;
        PlaySE(SE_SELECT);
        UiMailRead_Open(&blank, UI_TR("BAG"));
        break;
    }
    case ACT_GIVE:
        if (sGiveTo >= 0)
        {
            UiMon m;
            UiGame_Mon(sGiveTo, &m);
            if (ItemIsMail(item))
            {
                if (m.item != ITEM_NONE)
                {
                    PlaySE(SE_FAILURE);
                    UiKit_Toast(UI_TR("Can't transfer MAIL to a POKéMON holding an item."));
                    break;
                }
                void (*after)(void) = NULL;
                if (UiGame_GiveMailBegin(sGiveTo, item, &after))
                {
                    PlaySE(SE_SELECT);
                    sGiveTo = -1;
                    UiNav_Close(after);
                }
                else
                {
                    PlaySE(SE_FAILURE);
                    UiKit_Toast(UI_TR("No room for MAIL."));
                }
                break;
            }
            uint16_t prev = ITEM_NONE;
            if (!UiGame_GiveItem(sGiveTo, item, &prev))
            {
                PlaySE(SE_FAILURE);
                UiKit_Toast(ItemIsMail(prev)
                            ? UI_TR("Please remove the MAIL.")
                            : UI_TR("The BAG is full. The POKéMON's item couldn't be taken."));
                break;
            }
            UiGame_Mon(sGiveTo, &m);
            UiKit_Toast(UI_TR("%s was given the %s to hold."), m.name, name);
            sGiveTo = -1;
            UiNav_Pop();
        }
        else
        {
            UiTarget_Set(UI_TARGET_GIVE, item);
            UiNav_Push(&gUiTargetScreen);
        }
        break;
    case ACT_TOSS:
        UiQty_Open(&sQty, UI_TR("TOSS"), sRows[sList[sPocket].sel].qty);
        break;
    case ACT_REGISTER:
        UiGame_Register(item);
        PlaySE(SE_SELECT);
        UiKit_Toast(UiGame_RegisteredItem() == item
                    ? UI_TR("%s registered to SELECT.")
                    : UI_TR("%s is no longer registered."),
                    name);
        break;
    default:
        break;
    }
}

static void OpenMenu(void)
{
    const uint16_t item = CurItem();
    BuildActs(item);
    char name[24];
    UiGame_ItemName(item, name, sizeof(name));
    if (gUiTheme->gbaFrame)
    {
        const int32_t row = sList[sPocket].sel - sList[sPocket].first;
        int32_t my = GBA_LIST_Y + row * GBA_ROW_H;
        if (my > 270)
            my = 270;
        UiMenu_Open(&sMenu, name, 738, my, 196);
    }
    else
    {
        const int32_t row = sList[sPocket].sel - sList[sPocket].first;
        UiMenu_Open(&sMenu, name, LIST_X + LIST_W - 260, LIST_Y + row * ROW_H, 240);
    }
    for (int32_t i = 0; i < sActCount; i++)
        UiMenu_Add(&sMenu, UiLocale_Lookup(sActNames[sActs[i]]), sActs[i]);
    UiMenu_Add(&sMenu, UI_TR("CANCEL"), ACT_CANCEL);
}

static bool Update(const UiInput *const in)
{
    const bool gba = gUiTheme->gbaFrame;
    if (sQty.open)
    {
        const int32_t n = UiQty_Update(&sQty, in);
        if (n > 0)
        {
            char name[24];
            const uint16_t item = CurItem();
            UiGame_ItemName(item, name, sizeof(name));
            UiGame_Toss(item, (uint16_t)n);
            UiKit_Toast(UI_TR("Threw away %d %s."), (int)n, name);
            Refresh();
        }
        return true;
    }
    if (sMenu.open)
    {
        const int32_t id = UiMenu_Update(&sMenu, in);
        if (id >= 0 && id != ACT_CANCEL)
            DoAction(id);
        return true;
    }
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        sGiveTo = -1;
        return false;
    }

    int32_t tab = -1;
    if (gba)
    {
        if (UiHit(in, 66, 68, 44, 34))
            tab = (sPocket + UI_POCKET_COUNT - 1) % UI_POCKET_COUNT;
        else if (UiHit(in, 276, 68, 44, 34))
            tab = (sPocket + 1) % UI_POCKET_COUNT;
        else
        {
            for (int32_t i = 0; i < UI_POCKET_COUNT; i++)
                if (UiHit(in, 24 + i * 60, 110, 56, 28))
                    tab = i;
        }
    }
    else
    {
        tab = UiKit_TabHit(in, sTabX, sTabW, UI_POCKET_COUNT);
    }
    if (in->pressed & (VITA_BTN_L | VITA_BTN_LEFT))
        tab = (sPocket + UI_POCKET_COUNT - 1) % UI_POCKET_COUNT;
    if (in->pressed & (VITA_BTN_R | VITA_BTN_RIGHT))
        tab = (sPocket + 1) % UI_POCKET_COUNT;

    if (tab >= 0 && tab != sPocket)
    {
        sPocket = tab;
        PlaySE(SE_SELECT);
        Refresh();
        return true;
    }

    const bool sortHit = gba && UiHit(in, 178, 256, 140, 72);
    if ((in->pressed & (VITA_BTN_SELECT | VITA_BTN_TRIANGLE)) || sortHit)
    {
        sSort = (sSort + 1) % UI_SORT_COUNT;
        UiGame_SortPocket(sPocket, sSort);
        PlaySE(SE_SELECT);
        UiKit_Toast(UI_TR("POCKET SORTED: %s"), UiLocale_Lookup(sSortNames[sSort]));
        Refresh();
        return true;
    }

    if (!gba)
    {
        BuildActs(CurItem());
        if (sActCount > 0)
        {
            sActW = (272 - (sActCount - 1) * 6) / sActCount;
            for (int32_t i = 0; i < sActCount; i++)
                sActX[i] = 32 + i * (sActW + 6);
            for (int32_t i = 0; i < sActCount; i++)
            {
                if (UiHit(in, sActX[i], 440, sActW, 40))
                {
                    PlaySE(SE_SELECT);
                    DoAction(sActs[i]);
                    return true;
                }
            }
        }
    }

    sList[sPocket].rows = BagVisRows();
    const int32_t lx = gba ? GBA_LIST_X : LIST_X;
    const int32_t ly = gba ? GBA_LIST_Y : LIST_Y;
    const int32_t lw = gba ? GBA_LIST_W : LIST_W;
    const int32_t rh = gba ? GBA_ROW_H : ROW_H;
    const int32_t a = UiList_Update(&sList[sPocket], in, lx, ly, lw, rh, false);
    if (a >= 0)
    {
        PlaySE(SE_SELECT);
        if (a == sCount)
        {
            sGiveTo = -1;
            return false;
        }
        OpenMenu();
    }
    return true;
}

static void BuildGba(void)
{
    char buf[128], qtyBuf[16];
    UiW_GbaTopBar(UI_GBA_BG_BAG, UI_SPR_START_BAG,
                  UiGame_InBattleMenu() ? UI_TR("BATTLE BAG") : UI_TR("BAG"),
                  0, 0, false, NULL);

    int32_t rx = UI_SCREEN_W - 14;
    UiW_GbaHelpBadge(&rx, "O", UI_TR("CLOSE"), true);
    UiW_GbaHelpBadge(&rx, "TRI", UI_TR("SORT"), false);
    UiW_GbaHelpBadge(&rx, "X", sGiveTo >= 0 ? UI_TR("GIVE") : UI_TR("OPTIONS"), false);

    // Left column (14, 60, 316, 372): Pocket signboard
    UiW_Bevel(14, 60, 316, 86, 4, UI_RGBA(20, 26, 28, 255), UI_RGBA(40, 56, 64, 255), UI_RGBA(32, 46, 54, 255),
              2, UI_RGBA(56, 104, 136, 255), UI_RGBA(28, 52, 68, 255), 3);
    UiW_Bevel(24, 68, 34, 34, 4, UI_RGBA(40, 56, 64, 255), UI_RGBA(248, 224, 136, 255), UI_RGBA(224, 168, 48, 255),
              1, UI_RGBA(255, 246, 200, 255), UI_RGBA(192, 136, 24, 255), 0);
    UiDraw_Sprite(UI_SPR_BALL_POKE, 29, 73, 1);

    UiW_Bevel(66, 68, 254, 34, 3, UI_RGBA(40, 56, 64, 255), UI_RGBA(248, 232, 152, 255), UI_RGBA(232, 200, 104, 255),
              2, UI_RGBA(255, 253, 240, 255), UI_RGBA(208, 176, 80, 255), 2);
    UiW_GbaArrowH(75, 80, false, UI_RGBA(255, 253, 240, 255));
    UiW_GbaArrowH(74, 79, false, UI_RGBA(208, 48, 48, 255));
    UiW_GbaTextCentred(UI_FACE_VT323, 28, 86, 68, 214, 34, UiLocale_Lookup(sPocketTabs[sPocket]),
                       UI_RGBA(40, 56, 64, 255), UI_RGBA(255, 253, 240, 255));
    UiW_GbaArrowH(305, 80, true, UI_RGBA(255, 253, 240, 255));
    UiW_GbaArrowH(304, 79, true, UI_RGBA(208, 48, 48, 255));

    for (int32_t i = 0; i < UI_POCKET_COUNT; i++)
    {
        const int32_t tx = 24 + i * 60;
        if (i == sPocket)
        {
            UiW_GbaRingBox(tx, 110, 56, 28, 3, UI_RGBA(255, 90, 32, 255), UI_RGBA(255, 230, 57, 255),
                           UI_RGBA(255, 246, 205, 255), UI_RGBA(248, 224, 136, 255), C_WHITE, 0);
            UiW_GbaTextCentred(UI_FACE_VT323, 20, tx, 110, 56, 28, UiLocale_Lookup(sPocketShort[i]),
                               UI_RGBA(41, 49, 49, 255), C_WHITE);
        }
        else
        {
            UiW_GbaBox(tx, 110, 56, 28, 3, UI_RGBA(16, 21, 24, 255), UI_RGBA(27, 40, 46, 255),
                       UI_RGBA(27, 40, 46, 255), 0, 0);
            UiW_GbaTextCentred(UI_FACE_VT323, 20, tx, 110, 56, 28, UiLocale_Lookup(sPocketShort[i]),
                               UI_RGBA(156, 192, 202, 255), UI_GBA_SHAD_DARK);
        }
    }

    // Stage card + green checkerboard mat
    const int32_t stageY = 154;
    const int32_t stageH = 432 - stageY;
    UiW_GbaWin(14, stageY, 316, stageH, UI_GBA_WIN_DARK);
    const int32_t matY = stageY + 18;
    const int32_t matH = stageH - 36;
    UiW_GbaBox(26, matY, 142, matH, 4, UI_RGBA(20, 26, 28, 255), UI_RGBA(62, 146, 68, 255),
               UI_RGBA(62, 146, 68, 255), UI_RGBA(148, 246, 148, 255), 0);
    UiDraw_PushClip(29, matY + 3, 136, matH - 6);
    for (int32_t cy = 0; cy < matH; cy += 16)
        for (int32_t cx = ((cy / 16) & 1) ? 16 : 0; cx < 136; cx += 32)
            UiDraw_Rect(29 + cx, matY + 3 + cy, 16, 16, UI_RGBA(88, 186, 88, 255));
    UiDraw_PopClip();

    DrawBagArt(33, matY + (matH - 128) / 2);

    // 3 Stats cards beside mat
    const int32_t boxX = 178, boxW = 140, boxH = 74;
    UiW_GbaBox(boxX, matY, boxW, boxH, 3, UI_RGBA(20, 26, 28, 255), C_WHITE, C_WHITE, UI_GBA_SHAD_LIGHT, 2);
    UiW_GbaText(UI_FACE_VT323, 24, boxX + 10, matY + 8, UI_TR("MONEY"), UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
    snprintf(buf, sizeof(buf), "$%u", (unsigned)UiGame_Money());
    UiW_GbaText(UI_FACE_VT323, 28, boxX + 10, matY + 34, buf, UI_RGBA(41, 49, 49, 255), UI_GBA_SHAD_LIGHT);

    const int32_t b2Y = matY + 84;
    UiW_GbaBox(boxX, b2Y, boxW, boxH, 3, UI_RGBA(20, 26, 28, 255), C_WHITE, C_WHITE, UI_GBA_SHAD_LIGHT, 2);
    UiW_GbaText(UI_FACE_VT323, 24, boxX + 10, b2Y + 8, UI_TR("SORT BY"), UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
    UiW_GbaText(UI_FACE_VT323, 28, boxX + 10, b2Y + 34, UiLocale_Lookup(sSortNames[sSort]), UI_RGBA(32, 156, 8, 255), UI_GBA_SHAD_LIGHT);

    const int32_t b3Y = b2Y + 84;
    UiW_GbaBox(boxX, b3Y, boxW, boxH, 3, UI_RGBA(20, 26, 28, 255), C_WHITE, C_WHITE, UI_GBA_SHAD_LIGHT, 2);
    if (sGiveTo >= 0)
    {
        UiMon gm;
        UiGame_Mon(sGiveTo, &gm);
        UiW_GbaText(UI_FACE_VT323, 24, boxX + 10, b3Y + 8, UI_TR("GIVE TO"), UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
        UiDraw_PushClip(boxX + 8, b3Y + 30, boxW - 14, 38);
        UiW_GbaText(UI_FACE_VT323, 28, boxX + 10, b3Y + 34, gm.name, UI_RGBA(32, 104, 168, 255), UI_GBA_SHAD_LIGHT);
        UiDraw_PopClip();
    }
    else
    {
        UiW_GbaText(UI_FACE_VT323, 24, boxX + 10, b3Y + 8, UI_TR("IN POCKET"), UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
        snprintf(buf, sizeof(buf), UI_TR("%d KINDS"), (int)sCount);
        UiW_GbaText(UI_FACE_VT323, 28, boxX + 10, b3Y + 34, buf, UI_RGBA(41, 49, 49, 255), UI_GBA_SHAD_LIGHT);
    }

    // Right gold ledger
    UiW_GbaWin(342, 60, 604, 372, UI_GBA_WIN_GOLD);
    snprintf(buf, sizeof(buf), UI_TR("%s POCKET"), UiLocale_Lookup(sPocketTabs[sPocket]));
    UiW_GbaTextMid(UI_FACE_VT323, 28, 362, 70, 32, buf, UI_RGBA(92, 50, 16, 255), UI_RGBA(255, 250, 224, 255));
    UiW_GbaTextMid(UI_FACE_VT323, 28, 868, 70, 32, UI_TR("QTY"), UI_RGBA(92, 50, 16, 255), UI_RGBA(255, 250, 224, 255));
    UiDraw_Rect(358, 103, 572, 3, UI_RGBA(200, 160, 56, 255));

    UiList *const l = &sList[sPocket];
    l->rows = GBA_ROWS;
    UiDraw_PushClip(GBA_LIST_X, GBA_LIST_Y, GBA_LIST_W, GBA_ROWS * GBA_ROW_H);
    for (int32_t r = 0; r < GBA_ROWS && l->first + r < sCount + 1; r++)
    {
        const int32_t i = l->first + r;
        const int32_t y = GBA_LIST_Y + r * GBA_ROW_H;
        const bool on = (i == l->sel);
        if (on)
        {
            UiW_GbaBox(GBA_LIST_X, y + 1, GBA_LIST_W - 6, GBA_ROW_H - 2, 4, UI_RGBA(230, 148, 65, 255),
                       UI_RGBA(255, 246, 205, 255), UI_RGBA(255, 246, 205, 255), C_WHITE, 0);
            UiW_GbaArrowH(GBA_LIST_X + 9, y + 14, true, UI_GBA_SHAD_LIGHT);
            UiW_GbaArrowH(GBA_LIST_X + 8, y + 13, true, UI_RGBA(230, 8, 8, 255));
        }
        else if (i < sCount)
        {
            for (int32_t dx = 8; dx < GBA_LIST_W - 14; dx += 10)
                UiDraw_Rect(GBA_LIST_X + dx, y + GBA_ROW_H - 2, 6, 2, UI_RGBA(226, 212, 152, 255));
        }
        if (i == sCount)
        {
            UiW_GbaTextMid(UI_FACE_VT323, 28, GBA_LIST_X + 60, y, GBA_ROW_H, UI_TR("CLOSE BAG"),
                           on ? UI_RGBA(49, 49, 49, 255) : UI_RGBA(106, 100, 84, 255), UI_GBA_SHAD_LIGHT);
            continue;
        }
        const uint16_t rowItem = sRows[i].item;
        UiSpr_ItemIcon(rowItem, GBA_LIST_X + 26, y + 7, 1);
        FormatRowLabel(rowItem, buf, sizeof(buf));
        FormatQtyLabel(rowItem, sRows[i].qty, qtyBuf, sizeof(qtyBuf));
        const int32_t qtyW = qtyBuf[0] ? UiText_Width(UI_FACE_VT323, 28, qtyBuf) : 0;
        const int32_t nameMaxW = GBA_LIST_W - 76 - (qtyW > 0 ? qtyW + 16 : 0);
        UiDraw_PushClip(GBA_LIST_X + 58, y, nameMaxW, GBA_ROW_H);
        const int32_t nw = UiW_GbaTextMid(UI_FACE_VT323, 28, GBA_LIST_X + 60, y, GBA_ROW_H, buf,
                                          UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT);
        UiDraw_PopClip();
        if (UiGame_RegisteredItem() == rowItem && nw + 68 <= nameMaxW)
            UiW_Chip(GBA_LIST_X + 70 + nw, y + 6, "SEL", true);
        if (qtyBuf[0])
            UiW_GbaTextMid(UI_FACE_VT323, 28, GBA_LIST_X + GBA_LIST_W - 16 - qtyW, y, GBA_ROW_H,
                           qtyBuf, UI_RGBA(74, 74, 82, 255), UI_GBA_SHAD_LIGHT);
    }
    UiDraw_PopClip();
    UiW_ScrollBar(924, GBA_LIST_Y, GBA_ROWS * GBA_ROW_H, l->first, GBA_ROWS, sCount + 1);

    // Bottom GBA footer description
    UiW_GbaMsg(14, 438, 932, 98, false, true);
    UiW_GbaBox(38, 454, 66, 66, 4, UI_RGBA(40, 56, 64, 255), C_WHITE, C_WHITE, UI_GBA_SHAD_LIGHT, 0);
    const uint16_t item = CurItem();
    if (item == ITEM_NONE)
    {
        UiDraw_Sprite(UI_SPR_BALL_POKE, 59, 475, 1);
        UiW_GbaTextMid(UI_FACE_VT323, 28, 118, 444, 86,
                       sCount ? UI_TR("Close the BAG and return to the game.") : UI_TR("This pocket is currently empty."),
                       UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
    }
    else
    {
        UiSpr_ItemIcon(item, 47, 463, 2);
        FormatRowLabel(item, buf, sizeof(buf));
        const int32_t tw = UiW_GbaText(UI_FACE_PS2P, 16, 118, 452, buf, UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT);
        FormatQtyLabel(item, sRows[l->sel].qty, qtyBuf, sizeof(qtyBuf));
        if (qtyBuf[0])
        {
            snprintf(buf, sizeof(buf), UI_TR("IN BAG: %s"), qtyBuf);
            UiW_GbaText(UI_FACE_PS2P, 16, 134 + tw, 452, buf, UI_RGBA(32, 156, 8, 255), UI_GBA_SHAD_LIGHT);
        }
        UiGame_ItemDescription(item, buf, sizeof(buf));
        UiW_GbaTextWrapped(UI_FACE_VT323, 28, 118, 476, 786, 26, buf, UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT, 2);
    }
    if (sMenu.open)
        UiMenu_Draw(&sMenu);
    if (sQty.open)
        UiQty_Draw(&sQty, item);
}

static void Build(void)
{
    if (gUiTheme->gbaFrame)
    {
        BuildGba();
        return;
    }

    UiKit_Header(UI_TR("BAG"), sPocketTabs, UI_POCKET_COUNT, sPocket, sTabX, sTabW, 8);
    UiKit_HeaderMoney();
    if (sGiveTo >= 0)
    {
        UiMon m;
        UiGame_Mon(sGiveTo, &m);
        char gbuf[40];
        snprintf(gbuf, sizeof(gbuf), UI_TR("GIVE TO %s"), m.name);
        const int32_t w = UiText_Width(UI_FACE_VT323, UI_BODY_PX, gbuf) + 24;
        UiW_Capsule(UI_SCREEN_W - 180 - w, 8, w, 32);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, UI_SCREEN_W - 180 - w + 12, 8, gbuf, UI_COL_ON_FRAME);
    }

    // Bag art and pocket summary
    UiW_PanelDark(16, 60, 304, 136);
    DrawBagArt(22, 64);
    UiText_Draw(UI_FACE_PS2P, 16, 160, 100, UiLocale_Lookup(sPocketTabs[sPocket]), UI_COL_TITLE);
    char buf[128], qtyBuf[16];
    snprintf(buf, sizeof(buf), UI_TR("%d kinds"), (int)sCount);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 160, 122, buf, UI_COL_ON_SOFT);

    // Selected item card
    UiW_Panel(16, 208, 304, 284);
    const uint16_t item = CurItem();
    if (item == ITEM_NONE)
    {
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 32, 224,
                    sCount ? UI_TR("Close the BAG.") : UI_TR("This pocket is empty."), UI_COL_INK_SOFT);
    }
    else
    {
        UiW_Inset(32, 222, 56, 56);
        UiSpr_ItemIcon(item, 36, 226, 2);
        char name[24];
        UiGame_ItemName(item, name, sizeof(name));
        UiText_Draw(UI_FACE_PS2P, 16, 100, 228, name, UI_COL_INK);
        const int32_t cw = UiW_Chip(100, 252, UiLocale_Lookup(sPocketCat[sPocket]), false);
        FormatQtyLabel(item, sRows[sList[sPocket].sel].qty, qtyBuf, sizeof(qtyBuf));
        if (qtyBuf[0])
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 100 + cw + 10, 248, qtyBuf, UI_COL_INK);
        if (sPocket == UI_POCKET_TM_CASE)
        {
            char mv[24];
            UiGame_MoveName(UiGame_ItemMove(item), mv, sizeof(mv));
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 32, 282, mv, UI_COL_GOLD);
        }
        UiGame_ItemDescription(item, buf, sizeof(buf));
        UiW_TextWrapped(32, sPocket == UI_POCKET_TM_CASE ? 314 : 288, 272, 30, buf, UI_COL_INK,
                        sPocket == UI_POCKET_TM_CASE ? 3 : 4);
        BuildActs(item);
        if (sActCount > 0)
        {
            static const uint8_t colours[ACT_COUNT] = {
                UI_BTN_GREEN, UI_BTN_AMBER, UI_BTN_RED, UI_BTN_AMBER, UI_BTN_GREEN, UI_BTN_AMBER
            };
            sActW = (272 - (sActCount - 1) * 6) / sActCount;
            for (int32_t i = 0; i < sActCount; i++)
            {
                sActX[i] = 32 + i * (sActW + 6);
                const char *const label = (sActs[i] == ACT_REGISTER && UiGame_RegisteredItem() == item)
                                            ? UI_TR("UNREG.")
                                            : UiLocale_Lookup(sActNames[sActs[i]]);
                UiW_Button(sActX[i], 440, sActW, label, colours[sActs[i]], false);
            }
        }
    }

    // List
    UiW_Panel(332, 60, 612, 432);
    snprintf(buf, sizeof(buf), UI_TR("%s POCKET"), UiLocale_Lookup(sPocketTabs[sPocket]));
    char sortBuf[24];
    snprintf(sortBuf, sizeof(sortBuf), UI_TR("Sort: %s"), UiLocale_Lookup(sSortNames[sSort]));
    UiW_PanelHead(332, 60, 612, buf, sortBuf);
    UiList *const l = &sList[sPocket];
    l->rows = ROWS;
    UiDraw_PushClip(LIST_X, LIST_Y, LIST_W, ROWS * ROW_H);
    for (int32_t r = 0; r < ROWS && l->first + r < sCount + 1; r++)
    {
        const int32_t i = l->first + r, y = LIST_Y + r * ROW_H;
        const bool on = i == l->sel;
        const uint32_t ink = on ? UI_COL_SELECT_INK : UI_COL_INK;
        UiW_Row(LIST_X, y, LIST_W - 12, ROW_H, on, i);
        if (i == sCount)
        {
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 60, y + 4, UI_TR("CLOSE BAG"),
                        on ? ink : UI_COL_INK_SOFT);
            continue;
        }
        const uint16_t rowItem = sRows[i].item;
        UiSpr_ItemIcon(rowItem, LIST_X + 28, y + 8, 1);
        FormatRowLabel(rowItem, buf, sizeof(buf));
        FormatQtyLabel(rowItem, sRows[i].qty, qtyBuf, sizeof(qtyBuf));
        const int32_t qtyW = qtyBuf[0] ? UiText_Width(UI_FACE_VT323, UI_BODY_PX, qtyBuf) : 0;
        const int32_t nameMaxW = LIST_W - 88 - (qtyW > 0 ? qtyW + 16 : 0);
        UiDraw_PushClip(LIST_X + 58, y, nameMaxW, ROW_H);
        const int32_t nw = UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 60, y + 4, buf, ink);
        UiDraw_PopClip();
        if (UiGame_RegisteredItem() == rowItem && nw + 70 <= nameMaxW)
            UiW_Chip(LIST_X + 72 + nw, y + 8, "SEL", true);
        if (qtyBuf[0])
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + LIST_W - 24 - qtyW, y + 4, qtyBuf,
                        on ? ink : UI_COL_INK_SOFT);
    }
    UiDraw_PopClip();
    UiW_ScrollBar(LIST_X + LIST_W - 9, LIST_Y, ROWS * ROW_H, l->first, ROWS, sCount + 1);

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Options"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    UiW_FooterKey(&fx, "SELECT", UI_TR("Sort"));
    UiKit_FooterClock();
    if (sMenu.open)
        UiMenu_Draw(&sMenu);
    if (sQty.open)
        UiQty_Draw(&sQty, CurItem());
}

const UiScreen gUiBagScreen = { Enter, Update, Build, Resume };
