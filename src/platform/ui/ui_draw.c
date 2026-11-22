#include <math.h>
#include "ui_internal.h"

#define UI_DRAW_BUFFERS 3
#define CLIP_STACK_MAX 8

typedef struct
{
    int32_t x, y, w, h;
} UiClipRect;

static UiDrawList sBuffers[UI_DRAW_BUFFERS];
static uint32_t sWriteIdx = 0;
static uint32_t sPubIdx = 0;
static uint32_t sSeq = 0;
static uint16_t sWhiteX, sWhiteY;
static UiClipRect sClipStack[CLIP_STACK_MAX];
static int32_t sClipDepth = 0;

void UiDraw_Begin(void)
{
    UiTheme_Refresh();
    UiAssets_White(&sWhiteX, &sWhiteY);
    UiDrawList *const dst = &sBuffers[sWriteIdx];
    dst->vcount = 0;
    dst->backdropKind = -1;
    sClipDepth = 0;
}

void UiDraw_SetBackdrop(const uint8_t bgKind)
{
    sBuffers[sWriteIdx].backdropKind = (int8_t)bgKind;
}

bool UiDraw_IsClipped(void)
{
    return sClipDepth > 0;
}

uint32_t UiDraw_Count(void)
{
    return sBuffers[sWriteIdx].vcount / 6U;
}

static UiGlVertex *AllocQuad(void)
{
    UiDrawList *const dst = &sBuffers[sWriteIdx];
    if (dst->vcount + 6U > UI_MAX_QUADS * 6U)
        return 0;
    UiGlVertex *const v = &dst->verts[dst->vcount];
    dst->vcount += 6U;
    return v;
}

void UiDraw_Quad2(int32_t x, int32_t y, int32_t w, int32_t h,
                  uint32_t u0, uint32_t v0, uint32_t u1, uint32_t v1,
                  const uint32_t top, const uint32_t bottom)
{
    if (w <= 0 || h <= 0)
        return;

    if (sClipDepth > 0)
    {
        const UiClipRect *const c = &sClipStack[sClipDepth - 1];
        const int32_t x2 = x + w, y2 = y + h;
        const int32_t cx2 = c->x + c->w, cy2 = c->y + c->h;
        if (x2 <= c->x || x >= cx2 || y2 <= c->y || y >= cy2)
            return;

        if (x < c->x)
        {
            const int32_t cut = c->x - x;
            u0 += (uint32_t)((int64_t)(u1 - u0) * cut / w);
            x = c->x;
        }
        if (x2 > cx2)
        {
            const int32_t cut = x2 - cx2;
            u1 -= (uint32_t)((int64_t)(u1 - u0) * cut / (x2 - x));
        }
        if (y < c->y)
        {
            const int32_t cut = c->y - y;
            v0 += (uint32_t)((int64_t)(v1 - v0) * cut / h);
            y = c->y;
        }
        if (y2 > cy2)
        {
            const int32_t cut = y2 - cy2;
            v1 -= (uint32_t)((int64_t)(v1 - v0) * cut / (y2 - y));
        }
        const int32_t nx2 = x2 < cx2 ? x2 : cx2;
        const int32_t ny2 = y2 < cy2 ? y2 : cy2;
        w = nx2 - x;
        h = ny2 - y;
        if (w <= 0 || h <= 0)
            return;
    }

    UiGlVertex *const q = AllocQuad();
    if (q == 0)
        return;

    const float fx0 = (float)x, fy0 = (float)y;
    const float fx1 = (float)(x + w), fy1 = (float)(y + h);
    const float fu0 = (float)u0 / (float)UI_TEX_W;
    const float fv0 = (float)v0 / (float)UI_TEX_H;
    const float fu1 = (float)u1 / (float)UI_TEX_W;
    const float fv1 = (float)v1 / (float)UI_TEX_H;

    q[0] = (UiGlVertex){ fx0, fy0, fu0, fv0, top };
    q[1] = (UiGlVertex){ fx1, fy0, fu1, fv0, top };
    q[2] = (UiGlVertex){ fx1, fy1, fu1, fv1, bottom };
    q[3] = (UiGlVertex){ fx0, fy0, fu0, fv0, top };
    q[4] = (UiGlVertex){ fx1, fy1, fu1, fv1, bottom };
    q[5] = (UiGlVertex){ fx0, fy1, fu0, fv1, bottom };
}

void UiDraw_Quad(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                 const uint32_t u0, const uint32_t v0, const uint32_t u1, const uint32_t v1,
                 const uint32_t rgba)
{
    UiDraw_Quad2(x, y, w, h, u0, v0, u1, v1, rgba, rgba);
}

void UiDraw_RectV(const int32_t x, const int32_t y, const int32_t w, const int32_t h,
                  const uint32_t top, const uint32_t bottom)
{
    UiDraw_Quad2(x, y, w, h, sWhiteX + 1U, sWhiteY + 1U, sWhiteX + 3U, sWhiteY + 3U, top, bottom);
}

void UiDraw_Rect(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const uint32_t rgba)
{
    UiDraw_RectV(x, y, w, h, rgba, rgba);
}

void UiDraw_Frame(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const int32_t t, const uint32_t rgba)
{
    if (w <= 0 || h <= 0 || t <= 0)
        return;
    UiDraw_Rect(x, y, w, t, rgba);
    UiDraw_Rect(x, y + h - t, w, t, rgba);
    UiDraw_Rect(x, y + t, t, h - 2 * t, rgba);
    UiDraw_Rect(x + w - t, y + t, t, h - 2 * t, rgba);
}

void UiDraw_SpriteTint(const uint16_t id, const int32_t x, const int32_t y, int32_t scale, const uint32_t rgba)
{
    uint16_t sx, sy, sw, sh;
    if (!UiAssets_Sprite(id, &sx, &sy, &sw, &sh))
        return;
    if (scale < 1)
        scale = 1;
    UiDraw_Quad(x, y, (int32_t)sw * scale, (int32_t)sh * scale, sx, sy, sx + sw, sy + sh, rgba);
}

void UiDraw_Sprite(const uint16_t id, const int32_t x, const int32_t y, const int32_t scale)
{
    UiDraw_SpriteTint(id, x, y, scale, 0xFFFFFFFFu);
}

void UiDraw_Poly4(const float *const xs, const float *const ys, const uint32_t rgba)
{
    UiGlVertex *const q = AllocQuad();
    if (q == 0 || xs == 0 || ys == 0)
        return;
    const float u = ((float)sWhiteX + 2.0f) / (float)UI_TEX_W;
    const float v = ((float)sWhiteY + 2.0f) / (float)UI_TEX_H;
    q[0] = (UiGlVertex){ xs[0], ys[0], u, v, rgba };
    q[1] = (UiGlVertex){ xs[1], ys[1], u, v, rgba };
    q[2] = (UiGlVertex){ xs[2], ys[2], u, v, rgba };
    q[3] = (UiGlVertex){ xs[0], ys[0], u, v, rgba };
    q[4] = (UiGlVertex){ xs[2], ys[2], u, v, rgba };
    q[5] = (UiGlVertex){ xs[3], ys[3], u, v, rgba };
}

void UiDraw_Line(const float x0, const float y0, const float x1, const float y1, const float width, const uint32_t rgba)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;
    const float nx = -dy * (width * 0.5f) / len;
    const float ny = dx * (width * 0.5f) / len;
    const float xs[4] = { x0 + nx, x1 + nx, x1 - nx, x0 - nx };
    const float ys[4] = { y0 + ny, y1 + ny, y1 - ny, y0 - ny };
    UiDraw_Poly4(xs, ys, rgba);
}

void UiDraw_Tri(const float x0, const float y0, const float x1, const float y1, const float x2, const float y2, const uint32_t rgba)
{
    const float xs[4] = { x0, x1, x2, x2 };
    const float ys[4] = { y0, y1, y2, y2 };
    UiDraw_Poly4(xs, ys, rgba);
}

void UiDraw_PushClip(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (sClipDepth >= CLIP_STACK_MAX)
        return;
    if (sClipDepth > 0)
    {
        const UiClipRect *const p = &sClipStack[sClipDepth - 1];
        const int32_t x2 = x + w < p->x + p->w ? x + w : p->x + p->w;
        const int32_t y2 = y + h < p->y + p->h ? y + h : p->y + p->h;
        if (x < p->x) x = p->x;
        if (y < p->y) y = p->y;
        w = x2 - x;
        h = y2 - y;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
    }
    sClipStack[sClipDepth++] = (UiClipRect){ x, y, w, h };
}

void UiDraw_PopClip(void)
{
    if (sClipDepth > 0)
        sClipDepth--;
}

void UiDraw_Darken(const int32_t num, const int32_t den)
{
    UiDrawList *const list = &sBuffers[sWriteIdx];
    int32_t clamped = num;
    if (clamped <= 0 || den <= 0)
        return;
    if (clamped > den)
        clamped = den;
    for (uint32_t i = 0; i < list->vcount; i++)
    {
        const uint32_t c = list->verts[i].rgba;
        uint32_t out = c & 0xFF000000u;
        for (int32_t s = 0; s < 24; s += 8)
            out |= (uint32_t)((int32_t)((c >> s) & 0xFFu) * (den - clamped) / den) << s;
        list->verts[i].rgba = out;
    }
}

void UiDraw_Publish(const float tickMs)
{
    UiDrawList *const dst = &sBuffers[sWriteIdx];
    dst->seq = ++sSeq;
    dst->tickMs = tickMs;
    __atomic_store_n(&sPubIdx, sWriteIdx, __ATOMIC_RELEASE);
    sWriteIdx = (sWriteIdx + 1U) % UI_DRAW_BUFFERS;
    if (sWriteIdx == __atomic_load_n(&sPubIdx, __ATOMIC_ACQUIRE))
        sWriteIdx = (sWriteIdx + 1U) % UI_DRAW_BUFFERS;
}

const UiDrawList *UiDraw_Acquire(void)
{
    const uint32_t idx = __atomic_load_n(&sPubIdx, __ATOMIC_ACQUIRE);
    return &sBuffers[idx];
}
