// The player's PC of the native 960x544 UI (Ui_OpenPlayerPc, gUiPlayerPcScreen): ITEM STORAGE and
// MAILBOX of the bedroom and Pokémon Center PCs (player_pc.c), plus the Mail Reader screen
// (gUiMailReadScreen, UiMailRead_Open, UiMail_Draw). Ported 1:1 from pokeemerald-vita-3ds and
// adapted to FireRed v1.1 (2 tabs: ITEMS and MAILBOX; 5 FireRed Bag pockets for Deposit).

#include <stdio.h>
#include <string.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "platform/mail_ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "characters.h"
#include "event_data.h"
#include "item.h"
#include "item_menu.h"
#include "mail.h"
#include "mail_data.h"
#include "main.h"
#include "pokemon.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#pragma GCC diagnostic pop
#include "constants/items.h"
#include "constants/songs.h"

extern void PlayerPC_ReturnFromUi(u8 taskId);

enum { TAB_ITEMS = UI_PC_TAB_ITEMS, TAB_MAIL = UI_PC_TAB_MAIL, TAB_COUNT = 2 };
enum { MODE_WITHDRAW, MODE_DEPOSIT };
enum { ASK_NONE, ASK_WITHDRAW, ASK_TOSS, ASK_TOSS_CONFIRM, ASK_DEPOSIT, ASK_MAIL_TO_BAG, ASK_MAIL_GIVE };

#define LIST_X 344
#define LIST_Y 154
#define LIST_W 588
#define ROW_H 40
#define ROWS 8

static const char *const sTabNames[TAB_COUNT] = { UI_N("ITEMS"), UI_N("MAILBOX") };
static const char *const sPocketNames[UI_POCKET_COUNT] = {
    UI_N("ITEMS"), UI_N("KEY"), UI_N("BALLS"), UI_N("TMs"), UI_N("BERRIES")
};
static const uint8_t sPocketIds[UI_POCKET_COUNT] = {
    UI_POCKET_ITEMS, UI_POCKET_KEY_ITEMS, UI_POCKET_POKE_BALLS, UI_POCKET_TM_CASE, UI_POCKET_BERRY_POUCH
};

static uint8_t sTaskId, sKind;
static int32_t sTab, sMode, sPocket, sTabX[TAB_COUNT], sTabW[TAB_COUNT];
static const char *sTabs[TAB_COUNT];
static int32_t sTabIds[TAB_COUNT], sTabCount;
static int32_t sModeX[2], sModeW[2];
static int32_t sPocketX[UI_POCKET_COUNT], sPocketW[UI_POCKET_COUNT];
static UiList sList;
static UiBagRow sBag[64];
static int32_t sCount;
static int32_t sMoveFrom = -1;
static int32_t sAsk;
static UiQty sQty;
static UiMenu sMenu;
static int32_t sQuantity;
static uint8_t sMailSlots[MAIL_COUNT];
static uint32_t sFrame;
static char sQuestion[128];

static void AfterClose(void)
{
    PlayerPC_ReturnFromUi(sTaskId);
}

static int32_t MailCount(void)
{
    int32_t n = 0;
    for (int32_t i = PARTY_SIZE; i < MAIL_COUNT; i++)
        if (gSaveBlock1Ptr->mail[i].itemId != ITEM_NONE)
            sMailSlots[n++] = (uint8_t)i;
    return n;
}

static void CompactMail(void)
{
    struct Mail temp;
    for (int32_t i = PARTY_SIZE; i < MAIL_COUNT - 1; i++)
        for (int32_t j = i + 1; j < MAIL_COUNT; j++)
            if (gSaveBlock1Ptr->mail[i].itemId == ITEM_NONE)
                SWAP(gSaveBlock1Ptr->mail[i], gSaveBlock1Ptr->mail[j], temp);
}

static void Refresh(void)
{
    const int32_t sel = sList.sel;
    if (sTab == TAB_ITEMS && sMode == MODE_WITHDRAW)
        sCount = CountItemsInPC();
    else if (sTab == TAB_ITEMS)
        sCount = UiGame_PocketItems(sPocketIds[sPocket], sBag, 64);
    else
        sCount = MailCount();
    UiList_Reset(&sList, sCount, ROWS);
    if (sel < sCount) sList.sel = sel;
    else if (sCount > 0) sList.sel = sCount - 1;
    if (sList.sel < sList.first) sList.first = sList.sel;
    if (sList.sel >= sList.first + ROWS) sList.first = sList.sel - ROWS + 1;
}

bool Ui_OpenPlayerPc(uint8_t taskId, uint8_t tab, uint8_t kind)
{
    if (!Ui_IsEnabled() || Ui_IsOpen() || tab >= TAB_COUNT)
        return false;
    sTabCount = 2;
    sTabIds[0] = TAB_ITEMS;
    sTabIds[1] = TAB_MAIL;
    sTabs[0] = UiLocale_Lookup(sTabNames[0]);
    sTabs[1] = UiLocale_Lookup(sTabNames[1]);
    sTaskId = taskId;
    sTab = tab;
    sKind = kind;
    UiCore_OpenFromScript(&gUiPlayerPcScreen, AfterClose);
    return true;
}

static int32_t TabSlot(int32_t tab)
{
    for (int32_t i = 0; i < sTabCount; i++)
        if (sTabIds[i] == tab)
            return i;
    return 0;
}

static void ToastText(const u8 *gameText)
{
    char msg[160];
    StringExpandPlaceholders(gStringVar4, gameText);
    UiGame_Text(gStringVar4, msg, sizeof(msg));
    for (char *p = msg; *p; p++)
        if (*p == '\n') *p = ' ';
    UiKit_Toast("%s", msg);
}

static void Enter(void)
{
    sMode = MODE_WITHDRAW;
    sPocket = 0;
    sMoveFrom = -1;
    sAsk = ASK_NONE;
    sQty.open = false;
    sMenu.open = false;
    sList.sel = sList.first = 0;
    ItemPcCompaction();
    CompactMail();
    Refresh();
}

static uint16_t CurItem(void)
{
    if (sCount == 0) return ITEM_NONE;
    if (sTab == TAB_ITEMS && sMode == MODE_WITHDRAW) return gSaveBlock1Ptr->pcItems[sList.sel].itemId;
    if (sTab == TAB_ITEMS) return sBag[sList.sel].item;
    return gSaveBlock1Ptr->mail[sMailSlots[sList.sel]].itemId;
}

static uint16_t CurQuantity(void)
{
    if (sTab == TAB_ITEMS && sMode == MODE_WITHDRAW) return GetPcItemQuantity(&gSaveBlock1Ptr->pcItems[sList.sel].quantity);
    if (sTab == TAB_ITEMS) return sBag[sList.sel].qty;
    return 1;
}

static void SetItemVars(uint16_t item, int32_t n)
{
    CopyItemName(item, gStringVar1);
    ConvertIntToDecimalStringN(gStringVar2, n, STR_CONV_MODE_LEFT_ALIGN, 3);
}

// ---------------------------------------------------------------- items

static void Withdraw(int32_t n)
{
    const uint16_t item = CurItem();
    if (!AddBagItem(item, (u16)n))
    {
        PlaySE(SE_FAILURE);
        ToastText(gText_NoMoreRoomInBag);
        return;
    }
    PlaySE(SE_SELECT);
    SetItemVars(item, n);
    ToastText(gText_WithdrewQuantItem);
    RemovePCItem(item, (u16)n);
    ItemPcCompaction();
    Refresh();
}

static void TossAsk(int32_t n)
{
    if (ItemId_GetImportance(CurItem()))
    {
        PlaySE(SE_FAILURE);
        UiKit_Toast("%s", UI_TR("That's too important to throw out!"));
        sAsk = ASK_NONE;
        return;
    }
    sQuantity = n;
    SetItemVars(CurItem(), n);
    StringExpandPlaceholders(gStringVar4, gText_ThrowAwayStrVar2OfThisItemQM);
    UiGame_Text(gStringVar4, sQuestion, sizeof(sQuestion));
    UiMenu_Open(&sMenu, UI_TR("TOSS?"), 380, 200, 200);
    UiMenu_Add(&sMenu, UI_TR("YES"), 1);
    UiMenu_Add(&sMenu, UI_TR("NO"), 0);
    sAsk = ASK_TOSS_CONFIRM;
}

static void Deposit(int32_t n)
{
    const uint16_t item = CurItem();
    if (ItemId_GetImportance(item))
    {
        PlaySE(SE_FAILURE);
        UiKit_Toast("%s", UI_TR("That item can't be stored!"));
        return;
    }
    if (!AddPCItem(item, (u16)n))
    {
        PlaySE(SE_FAILURE);
        UiKit_Toast(UI_TR("There's no more room to store items."));
        return;
    }
    PlaySE(SE_SELECT);
    SetItemVars(item, n);
    UiKit_Toast(UI_TR("Deposited %d."), (int)n);
    RemoveBagItem(item, (u16)n);
    Refresh();
}

static void ItemAction(bool toss)
{
    const uint16_t item = CurItem();
    const uint16_t have = CurQuantity();
    if (item == ITEM_NONE)
        return;
    if (sMode == MODE_DEPOSIT)
    {
        if (have == 1) { Deposit(1); return; }
        sAsk = ASK_DEPOSIT;
        UiQty_Open(&sQty, UI_TR("DEPOSIT HOW MANY?"), have);
        return;
    }
    if (toss)
    {
        if (have == 1) { TossAsk(1); return; }
        sAsk = ASK_TOSS;
        UiQty_Open(&sQty, UI_TR("TOSS HOW MANY?"), have);
        return;
    }
    if (have == 1) { Withdraw(1); return; }
    sAsk = ASK_WITHDRAW;
    UiQty_Open(&sQty, UI_TR("WITHDRAW HOW MANY?"), have);
}

// ---------------------------------------------------------------- mail

static void MailToBagAsk(void)
{
    UiGame_Text(gText_MessageWillBeLost, sQuestion, sizeof(sQuestion));
    UiMenu_Open(&sMenu, UI_TR("TO THE BAG?"), 380, 200, 240);
    UiMenu_Add(&sMenu, UI_TR("YES"), 1);
    UiMenu_Add(&sMenu, UI_TR("NO"), 0);
    sAsk = ASK_MAIL_TO_BAG;
}

static void MailToBag(void)
{
    struct Mail *mail = &gSaveBlock1Ptr->mail[sMailSlots[sList.sel]];
    if (!AddBagItem(mail->itemId, 1))
    {
        PlaySE(SE_FAILURE);
        ToastText(gText_BagIsFull);
        return;
    }
    PlaySE(SE_SELECT);
    ToastText(gText_MailReturnedToBagMessageErased);
    ClearMailStruct(mail);
    CompactMail();
    Refresh();
}

static void MailGiveAsk(void)
{
    if (CalculatePlayerPartyCount() == 0)
    {
        PlaySE(SE_FAILURE);
        ToastText(gText_ThereIsNoPokemon);
        return;
    }
    snprintf(sQuestion, sizeof(sQuestion), UI_TR("Give the MAIL to which POKéMON?"));
    UiMenu_Open(&sMenu, UI_TR("GIVE TO"), 560, 100, 300);
    for (int32_t i = 0; i < CalculatePlayerPartyCount(); i++)
    {
        UiMon m;
        UiGame_Mon(i, &m);
        UiMenu_Add(&sMenu, m.name, i);
    }
    sAsk = ASK_MAIL_GIVE;
}

static bool MailGive(int32_t slot)
{
    struct Pokemon *mon = &gPlayerParty[slot];
    if (GetMonData(mon, MON_DATA_IS_EGG))
    {
        PlaySE(SE_FAILURE);
        return false;
    }
    PlaySE(SE_SELECT);
    if (GetMonData(mon, MON_DATA_HELD_ITEM) != ITEM_NONE)
    {
        ToastText(gText_PkmnHoldingItemCantHoldMail);
        return true;
    }
    struct Mail *mail = &gSaveBlock1Ptr->mail[sMailSlots[sList.sel]];
    GiveMailToMon2(mon, mail);
    ClearMailStruct(mail);
    ToastText(gText_MailTransferredFromMailbox);
    CompactMail();
    Refresh();
    return true;
}

// ---------------------------------------------------------------- input

static bool Update(const UiInput *in)
{
    sFrame++;
    if (sQty.open)
    {
        const int32_t n = UiQty_Update(&sQty, in);
        if (n > 0)
        {
            if (sAsk == ASK_WITHDRAW) Withdraw(n);
            else if (sAsk == ASK_DEPOSIT) Deposit(n);
            else if (sAsk == ASK_TOSS) { TossAsk(n); return true; }
            sAsk = ASK_NONE;
        }
        else if (n == 0)
            sAsk = ASK_NONE;
        return true;
    }
    if (sMenu.open)
    {
        const int32_t id = UiMenu_Update(&sMenu, in);
        if (id == UI_MENU_NONE)
            return true;
        if (sAsk == ASK_TOSS_CONFIRM && id == 1)
        {
            const uint16_t item = CurItem();
            SetItemVars(item, sQuantity);
            ToastText(gText_ThrewAwayStrVar2StrVar1s);
            RemovePCItem(item, (u16)sQuantity);
            ItemPcCompaction();
            Refresh();
        }
        else if (sAsk == ASK_MAIL_TO_BAG && id == 1)
            MailToBag();
        else if (sAsk == ASK_MAIL_GIVE && id >= 0)
        {
            if (!MailGive(id)) { sMenu.open = true; return true; }
        }
        sMenu.open = false;
        sAsk = ASK_NONE;
        return true;
    }

    if (in->pressed & VITA_BTN_CIRCLE)
    {
        if (sMoveFrom >= 0) { sMoveFrom = -1; PlaySE(SE_SELECT); return true; }
        PlaySE(SE_SELECT);
        return false;
    }
    const int32_t slot = TabSlot(sTab);
    int32_t t = UiKit_TabHit(in, sTabX, sTabW, sTabCount);
    if (in->pressed & VITA_BTN_L) t = (slot + sTabCount - 1) % sTabCount;
    if (in->pressed & VITA_BTN_R) t = (slot + 1) % sTabCount;
    if (t >= 0 && t != slot && sMoveFrom < 0)
    {
        sTab = sTabIds[t];
        sMode = MODE_WITHDRAW;
        sList.sel = sList.first = 0;
        PlaySE(SE_SELECT);
        Refresh();
        return true;
    }
    if (sTab == TAB_ITEMS && sMoveFrom < 0)
    {
        int32_t m = sMode, p = sPocket;
        if (UiHit(in, sModeX[0], 66, sModeW[0], 30)) m = MODE_WITHDRAW;
        if (UiHit(in, sModeX[1], 66, sModeW[1], 30)) m = MODE_DEPOSIT;
        if (in->pressed & VITA_BTN_SQUARE) { if (sMode == MODE_WITHDRAW) m = MODE_DEPOSIT; else p = (sPocket + 1) % UI_POCKET_COUNT; }
        if (sMode == MODE_DEPOSIT && (in->pressed & VITA_BTN_SELECT)) m = MODE_WITHDRAW;
        if (sMode == MODE_DEPOSIT && (in->repeat & VITA_BTN_LEFT))  p = (sPocket + UI_POCKET_COUNT - 1) % UI_POCKET_COUNT;
        if (sMode == MODE_DEPOSIT && (in->repeat & VITA_BTN_RIGHT)) p = (sPocket + 1) % UI_POCKET_COUNT;
        for (int32_t i = 0; sMode == MODE_DEPOSIT && i < UI_POCKET_COUNT; i++)
            if (UiHit(in, sPocketX[i], 108, sPocketW[i], 30)) p = i;
        if (m != sMode || p != sPocket)
        {
            sMode = m;
            sPocket = p;
            sList.sel = sList.first = 0;
            PlaySE(SE_SELECT);
            Refresh();
            return true;
        }
    }
    const int32_t a = UiList_Update(&sList, in, sTab == TAB_MAIL ? 28 : LIST_X, LIST_Y, sTab == TAB_MAIL ? 396 : LIST_W, ROW_H, false);
    if (sCount == 0)
        return true;
    if (sTab == TAB_ITEMS && sMode == MODE_WITHDRAW)
    {
        if (in->pressed & VITA_BTN_SELECT)
        {
            if (sMoveFrom < 0) { sMoveFrom = sList.sel; PlaySE(SE_SELECT); return true; }
        }
        if (sMoveFrom >= 0)
        {
            if ((in->pressed & VITA_BTN_SELECT) || a >= 0)
            {
                const int32_t to = sList.sel;
                PlaySE(SE_SELECT);
                if (to != sMoveFrom && to != sMoveFrom + 1)
                {
                    MoveItemSlotInList(gSaveBlock1Ptr->pcItems, (u32)sMoveFrom, (u32)to);
                    if (sMoveFrom < to) sList.sel--;
                }
                sMoveFrom = -1;
            }
            return true;
        }
        if (in->pressed & VITA_BTN_TRIANGLE) { ItemAction(true); return true; }
        if (a >= 0) ItemAction(false);
        return true;
    }
    if (sTab == TAB_ITEMS)
    {
        if (a >= 0) ItemAction(false);
        return true;
    }
    // MAILBOX
    if (a >= 0)
    {
        PlaySE(SE_SELECT);
        UiMailRead_Set(&gSaveBlock1Ptr->mail[sMailSlots[sList.sel]]);
        UiNav_Push(&gUiMailReadScreen);
        return true;
    }
    if (in->pressed & VITA_BTN_SQUARE) { PlaySE(SE_SELECT); MailToBagAsk(); return true; }
    if (in->pressed & VITA_BTN_TRIANGLE) { PlaySE(SE_SELECT); MailGiveAsk(); return true; }
    return true;
}

static void Resume(void)
{
    Refresh();
}

// ---------------------------------------------------------------- drawing

static void Build(void)
{
    char buf[96], name[24];
    UiKit_HeaderAs(UI_GBA_BG_OPTIONS, UI_SPR_BALL, UI_TR("PC"), sTabs, sTabCount, TabSlot(sTab), sTabX, sTabW, 12);
    const uint16_t item = CurItem();

    if (sTab == TAB_ITEMS)
    {
        snprintf(buf, sizeof(buf), UI_TR("STORED %u / %u"), (unsigned)CountItemsInPC(), (unsigned)PC_ITEMS_COUNT);
        UiW_HeaderValue(buf, UI_COL_ON_FRAME);
        UiW_Panel(16, 60, 312, 216);
        if (item != ITEM_NONE)
        {
            UiW_Inset(32, 76, 56, 56);
            UiSpr_ItemIcon(item, 36, 80, 2);
            UiGame_ItemName(item, name, sizeof(name));
            UiText_Draw(UI_FACE_PS2P, 16, 100, 82, name, UI_COL_INK);
            uint16_t inPc = 0;
            for (int32_t i = 0; i < PC_ITEMS_COUNT; i++)
                if (gSaveBlock1Ptr->pcItems[i].itemId == item)
                    inPc += GetPcItemQuantity(&gSaveBlock1Ptr->pcItems[i].quantity);
            snprintf(buf, sizeof(buf), UI_TR("PC x%u · BAG x%u"), (unsigned)inPc, (unsigned)BagGetQuantityByItemId(item));
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 100, 102, buf, UI_COL_INK);
            UiGame_ItemDescription(item, buf, sizeof(buf));
            UiW_TextWrapped(32, 144, 280, 30, buf, UI_COL_INK, 4);
        }
        UiW_Inset(16, 288, 312, 204);
        UiW_TextWrapped(30, 296, 284, 30, sMenu.open ? sQuestion : sMoveFrom >= 0 ? UI_TR("Move it where? SELECT or X puts it here.")
                        : sMode == MODE_WITHDRAW ? UI_TR("X takes items out to the BAG. TRIANGLE throws them away. SELECT moves one.")
                                                 : UI_TR("Choose an item from the BAG to store in the PC."), UI_COL_INK, 6);
        UiW_Panel(332, 60, 612, 432);
        UiW_PanelHead(332, 60, 612, sMode == MODE_WITHDRAW ? UI_TR("PC · WITHDRAW") : UI_TR("BAG · DEPOSIT"), NULL);
        sModeW[0] = UiText_Width(UI_FACE_VT323, UI_BODY_PX, UI_TR("WITHDRAW")) + 28;
        sModeW[1] = UiText_Width(UI_FACE_VT323, UI_BODY_PX, UI_TR("DEPOSIT")) + 28;
        sModeX[1] = 932 - sModeW[1];
        sModeX[0] = sModeX[1] - 8 - sModeW[0];
        UiW_Chip(sModeX[0], 66, UI_TR("WITHDRAW"), sMode == MODE_WITHDRAW);
        UiW_Chip(sModeX[1], 66, UI_TR("DEPOSIT"), sMode == MODE_DEPOSIT);
        UiDraw_Rect(335, 102, 606, 42, gUiTheme->paper1);
        UiDraw_Rect(335, 144, 606, 2, gUiTheme->paperShade);
        if (sMode == MODE_DEPOSIT)
        {
            int32_t px = 344;
            for (int32_t i = 0; i < UI_POCKET_COUNT; i++)
            {
                sPocketX[i] = px;
                sPocketW[i] = UiW_Chip(px, 108, UiLocale_Lookup(sPocketNames[i]), i == sPocket);
                px += sPocketW[i] + 8;
            }
        }
        else
        {
            UiText_DrawMid(UI_FACE_VT323, 26, 344, 102, 42, UI_TR("PC STORAGE · SELECT TO REORDER"), UI_COL_INK_SOFT);
        }
        UiDraw_PushClip(LIST_X, LIST_Y, LIST_W, ROWS * ROW_H);
        for (int32_t r = 0; r < ROWS && sList.first + r < sCount; r++)
        {
            const int32_t i = sList.first + r, y = LIST_Y + r * ROW_H;
            const uint16_t it = sMode == MODE_WITHDRAW ? gSaveBlock1Ptr->pcItems[i].itemId : sBag[i].item;
            const uint16_t q = sMode == MODE_WITHDRAW ? GetPcItemQuantity(&gSaveBlock1Ptr->pcItems[i].quantity) : sBag[i].qty;
            const bool on = i == sList.sel;
            UiW_Row(LIST_X, y, LIST_W - 12, ROW_H, on, i);
            if (i == sMoveFrom)
                UiDraw_Frame(LIST_X, y, LIST_W - 12, ROW_H, 3, UI_COL_GOLD);
            UiSpr_ItemIcon(it, LIST_X + 28, y + 8, 1);
            UiGame_ItemName(it, name, sizeof(name));
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 60, y + 4, name, on ? UI_COL_SELECT_INK : UI_COL_INK);
            snprintf(buf, sizeof(buf), "x%u", (unsigned)q);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + LIST_W - 28 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf), y + 4, buf,
                        on ? UI_COL_SELECT_INK : UI_COL_INK_SOFT);
        }
        UiDraw_PopClip();
        if (sCount == 0)
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, LIST_X + 28, LIST_Y + 4, sMode == MODE_WITHDRAW ? UI_TR("There are no items.") : UI_TR("No items in this pocket."),
                        UI_COL_INK_SOFT);
        UiW_ScrollBar(LIST_X + LIST_W - 9, LIST_Y, ROWS * ROW_H, sList.first, ROWS, sCount);
        int32_t fx = 16;
        if (sMode == MODE_WITHDRAW)
        {
            UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Withdraw"));
            UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Toss"));
            UiW_FooterGlyph(&fx, UI_GLYPH_SQUARE, UI_TR("Deposit"));
        }
        else
        {
            UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Deposit"));
            UiW_FooterGlyph(&fx, UI_GLYPH_SQUARE, UI_TR("Pocket"));
        }
        UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    }
    else
    {
        snprintf(buf, sizeof(buf), UI_TR("MAIL %u / %u"), (unsigned)sCount, (unsigned)(MAIL_COUNT - PARTY_SIZE));
        UiW_HeaderValue(buf, UI_COL_ON_FRAME);
        UiW_Panel(16, 60, 420, 432);
        UiW_PanelHead(16, 60, 420, UI_TR("STORED MAIL"), NULL);
        for (int32_t r = 0; r < ROWS && sList.first + r < sCount; r++)
        {
            const int32_t i = sList.first + r, y = LIST_Y + r * ROW_H;
            const struct Mail *mail = &gSaveBlock1Ptr->mail[sMailSlots[i]];
            const bool on = i == sList.sel;
            UiW_Row(28, y, 396, ROW_H, on, i);
            UiSpr_ItemIcon(mail->itemId, 40, y + 8, 1);
            u8 sender[PLAYER_NAME_LENGTH + 4];
            StringCopy(sender, mail->playerName);
            if (StringLength(sender) <= 5)
                ConvertInternationalString(sender, LANGUAGE_JAPANESE);
            UiGame_Text(sender, name, sizeof(name));
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 72, y + 4, name, on ? UI_COL_SELECT_INK : UI_COL_INK);
        }
        if (sCount == 0)
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 40, LIST_Y + 4, UI_TR("There's no MAIL here."), UI_COL_INK_SOFT);
        UiW_Panel(448, 60, 496, 432);
        if (sCount > 0)
            UiMail_Draw(&gSaveBlock1Ptr->mail[sMailSlots[sList.sel]], 456, 76, 2);
        if (sMenu.open)
        {
            UiW_Inset(456, 404, 480, 80);
            UiW_TextWrapped(470, 410, 452, 30, sQuestion, UI_COL_INK, 2);
        }
        int32_t fx = 16;
        UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Read"));
        UiW_FooterGlyph(&fx, UI_GLYPH_SQUARE, UI_TR("To the BAG"));
        UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Give"));
        UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    }
    UiKit_FooterClock();
    if (sMenu.open)
        UiMenu_Draw(&sMenu);
    if (sQty.open)
        UiQty_Draw(&sQty, item);
}

const UiScreen gUiPlayerPcScreen = { Enter, Update, Build, Resume };

// ---------------------------------------------------------------- reading a mail

static const struct Mail *sReading;
static const char *sReadHeader;

void UiMailRead_Set(const struct Mail *mail)
{
    sReading = mail;
    sReadHeader = NULL;
}

void UiMailRead_Open(const struct Mail *mail, const char *header)
{
    sReading = mail;
    sReadHeader = header;
    UiNav_Push(&gUiMailReadScreen);
}

void UiMail_Draw(const struct Mail *mail, int32_t x, int32_t y, int32_t scale)
{
    static struct MailUiLayout sLayout;
    char text[96];
    Mail_LayoutForUi(mail, &sLayout);
    UiSpr_MailStationery(sLayout.mailType, x, y, scale);
    const uint16_t c = sLayout.textColor;
    const uint32_t ink = UI_RGBA(((c & 31) * 255) / 31, (((c >> 5) & 31) * 255) / 31, (((c >> 10) & 31) * 255) / 31, 255);
    const uint8_t px = UI_BODY_PX, face = UI_FACE_VT323;
    for (int32_t i = 0; i < sLayout.numLines; i++)
    {
        UiGame_Text(sLayout.lines[i].text, text, sizeof(text));
        UiText_DrawMid(face, px, x + (16 + sLayout.lines[i].x) * scale, y + (24 + sLayout.lines[i].y) * scale, 14 * scale, text, ink);
    }
    UiGame_Text(sLayout.signature, text, sizeof(text));
    const int32_t sw = UiText_Width(face, px, text);
    UiText_DrawMid(face, px, x + (16 + 104 + sLayout.signatureWidth / 2) * scale - sw / 2, y + (24 + sLayout.signatureY) * scale, 14 * scale,
                   text, ink);
    if (sLayout.iconSpecies != SPECIES_NONE)
        UiSpr_MonIcon(sLayout.iconSpecies, 0, x + sLayout.iconX * scale - 16 * scale, y + sLayout.iconY * scale - 16 * scale, scale);
}

static void ReadEnter(void)
{
}

static bool ReadUpdate(const UiInput *in)
{
    if (in->pressed & (VITA_BTN_CROSS | VITA_BTN_CIRCLE) || in->tap)
    {
        PlaySE(SE_SELECT);
        return false;
    }
    return true;
}

static void ReadBuild(void)
{
    if (sReadHeader != NULL)
        UiKit_HeaderAs(UI_GBA_BG_OPTIONS, UI_SPR_BALL, sReadHeader, NULL, 0, 0, NULL, NULL, 12);
    else
        UiKit_HeaderAs(UI_GBA_BG_OPTIONS, UI_SPR_BALL, UI_TR("PC"), sTabs, sTabCount, TabSlot(TAB_MAIL), NULL, NULL, 12);
    char name[24];
    if (sReading != NULL)
    {
        UiGame_ItemName(sReading->itemId, name, sizeof(name));
        UiW_HeaderValue(name, UI_COL_ON_FRAME);
        UiMail_Draw(sReading, (UI_SCREEN_W - 480) / 2, 60 + (432 - 320) / 2, 2);
    }
    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Close"));
    UiKit_FooterClock();
}

const UiScreen gUiMailReadScreen = { ReadEnter, ReadUpdate, ReadBuild, NULL };
