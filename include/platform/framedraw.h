#ifndef GUARD_FRAMEDRAW_H
#define GUARD_FRAMEDRAW_H

#include "global.h"
#include "platform/widescreen.h"

#ifdef WIDESCREEN_SUPPORTED
// Frame lines are FRAME_LINE_WIDTH pixels; the GBA screen starts at x = FRAME_LINE_X0.
#define FRAME_LINE_WIDTH WIDESCREEN_WIDTH
#define FRAME_LINE_X0 WIDESCREEN_MARGIN

// How the frame being drawn uses the widescreen margins (set by Widescreen_PrepareDraw).
struct FastDrawWide
{
    bool8 output;       // lines are drawn and shown 288 px wide (else only the middle 240 px)
    bool8 extend;       // some layers fill the margins this frame
    u8 bgSpan[4];       // how each text BG fills the margins: WIDE_BG_*
    bool8 extendObj;    // sprites fill the margins
    u16 *bgMaps[4];     // RAM tilemaps (GBA layout) drawn instead of the BG's VRAM map, or NULL
    u8 bgMapBase[4];    // ...only while the BG still uses this screen base block
    u8 bgMapSize[4];    // BGCNT size of bgMaps: 1 = 64x32 tiles, 3 = 64x64
    u16 *bgFillMaps[4]; // WIDE_BG_FILL: a 32x32 map of the fill tile, drawn in the margins
};
extern struct FastDrawWide gFastDrawWide;
#else
#define FRAME_LINE_WIDTH DISPLAY_WIDTH
#define FRAME_LINE_X0 0
#endif

void DrawFrame(uint16_t *pixels);
extern int32_t gFastDrawBackdropOverride;
extern bool8 gFastDrawScreenObjOnly;
extern u16 gFastDrawDispcntClear;
extern u16 gFastDrawBldcntClear;
extern bool8 gFastDrawCentredOnly;
void GbaEngine_StepVCount(void);
void GbaEngine_StepHBlankScanlines(void);
void FastDraw_OnReset(void);
u16 FastDraw_GetLineBldAlpha(void);
#if defined(VITA_PARALLEL_DRAW)
void DrawFrameToTexture(uint16_t *pixels, void *texture, int32_t pitch);
#endif
#endif
