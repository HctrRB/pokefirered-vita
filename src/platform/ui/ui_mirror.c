// Mirrors of the game's field and 2D-scene windows in the 960x544 native style.
// The game keeps running its text printers and menus on Core 1; UiMirror_Draw reads
// their live state, draws native overlays (including the starter/showmonpic card), and
// sets sCovers so Core 0 skips GBA Pass 6 whenever every active BG0 window is mirrored.

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "data.h"
#include "field_message_box.h"
#include "list_menu.h"
#include "main.h"
#include "overworld.h"
#include "string_util.h"
#include "text.h"
#include "window.h"
#pragma GCC diagnostic pop
#include "characters.h"
#include "constants/species.h"

// Text printer tracking (src/text_printer.c)
extern const u8 *gUiPrinterStart[WINDOWS_MAX];
extern u32 gUiPrinterSerial;
extern const struct TextPrinter *GetTextPrinterForUi(u8 windowId);
extern struct Window gWindows[];

// FireRed *ForUi hooks (src/*.c)
extern bool8 FieldMessage_GetForUi(const u8 **outStr, bool8 *outWaiting);
extern bool8 Menu_GetYesNoForUi(u8 *outCursor);
extern u8 Port_YesNoWindow(void);
extern void Port_MenuSetCursor(u8 row);
extern bool8 ScriptMenu_GetMultichoiceForUi(const u8 **outLabels, u8 maxOut, u8 *outCount, u8 *outCursor, bool8 *outIgnoreB);
extern u8 ScriptMenu_GetMultichoiceWindowForUi(void);
extern bool8 PlayerPc_GetTopMenuForUi(const u8 **outLabels, u8 maxOut, u8 *outCount, u8 *outCursor, u8 *outWin);
extern bool8 PokeStorage_GetMainMenuForUi(const u8 **outLabels, u8 maxOut, u8 *outCount, u8 *outCursor, u8 *outWin);
extern bool8 ScriptMenu_GetMonPicForUi(u16 *species, u8 *windowId);
extern bool8 ListMenu_GetForUi(const struct ListMenuItem **outItems, u16 *outTotal, u16 *outMaxShowed,
                               u16 *outScroll, u16 *outRow, u8 *outTaskId);
extern u8 ListMenu_GetWindowForUi(void);
extern void Port_ListSetCursor(u8 listTaskId, u16 index);
extern bool8 Money_GetBoxForUi(u32 *outAmount);
extern u8 Money_GetWindowForUi(void);
extern bool8 Coins_GetBoxForUi(u32 *outAmount);
extern u8 Coins_GetWindowForUi(void);
extern bool8 EvolutionScene_GetMessageForUi(const u8 **outStr);
extern bool8 EggHatch_GetMessageForUi(const u8 **outStr);
extern bool8 TradeScene_GetMessageForUi(const u8 **outStr);

#define MIRROR_MC_MAX 10
#define HOLD_FRAMES   6

static bool sCovers;
static bool sSceneText;
static char sHeldLines[2][160];
static int32_t sHold;
static uint16_t sInjected;
static bool sTouchPrev;
static int32_t sTouchX, sTouchY, sTouchDownX, sTouchDownY;
static uint32_t sFrame;

uint16_t Ui_TakeInjectedKeys(void)
{
    return __atomic_exchange_n(&sInjected, 0U, __ATOMIC_ACQ_REL);
}

void UiMirror_Inject(const uint16_t keys)
{
    __atomic_fetch_or(&sInjected, keys, __ATOMIC_RELEASE);
}

bool UiMirror_Tap(int32_t *const x, int32_t *const y)
{
    int32_t tx = 0, ty = 0;
    const bool touch = VitaPlatform_ReadTouch(&tx, &ty);
    bool tap = false;
    if (touch && !sTouchPrev)
    {
        sTouchDownX = tx;
        sTouchDownY = ty;
    }
    if (touch)
    {
        sTouchX = tx;
        sTouchY = ty;
    }
    else if (sTouchPrev
             && sTouchX - sTouchDownX < 8 && sTouchDownX - sTouchX < 8
             && sTouchY - sTouchDownY < 8 && sTouchDownY - sTouchY < 8)
    {
        tap = true;
    }
    sTouchPrev = touch;
    if (x != NULL) *x = sTouchX;
    if (y != NULL) *y = sTouchY;
    return tap;
}

static void TapMenu(const int32_t tx, const int32_t ty, const int32_t x, const int32_t y,
                    const int32_t w, const int32_t count, const int32_t cursor)
{
    for (int32_t i = 0; i < count; i++)
    {
        if (tx >= x + 8 && tx < x + w - 8 && ty >= y + 8 + i * 42 && ty < y + 50 + i * 42)
        {
            if (i == cursor)
                UiMirror_Inject(A_BUTTON);
            else
                Port_MenuSetCursor((u8)i);
        }
    }
}

bool Ui_MirrorCovers(void)
{
    return Ui_IsEnabled() && !Ui_IsOpen() && __atomic_load_n(&sCovers, __ATOMIC_ACQUIRE);
}

void UiMirror_VisibleLines(const uint8_t *const start, const uint8_t *const end, char out[2][160])
{
    u8 lines[2][96];
    int32_t len[2] = { 0, 0 }, cur = 0;
    bool pendingClear = false, pendingScroll = false;

    for (const u8 *p = start; p != NULL && *p != EOS && (end == NULL || p < end); p++)
    {
        const u8 c = *p;
        if (c == CHAR_PROMPT_CLEAR) { pendingClear = true; pendingScroll = false; continue; }
        if (c == CHAR_PROMPT_SCROLL) { pendingScroll = true; continue; }
        if (pendingClear) { len[0] = len[1] = 0; cur = 0; pendingClear = false; }
        if (pendingScroll)
        {
            memcpy(lines[0], lines[1], (size_t)len[1]);
            len[0] = len[1];
            len[1] = 0;
            cur = 1;
            pendingScroll = false;
        }
        if (c == CHAR_NEWLINE)
        {
            if (cur == 0)
            {
                cur = 1;
            }
            else
            {
                memcpy(lines[0], lines[1], (size_t)len[1]);
                len[0] = len[1];
                len[1] = 0;
            }
            continue;
        }
        if (c == PLACEHOLDER_BEGIN || c == CHAR_KEYPAD_ICON || c == CHAR_DYNAMIC)
        {
            if (p[1] == EOS) break;
            p++;
            continue;
        }
        if (c == CHAR_EXTRA_SYMBOL)
        {
            if (p[1] == EOS) break;
            if (len[cur] < 92)
            {
                lines[cur][len[cur]++] = c;
                lines[cur][len[cur]++] = p[1];
            }
            p++;
            continue;
        }
        if (c == EXT_CTRL_CODE_BEGIN)
        {
            if (p[1] == EOS) break;
            p += GetExtCtrlCodeLength(p[1]);
            continue;
        }
        if (len[cur] < 94)
            lines[cur][len[cur]++] = c;
    }

    for (int32_t i = 0; i < 2; i++)
    {
        lines[i][len[i]] = EOS;
        UiGame_Text(lines[i], out[i], 160);
    }
}

static bool OtherBg0Windows(const u8 *const except, const int32_t n)
{
    const u8 moneyWin = Money_GetWindowForUi();
    const u8 coinsWin = Coins_GetWindowForUi();
    for (int32_t w = 1; w < WINDOWS_MAX; w++)
    {
        if (gWindows[w].window.bg != 0 || gWindows[w].tileData == NULL)
            continue;
        if (w == moneyWin || w == coinsWin)
            continue;
        bool mirrored = false;
        for (int32_t i = 0; i < n && !mirrored; i++)
        {
            if (except[i] == (u8)w)
                mirrored = true;
        }
        if (!mirrored)
            return true;
    }
    return false;
}

static bool HeldBox(void)
{
    if (sHold <= 0)
    {
        __atomic_store_n(&sCovers, false, __ATOMIC_RELEASE);
        return false;
    }
    sHold--;
    const int32_t x = 48, y = UI_SCREEN_H - 24 - 124, w = UI_SCREEN_W - 96, h = 124;
    UiW_DialogBox(x, y, w, h);
    UiText_Draw(UI_FACE_VT323, UI_DIALOG_PX, x + 28, y + 12, sHeldLines[0], UI_COL_INK);
    UiText_Draw(UI_FACE_VT323, UI_DIALOG_PX, x + 28, y + 58, sHeldLines[1], UI_COL_INK);
    __atomic_store_n(&sCovers, true, __ATOMIC_RELEASE);
    return true;
}

bool UiMirror_Draw(void)
{
    sFrame++;
    bool drew = false;
    const bool field = Ui_IsEnabled()
                    && UiGame_InOverworld()
                    && __atomic_load_n(&gUiVoxelFrame, __ATOMIC_RELAXED);

    const u8 *sceneGba = NULL;
    const bool scene = Ui_IsEnabled()
                    && !gMain.inBattle
                    && (EvolutionScene_GetMessageForUi(&sceneGba)
                        || EggHatch_GetMessageForUi(&sceneGba)
                        || TradeScene_GetMessageForUi(&sceneGba));

    static u32 sSceneSerial;
    const struct TextPrinter *const tp0 = GetTextPrinterForUi(0);
    if (!scene)
    {
        sSceneText = false;
        sSceneSerial = gUiPrinterSerial;
    }
    else if ((tp0 != NULL && tp0->active) || gUiPrinterSerial != sSceneSerial)
    {
        sSceneText = true;
    }
    const bool sceneBox = scene && sSceneText;

    const u8 *dlgStr = NULL;
    bool8 dlgWaiting = FALSE;
    const bool fieldMsg = field && (FieldMessage_GetForUi(&dlgStr, &dlgWaiting) || (tp0 != NULL && tp0->active));

    u8 yesCursor = 0;
    const bool yesNo = (field || sceneBox) && Menu_GetYesNoForUi(&yesCursor);
    const u8 yesWin = yesNo ? Port_YesNoWindow() : WINDOW_NONE;

    const u8 *mcLabels[MIRROR_MC_MAX];
    u8 mcCount = 0, mcCursor = 0;
    bool8 mcIgnoreB = FALSE;
    u8 choiceWin = WINDOW_NONE;
    bool choice = false;
    if (field && !yesNo)
    {
        if (ScriptMenu_GetMultichoiceForUi(mcLabels, MIRROR_MC_MAX, &mcCount, &mcCursor, &mcIgnoreB) && mcCount > 0)
        {
            choice = true;
            choiceWin = ScriptMenu_GetMultichoiceWindowForUi();
        }
        else if (PlayerPc_GetTopMenuForUi(mcLabels, MIRROR_MC_MAX, &mcCount, &mcCursor, &choiceWin) && mcCount > 0)
        {
            choice = true;
        }
        else if (PokeStorage_GetMainMenuForUi(mcLabels, MIRROR_MC_MAX, &mcCount, &mcCursor, &choiceWin) && mcCount > 0)
        {
            choice = true;
        }
    }
    (void)mcIgnoreB;

    const struct ListMenuItem *listItems = NULL;
    u16 listTotal = 0, listMaxShowed = 0, listScroll = 0, listRow = 0;
    u8 listTaskId = 0xFF;
    const bool listActive = field && !yesNo && !choice
                         && ListMenu_GetForUi(&listItems, &listTotal, &listMaxShowed, &listScroll, &listRow, &listTaskId)
                         && listTotal > 0;
    const u8 listWin = listActive ? ListMenu_GetWindowForUi() : WINDOW_NONE;

    const bool box = sceneBox || fieldMsg
                  || (field && (choice || yesNo || listActive) && gUiPrinterStart[0] != NULL && gWindows[0].tileData != NULL);

    u16 monSpecies = SPECIES_NONE;
    u8 monPicWin = WINDOW_NONE;
    const bool monPicActive = field && ScriptMenu_GetMonPicForUi(&monSpecies, &monPicWin) && monSpecies != SPECIES_NONE;

    const u8 except[4] = { yesWin, choiceWin, listWin, monPicWin };

    int32_t tapX = 0, tapY = 0;
    const bool tap = UiMirror_Tap(&tapX, &tapY);

    if ((!box && !yesNo && !choice && !listActive && !monPicActive) || (!sceneBox && OtherBg0Windows(except, 4)))
    {
        if (!field)
            sHold = 0;
        return HeldBox();
    }

    if (monPicActive)
    {
        const int32_t cardW = 208, cardH = 216;
        const int32_t cardX = (UI_SCREEN_W - cardW) / 2;
        const int32_t cardY = 88;
        char spName[24];
        UiGame_SpeciesName(monSpecies, spName, sizeof(spName));
        if (gUiTheme->gbaFrame)
        {
            UiW_GbaWin(cardX, cardY, cardW, cardH, UI_GBA_WIN_GOLD);
            UiSpr_MonFront(monSpecies, false, cardX + (cardW - 128) / 2, cardY + 18, 2);
            UiW_GbaSectionStrip(cardX + 10, cardY + cardH - 42, cardW - 20, 30, UI_GBA_TAG_CRIMSON, spName, NULL);
        }
        else
        {
            UiW_Panel(cardX, cardY, cardW, cardH);
            UiSpr_MonFront(monSpecies, false, cardX + (cardW - 128) / 2, cardY + 18, 2);
            UiText_DrawCentred(UI_FACE_PS2P, 16, cardX + 10, cardY + cardH - 42, cardW - 20, 32, spName, UI_COL_INK);
        }
        drew = true;
    }

    if (box)
    {
        const u8 *start = gUiPrinterStart[0];
        if (start == NULL)
            start = sceneBox ? sceneGba : dlgStr;
        const u8 *end = (tp0 != NULL && tp0->active) ? tp0->printerTemplate.currentChar : NULL;
        if (end != NULL && (start == NULL || end < start || end - start > 1024))
            end = NULL;
        char lines[2][160];
        UiMirror_VisibleLines(start, end, lines);

        const int32_t x = 48, y = UI_SCREEN_H - 24 - 124, w = UI_SCREEN_W - 96, h = 124;
        if (sceneBox)
        {
            UiDraw_Rect(0, UI_SCREEN_H - 172, UI_SCREEN_W, 172, UI_COL_FRAME1);
            UiDraw_Rect(0, UI_SCREEN_H - 172, UI_SCREEN_W, 3, UI_COL_LINE_DARK);
        }
        memcpy(sHeldLines, lines, sizeof(sHeldLines));
        sHold = sceneBox ? 0 : HOLD_FRAMES;

        const bool waiting = (tp0 == NULL || !tp0->active || tp0->state == RENDER_STATE_WAIT)
                          || (!sceneBox && dlgWaiting != FALSE);
        UiW_DialogBox(x, y, w, h);
        UiText_Draw(UI_FACE_VT323, UI_DIALOG_PX, x + 28, y + 12, lines[0], UI_COL_INK);
        UiText_Draw(UI_FACE_VT323, UI_DIALOG_PX, x + 28, y + 58, lines[1], UI_COL_INK);
        if (waiting && !yesNo && !choice && !listActive && ((sFrame / 16U) & 1U))
            UiDraw_Tri((float)(x + w - 40), (float)(y + h - 30), (float)(x + w - 24), (float)(y + h - 30),
                       (float)(x + w - 32), (float)(y + h - 20), UI_COL_MONEY);
        drew = true;
    }

    // Money / Coins info boxes stacked top-right
    {
        int32_t infoRow = 0;
        u32 moneyAmt = 0;
        if (field && Money_GetBoxForUi(&moneyAmt))
        {
            char text[32];
            snprintf(text, sizeof(text), UI_TR("MONEY $%u"), (unsigned)moneyAmt);
            const int32_t bw = UiText_Width(UI_FACE_VT323, UI_BODY_PX, text) + 28;
            const int32_t by = 16 + infoRow++ * 52;
            UiW_DialogBox(UI_SCREEN_W - 48 - bw, by, bw, 44);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, UI_SCREEN_W - 48 - bw + 14, by + 4, text, UI_COL_INK);
            drew = true;
        }
        u32 coinsAmt = 0;
        if (field && Coins_GetBoxForUi(&coinsAmt))
        {
            char text[32];
            snprintf(text, sizeof(text), UI_TR("COINS %u"), (unsigned)coinsAmt);
            const int32_t bw = UiText_Width(UI_FACE_VT323, UI_BODY_PX, text) + 28;
            const int32_t by = 16 + infoRow++ * 52;
            UiW_DialogBox(UI_SCREEN_W - 48 - bw, by, bw, 44);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, UI_SCREEN_W - 48 - bw + 14, by + 4, text, UI_COL_INK);
            drew = true;
        }
    }

    if (box && tap && !yesNo && !choice && !listActive && tapY >= UI_SCREEN_H - 24 - 124 && tapY < UI_SCREEN_H - 24)
        UiMirror_Inject(A_BUTTON);

    if (choice)
    {
        char opt[40];
        int32_t w = 200;
        for (u8 i = 0; i < mcCount; i++)
        {
            UiGame_Text(mcLabels[i], opt, sizeof(opt));
            const int32_t tw = UiText_Width(UI_FACE_VT323, UI_BODY_PX, opt) + 64;
            if (tw > w) w = tw;
        }
        const int32_t h = 16 + (int32_t)mcCount * 42;
        const int32_t x = UI_SCREEN_W - 48 - w;
        const int32_t y = UI_SCREEN_H - 24 - 124 - 12 - h;
        if (tap) TapMenu(tapX, tapY, x, y, w, mcCount, mcCursor);
        UiW_DialogBox(x, y, w, h);
        for (u8 i = 0; i < mcCount; i++)
        {
            UiGame_Text(mcLabels[i], opt, sizeof(opt));
            UiW_Row(x + 8, y + 8 + (int32_t)i * 42, w - 16, 42, mcCursor == i, i);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 36, y + 12 + (int32_t)i * 42, opt,
                        mcCursor == i ? UI_COL_SELECT_INK : UI_COL_INK);
        }
        drew = true;
    }

    if (listActive)
    {
        const int32_t total = (int32_t)listTotal;
        const int32_t shown = (int32_t)(listMaxShowed > 6 ? 6 : listMaxShowed);
        const int32_t first = (int32_t)listScroll;
        const int32_t cur = first + (int32_t)listRow;
        char opt[40];
        int32_t w = 240;
        for (int32_t i = 0; i < total; i++)
        {
            UiGame_Text(listItems[i].label, opt, sizeof(opt));
            const int32_t tw = UiText_Width(UI_FACE_VT323, UI_BODY_PX, opt) + 72;
            if (tw > w) w = tw;
        }
        if (w > UI_SCREEN_W / 2)
            w = UI_SCREEN_W / 2;
        const int32_t h = 16 + shown * 42;
        const int32_t x = UI_SCREEN_W - 48 - w;
        const int32_t y = UI_SCREEN_H - 24 - 124 - 12 - h;
        if (tap && tapX >= x + 8 && tapX < x + w - 8 && tapY >= y + 8 && tapY < y + 8 + shown * 42)
        {
            const int32_t r = (tapY - y - 8) / 42;
            if (first + r < total)
            {
                if (r == (int32_t)listRow)
                    UiMirror_Inject(A_BUTTON);
                else
                    Port_ListSetCursor(listTaskId, (u16)(first + r));
            }
        }
        UiW_DialogBox(x, y, w, h);
        for (int32_t r = 0; r < shown && first + r < total; r++)
        {
            UiGame_Text(listItems[first + r].label, opt, sizeof(opt));
            const bool on = (first + r == cur);
            UiW_Row(x + 8, y + 8 + r * 42, w - 28, 42, on, r);
            UiDraw_PushClip(x + 36, y + 8 + r * 42, w - 64, 42);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 36, y + 12 + r * 42, opt, on ? UI_COL_SELECT_INK : UI_COL_INK);
            UiDraw_PopClip();
        }
        UiW_ScrollBar(x + w - 18, y + 8, shown * 42, first, shown, total);
        drew = true;
    }

    if (yesNo)
    {
        const int32_t x = UI_SCREEN_W - 48 - 180;
        const int32_t y = UI_SCREEN_H - 24 - 124 - 112;
        const int32_t w = 180;
        if (tap) TapMenu(tapX, tapY, x, y, w, 2, yesCursor);
        UiW_DialogBox(x, y, w, 100);
        const char *const opt[2] = { UI_N("YES"), UI_N("NO") };
        for (int32_t i = 0; i < 2; i++)
        {
            UiW_Row(x + 8, y + 8 + i * 42, w - 16, 42, yesCursor == i, i);
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 36, y + 12 + i * 42,
                        UiLocale_Lookup(opt[i]), yesCursor == i ? UI_COL_SELECT_INK : UI_COL_INK);
        }
        drew = true;
    }

    __atomic_store_n(&sCovers, drew && !sceneBox, __ATOMIC_RELEASE);
    return drew;
}
