/*
 * Reader for voxel/relief.bin (`VXL4`).
 */
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef PORT_LOG
#include "port_log.h"
#endif

#include "voxel_relief.h"
#include "voxel_file.h"

#ifndef VOXEL_RELIEF_PATH
#define VOXEL_RELIEF_PATH "voxel/relief.bin"
#endif
#define GRID (VOXEL_RELIEF_SIDE * VOXEL_RELIEF_SIDE)
#define CELL_BYTES (2 + GRID)
#define WIDE_CELL_BYTES (2 + 2 * GRID)
#define ROW_BYTES 14

typedef struct
{
    uint16_t layoutId, width, height, count;
    int16_t base;
    bool drawn;
    uint8_t belt;
    uint8_t ease;
    uint8_t easeRows;
    int32_t faceTile;
    const uint8_t *cells;
    uint8_t stride;
    int16_t *heights;
    int8_t *surface;
    uint8_t surfaceUnit;
    uint16_t surfaceW, surfaceH;
    float top;
} ReliefLayout;

#define SURFACE_SPAN 6
#define SURFACE_WALL 8

static int32_t Find(const ReliefLayout *l, const VoxelMapInstance *inst, int32_t x, int32_t y);

static uint32_t EaseRows(const ReliefLayout *l)
{
    uint32_t rows = (6u * 8u * l->ease + 15u) / 16u;

    return rows < l->height / 2u ? rows : l->height / 2u;
}
static bool BuildSurface(ReliefLayout *l);

static uint8_t *sBlob;
static ReliefLayout *sLayouts;
static uint32_t sLayoutCount;
static uint16_t *sLayoutIndex;
static uint32_t sLayoutIndexSize;
static const uint8_t *sCutVariants, *sCutCells;
static uint32_t sCutVariantCount, sCutCellCount;
static const uint8_t *sEdges;
static uint32_t sEdgeCount;
#define EDGE_BYTES 18
#define CUT_VARIANT_BYTES 36
#define CUT_CELL_BYTES 14

static inline int32_t FloorI(float v)
{
    int32_t i = (int32_t)v;

    return (float)i > v ? i - 1 : i;
}

static uint32_t U16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t U32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void ForgetSeams(void);

bool VoxelRelief_Init(void)
{
    FILE *file;
    long size;

    VoxelRelief_Shutdown();
    file = VoxelFile_Open(VOXEL_RELIEF_PATH);
    if (file == NULL)
    {
        PORT_LOG("[VIDEO] VOXEL relief: %s absent, terrain stays level\n", VOXEL_RELIEF_PATH);
        return false;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    sBlob = size > 8 ? malloc((size_t)size) : NULL;
    if (sBlob == NULL || fread(sBlob, 1, (size_t)size, file) != (size_t)size
     || memcmp(sBlob, "VXL4", 4) != 0 || U16(sBlob + 6) != VOXEL_RELIEF_SIDE
     || memcmp(sBlob + size - 4, "CUTS", 4) != 0)
        goto fail;
    {
        uint32_t at = U32(sBlob + size - 8);

        if ((long)at + 4 > size - 8)
            goto fail;
        sCutVariantCount = U16(sBlob + at);
        sCutCellCount = U16(sBlob + at + 2);
        sCutVariants = sBlob + at + 4;
        sCutCells = sCutVariants + CUT_VARIANT_BYTES * sCutVariantCount;
        if ((long)(sCutCells - sBlob) + (long)(CUT_CELL_BYTES * sCutCellCount) > size - 8)
            goto fail;
        {
            const uint8_t *e = sCutCells + CUT_CELL_BYTES * sCutCellCount;

            if ((long)(e - sBlob) + 8 <= size - 8 && memcmp(e, "EDGE", 4) == 0)
            {
                sEdgeCount = U16(e + 4);
                sEdges = e + 8;
                if ((long)(sEdges - sBlob) + (long)(EDGE_BYTES * sEdgeCount) > size - 8)
                    goto fail;
            }
        }
    }
    sLayoutCount = U16(sBlob + 4);
    if (8 + (unsigned long)ROW_BYTES * sLayoutCount > (unsigned long)size)
        goto fail;
    sLayouts = calloc(sLayoutCount ? sLayoutCount : 1, sizeof(*sLayouts));
    if (sLayouts == NULL)
        goto fail;
    for (uint32_t i = 0; i < sLayoutCount; ++i)
    {
        const uint8_t *row = sBlob + 8 + ROW_BYTES * i;
        ReliefLayout *l = &sLayouts[i];
        uint32_t cells = U16(row + 2);
        uint32_t offset = U32(row + 8);

        l->layoutId = (uint16_t)U16(row);
        l->base = (int16_t)U16(row + 12);
        l->width = (uint16_t)(U16(row + 4) & 0x03FF);
        uint32_t unit = (U16(row + 6) & 0x4000) ? 2 : 1;
        bool wide = (U16(row + 4) & 0x4000) != 0;

        l->height = (uint16_t)(U16(row + 6) & 0x03FF);
        l->drawn = ((U16(row + 6) & 0x8000) != 0) || (cells > 0u);
        l->belt = (uint8_t)((U16(row + 6) >> 10) & 0x0F);
        l->ease = (uint8_t)((U16(row + 4) >> 10) & 0x0F);
        l->easeRows = EaseRows(l);
        l->cells = sBlob + offset;
        l->count = (uint16_t)cells;
        l->stride = wide ? WIDE_CELL_BYTES : CELL_BYTES;
        if (offset > (uint32_t)size || (uint32_t)cells * l->stride > (uint32_t)size - offset)
            goto fail;
        for (uint32_t k = 1; k < cells; ++k)
        {
            const uint8_t *a = l->cells + (k - 1) * l->stride, *c = a + l->stride;
            if (a[1] > c[1] || (a[1] == c[1] && a[0] >= c[0]))
                goto fail;
        }
        l->heights = malloc((size_t)(cells ? cells : 1) * GRID * sizeof(int16_t));
        if (l->heights == NULL)
            goto fail;
        for (uint32_t k = 0; k < cells; ++k)
            for (uint32_t g = 0; g < GRID; ++g)
                l->heights[k * GRID + g] = wide
                    ? (int16_t)U16(l->cells + k * WIDE_CELL_BYTES + 2 + 2 * g)
                    : (int16_t)((int8_t)l->cells[k * CELL_BYTES + 2 + g] * (int32_t)unit);
    }
    fclose(file);
    for (uint32_t i = 0; i < sLayoutCount; ++i)
        sLayouts[i].faceTile = -1;
    for (uint32_t k = 0; k < sCutCellCount;)
    {
        uint32_t layout = U16(sCutCells + CUT_CELL_BYTES * k), end = k;
        uint32_t walls[8] = {0}, counts[8] = {0}, kinds = 0, best = 0;

        while (end < sCutCellCount && U16(sCutCells + CUT_CELL_BYTES * end) == layout)
        {
            uint32_t wall = U16(sCutCells + CUT_CELL_BYTES * end + 10), j = 0;

            ++end;
            if (wall >= 0xFFFEu)
                continue;
            while (j < kinds && walls[j] != wall)
                ++j;
            if (j == kinds && kinds < 8)
                walls[kinds++] = wall;
            if (j < kinds)
                ++counts[j];
        }
        for (uint32_t j = 1; j < kinds; ++j)
            if (counts[j] > counts[best])
                best = j;
        for (uint32_t i = 0; kinds > 0 && i < sLayoutCount; ++i)
            if (sLayouts[i].layoutId == layout)
                sLayouts[i].faceTile = (int32_t)walls[best];
        k = end;
    }
    for (uint32_t i = 0; i < sLayoutCount; ++i)
        if (sLayouts[i].drawn && !BuildSurface(&sLayouts[i]))
        {
            PORT_LOG("[ERROR] VOXEL relief: no memory for a drawn map's surface\n");
            VoxelRelief_Shutdown();
            return false;
        }
    {
        uint32_t most = 0;

        for (uint32_t i = 0; i < sLayoutCount; ++i)
            if (sLayouts[i].layoutId > most)
                most = sLayouts[i].layoutId;
        sLayoutIndex = calloc((size_t)most + 1u, sizeof(*sLayoutIndex));
        if (sLayoutIndex != NULL)
        {
            sLayoutIndexSize = most + 1u;
            for (uint32_t i = sLayoutCount; i-- > 0;)
                sLayoutIndex[sLayouts[i].layoutId] = (uint16_t)(i + 1u);
        }
    }
    PORT_LOG("[VIDEO] VOXEL relief: %u layouts\n", sLayoutCount);
    return true;

fail:
    fclose(file);
    PORT_LOG("[ERROR] VOXEL relief: %s is truncated or malformed\n", VOXEL_RELIEF_PATH);
    VoxelRelief_Shutdown();
    return false;
}

void VoxelRelief_Shutdown(void)
{
    for (uint32_t i = 0; sLayouts != NULL && i < sLayoutCount; ++i)
    {
        free(sLayouts[i].surface);
        free(sLayouts[i].heights);
    }
    ForgetSeams();
    free(sLayouts);
    free(sLayoutIndex);
    sLayoutIndex = NULL;
    sLayoutIndexSize = 0;
    free(sBlob);
    sLayouts = NULL;
    sBlob = NULL;
    sLayoutCount = 0;
    sCutVariants = sCutCells = NULL;
    sCutVariantCount = sCutCellCount = 0;
    sEdges = NULL;
    sEdgeCount = 0;
}

bool VoxelRelief_EdgeFill(uint32_t layoutId, uint32_t edge, int32_t along, uint32_t index, VoxelEdgeFill *fill)
{
    for (uint32_t i = 0; i < sEdgeCount; ++i)
    {
        const uint8_t *e = sEdges + EDGE_BYTES * i;
        int32_t first = (int16_t)U16(e + 4), last = (int16_t)U16(e + 6);

        if (U16(e) != layoutId || e[2] != edge)
            continue;
        if (first >= 0 && (along < first || along > last))
            continue;
        index &= 3u;
        fill->metatile = (int32_t)U16(e + 10 + 2 * index);
        fill->water = (e[3] >> (4u + index) & 1u) != 0;
        fill->hasTop = (e[3] & 2) != 0;
        fill->top = (int16_t)U16(e + 8);
        return true;
    }
    return false;
}

bool VoxelRelief_EdgeFillAt(const VoxelMapInstance *ring, int32_t worldX, int32_t worldY, VoxelEdgeFill *fill)
{
    int32_t lx, ly, cx, cy;
    uint32_t index;

    if (sEdgeCount == 0 || ring == NULL || ring->indoor || ring->width <= 0 || ring->height <= 0)
        return false;
    lx = worldX - ring->originX;
    ly = worldY - ring->originY;
    cx = lx < 0 ? 0 : lx >= ring->width ? ring->width - 1 : lx;
    cy = ly < 0 ? 0 : ly >= ring->height ? ring->height - 1 : ly;
    index = (uint32_t)(lx & 1) | ((uint32_t)(ly & 1) << 1);
    if (lx < 0 && VoxelRelief_EdgeFill((uint32_t)ring->layoutId, 1, cy, index, fill))
        return true;
    if (lx >= ring->width && VoxelRelief_EdgeFill((uint32_t)ring->layoutId, 2, cy, index, fill))
        return true;
    if (ly < 0 && VoxelRelief_EdgeFill((uint32_t)ring->layoutId, 4, cx, index, fill))
        return true;
    if (ly >= ring->height && VoxelRelief_EdgeFill((uint32_t)ring->layoutId, 8, cx, index, fill))
        return true;
    return false;
}

bool VoxelRelief_EdgeFills(uint32_t layoutId, uint32_t i, int32_t *metatile)
{
    for (uint32_t k = 0; k < sEdgeCount; ++k)
    {
        const uint8_t *e = sEdges + EDGE_BYTES * k;

        if (U16(e) != layoutId)
            continue;
        if (i < 4u)
        {
            *metatile = (int32_t)U16(e + 10 + 2 * i);
            return true;
        }
        i -= 4u;
    }
    return false;
}

uint32_t VoxelRelief_CutCount(void)
{
    return sCutVariantCount;
}

bool VoxelRelief_CutVariant(uint32_t i, uint32_t *layout, uint32_t *metatile,
                            const uint8_t **rows)
{
    const uint8_t *v;

    if (i >= sCutVariantCount)
        return false;
    v = sCutVariants + CUT_VARIANT_BYTES * i;
    *layout = U16(v);
    *metatile = U16(v + 2);
    *rows = v + 4;
    return true;
}

static const int8_t kCutDir[8][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 },
                                      { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };

static bool CutHull(const uint8_t *rows, int32_t c[8])
{
    int32_t rock = 0;

    for (int32_t k = 0; k < 8; ++k)
        c[k] = INT16_MIN;
    for (int32_t j = 0; j < 16; ++j)
    {
        uint32_t bits = (uint32_t)rows[2 * j] | ((uint32_t)rows[2 * j + 1] << 8);

        for (int32_t i = 0; i < 16; ++i)
            if (!((bits >> i) & 1u))
            {
                ++rock;
                for (int32_t k = 0; k < 8; ++k)
                    if (kCutDir[k][0] * i + kCutDir[k][1] * j > c[k])
                        c[k] = kCutDir[k][0] * i + kCutDir[k][1] * j;
            }
    }
    if (rock == 0 || rock == 256)
        return false;
    for (int32_t j = 0; j < 16; ++j)
    {
        uint32_t bits = (uint32_t)rows[2 * j] | ((uint32_t)rows[2 * j + 1] << 8);

        for (int32_t i = 0; i < 16; ++i)
        {
            bool in = true;

            for (int32_t k = 0; k < 8 && in; ++k)
                in = kCutDir[k][0] * i + kCutDir[k][1] * j <= c[k];
            if (in == (((bits >> i) & 1u) != 0))
                return false;
        }
    }
    return true;
}

void VoxelRelief_CutDiagonals(const uint8_t *rows, VoxelCutDiagonals *out)
{
    int32_t c[8];

    out->count = 0;
    if (rows == NULL || !CutHull(rows, c))
        return;
    for (int32_t k = 4; k < 8; ++k)
    {
        int32_t a = kCutDir[k][0], b = kCutDir[k][1];
        int32_t corner = a * (a > 0 ? c[3] : -c[2]) + b * (b > 0 ? c[1] : -c[0]);

        if (c[k] >= corner)
            continue;
        out->a[out->count] = (int8_t)a;
        out->b[out->count] = (int8_t)b;
        out->c[out->count] = (int16_t)(c[k] + (a > 0) + (b > 0));
        ++out->count;
    }
}

static bool CutTakes(const int32_t c[8], int32_t i, int32_t j)
{
    for (int32_t k = 0; k < 8; ++k)
    {
        int32_t a = kCutDir[k][0], b = kCutDir[k][1];
        int32_t least = a * (a > 0 ? i : i + 1) + b * (b > 0 ? j : j + 1);

        if (least >= c[k] + (a > 0) + (b > 0))
            return false;
    }
    return true;
}

void VoxelRelief_CutClear(const uint8_t *rows, uint16_t clear[16])
{
    VoxelCutDiagonals d;
    int32_t c[8];

    for (int32_t j = 0; j < 16; ++j)
        clear[j] = (uint16_t)((uint32_t)rows[2 * j] | ((uint32_t)rows[2 * j + 1] << 8));
    VoxelRelief_CutDiagonals(rows, &d);
    if (d.count == 0)
        return;
    CutHull(rows, c);
    for (int32_t k = 0; k < 4; ++k)
    {
        int32_t gained = 0;

        if (c[k] >= (kCutDir[k][0] + kCutDir[k][1] > 0 ? 15 : 0))
            continue;
        ++c[k];
        for (int32_t j = 0; j < 16 && gained <= 1; ++j)
            for (int32_t i = 0; i < 16; ++i)
                if (((clear[j] >> i) & 1u) && kCutDir[k][0] * i + kCutDir[k][1] * j == c[k]
                 && CutTakes(c, i, j))
                    ++gained;
        if (gained != 1)
            --c[k];
    }
    for (int32_t j = 0; j < 16; ++j)
        for (int32_t i = 0; i < 16; ++i)
        {
            if (((clear[j] >> i) & 1u) && CutTakes(c, i, j))
                clear[j] &= (uint16_t)~(1u << i);
        }
}

int32_t VoxelRelief_CutFringe(const uint8_t *rows, const uint16_t clear[16], int32_t x, int32_t y)
{
    static const int8_t kAround[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1},
                                         {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    uint32_t row = (uint32_t)rows[2 * y] | ((uint32_t)rows[2 * y + 1] << 8);

    if (!((row >> x) & 1u) || ((clear[y] >> x) & 1u))
        return -1;
    for (int32_t k = 0; k < 8; ++k)
    {
        int32_t nx = x + kAround[k][0], ny = y + kAround[k][1];
        uint32_t nrow;

        if (nx < 0 || nx > 15 || ny < 0 || ny > 15)
            continue;
        nrow = (uint32_t)rows[2 * ny] | ((uint32_t)rows[2 * ny + 1] << 8);
        if (!((nrow >> nx) & 1u))
            return ny * 16 + nx;
    }
    return -1;
}

int32_t VoxelRelief_Cut(const VoxelMapInstance *inst, int32_t x, int32_t y, float *foot, int32_t *ground,
                        int32_t *wall, uint32_t *sides)
{
    uint32_t lo = 0, hi = sCutCellCount, key;
    const uint8_t *c;
    int32_t record = -1;

    if (inst == NULL || sCutCells == NULL)
        return -1;
    x -= inst->originX;
    y -= inst->originY;
    if (x < 0 || y < 0 || x > 255 || y > 255)
        return -1;
    key = ((uint32_t)inst->layoutId << 16) | ((uint32_t)x << 8) | (uint32_t)y;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;
        uint32_t at;

        c = sCutCells + CUT_CELL_BYTES * mid;
        at = (U16(c) << 16) | ((uint32_t)c[2] << 8) | c[3];
        if (at == key)
        {
            record = (int32_t)mid;
            break;
        }
        if (at < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (record < 0)
        return -1;
    c = sCutCells + CUT_CELL_BYTES * (uint32_t)record;
    *foot = (int16_t)U16(c + 6) / 16.0f;
    *ground = U16(c + 8) == 0xFFFFu ? -1 : (int32_t)U16(c + 8);
    *wall = U16(c + 10) == 0xFFFFu ? -1 : (int32_t)U16(c + 10);
    *sides = (uint32_t)c[12] | ((uint32_t)c[13] << 8);
    return (int32_t)U16(c + 4);
}

void VoxelRelief_ForEachCut(const VoxelMapInstance *inst, VoxelReliefCutVisit visit, void *ctx)
{
    uint32_t lo = 0, hi = sCutCellCount, first;

    if (inst == NULL || sCutCells == NULL)
        return;
    first = (uint32_t)inst->layoutId << 16;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *c = sCutCells + CUT_CELL_BYTES * mid;

        if (((uint32_t)U16(c) << 16) < first)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < sCutCellCount; ++lo)
    {
        const uint8_t *c = sCutCells + CUT_CELL_BYTES * lo;

        if (U16(c) != (uint32_t)inst->layoutId)
            break;
        visit((int32_t)c[2], (int32_t)c[3],
              U16(c + 8) == 0xFFFFu ? -1 : (int32_t)U16(c + 8),
              U16(c + 10) == 0xFFFFu ? -1 : (int32_t)U16(c + 10), ctx);
    }
}

static const ReliefLayout *LayoutOf(const VoxelMapInstance *inst)
{
    if (inst == NULL)
        return NULL;
    if (sLayoutIndex != NULL)
    {
        uint32_t k = (uint32_t)inst->layoutId < sLayoutIndexSize ? sLayoutIndex[inst->layoutId] : 0u;

        return k != 0 ? &sLayouts[k - 1u] : NULL;
    }
    for (uint32_t i = 0; i < sLayoutCount; ++i)
        if (sLayouts[i].layoutId == (uint32_t)inst->layoutId)
            return &sLayouts[i];
    return NULL;
}

float VoxelRelief_Base(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);

    if (inst == NULL)
        return 0.0f;
    return ((l != NULL ? l->base : 0) + inst->lift) / 16.0f;
}

#define SEAM_CELLS_MAX 256
#define SEAM_MEMO 256u

static struct
{
    bool used;
    uint16_t layoutA, layoutB;
    int16_t dx, dy, widthA, heightA, widthB, heightB;
    int32_t step;
} sSeamMemo[SEAM_MEMO];

static int32_t SeamStepOf(const VoxelMapInstance *a, const VoxelMapInstance *b);

static void ForgetSeams(void)
{
    memset(sSeamMemo, 0, sizeof(sSeamMemo));
}

int32_t VoxelRelief_SeamStep(const VoxelMapInstance *a, const VoxelMapInstance *b)
{
    int32_t dx, dy;
    uint32_t key;

    if (a == NULL || b == NULL)
        return 0;
    dx = b->originX - a->originX;
    dy = b->originY - a->originY;
    key = ((uint32_t)a->layoutId * 73856093u ^ (uint32_t)b->layoutId * 19349663u
         ^ (uint32_t)dx * 83492791u ^ (uint32_t)dy * 2654435761u) % SEAM_MEMO;
    if (!sSeamMemo[key].used || sSeamMemo[key].layoutA != a->layoutId
     || sSeamMemo[key].layoutB != b->layoutId || sSeamMemo[key].dx != dx || sSeamMemo[key].dy != dy
     || sSeamMemo[key].widthA != a->width || sSeamMemo[key].heightA != a->height
     || sSeamMemo[key].widthB != b->width || sSeamMemo[key].heightB != b->height)
    {
        sSeamMemo[key].used = true;
        sSeamMemo[key].layoutA = (uint16_t)a->layoutId;
        sSeamMemo[key].layoutB = (uint16_t)b->layoutId;
        sSeamMemo[key].dx = (int16_t)dx;
        sSeamMemo[key].dy = (int16_t)dy;
        sSeamMemo[key].widthA = (int16_t)a->width;
        sSeamMemo[key].heightA = (int16_t)a->height;
        sSeamMemo[key].widthB = (int16_t)b->width;
        sSeamMemo[key].heightB = (int16_t)b->height;
        sSeamMemo[key].step = SeamStepOf(a, b);
    }
    return sSeamMemo[key].step;
}

static int32_t SeamStepOf(const VoxelMapInstance *a, const VoxelMapInstance *b)
{
    const ReliefLayout *la = LayoutOf(a), *lb = LayoutOf(b);
    const uint32_t last = VOXEL_RELIEF_SIDE - 1;
    int32_t diff[SEAM_CELLS_MAX];
    int32_t from, to, ax, ay, bx, by, dx, dy;
    uint32_t ai, aj, bi, bj, di, dj, count = 0, best = 0, n = 0;
    int32_t step = 0;

    if (a == NULL || b == NULL || (la == NULL && lb == NULL))
        return 0;
    if (a->originX + a->width == b->originX || b->originX + b->width == a->originX)
    {
        bool east = a->originX + a->width == b->originX;

        from = a->originY > b->originY ? a->originY : b->originY;
        to = a->originY + a->height < b->originY + b->height ? a->originY + a->height
                                                             : b->originY + b->height;
        ax = east ? a->originX + a->width - 1 : a->originX;
        bx = east ? b->originX : b->originX + b->width - 1;
        ay = by = from;
        dx = 0, dy = 1;
        ai = east ? last : 0, bi = east ? 0 : last, aj = bj = 0, di = 0, dj = 1;
    }
    else if (a->originY + a->height == b->originY || b->originY + b->height == a->originY)
    {
        bool south = a->originY + a->height == b->originY;

        from = a->originX > b->originX ? a->originX : b->originX;
        to = a->originX + a->width < b->originX + b->width ? a->originX + a->width
                                                           : b->originX + b->width;
        ay = south ? a->originY + a->height - 1 : a->originY;
        by = south ? b->originY : b->originY + b->height - 1;
        ax = bx = from;
        dx = 1, dy = 0;
        aj = south ? last : 0, bj = south ? 0 : last, ai = bi = 0, di = 1, dj = 0;
    }
    else
        return 0;
    for (int32_t c = from; c < to && n < SEAM_CELLS_MAX; ++c, ++n)
    {
        const int16_t *ga = VoxelRelief_Cell(a, ax + dx * (c - from), ay + dy * (c - from));
        const int16_t *gb = VoxelRelief_Cell(b, bx + dx * (c - from), by + dy * (c - from));
        int32_t sum = 0;

        for (uint32_t p = 0; p < VOXEL_RELIEF_SIDE; ++p)
        {
            uint32_t ka = (aj + dj * p) * VOXEL_RELIEF_SIDE + ai + di * p;
            uint32_t kb = (bj + dj * p) * VOXEL_RELIEF_SIDE + bi + di * p;

            sum += (la != NULL ? la->base : 0) + (ga != NULL ? ga[ka] : 0)
                 - (lb != NULL ? lb->base : 0) - (gb != NULL ? gb[kb] : 0);
        }
        diff[n] = (int32_t)floorf(sum / (4.0f * VOXEL_RELIEF_SIDE) + 0.5f) * 4;
    }
    for (uint32_t i = 0; i < n; ++i)
    {
        count = 0;
        for (uint32_t j = 0; j < n; ++j)
            count += diff[j] == diff[i];
        if (count > best)
        {
            best = count;
            step = diff[i];
        }
    }
    return n != 0 && 4u * best >= 3u * n ? step : 0;
}

const int16_t *VoxelRelief_Cell(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    const ReliefLayout *l = LayoutOf(inst);
    int32_t k = Find(l, inst, x, y);
    return k >= 0 ? l->heights + (uint32_t)k * GRID : NULL;
}

const int16_t *VoxelRelief_Depth(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    return VoxelRelief_Cell(inst, x, y);
}

bool VoxelRelief_Stretch(const VoxelMapInstance *inst, float *from, float *to, float *add)
{
    const ReliefLayout *l = LayoutOf(inst);
    float a;

    if (l == NULL || l->ease == 0 || l->easeRows == 0)
        return false;
    a = 8.0f * l->ease / 16.0f;
    if (to != NULL)
        *to = (float)(inst->originY + l->height) - a;
    if (from != NULL)
        *from = (float)(inst->originY + l->height) - a - (float)l->easeRows;
    if (add != NULL)
        *add = a;
    return true;
}

float VoxelRelief_StretchZ(const VoxelMapInstance *inst, float z)
{
    float from, to, add;

    if (!VoxelRelief_Stretch(inst, &from, &to, &add) || z <= from)
        return z;
    if (z >= to)
        return z + add;
    return from + (z - from) * (to - from + add) / (to - from);
}

static int32_t Find(const ReliefLayout *l, const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    uint32_t lo = 0, hi, key;

    if (l == NULL)
        return -1;
    x -= inst->originX;
    y -= inst->originY;
    if (x < 0 || y < 0 || x >= l->width || y >= l->height)
        return -1;
    key = ((uint32_t)y << 8) | (uint32_t)x;
    hi = l->count;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *c = l->cells + mid * l->stride;
        uint32_t at = ((uint32_t)c[1] << 8) | c[0];

        if (at == key)
            return (int32_t)mid;
        if (at < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

bool VoxelRelief_IsDrawn(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL && l->drawn;
}

int32_t VoxelRelief_FaceTile(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL ? l->faceTile : -1;
}

uint32_t VoxelRelief_Belt(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL ? l->belt : 0u;
}

bool VoxelRelief_IsSlope(const int16_t *grid)
{
    for (uint32_t i = 1; grid != NULL && i < VOXEL_RELIEF_SIDE * VOXEL_RELIEF_SIDE; ++i)
        if (grid[i] != grid[0])
            return true;
    return false;
}

float VoxelRelief_CellLift(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    const int16_t *g = VoxelRelief_Cell(inst, x, y);
    return g ? g[2 * VOXEL_RELIEF_SIDE + 2] / 16.0f : 0.0f;
}

float VoxelRelief_CellShift(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    const int16_t *g = VoxelRelief_Depth(inst, x, y);
    return g ? g[2 * VOXEL_RELIEF_SIDE + 2] / 16.0f : 0.0f;
}

static float Sample(const int16_t *g, float worldX, float worldZ);

float VoxelRelief_LiftAt(float worldX, float worldZ)
{
    int32_t x = FloorI(worldX), y = FloorI(worldZ);
    const VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(x, y);

    return VoxelRelief_Base(inst) + Sample(VoxelRelief_Cell(inst, x, y), worldX, worldZ);
}

float VoxelRelief_ShiftAt(float worldX, float worldZ)
{
    int32_t x = FloorI(worldX), y = FloorI(worldZ);
    const VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(x, y);
    float shift = Sample(VoxelRelief_Depth(inst, x, y), worldX, worldZ);

    return VoxelRelief_StretchZ(inst, worldZ + shift) + VoxelRelief_Base(inst) - worldZ;
}

static float Sample(const int16_t *g, float worldX, float worldZ)
{
    int32_t x = FloorI(worldX), y = FloorI(worldZ);
    float fx, fy, a, b;
    int32_t i, j;

    if (g == NULL)
        return 0.0f;
    fx = (worldX - x) * (VOXEL_RELIEF_SIDE - 1);
    fy = (worldZ - y) * (VOXEL_RELIEF_SIDE - 1);
    i = (int32_t)fx;
    j = (int32_t)fy;
    if (i > VOXEL_RELIEF_SIDE - 2) i = VOXEL_RELIEF_SIDE - 2;
    if (j > VOXEL_RELIEF_SIDE - 2) j = VOXEL_RELIEF_SIDE - 2;
    fx -= i;
    fy -= j;
    a = g[j * VOXEL_RELIEF_SIDE + i] * (1 - fx) + g[j * VOXEL_RELIEF_SIDE + i + 1] * fx;
    b = g[(j + 1) * VOXEL_RELIEF_SIDE + i] * (1 - fx) + g[(j + 1) * VOXEL_RELIEF_SIDE + i + 1] * fx;
    return (a * (1 - fy) + b * fy) / 16.0f;
}

static void Point(const ReliefLayout *l, uint32_t i, uint32_t j, int32_t *h, int32_t *d)
{
    const uint32_t n = VOXEL_RELIEF_SIDE - 1;
    uint32_t x = i / n, y = j / n, a, b;
    int32_t k;
    VoxelMapInstance inst;

    if (x >= l->width) x = l->width - 1u;
    if (y >= l->height) y = l->height - 1u;
    a = i - x * n;
    b = j - y * n;
    memset(&inst, 0, sizeof(inst));
    inst.layoutId = l->layoutId;
    k = Find(l, &inst, (int32_t)x, (int32_t)y);
    *h = k >= 0 ? l->heights[(uint32_t)k * GRID + b * VOXEL_RELIEF_SIDE + a] : 0;
    *d = l->base + *h;
}

static bool BuildSurface(ReliefLayout *l)
{
    const uint32_t n = VOXEL_RELIEF_SIDE - 1, step = 16 / n;
    uint32_t w = l->width * n + 1u, rows, span = SURFACE_SPAN;
    int32_t top = 0, most = 0, deepest = l->base;

    for (uint32_t g = 0; g < (uint32_t)l->count * GRID; ++g)
    {
        if (abs(l->heights[g]) > most)
            most = abs(l->heights[g]);
        if (l->base + l->heights[g] > deepest)
            deepest = l->base + l->heights[g];
    }
    if (deepest > 0 && (uint32_t)(deepest + 15) / 16u + 1u > span)
        span = (uint32_t)(deepest + 15) / 16u + 1u;
    l->surfaceUnit = most > 254 ? 4 : 2;
    rows = (l->height + span) * n + 1u;
    l->surface = calloc((size_t)w * rows, 1);
    if (l->surface == NULL)
        return false;
    l->surfaceW = (uint16_t)w;
    l->surfaceH = (uint16_t)rows;
    for (uint32_t i = 0; i < w; ++i)
    {
        int32_t h0, d0;
        uint32_t k = 0;

        Point(l, i, 0, &h0, &d0);
        for (uint32_t j = 1; j <= l->height * n; ++j)
        {
            int32_t h1, d1;
            float z0 = (float)((j - 1) * step) + d0, z1;

            Point(l, i, j, &h1, &d1);
            z1 = (float)(j * step) + d1;
            for (; k < rows && (float)(k * step) <= z1; ++k)
            {
                float t = z1 > z0 ? ((float)(k * step) - z0) / (z1 - z0) : 1.0f;
                int32_t v;

                if (t < 0.0f) t = 0.0f;
                if (h1 - h0 >= SURFACE_WALL)
                    t = (float)((k + 1) * step) > z1 ? 1.0f : 0.0f;
                v = (int32_t)(h0 + (h1 - h0) * t + 0.5f);
                {
                    int32_t u = l->surfaceUnit;
                    l->surface[k * w + i] = (int8_t)(v >= 0 ? (v + u / 2) / u : -((u / 2 - v) / u));
                }
                if (v > top) top = v;
            }
            h0 = h1;
            d0 = d1;
        }
    }
    l->top = top / 16.0f;
    return true;
}

static float SurfacePoint(const ReliefLayout *l, int32_t i, int32_t k)
{
    if (i < 0 || k < 0 || i >= l->surfaceW || k >= l->surfaceH)
        return 0.0f;
    return (float)l->surfaceUnit * l->surface[k * l->surfaceW + i];
}

float VoxelRelief_SurfaceAt(float worldX, float worldZ)
{
    const int32_t n = VOXEL_RELIEF_SIDE - 1;
    int32_t x = FloorI(worldX), z = FloorI(worldZ);
    const VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(x, z);
    const ReliefLayout *l = LayoutOf(inst);
    float fx, fz;
    int32_t i, k;

    if (l == NULL || l->surface == NULL)
        return 0.0f;
    fx = (worldX - inst->originX) * n;
    fz = (worldZ - inst->originY) * n - inst->lift / 4.0f;
    i = FloorI(fx);
    k = FloorI(fz);
    fx -= i;
    fz -= k;
    return ((SurfacePoint(l, i, k) * (1 - fx) + SurfacePoint(l, i + 1, k) * fx) * (1 - fz)
          + (SurfacePoint(l, i, k + 1) * (1 - fx) + SurfacePoint(l, i + 1, k + 1) * fx) * fz)
           / 16.0f;
}

float VoxelRelief_SurfaceTop(const VoxelMapInstance *inst, int32_t x, int32_t z, float *bottom)
{
    const ReliefLayout *l = LayoutOf(inst);
    const int32_t n = VOXEL_RELIEF_SIDE - 1;
    float top = 0.0f, low = 1000.0f;

    *bottom = 0.0f;
    if (l == NULL || l->surface == NULL)
        return 0.0f;
    x -= inst->originX;
    z -= inst->originY;
    for (int32_t k = z * n - inst->lift / 4; k <= z * n + n - inst->lift / 4; ++k)
        for (int32_t i = x * n; i <= x * n + n; ++i)
        {
            float v = SurfacePoint(l, i, k);

            if (v > top)
                top = v;
            if (v < low)
                low = v;
        }
    *bottom = low / 16.0f;
    return top / 16.0f;
}

float VoxelRelief_DrawnTop(const VoxelMapInstance *inst)
{
    const ReliefLayout *l = LayoutOf(inst);
    return l != NULL && l->surface != NULL ? l->top : 0.0f;
}
