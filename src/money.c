#include "global.h"
#ifdef PLATFORM_VITA
#include "3ds_extras.h"
#endif
#include "gflib.h"
#include "event_data.h"
#include "menu.h"
#include "text_window.h"
#include "strings.h"

#define MAX_MONEY 999999

EWRAM_DATA static u8 sMoneyBoxWindowId = 0xFF;
#ifdef PLATFORM_VITA
static u32 sMoneyBoxAmountForUi;
#endif

u32 GetMoney(u32 *moneyPtr)
{
#ifdef PLATFORM_VITA
    // Infinite money (OPTIONS, CHEATS): the player's money is back at the most
    // whenever it is read; what was spent is gone only until then.
    if (moneyPtr == &gSaveBlock1Ptr->money && CtrExtras_Get("infinite_money")
     && (*moneyPtr ^ gSaveBlock2Ptr->encryptionKey) != MAX_MONEY)
        *moneyPtr = MAX_MONEY ^ gSaveBlock2Ptr->encryptionKey;
#endif
    return *moneyPtr ^ gSaveBlock2Ptr->encryptionKey;
}

void SetMoney(u32 *moneyPtr, u32 newValue)
{
    *moneyPtr = gSaveBlock2Ptr->encryptionKey ^ newValue;
}

bool8 IsEnoughMoney(u32 *moneyPtr, u32 cost)
{
    if (GetMoney(moneyPtr) >= cost)
        return TRUE;
    else
        return FALSE;
}

void AddMoney(u32 *moneyPtr, u32 toAdd)
{
    u32 toSet = GetMoney(moneyPtr);

    // can't have more money than MAX
    if (toSet + toAdd > MAX_MONEY)
    {
        toSet = MAX_MONEY;
    }
    else
    {
        toSet += toAdd;
        // check overflow, can't have less money after you receive more
        if (toSet < GetMoney(moneyPtr))
            toSet = MAX_MONEY;
    }

    SetMoney(moneyPtr, toSet);
}

void RemoveMoney(u32 *moneyPtr, u32 toSub)
{
    u32 toSet = GetMoney(moneyPtr);

    // can't subtract more than you already have
    if (toSet < toSub)
        toSet = 0;
    else
        toSet -= toSub;

    SetMoney(moneyPtr, toSet);
}

bool8 IsEnoughForCostInVar0x8005(void)
{
    return IsEnoughMoney(&gSaveBlock1Ptr->money, gSpecialVar_0x8005);
}

void SubtractMoneyFromVar0x8005(void)
{
    RemoveMoney(&gSaveBlock1Ptr->money, gSpecialVar_0x8005);
}

void PrintMoneyAmountInMoneyBox(u8 windowId, int amount, u8 speed)
{
    u8 *txtPtr;
    s32 strLength;

    ConvertIntToDecimalStringN(gStringVar1, amount, STR_CONV_MODE_LEFT_ALIGN, 6);

    strLength = 6 - StringLength(gStringVar1);
    txtPtr = gStringVar4;

    while (strLength-- != 0)
        *(txtPtr++) = 0;

    StringExpandPlaceholders(txtPtr, gText_PokedollarVar1);
    AddTextPrinterParameterized(windowId, FONT_SMALL, gStringVar4, 64 - GetStringWidth(FONT_SMALL, gStringVar4, 0), 0xC, speed, NULL);
}

void PrintMoneyAmount(u8 windowId, u8 x, u8 y, int amount, u8 speed)
{
    u8 *txtPtr;
    s32 strLength;

    ConvertIntToDecimalStringN(gStringVar1, amount, STR_CONV_MODE_LEFT_ALIGN, 6);

    strLength = 6 - StringLength(gStringVar1);
    txtPtr = gStringVar4;

    while (strLength-- != 0)
        *(txtPtr++) = 0;

    StringExpandPlaceholders(txtPtr, gText_PokedollarVar1);
    AddTextPrinterParameterized(windowId, FONT_SMALL, gStringVar4, x, y, speed, NULL);
}

void PrintMoneyAmountInMoneyBoxWithBorder(u8 windowId, u16 tileStart, u8 paletteNum, int amount)
{
    DrawStdFrameWithCustomTileAndPalette(windowId, FALSE, tileStart, paletteNum);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gText_TrainerCardMoney, 0, 0, 0xFF, 0);
    PrintMoneyAmountInMoneyBox(windowId, amount, 0);
}

void ChangeAmountInMoneyBox(int amount)
{
#ifdef PLATFORM_VITA
    sMoneyBoxAmountForUi = (u32)amount;
#endif
    PrintMoneyAmountInMoneyBox(sMoneyBoxWindowId, amount, 0);
}

void DrawMoneyBox(int amount, u8 x, u8 y)
{
    struct WindowTemplate template;

#ifdef PLATFORM_VITA
    sMoneyBoxAmountForUi = (u32)amount;
#endif
    template = SetWindowTemplateFields(0, x + 1, y + 1, 8, 3, 15, 8);
    sMoneyBoxWindowId = AddWindow(&template);
    FillWindowPixelBuffer(sMoneyBoxWindowId, 0);
    PutWindowTilemap(sMoneyBoxWindowId);
    LoadStdWindowGfx(sMoneyBoxWindowId, 0x21D, BG_PLTT_ID(13));
    PrintMoneyAmountInMoneyBoxWithBorder(sMoneyBoxWindowId, 0x21D, 13, amount);
}

void HideMoneyBox(void)
{
    u8 windowId = sMoneyBoxWindowId;
    if (windowId >= WINDOWS_MAX)
        return;
    sMoneyBoxWindowId = 0xFF;
    ClearStdWindowAndFrameToTransparent(windowId, FALSE);
    CopyWindowToVram(windowId, COPYWIN_GFX);
    RemoveWindow(windowId);
}

#ifdef PLATFORM_VITA
bool8 Money_GetBoxForUi(u32 *outAmount)
{
    if (sMoneyBoxWindowId >= WINDOWS_MAX || gWindows[sMoneyBoxWindowId].tileData == NULL)
        return FALSE;
    if (outAmount != NULL)
        *outAmount = sMoneyBoxAmountForUi;
    return TRUE;
}

u8 Money_GetWindowForUi(void)
{
    return sMoneyBoxWindowId;
}
#endif
