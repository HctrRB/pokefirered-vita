// Run-time sprites of the native UI: original GBA sprites (Pokémon icons, front sprites,
// item icons, footprints, trainer portraits, PC wallpapers, mail stationery) decoded from the
// game's own ROM tables into 64x64 cells of the UI texture below UiAssets_CellsY.
//
// Threading:
// - Core 1 owns the cache (sCellKey/Used/Upload). On a miss, Decode() unpacks into a staging slot
//   of an SPSC ring (RGBA5551) and publishes sHead with a release store before the draw list.
// - Core 0 (Ui_Render) consumes the ring with the UI texture bound and writes each slot directly
//   into the vitaGL texture buffer (vglGetTexDataPointer + Ui_UnpackTexel), then draws.

#include <string.h>
#include "ui_internal.h"
#include "global.h"
#include "data.h"
#include "decompress.h"
#include "graphics.h"
#include "item_menu_icons.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "constants/items.h"
#include "constants/species.h"
#include "constants/trainers.h"

#ifdef PLATFORM_VITA
#include <vitaGL.h>
#else
#include <GL/gl.h>
#endif

extern const u8 *const gMonFootprintTable[];
extern bool8 PokeStorage_WallpaperForUi(u8 wallpaperId, const u32 **tiles, const u32 **tilemap, u16 *pal32);
extern void Mail_StationeryForUi(uint8_t mailType, const uint32_t **tiles, const uint32_t **tilemap, uint16_t *palette);

static inline uint16_t To5551(uint16_t bgr);

#define LZ77_MAGIC 0x10u
#define UI_MON_PIC_MAX_FRAMES 4 // Castform / Deoxys multi-frame sheets in gMonFrontPicTable fit 8192 B

// Decompresses an LZ77 block when valid and within dstCap.
static bool UnpackLz(const u32 *src, void *dst, uint32_t dstCap)
{
    if (src == NULL || dst == NULL)
        return false;
    const uint32_t header = src[0];
    if ((header & 0xFFu) != LZ77_MAGIC)
        return false;
    const uint32_t size = header >> 8;
    if (size == 0u || size > dstCap)
        return false;
    LZDecompressWram(src, dst);
    return true;
}

// Decompresses LZ77 data or copies raw uncompressed fallback bytes within dstCap.
static bool UnpackLzOrRaw(const void *src, void *dst, uint32_t dstCap, uint32_t rawFallbackBytes)
{
    if (src == NULL || dst == NULL)
        return false;
    if ((*(const uint8_t *)src) == LZ77_MAGIC)
        return UnpackLz((const u32 *)src, dst, dstCap);
    if (rawFallbackBytes == 0u || rawFallbackBytes > dstCap)
        return false;
    memcpy(dst, src, rawFallbackBytes);
    return true;
}

// PC box wallpaper (160 x 144), composed once and sliced into 3 x 3 cells.
// Tilemap entries use palette 1 and 2 (mapping to banks 0 and 1 of pal32); color 0 is transparent.
#define WALL_W 160
#define WALL_H 144
static uint16_t sWall[WALL_W * WALL_H];
static int32_t sWallId = -1;

static void ComposeWallpaper(int32_t id)
{
    static uint8_t tiles[256 * 32];
    static uint16_t tmap[32 * 32];
    static uint16_t pal32[32];
    const u32 *tilesLz = NULL;
    const u32 *mapLz = NULL;

    memset(tiles, 0, sizeof(tiles));
    memset(tmap, 0, sizeof(tmap));
    if (!PokeStorage_WallpaperForUi((u8)id, &tilesLz, &mapLz, pal32))
        return;
    if (!UnpackLz(tilesLz, tiles, sizeof(tiles)) || !UnpackLz(mapLz, tmap, sizeof(tmap)))
        return;

    uint16_t pal[2][16];
    for (int32_t p = 0; p < 2; p++)
        for (int32_t i = 0; i < 16; i++)
            pal[p][i] = i ? To5551(pal32[p * 16 + i]) : 0;

    memset(sWall, 0, sizeof(sWall));
    for (int32_t t = 0; t < 20 * 18; t++)
    {
        const uint16_t e = tmap[t];
        const int32_t p = (e >> 12) - 1;
        if (e == 0 || p < 0 || p > 1)
            continue;
        const uint16_t tileIdx = e & 0x3FF;
        if (tileIdx >= 256)
            continue;
        const uint8_t *tile = &tiles[tileIdx * 32];
        const bool hf = (e >> 10) & 1;
        const bool vf = (e >> 11) & 1;
        for (int32_t y = 0; y < 8; y++)
            for (int32_t x = 0; x < 8; x++)
            {
                const int32_t sx = hf ? 7 - x : x;
                const int32_t sy = vf ? 7 - y : y;
                const uint8_t b = tile[sy * 4 + sx / 2];
                const uint8_t v = (sx & 1) ? (b >> 4) : (b & 15);
                if (v)
                    sWall[((t / 20) * 8 + y) * WALL_W + (t % 20) * 8 + x] = pal[p][v];
            }
    }
    sWallId = id;
}

// A full-screen GBA picture (240 x 160) composed for mail stationery.
#define PIC_W 240
#define PIC_H 160
static uint16_t sPic[PIC_W * PIC_H];
static uint32_t sPicKey;

static void PutPicTile(const uint8_t *tiles, uint16_t tileCount, uint16_t entry, int32_t tx, int32_t ty,
                       const uint16_t (*pals)[16], int32_t banks)
{
    const uint16_t t = entry & 0x3FF;
    if (t >= tileCount)
        return;
    const int32_t bank = (entry >> 12) < banks ? (entry >> 12) : 0;
    const bool hf = (entry >> 10) & 1;
    const bool vf = (entry >> 11) & 1;
    const uint8_t *tile = &tiles[t * 32];
    for (int32_t y = 0; y < 8; y++)
        for (int32_t x = 0; x < 8; x++)
        {
            const int32_t sx = hf ? 7 - x : x;
            const int32_t sy = vf ? 7 - y : y;
            const uint8_t b = tile[sy * 4 + sx / 2];
            const uint8_t v = (sx & 1) ? (b >> 4) : (b & 15);
            if (v)
                sPic[(ty * 8 + y) * PIC_W + tx * 8 + x] = pals[bank][v];
        }
}

static bool ComposePicture(uint32_t key, const uint32_t *tilesLz, const uint32_t *mapLz, int32_t fill, const uint16_t *bgr, int32_t banks)
{
    static uint8_t tiles[0x2000];
    static uint16_t tmap[32 * 32];
    static uint16_t pals[16][16];
    if (banks < 1 || banks > 16 || !UnpackLz(tilesLz, tiles, sizeof(tiles)) || !UnpackLz(mapLz, tmap, sizeof(tmap)))
        return false;
    for (int32_t p = 0; p < banks; p++)
        for (int32_t i = 0; i < 16; i++)
            pals[p][i] = To5551(bgr[p * 16 + i]);
    const uint16_t count = (uint16_t)((tilesLz[0] >> 8) / 32);
    for (int32_t i = 0; i < PIC_W * PIC_H; i++)
        sPic[i] = pals[0][0];
    if (fill >= 0)
        for (int32_t ty = 0; ty < PIC_H / 8; ty++)
            for (int32_t tx = 0; tx < PIC_W / 8; tx++)
                PutPicTile(tiles, count, (uint16_t)fill, tx, ty, pals, banks);
    for (int32_t ty = 0; ty < PIC_H / 8; ty++)
        for (int32_t tx = 0; tx < PIC_W / 8; tx++)
            PutPicTile(tiles, count, tmap[ty * 32 + tx], tx, ty, pals, banks);
    sPicKey = key;
    return true;
}

static void ComposeMail(uint32_t key, int32_t mailType)
{
    const uint32_t *tilesLz = NULL;
    const uint32_t *mapLz = NULL;
    uint16_t bgr[16];
    Mail_StationeryForUi((uint8_t)mailType, &tilesLz, &mapLz, bgr);
    ComposePicture(key, tilesLz, mapLz, 1, bgr, 1);
}

#define UI_SPR_RING 64
#define UI_SPR_DECODES_PER_TICK 16
#define UI_SPR_IDLE_TICKS 4

enum
{
    KIND_MON_ICON = 1,
    KIND_MON_FRONT,
    KIND_MON_FRONT_SHINY,
    KIND_ITEM,
    KIND_FOOTPRINT,
    KIND_TRAINER,
    KIND_WALLPAPER,
    KIND_MAIL,
};
#define PIC_PARTS_X 4

#define KEY(kind, id) (((uint32_t)(kind) << 16) | (uint32_t)(id))

// ---------- Core 1: cache ----------
static uint32_t sCellKey[UI_CELL_COUNT_MAX];
static uint32_t sCellUsed[UI_CELL_COUNT_MAX];
static uint32_t sCellUpload[UI_CELL_COUNT_MAX];
static uint32_t sTick;
static uint32_t sDecodes, sDecodesLast;

// ---------- Ring (Core 1 writes head, Core 0 writes tail) ----------
typedef struct
{
    uint16_t cell;
    uint16_t pixels[UI_CELL * UI_CELL];
} UiSprUpload;

static UiSprUpload sRing[UI_SPR_RING];
static uint32_t sHead, sTail;

static inline uint16_t To5551(uint16_t bgr)
{
    const uint16_t r = bgr & 31;
    const uint16_t g = (bgr >> 5) & 31;
    const uint16_t b = (bgr >> 10) & 31;
    return (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1);
}

static void Tiles4bpp(uint16_t *dst, int32_t x0, int32_t y0, const uint8_t *tiles, int32_t tilesW, int32_t tilesH,
                      const uint16_t *bgrPal)
{
    if (x0 < 0 || y0 < 0 || tilesW <= 0 || tilesH <= 0 || x0 + tilesW * 8 > UI_CELL || y0 + tilesH * 8 > UI_CELL)
        return;
    uint16_t pal[16];
    pal[0] = 0;
    for (int32_t i = 1; i < 16; i++)
        pal[i] = To5551(bgrPal[i]);
    for (int32_t ty = 0; ty < tilesH; ty++)
        for (int32_t tx = 0; tx < tilesW; tx++)
        {
            const uint8_t *tile = &tiles[(ty * tilesW + tx) * 32];
            for (int32_t py = 0; py < 8; py++)
                for (int32_t px = 0; px < 8; px += 2)
                {
                    const uint8_t b = tile[py * 4 + px / 2];
                    uint16_t *row = &dst[(y0 + ty * 8 + py) * UI_CELL + x0 + tx * 8 + px];
                    row[0] = pal[b & 15];
                    row[1] = pal[b >> 4];
                }
        }
}

static bool Decode(uint32_t key, uint16_t *dst)
{
    const uint32_t kind = key >> 16;
    const uint16_t id = (uint16_t)key;
    memset(dst, 0, UI_CELL * UI_CELL * sizeof(uint16_t));
    switch (kind)
    {
    case KIND_MON_ICON:
    {
        if (id == SPECIES_NONE || id > SPECIES_EGG)
            return false;
        const uint8_t *tiles = gMonIconTable[id];
        const uint16_t *pal = GetValidMonIconPalettePtr(id);
        if (tiles == NULL || pal == NULL)
            return false;
        Tiles4bpp(dst, 0, 0, tiles, 4, 4, pal);
        Tiles4bpp(dst, 0, 32, tiles + 512, 4, 4, pal);
        return true;
    }
    case KIND_MON_FRONT:
    case KIND_MON_FRONT_SHINY:
    {
        if (id == SPECIES_NONE || id > SPECIES_EGG)
            return false;
        static uint8_t tiles[(UI_CELL * UI_CELL / 2) * UI_MON_PIC_MAX_FRAMES];
        static uint16_t pal[16 * 16];
        const void *picSrc = gMonFrontPicTable[id].data;
        const void *palSrc = (kind == KIND_MON_FRONT_SHINY ? gMonShinyPaletteTable : gMonPaletteTable)[id].data;
        if (!UnpackLzOrRaw(picSrc, tiles, sizeof(tiles), UI_CELL * UI_CELL / 2)
            || !UnpackLzOrRaw(palSrc, pal, sizeof(pal), 32))
            return false;
        Tiles4bpp(dst, 0, 0, tiles, 8, 8, pal);
        return true;
    }
    case KIND_ITEM:
    {
        if (id >= ITEMS_COUNT)
            return false;
        static uint8_t tiles[512];
        static uint16_t pal[16];
        const void *pic = GetItemIconGfxPtr(id, ITEMICON_TILES);
        const void *palGfx = GetItemIconGfxPtr(id, ITEMICON_PAL);
        if (!UnpackLzOrRaw(pic, tiles, sizeof(tiles), 9 * 32)
            || !UnpackLzOrRaw(palGfx, pal, sizeof(pal), 32))
            return false;
        Tiles4bpp(dst, 0, 0, tiles, 3, 3, pal);
        return true;
    }
    case KIND_TRAINER:
    {
        static uint8_t tiles[(UI_CELL * UI_CELL / 2) * UI_MON_PIC_MAX_FRAMES];
        static uint16_t pal[16];
        if (id > TRAINER_PIC_PAINTER
            || !UnpackLzOrRaw(gTrainerFrontPicTable[id].data, tiles, sizeof(tiles), UI_CELL * UI_CELL / 2)
            || !UnpackLzOrRaw(gTrainerFrontPicPaletteTable[id].data, pal, sizeof(pal), 32))
            return false;
        Tiles4bpp(dst, 0, 0, tiles, 8, 8, pal);
        return true;
    }
    case KIND_FOOTPRINT:
    {
        if (id == SPECIES_NONE || id > SPECIES_EGG)
            return false;
        const uint8_t *fp = gMonFootprintTable[id];
        if (fp == NULL)
            return false;
        for (int32_t t = 0; t < 4; t++)
            for (int32_t py = 0; py < 8; py++)
                for (int32_t px = 0; px < 8; px++)
                    if ((fp[t * 8 + py] >> px) & 1)
                        dst[((t / 2) * 8 + py) * UI_CELL + (t % 2) * 8 + px] = 0xFFFF;
        return true;
    }
    case KIND_WALLPAPER:
    {
        const int32_t wid = id >> 4;
        const int32_t part = id & 15;
        if (wid != sWallId)
            ComposeWallpaper(wid);
        if (wid != sWallId)
            return false;
        const int32_t x0 = (part % 3) * UI_CELL;
        const int32_t y0 = (part / 3) * UI_CELL;
        for (int32_t y = 0; y < UI_CELL && y0 + y < WALL_H; y++)
            for (int32_t x = 0; x < UI_CELL && x0 + x < WALL_W; x++)
                dst[y * UI_CELL + x] = sWall[(y0 + y) * WALL_W + x0 + x];
        return true;
    }
    case KIND_MAIL:
    {
        const int32_t pic = id >> 4;
        const int32_t part = id & 15;
        const uint32_t picKey = KEY(kind, pic);
        if (sPicKey != picKey)
            ComposeMail(picKey, pic);
        if (sPicKey != picKey)
            return false;
        const int32_t x0 = (part % PIC_PARTS_X) * UI_CELL;
        const int32_t y0 = (part / PIC_PARTS_X) * UI_CELL;
        for (int32_t y = 0; y < UI_CELL && y0 + y < PIC_H; y++)
            for (int32_t x = 0; x < UI_CELL && x0 + x < PIC_W; x++)
                dst[y * UI_CELL + x] = sPic[(y0 + y) * PIC_W + x0 + x];
        return true;
    }
    default:
        return false;
    }
}

static int32_t Lookup(uint32_t key)
{
    const uint32_t tail = __atomic_load_n(&sTail, __ATOMIC_ACQUIRE);
    int32_t freeCell = -1;
    int32_t oldest = -1;
    const int32_t cellCount = (int32_t)UiAssets_CellCount();
    for (int32_t c = 0; c < cellCount; c++)
    {
        if (sCellKey[c] == key)
        {
            sCellUsed[c] = sTick;
            return c;
        }
        if (sCellKey[c] == 0)
        {
            if (freeCell < 0)
                freeCell = c;
        }
        else if ((int32_t)(tail - sCellUpload[c]) >= 0
                 && (oldest < 0 || sCellUsed[c] < sCellUsed[oldest]))
        {
            oldest = c;
        }
    }
    const int32_t victim = freeCell >= 0 ? freeCell
                         : (oldest >= 0 && sTick - sCellUsed[oldest] >= UI_SPR_IDLE_TICKS) ? oldest : -1;
    if (victim < 0 || sDecodes >= UI_SPR_DECODES_PER_TICK)
        return -1;
    if (sHead - tail >= UI_SPR_RING)
        return -1;
    UiSprUpload *up = &sRing[sHead % UI_SPR_RING];
    sDecodes++;
    if (!Decode(key, up->pixels))
        return -1;
    up->cell = (uint16_t)victim;
    const uint32_t nextHead = sHead + 1;
    __atomic_store_n(&sHead, nextHead, __ATOMIC_RELEASE);
    sCellKey[victim] = key;
    sCellUsed[victim] = sTick;
    sCellUpload[victim] = nextHead;
    return victim;
}

static bool DrawCell(uint32_t key, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
                     int32_t x, int32_t y, int32_t scale, uint32_t rgba)
{
    const int32_t c = Lookup(key);
    if (c < 0)
        return false;
    if (scale < 1)
        scale = 1;
    const uint32_t u = (uint32_t)(c % UI_CELL_COLS) * UI_CELL + (uint32_t)sx;
    const uint32_t v = UiAssets_CellsY() + (uint32_t)(c / UI_CELL_COLS) * UI_CELL + (uint32_t)sy;
    UiDraw_Quad(x, y, sw * scale, sh * scale, u, v, u + (uint32_t)sw, v + (uint32_t)sh, rgba);
    return true;
}

bool UiSpr_MonIcon(uint16_t species, uint8_t frame, int32_t x, int32_t y, int32_t scale)
{
    if (species == SPECIES_NONE || species > SPECIES_EGG)
        return false;
    return DrawCell(KEY(KIND_MON_ICON, species), 0, (frame & 1) ? 32 : 0, 32, 32, x, y, scale, 0xFFFFFFFFu);
}

bool UiSpr_MonFront(uint16_t species, bool shiny, int32_t x, int32_t y, int32_t scale)
{
    if (species == SPECIES_NONE || species > SPECIES_EGG)
        return false;
    return DrawCell(KEY(shiny ? KIND_MON_FRONT_SHINY : KIND_MON_FRONT, species), 0, 0, 64, 64, x, y, scale, 0xFFFFFFFFu);
}

bool UiSpr_ItemIcon(uint16_t itemId, int32_t x, int32_t y, int32_t scale)
{
    if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
        return false;
    return DrawCell(KEY(KIND_ITEM, itemId), 0, 0, 24, 24, x, y, scale, 0xFFFFFFFFu);
}

bool UiSpr_Trainer(uint16_t trainerPic, int32_t x, int32_t y, int32_t scale)
{
    return DrawCell(KEY(KIND_TRAINER, trainerPic), 0, 0, 64, 64, x, y, scale, 0xFFFFFFFFu);
}

bool UiSpr_MonFrontTint(uint16_t species, int32_t x, int32_t y, int32_t scale, uint32_t rgba)
{
    if (species == SPECIES_NONE || species > SPECIES_EGG)
        return false;
    return DrawCell(KEY(KIND_MON_FRONT, species), 0, 0, 64, 64, x, y, scale, rgba);
}

bool UiSpr_MonFrontSized(uint16_t species, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (species == SPECIES_NONE || species > SPECIES_EGG)
        return false;
    const int32_t c = Lookup(KEY(KIND_MON_FRONT, species));
    if (c < 0)
        return false;
    const uint32_t u = (uint32_t)(c % UI_CELL_COLS) * UI_CELL;
    const uint32_t v = UiAssets_CellsY() + (uint32_t)(c / UI_CELL_COLS) * UI_CELL;
    UiDraw_Quad(x, y, w, h, u, v, u + 64, v + 64, 0xFFFFFFFFu);
    return true;
}

bool UiSpr_TrainerSized(uint16_t pic, int32_t x, int32_t y, int32_t w, int32_t h)
{
    const int32_t c = Lookup(KEY(KIND_TRAINER, pic));
    if (c < 0)
        return false;
    const uint32_t u = (uint32_t)(c % UI_CELL_COLS) * UI_CELL;
    const uint32_t v = UiAssets_CellsY() + (uint32_t)(c / UI_CELL_COLS) * UI_CELL;
    UiDraw_Quad(x, y, w, h, u, v, u + 64, v + 64, 0xFFFFFFFFu);
    return true;
}

bool UiSpr_Wallpaper(uint8_t wallpaperId, int32_t x, int32_t y, int32_t scale)
{
    bool all = true;
    for (int32_t part = 0; part < 9; part++)
    {
        const int32_t px = (part % 3) * UI_CELL;
        const int32_t py = (part / 3) * UI_CELL;
        const int32_t w = WALL_W - px < UI_CELL ? WALL_W - px : UI_CELL;
        const int32_t h = WALL_H - py < UI_CELL ? WALL_H - py : UI_CELL;
        all &= DrawCell(KEY(KIND_WALLPAPER, ((uint32_t)wallpaperId << 4) | (uint32_t)part), 0, 0, w, h,
                        x + px * scale, y + py * scale, scale, 0xFFFFFFFFu);
    }
    return all;
}

bool UiSpr_ZonePlate(uint8_t theme, bool underwater, int32_t x, int32_t y, int32_t w)
{
    (void)theme;
    (void)underwater;
    (void)x;
    (void)y;
    (void)w;
    return false;
}

static bool DrawPicture(uint32_t kind, uint32_t pic, int32_t x, int32_t y, int32_t scale)
{
    bool all = true;
    for (int32_t part = 0; part < PIC_PARTS_X * 3; part++)
    {
        const int32_t px = (part % PIC_PARTS_X) * UI_CELL;
        const int32_t py = (part / PIC_PARTS_X) * UI_CELL;
        const int32_t w = PIC_W - px < UI_CELL ? PIC_W - px : UI_CELL;
        const int32_t h = PIC_H - py < UI_CELL ? PIC_H - py : UI_CELL;
        all &= DrawCell(KEY(kind, (pic << 4) | (uint32_t)part), 0, 0, w, h,
                        x + px * scale, y + py * scale, scale, 0xFFFFFFFFu);
    }
    return all;
}

bool UiSpr_MailStationery(uint8_t mailType, int32_t x, int32_t y, int32_t scale)
{
    return DrawPicture(KIND_MAIL, mailType, x, y, scale);
}

bool UiSpr_Footprint(uint16_t species, int32_t x, int32_t y, int32_t scale, uint32_t rgba)
{
    if (species == SPECIES_NONE || species > SPECIES_EGG)
        return false;
    return DrawCell(KEY(KIND_FOOTPRINT, species), 0, 0, 16, 16, x, y, scale, rgba);
}

void UiSpr_BeginTick(void)
{
    sTick++;
    sDecodesLast = sDecodes;
    sDecodes = 0;
}

uint32_t UiSpr_DecodesLastTick(void)
{
    return sDecodesLast;
}

// ---------- Core 0 ----------
void UiSpr_FlushUploads(void)
{
    const uint32_t head = __atomic_load_n(&sHead, __ATOMIC_ACQUIRE);
    uint32_t tail = sTail;
    if (tail == head)
        return;
#ifdef PLATFORM_VITA
    uint32_t *const texels = (uint32_t *)vglGetTexDataPointer(GL_TEXTURE_2D);
#else
    uint32_t *const texels = NULL;
#endif
    for (; tail != head; tail++)
    {
        const UiSprUpload *up = &sRing[tail % UI_SPR_RING];
        if (up->cell >= UiAssets_CellCount())
            continue;
        const uint32_t cx = (uint32_t)(up->cell % UI_CELL_COLS) * UI_CELL;
        const uint32_t cy = UiAssets_CellsY() + (uint32_t)(up->cell / UI_CELL_COLS) * UI_CELL;
        if (texels != NULL)
        {
            for (uint32_t r = 0; r < UI_CELL; r++)
            {
                uint32_t *dst = &texels[(cy + r) * UI_TEX_W + cx];
                const uint16_t *src = &up->pixels[r * UI_CELL];
                for (uint32_t c = 0; c < UI_CELL; c++)
                    dst[c] = Ui_UnpackTexel(src[c]);
            }
        }
        else
        {
            static uint32_t cellRgba[UI_CELL * UI_CELL];
            for (uint32_t i = 0; i < UI_CELL * UI_CELL; i++)
                cellRgba[i] = Ui_UnpackTexel(up->pixels[i]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)cx, (GLint)cy, UI_CELL, UI_CELL, GL_RGBA, GL_UNSIGNED_BYTE, cellRgba);
        }
    }
    __atomic_store_n(&sTail, head, __ATOMIC_RELEASE);
}
