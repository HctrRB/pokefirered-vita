#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "ui_town_signs.h"
#include "platform/ui.h"

#define SIGN_SLIDE_FRAMES 14
#define SIGN_HOLD_FRAMES  136
#define SIGN_TOTAL_FRAMES (SIGN_SLIDE_FRAMES * 2 + SIGN_HOLD_FRAMES)
#define FLOOR_ROOFTOP     127

static bool sActive;
static int32_t sTimer;
static char sTitle[48];
static const char *sMotto;

static void FormatSignTitle(const uint16_t mapSec, const int8_t floorNum)
{
    char baseName[32];
    UiGame_GetMapSectionName(mapSec, baseName, sizeof(baseName));
    if (floorNum == 0)
        snprintf(sTitle, sizeof(sTitle), "%s", baseName);
    else if (floorNum == FLOOR_ROOFTOP)
        snprintf(sTitle, sizeof(sTitle), "%s ROOFTOP", baseName);
    else if (floorNum < 0)
        snprintf(sTitle, sizeof(sTitle), "%s B%dF", baseName, (int)(-floorNum));
    else
        snprintf(sTitle, sizeof(sTitle), "%s %dF", baseName, (int)floorNum);
}

void Ui_ShowZoneSign(const uint16_t mapSec, const int8_t floorNum)
{
    FormatSignTitle(mapSec, floorNum);
    sMotto = (floorNum == 0) ? Ui_TownMotto(mapSec) : NULL;
    if (sActive && sTimer > SIGN_SLIDE_FRAMES && sTimer < SIGN_SLIDE_FRAMES + SIGN_HOLD_FRAMES)
        sTimer = SIGN_SLIDE_FRAMES;
    else
        sTimer = 0;
    sActive = true;
}

void Ui_HideZoneSign(void)
{
    sActive = false;
    sTimer = 0;
}

void UiSign_Reset(void)
{
    sActive = false;
    sTimer = 0;
    sTitle[0] = '\0';
    sMotto = NULL;
}

bool UiSign_Draw(void)
{
    if (!sActive || sTitle[0] == '\0')
        return false;

    sTimer++;
    if (sTimer >= SIGN_TOTAL_FRAMES)
    {
        sActive = false;
        return false;
    }

    const bool hasMotto = (sMotto != NULL && sMotto[0] != '\0');
    const int32_t titleW = UiText_Width(UI_FACE_VT323, UI_BODY_PX, sTitle);
    const int32_t mottoW = hasMotto ? UiText_Width(UI_FACE_VT323, 24, sMotto) : 0;
    const int32_t contentW = (titleW > mottoW) ? titleW : mottoW;
    int32_t boxW = contentW + 44;
    if (boxW < 256)
        boxW = 256;
    if (boxW > 560)
        boxW = 560;
    const int32_t boxH = hasMotto ? 78 : 54;

    int32_t y = 16;
    if (sTimer < SIGN_SLIDE_FRAMES)
    {
        const int32_t rem = SIGN_SLIDE_FRAMES - sTimer;
        y = 16 - (rem * (boxH + 20)) / SIGN_SLIDE_FRAMES;
    }
    else if (sTimer >= SIGN_SLIDE_FRAMES + SIGN_HOLD_FRAMES)
    {
        const int32_t outT = sTimer - (SIGN_SLIDE_FRAMES + SIGN_HOLD_FRAMES);
        y = 16 - (outT * (boxH + 20)) / SIGN_SLIDE_FRAMES;
    }

    const int32_t x = 20;
    if (gUiTheme->gbaFrame)
    {
        UiW_GbaWin(x, y, boxW, boxH, hasMotto ? UI_GBA_WIN_GOLD : UI_GBA_WIN_DEFAULT);
        UiDraw_Rect(x + 8, y + 8, 4, boxH - 16, gUiTheme->accent);
        if (hasMotto)
        {
            UiW_GbaText(UI_FACE_VT323, UI_BODY_PX, x + 20, y + 8, sTitle, gUiTheme->ink, UI_GBA_SHAD_LIGHT);
            UiW_GbaText(UI_FACE_VT323, 24, x + 20, y + 40, sMotto, gUiTheme->inkSoft, 0);
        }
        else
        {
            UiW_GbaTextMid(UI_FACE_VT323, UI_BODY_PX, x + 20, y, boxH, sTitle, gUiTheme->ink, UI_GBA_SHAD_LIGHT);
        }
    }
    else
    {
        UiW_Panel(x, y, boxW, boxH);
        UiDraw_Rect(x + 6, y + 6, 4, boxH - 12, gUiTheme->accent);
        if (hasMotto)
        {
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 18, y + 8, sTitle, gUiTheme->ink);
            UiText_Draw(UI_FACE_VT323, 24, x + 18, y + 40, sMotto, gUiTheme->inkSoft);
        }
        else
        {
            UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, x + 18, y, boxH, sTitle, gUiTheme->ink);
        }
    }
    return true;
}
