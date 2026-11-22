// Components of the 960x544 native UI (ported 1:1 from pokeemerald-vita-3ds with FireRed GBA palette).
// Everything is rectangles from the atlas white block (with vertical two-colour gradients),
// glyphs and sprites, so a whole screen stays one draw call.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

#define HEADER_H 48
#define FOOTER_H 40

#define T gUiTheme
#define C_SHADOW      UI_RGBA(0, 0, 0, 102)
#ifndef C_WHITE
#define C_WHITE       UI_RGBA(255, 255, 255, 255)
#endif
#define C_GLOSS       UI_RGBA(255, 255, 255, 46)

// FireRed GBA frame palette (type1 indigo-slate border + warm charcoal outline, zero Emerald greens)
#define GBA_CHARCOAL  UI_RGBA(41, 49, 49, 255)
#define GBA_FR_HI     UI_RGBA(139, 139, 205, 255)
#define GBA_FR_MID    UI_RGBA(74, 74, 118, 255)
#define GBA_DARK_SH   UI_RGBA(20, 26, 40, 255)
#define GBA_YELLOW    UI_RGBA(255, 213, 82, 255)
#define GBA_TOPBAR_H  52

// ---------- Shape primitives (rounded or chamfered corners, from the theme) ----------

static int32_t CornerInset(const int32_t r, const int32_t k, const bool chamfer)
{
    if (chamfer)
        return r - k;
    const float d = (float)r - (float)k - 0.5f;
    const float in = (float)r - sqrtf((float)(r * r) - d * d);
    return (int32_t)(in + 0.5f);
}

static void ShapeFill(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                      const int32_t r, const bool chamfer, const uint32_t top, const uint32_t bottom)
{
    if (w <= 0 || h <= 0)
        return;
    if (r <= 0)
    {
        if (top == bottom)
            UiDraw_Rect(x, y, w, h, top);
        else
            UiDraw_RectV(x, y, w, h, top, bottom);
        return;
    }
    if (UiDraw_IsClipped())
    {
        for (int32_t k = 0; k < r; k++)
        {
            const int32_t in = CornerInset(r, k, chamfer);
            UiDraw_Rect(x + in, y + k, w - 2 * in, 1, top);
            UiDraw_Rect(x + in, y + h - 1 - k, w - 2 * in, 1, bottom);
        }
    }
    else if (chamfer || r <= 4)
    {
        const float tx[4] = { (float)(x + r), (float)(x + w - r), (float)(x + w), (float)x };
        const float ty[4] = { (float)y, (float)y, (float)(y + r), (float)(y + r) };
        UiDraw_Poly4(tx, ty, top);
        const float bx[4] = { (float)x, (float)(x + w), (float)(x + w - r), (float)(x + r) };
        const float by[4] = { (float)(y + h - r), (float)(y + h - r), (float)(y + h), (float)(y + h) };
        UiDraw_Poly4(bx, by, bottom);
    }
    else
    {
        const int32_t mid = r / 2;
        const int32_t inMid = CornerInset(r, mid, false);
        const float tx0[4] = { (float)(x + r), (float)(x + w - r), (float)(x + w - inMid), (float)(x + inMid) };
        const float ty0[4] = { (float)y, (float)y, (float)(y + mid), (float)(y + mid) };
        UiDraw_Poly4(tx0, ty0, top);
        const float tx1[4] = { (float)(x + inMid), (float)(x + w - inMid), (float)(x + w), (float)x };
        const float ty1[4] = { (float)(y + mid), (float)(y + mid), (float)(y + r), (float)(y + r) };
        UiDraw_Poly4(tx1, ty1, top);

        const float bx0[4] = { (float)x, (float)(x + w), (float)(x + w - inMid), (float)(x + inMid) };
        const float by0[4] = { (float)(y + h - r), (float)(y + h - r), (float)(y + h - mid), (float)(y + h - mid) };
        UiDraw_Poly4(bx0, by0, bottom);
        const float bx1[4] = { (float)(x + inMid), (float)(x + w - inMid), (float)(x + w - r), (float)(x + r) };
        const float by1[4] = { (float)(y + h - mid), (float)(y + h - mid), (float)(y + h), (float)(y + h) };
        UiDraw_Poly4(bx1, by1, bottom);
    }
    if (top == bottom)
        UiDraw_Rect(x, y + r, w, h - 2 * r, top);
    else
        UiDraw_RectV(x, y + r, w, h - 2 * r, top, bottom);
}

static void BevelBoxR(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                      int32_t r, const bool chamfer, const int32_t t, const uint32_t edge,
                      const uint32_t top, const uint32_t bottom, const int32_t b,
                      const uint32_t hi, const uint32_t shade, const int32_t sx, const int32_t sy,
                      const uint32_t shadowCol, const bool gloss)
{
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    if (sx > 0 || sy > 0)
        ShapeFill(x + sx, y + sy, w, h, r, chamfer, shadowCol, shadowCol);
    if (t > 0)
        ShapeFill(x, y, w, h, r, chamfer, edge, edge);
    const int32_t fx = x + t, fy = y + t, fw = w - 2 * t, fh = h - 2 * t;
    const int32_t fr = r > t ? r - t : 0;
    ShapeFill(fx, fy, fw, fh, fr, chamfer, top, bottom);
    if (b > 0)
    {
        UiDraw_Rect(fx + fr, fy, fw - 2 * fr, b, hi);
        UiDraw_Rect(fx, fy + fr, b, fh - 2 * fr, hi);
        UiDraw_Rect(fx + fr, fy + fh - b, fw - 2 * fr, b, shade);
        UiDraw_Rect(fx + fw - b, fy + fr, b, fh - 2 * fr, shade);
    }
    if (gloss && fh > 6)
        UiDraw_Rect(fx + fr, fy + 1, fw - 2 * fr, fh / 2 - 1, C_GLOSS);
}

static void BevelBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                     const int32_t t, const uint32_t edge, const uint32_t top, const uint32_t bottom,
                     const int32_t b, const uint32_t hi, const uint32_t shade, const int32_t shadow)
{
    int32_t r = T->radius;
    if (h < 12) r = 0;
    else if (h < 24 && r > 3) r = T->chamfer ? 4 : 3;
    const int32_t sy = shadow > 0 ? (shadow * T->shadow + 3) / 4 : 0;
    const int32_t sx = shadow > 0 ? (shadow * T->shadowR + 3) / 4 : 0;
    BevelBoxR(x, y, w, h, r, T->chamfer, t, edge, top, bottom, T->bevel ? b : 0, hi, shade, sx, sy, T->shadowCol, false);
}

static void Gloss(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const int32_t t)
{
    if (!T->gloss || h <= 2 * t + 6)
        return;
    const int32_t r = T->radius > t ? T->radius - t : 0;
    const int32_t rr = r > (h - 2 * t) / 2 ? (h - 2 * t) / 2 : r;
    UiDraw_Rect(x + t + rr, y + t + 1, w - 2 * t - 2 * rr, (h - 2 * t) / 2 - 1, C_GLOSS);
}

// ---------- Bevel primitives ----------

void UiW_Bevel(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
               const int32_t t, const uint32_t edge, const uint32_t top, const uint32_t bottom,
               const int32_t b, const uint32_t hi, const uint32_t shade, const int32_t shadow)
{
    const int32_t r = h >= 24 ? 6 : h >= 12 ? 3 : 0;
    BevelBoxR(x, y, w, h, r, false, t, edge, top, bottom, b, hi, shade, 0, shadow, C_SHADOW, false);
}

void UiW_ThemeBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                  const int32_t t, const uint32_t edge, const uint32_t top, const uint32_t bottom,
                  const uint32_t hi, const uint32_t shade, const int32_t shadow, const bool pill, const uint32_t ring)
{
    int32_t r = T->radius;
    if (pill) r = h / 2;
    else if (h < 12) r = 0;
    else if (h < 24 && r > 3) r = T->chamfer ? 4 : 3;
    const bool chamfer = T->chamfer && !pill;
    const int32_t sy = shadow > 0 ? (shadow * T->shadow + 3) / 4 : 0;
    const int32_t sx = shadow > 0 ? (shadow * T->shadowR + 3) / 4 : 0;
    if (ring)
        BevelBoxR(x - 3, y - 3, w + 6, h + 6, r + 3, chamfer, 0, 0, ring, ring, 0, 0, 0, sx, sy, T->shadowCol, false);
    BevelBoxR(x, y, w, h, r, chamfer, t, edge, top, bottom, T->bevel ? 2 : 0, hi, shade,
              ring ? 0 : sx, ring ? 0 : sy, T->shadowCol, false);
    if (T->gloss && h > 2 * t + 6)
    {
        const int32_t fr = r > t ? r - t : 0;
        const int32_t rr = fr > (h - 2 * t) / 2 ? (h - 2 * t) / 2 : fr;
        UiDraw_Rect(x + t + rr, y + t + 1, w - 2 * t - 2 * rr, (h - 2 * t) / 2 - 1, C_GLOSS);
    }
}

static void GbaFrameBoxPal(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                           const uint32_t outerEdge, const uint32_t midRing, const uint32_t innerRing,
                           const uint32_t innerEdge, const uint32_t top, const uint32_t bot,
                           const uint32_t shade, const int32_t shadow)
{
    if (w <= 0 || h <= 0)
        return;
    int32_t r = 6;
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    if (shadow > 0)
        ShapeFill(x, y + shadow, w, h, r, false, UI_RGBA(0, 0, 0, 92), UI_RGBA(0, 0, 0, 92));
    ShapeFill(x, y, w, h, r, false, outerEdge, outerEdge);
    if (w > 4 && h > 4)
    {
        int32_t r2 = r > 1 ? r - 1 : 0;
        if (r2 > (h - 4) / 2) r2 = (h - 4) / 2;
        if (r2 > (w - 4) / 2) r2 = (w - 4) / 2;
        ShapeFill(x + 2, y + 2, w - 4, h - 4, r2, false, midRing, midRing);
    }
    if (w > 8 && h > 8)
    {
        int32_t r1 = r > 2 ? r - 2 : 0;
        if (r1 > (h - 8) / 2) r1 = (h - 8) / 2;
        if (r1 > (w - 8) / 2) r1 = (w - 8) / 2;
        ShapeFill(x + 4, y + 4, w - 8, h - 8, r1, false, innerRing, innerRing);
    }
    if (w > 10 && h > 10)
    {
        int32_t r0 = r > 3 ? r - 3 : 0;
        if (r0 > (h - 10) / 2) r0 = (h - 10) / 2;
        if (r0 > (w - 10) / 2) r0 = (w - 10) / 2;
        ShapeFill(x + 5, y + 5, w - 10, h - 10, r0, false, innerEdge, innerEdge);
    }
    const int32_t ix = x + 6, iy = y + 6, iw = w - 12, ih = h - 12;
    if (iw <= 0 || ih <= 0)
        return;
    int32_t ri = r > 4 ? r - 4 : 0;
    if (ri > ih / 2) ri = ih / 2;
    if (ri > iw / 2) ri = iw / 2;
    if (iw > 6 && ih > 6 && shade != 0)
    {
        ShapeFill(ix, iy, iw, ih, ri, false, shade, shade);
        ShapeFill(ix + 2, iy + 2, iw - 4, ih - 4, ri > 1 ? ri - 1 : 0, false, top, bot);
    }
    else
    {
        ShapeFill(ix, iy, iw, ih, ri, false, top, bot);
    }
}

static void GbaFrameBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                        const uint32_t top, const uint32_t bot, const uint32_t shade, const int32_t shadow)
{
    GbaFrameBoxPal(x, y, w, h, GBA_CHARCOAL, GBA_FR_HI, GBA_FR_MID, GBA_CHARCOAL, top, bot, shade, shadow);
}

void UiW_DialogBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    if (T->gbaFrame)
    {
        UiW_GbaMsg(x, y, w, h, false, false);
        return;
    }
    BevelBox(x, y, w, h, T->edge > 3 ? T->edge : 3, T->dlgEdge, T->dlgTop, T->dlgBot, 2, C_WHITE, T->dlgShade, 6);
    if (T->accent)
        UiDraw_Rect(x, y + (T->chamfer ? T->radius : 0), 8, h - (T->chamfer ? 2 * T->radius : 0), T->accent);
}

void UiW_Inset(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    if (T->gbaFrame)
    {
        UiW_GbaBox(x, y, w, h, 3, UI_RGBA(144, 154, 190, 255), UI_RGBA(246, 246, 238, 255),
                   UI_RGBA(246, 246, 238, 255), UI_GBA_SHAD_LIGHT, 0);
        return;
    }
    BevelBox(x, y, w, h, 2, T->paperShade, T->paper1, T->paper1, 2, T->paperShade, C_WHITE, 0);
}

static void KeyCap(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    if (T->bevel || T->gloss)
    {
        const int32_t r = T->radius >= 10 ? (h >= 40 ? 10 : 6) : 0;
        if (r > 0)
        {
            BevelBoxR(x, y, w, h, r, T->chamfer, 2, T->keyLight, T->keyTop, T->keyBot, 0, 0, 0, 0, 2, C_SHADOW, T->gloss);
            return;
        }
        UiDraw_Rect(x + 1, y + h, w - 2, 2, C_SHADOW);
        UiDraw_RectV(x, y, w, h, T->keyTop, T->keyBot);
        UiDraw_Rect(x, y, w, 2, T->keyLight);
        UiDraw_Rect(x, y, 2, h, T->keyLight);
        UiDraw_Rect(x, y + h - 2, w, 2, T->keyDark);
        UiDraw_Rect(x + w - 2, y, 2, h, T->keyDark);
        return;
    }
    const int32_t r = T->chamfer ? 4 : (T->radius >= 10 ? h / 2 : 3);
    const int32_t t = T->edge >= 4 ? 3 : 0;
    BevelBoxR(x, y, w, h, r, T->chamfer, t, T->keyDark, T->keyTop, T->keyBot, 0, 0, 0, 0, t ? 2 : 0, T->keyDark, false);
}

static int32_t KeyEx(const int32_t x, const int32_t y, const char *const label, const bool small)
{
    const int32_t w = (small ? UiText_Width(UI_FACE_PS2P, UI_FOOT_PX, label) + 12 : UiText_Width(UI_FACE_VT323, UI_BODY_PX, label) + 14);
    KeyCap(x, y, w, 24);
    if (small)
        UiText_DrawMid(UI_FACE_PS2P, UI_FOOT_PX, x + 6, y, 24, label, T->keyInk);
    else
        UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, x + 7, y, 24, label, T->keyInk);
    return w;
}

static int32_t Key(const int32_t x, const int32_t y, const char *const label)
{
    return KeyEx(x, y, label, false);
}

static void Disc(const int32_t cx, const int32_t cy, const int32_t r, const uint32_t colour)
{
    int32_t runStart = 0, runHw = -1;
    for (int32_t dy = -r; dy <= r; dy++)
    {
        int32_t hw = 0;
        while ((hw + 1) * (hw + 1) + dy * dy <= r * r + r)
            hw++;
        if (hw != runHw)
        {
            if (runHw >= 0)
                UiDraw_Rect(cx - runHw, cy + runStart, runHw * 2 + 1, dy - runStart, colour);
            runStart = dy;
            runHw = hw;
        }
    }
    UiDraw_Rect(cx - runHw, cy + runStart, runHw * 2 + 1, r + 1 - runStart, colour);
}

void UiW_Glyph(const int32_t x, const int32_t y, const uint8_t kind)
{
    static const uint32_t ring[4] = { UI_RGBA(91, 127, 214, 255), UI_RGBA(214, 81, 81, 255), UI_RGBA(47, 167, 126, 255), UI_RGBA(194, 102, 168, 255) };
    static const uint32_t ink[4]  = { UI_RGBA(143, 180, 255, 255), UI_RGBA(255, 122, 122, 255), UI_RGBA(95, 224, 176, 255), UI_RGBA(244, 155, 216, 255) };
    const int32_t cx = x + 12, cy = y + 12;
    Disc(cx, cy + 2, 12, C_SHADOW);
    Disc(cx, cy, 12, ring[kind & 3]);
    Disc(cx, cy, 10, UI_RGBA(27, 30, 36, 255));
    const uint32_t c = ink[kind & 3];
    switch (kind & 3)
    {
    case UI_GLYPH_CROSS:
        for (int32_t i = -4; i <= 3; i++)
        {
            UiDraw_Rect(cx + i, cy + i, 2, 2, c);
            UiDraw_Rect(cx + i, cy - i - 1, 2, 2, c);
        }
        break;
    case UI_GLYPH_CIRCLE:
        Disc(cx, cy, 5, c);
        Disc(cx, cy, 3, UI_RGBA(27, 30, 36, 255));
        break;
    case UI_GLYPH_TRIANGLE:
        for (int32_t i = 0; i < 8; i++)
        {
            const int32_t half = i / 2 + 1;
            if (i == 7 || i == 0)
            {
                UiDraw_Rect(cx - half, cy - 4 + i, half * 2, 1, c);
            }
            else
            {
                UiDraw_Rect(cx - half, cy - 4 + i, 2, 1, c);
                UiDraw_Rect(cx + half - 2, cy - 4 + i, 2, 1, c);
            }
        }
        break;
    default:
        UiDraw_Frame(cx - 4, cy - 4, 9, 9, 2, c);
        break;
    }
}

// ---------- Frame, header, footer ----------

void UiW_FullFrame(void)
{
    UiDraw_Rect(0, HEADER_H, UI_SCREEN_W, UI_SCREEN_H - HEADER_H - FOOTER_H, UI_COL_FRAME0_A);
    if (T->gbaFrame)
    {
        for (int32_t gy = HEADER_H; gy < UI_SCREEN_H - FOOTER_H; gy += 16)
            UiDraw_Rect(0, gy, UI_SCREEN_W, 1, UI_RGBA(164, 188, 240, 20));
        for (int32_t gx = 0; gx < UI_SCREEN_W; gx += 16)
            UiDraw_Rect(gx, HEADER_H, 1, UI_SCREEN_H - HEADER_H - FOOTER_H, UI_RGBA(164, 188, 240, 14));
    }
    else if (T->stripes)
    {
        const float top = HEADER_H, bot = UI_SCREEN_H - FOOTER_H, hgt = bot - top;
        UiDraw_PushClip(0, HEADER_H, UI_SCREEN_W, UI_SCREEN_H - HEADER_H - FOOTER_H);
        for (int32_t sx = -(int32_t)hgt; sx < UI_SCREEN_W; sx += 28)
        {
            const float xs[4] = { (float)sx + hgt, (float)sx + hgt + 14.0f, (float)sx + 14.0f, (float)sx };
            const float ys[4] = { top, top, bot, bot };
            UiDraw_Poly4(xs, ys, UI_RGBA(255, 255, 255, 10));
        }
        UiDraw_PopClip();
    }
    UiDraw_RectV(0, 0, UI_SCREEN_W, HEADER_H - 2, T->barTop, T->barBot);
    if (T->gloss)
        UiDraw_Rect(0, 1, UI_SCREEN_W, HEADER_H / 2 - 2, C_GLOSS);
    UiDraw_Rect(0, HEADER_H - 2, UI_SCREEN_W, 2, UI_COL_LINE_DARK);
    if (T->gbaFrame)
    {
        UiDraw_Rect(0, HEADER_H, UI_SCREEN_W, 1, GBA_CHARCOAL);
        UiDraw_Rect(0, HEADER_H + 1, UI_SCREEN_W, 2, C_SHADOW);
        UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H - 3, UI_SCREEN_W, 2, C_SHADOW);
        UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H - 1, UI_SCREEN_W, 1, GBA_CHARCOAL);
    }
    else
    {
        UiDraw_Rect(0, HEADER_H, UI_SCREEN_W, 2, C_SHADOW);
        UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H - 2, UI_SCREEN_W, 2, C_SHADOW);
    }
    UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H, UI_SCREEN_W, 2, UI_COL_LINE_DARK);
    UiDraw_RectV(0, UI_SCREEN_H - FOOTER_H + 2, UI_SCREEN_W, FOOTER_H - 2, T->barBot, T->barTop);
}

void UiW_FooterBar(void)
{
    if (T->gbaFrame)
    {
        UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H - 3, UI_SCREEN_W, 2, C_SHADOW);
        UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H - 1, UI_SCREEN_W, 1, GBA_CHARCOAL);
    }
    else
    {
        UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H - 2, UI_SCREEN_W, 2, C_SHADOW);
    }
    UiDraw_Rect(0, UI_SCREEN_H - FOOTER_H, UI_SCREEN_W, 2, UI_COL_LINE_DARK);
    UiDraw_RectV(0, UI_SCREEN_H - FOOTER_H + 2, UI_SCREEN_W, FOOTER_H - 2, T->barBot, T->barTop);
}

int32_t UiW_Capsule(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    BevelBox(x, y, w, h, 2, T->capEdge, T->capTop, T->capBot, 1, T->capHi, T->capBot, 2);
    Gloss(x, y, w, h, 2);
    return w;
}

static void FitBarFace(const char *const text, const int32_t maxW, uint8_t *const face, uint8_t *const px)
{
    *face = UI_FACE_PS2P;
    *px = 16;
    if (text == NULL || UiText_Width(UI_FACE_PS2P, 16, text) <= maxW)
        return;
    *face = UI_FACE_VT323;
    *px = UiText_Width(UI_FACE_VT323, 28, text) <= maxW ? 28 : 22;
}

static void DrawGbaSectionPill(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                               const bool lens, const uint16_t iconSpr, const char *const title,
                               const uint32_t border, const uint32_t bgTop, const uint32_t bgBot,
                               const uint32_t ink, const bool textShadow, const bool barAccent)
{
    BevelBoxR(x, y, w, h, 5, false, 2, border, bgTop, bgBot, 1, UI_RGBA(255, 255, 255, 96),
              UI_RGBA(0, 0, 0, 56), 0, 2, UI_RGBA(0, 0, 0, 120), false);
    if (barAccent)
        UiDraw_Rect(x + 8, y + (h - 18) / 2, 4, 18, GBA_YELLOW);
    if (lens)
    {
        const int32_t lcx = x + 26, lcy = y + h / 2;
        Disc(lcx, lcy, 10, UI_RGBA(40, 56, 64, 255));
        Disc(lcx, lcy, 8, UI_RGBA(0, 88, 168, 255));
        Disc(lcx - 1, lcy - 1, 5, UI_RGBA(56, 184, 248, 255));
        UiDraw_Rect(lcx - 3, lcy - 4, 3, 3, C_WHITE);
    }
    else if (iconSpr != 0)
    {
        uint16_t sx, sy, sw, sh;
        if (UiAssets_Sprite(iconSpr, &sx, &sy, &sw, &sh))
        {
            const int32_t drawW = sw > 24 ? 22 : (int32_t)sw;
            const int32_t drawH = sh > 24 ? 22 : (int32_t)sh;
            UiDraw_Quad(x + 16, y + (h - drawH) / 2, drawW, drawH, sx, sy, sx + sw, sy + sh, C_WHITE);
        }
    }
    const int32_t tx = x + 42;
    uint8_t face, px;
    FitBarFace(title, w - 50, &face, &px);
    UiDraw_PushClip(tx, y, w - 50, h);
    if (textShadow)
        UiText_DrawMid(face, px, tx + 1, y + 1, h, title, UI_RGBA(24, 32, 32, 200));
    UiText_DrawMid(face, px, tx, y, h, title, ink);
    UiDraw_PopClip();
}

void UiW_HeaderTitle(const char *const title)
{
    if (T->gbaFrame)
    {
        DrawGbaSectionPill(12, 8, 244, 32, false, UI_SPR_BALL, title, GBA_CHARCOAL,
                           UI_RGBA(213, 65, 41, 255), UI_RGBA(156, 36, 24, 255), C_WHITE, true, true);
        return;
    }
    const int32_t w = UiText_Width(UI_FACE_PS2P, 16, title) + 48;
    UiW_Capsule(16, 8, w, 32);
    UiDraw_Sprite(UI_SPR_BALL, 24, 16, 1);
    UiText_DrawMid(UI_FACE_PS2P, 16, 48, 8, 32, title, T->title);
}

// ---------- The GBA page (UiKit_HeaderAs in the GBA theme) ----------
#define GBA_PAGE_BADGES 8
typedef struct
{
    char key[8];
    char label[24];
    bool gold;
} GbaPageBadge;

static bool sGbaPage;
static int32_t sGbaPageValueX;
static GbaPageBadge sGbaBadges[GBA_PAGE_BADGES];
static int32_t sGbaBadgeCount;
static char sGbaValue[40];
static uint32_t sGbaValueColour;

void UiW_GbaPageReset(void)
{
    sGbaPage = false;
    sGbaBadgeCount = 0;
    sGbaValue[0] = 0;
}

void UiW_GbaPageStart(const int32_t valueX)
{
    UiW_GbaPageReset();
    sGbaPage = true;
    sGbaPageValueX = valueX;
}

static void QueueGbaBadge(const char *const key, const char *const label, const bool gold)
{
    if (sGbaBadgeCount >= GBA_PAGE_BADGES)
        return;
    GbaPageBadge *const b = &sGbaBadges[sGbaBadgeCount++];
    snprintf(b->key, sizeof(b->key), "%s", key);
    snprintf(b->label, sizeof(b->label), "%s", label ? label : "");
    b->gold = gold;
}

static int32_t PageBadge(int32_t *const rx, const GbaPageBadge *const b, const uint8_t labelPx, const bool draw)
{
    const int32_t capW = UiW_GbaKeyCapWidth(b->key, 24);
    const int32_t actW = (labelPx && b->label[0]) ? UiText_Width(UI_FACE_VT323, labelPx, b->label) : 0;
    const int32_t totalW = capW + (actW > 0 ? 5 + actW : 0);
    if (draw)
    {
        const int32_t x = *rx - totalW;
        UiW_GbaKeyCap(x, 14, 24, b->key, b->gold);
        if (actW > 0)
        {
            UiText_DrawMid(UI_FACE_VT323, labelPx, x + capW + 6, 15, 24, b->label, UI_RGBA(16, 20, 22, 220));
            UiText_DrawMid(UI_FACE_VT323, labelPx, x + capW + 5, 14, 24, b->label, C_WHITE);
        }
        *rx = x - 12;
    }
    return totalW + 12;
}

static int32_t PageBadges(const uint8_t labelPx, const bool draw)
{
    int32_t rx = UI_SCREEN_W - 14, total = 0;
    for (int32_t pass = 0; pass < 2; pass++)
        for (int32_t i = sGbaBadgeCount - 1; i >= 0; i--)
            if ((strcmp(sGbaBadges[i].key, "O") == 0) == (pass == 0))
                total += PageBadge(&rx, &sGbaBadges[i], labelPx, draw);
    return total;
}

void UiW_GbaPageFlush(void)
{
    if (!sGbaPage)
        return;
    sGbaPage = false;
    const int32_t avail = UI_SCREEN_W - 14 - sGbaPageValueX;
    const int32_t valueW = sGbaValue[0] ? UiText_Width(UI_FACE_VT323, 22, sGbaValue) + 24 + 10 : 0;
    static const uint8_t kLevels[3] = { 24, 20, 0 };
    uint8_t labelPx = 0;
    for (int32_t l = 0; l < 3; l++)
    {
        labelPx = kLevels[l];
        if (PageBadges(labelPx, false) + valueW <= avail)
            break;
    }
    const int32_t used = PageBadges(labelPx, true);
    if (sGbaValue[0])
    {
        const int32_t right = UI_SCREEN_W - 14 - used - 10, room = right - sGbaPageValueX;
        const uint8_t px = UiText_Width(UI_FACE_VT323, 28, sGbaValue) + 24 <= room ? 28 : 22;
        const int32_t w = UiText_Width(UI_FACE_VT323, px, sGbaValue) + 24;
        if (w <= room)
        {
            const int32_t bx = right - w;
            BevelBoxR(bx, 8, w, 34, 5, false, 2, UI_RGBA(20, 26, 40, 255), UI_RGBA(28, 36, 56, 255),
                      UI_RGBA(22, 28, 46, 255), 1, UI_RGBA(96, 112, 160, 255), UI_RGBA(12, 16, 24, 200), 0, 0, 0, false);
            UiText_DrawMid(UI_FACE_VT323, px, bx + 12, 8, 34, sGbaValue, sGbaValueColour);
        }
    }
    sGbaBadgeCount = 0;
    sGbaValue[0] = 0;
}

void UiW_HeaderValue(const char *const text, const uint32_t colour)
{
    if (sGbaPage)
    {
        snprintf(sGbaValue, sizeof(sGbaValue), "%s", text);
        sGbaValueColour = colour == UI_COL_ON_FRAME ? C_WHITE : colour;
        return;
    }
    const int32_t w = UiText_Width(UI_FACE_VT323, UI_BODY_PX, text) + 24;
    UiW_Capsule(UI_SCREEN_W - 16 - w, 8, w, 32);
    UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, UI_SCREEN_W - 16 - w + 12, 8, 32, text, colour);
}

int32_t UiW_TabsPad(const int32_t x, const char *const *const labels, const int32_t count,
                    const int32_t current, int32_t *const tabX, int32_t *const tabW, const int32_t pad)
{
    int32_t total = 4;
    total += UiText_Width(UI_FACE_VT323, UI_BODY_PX, "L") + 14 + 4;
    for (int32_t i = 0; i < count; i++)
        total += UiText_Width(UI_FACE_VT323, UI_BODY_PX, UiLocale_Lookup(labels[i])) + 2 * pad + 2;
    total += UiText_Width(UI_FACE_VT323, UI_BODY_PX, "R") + 14 + 4;

    if (T->gbaFrame)
    {
        BevelBoxR(x, 6, total, 36, 4, false, 2, UI_RGBA(20, 26, 40, 255), UI_RGBA(28, 36, 56, 255),
                  UI_RGBA(22, 28, 46, 255), 0, 0, 0, 0, 0, 0, false);
    }
    else
    {
        const int32_t r = T->chamfer ? 6 : T->radius >= 10 ? 12 : 1;
        BevelBoxR(x, 6, total, 36, r, T->chamfer, 2, T->railEdge, T->rail, T->rail, 0, 0, 0, 0, 0, 0, false);
        if (T->bevel)
            UiDraw_Rect(x + 2 + r, 8, total - 4 - 2 * r, 2, C_SHADOW);
    }

    int32_t cx = x + 4;
    cx += Key(cx, 12, "L") + 4;
    for (int32_t i = 0; i < count; i++)
    {
        const int32_t w = UiText_Width(UI_FACE_VT323, UI_BODY_PX, UiLocale_Lookup(labels[i])) + 2 * pad;
        if (i == current)
        {
            if (T->gbaFrame)
            {
                BevelBoxR(cx, 10, w, 28, 3, false, 2, GBA_YELLOW, UI_RGBA(255, 246, 205, 255),
                          UI_RGBA(248, 228, 152, 255), 0, 0, 0, 0, 0, 0, false);
            }
            else
            {
                const int32_t r = T->chamfer ? 5 : T->radius >= 10 ? 10 : 3;
                BevelBoxR(cx, 10, w, 28, r, T->chamfer, 2, T->tabEdge, T->tabTop, T->tabBot,
                          T->bevel ? 1 : 0, T->tabHi, T->tabBot, 0, 0, 0, T->gloss);
            }
            UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, cx + pad, 10, 28, UiLocale_Lookup(labels[i]),
                           T->gbaFrame ? GBA_CHARCOAL : T->tabInk);
        }
        else
        {
            UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, cx + pad, 10, 28, UiLocale_Lookup(labels[i]), UI_COL_ON_SOFT);
        }
        if (tabX) tabX[i] = cx;
        if (tabW) tabW[i] = w;
        cx += w + 2;
    }
    cx += 2;
    cx += Key(cx, 12, "R");
    (void)cx;
    return x + total;
}

int32_t UiW_Tabs(const int32_t x, const char *const *const labels, const int32_t count,
                 const int32_t current, int32_t *const tabX, int32_t *const tabW)
{
    return UiW_TabsPad(x, labels, count, current, tabX, tabW, 12);
}

int32_t UiW_TextWrapped(const int32_t x, const int32_t y, const int32_t w, const int32_t lineH,
                        const char *const text, const uint32_t colour, const int32_t maxLines)
{
    return UiW_TextWrappedF(UI_FACE_VT323, UI_BODY_PX, x, y, w, lineH, text, colour, maxLines);
}

int32_t UiW_TextWrappedF(const uint8_t face, const uint8_t px, const int32_t x, const int32_t y,
                         const int32_t w, const int32_t lineH, const char *const text,
                         const uint32_t colour, const int32_t maxLines)
{
    char line[160];
    int32_t lines = 0;
    const char *p = text;
    while (p && *p && lines < maxLines)
    {
        size_t len = 0, lastSpace = 0;
        while (p[len] && len < sizeof(line) - 1)
        {
            size_t step = 1;
            while (len + step < sizeof(line) - 1 && (p[len + step] & 0xC0) == 0x80)
                step++;
            memcpy(line, p, len + step);
            line[len + step] = '\0';
            if (UiText_Width(face, px, line) > w)
                break;
            if (p[len] == ' ')
                lastSpace = len;
            len += step;
        }
        if (p[len] && lastSpace > 0)
            len = lastSpace;
        memcpy(line, p, len);
        line[len] = '\0';
        if (colour != 0)
            UiText_Draw(face, px, x, y + lines * lineH, line, colour);
        lines++;
        p += len;
        while (*p == ' ')
            p++;
    }
    return lines;
}

static void DrawGbaFooterItem(int32_t *const x, const int32_t y, const char *const key,
                              const char *const label, const bool gold)
{
    const int32_t capW = UiW_GbaKeyCapWidth(key, 24);
    UiW_GbaKeyCap(*x, y, 24, key, gold);
    *x += capW + 6;
    if (label && label[0])
    {
        UiText_DrawMid(UI_FACE_VT323, 24, *x + 1, y + 1, 24, label, UI_RGBA(16, 20, 22, 220));
        *x += UiText_DrawMid(UI_FACE_VT323, 24, *x, y, 24, label, C_WHITE) + 18;
    }
}

void UiW_FooterGlyph(int32_t *const x, const uint8_t glyph, const char *const label)
{
    const int32_t y = UI_SCREEN_H - FOOTER_H + 8;
    if (T->gbaFrame)
    {
        static const char *const kGlyphKeys[4] = { "X", "O", "TRI", "SQ" };
        if (sGbaPage)
            QueueGbaBadge(kGlyphKeys[glyph & 3u], label, (glyph & 3u) == UI_GLYPH_CIRCLE);
        else
            DrawGbaFooterItem(x, y, kGlyphKeys[glyph & 3u], label, (glyph & 3u) == UI_GLYPH_CIRCLE);
        return;
    }
    UiW_Glyph(*x, y, glyph);
    *x += 30;
    *x += UiText_DrawMid(UI_FACE_PS2P, UI_FOOT_PX, *x, y, 24, label, UI_COL_ON_FRAME) + 18;
}

void UiW_FooterClock(const char *const clock)
{
    if (sGbaPage)
    {
        UiW_GbaPageFlush();
        return;
    }
    const int32_t y = UI_SCREEN_H - FOOTER_H + 5;
    const int32_t w = UiText_InkWidth(UI_FACE_PS2P, UI_FOOT_PX, clock) + 26;
    const int32_t x = UI_SCREEN_W - 8 - w;
    UiW_Capsule(x, y, w, 30);
    UiText_DrawCentred(UI_FACE_PS2P, UI_FOOT_PX, x, y, w, 30, clock, UI_COL_ON_FRAME);
}

void UiW_FooterKey(int32_t *const x, const char *const key, const char *const label)
{
    const int32_t y = UI_SCREEN_H - FOOTER_H + 8;
    if (T->gbaFrame)
    {
        const bool gold = strcmp(key, "SELECT") == 0 || strcmp(key, "START") == 0;
        if (sGbaPage)
            QueueGbaBadge(key, label, gold);
        else
            DrawGbaFooterItem(x, y, key, label, gold);
        return;
    }
    *x += KeyEx(*x, y, key, true) + 6;
    *x += UiText_DrawMid(UI_FACE_PS2P, UI_FOOT_PX, *x, y, 24, label, UI_COL_ON_FRAME) + 18;
}

// ---------- Boxes ----------

static void AccentBar(const int32_t x, const int32_t y, const int32_t h)
{
    if (!T->accent)
        return;
    const int32_t r = T->chamfer && h >= 24 ? T->radius : 0;
    UiDraw_Rect(x, y + r, 6, h - 2 * r, T->accent);
}

void UiW_Panel(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    if (T->gbaFrame)
    {
        GbaFrameBox(x, y, w, h, T->paper0, T->paper0, T->paperShade, 4);
        return;
    }
    BevelBox(x, y, w, h, T->edge, T->paperEdge, T->paper0, T->paper0, 2, C_WHITE, T->paperShade, 4);
    AccentBar(x, y, h);
}

void UiW_PanelDark(const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    if (T->gbaFrame)
    {
        GbaFrameBox(x, y, w, h, T->darkTop, T->darkBot, GBA_DARK_SH, 4);
        return;
    }
    BevelBox(x, y, w, h, T->edge > 3 ? T->edge : 3, T->darkEdge, T->darkTop, T->darkBot, 1, T->darkHi, T->darkBot, 4);
    Gloss(x, y, w, h, 3);
    AccentBar(x, y, h);
}

void UiW_PanelHead(const int32_t x, const int32_t y, const int32_t w,
                   const char *const label, const char *const right)
{
    if (T->gbaFrame)
    {
        UiDraw_Rect(x + 6, y + 6, w - 12, 33, T->paperStrip);
        UiDraw_Rect(x + 6, y + 39, w - 12, 2, GBA_CHARCOAL);
        int32_t tagW = UiText_Width(UI_FACE_PS2P, 16, label) + 28;
        if (tagW > w - 24)
            tagW = w - 24;
        if (tagW > 0)
        {
            UiDraw_RectV(x + 6, y + 6, tagW, 33, UI_RGBA(213, 65, 41, 255), UI_RGBA(156, 36, 24, 255));
            const float rx0 = (float)(x + 6 + tagW), rx1 = (float)(x + 6 + tagW + 12);
            const float xs[4] = { rx0, rx1, rx0, rx0 };
            const float ys[4] = { (float)(y + 6), (float)(y + 39), (float)(y + 39), (float)(y + 39) };
            UiDraw_Poly4(xs, ys, UI_RGBA(156, 36, 24, 255));
            for (int32_t k = 0; k < 3; k++)
                UiDraw_Rect(x + 9, y + 11 + k * 8, 2, 5, UI_RGBA(255, 196, 176, 255));
        }
        UiText_DrawMid(UI_FACE_PS2P, 16, x + 16, y + 6, 33, label, T->stripInk);
        if (right)
            UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX,
                           x + w - 16 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, right),
                           y + 6, 33, right, T->stripSoft);
        return;
    }
    const int32_t e = T->edge, r = T->radius > e ? T->radius - e : 0;
    ShapeFill(x + e, y + e, w - 2 * e, 2 * r + 2, r, T->chamfer, T->paperStrip, T->paperStrip);
    UiDraw_Rect(x + e, y + e + r, w - 2 * e, 39 - e - r, T->paperStrip);
    if (T->bevel)
        UiDraw_Rect(x + e + r, y + e, w - 2 * e - 2 * r, 2, C_WHITE);
    UiDraw_Rect(x + e, y + 39, w - 2 * e, 2, T->edge >= 4 ? T->paperEdge : T->paperShade);
    AccentBar(x, y, 41);
    UiText_DrawMid(UI_FACE_PS2P, 16, x + 16, y + T->edge, 39 - T->edge, label, T->stripInk);
    if (right)
        UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX,
                       x + w - 16 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, right),
                       y + T->edge, 39 - T->edge, right, T->stripSoft);
}

void UiW_Window(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const char *const title)
{
    if (T->gbaFrame)
    {
        GbaFrameBox(x, y, w, h, T->paper0, T->paper0, T->paperShade, 6);
        UiDraw_RectV(x + 6, y + 6, w - 12, 35, T->winTop, T->winBot);
        UiDraw_Rect(x + 6, y + 6, w - 12, 1, T->capHi);
        UiDraw_Rect(x + 6, y + 41, w - 12, 2, GBA_CHARCOAL);
        UiText_DrawMid(UI_FACE_PS2P, 16, x + 16, y + 6, 35, title, T->winInk);
        return;
    }
    const int32_t e = T->edge;
    BevelBox(x, y, w, h, e, T->paperEdge, T->paper0, T->paper0, 2, C_WHITE, T->paperShade, 6);
    const int32_t r = T->radius > e ? T->radius - e : 0;
    ShapeFill(x + e, y + e, w - 2 * e, 2 * r + 2, r, T->chamfer, T->winTop, T->winTop);
    UiDraw_RectV(x + e, y + e + r, w - 2 * e, 41 - e - r, T->winTop, T->winBot);
    if (T->bevel)
        UiDraw_Rect(x + e + r, y + e, w - 2 * e - 2 * r, 1, T->capHi);
    if (T->gloss)
        UiDraw_Rect(x + e + r, y + e + 1, w - 2 * e - 2 * r, 18, C_GLOSS);
    UiDraw_Rect(x + e, y + 41, w - 2 * e, 2, T->edge >= 4 ? T->paperEdge : T->darkEdge);
    if (T->accent)
        UiDraw_Rect(x + e, y + 41, w - 2 * e, 3, T->accent);
    UiText_DrawMid(UI_FACE_PS2P, 16, x + 16, y + T->edge, 41 - T->edge, title, T->winInk);
}

// ---------- Rows, chips, bars, buttons ----------

void UiW_Arrow(const int32_t x, const int32_t y, const uint32_t colour)
{
    for (int32_t i = 0; i < 6; i++)
    {
        const int32_t len = (i + 1) * 4 / 3 + 1;
        UiDraw_Rect(x, y + i, len, 1, colour);
        UiDraw_Rect(x, y + 11 - i, len, 1, colour);
    }
}

void UiW_Row(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
             const bool selected, const int32_t index)
{
    if (selected)
    {
        if (T->gbaFrame)
        {
            UiDraw_Rect(x, y, w, h - 2, (index & 1) ? T->rowB : T->rowA);
            const int32_t r = 4 < (h - 2) / 2 ? 4 : (h - 2) / 2;
            BevelBoxR(x, y, w, h - 2, r, false, 2, T->selEdge, T->selTop, T->selBot, 1, C_WHITE, 0, 0, 0, 0, false);
            UiW_Arrow(x + 9, y + (h - 12) / 2 + 1, UI_GBA_SHAD_LIGHT);
            UiW_Arrow(x + 8, y + (h - 12) / 2, T->selArrow);
        }
        else if (T->selRadius > 0)
        {
            UiDraw_Rect(x, y, w, h - 2, (index & 1) ? T->rowB : T->rowA);
            const int32_t r = T->selRadius < (h - 2) / 2 ? T->selRadius : (h - 2) / 2;
            BevelBoxR(x, y, w, h - 2, r, false, T->edge >= 4 ? 3 : 2, T->selEdge, T->selTop, T->selBot,
                      0, 0, 0, 0, 0, 0, T->gloss);
            UiW_Arrow(x + 10, y + (h - 14) / 2, T->selArrow);
        }
        else if (T->chamfer)
        {
            const int32_t c = h - 2 < 20 ? 6 : 12;
            UiDraw_RectV(x, y, w - c, h - 2, T->selTop, T->selBot);
            const float xs[4] = { (float)(x + w - c), (float)(x + w), (float)(x + w - c), (float)(x + w - c) };
            const float ys[4] = { (float)y, (float)y, (float)(y + h - 2), (float)(y + h - 2) };
            UiDraw_Poly4(xs, ys, T->selBot);
            UiDraw_Rect(x, y + h - 2, w - c, 2, T->selEdge);
            UiW_Arrow(x + 8, y + (h - 12) / 2, T->selArrow);
        }
        else
        {
            UiDraw_RectV(x, y, w, h - 2, T->selTop, T->selBot);
            UiDraw_Rect(x, y, w, 1, C_WHITE);
            UiDraw_Rect(x, y + h - 2, w, 2, T->selEdge);
            UiW_Arrow(x + 8, y + (h - 12) / 2, T->selArrow);
        }
    }
    else
    {
        UiDraw_Rect(x, y, w, h - 2, (index & 1) ? T->rowB : T->rowA);
        UiDraw_Rect(x, y + h - 2, w, 2, T->rowLine);
    }
}

int32_t UiW_Chip(const int32_t x, const int32_t y, const char *const label, const bool amber)
{
    const int32_t w = UiText_Width(UI_FACE_VT323, UI_BODY_PX, label) + 16;
    const int32_t r = T->chamfer ? 4 : T->radius >= 10 ? 12 : 3;
    if (amber)
        BevelBoxR(x, y, w, 26, r, T->chamfer, 2, T->tabEdge, T->tabTop, T->tabBot, T->bevel ? 1 : 0, T->tabHi, T->tabBot, 0, 0, 0, false);
    else
        BevelBoxR(x, y, w, 26, r, T->chamfer, 2, T->chipEdge, T->chipTop, T->chipBot, T->bevel ? 1 : 0, T->capHi, T->chipBot, 0, 0, 0, false);
    UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, x + 8, y, 26, label, amber ? T->tabInk : T->chipInk);
    return w;
}

void UiW_Bar(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
             int32_t percent, const uint32_t top, const uint32_t bottom)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    UiDraw_Rect(x + 1, y, w - 2, h, UI_RGBA(26, 38, 32, 255));
    UiDraw_Rect(x, y + 1, w, h - 2, UI_RGBA(26, 38, 32, 255));
    UiDraw_Rect(x + 2, y + 2, w - 4, h - 4, UI_COL_BAR_BG);
    UiDraw_Rect(x + 2, y + 2, w - 4, 1, UI_RGBA(255, 255, 255, 26));
    UiDraw_RectV(x + 2, y + 2, (w - 4) * percent / 100, h - 4, top, bottom);
}

void UiW_Button(const int32_t x, const int32_t y, const int32_t w,
                const char *const label, const uint8_t colour, const bool pressed)
{
    const int32_t dy = pressed ? 2 : 0;
    const int32_t r = T->chamfer ? 8 : T->radius >= 10 ? 20 : 6;
    const int32_t sh = pressed ? 0 : (T->shadowR ? 4 : 2);
    BevelBoxR(x, y + dy, w, 40, r, T->chamfer, T->edge >= 4 ? 3 : 2, T->btnEdge,
              T->btnTop[colour % 3], T->btnBot[colour % 3],
              T->bevel ? 1 : 0, UI_RGBA(255, 255, 255, 102), UI_RGBA(0, 0, 0, 102),
              T->shadowR ? sh : 0, sh, T->shadowR ? T->shadowCol : C_SHADOW, T->gloss);
    UiText_DrawMid(UI_FACE_PS2P, 16, x + (w - UiText_Width(UI_FACE_PS2P, 16, label)) / 2, y + dy, 40, label, T->btnInk);
}

void UiW_Strip3(const int32_t x, const int32_t y, const int32_t w,
                const char *const labels[3], const char *const values[3])
{
    UiDraw_Rect(x, y, w, 64, T->rowLine);
    UiDraw_Rect(x + 2, y + 2, w - 4, 60, T->paper1);
    const int32_t cw = (w - 4) / 3;
    for (int32_t i = 0; i < 3; i++)
    {
        const int32_t cx = x + 2 + i * cw;
        if (i > 0)
            UiDraw_Rect(cx - 1, y + 2, 2, 60, T->rowLine);
        UiText_Draw(UI_FACE_PS2P, 16, cx + (cw - UiText_Width(UI_FACE_PS2P, 16, labels[i])) / 2, y + 8, labels[i], UI_COL_INK_SOFT);
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, cx + (cw - UiText_Width(UI_FACE_VT323, UI_BODY_PX, values[i])) / 2, y + 26, values[i], UI_COL_INK);
    }
}

void UiW_Stepper(const int32_t x, const int32_t y, const int32_t value, int32_t *const minusX, int32_t *const plusX)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "x%02d", (int)value);
    for (int32_t k = 0; k < 2; k++)
    {
        const int32_t kx = x + k * 136;
        KeyCap(kx, y, 40, 40);
        UiText_DrawMid(UI_FACE_VT323, 32, kx + (40 - UiText_Width(UI_FACE_VT323, 32, k ? "+" : "-")) / 2, y, 40, k ? "+" : "-", T->keyInk);
    }
    UiDraw_Rect(x + 48, y, 80, 40, T->paperShade);
    UiDraw_Rect(x + 50, y + 2, 76, 36, T->paper1);
    UiDraw_Rect(x + 50, y + 2, 76, 2, UI_RGBA(0, 0, 0, 31));
    UiText_DrawMid(UI_FACE_VT323, 32, x + 48 + (80 - UiText_Width(UI_FACE_VT323, 32, buf)) / 2, y, 40, buf, UI_COL_INK);
    if (minusX) *minusX = x;
    if (plusX) *plusX = x + 136;
}

void UiW_ScrollBar(const int32_t x, const int32_t y, const int32_t h,
                   const int32_t first, const int32_t visible, const int32_t total)
{
    if (total <= visible || total <= 0)
        return;
    int32_t barH = h * visible / total;
    if (barH < 24) barH = 24;
    const int32_t top = y + (h - barH) * first / (total - visible);
    UiDraw_Rect(x, top, 7, barH, UI_RGBA(62, 68, 86, 255));
    UiDraw_RectV(x + 1, top + 1, 5, barH - 2, UI_RGBA(156, 168, 208, 255), UI_RGBA(98, 108, 146, 255));
}

void UiW_Toast(const char *const text)
{
    const int32_t w = UiText_Width(UI_FACE_VT323, UI_BODY_PX, text) + 28;
    const int32_t x = (UI_SCREEN_W - w) / 2, y = 446;
    UiW_PanelDark(x, y, w, 42);
    UiText_DrawMid(UI_FACE_VT323, UI_BODY_PX, x + 14, y, 42, text, UI_COL_ON_FRAME);
}

// ---------- GBA Gen 3 screen chrome and widgets (FireRed GBA palettes) ----------

static void DrawBits(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                     const uint16_t *const rows, const int32_t scale, const uint32_t col)
{
    for (int32_t r = 0; r < h; r++)
    {
        const uint16_t bits = rows[r];
        int32_t c = 0;
        while (c < w)
        {
            while (c < w && !(bits & (1u << (w - 1 - c))))
                c++;
            if (c >= w)
                break;
            const int32_t c0 = c;
            while (c < w && (bits & (1u << (w - 1 - c))))
                c++;
            UiDraw_Rect(x + c0 * scale, y + r * scale, (c - c0) * scale, scale, col);
        }
    }
}

void UiW_GbaArrowH(const int32_t x, const int32_t y, const bool right, const uint32_t colour)
{
    for (int32_t i = 0; i < 6; i++)
    {
        const int32_t len = (i + 1) * 4 / 3 + 1;
        const int32_t rx = right ? x : (x + 8 - len);
        UiDraw_Rect(rx, y + i, len, 1, colour);
        UiDraw_Rect(rx, y + 11 - i, len, 1, colour);
    }
}

void UiW_GbaArrowV(const int32_t x, const int32_t y, const bool down, const uint32_t colour)
{
    for (int32_t i = 0; i < 6; i++)
    {
        const int32_t ry = down ? (y + i) : (y + 5 - i);
        const int32_t w = (5 - i) * 2 + 2;
        UiDraw_Rect(x + i, ry, w, 1, colour);
    }
}

void UiW_GbaStar(const int32_t x, const int32_t y, const uint32_t colour)
{
    static const uint16_t kStar[9] = {
        0x010, 0x038, 0x038, 0x1FF, 0x0FE, 0x07C, 0x0EE, 0x183, 0x000
    };
    DrawBits(x, y, 9, 8, kStar, 2, colour);
}

void UiW_GbaMark(const int32_t x, const int32_t y, const uint8_t shape, const uint32_t colour)
{
    static const uint16_t kCircle[7] = { 0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C };
    static const uint16_t kSquare[7] = { 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };
    static const uint16_t kTri[7]    = { 0x08, 0x1C, 0x1C, 0x3E, 0x3E, 0x7F, 0x7F };
    static const uint16_t kHeart[7]  = { 0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 };
    const uint16_t *const kShapes[4] = { kCircle, kSquare, kTri, kHeart };
    DrawBits(x, y, 7, 7, kShapes[shape & 3u], 2, colour);
}

int32_t UiW_GbaKeyCapWidth(const char *const key, const int32_t h)
{
    if (strcmp(key, "X") == 0 || strcmp(key, "O") == 0 || strcmp(key, "TRI") == 0
        || strcmp(key, "SQ") == 0 || strcmp(key, "LR") == 0 || strcmp(key, "UD") == 0)
        return h;
    return UiText_Width(UI_FACE_VT323, 24, key) + 14;
}

void UiW_GbaKeyCap(const int32_t x, const int32_t y, const int32_t h, const char *const key, const bool gold)
{
    const int32_t w = UiW_GbaKeyCapWidth(key, h);
    const uint32_t top = gold ? UI_RGBA(240, 184, 40, 255) : UI_RGBA(224, 56, 44, 255);
    const uint32_t bot = gold ? UI_RGBA(196, 136, 16, 255) : UI_RGBA(172, 28, 22, 255);
    const uint32_t hi  = gold ? UI_RGBA(255, 244, 160, 255) : UI_RGBA(255, 164, 152, 255);
    const uint32_t sh  = gold ? UI_RGBA(120, 72, 0, 140) : UI_RGBA(80, 12, 10, 150);
    BevelBoxR(x, y, w, h, 4, false, 2, GBA_CHARCOAL, top, bot, 0, 0, 0, 0, 2, UI_RGBA(16, 20, 22, 200), false);
    UiDraw_Rect(x + 3, y + 2, w - 6, 2, hi);
    UiDraw_Rect(x + 3, y + h - 4, w - 6, 2, sh);

    static const uint16_t kCross[7]  = { 0x41, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x41 };
    static const uint16_t kCircle[7] = { 0x1C, 0x22, 0x41, 0x41, 0x41, 0x22, 0x1C };
    static const uint16_t kTri[7]    = { 0x08, 0x14, 0x14, 0x22, 0x22, 0x41, 0x7F };
    static const uint16_t kSq[7]     = { 0x7F, 0x41, 0x41, 0x41, 0x41, 0x41, 0x7F };
    static const uint16_t kLR[7]     = { 0x00, 0x24, 0x66, 0xE7, 0x66, 0x24, 0x00 };
    static const uint16_t kUD[7]     = { 0x08, 0x1C, 0x3E, 0x00, 0x3E, 0x1C, 0x08 };

    const uint32_t ink = gold ? UI_RGBA(48, 32, 0, 255) : C_WHITE;
    const int32_t ix = x + (w - 14) / 2, iy = y + (h - 14) / 2;
    if (strcmp(key, "X") == 0) DrawBits(ix, iy, 7, 7, kCross, 2, ink);
    else if (strcmp(key, "O") == 0) DrawBits(ix, iy, 7, 7, kCircle, 2, ink);
    else if (strcmp(key, "TRI") == 0) DrawBits(ix, iy, 7, 7, kTri, 2, ink);
    else if (strcmp(key, "SQ") == 0) DrawBits(ix, iy, 7, 7, kSq, 2, ink);
    else if (strcmp(key, "LR") == 0) DrawBits(x + (w - 16) / 2, iy, 8, 7, kLR, 2, ink);
    else if (strcmp(key, "UD") == 0) DrawBits(ix, iy, 7, 7, kUD, 2, ink);
    else UiText_DrawCentred(UI_FACE_VT323, 24, x, y, w, h, key, ink);
}

void UiW_GbaHelpBadgeY(int32_t *const rx, const int32_t y, const char *const key, const char *const action, const bool gold)
{
    const int32_t capW = UiW_GbaKeyCapWidth(key, 24);
    const int32_t actW = (action && action[0]) ? UiText_Width(UI_FACE_VT323, 24, action) : 0;
    const int32_t totalW = capW + (actW > 0 ? 5 + actW : 0);
    const int32_t x = *rx - totalW;
    UiW_GbaKeyCap(x, y, 24, key, gold);
    if (actW > 0)
    {
        UiText_DrawMid(UI_FACE_VT323, 24, x + capW + 5, y, 24, action, C_WHITE);
    }
    *rx = x - 12;
}

void UiW_GbaHelpBadge(int32_t *const rx, const char *const key, const char *const action, const bool gold)
{
    UiW_GbaHelpBadgeY(rx, 14, key, action, gold);
}

typedef struct
{
    uint32_t body, scan, grid, barAccent, pillBg, pillBorder, pillInk;
    bool stripes;
} GbaBgSpec;

static GbaBgSpec BgSpecFor(const uint8_t bgKind)
{
    // FireRed GBA section palettes (party blue + crimson, bag coral, summary lavender, red dex, cerulean map, burgundy card, slate options)
    static const GbaBgSpec kSpecs[7] = {
        [UI_GBA_BG_PARTY]   = { UI_RGBA(44, 86, 144, 152),  UI_RGBA(24, 52, 96, 140),  UI_RGBA(156, 188, 244, 24), UI_RGBA(213, 65, 41, 255),  UI_RGBA(213, 65, 41, 255),  GBA_CHARCOAL, C_WHITE, false },
        [UI_GBA_BG_BAG]     = { UI_RGBA(148, 54, 48, 152),  UI_RGBA(98, 32, 28, 140),  UI_RGBA(255, 176, 164, 22), UI_RGBA(230, 104, 82, 255), UI_RGBA(196, 56, 44, 255),  GBA_CHARCOAL, C_WHITE, false },
        [UI_GBA_BG_SUMMARY] = { UI_RGBA(88, 78, 148, 152),  UI_RGBA(52, 44, 104, 140), UI_RGBA(192, 188, 244, 24), UI_RGBA(139, 139, 205, 255),UI_RGBA(104, 96, 176, 255), GBA_CHARCOAL, C_WHITE, false },
        [UI_GBA_BG_POKEDEX] = { UI_RGBA(176, 34, 34, 152),  UI_RGBA(224, 64, 56, 28),  UI_RGBA(0, 0, 0, 28),       UI_RGBA(232, 56, 48, 255),  UI_RGBA(248, 248, 248, 255),UI_RGBA(41, 49, 49, 255), UI_RGBA(41, 49, 49, 255), true },
        [UI_GBA_BG_POKENAV] = { UI_RGBA(32, 76, 128, 152),  UI_RGBA(18, 46, 84, 140),  UI_RGBA(115, 196, 248, 26), UI_RGBA(88, 184, 248, 255), UI_RGBA(42, 102, 164, 255), GBA_CHARCOAL, C_WHITE, false },
        [UI_GBA_BG_CARD]    = { UI_RGBA(72, 38, 52, 152),   UI_RGBA(44, 22, 32, 140),  UI_RGBA(240, 198, 84, 22),  UI_RGBA(232, 192, 80, 255), UI_RGBA(164, 116, 20, 255), GBA_CHARCOAL, C_WHITE, false },
        [UI_GBA_BG_OPTIONS] = { UI_RGBA(52, 62, 96, 152),   UI_RGBA(30, 36, 60, 140),  UI_RGBA(148, 172, 232, 22), UI_RGBA(213, 65, 41, 255),  UI_RGBA(65, 74, 106, 255),  GBA_CHARCOAL, C_WHITE, false },
    };
    return kSpecs[bgKind < 7 ? bgKind : 0];
}

static uint32_t BlendOverRgba(const uint32_t dst, const uint32_t src)
{
    const uint32_t sa = (src >> 24) & 0xFFu;
    if (sa == 0u)
        return dst;
    const uint32_t da = (dst >> 24) & 0xFFu;
    const uint32_t invSa = 255u - sa;
    const uint32_t outA = sa + (da * invSa + 127u) / 255u;
    if (outA == 0u)
        return 0u;
    uint32_t out = outA << 24;
    for (uint32_t s = 0u; s < 24u; s += 8u)
    {
        const uint32_t sc = (src >> s) & 0xFFu;
        const uint32_t dc = (dst >> s) & 0xFFu;
        const uint32_t c = (sc * sa + dc * da * invSa / 255u) / outA;
        out |= (c > 255u ? 255u : c) << s;
    }
    return out;
}

void UiW_GbaBgPattern(const uint8_t bgKind, uint32_t *const out16x16)
{
    const GbaBgSpec sp = BgSpecFor(bgKind);
    for (int32_t y = 0; y < 16; y++)
    {
        uint32_t rowCol = sp.body;
        if (sp.stripes)
        {
            if (y >= 8)
                rowCol = BlendOverRgba(rowCol, sp.scan);
        }
        else
        {
            rowCol = BlendOverRgba(rowCol, (y & 7) < 4 ? UI_RGBA(255, 255, 255, 14) : UI_RGBA(0, 0, 0, 22));
        }
        if (y == 0)
            rowCol = BlendOverRgba(rowCol, sp.grid);
        for (int32_t x = 0; x < 16; x++)
            out16x16[y * 16 + x] = (x == 0) ? BlendOverRgba(rowCol, sp.grid) : rowCol;
    }
}

static int32_t sPagerW = UI_GBA_PAGER_W_MAX;

static void GbaTopBar(const uint8_t bgKind, const uint16_t iconSpr, const char *const secTitle,
                      const int32_t dotCount, const int32_t dotActive, const bool dotAllGreen,
                      const char *const pagerText, const int32_t pagerW)
{
    const GbaBgSpec sp = BgSpecFor(bgKind);
    sPagerW = pagerW;

    UiDraw_SetBackdrop(bgKind);

    UiDraw_RectV(0, 0, UI_SCREEN_W, GBA_TOPBAR_H - 6, UI_RGBA(58, 66, 94, 255), UI_RGBA(38, 44, 64, 255));
    UiDraw_Rect(0, 0, UI_SCREEN_W, 1, UI_RGBA(255, 255, 255, 36));
    UiDraw_Rect(0, GBA_TOPBAR_H - 6, UI_SCREEN_W, 3, sp.barAccent);
    UiDraw_Rect(0, GBA_TOPBAR_H - 3, UI_SCREEN_W, 3, GBA_CHARCOAL);
    UiDraw_Rect(0, GBA_TOPBAR_H, UI_SCREEN_W, 3, UI_RGBA(0, 0, 0, 105));

    const bool hasSec = (secTitle && secTitle[0] != '\0');
    if (hasSec)
    {
        DrawGbaSectionPill(12, 8, 244, 34, bgKind == UI_GBA_BG_POKEDEX, iconSpr, secTitle,
                           sp.pillBorder, sp.pillBg, sp.pillBg, sp.pillInk, !sp.stripes, !sp.stripes);
    }

    if (dotCount > 0 || (pagerText && pagerText[0]))
    {
        const int32_t lW = 24, rW = 24;
        const int32_t px = hasSec ? 268 : 14;
        BevelBoxR(px, 8, pagerW, 34, 5, false, 2, UI_RGBA(18, 24, 38, 255), UI_RGBA(26, 34, 52, 255),
                  UI_RGBA(26, 34, 52, 255), 1, UI_RGBA(82, 96, 138, 255), UI_RGBA(8, 12, 20, 200), 0, 0, 0, false);

        const int32_t lx = px + 6;
        BevelBoxR(lx, 13, lW, 24, 3, false, 1, UI_RGBA(16, 20, 28, 255), UI_RGBA(74, 86, 118, 255),
                  UI_RGBA(54, 64, 92, 255), 1, UI_RGBA(130, 144, 188, 255), UI_RGBA(28, 34, 48, 255), 0, 0, 0, false);
        UiText_DrawCentred(UI_FACE_PS2P, 16, lx, 13, lW, 24, "L", GBA_YELLOW);

        const int32_t rxBtn = px + pagerW - 6 - rW;
        BevelBoxR(rxBtn, 13, rW, 24, 3, false, 1, UI_RGBA(16, 20, 28, 255), UI_RGBA(74, 86, 118, 255),
                  UI_RGBA(54, 64, 92, 255), 1, UI_RGBA(130, 144, 188, 255), UI_RGBA(28, 34, 48, 255), 0, 0, 0, false);
        UiText_DrawCentred(UI_FACE_PS2P, 16, rxBtn, 13, rW, 24, "R", GBA_YELLOW);

        const int32_t dotsW = dotCount > 0 ? dotCount * 15 - 3 : 0;
        const int32_t innerW = pagerW - 68;
        uint8_t pagerFace, pagerPx;
        FitBarFace(pagerText, innerW - (dotsW > 0 ? dotsW + 8 : 0), &pagerFace, &pagerPx);
        const int32_t txtW = (pagerText && pagerText[0]) ? UiText_Width(pagerFace, pagerPx, pagerText) : 0;
        const int32_t gap = (dotsW > 0 && txtW > 0) ? 8 : 0;
        int32_t contentW = dotsW + gap + txtW;
        if (contentW > innerW) contentW = innerW;
        int32_t cx = px + 34 + (innerW - contentW) / 2;

        UiDraw_PushClip(px + 32, 8, innerW + 4, 34);
        for (int32_t i = 0; i < dotCount; i++)
        {
            const int32_t dx = cx + 6, dy = 25;
            if (dotAllGreen)
            {
                Disc(dx, dy, 6, UI_RGBA(24, 32, 32, 255));
                Disc(dx, dy, 4, UI_RGBA(90, 213, 131, 255));
            }
            else if (i == dotActive)
            {
                Disc(dx, dy, 6, GBA_YELLOW);
                Disc(dx, dy, 4, C_WHITE);
            }
            else
            {
                Disc(dx, dy, 5, UI_RGBA(16, 20, 28, 255));
                Disc(dx, dy, 3, UI_RGBA(70, 82, 110, 255));
            }
            cx += 15;
        }
        if (dotCount > 0)
            cx += 5;
        if (pagerText && pagerText[0])
            UiText_DrawMid(pagerFace, pagerPx, cx, 8, 34, pagerText, GBA_YELLOW);
        UiDraw_PopClip();
    }
}

void UiW_GbaBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                const int32_t r, const uint32_t border, const uint32_t top, const uint32_t bot,
                const uint32_t innerTop, const int32_t shadowY)
{
    if (w <= 0 || h <= 0)
        return;
    int32_t rr = r;
    if (rr > h / 2) rr = h / 2;
    if (rr > w / 2) rr = w / 2;
    BevelBoxR(x, y, w, h, rr, false, 2, border, top, bot, innerTop ? 2 : 0, innerTop,
              innerTop ? UI_RGBA(0, 0, 0, 32) : 0, 0, shadowY, UI_RGBA(0, 0, 0, 100), false);
}

void UiW_GbaRingBox(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                    const int32_t r, const uint32_t outerRing, const uint32_t innerRing,
                    const uint32_t top, const uint32_t bot, const uint32_t innerHi, const int32_t shadowY)
{
    if (w <= 0 || h <= 0)
        return;
    int32_t rr = r;
    if (rr > h / 2) rr = h / 2;
    if (rr > w / 2) rr = w / 2;
    if (shadowY > 0)
        ShapeFill(x - 2, y + shadowY, w + 4, h + 2, rr + 2, false, UI_RGBA(0, 0, 0, 100), UI_RGBA(0, 0, 0, 100));
    ShapeFill(x - 2, y - 2, w + 4, h + 4, rr + 2, false, outerRing, outerRing);
    ShapeFill(x, y, w, h, rr, false, innerRing, innerRing);
    if (w > 4 && h > 4)
        ShapeFill(x + 2, y + 2, w - 4, h - 4, rr > 2 ? rr - 2 : 0, false, top, bot);
    if (innerHi != 0 && w > 8 && h > 8)
    {
        const int32_t hiR = rr > 3 ? rr - 3 : 0;
        UiDraw_Rect(x + 3 + hiR, y + 2, w - 6 - 2 * hiR, 2, innerHi);
        UiDraw_Rect(x + 3 + hiR, y + h - 4, w - 6 - 2 * hiR, 2, UI_RGBA(0, 0, 0, 36));
    }
}

int32_t UiW_GbaText(const uint8_t face, const uint8_t px, const int32_t x, const int32_t y,
                    const char *const text, const uint32_t ink, const uint32_t shadow)
{
    (void)shadow;
    if (!text || !text[0])
        return 0;
    return UiText_Draw(face, px, x, y, text, ink);
}

int32_t UiW_GbaTextMid(const uint8_t face, const uint8_t px, const int32_t x, const int32_t y, const int32_t h,
                       const char *const text, const uint32_t ink, const uint32_t shadow)
{
    (void)shadow;
    if (!text || !text[0])
        return 0;
    return UiText_DrawMid(face, px, x, y, h, text, ink);
}

void UiW_GbaTextCentred(const uint8_t face, const uint8_t px, const int32_t x, const int32_t y,
                        const int32_t w, const int32_t h, const char *const text,
                        const uint32_t ink, const uint32_t shadow)
{
    (void)shadow;
    if (!text || !text[0])
        return;
    UiText_DrawCentred(face, px, x, y, w, h, text, ink);
}

int32_t UiW_GbaTextWrapped(const uint8_t face, const uint8_t px, const int32_t x, const int32_t y,
                           const int32_t w, const int32_t lineH, const char *const text,
                           const uint32_t ink, const uint32_t shadow, const int32_t maxLines)
{
    (void)shadow;
    if (!text || !text[0])
        return 0;
    return UiW_TextWrappedF(face, px, x, y, w, lineH, text, ink, maxLines);
}

void UiW_GbaTopBar(const uint8_t bgKind, const uint16_t iconSpr, const char *const secTitle,
                   const int32_t dotCount, const int32_t dotActive, const bool dotAllGreen,
                   const char *const pagerText)
{
    GbaTopBar(bgKind, iconSpr, secTitle, dotCount, dotActive, dotAllGreen, pagerText, UI_GBA_PAGER_W_MAX);
}

int32_t UiW_GbaPagerWidth(const char *const *const pages, const int32_t count, const int32_t dotCount)
{
    int32_t text = 0;
    for (int32_t i = 0; pages != NULL && i < count; i++)
    {
        const int32_t w = UiText_Width(UI_FACE_PS2P, 16, UiLocale_Lookup(pages[i]));
        if (w > text)
            text = w;
    }
    const int32_t dots = dotCount > 0 ? dotCount * 15 - 3 : 0;
    const int32_t w = 68 + dots + (dots > 0 && text > 0 ? 8 : 0) + text + 16;
    return w < UI_GBA_PAGER_W_MIN ? UI_GBA_PAGER_W_MIN : w > UI_GBA_PAGER_W_MAX ? UI_GBA_PAGER_W_MAX : w;
}

void UiW_GbaTopBarPages(const uint8_t bgKind, const uint16_t iconSpr, const char *const secTitle,
                        const char *const *const pages, const int32_t count, const int32_t cur)
{
    GbaTopBar(bgKind, iconSpr, secTitle, count, cur, false,
              (cur >= 0 && cur < count) ? UiLocale_Lookup(pages[cur]) : NULL,
              UiW_GbaPagerWidth(pages, count, count));
}

int32_t UiW_GbaPagerLastWidth(void)
{
    return sPagerW;
}

int32_t UiW_GbaPagerHit(const UiInput *const in, const bool hasSecPill, const int32_t dotCount)
{
    const int32_t px = hasSecPill ? 268 : 14;
    if (UiHit(in, px, 6, 44, 38))
        return -1;
    if (UiHit(in, px + sPagerW - 44, 6, 44, 38))
        return 1;
    if (dotCount > 0 && UiHit(in, px + 44, 6, sPagerW - 88, 38))
        return 1;
    return 0;
}

void UiW_GbaWin(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const uint8_t kind)
{
    switch (kind)
    {
    case UI_GBA_WIN_GOLD:
        GbaFrameBoxPal(x, y, w, h, GBA_CHARCOAL, UI_RGBA(248, 224, 136, 255), UI_RGBA(216, 168, 56, 255),
                       UI_RGBA(24, 32, 32, 255), UI_RGBA(255, 253, 240, 255), UI_RGBA(255, 248, 220, 255),
                       UI_RGBA(232, 220, 172, 255), 4);
        break;
    case UI_GBA_WIN_LAVENDER:
        GbaFrameBoxPal(x, y, w, h, GBA_CHARCOAL, UI_RGBA(144, 148, 200, 255), UI_RGBA(82, 82, 136, 255),
                       UI_RGBA(24, 32, 32, 255), C_WHITE, UI_RGBA(246, 246, 252, 255),
                       UI_RGBA(216, 216, 236, 255), 4);
        break;
    case UI_GBA_WIN_DARK:
        GbaFrameBoxPal(x, y, w, h, UI_RGBA(20, 26, 40, 255), GBA_FR_HI, UI_RGBA(74, 74, 128, 255),
                       UI_RGBA(20, 26, 40, 255), UI_RGBA(44, 54, 82, 255), UI_RGBA(32, 40, 62, 255),
                       GBA_DARK_SH, 4);
        if (w > 24 && h > 18)
            UiDraw_Rect(x + 9, y + 8, w - 18, 1, UI_RGBA(164, 180, 232, 60));
        break;
    default:
        GbaFrameBoxPal(x, y, w, h, GBA_CHARCOAL, GBA_FR_HI, UI_RGBA(82, 90, 148, 255),
                       GBA_CHARCOAL, C_WHITE, UI_RGBA(248, 248, 244, 255), UI_GBA_SHAD_LIGHT, 4);
        break;
    }
}

void UiW_GbaMsg(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                const bool battleDark, const bool redArrow)
{
    const uint32_t innerEdge = battleDark ? UI_RGBA(20, 26, 28, 255) : GBA_CHARCOAL;
    if (battleDark)
    {
        GbaFrameBoxPal(x, y, w, h, UI_RGBA(20, 26, 28, 255), UI_RGBA(255, 164, 98, 255), UI_RGBA(220, 90, 65, 255),
                       innerEdge, UI_RGBA(41, 57, 65, 255), UI_RGBA(33, 47, 54, 255), UI_RGBA(20, 28, 34, 255), 4);
        if (w > 36 && h > 24)
        {
            UiDraw_Rect(x + 8, y + 6, w - 16, 3, UI_RGBA(92, 114, 128, 255));
            UiDraw_Rect(x + 7, y + 6, 8, h - 12, UI_RGBA(220, 90, 65, 255));
            UiDraw_Rect(x + 11, y + 6, 4, h - 12, UI_RGBA(255, 164, 98, 255));
            UiDraw_Rect(x + 15, y + 6, 2, h - 12, UI_RGBA(216, 228, 236, 255));
            UiDraw_Rect(x + w - 15, y + 6, 8, h - 12, UI_RGBA(255, 164, 98, 255));
            UiDraw_Rect(x + w - 11, y + 6, 4, h - 12, UI_RGBA(220, 90, 65, 255));
            UiDraw_Rect(x + w - 17, y + 6, 2, h - 12, UI_RGBA(216, 228, 236, 255));
        }
    }
    else
    {
        // FireRed overworld message frame (cobalt-blue std side rails + ivory paper)
        GbaFrameBoxPal(x, y, w, h, GBA_CHARCOAL, UI_RGBA(115, 168, 228, 255), UI_RGBA(62, 104, 176, 255),
                       innerEdge, C_WHITE, UI_RGBA(248, 248, 244, 255), UI_GBA_SHAD_LIGHT, 4);
        if (w > 36 && h > 24)
        {
            UiDraw_Rect(x + 8, y + 6, w - 16, 3, UI_GBA_SHAD_LIGHT);
            UiDraw_Rect(x + 8, y + h - 9, w - 16, 3, UI_GBA_SHAD_LIGHT);
            UiDraw_Rect(x + 6, y + 6, 9, h - 12, UI_RGBA(115, 168, 228, 255));
            UiDraw_Rect(x + 10, y + 6, 5, h - 12, UI_RGBA(62, 104, 176, 255));
            UiDraw_Rect(x + 15, y + 6, 2, h - 12, GBA_CHARCOAL);
            UiDraw_Rect(x + w - 15, y + 6, 9, h - 12, UI_RGBA(62, 104, 176, 255));
            UiDraw_Rect(x + w - 11, y + 6, 5, h - 12, UI_RGBA(115, 168, 228, 255));
            UiDraw_Rect(x + w - 17, y + 6, 2, h - 12, GBA_CHARCOAL);
        }
    }
    if (redArrow && w > 50 && h > 28)
    {
        UiW_GbaArrowV(x + w - 34, y + h - 18, true, UI_RGBA(24, 32, 32, 180));
        UiW_GbaArrowV(x + w - 35, y + h - 19, true, UI_RGBA(230, 8, 8, 255));
    }
}

void UiW_GbaSectionStrip(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                         const uint8_t tagKind, const char *const label, const char *const rightSub)
{
    static const uint32_t kTop[7] = {
        UI_RGBA(72, 136, 208, 255),  // BLUE
        UI_RGBA(62, 144, 112, 255),  // TEAL
        UI_RGBA(200, 148, 24, 255),  // GOLD
        UI_RGBA(74, 168, 64, 255),   // GREEN
        UI_RGBA(184, 64, 168, 255),  // MAGENTA
        UI_RGBA(213, 65, 41, 255),   // CRIMSON
        UI_RGBA(88, 96, 156, 255),   // SLATE
    };
    static const uint32_t kBot[7] = {
        UI_RGBA(44, 100, 168, 255),
        UI_RGBA(40, 112, 84, 255),
        UI_RGBA(158, 112, 12, 255),
        UI_RGBA(52, 128, 44, 255),
        UI_RGBA(140, 40, 128, 255),
        UI_RGBA(156, 36, 24, 255),
        UI_RGBA(62, 68, 120, 255),
    };
    const uint8_t idx = tagKind < 7 ? tagKind : 0;
    BevelBoxR(x, y, w, h, 3, false, 2, GBA_CHARCOAL, UI_RGBA(56, 64, 88, 255), UI_RGBA(42, 48, 68, 255),
              1, UI_RGBA(255, 255, 255, 24), UI_RGBA(0, 0, 0, 64), 0, 0, 0, false);

    const int32_t rw = (rightSub && rightSub[0]) ? UiText_Width(UI_FACE_VT323, 24, rightSub) : 0;
    const int32_t maxTagW = rw > 0 ? (w - rw - 26) : (w - 18);
    int32_t tagW = UiText_Width(UI_FACE_PS2P, 16, label) + 22;
    if (tagW > maxTagW) tagW = maxTagW;
    if (tagW < 0) tagW = 0;
    if (tagW > 0)
    {
        UiDraw_RectV(x + 2, y + 2, tagW, h - 4, kTop[idx], kBot[idx]);
        UiDraw_Rect(x + 2, y + 2, tagW, 1, UI_RGBA(255, 255, 255, 64));
        const float rx0 = (float)(x + 2 + tagW), rx1 = (float)(x + 2 + tagW + 10);
        const float xs[4] = { rx0, rx1, rx0, rx0 };
        const float ys[4] = { (float)(y + 2), (float)(y + h - 2), (float)(y + h - 2), (float)(y + h - 2) };
        UiDraw_Poly4(xs, ys, kBot[idx]);
        for (int32_t g = 4; g + 4 < h - 4; g += 6)
            UiDraw_Rect(x + 5, y + 2 + g, 3, 4, UI_RGBA(255, 255, 255, 160));
    }
    if (tagW > 14)
    {
        UiDraw_PushClip(x + 10, y + 2, tagW - 12, h - 4);
        UiText_DrawMid(UI_FACE_PS2P, 16, x + 12, y, h, label, C_WHITE);
        UiDraw_PopClip();
    }
    if (rw > 0)
    {
        UiDraw_PushClip(x + 10, y + 2, w - 14, h - 4);
        UiW_GbaTextMid(UI_FACE_VT323, 24, x + w - 10 - rw, y, h, rightSub, GBA_YELLOW, UI_GBA_SHAD_DARK);
        UiDraw_PopClip();
    }
}

void UiW_GbaHpBar(const int32_t x, const int32_t y, const int32_t w, int32_t h,
                  const uint16_t hp, const uint16_t maxHp)
{
    if (h < 14) h = 14;
    BevelBoxR(x, y, w, h, h / 2, false, 2, UI_RGBA(24, 32, 32, 255), UI_RGBA(57, 64, 64, 255),
              UI_RGBA(57, 64, 64, 255), 0, 0, 0, 0, 0, 0, false);
    UiDraw_Rect(x + 3, y + 2, 24, h - 4, UI_RGBA(36, 42, 42, 255));

    static const uint16_t kHP[7] = {
        0x45E, 0x451, 0x451, 0x7DE, 0x450, 0x450, 0x450,
    };
    DrawBits(x + 10, y + (h - 7) / 2 + 1, 11, 7, kHP, 1, UI_RGBA(168, 100, 0, 255));
    DrawBits(x + 9, y + (h - 7) / 2, 11, 7, kHP, 1, UI_RGBA(255, 230, 0, 255));

    const int32_t tx = x + 28, ty = y + 3, tw = w - 32, th = h - 6;
    if (tw <= 2 || th <= 2)
        return;
    UiDraw_Rect(tx - 1, ty - 1, tw + 2, th + 2, UI_RGBA(24, 32, 32, 255));
    UiDraw_Rect(tx, ty, tw, th, UI_RGBA(32, 57, 0, 255));
    if (maxHp == 0 || hp == 0)
        return;
    int32_t fw = (int32_t)((int64_t)tw * hp / maxHp);
    if (fw < 2) fw = 2;
    if (fw > tw) fw = tw;
    const int32_t pct = (int32_t)((int32_t)hp * 100 / maxHp);
    uint32_t top = UI_RGBA(115, 255, 172, 255), bot = UI_RGBA(90, 213, 131, 255);
    if (pct <= 20)
    {
        top = UI_RGBA(255, 180, 123, 255);
        bot = UI_RGBA(222, 106, 90, 255);
    }
    else if (pct <= 50)
    {
        top = UI_RGBA(255, 246, 139, 255);
        bot = UI_RGBA(255, 230, 57, 255);
    }
    const int32_t hiH = (th * 42 + 50) / 100;
    if (hiH > 0)
        UiDraw_Rect(tx, ty, fw, hiH, top);
    if (th - hiH > 0)
        UiDraw_Rect(tx, ty + hiH, fw, th - hiH, bot);
}

void UiW_GbaExpBar(const int32_t x, const int32_t y, const int32_t w, int32_t pct, const char *const nextText)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const int32_t h = 20;
    const int32_t nextW = (nextText && nextText[0]) ? UiText_Width(UI_FACE_VT323, 20, nextText) + 12 : 0;
    const int32_t barW = w - nextW;
    BevelBoxR(x, y, w, h, 3, false, 2, UI_RGBA(24, 32, 32, 255), UI_RGBA(62, 68, 82, 255),
              UI_RGBA(62, 68, 82, 255), 0, 0, 0, 0, 0, 0, false);
    UiDraw_Rect(x + 2, y + 2, 30, h - 4, UI_RGBA(36, 40, 48, 255));

    static const uint16_t kExp[7] = {
        0x7A2E, 0x4229, 0x4149, 0x708E, 0x4148, 0x4228, 0x7A28,
    };
    DrawBits(x + 9, y + (h - 7) / 2, 15, 7, kExp, 1, GBA_YELLOW);

    const int32_t tx = x + 34, ty = y + 5, tw = barW - 38, th = h - 10;
    if (tw > 2 && th > 2)
    {
        UiDraw_Rect(tx - 1, ty - 1, tw + 2, th + 2, UI_RGBA(16, 20, 24, 255));
        UiDraw_Rect(tx, ty, tw, th, UI_RGBA(24, 32, 42, 255));
        const int32_t fw = tw * pct / 100;
        if (fw > 0)
        {
            UiDraw_RectV(tx, ty, fw, th, UI_RGBA(106, 230, 255, 255), UI_RGBA(36, 136, 184, 255));
            UiDraw_Rect(tx, ty, fw, 2, UI_RGBA(170, 244, 255, 255));
        }
        for (int32_t sx = tx + 12; sx < tx + tw; sx += 12)
            UiDraw_Rect(sx, ty, 2, th, UI_RGBA(24, 32, 42, 210));
    }
    if (nextW > 0)
        UiW_GbaTextMid(UI_FACE_VT323, 20, x + barW + 4, y, h, nextText, UI_RGBA(172, 238, 255, 255), UI_GBA_SHAD_DARK);
}
