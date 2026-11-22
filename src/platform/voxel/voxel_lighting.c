#include <stdint.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "voxel_lighting.h"
#include "voxel_tree.h"
#include "voxel_building.h"
#include "voxel_relief.h"
#include "voxel_sign.h"
#include "../vita/vita_perf.h"

#if CTR_VOXEL_LIGHTING
#define CELL_CACHE_SIZE 4096u
typedef struct
{
    int16_t x, z;
    float top;
    float base;
    float lift;
    float shift;
    float bottom;
    float clear;
    uint32_t generation;
    int8_t crownPart;
    bool surface;
    bool sign;
    bool clearKnown;
    const uint16_t *mask;
} LightCell;

#define SAMPLE_CACHE_SIZE 4096u
typedef struct
{
    float x, y, z;
    float contact;
    bool lit;
    bool rayCast;
    uint32_t generation;
} LightSample;
#define SAMPLE_WAYS 4u
#define SAMPLE_SETS (SAMPLE_CACHE_SIZE / SAMPLE_WAYS)
VoxelSun gVoxelSun = {0.85f, 0.55f, 1.0f, 0};
static float sAmbient = VOXEL_AMBIENT;
static float sStepInverseX = 4.0f / 0.85f, sStepInverseZ = 4.0f / 0.55f;

#define CROWN_TOP 1.62f
#define PROP_TOP  2.0f

#define BLOCK_SHIFT      2
#define BLOCK_CACHE_SIZE 1024u
#define BLOCK_MARGIN     (1.0f / 256.0f)
typedef struct
{
    int16_t bx, bz;
    float ceiling;
    uint32_t generation;
} LightBlock;

#define REACH_BLOCKS    2
#define SHIFT_ROWS      4
#define DRAWN_PAST_ROWS 16

static int32_t DrawnReach(const VoxelMapInstance *inst)
{
    float reach = fabsf(VoxelRelief_Base(inst));

    return (int32_t)ceilf(reach) + (VoxelRelief_IsDrawn(inst) ? SHIFT_ROWS : 0);
}
#define BLOCK_CLIMB 1.0f

static int32_t Tile(float n)
{
    int32_t i = (int32_t)n;
    return n < (float)i ? i - 1 : i;
}

typedef struct
{
    float x0, x1, from, to, add;
} LightStretch;

typedef struct
{
    LightCell cells[CELL_CACHE_SIZE];
    LightSample samples[SAMPLE_CACHE_SIZE];
    uint8_t sampleNext[SAMPLE_SETS];
    uint32_t generation;
    uint32_t sampleGeneration;
    float ceiling;
    float mapCeiling[MAX_VOXEL_MAP_INSTANCES];
    uint32_t mapCeilings;
    LightBlock blocks[BLOCK_CACHE_SIZE];
    LightBlock reach[BLOCK_CACHE_SIZE];
    LightStretch stretches[MAX_VOXEL_MAP_INSTANCES];
    uint32_t stretchCount;
    uint32_t stretchMaps;
    uint32_t rays;
    uint32_t steps;
    uint32_t surfaceTests;
    uint32_t cellsClassified;
    uint32_t blocksComputed;
    double rayMs;
    double contactMs;
    int32_t apiDepth;
    double apiStart;
    double apiMs;
} LightContext;

/* Zero-initialized BSS contexts isolate Core 2 chunk baking from Core 0 entity sampling. */
static LightContext sChunkCtx;
static LightContext sEntityCtx;

#define LIGHT_API_BEGIN(ctx) do { if ((ctx)->apiDepth++ == 0) (ctx)->apiStart = VitaPerf_NowMs(); } while (0)
#define LIGHT_API_END(ctx)   do { if (--(ctx)->apiDepth == 0) (ctx)->apiMs += VitaPerf_NowMs() - (ctx)->apiStart; } while (0)

static void ResetContext(LightContext *const ctx);

static void ReadStretches(LightContext *const ctx)
{
    ctx->stretchCount = 0;
    ctx->stretchMaps = VoxelWorld_InstanceCount();
    for (uint32_t i = 0; i < ctx->stretchMaps && i < MAX_VOXEL_MAP_INSTANCES; ++i)
    {
        const VoxelMapInstance *inst = VoxelWorld_Instance(i);
        LightStretch *s = &ctx->stretches[ctx->stretchCount];

        if (inst != NULL && VoxelRelief_Stretch(inst, &s->from, &s->to, &s->add))
        {
            s->x0 = (float)inst->originX;
            s->x1 = (float)(inst->originX + inst->width);
            ++ctx->stretchCount;
        }
    }
}

static float Standing(LightContext *const ctx, float x, float z)
{
    if (ctx->generation == 0u)
        ResetContext(ctx);
    if (ctx->stretchMaps != VoxelWorld_InstanceCount())
        ReadStretches(ctx);
    for (uint32_t i = 0; i < ctx->stretchCount; ++i)
    {
        const LightStretch *s = &ctx->stretches[i];

        if (x >= s->x0 && x < s->x1 && z > s->from && z < s->to + s->add)
            return s->from + (z - s->from) * (s->to - s->from) / (s->to - s->from + s->add);
    }
    return z;
}

static bool RayStretched(LightContext *const ctx, float x, float z)
{
    float endX = x - VOXEL_SUN_DX * VOXEL_LIGHT_REACH;
    float endZ = z - VOXEL_SUN_DZ * VOXEL_LIGHT_REACH;

    if (ctx->generation == 0u)
        ResetContext(ctx);
    if (ctx->stretchMaps != VoxelWorld_InstanceCount())
        ReadStretches(ctx);
    for (uint32_t i = 0; i < ctx->stretchCount; ++i)
    {
        const LightStretch *s = &ctx->stretches[i];

        if (fmaxf(x, endX) + 1.0f > s->x0 && fminf(x, endX) - 1.0f < s->x1
         && fmaxf(z, endZ) + 1.0f > s->from && fminf(z, endZ) - 1.0f < s->to + s->add)
            return true;
    }
    return false;
}

static float CasterCeiling(void)
{
    float ceiling = PROP_TOP > CROWN_TOP ? PROP_TOP : CROWN_TOP;
    float buildings = VoxelBuildings_MaxTop(), highest = 0.0f;

    if (buildings > ceiling)
        ceiling = buildings;
    for (uint32_t i = 0; i < VoxelWorld_InstanceCount(); ++i)
    {
        const VoxelMapInstance *inst = VoxelWorld_Instance(i);

        if (inst == NULL || inst->indoor)
            continue;
        if (VoxelRelief_DrawnTop(inst) > ceiling)
            ceiling = VoxelRelief_DrawnTop(inst);
        if (VoxelRelief_Base(inst) > highest)
            highest = VoxelRelief_Base(inst);
    }
    return ceiling + highest;
}

static void MapCeilings(LightContext *const ctx)
{
    ReadStretches(ctx);
    ctx->mapCeilings = 0;
    for (uint32_t i = 0; i < VoxelWorld_InstanceCount() && i < MAX_VOXEL_MAP_INSTANCES; ++i)
    {
        const VoxelMapInstance *inst = VoxelWorld_Instance(i);
        float top = PROP_TOP > CROWN_TOP ? PROP_TOP : CROWN_TOP;

        if (inst == NULL)
            break;
        if (inst->indoor)
            top = -1000.0f;
        else
        {
            if (VoxelBuildings_LayoutTop(inst) > top)
                top = VoxelBuildings_LayoutTop(inst);
            if (VoxelRelief_DrawnTop(inst) > top)
                top = VoxelRelief_DrawnTop(inst);
            top += VoxelRelief_Base(inst);
        }
        ctx->mapCeiling[ctx->mapCeilings++] = top;
    }
}

static void ResetSamplesCtx(LightContext *const ctx)
{
    if (++ctx->sampleGeneration == 0)
    {
        memset(ctx->samples, 0, sizeof(ctx->samples));
        ctx->sampleGeneration = 1;
    }
}

static void ResetContext(LightContext *const ctx)
{
    ctx->stretchMaps = ~0u;
    if (++ctx->generation == 0)
    {
        memset(ctx->cells, 0, sizeof(ctx->cells));
        memset(ctx->blocks, 0, sizeof(ctx->blocks));
        memset(ctx->reach, 0, sizeof(ctx->reach));
        ctx->generation = 1;
    }
    ResetSamplesCtx(ctx);
    ctx->ceiling = CasterCeiling();
    MapCeilings(ctx);
}

bool VoxelLighting_SetSun(float dx, float dz, float strength, uint32_t key)
{
    if (!isfinite(dx) || !isfinite(dz) || !isfinite(strength)
     || fabsf(dx) > 1.5f || fabsf(dz) > 1.5f || strength < 0.0f || strength > 1.0f)
        return false;
    if (gVoxelSun.dx == dx && gVoxelSun.dz == dz
     && gVoxelSun.strength == strength && gVoxelSun.key == key)
        return false;
    gVoxelSun = (VoxelSun){dx, dz, strength, key};
    sAmbient = 1.0f - (1.0f - VOXEL_AMBIENT) * strength;
    sStepInverseX = fabsf(dx) > 0.0001f ? 4.0f / dx : 0.0f;
    sStepInverseZ = fabsf(dz) > 0.0001f ? 4.0f / dz : 0.0f;
    ResetSamplesCtx(&sChunkCtx);
    ResetSamplesCtx(&sEntityCtx);
    return true;
}

void VoxelLighting_Reset(void)
{
    ResetContext(&sChunkCtx);
    ResetContext(&sEntityCtx);
}

static float RayCeiling(const LightContext *const ctx, float x, float z)
{
    float endX = x - VOXEL_SUN_DX * VOXEL_LIGHT_REACH;
    float endZ = z - VOXEL_SUN_DZ * VOXEL_LIGHT_REACH;
    float x0 = fminf(x, endX) - 1.0f, x1 = fmaxf(x, endX) + 1.0f;
    float z0 = fminf(z, endZ) - 1.0f, z1 = fmaxf(z, endZ) + 1.0f;
    float ceiling = -1000.0f;
    uint32_t count = VoxelWorld_InstanceCount();

    if (count != ctx->mapCeilings)
        return ctx->ceiling;
    for (uint32_t i = 0; i < count; ++i)
    {
        const VoxelMapInstance *inst = VoxelWorld_Instance(i);

        if ((float)inst->originX < x1 && (float)(inst->originX + inst->width) > x0
         && (float)inst->originY < z1 && (float)(inst->originY + inst->height) > z0
         && ctx->mapCeiling[i] > ceiling)
            ceiling = ctx->mapCeiling[i];
    }
    return ceiling < ctx->ceiling ? ceiling : ctx->ceiling;
}

static const LightCell *Cell(LightContext *const ctx, int32_t x, int32_t z)
{
    if (ctx->generation == 0u)
        ResetContext(ctx);
    uint32_t key = ((uint32_t)x * 73856093u ^ (uint32_t)z * 19349663u)
                 & (CELL_CACHE_SIZE - 1);
    LightCell *cell = &ctx->cells[key];
    const VoxelMapInstance *inst;
    int32_t metatile;

    if (cell->generation == ctx->generation && cell->x == x && cell->z == z)
        return cell;
    ++ctx->cellsClassified;
    cell->generation = ctx->generation;
    cell->x = (int16_t)x;
    cell->z = (int16_t)z;
    cell->top = 0.0f;
    cell->crownPart = -1;
    cell->surface = false;
    cell->sign = false;
    cell->clearKnown = false;
    cell->mask = NULL;
    inst = VoxelWorld_GetInstanceAt(x, z);
    cell->base = VoxelRelief_Base(inst);
    cell->lift = 0.0f;
    cell->shift = 0.0f;
    if (inst == NULL)
    {
        for (int32_t k = 1; k <= DRAWN_PAST_ROWS; ++k)
        {
            const VoxelMapInstance *north = VoxelWorld_GetInstanceAt(x, z - k);
            float reach;

            if (north == NULL)
                continue;
            reach = VoxelRelief_Base(north)
                  + (VoxelRelief_IsDrawn(north) ? VoxelRelief_CellShift(north, x, z - k) : 0.0f);
            if (!north->indoor && reach > (float)k - 1.0f)
            {
                cell->base = VoxelRelief_Base(north);
                cell->shift = reach;
            }
            break;
        }
        return cell;
    }
    if (inst->indoor)
        return cell;
    if (VoxelRelief_IsDrawn(inst))
        cell->lift = VoxelRelief_CellShift(inst, x, z);
    cell->shift = cell->lift + cell->base;
    metatile = VoxelWorld_GetMetatileId(x, z);
    if (VoxelWorld_UsesTreeSprites(inst))
    {
        int32_t part = VoxelTree_Part(inst->secondaryTileset, metatile);

        /* FireRed wood floor sits flat under canopies that cast from their bases. */
        if (part == VOXEL_TREE_WOOD_FLOOR)
            return cell;
        if (part >= 0)
        {
            cell->crownPart = (int8_t)part;
            cell->top = CROWN_TOP;
            return cell;
        }
        if (VoxelTree_GroundMetatile(inst->secondaryTileset, metatile) != metatile)
            return cell;
    }
    if (VoxelBuildings_CellAt(inst, x, z, NULL, &cell->top))
    {
        cell->mask = VoxelBuildings_Footprint(inst, x, z);
        return cell;
    }
    cell->top = VoxelSign_CasterTop(inst, x, z);
    if (cell->top > 0.0f)
    {
        cell->sign = true;
        return cell;
    }
    if (VoxelRelief_IsDrawn(inst))
    {
        cell->top = VoxelRelief_SurfaceTop(inst, x, z, &cell->bottom);
        cell->surface = cell->top > 0.05f;
        return cell;
    }
    return cell;
}

static int32_t BlockOf(int32_t tile)
{
    return tile >= 0 ? tile >> BLOCK_SHIFT : -((-tile + (1 << BLOCK_SHIFT) - 1) >> BLOCK_SHIFT);
}

static float BlockCeiling(LightContext *const ctx, int32_t bx, int32_t bz)
{
    uint32_t key = ((uint32_t)bx * 73856093u ^ (uint32_t)bz * 19349663u)
                 & (BLOCK_CACHE_SIZE - 1);
    LightBlock *block = &ctx->blocks[key];
    float ceiling = -1000.0f;
    int32_t x0 = bx * (1 << BLOCK_SHIFT), z0 = bz * (1 << BLOCK_SHIFT);
    int32_t x1 = x0 + (1 << BLOCK_SHIFT), z1 = z0 + (1 << BLOCK_SHIFT);
    bool onMap = false;
    int32_t margin = 0;

    if (block->generation == ctx->generation && block->bx == bx && block->bz == bz)
        return block->ceiling;
    ++ctx->blocksComputed;
    for (uint32_t i = 0; i < VoxelWorld_InstanceCount(); ++i)
    {
        const VoxelMapInstance *inst = VoxelWorld_Instance(i);
        int32_t reach;

        if (inst == NULL || inst->originX >= x1 || inst->originX + inst->width <= x0)
            continue;
        reach = DrawnReach(inst);
        if (inst->originY < z1 + reach && inst->originY + inst->height > z0 - reach)
        {
            onMap = true;
            if (reach > margin)
                margin = reach;
        }
    }
    for (int32_t z = z0 - margin; onMap && z < z1 + margin; ++z)
        for (int32_t x = x0; x < x1; ++x)
        {
            const LightCell *cell = Cell(ctx, x, z);
            float lift = cell->surface ? 0.0f : cell->lift;
            int32_t at = cell->surface ? z : Tile((float)z + 0.5f + cell->shift);

            if (at < z0 || at >= z1)
                continue;
            if (cell->top > 0.0f && cell->top + cell->base + lift + BLOCK_MARGIN > ceiling)
                ceiling = cell->top + cell->base + lift + BLOCK_MARGIN;
        }
    block->generation = ctx->generation;
    block->bx = (int16_t)bx;
    block->bz = (int16_t)bz;
    block->ceiling = ceiling;
    return ceiling;
}

static float BlocksCeiling(LightContext *const ctx, int32_t bx0, int32_t bz0, int32_t bx1, int32_t bz1)
{
    float ceiling = -1000.0f;

    for (int32_t bz = bz0; bz <= bz1; ++bz)
        for (int32_t bx = bx0; bx <= bx1; ++bx)
        {
            float block = BlockCeiling(ctx, bx, bz);

            if (block > ceiling)
                ceiling = block;
        }
    return ceiling;
}

static float ReachCeiling(LightContext *const ctx, int32_t bx0, int32_t bz0, int32_t bx1, int32_t bz1)
{
    if (bx0 > bx1)
    {
        int32_t first = bx1;
        bx1 = bx0;
        bx0 = first;
    }
    if (bz0 > bz1)
    {
        int32_t first = bz1;
        bz1 = bz0;
        bz0 = first;
    }
    uint32_t key = ((uint32_t)bx1 * 73856093u ^ (uint32_t)bz1 * 19349663u)
                 & (BLOCK_CACHE_SIZE - 1);
    LightBlock *reach = &ctx->reach[key];

    if (bx1 - bx0 > REACH_BLOCKS || bz1 - bz0 > REACH_BLOCKS)
        return BlocksCeiling(ctx, bx0, bz0, bx1, bz1);
    if (reach->generation != ctx->generation || reach->bx != bx1 || reach->bz != bz1)
    {
        reach->ceiling = BlocksCeiling(ctx, bx1 - REACH_BLOCKS, bz1 - REACH_BLOCKS, bx1, bz1);
        reach->generation = ctx->generation;
        reach->bx = (int16_t)bx1;
        reach->bz = (int16_t)bz1;
    }
    return reach->ceiling;
}

#define SURFACE_BIAS 0.06f

static bool CellOccludes(LightContext *const ctx, const LightCell *cell, float x, float y, float z)
{
    y -= cell->base;
    if (cell->top <= y || (cell->top <= 0.0f && !cell->surface))
        return false;
    if (cell->surface)
    {
        if (y < cell->bottom - SURFACE_BIAS)
            return true;
        ++ctx->surfaceTests;
        return y < VoxelRelief_SurfaceAt(x, z) - SURFACE_BIAS;
    }
    if (cell->mask != NULL)
    {
        int32_t px = (int32_t)((x - (float)cell->x) * 16.0f), pz = (int32_t)((z - (float)cell->z) * 16.0f);

        px = px < 0 ? 0 : px > 15 ? 15 : px;
        pz = pz < 0 ? 0 : pz > 15 ? 15 : pz;
        return (cell->mask[pz] >> px) & 1u;
    }
    if (cell->sign)
        return VoxelSign_Occludes(VoxelWorld_GetInstanceAt(cell->x, cell->z), cell->x, cell->z,
                                  x, y, z);
    if (cell->crownPart == VOXEL_TREE_SMALL)
    {
        float dx = (x - ((float)cell->x + 0.5f)) * (1.0f / 0.48f);
        float dz = (z - ((float)cell->z + 0.35f)) * (1.0f / 0.55f);
        float dy = (y - 0.80f) * (1.0f / 0.62f);
        return dx * dx + dz * dz + dy * dy < 1.0f;
    }
    if (cell->crownPart >= VOXEL_TREE_WOOD)
    {
        float dx = (x - ((float)(cell->x - (cell->crownPart - VOXEL_TREE_WOOD)) + 1.5f)) * (1.0f / 1.45f);
        float dz = (z - ((float)cell->z + 0.35f)) * (1.0f / 0.65f);
        float dy = (y - 1.15f) * (1.0f / 0.80f);
        return dx * dx + dz * dz + dy * dy < 1.0f;
    }
    if (cell->crownPart >= 0)
    {
        float crownX = (float)(cell->x - (cell->crownPart & 1)) + 1.0f;
        float crownZ = (float)(cell->z - (cell->crownPart >> 1)) + 0.65f;
        float dx = (x - crownX) * (1.0f / 0.95f);
        float dz = (z - crownZ) * (1.0f / 0.65f);
        float dy = (y - 0.95f) * (1.0f / 0.67f);
        return dx * dx + dz * dz + dy * dy < 1.0f;
    }
    return true;
}

static bool ObjectAt(LightContext *const ctx, int32_t tx, float x, float y, float z, const LightCell **seen)
{
    const LightCell *cell = Cell(ctx, tx, Tile(z)), *o;

    *seen = cell;
    if (cell->shift == 0.0f)
        return CellOccludes(ctx, cell, x, y - cell->lift, z);
    if (cell->surface && CellOccludes(ctx, cell, x, y, z))
        return true;
    o = Cell(ctx, tx, Tile(z - cell->shift));
    if (o->surface || o->shift == 0.0f)
        return false;
    return CellOccludes(ctx, o, x, y - o->lift, z - o->shift);
}

static float ShiftedClear(LightContext *const ctx, int32_t tx, int32_t tz)
{
    LightCell *cell = (LightCell *)Cell(ctx, tx, tz);
    float clear, shift;

    if (cell->clearKnown)
        return cell->clear;
    clear = cell->surface ? cell->top + cell->base : -1000.0f;
    shift = cell->shift;
    for (int32_t oz = Tile((float)tz - shift); oz <= Tile((float)(tz + 1) - shift); ++oz)
    {
        const LightCell *o = Cell(ctx, tx, oz);

        if (!o->surface && o->shift != 0.0f && o->top > 0.0f
         && o->top + o->base + o->lift > clear)
            clear = o->top + o->base + o->lift;
    }
    cell = (LightCell *)Cell(ctx, tx, tz);
    cell->clear = clear;
    cell->clearKnown = true;
    return clear;
}

static bool Occludes(LightContext *const ctx, float x, float y, float z)
{
    const LightCell *cell;

    z = Standing(ctx, x, z);
    return ObjectAt(ctx, Tile(x), x, y, z, &cell);
}

static void RayPoint(float x, float y, float z, int32_t step, float *rx, float *ry, float *rz)
{
    float rise = (float)step * 0.25f;

    *rx = x - VOXEL_SUN_DX * rise;
    *ry = y + rise + 0.08f;
    *rz = z - VOXEL_SUN_DZ * rise;
}

static bool Lit(LightContext *const ctx, float x, float y, float z, bool alone)
{
    float ceiling;
    bool blocks;
    bool stretched;

    if (gVoxelSun.strength == 0.0f)
        return true;
    if (ctx->generation == 0u)
        ResetContext(ctx);
    ceiling = RayCeiling(ctx, x, z);
    stretched = RayStretched(ctx, x, z);
    blocks = !alone && VoxelWorld_InstanceCount() == ctx->mapCeilings;
    for (int32_t step = 1; step <= VOXEL_LIGHT_REACH * 4;)
    {
        float rx, ry, rz;
        int32_t tx, tz;
        const LightCell *cell;

        RayPoint(x, y, z, step, &rx, &ry, &rz);
        if (ry >= ceiling)
            break;
        if (stretched)
            rz = Standing(ctx, rx, rz);
        tx = Tile(rx);
        tz = Tile(rz);
        if (blocks && ceiling - ry > BLOCK_CLIMB)
        {
            float fx, fy, fz, reach;

            blocks = false;
            RayPoint(x, y, z, VOXEL_LIGHT_REACH * 4, &fx, &fy, &fz);
            reach = ReachCeiling(ctx, BlockOf(Tile(fx)), BlockOf(Tile(fz)), BlockOf(tx), BlockOf(tz));
            if (reach < ceiling)
                ceiling = reach;
            if (ry >= ceiling)
                break;
        }
        ++ctx->steps;
        if (ObjectAt(ctx, tx, rx, ry, rz, &cell))
            return false;
        ++step;
        if (stretched)
            continue;
        if (cell->x != tx || cell->z != tz)
            cell = Cell(ctx, tx, tz);
        if (cell->shift != 0.0f)
        {
            if (ShiftedClear(ctx, tx, tz) > ry)
                continue;
        }
        else if ((cell->crownPart >= 0 || cell->surface || cell->sign || cell->mask != NULL)
              && cell->top + cell->base > ry)
            continue;
        {
            float exitX = (float)tx + (VOXEL_SUN_DX < 0.0f ? 1.0f : 0.0f);
            float exitZ = (float)tz + (VOXEL_SUN_DZ < 0.0f ? 1.0f : 0.0f);
            float ex = sStepInverseX != 0.0f ? (x - exitX) * sStepInverseX : 33.0f;
            float ez = sStepInverseZ != 0.0f ? (z - exitZ) * sStepInverseZ : 33.0f;
            int32_t guess = (int32_t)fminf(33.0f, fminf(ex, ez)) - 1;
            const float left = (float)tx, top = (float)tz;

            if (guess > step)
                step = guess;
            for (; step <= VOXEL_LIGHT_REACH * 4; ++step)
            {
                RayPoint(x, y, z, step, &rx, &ry, &rz);
                if (rx < left || rx >= left + 1.0f
                 || rz < top || rz >= top + 1.0f || ry >= ceiling)
                    break;
            }
        }
    }
    return true;
}

bool VoxelLighting_Warm(int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t *cursor, uint32_t budget)
{
    enum { REGION = 64 };
    LightContext *const ctx = &sChunkCtx;
    const int32_t across = x1 - x0 + 1, points = across * (z1 - z0 + 1);
    uint32_t read = 0;

    if (gVoxelSun.strength == 0.0f)
        return true;
    if (ctx->generation == 0u)
        ResetContext(ctx);
    if (VoxelWorld_InstanceCount() != ctx->mapCeilings)
        return true;
    for (uint32_t point = *cursor / REGION, first = *cursor % REGION; point < (uint32_t)points; ++point, first = 0)
    {
        float x = (float)(x0 + (int32_t)point % across), z = (float)(z0 + (int32_t)point / across);
        const LightCell *ground = Cell(ctx, Tile(x), Tile(z));
        float y = ground->base + ground->lift, sx, sy, sz, fx, fy, fz;
        int32_t bx0, bz0, bx1, bz1, w;

        z += ground->shift;
        RayPoint(x, y, z, 1, &sx, &sy, &sz);
        if (RayCeiling(ctx, x, z) - sy <= BLOCK_CLIMB)
            continue;
        RayPoint(x, y, z, VOXEL_LIGHT_REACH * 4, &fx, &fy, &fz);
        bx0 = BlockOf(Tile(fx));
        bz0 = BlockOf(Tile(fz));
        bx1 = BlockOf(Tile(sx));
        bz1 = BlockOf(Tile(sz));
        if (bx0 > bx1) { int32_t t = bx0; bx0 = bx1; bx1 = t; }
        if (bz0 > bz1) { int32_t t = bz0; bz0 = bz1; bz1 = t; }
        if (bx1 - bx0 <= REACH_BLOCKS && bz1 - bz0 <= REACH_BLOCKS)
        {
            bx0 = bx1 - REACH_BLOCKS;
            bz0 = bz1 - REACH_BLOCKS;
        }
        w = bx1 - bx0 + 1;
        for (uint32_t k = first; k < (uint32_t)(w * (bz1 - bz0 + 1)) && k < REGION; ++k)
        {
            int32_t bx = bx0 + (int32_t)k % w, bz = bz0 + (int32_t)k / w;
            const LightBlock *block = &ctx->blocks[((uint32_t)bx * 73856093u ^ (uint32_t)bz * 19349663u)
                                                   & (BLOCK_CACHE_SIZE - 1)];

            if (block->generation == ctx->generation && block->bx == bx && block->bz == bz)
                continue;
            if (read == budget)
            {
                *cursor = point * REGION + k;
                return false;
            }
            BlockCeiling(ctx, bx, bz);
            ++read;
        }
    }
    *cursor = (uint32_t)points * REGION;
    return true;
}

static float Contact(LightContext *const ctx, float x, float y, float z)
{
    uint32_t covered = 0;

    if (y - Cell(ctx, Tile(x), Tile(Standing(ctx, x, z)))->base > 0.41f)
        return 1.0f;
    covered += Occludes(ctx, x - 0.28f, y + 0.3f, z);
    covered += Occludes(ctx, x + 0.28f, y + 0.3f, z);
    covered += Occludes(ctx, x, y + 0.3f, z - 0.28f);
    covered += Occludes(ctx, x, y + 0.3f, z + 0.28f);
    return 1.0f - 0.035f * (float)covered;
}

static LightSample *FindSample(LightContext *const ctx, float x, float y, float z)
{
    uint32_t hash = (uint32_t)Tile(x * 8.0f) * 73856093u
                  ^ (uint32_t)Tile(z * 8.0f) * 19349663u
                  ^ (uint32_t)Tile(y * 16.0f) * 83492791u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    uint32_t set = hash & (SAMPLE_SETS - 1u);
    LightSample *ways = &ctx->samples[set * SAMPLE_WAYS], *sample = NULL;

    if (ctx->generation == 0u)
        ResetContext(ctx);
    for (uint32_t i = 0; i < SAMPLE_WAYS; ++i)
    {
        if (ways[i].generation == ctx->sampleGeneration)
        {
            if (ways[i].x == x && ways[i].y == y && ways[i].z == z)
                return &ways[i];
        }
        else if (sample == NULL)
            sample = &ways[i];
    }
    if (sample == NULL)
    {
        sample = &ways[ctx->sampleNext[set]];
        ctx->sampleNext[set] = (ctx->sampleNext[set] + 1u) & (SAMPLE_WAYS - 1u);
    }
    {
        const double start = VitaPerf_NowMs();
        sample->contact = Contact(ctx, x, y, z);
        ctx->contactMs += VitaPerf_NowMs() - start;
    }
    sample->rayCast = false;
    sample->x = x;
    sample->y = y;
    sample->z = z;
    sample->generation = ctx->sampleGeneration;
    return sample;
}

static const LightSample *CachedSample(LightContext *const ctx, float x, float y, float z, bool alone)
{
    LightSample *sample = FindSample(ctx, x, y, z);

    if (!sample->rayCast)
    {
        const double start = VitaPerf_NowMs();
        if (gVoxelSun.strength > 0.0f)
            ++ctx->rays;
        sample->lit = Lit(ctx, x, y, z, alone);
        sample->rayCast = true;
        ctx->rayMs += VitaPerf_NowMs() - start;
    }
    return sample;
}

static float CachedContact(LightContext *const ctx, float x, float y, float z)
{
    if (y - Cell(ctx, Tile(x), Tile(Standing(ctx, x, z)))->base > 0.41f)
        return 1.0f;
    return FindSample(ctx, x, y, z)->contact;
}

uint32_t VoxelLighting_Rays(void)
{
    return sChunkCtx.rays;
}

uint32_t VoxelLighting_Steps(void)
{
    return sChunkCtx.steps;
}

uint32_t VoxelLighting_SurfaceTests(void)
{
    return sChunkCtx.surfaceTests;
}

void VoxelLighting_GetWork(VoxelLightingWork *work)
{
    work->rays = sChunkCtx.rays;
    work->steps = sChunkCtx.steps;
    work->cells = sChunkCtx.cellsClassified;
    work->blocks = sChunkCtx.blocksComputed;
    work->rayMs = sChunkCtx.rayMs;
    work->contactMs = sChunkCtx.contactMs;
    work->apiMs = sChunkCtx.apiMs;
}

static float SampleInContext(LightContext *const ctx, float x, float y, float z, bool alone)
{
    LIGHT_API_BEGIN(ctx);
    const LightSample *sample = CachedSample(ctx, x, y, z, alone);
    const float shade = (sample->lit ? 1.0f : sAmbient) * sample->contact;
    LIGHT_API_END(ctx);
    return shade;
}

float VoxelLighting_Sample(float x, float y, float z)
{
    return SampleInContext(&sChunkCtx, x, y, z, false);
}

float VoxelLighting_SampleAlone(float x, float y, float z)
{
    return SampleInContext(&sEntityCtx, x, y, z, true);
}

uint32_t VoxelLighting_Hash(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    int32_t reachX = (int32_t)ceilf(fabsf(VOXEL_SUN_DX) * VOXEL_LIGHT_REACH);
    int32_t reachZ = (int32_t)ceilf(fabsf(VOXEL_SUN_DZ) * VOXEL_LIGHT_REACH);
    int32_t northReach = VOXEL_SUN_DZ > 0.0f ? reachZ : 0;
    int32_t hx0 = x0 - (VOXEL_SUN_DX > 0.0f ? reachX : 0) - 1;
    int32_t hz0 = z0 - (northReach > VOXEL_CHUNK_MARGIN_NORTH
                  ? northReach : VOXEL_CHUNK_MARGIN_NORTH) - 1;
    int32_t hx1 = x1 + (VOXEL_SUN_DX < 0.0f ? reachX : 0) + 1;
    int32_t hz1 = z1 + (VOXEL_SUN_DZ < 0.0f ? reachZ : 0) + 2;
    uint32_t hash = VoxelWorld_BlockHash(hx0, hz0, hx1, hz1);

    hash = (hash ^ gVoxelSun.key) * 16777619u;

    for (uint32_t i = 0; i < VoxelWorld_InstanceCount(); ++i)
    {
        const VoxelMapInstance *inst = VoxelWorld_Instance(i);
        uint32_t tag;
        if (inst->originX >= hx1 || inst->originY >= hz1
         || inst->originX + inst->width <= hx0 || inst->originY + inst->height <= hz0)
            continue;
        tag = (uint32_t)inst->layoutId * 16777619u;
        tag ^= (uint32_t)(inst->originX - x0) * 73856093u;
        tag ^= (uint32_t)(inst->originY - z0) * 19349663u;
        tag ^= (uint32_t)(uintptr_t)inst->primaryTileset;
        tag ^= (uint32_t)(uintptr_t)inst->secondaryTileset;
        tag ^= inst->indoor ? 0x80000000u : 0;
        hash ^= tag;
    }
    return hash;
}

static VoxelVertex Mix(VoxelVertex a, VoxelVertex b, float t)
{
    VoxelVertex v;
    v.x = a.x + (b.x - a.x) * t;
    v.y = a.y + (b.y - a.y) * t;
    v.z = a.z + (b.z - a.z) * t;
    v.u = a.u + (b.u - a.u) * t;
    v.v = a.v + (b.v - a.v) * t;
    v.shade = a.shade + (b.shade - a.shade) * t;
    return v;
}

static void RawQuad(VoxelBuilder *builder, const VoxelVertex *a,
                    const VoxelVertex *b, const VoxelVertex *c, const VoxelVertex *d)
{
    if (builder->count + 6 > builder->capacity)
    {
        builder->dropped += 2;
        return;
    }
    if (fabsf(a->shade - c->shade) > fabsf(b->shade - d->shade)
     && fabsf(a->x + c->x - b->x - d->x) < 0.001f
     && fabsf(a->y + c->y - b->y - d->y) < 0.001f
     && fabsf(a->z + c->z - b->z - d->z) < 0.001f
     && fabsf(a->u + c->u - b->u - d->u) < 0.0001f
     && fabsf(a->v + c->v - b->v - d->v) < 0.0001f)
    {
        VoxelBuilder_Tri(builder, a, b, d);
        VoxelBuilder_Tri(builder, b, c, d);
        return;
    }
    VoxelBuilder_Tri(builder, a, b, c);
    VoxelBuilder_Tri(builder, a, c, d);
}

float VoxelLighting_Face(float nx, float ny, float nz)
{
    float length = sqrtf(nx * nx + ny * ny + nz * nz);
    float facing;

    if (length <= 0.0f)
        return 1.0f;
    facing = (ny - VOXEL_SUN_DX * nx - VOXEL_SUN_DZ * nz) / length;
    if (facing <= 0.0f)
        return sAmbient;
    if (facing >= 1.0f)
        return 1.0f;
    return sAmbient + (1.0f - sAmbient) * facing;
}

static void WorldPoint(const VoxelBuilder *builder, const VoxelVertex *v,
                       float *x, float *y, float *z)
{
    *x = v->x;
    *y = v->y + builder->lift + builder->base;
    *z = VoxelBuilder_StretchZ(builder, v->z + builder->shift);
}

static void EmitPolygon(VoxelBuilder *builder, const VoxelVertex *out, uint32_t count)
{
    if (count == 4)
    {
        RawQuad(builder, &out[0], &out[1], &out[2], &out[3]);
        return;
    }
    VoxelBuilder_Tri(builder, &out[0], &out[1], &out[2]);
}

static void LightPolygon(LightContext *const ctx, VoxelBuilder *builder, const VoxelVertex *const *v, uint32_t count,
                         float nx, float ny, float nz)
{
    VoxelVertex out[4];
    float face = VoxelLighting_Face(nx, ny, nz);
    bool sunward = face > sAmbient;
    float px, py, pz;

    for (uint32_t i = 0; i < count; ++i)
        out[i] = *v[i];

    for (uint32_t i = 0; i < count; ++i)
    {
        const LightSample *sample;

        WorldPoint(builder, v[i], &px, &py, &pz);
        if (!sunward)
        {
            out[i].shade *= sAmbient * CachedContact(ctx, px, py, pz);
            continue;
        }
        sample = CachedSample(ctx, px, py, pz, false);
        out[i].shade *= (sample->lit ? face : sAmbient) * sample->contact;
    }
    EmitPolygon(builder, out, count);
}

static void LightVertexFaces(LightContext *const ctx, VoxelBuilder *builder, const VoxelVertex *const *v, uint32_t count)
{
    VoxelVertex out[4];

    for (uint32_t i = 0; i < count; ++i)
    {
        float face = v[i]->shade, px, py, pz;

        out[i] = *v[i];
        WorldPoint(builder, v[i], &px, &py, &pz);
        if (face <= sAmbient + 0.001f)
        {
            out[i].shade = sAmbient * CachedContact(ctx, px, py, pz);
            continue;
        }
        {
            const LightSample *sample = CachedSample(ctx, px, py, pz, false);

            out[i].shade = (sample->lit ? face : sAmbient) * sample->contact;
        }
    }
    EmitPolygon(builder, out, count);
}

static int32_t LatticeInfer(const float *wy, const float *wz, const int8_t *lit, int32_t a, int32_t c)
{
    enum { SIDE = VOXEL_RELIEF_SIDE };
    int32_t k = c * SIDE + a, from[4], count = 0;
    float my = 0.0f, mz = 0.0f;

    if ((a & 1) && (c & 1))
    {
        from[count++] = k - SIDE - 1;
        from[count++] = k - SIDE + 1;
        from[count++] = k + SIDE - 1;
        from[count++] = k + SIDE + 1;
    }
    else if (a & 1)
    {
        from[count++] = k - 1;
        from[count++] = k + 1;
    }
    else
    {
        from[count++] = k - SIDE;
        from[count++] = k + SIDE;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        if (lit[from[i]] < 0 || lit[from[i]] != lit[from[0]])
            return -1;
        my += wy[from[i]];
        mz += wz[from[i]];
    }
    if (fabsf(my / count - wy[k]) > 0.02f || fabsf(mz / count - wz[k]) > 0.02f)
        return -1;
    return lit[from[0]];
}

static void LatticeImpl(LightContext *const ctx, VoxelBuilder *builder, const VoxelVertex *lattice)
{
    enum { SIDE = VOXEL_RELIEF_SIDE, LAST = VOXEL_RELIEF_SIDE - 1 };
    float wx[SIDE * SIDE], wy[SIDE * SIDE], wz[SIDE * SIDE];
    int8_t lit[SIDE * SIDE];

    if (gVoxelSun.strength == 0.0f)
        return;
    for (int32_t k = 0; k < SIDE * SIDE; ++k)
    {
        WorldPoint(builder, &lattice[k], &wx[k], &wy[k], &wz[k]);
        lit[k] = -1;
    }
    for (int32_t c = 0; c <= LAST; c += 2)
        for (int32_t a = 0; a <= LAST; a += 2)
        {
            int32_t k = c * SIDE + a;

            if (lattice[k].shade > sAmbient + 0.001f)
                lit[k] = CachedSample(ctx, wx[k], wy[k], wz[k], false)->lit;
        }
    for (int32_t c = 0; c <= LAST; ++c)
        for (int32_t a = 0; a <= LAST; ++a)
        {
            int32_t k = c * SIDE + a, v;
            LightSample *sample;

            if (((a | c) & 1) == 0 || lattice[k].shade <= sAmbient + 0.001f
             || (v = LatticeInfer(wy, wz, lit, a, c)) < 0)
                continue;
            sample = FindSample(ctx, wx[k], wy[k], wz[k]);
            if (!sample->rayCast)
            {
                sample->lit = v != 0;
                sample->rayCast = true;
            }
        }
}

static void LatticeShadesImpl(LightContext *const ctx, VoxelBuilder *builder, const VoxelVertex *lattice, float *shade)
{
    for (int32_t k = 0; k < VOXEL_RELIEF_SIDE * VOXEL_RELIEF_SIDE; ++k)
    {
        float face = lattice[k].shade, px, py, pz;

        WorldPoint(builder, &lattice[k], &px, &py, &pz);
        if (face <= sAmbient + 0.001f)
            shade[k] = sAmbient * CachedContact(ctx, px, py, pz);
        else
        {
            const LightSample *sample = CachedSample(ctx, px, py, pz, false);

            shade[k] = (sample->lit ? face : sAmbient) * sample->contact;
        }
    }
}

static void LightFace(LightContext *const ctx, VoxelBuilder *builder, const VoxelVertex *a,
                      const VoxelVertex *b, const VoxelVertex *c,
                      const VoxelVertex *d, uint32_t count)
{
    VoxelVertex corners[4] = {*a, *b, *c, *d};
    const VoxelVertex *points[4] = {a, b, c, d};
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
    float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float face;

    if (builder->vertexFace && builder->lightingConstant < 0.0f && !builder->rounded)
    {
        LightVertexFaces(ctx, builder, points, count);
        return;
    }
    if (ny < 0.0f)
    {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    if (builder->rounded)
    {
        float length = sqrtf(nx * nx + ny * ny + nz * nz);

        if (length > 0.0f)
        {
            nx /= length;
            ny = ny / length + 1.0f;
            nz /= length;
        }
    }
    face = VoxelLighting_Face(nx, ny, nz);

    if (builder->lightingConstant >= 0.0f)
    {
        float light = builder->lightingConstant > sAmbient + 0.01f
                    ? builder->lightingConstant * face : builder->lightingConstant;

        for (uint32_t i = 0; i < 4; ++i)
            corners[i].shade *= light;
        EmitPolygon(builder, corners, count);
        return;
    }

    if (builder->rounded)
    {
        for (uint32_t i = 0; i < 4; ++i)
        {
            float px, py, pz;
            const LightSample *sample;

            WorldPoint(builder, points[i], &px, &py, &pz);
            sample = CachedSample(ctx, px, py, pz, false);
            corners[i].shade *= (sample->lit ? face : sAmbient) * sample->contact;
        }
        EmitPolygon(builder, corners, count);
        return;
    }

    if (builder->lightingRefine
     && a->y == b->y && a->y == c->y && a->y == d->y && (a->y <= 0.41f || builder->artShaded)
     && fabsf(a->x - c->x) >= 0.99f && fabsf(a->z - c->z) >= 0.99f
     && fabsf(a->x - c->x) <= 1.01f && fabsf(a->z - c->z) <= 1.01f)
    {
        VoxelVertex grid[9];
        float px, py, pz;

        WorldPoint(builder, a, &px, &py, &pz);
        const float l0 = VoxelLighting_Sample(px, py, pz);
        grid[0] = *a;
        grid[0].shade *= l0;

        WorldPoint(builder, b, &px, &py, &pz);
        const float l2 = VoxelLighting_Sample(px, py, pz);
        grid[2] = *b;
        grid[2].shade *= l2;

        WorldPoint(builder, d, &px, &py, &pz);
        const float l6 = VoxelLighting_Sample(px, py, pz);
        grid[6] = *d;
        grid[6].shade *= l6;

        WorldPoint(builder, c, &px, &py, &pz);
        const float l8 = VoxelLighting_Sample(px, py, pz);
        grid[8] = *c;
        grid[8].shade *= l8;

        float lo = fminf(fminf(l0, l2), fminf(l6, l8));
        float hi = fmaxf(fmaxf(l0, l2), fmaxf(l6, l8));

        if (hi - lo <= 0.08f)
        {
            RawQuad(builder, &grid[0], &grid[2], &grid[8], &grid[6]);
            return;
        }

        static const uint8_t remRows[5] = {0, 1, 1, 1, 2};
        static const uint8_t remCols[5] = {1, 0, 1, 2, 1};
        static const uint8_t remIdx[5]  = {1, 3, 4, 5, 7};
        for (int32_t k = 0; k < 5; ++k)
        {
            const float t = (float)remCols[k] * 0.5f;
            const float s = (float)remRows[k] * 0.5f;
            VoxelVertex v = Mix(Mix(*a, *b, t), Mix(*d, *c, t), s);
            WorldPoint(builder, &v, &px, &py, &pz);
            const float light = VoxelLighting_Sample(px, py, pz);
            if (light < lo) lo = light;
            if (light > hi) hi = light;
            v.shade *= light;
            grid[remIdx[k]] = v;
        }

        if (hi - lo > 0.12f && builder->count + 24 <= builder->capacity)
        {
            for (int32_t row = 0; row < 2; ++row)
                for (int32_t col = 0; col < 2; ++col)
                {
                    int32_t i = row * 3 + col;
                    RawQuad(builder, &grid[i], &grid[i + 1], &grid[i + 4], &grid[i + 3]);
                }
        }
        else
        {
            RawQuad(builder, &grid[0], &grid[2], &grid[8], &grid[6]);
        }
        return;
    }
    LightPolygon(ctx, builder, points, count, nx, ny, nz);
}

void VoxelLighting_Lattice(VoxelBuilder *builder, const VoxelVertex *lattice)
{
    LIGHT_API_BEGIN(&sChunkCtx);
    LatticeImpl(&sChunkCtx, builder, lattice);
    LIGHT_API_END(&sChunkCtx);
}

void VoxelLighting_LatticeShades(VoxelBuilder *builder, const VoxelVertex *lattice, float *shade)
{
    LIGHT_API_BEGIN(&sChunkCtx);
    LatticeShadesImpl(&sChunkCtx, builder, lattice, shade);
    LIGHT_API_END(&sChunkCtx);
}

void VoxelLighting_Quad(VoxelBuilder *builder, const VoxelVertex *a,
                        const VoxelVertex *b, const VoxelVertex *c,
                        const VoxelVertex *d)
{
    LIGHT_API_BEGIN(&sChunkCtx);
    LightFace(&sChunkCtx, builder, a, b, c, d, 4);
    LIGHT_API_END(&sChunkCtx);
}

void VoxelLighting_Tri(VoxelBuilder *builder, const VoxelVertex *a,
                       const VoxelVertex *b, const VoxelVertex *c)
{
    LIGHT_API_BEGIN(&sChunkCtx);
    LightFace(&sChunkCtx, builder, a, b, c, c, 3);
    LIGHT_API_END(&sChunkCtx);
}

#define MODEL_SHADE_WEST  0.80f
#define MODEL_SHADE_EAST  0.72f
#define MODEL_SHADE_BACK  0.66f
#define MODEL_SHADE_WOUND 0.90f

void VoxelLighting_ModelTri(VoxelBuilder *builder, const VoxelVertex *a,
                            const VoxelVertex *b, const VoxelVertex *c, float drawnShade)
{
    float face = VoxelLighting_ModelShade(a, b, c, drawnShade);
    VoxelVertex plain[3];
    plain[0] = *a; plain[0].shade = face;
    plain[1] = *b; plain[1].shade = face;
    plain[2] = *c; plain[2].shade = face;
    VoxelBuilder_Tri(builder, &plain[0], &plain[1], &plain[2]);
}

float VoxelLighting_ModelShade(const VoxelVertex *a, const VoxelVertex *b,
                               const VoxelVertex *c, float drawnShade)
{
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
    float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    bool flip;

    if (fabsf(drawnShade - MODEL_SHADE_WOUND) < 0.02f)
        flip = false;
    else if (fabsf(drawnShade - MODEL_SHADE_WEST) < 0.02f)
        flip = nx > 0.0f;
    else if (fabsf(drawnShade - MODEL_SHADE_EAST) < 0.02f)
        flip = nx < 0.0f;
    else if (fabsf(drawnShade - MODEL_SHADE_BACK) < 0.02f)
        flip = ny + nz > 0.0f;
    else
        flip = ny + nz < 0.0f;
    if (flip)
    {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    return VoxelLighting_Face(nx, ny, nz);
}
#endif
