#ifndef GUARD_PLATFORM_WIDESCREEN_H
#define GUARD_PLATFORM_WIDESCREEN_H

// Vita widescreen: renderer draws 288-px lines with the GBA's 240 px centered.
// Field map layers (BG1-3) and sprites fill the 24 px margins on each side; battle BGs, text
// box and menus stretch cleanly; Group A scenes stay centered 816x544 with black sidebars.
#if defined(VITA_PARALLEL_DRAW)
#define WIDESCREEN_SUPPORTED
#define WIDESCREEN_MARGIN 24
#define WIDESCREEN_WIDTH (DISPLAY_WIDTH + 2 * WIDESCREEN_MARGIN)

#define WIDESCREEN_SCREEN_W 960.0f
#define WIDESCREEN_SCREEN_H 544.0f
#define WIDESCREEN_SCALE (WIDESCREEN_SCREEN_H / (float)DISPLAY_HEIGHT)
#define WIDESCREEN_VISIBLE_U0 \
    (((float)WIDESCREEN_WIDTH - WIDESCREEN_SCREEN_W / WIDESCREEN_SCALE) * 0.5f / (float)WIDESCREEN_WIDTH)
#define WIDESCREEN_VISIBLE_U1 (1.0f - WIDESCREEN_VISIBLE_U0)
// Screen x where the GBA's 240 px start and end under that mapping (72 and 888).
#define WIDESCREEN_PICTURE_X0 ((WIDESCREEN_SCREEN_W - (float)DISPLAY_WIDTH * WIDESCREEN_SCALE) * 0.5f)
#define WIDESCREEN_PICTURE_X1 (WIDESCREEN_SCREEN_W - WIDESCREEN_PICTURE_X0)

bool8 Widescreen_IsOn(void);
#define WIDESCREEN_EXTRA_X (Widescreen_IsOn() ? WIDESCREEN_MARGIN : 0)

void Widescreen_SetFieldFrame(void);
void Widescreen_SetBattleFrame(bool8 introSlide);

// How a text BG fills the margins.
enum
{
    WIDE_BG_CENTRED,
    WIDE_BG_WIDE,
    WIDE_BG_EDGES,
    WIDE_BG_AUTO,
    WIDE_BG_FILL,
    WIDE_BG_EDGE_TILE,
};

bool8 Widescreen_StageFadesMargins(void);
void Widescreen_PrepareDraw(void);
void Widescreen_OnReset(void);
#endif

#endif // GUARD_PLATFORM_WIDESCREEN_H
