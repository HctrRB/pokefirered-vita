/*
 * FireRed trees, props, fences, ledges, and dense wood canopies.
 * Derived from Pokemon-FireRed-3DS (2d7c8ec, 3ds_port/src/voxel/voxel_tree.c),
 * with Core 0/Core 2 safe initialization via VoxelTree_Init().
 */
#include <stddef.h>

#include "voxel_tree.h"
#include "voxel_relief.h"
#include "voxel_props_table.h"
#include "voxel_atlas.h"
#include "voxel_building.h"
#include "voxel_room.h"

#define TEX_U(px) ((float)(px) / VOXEL_TREE_TEXTURE_DIM)
#define TEX_V(py) (1.0f - (float)(py) / VOXEL_TREE_TEXTURE_H)

enum { WOODS = (int32_t)(sizeof(sWoods) / sizeof(sWoods[0])) };

static int16_t sPropIndex[0x280];
static int8_t sWoodCellIndex[WOODS][0x100];
static int32_t sSideWoodIdx = -1;
static int sTablesState = 0; /* 0 = uninit, 1 = building, 2 = ready */

static void EnsureTables(void)
{
    if (__atomic_load_n(&sTablesState, __ATOMIC_ACQUIRE) == 2)
        return;
    int expected = 0;
    if (__atomic_compare_exchange_n(&sTablesState, &expected, 1, false,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(sPropIndex) / sizeof(sPropIndex[0])); ++i)
            sPropIndex[i] = -1;
        for (uint32_t i = 0; i < (uint32_t)(sizeof(sProps) / sizeof(sProps[0])); ++i)
        {
            if (sProps[i].set == 0 && sProps[i].id < 0x280u)
                sPropIndex[sProps[i].id] = (int16_t)i;
            if (sProps[i].set == 0 && sProps[i].id == 0x0E6u)
                sSideWoodIdx = (int32_t)i;
        }
        for (int32_t w = 0; w < WOODS; ++w)
            for (int32_t i = 0; i < 0x100; ++i)
                sWoodCellIndex[w][i] = -1;
        for (uint32_t i = 0; i < (uint32_t)(sizeof(sWoodCells) / sizeof(sWoodCells[0])); ++i)
        {
            if (sWoodCells[i].wood < WOODS && sWoodCells[i].id >= 0x280u && sWoodCells[i].id < 0x380u)
                sWoodCellIndex[sWoodCells[i].wood][sWoodCells[i].id - 0x280u] = (int8_t)i;
        }
        __atomic_store_n(&sTablesState, 2, __ATOMIC_RELEASE);
    }
    else
    {
        while (__atomic_load_n(&sTablesState, __ATOMIC_ACQUIRE) != 2)
        {
        }
    }
}

void VoxelTree_Init(void)
{
    EnsureTables();
}

static int32_t PropIndex(const void *secondaryTileset, int32_t metatileId)
{
    EnsureTables();
    if (metatileId < 0)
        return -1;
    if (metatileId < 0x280)
        return sPropIndex[metatileId];
    for (uint32_t i = 0; i < (uint32_t)(sizeof(sProps) / sizeof(sProps[0])); ++i)
        if (sProps[i].id == (uint16_t)metatileId && sPropTilesets[sProps[i].set] == secondaryTileset)
            return (int32_t)i;
    return -1;
}

static bool SideIsWood(int32_t prop)
{
    EnsureTables();
    return sSideWoodIdx >= 0
        && sProps[prop].sx == sProps[sSideWoodIdx].x
        && sProps[prop].sy == sProps[sSideWoodIdx].y;
}

static void EmitPost(VoxelBuilder *builder, int32_t x, int32_t y, int32_t prop,
                     int32_t north, int32_t south)
{
    float cx = (float)x + sProps[prop].centre / 16.0f;
    float h = (16 - sProps[prop].sr) / 16.0f;
    float v0 = TEX_V(sProps[prop].sy + sProps[prop].sr);
    float v1 = TEX_V(sProps[prop].sy + 16);
    float pu0 = TEX_U(sProps[prop].sx + 1), pu1 = TEX_U(sProps[prop].sx + 8);
    float ru0 = TEX_U(sProps[prop].sx + 8), ru1 = TEX_U(sProps[prop].sx + 9);
    const float half = 3.5f / 16.0f;
    float pz = south < 0 ? (float)y + 0.5f : (float)y + 1.0f;
    float z0 = north < 0 ? pz : (float)y;
    float z1 = south < 0 ? pz
             : sProps[south].kind == 1 ? (float)y + 1.0f
             : (float)(y + 1) + sProps[south].r1 / 16.0f;

    {
        float x0 = cx - half, x1 = cx + half;
        float zb = pz - 2.5f / 16.0f, zf = pz + 2.5f / 16.0f;
        float vt = TEX_V(sProps[prop].sy + sProps[prop].sr + 2);

        VoxelBuilder_Quad(builder,
            &(VoxelVertex){x0, h, zb, pu0, v0, 1.0f}, &(VoxelVertex){x1, h, zb, pu1, v0, 1.0f},
            &(VoxelVertex){x1, h, zf, pu1, vt, 1.0f}, &(VoxelVertex){x0, h, zf, pu0, vt, 1.0f});
        VoxelBuilder_Quad(builder,
            &(VoxelVertex){x0, h,    zf, pu0, v0, 1.0f}, &(VoxelVertex){x1, h,    zf, pu1, v0, 1.0f},
            &(VoxelVertex){x1, 0.0f, zf, pu1, v1, 1.0f}, &(VoxelVertex){x0, 0.0f, zf, pu0, v1, 1.0f});
    }
    if (!SideIsWood(prop) && z1 > z0)
    {
        float hr = 3.0f / 16.0f, vr = TEX_V(sProps[prop].sy + 13);

        VoxelBuilder_Quad(builder,
            &(VoxelVertex){cx, h,  z0, ru0, v0, 0.9f}, &(VoxelVertex){cx, h,  z1, ru1, v0, 0.9f},
            &(VoxelVertex){cx, hr, z1, ru1, vr, 0.9f}, &(VoxelVertex){cx, hr, z0, ru0, vr, 0.9f});
    }
}

static void EmitLedge(VoxelBuilder *builder, int32_t x, int32_t y, int32_t prop)
{
    float wx = (float)x;
    float h = (sProps[prop].r1 - sProps[prop].r0) / 16.0f;
    float u0 = TEX_U(sProps[prop].x), u1 = TEX_U(sProps[prop].x + 16);
    float vm = TEX_V(sProps[prop].y + sProps[prop].r0);
    float vb = TEX_V(sProps[prop].y + sProps[prop].r1);
    float zf = (float)y + sProps[prop].r1 / 16.0f;

    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        h,    zf, u0, vm, 1.0f}, &(VoxelVertex){wx + 1.0f, h,    zf, u1, vm, 1.0f},
        &(VoxelVertex){wx + 1.0f, 0.0f, zf, u1, vb, 1.0f}, &(VoxelVertex){wx,        0.0f, zf, u0, vb, 1.0f});
}

static void EmitBlock(VoxelBuilder *builder, int32_t x, int32_t y, int32_t prop)
{
    float x0 = (float)x, x1 = x0 + 1.0f;
    float z0 = (float)y + sProps[prop].r0 / 16.0f, z1 = (float)y + sProps[prop].r1 / 16.0f;
    const float h = 0.5f;
    float u0 = TEX_U(sProps[prop].x), u1 = TEX_U(sProps[prop].x + 16);
    float vt = TEX_V(sProps[prop].y + sProps[prop].r0), vb = TEX_V(sProps[prop].y + sProps[prop].r1);
    float ve = TEX_V(sProps[prop].y + sProps[prop].r1 - 1);

    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x0, h, z0, u0, vt, 1.0f}, &(VoxelVertex){x1, h, z0, u1, vt, 1.0f},
        &(VoxelVertex){x1, h, z1, u1, vb, 1.0f}, &(VoxelVertex){x0, h, z1, u0, vb, 1.0f});
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x0, h, z1, u0, ve, 0.85f}, &(VoxelVertex){x1, h, z1, u1, ve, 0.85f},
        &(VoxelVertex){x1, 0.0f, z1, u1, vb, 0.85f}, &(VoxelVertex){x0, 0.0f, z1, u0, vb, 0.85f});
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x1, h, z0, u1, vt, 0.7f}, &(VoxelVertex){x0, h, z0, u0, vt, 0.7f},
        &(VoxelVertex){x0, 0.0f, z0, u0, ve, 0.7f}, &(VoxelVertex){x1, 0.0f, z0, u1, ve, 0.7f});
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x0, h, z0, u0, vt, 0.8f}, &(VoxelVertex){x0, h, z1, u0, vb, 0.8f},
        &(VoxelVertex){x0, 0.0f, z1, u0 + 1.0f / VOXEL_TREE_TEXTURE_DIM, vb, 0.8f},
        &(VoxelVertex){x0, 0.0f, z0, u0 + 1.0f / VOXEL_TREE_TEXTURE_DIM, vt, 0.8f});
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x1, h, z1, u1, vb, 0.8f}, &(VoxelVertex){x1, h, z0, u1, vt, 0.8f},
        &(VoxelVertex){x1, 0.0f, z0, u1 - 1.0f / VOXEL_TREE_TEXTURE_DIM, vt, 0.8f},
        &(VoxelVertex){x1, 0.0f, z1, u1 - 1.0f / VOXEL_TREE_TEXTURE_DIM, vb, 0.8f});
}

static void EmitDrawnBlock(VoxelBuilder *builder, int32_t x, int32_t y, int32_t prop,
                           bool westEnd, bool eastEnd)
{
    float x0 = (float)x, x1 = x0 + 1.0f;
    float h = (16 - sProps[prop].centre) / 16.0f;
    float zb = (float)y + h, zf = (float)y + 1.0f;
    float u0 = TEX_U(sProps[prop].x), u1 = TEX_U(sProps[prop].x + 16);
    float vt = TEX_V(sProps[prop].y), vm = TEX_V(sProps[prop].y + sProps[prop].centre);
    float vb = TEX_V(sProps[prop].y + 16);

    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x0, h, zb, u0, vt, 1.0f}, &(VoxelVertex){x1, h, zb, u1, vt, 1.0f},
        &(VoxelVertex){x1, h, zf, u1, vm, 1.0f}, &(VoxelVertex){x0, h, zf, u0, vm, 1.0f});
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){x0, h,    zf, u0, vm, 1.0f}, &(VoxelVertex){x1, h,    zf, u1, vm, 1.0f},
        &(VoxelVertex){x1, 0.0f, zf, u1, vb, 1.0f}, &(VoxelVertex){x0, 0.0f, zf, u0, vb, 1.0f});
    if (westEnd)
    {
        float uc0 = TEX_U(sProps[prop].x + 2), uc1 = TEX_U(sProps[prop].x + 3);

        VoxelBuilder_Quad(builder,
            &(VoxelVertex){x0 + 1.0f / 16.0f, h,    zb, uc0, vm, 0.8f},
            &(VoxelVertex){x0 + 1.0f / 16.0f, h,    zf, uc1, vm, 0.8f},
            &(VoxelVertex){x0 + 1.0f / 16.0f, 0.0f, zf, uc1, vb, 0.8f},
            &(VoxelVertex){x0 + 1.0f / 16.0f, 0.0f, zb, uc0, vb, 0.8f});
    }
    if (eastEnd)
    {
        float uc0 = TEX_U(sProps[prop].x + 13), uc1 = TEX_U(sProps[prop].x + 14);

        VoxelBuilder_Quad(builder,
            &(VoxelVertex){x1 - 1.0f / 16.0f, h,    zf, uc1, vm, 0.8f},
            &(VoxelVertex){x1 - 1.0f / 16.0f, h,    zb, uc0, vm, 0.8f},
            &(VoxelVertex){x1 - 1.0f / 16.0f, 0.0f, zb, uc0, vb, 0.8f},
            &(VoxelVertex){x1 - 1.0f / 16.0f, 0.0f, zf, uc1, vb, 0.8f});
    }
}

static void EmitProp(VoxelBuilder *builder, int32_t x, int32_t y, int32_t prop,
                     int32_t below, int32_t north, bool westFence, bool eastFence)
{
    float wx = (float)x;
    float s0 = 0.0f, s1 = 1.0f;
    float zc = (float)y + sProps[prop].r1 / 16.0f;
    float base = 0.0f;
    float h = (sProps[prop].r1 - sProps[prop].r0) / 16.0f;
    float u0 = TEX_U(sProps[prop].x), u1 = TEX_U(sProps[prop].x + 16);
    float v0 = TEX_V(sProps[prop].y + sProps[prop].r0), v1 = TEX_V(sProps[prop].y + sProps[prop].r1);

    if (sProps[prop].kind == 2)
    {
        VoxelBuilder_Quad(builder,
            &(VoxelVertex){wx,        0.35f, zc - 0.5f, u0, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, 0.35f, zc - 0.5f, u1, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, 0.0f,  zc,        u1, v1, 1.0f},
            &(VoxelVertex){wx,        0.0f,  zc,        u0, v1, 1.0f});
        return;
    }
    {
        int32_t post = below >= 0 && sProps[below].kind == 1 ? below
                     : north >= 0 && sProps[north].kind == 1 ? north : -1;

        if (post >= 0 && sProps[prop].kind == 0 && westFence != eastFence)
        {
            float c = sProps[post].centre / 16.0f;

            if (westFence)
                s1 = c + 2.0f / 16.0f;
            else
                s0 = c - 2.0f / 16.0f;
        }
    }
    if (below >= 0 && sProps[prop].r1 == 16 && sProps[prop].r0 > 0 && sProps[below].r0 == 0
        && sProps[below].kind == 0)
    {
        zc = (float)(y + 1) + sProps[below].r1 / 16.0f;
        base = sProps[below].r1 / 16.0f;
    }
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx + s0, base + h, zc, u0 + (u1 - u0) * s0, v0, 1.0f},
        &(VoxelVertex){wx + s1, base + h, zc, u0 + (u1 - u0) * s1, v0, 1.0f},
        &(VoxelVertex){wx + s1, base,     zc, u0 + (u1 - u0) * s1, v1, 1.0f},
        &(VoxelVertex){wx + s0, base,     zc, u0 + (u1 - u0) * s0, v1, 1.0f});
}

static int32_t WoodCell(const void *secondaryTileset, int32_t metatileId)
{
    int32_t wood;

    EnsureTables();
    if (metatileId < 0x280 || metatileId >= 0x380)
        return -1;
    for (wood = 0; wood < WOODS && sWoods[wood].set != secondaryTileset; ++wood)
        ;
    return wood < WOODS ? sWoodCellIndex[wood][metatileId - 0x280] : -1;
}

bool VoxelTree_InWood(const void *secondaryTileset, int32_t metatileId)
{
    return WoodCell(secondaryTileset, metatileId) >= 0;
}

int32_t VoxelTree_Part(const void *secondaryTileset, int32_t metatileId)
{
    int32_t wood = WoodCell(secondaryTileset, metatileId);

    if (wood >= 0)
        return sWoodCells[wood].base >= 0 ? VOXEL_TREE_WOOD + sWoodCells[wood].base
             : sWoodCells[wood].gx != 0xFFFF ? VOXEL_TREE_WOOD_FLOOR : -1;
    switch (metatileId)
    {
    case 0x014: case 0x016: case 0x024: case 0x026: return VOXEL_TREE_BASE_LEFT;
    case 0x015: case 0x017: case 0x025: case 0x027: return VOXEL_TREE_BASE_RIGHT;
    default: return -1;
    }
}

static int32_t PalletFencedTree(const void *secondaryTileset, int32_t metatileId)
{
    if (secondaryTileset != &gTileset_PalletTown)
        return -1;
    switch (metatileId)
    {
    case 0x2AA: return 0x00E;
    case 0x2AB: return 0x00F;
    case 0x2B2: return 0x01E;
    case 0x2BA: return 0x01F;
    default: return -1;
    }
}

int32_t VoxelTree_Crown(const void *secondaryTileset, int32_t metatileId)
{
    int32_t as = PalletFencedTree(secondaryTileset, metatileId);
    int32_t wood = WoodCell(secondaryTileset, metatileId);

    if (wood >= 0)
        return sWoodCells[wood].crown;
    if (as >= 0)
        metatileId = as;
    switch (metatileId)
    {
    case 0x01C: case 0x01E: return 0;
    case 0x01D: case 0x01F: return 1;
    default: return -1;
    }
}

int32_t VoxelTree_GroundMetatile(const void *secondaryTileset, int32_t metatileId)
{
    int32_t prop = PropIndex(secondaryTileset, metatileId);
    int32_t as = PalletFencedTree(secondaryTileset, metatileId);
    int32_t wood = WoodCell(secondaryTileset, metatileId);

    if (wood >= 0)
        return sWoodCells[wood].ground != metatileId ? sWoodCells[wood].ground
                                                     : (int32_t)VOXEL_BARE_FIRST + metatileId;
    if (as >= 0)
        metatileId = as;

    if (prop >= 0 && sProps[prop].kind == 3)
        return metatileId;
    if (prop >= 0)
        return sProps[prop].ground != metatileId ? sProps[prop].ground
                                                 : (int32_t)VOXEL_BARE_FIRST + metatileId;
    if (secondaryTileset == &gTileset_LavenderTown && metatileId >= 0x2A5 && metatileId <= 0x2AF)
        return (int32_t)VOXEL_BARE_FIRST + metatileId;
    switch (metatileId)
    {
    case 0x01C: case 0x01E: return 0x010;
    case 0x01D: case 0x01F: return 0x011;
    case 0x00E: return 0x010;
    case 0x00F: return 0x011;
    case 0x00A: case 0x00B: case 0x00C: return 0x00D;
    case 0x0B4: case 0x0B5: return 0x07C;
    case 0x10A: case 0x10B: return 0x12B;
    case 0x255: case 0x25D: return 0x273;
    default: return metatileId;
    }
}

static void EmitCrown(VoxelBuilder *builder, int32_t x, int32_t y, int32_t col)
{
    float wx = (float)x, wz = (float)y;
    float u0 = TEX_U(col * 16), u1 = TEX_U(col * 16 + 16);
    float v0 = TEX_V(0), v1 = TEX_V(48);
    const float rise = 2.298133f, run = 1.928363f;
    const float baseHeight = -0.10f;
    float baseZ = wz + 1.9f;

    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       u0, v1, 1.0f});
    builder->rounded = false;
}

static void EmitWoodCrown(VoxelBuilder *builder, int32_t x, int32_t y, int32_t col, int32_t wood)
{
    float wx = (float)x;
    float u0 = TEX_U(sWoods[wood].x + col * 16), u1 = TEX_U(sWoods[wood].x + col * 16 + 16);
    float v0 = TEX_V(sWoods[wood].y), v1 = TEX_V(sWoods[wood].y + sWoods[wood].foot);
    float length = sWoods[wood].foot / 16.0f;
    float rise = length * 0.766044f, run = length * 0.642788f;
    const float baseHeight = -0.05f;
    float baseZ = (float)y + (sWoods[wood].foot - sWoods[wood].top) / 16.0f;

    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       u0, v1, 1.0f});
    builder->rounded = false;
}

static void EmitCell(VoxelBuilder *builder, int32_t x, int32_t y, int32_t part, int32_t crown, int32_t cell)
{
    if (part >= VOXEL_TREE_WOOD && cell >= 0)
    {
        float u = (float)sWoodCells[cell].gx, v = (float)sWoodCells[cell].gy;

        VoxelMesh_Top(builder, (float)x, (float)y, 0.0f, 0.0f,
                      TEX_U(u), TEX_V(v), TEX_U(u + 16.0f), TEX_V(v + 16.0f), 1.0f);
    }
    else if (part >= 0)
    {
        int32_t col = part - VOXEL_TREE_BASE_LEFT;

        VoxelMesh_Top(builder, (float)x, (float)y, 0.0f, 0.0f,
                      TEX_U(32 + col * 16), TEX_V(0), TEX_U(48 + col * 16), TEX_V(16), 1.0f);
    }
    if (crown >= 0 && cell >= 0)
        EmitWoodCrown(builder, x, y, crown, sWoodCells[cell].wood);
    else if (crown >= 0)
        EmitCrown(builder, x, y, crown);
}

static void EmitSeaRockWater(VoxelBuilder *builder, int32_t x, int32_t y, int32_t metatileId)
{
    if (metatileId < 0x110 || metatileId > 0x201)
        return;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(sSeaRockArcs) / sizeof(sSeaRockArcs[0])); ++i)
        if (sSeaRockArcs[i].id == (uint16_t)metatileId)
        {
            float u = (float)sSeaRockArcs[i].x, v = (float)sSeaRockArcs[i].y;

            VoxelMesh_Top(builder, (float)x, (float)y, 0.02f, 0.0f,
                          TEX_U(u), TEX_V(v), TEX_U(u + 16.0f), TEX_V(v + 16.0f), 1.0f);
            return;
        }
}

/* Standing 3D tall grass card using FireRed's GBA metatile 0x00D tuft packed into trees.rgba5551. */
static void EmitTallGrass(VoxelBuilder *builder, int32_t x, int32_t y)
{
    const float rise = 1.532089f * 0.42f;
    const float run = 1.285575f * 0.42f;
    const float baseHeight = -0.10f;
    const float wx = (float)x;
    const float wz = (float)y;
    const float baseZ = wz + 0.825f;
    const float u0 = TEX_U(VOXEL_TREE_TALL_GRASS_X);
    const float u1 = TEX_U(VOXEL_TREE_TALL_GRASS_X + 16u);
    const float v0 = TEX_V(VOXEL_TREE_TALL_GRASS_Y);
    const float v1 = TEX_V(VOXEL_TREE_TALL_GRASS_Y + 16u);

    builder->rounded = true;
    VoxelBuilder_Quad(builder,
        &(VoxelVertex){wx,        baseHeight + rise, baseZ - run, u0, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight + rise, baseZ - run, u1, v0, 1.0f},
        &(VoxelVertex){wx + 1.0f, baseHeight,        baseZ,       u1, v1, 1.0f},
        &(VoxelVertex){wx,        baseHeight,        baseZ,       u0, v1, 1.0f});
    builder->rounded = false;
}

void VoxelTree_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (inst != NULL && inst->indoor)
    {
        VoxelRoom_EmitInstance(builder, inst, x0, y0, x1, y1);
        return;
    }
    if (!VoxelWorld_UsesTreeSprites(inst))
        return;
    if (x0 < inst->originX) x0 = inst->originX;
    if (y0 < inst->originY) y0 = inst->originY;
    if (x1 > inst->originX + inst->width) x1 = inst->originX + inst->width;
    if (y1 > inst->originY + inst->height) y1 = inst->originY + inst->height;
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x)
        {
            int32_t metatileId = VoxelWorld_GetMetatileId(x, y);
            int32_t part = VoxelTree_Part(inst->secondaryTileset, metatileId);
            int32_t crown = VoxelTree_Crown(inst->secondaryTileset, metatileId);
            int32_t prop = PropIndex(inst->secondaryTileset, metatileId);

            if (prop >= 0 && !VoxelBuildings_CellAt(inst, x, y, NULL, NULL))
            {
                int32_t below = VoxelWorld_GetInstanceAt(x, y + 1) == inst
                              ? PropIndex(inst->secondaryTileset, VoxelWorld_GetMetatileId(x, y + 1)) : -1;
                int32_t north = VoxelWorld_GetInstanceAt(x, y - 1) == inst
                              ? PropIndex(inst->secondaryTileset, VoxelWorld_GetMetatileId(x, y - 1)) : -1;

                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitSeaRockWater(builder, x, y, metatileId);
                if (sProps[prop].kind == 1)
                    EmitPost(builder, x, y, prop, north, below);
                else if (sProps[prop].kind == 3)
                    EmitLedge(builder, x, y, prop);
                else if (sProps[prop].kind == 4)
                    EmitBlock(builder, x, y, prop);
                else if (sProps[prop].kind == 5)
                {
                    int32_t w = VoxelWorld_GetInstanceAt(x - 1, y) == inst
                              ? PropIndex(inst->secondaryTileset, VoxelWorld_GetMetatileId(x - 1, y)) : -1;
                    int32_t e = VoxelWorld_GetInstanceAt(x + 1, y) == inst
                              ? PropIndex(inst->secondaryTileset, VoxelWorld_GetMetatileId(x + 1, y)) : -1;
                    EmitDrawnBlock(builder, x, y, prop, w < 0 || sProps[w].kind != 5,
                                   e < 0 || sProps[e].kind != 5);
                }
                else
                {
                    bool west = VoxelWorld_GetInstanceAt(x - 1, y) == inst
                             && PropIndex(inst->secondaryTileset, VoxelWorld_GetMetatileId(x - 1, y)) >= 0;
                    bool east = VoxelWorld_GetInstanceAt(x + 1, y) == inst
                             && PropIndex(inst->secondaryTileset, VoxelWorld_GetMetatileId(x + 1, y)) >= 0;
                    EmitProp(builder, x, y, prop, below, north, west, east);
                }
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
            if (part >= 0 || crown >= 0)
            {
                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitCell(builder, x, y, part, crown, WoodCell(inst->secondaryTileset, metatileId));
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
            else if (prop < 0 && VoxelWorld_IsTallGrassCell(x, y))
            {
                builder->lift = VoxelRelief_CellLift(inst, x, y);
                builder->shift = VoxelRelief_CellShift(inst, x, y);
                EmitTallGrass(builder, x, y);
                builder->lift = 0.0f;
                builder->shift = 0.0f;
            }
        }
}

static bool WoodBorderPhase(const VoxelMapInstance *inst, int32_t *column, int32_t *row)
{
    static int32_t memoLayout = -1, memoColumn, memoRow;
    static bool memoWood;

    if (inst->layoutId != memoLayout)
    {
        int32_t columns[3] = { 0, 0, 0 }, rows[2] = { 0, 0 };

        memoLayout = inst->layoutId;
        for (int32_t y = inst->originY; y < inst->originY + inst->height; ++y)
            for (int32_t x = inst->originX; x < inst->originX + inst->width; ++x)
            {
                int32_t id = VoxelWorld_GetMetatileId(x, y);
                int32_t crown = VoxelTree_Crown(inst->secondaryTileset, id);

                if (crown >= 0 && WoodCell(inst->secondaryTileset, id) >= 0)
                {
                    ++columns[((x - crown) % 3 + 3) % 3];
                    ++rows[y & 1];
                }
            }
        memoWood = rows[0] + rows[1] > 0;
        memoColumn = columns[1] > columns[0] ? (columns[2] > columns[1] ? 2 : 1) : (columns[2] > columns[0] ? 2 : 0);
        memoRow = rows[1] > rows[0] ? 1 : 0;
    }
    *column = memoColumn;
    *row = memoRow;
    return memoWood;
}

void VoxelTree_EmitBorder(VoxelBuilder *builder, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    const VoxelMapInstance *current = VoxelWorld_Instance(0);

    if (!VoxelWorld_UsesTreeSprites(current))
        return;
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x)
        {
            int32_t metatileId, part, crown, prop, column, row;
            if (VoxelWorld_GetInstanceAt(x, y) != NULL)
                continue;
            metatileId = VoxelWorld_BorderMetatile(x, y);
            if (WoodCell(current->secondaryTileset, metatileId) >= 0
             && WoodBorderPhase(current, &column, &row))
            {
                if ((y & 1) == row)
                    EmitWoodCrown(builder, x, y, ((x - column) % 3 + 3) % 3,
                                  sWoodCells[WoodCell(current->secondaryTileset, metatileId)].wood);
                continue;
            }
            part = VoxelTree_Part(current->secondaryTileset, metatileId);
            crown = VoxelTree_Crown(current->secondaryTileset, metatileId);
            prop = PropIndex(current->secondaryTileset, metatileId);
            if (prop >= 0 && sProps[prop].kind == 1)
                EmitPost(builder, x, y, prop, prop, prop);
            else if (prop >= 0 && sProps[prop].kind == 3)
                EmitLedge(builder, x, y, prop);
            else if (prop >= 0 && sProps[prop].kind == 4)
                EmitBlock(builder, x, y, prop);
            else if (prop >= 0 && sProps[prop].kind == 5)
                EmitDrawnBlock(builder, x, y, prop, false, false);
            else if (prop >= 0)
                EmitProp(builder, x, y, prop, -1, -1, false, false);
            if (part >= 0 || crown >= 0)
                EmitCell(builder, x, y, part, crown, WoodCell(current->secondaryTileset, metatileId));
        }
}
