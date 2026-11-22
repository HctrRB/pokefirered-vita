// Easy Chat of the native 960x544 UI (Ui_OpenEasyChat, gUiEasyChatScreen): every type of the game,
// from mail, profile, battle start/won/lost, and interviews to the Bard's song, Trendy Phrase and
// Mystery Gift Questionnaire. Ported 1:1 from pokeemerald-vita-3ds and adapted to FireRed v1.1.

#include <stdio.h>
#include <string.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "easy_chat.h"
#include "main.h"
#include "sound.h"
#pragma GCC diagnostic pop
#include "constants/easy_chat.h"
#include "constants/songs.h"

extern bool8 EasyChat_UiInit(u8 type, u16 *words);
extern u8 EasyChat_UiColumns(void);
extern u8 EasyChat_UiMaxWords(void);
extern u16 *EasyChat_UiPhrase(void);
extern const u8 *EasyChat_UiTitle(void);
extern void EasyChat_UiInstructions(const u8 **a, const u8 **b);
extern void EasyChat_UiConfirmText(const u8 **a, const u8 **b);
extern u8 EasyChat_UiGroupCount(void);
extern u8 EasyChat_UiGroupId(u8 index);
extern const u8 *EasyChat_UiGroupName(u8 groupId);
extern u16 EasyChat_UiSelectGroup(bool8 alphabet, u16 group);
extern u16 EasyChat_UiWordAt(u16 index);
extern u16 EasyChat_UiAlphabetCount(u8 letter);
extern u8 EasyChat_UiTryConfirm(const u8 **t1, const u8 **t2);
extern u8 EasyChat_UiTryQuit(const u8 **t1, const u8 **t2);
extern u8 EasyChat_UiTryDeleteAll(const u8 **t1, const u8 **t2);
extern bool8 EasyChat_UiCanDeleteWord(void);
extern bool8 EasyChat_UiConfirmsEachWord(void);
extern void EasyChat_UiRevertLyrics(const u8 **t1, const u8 **t2);
extern void EasyChat_UiFinish(bool8 confirmed, MainCallback callback);

enum { AT_MSG, AT_GROUPS, AT_WORDS };
enum { ASK_NONE, ASK_CONFIRM, ASK_QUIT, ASK_DELALL, ASK_LYRICS, ASK_MSG };

#define MSG_X 16
#define MSG_Y 60
#define MSG_W 928
#define BTN_W 160
#define BTN_PITCH 50
#define GROUP_W 300
#define WORD_COLS 3
#define ROW_H 40
#define LOW_BOTTOM 492
#define LIST_TOP 52
#define MAX_BUTTONS 4
#define EC_NUM_ALPHABET_GROUPS 27

typedef struct
{
    const char *label;
    uint8_t color;
    void (*press)(void);
} EcButton;

static void (*sExit)(void);
static int32_t sAt, sSlot, sBtn;
static bool sAlpha;
static UiList sGroups;
static int32_t sGroupIds[EC_NUM_GROUPS + EC_NUM_ALPHABET_GROUPS];
static int32_t sGroupCount, sGroup;
static int32_t sWordCount, sWordSel, sWordFirst;
static int32_t sAsk;
static UiMenu sYesNo;
static char sAskText[2][120];
static int32_t sTabX[2], sTabW[2];
static EcButton sBtns[MAX_BUTTONS];
static int32_t sBtnCount;
static int32_t sSlotPitch, sMsgH, sLowY, sListRows;

static void Text(const u8 *gba, char *out, size_t cap)
{
    if (gba == NULL) { out[0] = 0; return; }
    UiGame_Text(gba, out, cap);
}

static void WordText(u16 word, char *out, size_t cap)
{
    if (word == EC_WORD_UNDEFINED) { snprintf(out, cap, "-----"); return; }
    u8 buf[64];
    CopyEasyChatWord(buf, word);
    UiGame_Text(buf, out, cap);
}

static int32_t Rows(void)
{
    const int32_t cols = EasyChat_UiColumns();
    return (EasyChat_UiMaxWords() + cols - 1) / cols;
}

static int32_t SlotW(void)
{
    return (MSG_W - 32 - BTN_W - 16) / EasyChat_UiColumns();
}

static void SlotPos(int32_t i, int32_t *x, int32_t *y)
{
    *x = MSG_X + 16 + (i % EasyChat_UiColumns()) * SlotW();
    *y = MSG_Y + 16 + (i / EasyChat_UiColumns()) * sSlotPitch;
}

static void Layout(void)
{
    const int32_t rows = Rows();
    sSlotPitch = rows <= 3 ? 52 : 40;
    const int32_t words = 32 + rows * sSlotPitch - 8;
    const int32_t buttons = 32 + sBtnCount * BTN_PITCH - 8;
    sMsgH = words > buttons ? words : buttons;
    if (sMsgH < 184)
        sMsgH = 184;
    sLowY = MSG_Y + sMsgH + 8;
    sListRows = (LOW_BOTTOM - sLowY - LIST_TOP + 12) / ROW_H;
}

static bool sConfirmedResult;

static void Finish(void)
{
    EasyChat_UiFinish(sConfirmedResult, sExit);
}

static void End(bool confirmed)
{
    sConfirmedResult = confirmed;
    UiNav_Close(Finish);
}

static void Ask(int32_t kind, const u8 *t1, const u8 *t2)
{
    Text(t1, sAskText[0], sizeof(sAskText[0]));
    Text(t2, sAskText[1], sizeof(sAskText[1]));
    sAsk = kind;
    if (kind != ASK_MSG)
    {
        UiMenu_Open(&sYesNo, UI_TR("EASY CHAT"), 700, 330, 220);
        UiMenu_Add(&sYesNo, UI_TR("YES"), 1);
        UiMenu_Add(&sYesNo, UI_TR("NO"), UI_MENU_CANCEL);
        sYesNo.list.sel = kind == ASK_CONFIRM || kind == ASK_LYRICS ? 0 : 1;
    }
}

static void TryOk(void)
{
    const u8 *a = NULL, *b = NULL;
    const u8 r = EasyChat_UiTryConfirm(&a, &b);
    Ask(r == 0 ? ASK_CONFIRM : r == 1 ? ASK_QUIT : ASK_MSG, a, b);
    PlaySE(SE_SELECT);
}

static void TryQuit(void)
{
    const u8 *a = NULL, *b = NULL;
    Ask(EasyChat_UiTryQuit(&a, &b) == 1 ? ASK_QUIT : ASK_MSG, a, b);
    PlaySE(SE_SELECT);
}

static void TryDeleteAll(void)
{
    const u8 *a = NULL, *b = NULL;
    Ask(EasyChat_UiTryDeleteAll(&a, &b) == 1 ? ASK_DELALL : ASK_MSG, a, b);
    PlaySE(SE_SELECT);
}

static void BuildButtons(void)
{
    sBtnCount = 0;
    sBtns[sBtnCount++] = (EcButton){ UI_TR("DEL ALL"), UI_BTN_RED, TryDeleteAll };
    sBtns[sBtnCount++] = (EcButton){ UI_TR("CANCEL"), UI_BTN_AMBER, TryQuit };
    sBtns[sBtnCount++] = (EcButton){ UI_TR("OK"), UI_BTN_GREEN, TryOk };
}

static void BuildGroups(void)
{
    sGroupCount = 0;
    if (sAlpha)
    {
        for (int32_t i = 0; i < EC_NUM_ALPHABET_GROUPS; i++)
            if (EasyChat_UiAlphabetCount((u8)i) > 0)
                sGroupIds[sGroupCount++] = i;
    }
    else
        for (int32_t i = 0; i < EasyChat_UiGroupCount(); i++)
            sGroupIds[sGroupCount++] = EasyChat_UiGroupId((u8)i);
    UiList_Reset(&sGroups, sGroupCount, sListRows);
}

static void SelectGroup(int32_t index)
{
    sGroup = index;
    sWordCount = sGroupCount > 0 ? EasyChat_UiSelectGroup(sAlpha, (u16)sGroupIds[index]) : 0;
    sWordSel = sWordFirst = 0;
}

static void GroupLabel(int32_t index, char *out, size_t cap)
{
    if (sAlpha)
    {
        const int32_t g = sGroupIds[index];
        if (g == 26) snprintf(out, cap, UI_TR("OTHERS"));
        else snprintf(out, cap, "%c", 'A' + g);
    }
    else
        Text(EasyChat_UiGroupName((u8)sGroupIds[index]), out, cap);
}

bool Ui_OpenEasyChat(uint8_t type, uint16_t *words, void (*exitCallback)(void))
{
    if (!Ui_IsEnabled() || Ui_IsOpen() || words == NULL || !EasyChat_UiInit(type, words))
        return false;
    sExit = exitCallback;
    UiCore_OpenHosted(&gUiEasyChatScreen, NULL);
    return true;
}

static void Enter(void)
{
    sAt = AT_MSG;
    sBtn = -1;
    sAlpha = false;
    sAsk = ASK_NONE;
    BuildButtons();
    Layout();
    u16 *p = EasyChat_UiPhrase();
    sSlot = 0;
    for (int32_t i = 0; i < EasyChat_UiMaxWords(); i++)
        if (p[i] == EC_WORD_UNDEFINED) { sSlot = i; break; }
    BuildGroups();
    SelectGroup(0);
}

static void SetAlpha(bool alpha)
{
    if (alpha == sAlpha)
        return;
    sAlpha = alpha;
    BuildGroups();
    SelectGroup(0);
    PlaySE(SE_SELECT);
}

static void PutWord(u16 word)
{
    u16 *p = EasyChat_UiPhrase();
    p[sSlot] = word;
    sAt = AT_MSG;
    PlaySE(SE_SELECT);
    if (EasyChat_UiConfirmsEachWord())
    {
        const u8 *a = NULL, *b = NULL;
        EasyChat_UiConfirmText(&a, &b);
        Ask(ASK_LYRICS, a, b);
        return;
    }
    for (int32_t i = 1; i <= EasyChat_UiMaxWords(); i++)
    {
        const int32_t j = (sSlot + i) % EasyChat_UiMaxWords();
        if (p[j] == EC_WORD_UNDEFINED) { sSlot = j; break; }
    }
}

static bool UpdateAsk(const UiInput *in)
{
    if (sAsk == ASK_MSG)
    {
        if ((in->pressed & (VITA_BTN_CROSS | VITA_BTN_CIRCLE)) || in->tap)
            sAsk = ASK_NONE;
        return true;
    }
    const int32_t id = UiMenu_Update(&sYesNo, in);
    if (id == UI_MENU_NONE)
        return true;
    const int32_t kind = sAsk;
    sAsk = ASK_NONE;
    if (id != 1)
    {
        if (kind == ASK_LYRICS)
        {
            const u8 *a = NULL, *b = NULL;
            EasyChat_UiRevertLyrics(&a, &b);
            Ask(ASK_MSG, a, b);
        }
        return true;
    }
    if (kind == ASK_CONFIRM || kind == ASK_LYRICS) End(TRUE);
    else if (kind == ASK_QUIT) End(FALSE);
    else if (kind == ASK_DELALL)
    {
        u16 *p = EasyChat_UiPhrase();
        for (int32_t i = 0; i < EasyChat_UiMaxWords(); i++)
            p[i] = EC_WORD_UNDEFINED;
        sSlot = 0;
    }
    return true;
}

static void PressButton(int32_t b)
{
    if (b >= 0 && b < sBtnCount)
        sBtns[b].press();
}

static void ButtonPos(int32_t b, int32_t *x, int32_t *y)
{
    *x = MSG_X + MSG_W - 16 - BTN_W;
    *y = MSG_Y + 16 + b * BTN_PITCH;
}

static bool Update(const UiInput *in)
{
    if (sAsk != ASK_NONE)
        return UpdateAsk(in);
    const int32_t max = EasyChat_UiMaxWords(), cols = EasyChat_UiColumns(), rows = Rows();
    const int32_t slotW = SlotW(), visWords = WORD_COLS * sListRows;

    if (in->pressed & (VITA_BTN_L | VITA_BTN_R)) { SetAlpha(!sAlpha); return true; }
    const int32_t t = UiKit_TabHit(in, sTabX, sTabW, 2);
    if (t >= 0) { SetAlpha(t == 1); return true; }
    if (in->pressed & VITA_BTN_START) { TryOk(); return true; }

    if (in->tap)
    {
        for (int32_t i = 0; i < max; i++)
        {
            int32_t x, y;
            SlotPos(i, &x, &y);
            if (!UiHit(in, x, y, slotW - 8, sSlotPitch - 8)) continue;
            if (sAt == AT_MSG && sBtn < 0 && sSlot == i) sAt = AT_GROUPS;
            else { sAt = AT_MSG; sBtn = -1; sSlot = i; }
            PlaySE(SE_SELECT);
            return true;
        }
        for (int32_t b = 0; b < sBtnCount; b++)
        {
            int32_t x, y;
            ButtonPos(b, &x, &y);
            if (UiHit(in, x, y, BTN_W, 42)) { sAt = AT_MSG; sBtn = b; PressButton(b); return true; }
        }
        for (int32_t r = 0; r < sListRows && sGroups.first + r < sGroupCount; r++)
            if (UiHit(in, 28, sLowY + LIST_TOP + r * ROW_H, GROUP_W - 24, ROW_H))
            {
                const int32_t g = sGroups.first + r;
                if (sAt == AT_GROUPS && sGroups.sel == g) sAt = AT_WORDS;
                else { sAt = AT_GROUPS; sGroups.sel = g; SelectGroup(g); }
                PlaySE(SE_SELECT);
                return true;
            }
        const int32_t ww = (MSG_W - GROUP_W - 12 - 24) / WORD_COLS;
        for (int32_t k = 0; k < visWords; k++)
        {
            const int32_t i = sWordFirst + k;
            if (i >= sWordCount) break;
            if (!UiHit(in, GROUP_W + 40 + (k % WORD_COLS) * ww, sLowY + LIST_TOP + (k / WORD_COLS) * ROW_H, ww, ROW_H)) continue;
            if (sAt == AT_WORDS && sWordSel == i) PutWord(EasyChat_UiWordAt((u16)i));
            else { sAt = AT_WORDS; sWordSel = i; PlaySE(SE_SELECT); }
            return true;
        }
        return true;
    }

    const int32_t dx = (in->repeat & VITA_BTN_RIGHT) ? 1 : (in->repeat & VITA_BTN_LEFT) ? -1 : 0;
    const int32_t dy = (in->repeat & VITA_BTN_DOWN) ? 1 : (in->repeat & VITA_BTN_UP) ? -1 : 0;
    if ((in->pressed & VITA_BTN_TRIANGLE) && sBtn < 0)
    {
        if (EasyChat_UiCanDeleteWord())
        {
            EasyChat_UiPhrase()[sSlot] = EC_WORD_UNDEFINED;
            PlaySE(SE_SELECT);
        }
        else
            PlaySE(SE_FAILURE);
        return true;
    }
    switch (sAt)
    {
    case AT_MSG:
        if (in->pressed & VITA_BTN_CIRCLE) { TryQuit(); return true; }
        if (in->pressed & VITA_BTN_CROSS)
        {
            if (sBtn >= 0) PressButton(sBtn);
            else { sAt = AT_GROUPS; PlaySE(SE_SELECT); }
            return true;
        }
        if (dx || dy)
        {
            if (sBtn >= 0)
            {
                if (dx < 0) { sBtn = -1; sSlot = (sSlot / cols) * cols + cols - 1; if (sSlot >= max) sSlot = max - 1; }
                else if (dy) sBtn = (sBtn + dy + sBtnCount) % sBtnCount;
            }
            else
            {
                const int32_t c = sSlot % cols, r = sSlot / cols;
                if (dx > 0 && c == cols - 1) sBtn = r < sBtnCount ? r : sBtnCount - 1;
                else if (dx) sSlot = r * cols + (c + dx + cols) % cols;
                else sSlot = ((r + dy + rows) % rows) * cols + c;
                if (sSlot >= max) sSlot = max - 1;
            }
            PlaySE(SE_SELECT);
        }
        return true;
    case AT_GROUPS:
    {
        if (in->pressed & VITA_BTN_CIRCLE) { sAt = AT_MSG; PlaySE(SE_SELECT); return true; }
        const int32_t before = sGroups.sel;
        const int32_t a = UiList_Update(&sGroups, in, 28, sLowY + LIST_TOP, GROUP_W - 24, ROW_H, true);
        if (sGroups.sel != before) SelectGroup(sGroups.sel);
        if ((a >= 0 || (in->pressed & VITA_BTN_RIGHT)) && sWordCount > 0) { sAt = AT_WORDS; PlaySE(SE_SELECT); }
        return true;
    }
    default:
        if (in->pressed & VITA_BTN_CIRCLE) { sAt = AT_GROUPS; PlaySE(SE_SELECT); return true; }
        if (in->pressed & VITA_BTN_CROSS) { if (sWordCount) PutWord(EasyChat_UiWordAt((u16)sWordSel)); return true; }
        if (dx || dy)
        {
            if (dx < 0 && sWordSel % WORD_COLS == 0) { sAt = AT_GROUPS; PlaySE(SE_SELECT); return true; }
            int32_t n = sWordSel + dx + dy * WORD_COLS;
            if (n < 0) n = 0;
            if (n >= sWordCount) n = sWordCount - 1;
            sWordSel = n;
            if (sWordSel < sWordFirst) sWordFirst = (sWordSel / WORD_COLS) * WORD_COLS;
            if (sWordSel >= sWordFirst + visWords) sWordFirst = (sWordSel / WORD_COLS - sListRows + 1) * WORD_COLS;
            PlaySE(SE_SELECT);
        }
        return true;
    }
}

static void BuildMessage(void)
{
    char buf[96];
    const int32_t max = EasyChat_UiMaxWords(), slotW = SlotW(), slotH = sSlotPitch - 8;
    UiW_Panel(MSG_X, MSG_Y, MSG_W, sMsgH);
    const u16 *p = EasyChat_UiPhrase();
    for (int32_t i = 0; i < max; i++)
    {
        int32_t x, y;
        SlotPos(i, &x, &y);
        const bool on = sAt == AT_MSG && sBtn < 0 && i == sSlot;
        const bool target = sAt != AT_MSG && i == sSlot;
        UiW_Row(x, y, slotW - 8, slotH, on, 0);
        if (target)
            UiDraw_Frame(x, y, slotW - 8, slotH, 3, UI_COL_SELECT);
        WordText(p[i], buf, sizeof(buf));
        const uint32_t ink = p[i] == EC_WORD_UNDEFINED ? UI_COL_INK_SOFT : on ? UI_COL_SELECT_INK : UI_COL_INK;
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + (slotW - 8 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, buf)) / 2,
                    y + (slotH - UI_BODY_PX) / 2, buf, ink);
    }
    for (int32_t b = 0; b < sBtnCount; b++)
    {
        int32_t x, y;
        ButtonPos(b, &x, &y);
        if (sAt == AT_MSG && sBtn == b)
            UiDraw_Rect(x - 4, y - 4, BTN_W + 8, 48, UI_COL_SELECT);
        UiW_Button(x, y, BTN_W, sBtns[b].label, sBtns[b].color, false);
    }
}

static void BuildLists(void)
{
    char buf[96];
    const int32_t lowH = LOW_BOTTOM - sLowY, visWords = WORD_COLS * sListRows;
    UiW_Panel(16, sLowY, GROUP_W, lowH);
    UiW_PanelHead(16, sLowY, GROUP_W, sAlpha ? UI_TR("A-Z") : UI_TR("GROUPS"), NULL);
    for (int32_t r = 0; r < sListRows && sGroups.first + r < sGroupCount; r++)
    {
        const int32_t g = sGroups.first + r, y = sLowY + LIST_TOP + r * ROW_H;
        const bool on = g == sGroups.sel;
        UiW_Row(28, y, GROUP_W - 24, ROW_H, on && sAt == AT_GROUPS, g);
        if (on && sAt != AT_GROUPS)
            UiDraw_Frame(28, y, GROUP_W - 24, ROW_H - 2, 2, UI_COL_SELECT);
        GroupLabel(g, buf, sizeof(buf));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 56, y + 2, buf, on && sAt == AT_GROUPS ? UI_COL_SELECT_INK : UI_COL_INK);
    }
    UiW_ScrollBar(16 + GROUP_W - 12, sLowY + LIST_TOP, sListRows * ROW_H, sGroups.first, sListRows, sGroupCount);

    const int32_t wx = 16 + GROUP_W + 12, ww = (MSG_W - GROUP_W - 12 - 24) / WORD_COLS;
    UiW_Panel(wx, sLowY, MSG_W - GROUP_W - 12, lowH);
    if (sGroupCount > 0)
        GroupLabel(sGroup, buf, sizeof(buf));
    else
        buf[0] = 0;
    char right[24];
    snprintf(right, sizeof(right), UI_TR("%d words"), (int)sWordCount);
    UiW_PanelHead(wx, sLowY, MSG_W - GROUP_W - 12, buf, right);
    for (int32_t k = 0; k < visWords; k++)
    {
        const int32_t i = sWordFirst + k;
        if (i >= sWordCount) break;
        const int32_t x = wx + 12 + (k % WORD_COLS) * ww, y = sLowY + LIST_TOP + (k / WORD_COLS) * ROW_H;
        const bool on = sAt == AT_WORDS && i == sWordSel;
        UiW_Row(x, y, ww - 4, ROW_H, on, k / WORD_COLS);
        WordText(EasyChat_UiWordAt((u16)i), buf, sizeof(buf));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 24, y + 2, buf, on ? UI_COL_SELECT_INK : UI_COL_INK);
    }
    UiW_ScrollBar(wx + MSG_W - GROUP_W - 12 - 12, sLowY + LIST_TOP, sListRows * ROW_H, sWordFirst / WORD_COLS, sListRows,
                  (sWordCount + WORD_COLS - 1) / WORD_COLS);
}

static void BuildDialog(const char *a, const char *b)
{
    UiW_DialogBox(48, UI_SCREEN_H - 24 - 124, UI_SCREEN_W - 96, 124);
    UiText_Draw(UI_FACE_VT323, UI_DIALOG_PX, 76, UI_SCREEN_H - 24 - 112, a, UI_COL_INK);
    UiText_Draw(UI_FACE_VT323, UI_DIALOG_PX, 76, UI_SCREEN_H - 24 - 66, b, UI_COL_INK);
}

static void Build(void)
{
    static const char *const tabs[2] = { UI_N("GROUPS"), UI_N("A-Z") };
    char title[48];
    Text(EasyChat_UiTitle(), title, sizeof(title));
    UiKit_HeaderAs(UI_GBA_BG_POKENAV, UI_SPR_HOLD_MAIL, UI_TR("EASY CHAT"), tabs, 2, sAlpha ? 1 : 0, sTabX, sTabW, 12);
    if (title[0])
        UiW_HeaderValue(title, UI_COL_ON_FRAME);

    BuildMessage();
    int32_t fx = 16;
    BuildLists();
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Choose"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    if (EasyChat_UiCanDeleteWord())
        UiW_FooterGlyph(&fx, UI_GLYPH_TRIANGLE, UI_TR("Delete word"));
    UiW_FooterKey(&fx, "START", UI_TR("OK"));
    UiKit_FooterClock();

    if (sAsk != ASK_NONE)
    {
        UiDraw_Darken(1, 3);
        BuildDialog(sAskText[0], sAskText[1]);
        if (sAsk != ASK_MSG)
            UiMenu_Draw(&sYesNo);
    }
}

const UiScreen gUiEasyChatScreen = { Enter, Update, Build, NULL };
