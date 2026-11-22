#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"
#include "platform/vita/vita_platform.h"

#define ATLAS_MAGIC 0x33414955u // "UIA3"
#define ATLAS_VERSION 3u
#define ATLAS_REL_PATH "ui2/ui_atlas.bin"

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t texW;
    uint32_t texH;
    uint32_t fontCount;
    uint32_t spriteCount;
    uint32_t whiteX;
    uint32_t whiteY;
} AtlasHeader;

typedef struct
{
    uint8_t face;
    uint8_t px;
    uint8_t ascent;
    uint8_t lineH;
    uint16_t glyphCount;
    uint16_t pad0;
    uint32_t firstGlyph;
    uint32_t pad1;
} AtlasFont;

typedef struct
{
    uint32_t cp;
    uint16_t x;
    uint16_t y;
    uint8_t w;
    uint8_t h;
    int8_t xoff;
    int8_t yoff;
    uint8_t adv;
    uint8_t pad[3];
} AtlasGlyph;

typedef struct
{
    uint16_t x, y, w, h;
} AtlasSprite;

_Static_assert(sizeof(AtlasHeader) == 32, "AtlasHeader size mismatch");
_Static_assert(sizeof(AtlasFont) == 16, "AtlasFont size mismatch");
_Static_assert(sizeof(AtlasGlyph) == 16, "AtlasGlyph size mismatch");
_Static_assert(sizeof(AtlasSprite) == 8, "AtlasSprite size mismatch");

static uint8_t *sAtlasFileBuf;
static const AtlasHeader *sHdr;
static const AtlasFont *sFonts;
static const AtlasGlyph *sGlyphs;
static const AtlasSprite *sSprites;
static const uint16_t *sPixels;
static bool sReady;

bool UiAssets_Load(void)
{
    size_t size = 0;
    uint8_t *const data = VitaPlatform_LoadAsset(ATLAS_REL_PATH, &size);
    if (data == NULL || size < sizeof(AtlasHeader))
    {
        free(data);
        VITA_LOG("[UI2] failed to load %s\n", ATLAS_REL_PATH);
        return false;
    }

    const AtlasHeader *const hdr = (const AtlasHeader *)data;
    if (hdr->magic != ATLAS_MAGIC || hdr->version != ATLAS_VERSION
        || hdr->texW != UI_TEX_W || hdr->texH == 0 || hdr->texH > UI_FIXED_MAX_H
        || hdr->fontCount > 40U || hdr->spriteCount > 256U)
    {
        free(data);
        VITA_LOG("[UI2] invalid atlas header in %s\n", ATLAS_REL_PATH);
        return false;
    }

    const size_t fontsOff = sizeof(AtlasHeader);
    const size_t glyphsOff = fontsOff + (size_t)hdr->fontCount * sizeof(AtlasFont);
    if (size < glyphsOff)
    {
        free(data);
        return false;
    }

    const AtlasFont *const fonts = (const AtlasFont *)(data + fontsOff);
    uint32_t totalGlyphs = 0;
    for (uint32_t i = 0; i < hdr->fontCount; i++)
    {
        if (fonts[i].firstGlyph != totalGlyphs)
        {
            free(data);
            return false;
        }
        totalGlyphs += fonts[i].glyphCount;
    }

    const size_t spritesOff = glyphsOff + (size_t)totalGlyphs * sizeof(AtlasGlyph);
    const size_t pixelsOff = spritesOff + (size_t)hdr->spriteCount * sizeof(AtlasSprite);
    const size_t pixelBytes = (size_t)hdr->texW * (size_t)hdr->texH * sizeof(uint16_t);
    if (size < pixelsOff + pixelBytes)
    {
        free(data);
        return false;
    }

    sAtlasFileBuf = data;
    sHdr = hdr;
    sFonts = fonts;
    sGlyphs = (const AtlasGlyph *)(data + glyphsOff);
    sSprites = (const AtlasSprite *)(data + spritesOff);
    sPixels = (const uint16_t *)(data + pixelsOff);
    __atomic_store_n(&sReady, true, __ATOMIC_RELEASE);

    VITA_LOG("[UI2] loaded %s: %ux%u, %u fonts (%u glyphs), %u sprites\n",
             ATLAS_REL_PATH, hdr->texW, hdr->texH, hdr->fontCount, totalGlyphs, hdr->spriteCount);
    return true;
}

void UiAssets_Unload(void)
{
    __atomic_store_n(&sReady, false, __ATOMIC_RELEASE);
    free(sAtlasFileBuf);
    sAtlasFileBuf = NULL;
    sHdr = NULL;
    sFonts = NULL;
    sGlyphs = NULL;
    sSprites = NULL;
    sPixels = NULL;
}

bool UiAssets_Ready(void)
{
    return __atomic_load_n(&sReady, __ATOMIC_ACQUIRE);
}

const uint16_t *UiAssets_Pixels(uint16_t *const outW, uint16_t *const outH)
{
    if (!UiAssets_Ready())
        return NULL;
    if (outW) *outW = (uint16_t)sHdr->texW;
    if (outH) *outH = (uint16_t)sHdr->texH;
    return sPixels;
}

uint32_t UiAssets_CellsY(void)
{
    if (!UiAssets_Ready())
        return UI_FIXED_MAX_H;
    return ((sHdr->texH + UI_CELL - 1U) / UI_CELL) * UI_CELL;
}

uint32_t UiAssets_CellCount(void)
{
    if (!UiAssets_Ready())
        return 0U;
    const uint32_t cellsY = UiAssets_CellsY();
    if (cellsY >= UI_TEX_H)
        return 0U;
    return ((UI_TEX_H - cellsY) / UI_CELL) * UI_CELL_COLS;
}

void UiAssets_TexSize(uint16_t *const outW, uint16_t *const outH)
{
    if (outW) *outW = UI_TEX_W;
    if (outH) *outH = UI_TEX_H;
}

void UiAssets_White(uint16_t *const x, uint16_t *const y)
{
    if (!UiAssets_Ready())
    {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    if (x) *x = (uint16_t)sHdr->whiteX;
    if (y) *y = (uint16_t)sHdr->whiteY;
}

bool UiAssets_Sprite(const uint16_t id, uint16_t *const x, uint16_t *const y, uint16_t *const w, uint16_t *const h)
{
    if (!UiAssets_Ready() || id >= sHdr->spriteCount)
        return false;
    const AtlasSprite *const sp = &sSprites[id];
    if (x) *x = sp->x;
    if (y) *y = sp->y;
    if (w) *w = sp->w;
    if (h) *h = sp->h;
    return true;
}

// ---------------- Text measurement & drawing ----------------

// The two faces screens ask for are roles: Press Start 2P = titles, VT323 = body text.
// Each theme maps them to its own natural font faces defined in ui_theme.c.
static uint8_t ThemeFace(const uint8_t face)
{
    if (gUiTheme == NULL)
        return face;
    if (face == UI_FACE_PS2P)
        return gUiTheme->faceTitle;
    if (face == UI_FACE_VT323)
        return gUiTheme->faceBody;
    return face;
}

static const AtlasFont *FindFont(uint8_t face, const uint8_t px)
{
    if (!UiAssets_Ready())
        return NULL;
    face = ThemeFace(face);
    const AtlasFont *best = NULL;
    for (uint32_t i = 0; i < sHdr->fontCount; i++)
    {
        if (sFonts[i].face != face)
            continue;
        if (sFonts[i].px == px)
            return &sFonts[i];
        if (best == NULL || abs((int)sFonts[i].px - (int)px) < abs((int)best->px - (int)px))
            best = &sFonts[i];
    }
    return best != NULL ? best : &sFonts[0];
}

static uint32_t NextUtf8(const char **const p)
{
    const uint8_t *s = (const uint8_t *)*p;
    const uint32_t b0 = s[0];
    if (b0 < 0x80U)
    {
        *p = (const char *)(s + 1);
        return b0;
    }
    if ((b0 & 0xE0U) == 0xC0U && (s[1] & 0xC0U) == 0x80U)
    {
        *p = (const char *)(s + 2);
        return ((b0 & 0x1FU) << 6) | (s[1] & 0x3FU);
    }
    if ((b0 & 0xF0U) == 0xE0U && (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U)
    {
        *p = (const char *)(s + 3);
        return ((b0 & 0x0FU) << 12) | ((s[1] & 0x3FU) << 6) | (s[2] & 0x3FU);
    }
    *p = (const char *)(s + 1);
    return '?';
}

static const AtlasGlyph *FindGlyphBinary(const AtlasFont *const font, const uint32_t cp)
{
    uint32_t lo = font->firstGlyph;
    uint32_t hi = font->firstGlyph + font->glyphCount;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2U;
        const uint32_t mcp = sGlyphs[mid].cp;
        if (mcp == cp)
            return &sGlyphs[mid];
        if (mcp < cp)
            lo = mid + 1U;
        else
            hi = mid;
    }
    return NULL;
}

static uint32_t AccentFallback(const uint32_t cp)
{
    switch (cp)
    {
    case 0x00E9u: return 'e';
    case 0x00C9u: return 'E';
    case 0x00E1u: return 'a';
    case 0x00EDu: return 'i';
    case 0x00F3u: return 'o';
    case 0x00FAu: return 'u';
    case 0x00F1u: return 'n';
    case 0x00D1u: return 'N';
    default:      return '?';
    }
}

static const AtlasGlyph *LookupGlyph(const AtlasFont *const font, const uint32_t cp)
{
    const AtlasGlyph *g = FindGlyphBinary(font, cp);
    if (g != NULL)
        return g;
    if (cp >= 0x80U)
        g = FindGlyphBinary(font, AccentFallback(cp));
    if (g == NULL)
        g = FindGlyphBinary(font, '?');
    return g;
}

int32_t UiText_LineHeight(const uint8_t face, const uint8_t px)
{
    const AtlasFont *const f = FindFont(face, px);
    return (f != NULL) ? f->lineH : (int32_t)px;
}

int32_t UiText_CapMid(const uint8_t face, const uint8_t px)
{
    const AtlasFont *const f = FindFont(face, px);
    if (f == NULL)
        return (int32_t)px / 2;
    const AtlasGlyph *const g = LookupGlyph(f, 'H');
    if (g != NULL && g->h > 0)
        return (int32_t)f->ascent + (int32_t)g->yoff + (int32_t)g->h / 2;
    return (int32_t)f->ascent / 2;
}

int32_t UiText_Width(const uint8_t face, const uint8_t px, const char *s)
{
    const AtlasFont *const f = FindFont(face, px);
    if (f == NULL || s == NULL)
        return 0;
    int32_t w = 0;
    while (*s != '\0')
    {
        const uint32_t cp = NextUtf8(&s);
        const AtlasGlyph *const g = LookupGlyph(f, cp);
        if (g != NULL)
            w += g->adv;
    }
    return w;
}

int32_t UiText_InkWidth(const uint8_t face, const uint8_t px, const char *s)
{
    return UiText_Width(face, px, s);
}

int32_t UiText_Draw(const uint8_t face, const uint8_t px, int32_t x, const int32_t y, const char *s, const uint32_t rgba)
{
    const AtlasFont *const f = FindFont(face, px);
    if (f == NULL || s == NULL)
        return 0;
    const int32_t startX = x;
    const int32_t baseY = y + f->ascent;
    while (*s != '\0')
    {
        const uint32_t cp = NextUtf8(&s);
        const AtlasGlyph *const g = LookupGlyph(f, cp);
        if (g == NULL)
            continue;
        if (g->w > 0 && g->h > 0)
            UiDraw_Quad(x + g->xoff, baseY + g->yoff, g->w, g->h, g->x, g->y, g->x + g->w, g->y + g->h, rgba);
        x += g->adv;
    }
    return x - startX;
}

int32_t UiText_DrawCentred(const uint8_t face, const uint8_t px,
                           const int32_t boxX, const int32_t boxY, const int32_t boxW, const int32_t boxH,
                           const char *const s, const uint32_t rgba)
{
    if (s == NULL || s[0] == '\0')
        return 0;
    const int32_t tw = UiText_Width(face, px, s);
    const int32_t mid = UiText_CapMid(face, px);
    return UiText_Draw(face, px, boxX + (boxW - tw) / 2, boxY + boxH / 2 - mid, s, rgba);
}

int32_t UiText_DrawMid(const uint8_t face, const uint8_t px,
                       const int32_t x, const int32_t boxY, const int32_t boxH,
                       const char *const s, const uint32_t rgba)
{
    if (s == NULL || s[0] == '\0')
        return 0;
    const int32_t mid = UiText_CapMid(face, px);
    return UiText_Draw(face, px, x, boxY + boxH / 2 - mid, s, rgba);
}
