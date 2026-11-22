/*
 * Reader and emitter for voxel/buildings.bin (`VXB7` FireRed-3DS and `VXB8`/`VXB9`).
 */
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef PLATFORM_VITA
#include <psp2/kernel/threadmgr.h>
#endif

#ifndef PORT_LOG
#include "port_log.h"
#endif

#include "voxel_atlas.h"
#include "voxel_building.h"
#include "voxel_file.h"
#include "voxel_grade.h"
#include "voxel_lighting.h"
#include "voxel_relief.h"

#ifndef VOXEL_BUILDINGS_PATH
#define VOXEL_BUILDINGS_PATH "voxel/buildings.bin"
#endif

typedef struct
{
    uint16_t w, h;
    uint16_t texHeight;
    uint32_t offset;
    bool glow;
} BuildingPage;

typedef struct
{
    uint8_t w, h;
    uint16_t ground;
    uint32_t firstVertex, vertexCount, heights;
    bool isStatic;
    float top;
} BuildingModel;

typedef struct
{
    uint16_t model, page;
    int16_t ox, oy;
} BuildingPageModel;

typedef struct
{
    uint16_t layout, pageModel, x, y, ground, extraCount;
    uint32_t extraFirst;
    uint16_t when;
    uint8_t whenX, whenY;
} BuildingPlacement;

#define NO_CONDITION 0xFFFFu

static BuildingPage *sPages;
static uint32_t sPageCount;
static BuildingModel *sModels;
static uint32_t sModelCount;
static BuildingPageModel *sPageModels;
static uint32_t sPageModelCount;
static BuildingPlacement *sPlacements;
static uint32_t sPlacementCount;
static uint8_t *sHeights;
static uint16_t *sFootprints;
static uint16_t *sMasks;
static uint32_t sMaskCount;
static VoxelVertex *sVertices;
static uint32_t sVertexCount;
static uint8_t *sQuarters;
static uint8_t *sVariants;
static uint32_t sVariantCount;
static uint8_t *sDecks;
static uint32_t sDeckCount;

#define OWN_GROUND 0xFFFFu
static bool (*sVariantFits)(const VoxelMapInstance *inst, uint32_t layout, uint32_t metatile);
static float sMaxTop;
static FILE *sPageFile;

#define LIVE_METATILE_MASK 0x3FFu
#define LIVE_CACHE_SLOTS MAX_VOXEL_MAP_INSTANCES
static const uint16_t *(*sDrawingOf)(int32_t layoutId, int32_t *width, int32_t *height);
typedef struct
{
    bool valid;
    int32_t layout, header, originX, originY;
    int32_t chosen;
    uint32_t signature;
} LiveLayoutEntry;

static LiveLayoutEntry sLive[LIVE_CACHE_SLOTS];
static uint32_t sLiveNext;
#ifdef PLATFORM_VITA
static SceKernelLwMutexWork sBuildingCacheMutex;
static bool sBuildingCacheMutexReady;

static inline void CacheLock(void)
{
    if (sBuildingCacheMutexReady)
        sceKernelLockLwMutex(&sBuildingCacheMutex, 1, NULL);
}

static inline void CacheUnlock(void)
{
    if (sBuildingCacheMutexReady)
        sceKernelUnlockLwMutex(&sBuildingCacheMutex, 1);
}
#else
static inline void CacheLock(void) { }
static inline void CacheUnlock(void) { }
#endif

typedef struct
{
    float lo, hi, z;
} FrontFace;

#define FRONT_SETS 64u
#define FRONT_FACES 16u

typedef struct
{
    bool valid;
    int32_t layout, originX, originY, x, y;
    float height;
    uint32_t count;
    FrontFace face[FRONT_FACES];
} FrontCell;

static FrontCell sFrontCells[FRONT_SETS];
static bool sStaticsFullLogged;

typedef struct
{
    uint16_t page, layout;
    uint32_t offset;
    uint16_t blocks, lives;
    uint32_t firstLive, firstStatic;
} AnimRun;

typedef struct
{
    uint16_t texel, tile;
    uint8_t pixel, palette;
    uint16_t bg;
} AnimLive;

static AnimRun *sAnimRuns;
static uint32_t sAnimRunCount;
static AnimLive *sAnimLives;
static uint32_t sAnimLiveCount;
static uint16_t *sAnimPalettes;
static uint32_t sAnimPaletteCount;
static uint16_t *sAnimStatics;
static uint32_t sAnimStaticCount;

static uint32_t U16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t U32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static bool ReadRows(FILE *file, uint32_t count, uint32_t size, uint8_t *row,
                     bool (*take)(uint32_t, const uint8_t *))
{
    for (uint32_t i = 0; i < count; ++i)
        if (fread(row, 1, size, file) != size || !take(i, row))
            return false;
    return true;
}

static uint32_t sHeightBytes;
static void FreeCellGrids(void);

static bool TakePage(uint32_t i, const uint8_t *r)
{
    uint32_t w = U16(r), h = U16(r + 2) & 0x7FFFu, th = 8;

    while (th < h)
        th <<= 1;
    sPages[i].w = (uint16_t)w;
    sPages[i].h = (uint16_t)h;
    sPages[i].texHeight = (uint16_t)th;
    sPages[i].glow = (U16(r + 2) & 0x8000u) != 0;
    sPages[i].offset = U32(r + 4);
    return w >= 8 && w <= 1024 && (w & (w - 1)) == 0 && h > 0 && h % 8 == 0 && th <= 1024;
}

static bool TakeModel(uint32_t i, const uint8_t *r)
{
    BuildingModel *m = &sModels[i];
    m->w = r[0];
    m->h = r[1];
    m->ground = (uint16_t)U16(r + 2);
    m->firstVertex = U32(r + 4);
    m->vertexCount = U32(r + 8) & 0x7FFFFFFFu;
    m->isStatic = (U32(r + 8) & 0x80000000u) != 0;
    m->heights = U32(r + 12);
    m->top = 0.0f;
    return m->vertexCount <= sVertexCount && m->firstVertex <= sVertexCount - m->vertexCount
        && (uint32_t)m->w * m->h <= sHeightBytes && m->heights <= sHeightBytes - (uint32_t)m->w * m->h;
}

static bool TakePageModel(uint32_t i, const uint8_t *r)
{
    BuildingPageModel *pm = &sPageModels[i];
    pm->model = (uint16_t)U16(r);
    pm->page = (uint16_t)U16(r + 2);
    pm->ox = (int16_t)U16(r + 4);
    pm->oy = (int16_t)U16(r + 6);
    return pm->model < sModelCount && pm->page < sPageCount;
}

static bool TakePlacement(uint32_t i, const uint8_t *r)
{
    BuildingPlacement *p = &sPlacements[i];
    p->layout = (uint16_t)U16(r);
    p->pageModel = (uint16_t)U16(r + 2);
    p->x = (uint16_t)U16(r + 4);
    p->y = (uint16_t)U16(r + 6);
    p->ground = (uint16_t)U16(r + 8);
    p->extraCount = (uint16_t)U16(r + 10);
    p->extraFirst = U32(r + 12);
    p->when = NO_CONDITION;
    return p->pageModel < sPageModelCount
        && p->extraCount <= sVertexCount && p->extraFirst <= sVertexCount - p->extraCount;
}

static bool ReadAnims(FILE *file)
{
    uint8_t tail[8], head[12], row[20];
    long size, at;

    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 8
     || fseek(file, size - 8, SEEK_SET) != 0 || fread(tail, 1, sizeof(tail), file) != sizeof(tail))
        return false;
    if (memcmp(tail + 4, "ANIM", 4) != 0)
        return true;
    at = (long)U32(tail);
    if (at < 0 || at + (long)sizeof(head) > size - 8 || fseek(file, at, SEEK_SET) != 0
     || fread(head, 1, sizeof(head), file) != sizeof(head))
        return false;
    sAnimRunCount = U16(head);
    sAnimPaletteCount = U16(head + 2);
    sAnimLiveCount = U32(head + 4);
    sAnimStaticCount = U32(head + 8);
    if (sAnimRunCount == 0)
        return true;
    if (sAnimLiveCount > (unsigned long)(size - at) / 8u
     || sAnimStaticCount > (unsigned long)(size - at) / sizeof(uint16_t))
        return false;
    sAnimRuns = malloc(sAnimRunCount * sizeof(*sAnimRuns));
    sAnimPalettes = malloc(sAnimPaletteCount * 16u * sizeof(uint16_t) + 1);
    sAnimLives = malloc(sAnimLiveCount * sizeof(*sAnimLives) + 1);
    sAnimStatics = malloc(sAnimStaticCount * sizeof(uint16_t) + 1);
    if (!sAnimRuns || !sAnimPalettes || !sAnimLives || !sAnimStatics)
        return false;
    for (uint32_t i = 0; i < sAnimRunCount; ++i)
    {
        AnimRun *r = &sAnimRuns[i];

        if (fread(row, 1, 20, file) != 20)
            return false;
        r->page = (uint16_t)U16(row);
        r->layout = (uint16_t)U16(row + 2);
        r->offset = U32(row + 4);
        r->blocks = (uint16_t)U16(row + 8);
        r->lives = (uint16_t)U16(row + 10);
        r->firstLive = U32(row + 12);
        r->firstStatic = U32(row + 16);
        if (r->page >= sPageCount || r->blocks == 0
         || r->blocks * 128u > (uint32_t)sPages[r->page].w * sPages[r->page].h * 2u
         || r->offset > (uint32_t)sPages[r->page].w * sPages[r->page].h * 2u - r->blocks * 128u
         || r->lives > sAnimLiveCount || r->firstLive > sAnimLiveCount - r->lives
         || r->blocks * 64u > sAnimStaticCount || r->firstStatic > sAnimStaticCount - r->blocks * 64u)
            return false;
    }
    if (fread(sAnimPalettes, 16u * sizeof(uint16_t), sAnimPaletteCount, file) != sAnimPaletteCount)
        return false;
    for (uint32_t i = 0; i < sAnimLiveCount; ++i)
    {
        AnimLive *l = &sAnimLives[i];

        if (fread(row, 1, 8, file) != 8)
            return false;
        l->texel = (uint16_t)U16(row);
        l->tile = (uint16_t)U16(row + 2);
        l->pixel = row[4];
        l->palette = row[5];
        l->bg = (uint16_t)U16(row + 6);
        if (l->tile >= 1024u || l->pixel >= 64u || l->palette >= sAnimPaletteCount)
            return false;
    }
    if (fread(sAnimStatics, sizeof(uint16_t), sAnimStaticCount, file) != sAnimStaticCount)
        return false;
    for (uint32_t i = 0; i < sAnimRunCount; ++i)
        for (uint32_t k = 0; k < sAnimRuns[i].lives; ++k)
            if (sAnimLives[sAnimRuns[i].firstLive + k].texel >= sAnimRuns[i].blocks * 64u)
                return false;
    return true;
}

static void FreeAnims(void)
{
    free(sAnimRuns);
    free(sAnimLives);
    free(sAnimPalettes);
    free(sAnimStatics);
    sAnimRuns = NULL;
    sAnimLives = NULL;
    sAnimPalettes = NULL;
    sAnimStatics = NULL;
    sAnimRunCount = sAnimLiveCount = sAnimPaletteCount = sAnimStaticCount = 0;
}

bool VoxelBuildings_Init(void)
{
    uint8_t header[28], row[16];
    uint32_t conditions = 0;
    bool vxb7 = false, vxb8 = false, vxb9 = false;
    long offset;
    FILE *file;
    bool ok = false;

    VoxelBuildings_Shutdown();
#ifdef PLATFORM_VITA
    if (!sBuildingCacheMutexReady)
        sBuildingCacheMutexReady = (sceKernelCreateLwMutex(&sBuildingCacheMutex, "voxel_bld", 0, 0, NULL) >= 0);
#endif
    file = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
    if (file == NULL)
    {
        PORT_LOG("[VIDEO] VOXEL buildings: %s absent, houses stay extruded\n",
                 VOXEL_BUILDINGS_PATH);
        return false;
    }
    static char sBuildingsIoBuffer[65536];
    setvbuf(file, sBuildingsIoBuffer, _IOFBF, sizeof(sBuildingsIoBuffer));
    if (fread(header, 1, 24, file) != 24)
        goto done;
    vxb9 = memcmp(header, "VXB9", 4) == 0;
    vxb8 = memcmp(header, "VXB8", 4) == 0;
    vxb7 = memcmp(header, "VXB7", 4) == 0;
    if (!vxb7 && !vxb8 && !vxb9)
        goto done;
    if (vxb9)
    {
        if (fread(header + 24, 1, 4, file) != 4)
            goto done;
        conditions = U16(header + 24);
    }
    sPageCount = U16(header + 4);
    sModelCount = U16(header + 6);
    sPageModelCount = U16(header + 8);
    sPlacementCount = U16(header + 10);
    sHeightBytes = U16(header + 12);
    sMaskCount = U16(header + 14);
    sVertexCount = U32(header + 16);
    sVariantCount = U16(header + 20);
    sDeckCount = vxb7 ? 0u : U16(header + 22);

    if (sVertexCount > 0x7FFFFFFFu / sizeof(VoxelVertex))
        goto done;
    sPages = malloc(sPageCount * sizeof(*sPages) + 1);
    sModels = malloc(sModelCount * sizeof(*sModels) + 1);
    sPageModels = malloc(sPageModelCount * sizeof(*sPageModels) + 1);
    sPlacements = malloc(sPlacementCount * sizeof(*sPlacements) + 1);
    sHeights = malloc(sHeightBytes + 1);
    sFootprints = malloc(sHeightBytes * sizeof(uint16_t) + 1);
    sMasks = malloc(sMaskCount * 16 * sizeof(uint16_t) + 1);
    sVertices = malloc(sVertexCount * sizeof(VoxelVertex) + 1);
    sQuarters = malloc(sHeightBytes + 1);
    sVariants = malloc(sVariantCount * 6u + 1);
    sDecks = malloc(sDeckCount * 8u + 1);
    if (!sPages || !sModels || !sPageModels || !sPlacements || !sHeights || !sVertices
     || !sFootprints || !sMasks || !sQuarters || !sVariants || !sDecks)
        goto done;
    if (!ReadRows(file, sPageCount, 8, row, TakePage)
     || !ReadRows(file, sModelCount, 16, row, TakeModel)
     || !ReadRows(file, sPageModelCount, 8, row, TakePageModel)
     || !ReadRows(file, sPlacementCount, 16, row, TakePlacement))
        goto done;
    if (sHeightBytes > 0 && fread(sHeights, 1, sHeightBytes, file) != sHeightBytes)
        goto done;
    if ((sHeightBytes & 1) != 0 && fseek(file, 1, SEEK_CUR) != 0)
        goto done;
    if ((sHeightBytes > 0 && fread(sFootprints, sizeof(uint16_t), sHeightBytes, file) != sHeightBytes)
     || (sMaskCount > 0 && fread(sMasks, 16 * sizeof(uint16_t), sMaskCount, file) != sMaskCount)
     || (sHeightBytes > 0 && fread(sQuarters, 1, sHeightBytes, file) != sHeightBytes)
     || ((sHeightBytes & 1) != 0 && fseek(file, 1, SEEK_CUR) != 0)
     || (sVariantCount > 0 && fread(sVariants, 6, sVariantCount, file) != sVariantCount)
     || (sDeckCount > 0 && fread(sDecks, 8, sDeckCount, file) != sDeckCount))
        goto done;
    for (uint32_t i = 0; i < conditions; ++i)
    {
        BuildingPlacement *p;

        if (fread(row, 1, 6, file) != 6 || U16(row) >= sPlacementCount)
            goto done;
        p = &sPlacements[U16(row)];
        p->when = (uint16_t)U16(row + 2);
        p->whenX = row[4];
        p->whenY = row[5];
    }
    for (uint32_t i = 0; i < sHeightBytes; ++i)
        if (sFootprints[i] != 0xFFFF && sFootprints[i] >= sMaskCount)
            goto done;
    offset = ftell(file);
    if (offset < 0 || fseek(file, (4 - (offset & 3)) & 3, SEEK_CUR) != 0)
        goto done;
    if (sVertexCount > 0 && fread(sVertices, sizeof(VoxelVertex), sVertexCount, file) != sVertexCount)
        goto done;
    if (!ReadAnims(file))
        goto done;
    sMaxTop = 0.0f;
    for (uint32_t i = 0; i < sHeightBytes; ++i)
        if (sHeights[i] != 0xFF && sHeights[i] / 16.0f > sMaxTop)
            sMaxTop = sHeights[i] / 16.0f;
    for (uint32_t i = 0; i < sModelCount; ++i)
    {
        BuildingModel *m = &sModels[i];

        for (uint32_t c = 0; c < (uint32_t)m->w * m->h; ++c)
            if (sHeights[m->heights + c] != 0xFF && sHeights[m->heights + c] / 16.0f > m->top)
                m->top = sHeights[m->heights + c] / 16.0f;
    }
    ok = true;

done:
    fclose(file);
    if (!ok)
    {
        PORT_LOG("[ERROR] VOXEL buildings: %s is truncated or malformed\n",
                 VOXEL_BUILDINGS_PATH);
        VoxelBuildings_Shutdown();
        return false;
    }
    PORT_LOG("[VIDEO] VOXEL buildings: %u models on %u pages, %u placements, %u vertices, "
             "%u animated runs\n", sModelCount, sPageCount, sPlacementCount, sVertexCount,
             sAnimRunCount);
    return true;
}

void VoxelBuildings_Shutdown(void)
{
    free(sPages);
    free(sModels);
    free(sPageModels);
    free(sPlacements);
    free(sHeights);
    free(sFootprints);
    free(sMasks);
    free(sVertices);
    free(sQuarters);
    free(sVariants);
    CacheLock();
    memset(sLive, 0, sizeof(sLive));
    sLiveNext = 0;
    CacheUnlock();
    FreeAnims();
    free(sDecks);
    sQuarters = NULL;
    sVariants = NULL;
    sVariantCount = 0;
    sDecks = NULL;
    sDeckCount = 0;
    sPages = NULL;
    sModels = NULL;
    sPageModels = NULL;
    sPlacements = NULL;
    sHeights = NULL;
    sFootprints = NULL;
    sMasks = NULL;
    sMaskCount = 0;
    sVertices = NULL;
    sPageCount = sModelCount = sPageModelCount = sPlacementCount = sVertexCount = 0;
    sHeightBytes = 0;
    FreeCellGrids();
    memset(sFrontCells, 0, sizeof(sFrontCells));
    sMaxTop = 0.0f;
    if (sPageFile != NULL)
    {
        fclose(sPageFile);
        sPageFile = NULL;
    }
    VoxelBuildings_CloseWorkerFile();
}

float VoxelBuildings_MaxTop(void)
{
    return sMaxTop;
}

bool VoxelBuildings_PageSize(uint32_t page, uint32_t *width, uint32_t *height,
                             uint32_t *texHeight)
{
    if (page >= sPageCount)
        return false;
    *width = sPages[page].w;
    *height = sPages[page].h;
    *texHeight = sPages[page].texHeight;
    return true;
}

bool VoxelBuildings_ReadPage(uint32_t page, uint32_t first, uint32_t count, uint16_t *dest)
{
    if (page >= sPageCount || first > (uint32_t)sPages[page].w * sPages[page].h
     || count > (uint32_t)sPages[page].w * sPages[page].h - first)
        return false;
    if (sPageFile == NULL)
    {
        sPageFile = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
        if (sPageFile == NULL)
            return false;
        static char sPageIoBuffer[32768];
        setvbuf(sPageFile, sPageIoBuffer, _IOFBF, sizeof(sPageIoBuffer));
    }
    if (fseek(sPageFile, (long)(sPages[page].offset + first * sizeof(uint16_t)), SEEK_SET) != 0
     || fread(dest, sizeof(uint16_t), count, sPageFile) != count)
        return false;
    VoxelGrade_Texels(dest, count);
    return true;
}

static FILE *sWorkerPageFile = NULL;
static char sWorkerPageIoBuffer[32768];

bool VoxelBuildings_ReadPageWorker(uint32_t page, uint32_t first, uint32_t count, uint16_t *dest)
{
    if (page >= sPageCount || first > (uint32_t)sPages[page].w * sPages[page].h
     || count > (uint32_t)sPages[page].w * sPages[page].h - first)
        return false;
    if (sWorkerPageFile == NULL)
    {
        sWorkerPageFile = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
        if (sWorkerPageFile == NULL)
            return false;
        setvbuf(sWorkerPageFile, sWorkerPageIoBuffer, _IOFBF, sizeof(sWorkerPageIoBuffer));
    }
    if (fseek(sWorkerPageFile, (long)(sPages[page].offset + first * sizeof(uint16_t)), SEEK_SET) != 0
     || fread(dest, sizeof(uint16_t), count, sWorkerPageFile) != count)
        return false;
    VoxelGrade_Texels(dest, count);
    return true;
}

void VoxelBuildings_CloseWorkerFile(void)
{
    if (sWorkerPageFile != NULL)
    {
        fclose(sWorkerPageFile);
        sWorkerPageFile = NULL;
    }
}

uint32_t VoxelBuildings_AnimRunCount(void)
{
    return sAnimRunCount;
}

bool VoxelBuildings_AnimRun(uint32_t run, uint32_t *page, int32_t *layout, uint32_t *offset,
                            uint32_t *bytes)
{
    if (run >= sAnimRunCount)
        return false;
    *page = sAnimRuns[run].page;
    *layout = sAnimRuns[run].layout;
    *offset = sAnimRuns[run].offset;
    *bytes = sAnimRuns[run].blocks * 128u;
    return true;
}

bool VoxelBuildings_AnimRunDirty(uint32_t run, const uint8_t dirty[128])
{
    const AnimRun *r;

    if (run >= sAnimRunCount)
        return false;
    r = &sAnimRuns[run];
    for (uint32_t k = 0; k < r->lives; ++k)
    {
        uint32_t tile = sAnimLives[r->firstLive + k].tile;

        if (dirty[tile >> 3] & (1u << (tile & 7)))
            return true;
    }
    return false;
}

void VoxelBuildings_ComposeAnimRun(uint32_t run, const uint8_t *liveTiles, uint16_t *dest)
{
    const AnimRun *r;
    uint32_t count;

    if (run >= sAnimRunCount)
        return;
    r = &sAnimRuns[run];
    count = r->blocks * 64u;
    memcpy(dest, sAnimStatics + r->firstStatic, count * sizeof(uint16_t));
    for (uint32_t k = 0; k < r->lives; ++k)
    {
        const AnimLive *l = &sAnimLives[r->firstLive + k];
        uint8_t b = liveTiles[l->tile * 32u + (l->pixel >> 3) * 4u + ((l->pixel & 7u) >> 1)];
        uint32_t index = (l->pixel & 1u) ? (uint32_t)(b >> 4) : (uint32_t)(b & 15u);
        uint16_t c = index != 0 ? sAnimPalettes[l->palette * 16u + index] : 0;

        dest[l->texel] = c == l->bg ? 0 : c;
    }
    VoxelGrade_Texels(dest, count);
}

bool VoxelBuildings_PageHasGlow(uint32_t page)
{
    return page < sPageCount && sPages[page].glow;
}

bool VoxelBuildings_ReadGlow(uint32_t page, uint32_t first, uint32_t count, uint8_t *dest)
{
    uint32_t pixels;

    if (!VoxelBuildings_PageHasGlow(page) || sPageFile == NULL)
        return false;
    pixels = (uint32_t)sPages[page].w * sPages[page].h;
    if (first > pixels || count > pixels - first)
        return false;
    return fseek(sPageFile, (long)(sPages[page].offset + pixels * 2u + first), SEEK_SET) == 0
        && fread(dest, 1, count, sPageFile) == count;
}

bool VoxelBuildings_ReadGlowWorker(uint32_t page, uint32_t first, uint32_t count, uint8_t *dest)
{
    uint32_t pixels;

    if (!VoxelBuildings_PageHasGlow(page))
        return false;
    pixels = (uint32_t)sPages[page].w * sPages[page].h;
    if (first > pixels || count > pixels - first)
        return false;
    if (sWorkerPageFile == NULL)
    {
        sWorkerPageFile = VoxelFile_Open(VOXEL_BUILDINGS_PATH);
        if (sWorkerPageFile == NULL)
            return false;
        setvbuf(sWorkerPageFile, sWorkerPageIoBuffer, _IOFBF, sizeof(sWorkerPageIoBuffer));
    }
    return fseek(sWorkerPageFile, (long)(sPages[page].offset + pixels * 2u + first), SEEK_SET) == 0
        && fread(dest, 1, count, sWorkerPageFile) == count;
}

/* Pure stateless binary search over placements sorted by layout: safe across Core 0 and Core 2. */
static const BuildingPlacement *PlacementsOf(int32_t layout, uint32_t *count)
{
    uint32_t lo = 0, hi = sPlacementCount, n = 0;

    *count = 0;
    if (sPlacementCount == 0 || sPlacements == NULL)
        return NULL;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;

        if ((int32_t)sPlacements[mid].layout < layout)
            lo = mid + 1;
        else
            hi = mid;
    }
    while (lo + n < sPlacementCount && (int32_t)sPlacements[lo + n].layout == layout)
        ++n;
    *count = n;
    return n != 0u ? &sPlacements[lo] : NULL;
}

void VoxelBuildings_SetDrawings(const uint16_t *(*drawing)(int32_t layoutId, int32_t *width, int32_t *height))
{
    CacheLock();
    if (drawing != sDrawingOf)
    {
        memset(sLive, 0, sizeof(sLive));
        sLiveNext = 0;
    }
    sDrawingOf = drawing;
    CacheUnlock();
}

void VoxelBuildings_WorldChanged(void)
{
    CacheLock();
    memset(sLive, 0, sizeof(sLive));
    sLiveNext = 0;
    CacheUnlock();
}

static void Tally(const VoxelMapInstance *inst, const BuildingPlacement *p,
                  const uint16_t *drawing, const uint16_t *other, int32_t width, int32_t height,
                  uint32_t *covered, uint32_t *live, uint32_t *twin)
{
    const BuildingModel *m = &sModels[sPageModels[p->pageModel].model];

    *covered = *live = *twin = 0;
    for (int32_t cy = 0; cy < m->h; ++cy)
        for (int32_t cx = 0; cx < m->w; ++cx)
        {
            int32_t x = p->x + cx, y = p->y + cy;
            uint32_t own;

            if (x >= width || y >= height
             || sHeights[m->heights + (uint32_t)cy * m->w + (uint32_t)cx] == 0xFF)
                continue;
            own = drawing[(size_t)y * width + x] & LIVE_METATILE_MASK;
            ++*covered;
            if ((uint32_t)VoxelWorld_GetMetatileId(inst->originX + x, inst->originY + y) == own)
                ++*live;
            if (other != NULL && (other[(size_t)y * width + x] & LIVE_METATILE_MASK) == own)
                ++*twin;
        }
}

static uint32_t ShownAlone(const VoxelMapInstance *inst, int32_t layout, const uint16_t *drawing,
                           const uint16_t *other, int32_t width, int32_t height)
{
    uint32_t count, shown = 0;
    const BuildingPlacement *p = PlacementsOf(layout, &count);

    for (uint32_t i = 0; p != NULL && i < count; ++i)
    {
        uint32_t covered, live, twin;

        Tally(inst, &p[i], drawing, other, width, height, &covered, &live, &twin);
        if (covered != 0 && 2u * live > covered && 2u * twin <= covered)
            ++shown;
    }
    return shown;
}

static LiveLayoutEntry ComputeLive(const VoxelMapInstance *inst)
{
    const uint16_t *own, *other;
    int32_t width = 0, height = 0, otherWidth = 0, otherHeight = 0;
    LiveLayoutEntry entry;

    entry.valid = true;
    entry.layout = inst->layoutId;
    entry.header = inst->headerLayoutId;
    entry.originX = inst->originX;
    entry.originY = inst->originY;
    entry.chosen = inst->layoutId;
    entry.signature = 0;
    if (inst->headerLayoutId == inst->layoutId)
        return entry;
    own = sDrawingOf(inst->layoutId, &width, &height);
    other = sDrawingOf(inst->headerLayoutId, &otherWidth, &otherHeight);
    if (own == NULL || other == NULL || otherWidth != width || otherHeight != height)
        return entry;
    if (ShownAlone(inst, inst->headerLayoutId, other, own, width, height)
        > ShownAlone(inst, inst->layoutId, own, other, width, height))
    {
        entry.chosen = inst->headerLayoutId;
        entry.signature = (2166136261u ^ (uint32_t)entry.chosen) * 16777619u;
    }
    return entry;
}

static bool FindLiveEntry(const VoxelMapInstance *inst, LiveLayoutEntry *out)
{
    for (uint32_t i = 0; i < LIVE_CACHE_SLOTS; ++i)
    {
        if (sLive[i].valid && sLive[i].layout == inst->layoutId && sLive[i].header == inst->headerLayoutId
         && sLive[i].originX == inst->originX && sLive[i].originY == inst->originY)
        {
            *out = sLive[i];
            return true;
        }
    }
    return false;
}

static LiveLayoutEntry ResolveLive(const VoxelMapInstance *inst)
{
    LiveLayoutEntry entry;

    CacheLock();
    if (FindLiveEntry(inst, &entry))
    {
        CacheUnlock();
        return entry;
    }
    CacheUnlock();

    entry = ComputeLive(inst);

    CacheLock();
    LiveLayoutEntry existing;
    if (!FindLiveEntry(inst, &existing))
    {
        sLive[sLiveNext] = entry;
        sLiveNext = (sLiveNext + 1u) % LIVE_CACHE_SLOTS;
    }
    else
    {
        entry = existing;
    }
    CacheUnlock();
    return entry;
}

static int32_t BuildingLayout(const VoxelMapInstance *inst)
{
    if (!inst->live || sDrawingOf == NULL || sModels == NULL)
        return inst->layoutId;
    return ResolveLive(inst).chosen;
}

static const BuildingPlacement *LayoutPlacements(const VoxelMapInstance *inst, uint32_t *count)
{
    *count = 0;
    if (inst == NULL || sPlacementCount == 0)
        return NULL;
    return PlacementsOf(BuildingLayout(inst), count);
}

int32_t VoxelBuildings_LayoutOf(const VoxelMapInstance *inst)
{
    return inst == NULL ? -1 : BuildingLayout(inst);
}

uint32_t VoxelBuildings_LiveSignature(const VoxelMapInstance *inst)
{
    if (inst == NULL || !inst->live || sDrawingOf == NULL || sModels == NULL)
        return 0;
    return ResolveLive(inst).signature;
}

static bool PlacementLive(const VoxelMapInstance *inst, const BuildingPlacement *p)
{
    return p->when == NO_CONDITION
        || VoxelWorld_GetMetatileId(inst->originX + p->x + p->whenX,
                                    inst->originY + p->y + p->whenY) == (int32_t)p->when;
}

float VoxelBuildings_LayoutTop(const VoxelMapInstance *inst)
{
    uint32_t count;
    const BuildingPlacement *p;
    float top = 0.0f;

    if (inst == NULL || sModels == NULL)
        return 0.0f;
    p = PlacementsOf(BuildingLayout(inst), &count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const BuildingModel *m = &sModels[sPageModels[p[i].pageModel].model];

        for (uint32_t k = 0; k < (uint32_t)m->w * m->h; ++k)
            if (sHeights[m->heights + k] != 0xFF && sHeights[m->heights + k] / 16.0f > top)
                top = sHeights[m->heights + k] / 16.0f;
    }
    return top;
}

int32_t VoxelBuildings_PageOf(const VoxelMapInstance *inst)
{
    uint32_t count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);

    return p ? (int32_t)sPageModels[p[0].pageModel].page : -1;
}

#define ROW_INDEXES MAX_VOXEL_MAP_INSTANCES

typedef struct
{
    int32_t layout;
    int32_t rows;
    uint32_t *start;
    uint16_t *placements;
} RowIndex;

static RowIndex sRowIndexes[ROW_INDEXES];
static uint32_t sRowIndexNext;

static void FreeRowIndex(RowIndex *r)
{
    free(r->start);
    free(r->placements);
    r->start = NULL;
    r->placements = NULL;
    r->layout = 0;
}

static void FreeCellGrids(void)
{
    CacheLock();
    for (uint32_t i = 0; i < ROW_INDEXES; ++i)
        FreeRowIndex(&sRowIndexes[i]);
    sRowIndexNext = 0;
    CacheUnlock();
}

#define NO_CELL 0xFFFFu

static const RowIndex *RowIndexOf(const VoxelMapInstance *inst, const BuildingPlacement *p,
                                  uint32_t count)
{
    int32_t layout = BuildingLayout(inst);
    RowIndex *r;
    uint32_t total = 0;
    int32_t rows = 0;
    uint32_t *start = NULL;
    uint16_t *placements = NULL;
    uint32_t *fill = NULL;

    CacheLock();
    for (uint32_t i = 0; i < ROW_INDEXES; ++i)
    {
        if (sRowIndexes[i].layout == layout + 1)
        {
            const RowIndex *found = &sRowIndexes[i];
            CacheUnlock();
            return found;
        }
    }
    CacheUnlock();

    if (count >= NO_CELL)
        return NULL;
    for (uint32_t i = 0; i < count; ++i)
    {
        int32_t h = sModels[sPageModels[p[i].pageModel].model].h;

        if ((int32_t)p[i].y + h > rows)
            rows = (int32_t)p[i].y + h;
        total += (uint32_t)h;
    }
    start = calloc((size_t)rows + 1u, sizeof(uint32_t));
    placements = malloc((total ? total : 1u) * sizeof(uint16_t));
    fill = rows > 0 ? malloc((size_t)rows * sizeof(uint32_t)) : NULL;
    if (start == NULL || placements == NULL || (rows > 0 && fill == NULL))
    {
        free(start);
        free(placements);
        free(fill);
        return NULL;
    }
    for (uint32_t i = 0; i < count; ++i)
        for (int32_t y = p[i].y; y < (int32_t)p[i].y + sModels[sPageModels[p[i].pageModel].model].h; ++y)
            ++start[y + 1];
    for (int32_t y = 0; y < rows; ++y)
        start[y + 1] += start[y];
    if (rows > 0)
        memcpy(fill, start, (size_t)rows * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; ++i)
        for (int32_t y = p[i].y; y < (int32_t)p[i].y + sModels[sPageModels[p[i].pageModel].model].h; ++y)
            placements[fill[y]++] = (uint16_t)i;
    free(fill);

    CacheLock();
    for (uint32_t i = 0; i < ROW_INDEXES; ++i)
    {
        if (sRowIndexes[i].layout == layout + 1)
        {
            const RowIndex *found = &sRowIndexes[i];
            CacheUnlock();
            free(start);
            free(placements);
            return found;
        }
    }
    r = &sRowIndexes[sRowIndexNext];
    sRowIndexNext = (sRowIndexNext + 1u) % ROW_INDEXES;
    FreeRowIndex(r);
    r->start = start;
    r->placements = placements;
    r->rows = rows;
    r->layout = layout + 1;
    CacheUnlock();
    return r;
}

void VoxelBuildings_PrepareWorld(void)
{
    const uint32_t instCount = VoxelWorld_InstanceCount();

    if (sModels == NULL || sPlacementCount == 0)
        return;
    for (uint32_t i = 0; i < instCount; ++i)
    {
        const VoxelMapInstance *const inst = VoxelWorld_Instance(i);
        uint32_t count = 0;
        const BuildingPlacement *const p = LayoutPlacements(inst, &count);
        if (p != NULL && count > 0)
            RowIndexOf(inst, p, count);
    }
}

static long ModelCell(const VoxelMapInstance *inst, int32_t x, int32_t y, uint32_t *placement)
{
    uint32_t count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);
    int32_t lx, ly;

    if (p == NULL)
        return -1;
    lx = x - inst->originX;
    ly = y - inst->originY;
    for (uint32_t i = 0; i < count; ++i)
    {
        const BuildingModel *m = &sModels[sPageModels[p[i].pageModel].model];
        int32_t cx = lx - p[i].x, cy = ly - p[i].y;
        uint32_t k;

        if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h)
            continue;
        k = m->heights + (uint32_t)cy * m->w + (uint32_t)cx;
        if (sHeights[k] == 0xFF || !PlacementLive(inst, &p[i]))
            continue;
        if (placement != NULL)
            *placement = i;
        return (long)k;
    }
    return -1;
}

bool VoxelBuildings_CellAt(const VoxelMapInstance *inst, int32_t x, int32_t y,
                           int32_t *groundMetatile, float *top)
{
    uint32_t i, count;
    long k = ModelCell(inst, x, y, &i);

    if (k < 0)
        return false;
    if (groundMetatile != NULL)
    {
        *groundMetatile = LayoutPlacements(inst, &count)[i].ground;
        if (*groundMetatile == (int32_t)OWN_GROUND || (sHeights[k] == 0 && sQuarters[k] != 0))
        {
            uint32_t own = (uint32_t)VoxelWorld_GetMetatileId(x, y);

            *groundMetatile = (int32_t)own;
            for (uint32_t v = 0; sQuarters[k] != 0 && v < sVariantCount && v < VOXEL_VARIANTS; ++v)
                if (U16(sVariants + 6u * v + 2) == own && sVariants[6u * v + 4] == sQuarters[k]
                 && (sVariantFits == NULL || sVariantFits(inst, U16(sVariants + 6u * v), own)))
                {
                    *groundMetatile = (int32_t)(VOXEL_METATILE_REAL + v);
                    break;
                }
        }
    }
    if (top != NULL)
        *top = sHeights[k] / 16.0f;
    return true;
}

bool VoxelBuildings_BareVariant(const VoxelMapInstance *inst, int32_t x, int32_t y, int32_t *metatile)
{
    uint32_t i;
    long k = ModelCell(inst, x, y, &i);
    uint32_t own;

    if (k < 0 || sHeights[k] != 0 || sQuarters[k] == 0)
        return false;
    own = (uint32_t)VoxelWorld_GetMetatileId(x, y);
    for (uint32_t v = 0; v < sVariantCount && v < VOXEL_VARIANTS; ++v)
        if (U16(sVariants + 6u * v + 2) == own && sVariants[6u * v + 4] == sQuarters[k]
         && (sVariantFits == NULL || sVariantFits(inst, U16(sVariants + 6u * v), own)))
        {
            *metatile = (int32_t)(VOXEL_METATILE_REAL + v);
            return true;
        }
    return false;
}

bool VoxelBuildings_DeckAt(const VoxelMapInstance *inst, float worldX, float worldZ,
                           uint32_t elevation, float *lift)
{
    int32_t x, y;
    uint32_t lo = 0, hi = sDeckCount, key;

    if (inst == NULL || sDeckCount == 0)
        return false;
    x = (int32_t)floorf(worldX) - inst->originX;
    y = (int32_t)floorf(worldZ) - inst->originY;
    if (x < 0 || y < 0 || x > 255 || y > 255)
        return false;
    key = ((uint32_t)BuildingLayout(inst) << 16) | ((uint32_t)y << 8) | (uint32_t)x;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *d = sDecks + 8u * mid;
        uint32_t k = (U16(d) << 16) | ((uint32_t)d[3] << 8) | d[2];

        if (k < key)
            lo = mid + 1;
        else if (k > key)
            hi = mid;
        else
        {
            float f = worldZ - floorf(worldZ);

            if (d[6] != 0 && d[6] != elevation)
                return false;
            *lift = (d[4] + (d[5] - (float)d[4]) * f) / 16.0f;
            return true;
        }
    }
    return false;
}

void VoxelBuildings_SetVariantFits(bool (*fits)(const VoxelMapInstance *inst, uint32_t layout,
                                                uint32_t metatile))
{
    sVariantFits = fits;
}

bool VoxelBuildings_Variant(uint32_t i, uint32_t *layout, uint32_t *metatile, uint32_t *quarters)
{
    if (i >= sVariantCount)
        return false;
    *layout = U16(sVariants + 6u * i);
    *metatile = U16(sVariants + 6u * i + 2);
    *quarters = sVariants[6u * i + 4];
    return true;
}

const uint16_t *VoxelBuildings_Footprint(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    long k = ModelCell(inst, x, y, NULL);

    if (k < 0 || sFootprints[k] == 0xFFFF)
        return NULL;
    return &sMasks[(uint32_t)sFootprints[k] * 16u];
}

static uint32_t CollectFronts(const VoxelMapInstance *inst, int32_t x, int32_t y, float height,
                              FrontFace *out, uint32_t max)
{
    uint32_t count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);
    const RowIndex *rows = p != NULL ? RowIndexOf(inst, p, count) : NULL;
    int32_t row = y - inst->originY;
    uint32_t n = count, first = 0;
    uint32_t found = 0;

    if (rows != NULL)
    {
        if (row < 0 || row >= rows->rows)
            return 0;
        first = rows->start[row];
        n = rows->start[row + 1] - first;
    }
    for (uint32_t t = 0; p != NULL && t < n; ++t)
    {
        uint32_t i = rows != NULL ? rows->placements[first + t] : t;
        const BuildingModel *m = &sModels[sPageModels[p[i].pageModel].model];
        const VoxelVertex *v = &sVertices[m->firstVertex];
        int32_t lx = x - inst->originX - p[i].x, ly = y - inst->originY - p[i].y;
        float left = (float)(inst->originX + p[i].x), north = (float)ly;

        if (lx < 0 || ly < 0 || lx >= m->w || ly >= m->h || !PlacementLive(inst, &p[i]))
            continue;
        for (uint32_t k = 0; k + 2 < m->vertexCount; k += 3)
        {
            const VoxelVertex *a = &v[k], *b = &v[k + 1], *c = &v[k + 2];
            float lo = a->x < b->x ? a->x : b->x, hi = a->x > b->x ? a->x : b->x;
            float y0 = a->y < b->y ? a->y : b->y, y1 = a->y > b->y ? a->y : b->y;

            if (c->x < lo) lo = c->x;
            if (c->x > hi) hi = c->x;
            if (c->y < y0) y0 = c->y;
            if (c->y > y1) y1 = c->y;
            if (a->z != b->z || a->z != c->z || a->z < north - 0.01f || a->z > north + 1.01f
             || y0 > height || y1 < height)
                continue;
            if ((b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x) <= 0.0f)
                continue;
            {
                FrontFace face = {lo + left, hi + left, (float)(inst->originY + p[i].y) + a->z};
                uint32_t j;

                for (j = 0; j < found && j < max; ++j)
                    if (out[j].z == face.z && face.lo <= out[j].hi && face.hi >= out[j].lo)
                    {
                        if (face.lo < out[j].lo) out[j].lo = face.lo;
                        if (face.hi > out[j].hi) out[j].hi = face.hi;
                        break;
                    }
                if (j < found && j < max)
                    continue;
                if (found < max)
                    out[found] = face;
                ++found;
            }
        }
    }
    return found;
}

void VoxelBuildings_MapChanged(void)
{
    memset(sFrontCells, 0, sizeof(sFrontCells));
}

static const FrontCell *FrontCellAt(const VoxelMapInstance *inst, int32_t x, int32_t y, float height)
{
    int32_t layout = BuildingLayout(inst);
    uint32_t hash = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u
                  ^ (uint32_t)layout * 83492791u;
    FrontCell *cell = &sFrontCells[(hash ^ (hash >> 16)) & (FRONT_SETS - 1u)];

    if (!cell->valid || cell->layout != layout || cell->originX != inst->originX
     || cell->originY != inst->originY || cell->x != x || cell->y != y || cell->height != height)
    {
        cell->valid = true;
        cell->layout = layout;
        cell->originX = inst->originX;
        cell->originY = inst->originY;
        cell->x = x;
        cell->y = y;
        cell->height = height;
        cell->count = CollectFronts(inst, x, y, height, cell->face, FRONT_FACES);
    }
    return cell;
}

bool VoxelBuildings_FrontAt(const VoxelMapInstance *inst, int32_t x, int32_t y,
                            float x0, float x1, float height, float *z)
{
    FrontFace many[64];
    const FrontCell *cell = FrontCellAt(inst, x, y, height);
    const FrontFace *face = cell->face;
    uint32_t count = cell->count;
    bool found = false;

    if (count > FRONT_FACES)
    {
        static bool logged;

        if (!logged)
            PORT_LOG("[VIDEO] VOXEL front faces: %u in cell %d,%d, read each time\n", count, x, y);
        logged = true;
        count = CollectFronts(inst, x, y, height, many, 64);
        if (count > 64)
            count = 64;
        face = many;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        if (face[i].lo >= x1 || face[i].hi <= x0)
            continue;
        if (!found || face[i].z > *z)
            *z = face[i].z;
        found = true;
    }
    return found;
}

bool VoxelBuildings_StaticModel(uint32_t pageModel, const VoxelVertex **vertices,
                                uint32_t *count, float *ox, float *oy, float *su, float *sv)
{
    const BuildingPageModel *pm;
    const BuildingModel *m;
    const BuildingPage *page;

    if (pageModel >= sPageModelCount)
        return false;
    pm = &sPageModels[pageModel];
    m = &sModels[pm->model];
    page = &sPages[pm->page];
    *vertices = &sVertices[m->firstVertex];
    *count = m->vertexCount;
    *ox = (float)pm->ox;
    *oy = (float)pm->oy;
    *su = 1.0f / page->w;
    *sv = 1.0f / page->texHeight;
    return true;
}

bool VoxelBuildings_DoorWall(const VoxelMapInstance *inst, int32_t x, int32_t y, float *z)
{
    return VoxelBuildings_FrontAt(inst, x, y, (float)x + 0.49f, (float)x + 0.51f, 0.5f, z);
}

bool VoxelBuildings_EmitSome(VoxelBuilder *builder, const VoxelMapInstance *inst,
                             int32_t x0, int32_t y0, int32_t x1, int32_t y1, VoxelBuildingCursor *cursor,
                             uint32_t triangles)
{
    uint32_t count;
    const BuildingPlacement *p = LayoutPlacements(inst, &count);

    for (; p != NULL && cursor->placement < count; ++cursor->placement, cursor->part = 0,
                                                     cursor->vertex = 0)
    {
        uint32_t i = cursor->placement;
        const BuildingPageModel *pm = &sPageModels[p[i].pageModel];
        const BuildingModel *m = &sModels[pm->model];
        const BuildingPage *page = &sPages[pm->page];
        float wx = (float)(inst->originX + p[i].x);
        float wz = (float)(inst->originY + p[i].y);
        float su = 1.0f / page->w, sv = 1.0f / page->texHeight;
        const uint32_t first[2] = { m->firstVertex, p[i].extraFirst };
        const uint32_t total[2] = { m->vertexCount, p[i].extraCount };
        const float ox[2] = { (float)pm->ox, 0.0f }, oy[2] = { (float)pm->oy, 0.0f };

        if (inst->originX + p[i].x < x0 || inst->originX + p[i].x >= x1
         || inst->originY + p[i].y < y0 || inst->originY + p[i].y >= y1
         || !PlacementLive(inst, &p[i]))
            continue;
        builder->lift = VoxelRelief_CellLift(inst, inst->originX + p[i].x,
                                             inst->originY + p[i].y + m->h - 1);
        builder->shift = VoxelRelief_CellShift(inst, inst->originX + p[i].x,
                                              inst->originY + p[i].y + m->h - 1);
        if (m->isStatic && builder->statics != NULL && cursor->part == 0
         && !(builder->stretchAdd > 0.0f && wz + builder->shift + (float)m->h > builder->stretchFrom))
        {
            if (cursor->vertex == 0 && builder->staticCount < builder->staticCapacity)
            {
                VoxelStaticDraw *s = &builder->statics[builder->staticCount++];

                s->pageModel = p[i].pageModel;
                s->lit = builder->lighting;
                s->x = wx - builder->originX;
                s->y = builder->lift + builder->base;
                s->z = VoxelBuilder_StretchZ(builder, wz + builder->shift) - builder->originZ;
                s->w = (float)m->w;
                s->d = (float)m->h;
                s->top = m->top;
            }
            else if (cursor->vertex == 0 && !sStaticsFullLogged)
            {
                sStaticsFullLogged = true;
                PORT_LOG("[VIDEO] VOXEL static models: a chunk holds more than %u\n",
                         builder->staticCapacity);
            }
            cursor->part = 1;
            cursor->vertex = 0;
        }
        for (; cursor->part < 2; ++cursor->part, cursor->vertex = 0)
        {
            uint32_t r = cursor->part;
            const VoxelVertex *v = &sVertices[first[r]];
            uint32_t step = r == 1 ? 6u : 3u;

            for (; cursor->vertex + step - 1 < total[r]; cursor->vertex += step)
            {
                uint32_t k = cursor->vertex;
                VoxelVertex t[6];

                if (triangles < step / 3u)
                {
                    builder->lift = 0.0f;
                    builder->shift = 0.0f;
                    return false;
                }
                triangles -= step / 3u;

                for (uint32_t j = 0; j < step; ++j)
                {
                    t[j] = v[k + j];
                    t[j].x += wx;
                    t[j].z += wz;
                    t[j].u = (ox[r] + t[j].u) * su;
                    t[j].v = 1.0f - (oy[r] + t[j].v) * sv;
                }
                if (step == 6)
                    VoxelBuilder_Quad(builder, &t[0], &t[1], &t[2], &t[5]);
#if CTR_VOXEL_LIGHTING
                else if (builder->lighting)
                {
                    VoxelLighting_ModelTri(builder, &t[0], &t[1], &t[2], t[0].shade);
                }
#endif
                else
                    VoxelBuilder_Tri(builder, &t[0], &t[1], &t[2]);
            }
        }
        builder->lift = 0.0f;
        builder->shift = 0.0f;
    }
    return true;
}

void VoxelBuildings_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                                 int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    VoxelBuildingCursor cursor = { 0, 0, 0 };

    VoxelBuildings_EmitSome(builder, inst, x0, y0, x1, y1, &cursor, UINT32_MAX);
}
