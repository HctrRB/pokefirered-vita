/*
 * Interior room walls, furniture, doors, and solid gates (derived from
 * Pokemon-FireRed-3DS 3ds_port/src/voxel/voxel_room.c, MIT; see NOTICE.md).
 *
 * Adapted for pokefirered-vita multicore safety: RoomRange(), DoorRange(), and
 * solid-layout lookup use stateless binary/linear scans instead of mutable
 * static caches because Core 2 (VoxelChunkWorkerMain) and Core 0
 * (VoxelEntities_Emit -> VoxelRoom_StandOn) call these helpers concurrently.
 */
#include <stddef.h>
#include <stdint.h>

#include "platform/game_frame.h"
#include "voxel_room.h"
#include "voxel_tree.h"
#include "voxel_atlas.h"
#include "voxel_room_table.h"

#define TEX_U(px) ((float)(px) / (float)VOXEL_TREE_TEXTURE_DIM)
#define TEX_V(py) (1.0f - (float)(py) / (float)VOXEL_TREE_TEXTURE_H)
#define ROOM_LAYOUTS (sizeof(sRoomLayouts) / sizeof(sRoomLayouts[0]))
#define ROOM_SOLID_LAYOUTS (sizeof(sRoomSolidLayouts) / sizeof(sRoomSolidLayouts[0]))

/* The layout's run of cells (stateless binary search; safe across Core 0 & Core 2). */
static bool RoomRange(const VoxelMapInstance *inst, uint32_t *first, uint32_t *count)
{
    uint32_t lo = 0u;
    uint32_t hi = (uint32_t)ROOM_LAYOUTS;

    if (inst == NULL || !inst->indoor || inst->layoutId < 0)
        return false;

    const uint32_t target = (uint32_t)inst->layoutId;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2u;
        const uint32_t cur = (uint32_t)sRoomLayouts[mid].layout;
        if (cur == target)
        {
            *first = (uint32_t)sRoomLayouts[mid].first;
            *count = (uint32_t)sRoomLayouts[mid].count;
            return true;
        }
        if (cur < target)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return false;
}

/* A row on a cell a script changes stands only while the cell holds what the layout does. */
static bool RowStands(uint32_t i, int32_t x, int32_t y)
{
    if (!(sRoomCells[i].flags & ROOM_LIVE))
        return true;
    if (!GameFrame_MapSettled())
        return false;
    const int32_t curId = VoxelWorld_GetMetatileId(x, y);
    for (uint32_t k = 0u; k < (uint32_t)(sizeof(sRoomLive) / sizeof(sRoomLive[0])); ++k)
    {
        if (sRoomLive[k].row == i)
            return curId == (int32_t)sRoomLive[k].id;
    }
    return true;
}

/* The cell's own piece: an overlay standing on it is not. */
static int32_t RoomCell(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    uint32_t first = 0u, count = 0u;
    const int32_t lx = x - inst->originX;
    const int32_t ly = y - inst->originY;

    if (!RoomRange(inst, &first, &count) || lx < 0 || ly < 0)
        return -1;
    for (uint32_t i = first; i < first + count; ++i)
    {
        if ((int32_t)sRoomCells[i].y > ly)
            break;
        if ((int32_t)sRoomCells[i].y == ly
         && (int32_t)sRoomCells[i].x == lx
         && !(sRoomCells[i].flags & ROOM_OVERLAY)
         && RowStands(i, x, y))
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/* Stateless run of sRoomDoors[] matching inst->secondaryTileset (Core 0 & Core 2 safe). */
static bool DoorRange(const VoxelMapInstance *inst, uint32_t *first, uint32_t *end)
{
    enum { DOORS = (int32_t)(sizeof(sRoomDoors) / sizeof(sRoomDoors[0])) };
    bool found = false;
    uint32_t outFirst = 0u, outEnd = 0u;

    if (inst == NULL || inst->secondaryTileset == NULL)
        return false;

    for (uint32_t d = 0u; d < (uint32_t)DOORS; ++d)
    {
        if ((const void *)sRoomDoors[d].set == inst->secondaryTileset)
        {
            if (!found)
            {
                outFirst = d;
                found = true;
            }
            outEnd = d + 1u;
        }
        else if (found)
        {
            break;
        }
    }
    *first = outFirst;
    *end = outEnd;
    return found;
}

static int32_t DoorAt(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    uint32_t first = 0u, end = 0u;
    int32_t id;

    if (!DoorRange(inst, &first, &end))
        return -1;
    id = VoxelWorld_GetMetatileId(x, y);
    if (id < 0)
        return -1;
    for (uint32_t d = first; d < end; ++d)
    {
        if ((int32_t)sRoomDoors[d].id == id)
            return (int32_t)d;
    }
    return -1;
}

int32_t VoxelRoom_Ground(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    const int32_t i = RoomCell(inst, x, y);

    if (i < 0)
    {
        const int32_t d = DoorAt(inst, x, y);
        return d < 0 ? VOXEL_ROOM_NONE : (int32_t)sRoomDoors[d].floor;
    }
    if (sRoomCells[i].flags & ROOM_NO_GROUND)
        return VOXEL_ROOM_NO_GROUND;
    if (sRoomCells[i].flags & ROOM_BARE)
        return (int32_t)VOXEL_BARE_FIRST + (int32_t)sRoomCells[i].ground;
    return (int32_t)sRoomCells[i].ground;
}

bool VoxelRoom_StandOn(const VoxelMapInstance *inst, int32_t x, int32_t y, float *rise, float *offZ)
{
    const int32_t i = RoomCell(inst, x, y);
    int32_t top, back;
    float standOffZ;

    /* Walls and nothing-there; and only a piece that stops low (tables/counters). */
    if (i < 0
     || (sRoomCells[i].flags & (ROOM_NO_GROUND | ROOM_FLAT | ROOM_NO_STAND)) != 0
     || sRoomCells[i].h == 0u
     || (uint32_t)sRoomCells[i].h + (uint32_t)sRoomCells[i].lift > 16u)
    {
        return false;
    }

    top = (int32_t)sRoomCells[i].h + (int32_t)sRoomCells[i].lift;
    if (sRoomCells[i].flags & ROOM_MODEL)
    {
        back = (int32_t)sRoomCells[i].ybot + (int32_t)sRoomCells[i].lift - (int32_t)sRoomCells[i].depth;
        standOffZ = ((float)back + (float)sRoomCells[i].depth * 0.5f) / 16.0f
                  - ((float)sRoomCells[i].y + 0.5f);
    }
    else
    {
        const int32_t split = (int32_t)sRoomCells[i].ybot - (int32_t)sRoomCells[i].h;
        const int32_t first = (int32_t)sRoomCells[i].y * 16;
        int32_t rows = split - first;

        if (rows < 0)
            rows = 0;
        if (rows > 16)
            rows = 16;
        standOffZ = ((float)first + (float)rows * 0.5f + (float)top) / 16.0f
                  - ((float)sRoomCells[i].y + 0.5f);
    }
    standOffZ -= (float)sRoomCells[i].back / 16.0f;
    if (offZ != NULL)
        *offZ = standOffZ;
    if (rise != NULL)
        *rise = (float)top / 16.0f;
    return true;
}

static void SideQuad(VoxelBuilder *b, float x, float y0, float y1, float za, float zb,
                     float u0, float v0, float u1, float v1, bool east)
{
    if (east)
    {
        VoxelBuilder_Quad(b,
            &(VoxelVertex){x, y1, zb, u1, v0, 0.8f}, &(VoxelVertex){x, y1, za, u0, v0, 0.8f},
            &(VoxelVertex){x, y0, za, u0, v1, 0.8f}, &(VoxelVertex){x, y0, zb, u1, v1, 0.8f});
    }
    else
    {
        VoxelBuilder_Quad(b,
            &(VoxelVertex){x, y1, za, u0, v0, 0.8f}, &(VoxelVertex){x, y1, zb, u1, v0, 0.8f},
            &(VoxelVertex){x, y0, zb, u1, v1, 0.8f}, &(VoxelVertex){x, y0, za, u0, v1, 0.8f});
    }
}

static void EmitModel(VoxelBuilder *b, const VoxelMapInstance *inst, uint32_t i,
                      float u0, float v0, float u1, float v1)
{
    const float x0 = (float)(inst->originX + (int32_t)sRoomCells[i].x), x1 = x0 + 1.0f;
    const float oz = (float)inst->originY - (float)sRoomCells[i].back / 16.0f;
    const int32_t ly = (int32_t)sRoomCells[i].y;
    const int32_t top = (int32_t)sRoomCells[i].top;
    const int32_t ybot = (int32_t)sRoomCells[i].ybot;
    const float lift = (float)sRoomCells[i].lift / 16.0f;
    const float h = (float)sRoomCells[i].h / 16.0f;
    const float depth = (float)sRoomCells[i].depth / 16.0f;
    const float zf = oz + (float)(ybot + (int32_t)sRoomCells[i].lift) / 16.0f;
    const int32_t span = ybot - top;
    const int32_t p0 = ly * 16 > top ? ly * 16 : top;
    const int32_t p1 = ly * 16 + 16 < ybot ? ly * 16 + 16 : ybot;
#define ROW_V(r) (v0 + (v1 - v0) * (float)(r) / 16.0f)

    if (span <= 0)
        return;
    if (p1 > p0)
    {
        const float ya = lift + h * (float)(ybot - p0) / (float)span;
        const float yb = lift + h * (float)(ybot - p1) / (float)span;
        const float va = ROW_V(p0 - ly * 16);
        const float vb = ROW_V(p1 - ly * 16);

        VoxelBuilder_Quad(b,
            &(VoxelVertex){x0, ya, zf, u0, va, 1.0f}, &(VoxelVertex){x1, ya, zf, u1, va, 1.0f},
            &(VoxelVertex){x1, yb, zf, u1, vb, 1.0f}, &(VoxelVertex){x0, yb, zf, u0, vb, 1.0f});
    }
    if (ly == top / 16)
    {
        const int32_t r = top - ly * 16;
        const int32_t r1 = r + 3 < 16 ? r + 3 : 16;
        const float yt = lift + h;
        const float za = zf - depth;
        const float yw = lift + h * (float)(ybot - (int32_t)sRoomCells[i].tw) / (float)span;
        const float ye = lift + h * (float)(ybot - (int32_t)sRoomCells[i].te) / (float)span;
        const uint32_t flags = sRoomCells[i].flags;

        if (sRoomCells[i].tsx != 0u || sRoomCells[i].tsy != 0u)
        {
            const float tu0 = TEX_U(sRoomCells[i].tsx), tu1 = TEX_U(sRoomCells[i].tsx + 16u);
            const float tv0 = TEX_V(sRoomCells[i].tsy), tv1 = TEX_V(sRoomCells[i].tsy + 16u);

            VoxelBuilder_Quad(b,
                &(VoxelVertex){x0, yt, za, tu0, tv0, 1.0f}, &(VoxelVertex){x1, yt, za, tu1, tv0, 1.0f},
                &(VoxelVertex){x1, yt, zf, tu1, tv1, 1.0f}, &(VoxelVertex){x0, yt, zf, tu0, tv1, 1.0f});
        }
        else
        {
            VoxelBuilder_Quad(b,
                &(VoxelVertex){x0, yt, za, u0, ROW_V(r), 1.0f}, &(VoxelVertex){x1, yt, za, u1, ROW_V(r), 1.0f},
                &(VoxelVertex){x1, yt, zf, u1, ROW_V(r1), 1.0f}, &(VoxelVertex){x0, yt, zf, u0, ROW_V(r1), 1.0f});
        }
        if (sRoomCells[i].ssx != 0u || sRoomCells[i].ssy != 0u)
        {
            const float su0 = TEX_U(sRoomCells[i].ssx), su1 = TEX_U(sRoomCells[i].ssx + 16u);
            const float sv0 = TEX_V(sRoomCells[i].ssy), sv1 = TEX_V(sRoomCells[i].ssy + 16u);

            if (flags & ROOM_WEST)
                SideQuad(b, x0 + (float)sRoomCells[i].inw / 16.0f, lift, yw > yt ? yt : yw, za, zf,
                         su0, sv0, su1, sv1, false);
            if (flags & ROOM_EAST)
                SideQuad(b, x1 - (float)sRoomCells[i].ine / 16.0f, lift, ye > yt ? yt : ye, za, zf,
                         su0, sv0, su1, sv1, true);
        }
    }
#undef ROW_V
}

static void EmitCell(VoxelBuilder *b, const VoxelMapInstance *inst, uint32_t i,
                     float u0, float v0, float u1, float v1)
{
    const float x0 = (float)(inst->originX + (int32_t)sRoomCells[i].x), x1 = x0 + 1.0f;
    const int32_t ly = (int32_t)sRoomCells[i].y;
    const int32_t h = (int32_t)sRoomCells[i].h;
    const int32_t ybot = (int32_t)sRoomCells[i].ybot;
    const int32_t lift = (int32_t)sRoomCells[i].lift;
    const float oz = (float)inst->originY - (float)sRoomCells[i].back / 16.0f;
    const int32_t split = ybot - h;
    int32_t ps = split - ly * 16;
    int32_t pe = ybot - ly * 16;
#define ROW_V(r) (v0 + (v1 - v0) * (float)(r) / 16.0f)
#define COL_U(c) (u0 + (u1 - u0) * (float)(c) / 16.0f)

    if (sRoomCells[i].flags & ROOM_MODEL)
    {
        EmitModel(b, inst, i, u0, v0, u1, v1);
        return;
    }
    if (sRoomCells[i].flags & ROOM_HANG)
    {
        const float z = oz + (float)ybot / 16.0f;
        const float ya = (float)(ybot - ly * 16) / 16.0f;
        const float yb = ya - 1.0f;

        VoxelBuilder_Quad(b,
            &(VoxelVertex){x0, ya, z, u0, v0, 1.0f}, &(VoxelVertex){x1, ya, z, u1, v0, 1.0f},
            &(VoxelVertex){x1, yb, z, u1, v1, 1.0f}, &(VoxelVertex){x0, yb, z, u0, v1, 1.0f});
        return;
    }
    if (sRoomCells[i].flags & ROOM_SLANT)
    {
        const int32_t te = (int32_t)sRoomCells[i].top;
        const float zw = oz + (float)ybot / 16.0f, ze = oz + (float)te / 16.0f;
        const float yw = (float)(ybot - ly * 16) / 16.0f, ye = (float)(te - ly * 16) / 16.0f;

        VoxelBuilder_Quad(b,
            &(VoxelVertex){x0, yw, zw, u0, v0, 1.0f}, &(VoxelVertex){x1, ye, ze, u1, v0, 1.0f},
            &(VoxelVertex){x1, ye - 1.0f, ze, u1, v1, 1.0f}, &(VoxelVertex){x0, yw - 1.0f, zw, u0, v1, 1.0f});
        return;
    }
    if (ps < 0) ps = 0;
    if (ps > 16) ps = 16;
    if (pe > 16) pe = 16;
    if (ps > 0)
    {
        const float ht = (float)(h + lift) / 16.0f;
        float za = oz + (float)(ly * 16 + h + lift) / 16.0f;
        const float zb = oz + (float)(ly * 16 + ps + h + lift) / 16.0f;
        const float va = ROW_V(0), vb = ROW_V(ps);

        VoxelBuilder_Quad(b,
            &(VoxelVertex){x0, ht, za, u0, va, 1.0f}, &(VoxelVertex){x1, ht, za, u1, va, 1.0f},
            &(VoxelVertex){x1, ht, zb, u1, vb, 1.0f}, &(VoxelVertex){x0, ht, zb, u0, vb, 1.0f});
        if (sRoomCells[i].depth != 0u)
        {
            const float zd = za + (float)((int32_t)sRoomCells[i].cap - (int32_t)sRoomCells[i].depth) / 16.0f;
            const float vcap = ROW_V(sRoomCells[i].cap);
            const float v1r = ROW_V(sRoomCells[i].cap + 1u);

            za = zd;
            VoxelBuilder_Quad(b,
                &(VoxelVertex){x0, ht, zd, u0, vcap, 1.0f}, &(VoxelVertex){x1, ht, zd, u1, vcap, 1.0f},
                &(VoxelVertex){x1, ht, zd + (float)sRoomCells[i].depth / 16.0f, u1, v1r, 1.0f},
                &(VoxelVertex){x0, ht, zd + (float)sRoomCells[i].depth / 16.0f, u0, v1r, 1.0f});
            VoxelBuilder_Quad(b,
                &(VoxelVertex){x1, ht, zd, u1, vcap, 0.7f}, &(VoxelVertex){x0, ht, zd, u0, vcap, 0.7f},
                &(VoxelVertex){x0, (float)lift / 16.0f, zd, u0, v1r, 0.7f},
                &(VoxelVertex){x1, (float)lift / 16.0f, zd, u1, v1r, 0.7f});
        }
        if (sRoomCells[i].flags & ROOM_ATLAS)
        {
            if (zb - za >= 4.0f / 16.0f)
            {
                const float vf0 = ROW_V(ps < 16 ? ps : 15), vf1 = ROW_V(16);

                if (sRoomCells[i].flags & ROOM_WEST)
                    SideQuad(b, x0, 0.0f, ht, za, zb, COL_U(1.5f), vf0, COL_U(1.5f), vf1, false);
                if (sRoomCells[i].flags & ROOM_EAST)
                    SideQuad(b, x1, 0.0f, ht, za, zb, COL_U(14.5f), vf0, COL_U(14.5f), vf1, true);
            }
        }
        else if (sRoomCells[i].ssx != 0u || sRoomCells[i].ssy != 0u)
        {
            const float su0 = TEX_U(sRoomCells[i].ssx), su1 = TEX_U(sRoomCells[i].ssx + 16u);
            const float sv0 = TEX_V(sRoomCells[i].ssy), sv1 = TEX_V(sRoomCells[i].ssy + 16u);
            float zw = oz + (float)((int32_t)sRoomCells[i].tw + h + lift) / 16.0f;
            float ze = oz + (float)((int32_t)sRoomCells[i].te + h + lift) / 16.0f;

            if (zw < za) zw = za;
            if (ze < za) ze = za;
            if ((sRoomCells[i].flags & ROOM_WEST) && zw < zb)
                SideQuad(b, x0 + (float)sRoomCells[i].inw / 16.0f, (float)lift / 16.0f, ht, zw, zb,
                         su0, sv0, su1, sv1, false);
            if ((sRoomCells[i].flags & ROOM_EAST) && ze < zb)
                SideQuad(b, x1 - (float)sRoomCells[i].ine / 16.0f, (float)lift / 16.0f, ht, ze, zb,
                         su0, sv0, su1, sv1, true);
        }
    }
    if (ps < pe)
    {
        const float z = oz + (float)(ybot + lift) / 16.0f;
        const float ya = (float)(ybot - (ly * 16 + ps) + lift) / 16.0f;
        const float yb = (float)(ybot - (ly * 16 + pe) + lift) / 16.0f;
        const float va = ROW_V(ps), vb = ROW_V(pe);

        VoxelBuilder_Quad(b,
            &(VoxelVertex){x0, ya, z, u0, va, 1.0f}, &(VoxelVertex){x1, ya, z, u1, va, 1.0f},
            &(VoxelVertex){x1, yb, z, u1, vb, 1.0f}, &(VoxelVertex){x0, yb, z, u0, vb, 1.0f});
    }
#undef ROW_V
#undef COL_U
}

int32_t VoxelRoom_AtlasArt(const VoxelMapInstance *inst, int32_t x, int32_t y)
{
    const int32_t i = RoomCell(inst, x, y);

    return (i >= 0 && (sRoomCells[i].flags & ROOM_ATLAS)) ? (int32_t)sRoomCells[i].sx : -1;
}

void VoxelRoom_EmitAtlasCell(VoxelBuilder *builder, const VoxelMapInstance *inst, int32_t x, int32_t y,
                             float u0, float v0, float u1, float v1)
{
    const int32_t i = RoomCell(inst, x, y);

    if (i >= 0 && (sRoomCells[i].flags & ROOM_ATLAS))
        EmitCell(builder, inst, (uint32_t)i, u0, v0, u1, v1);
}

#define DOOR_LID_H 1.5f
#define DOOR_DEPTH (13.0f / 16.0f)
#define DOOR_BAND  5u

static int32_t DoorOfId(uint32_t first, uint32_t end, uint32_t id)
{
    for (uint32_t d = first; d < end; ++d)
    {
        if (sRoomDoors[d].id == id)
            return (int32_t)d;
    }
    return -1;
}

static void EmitGateSide(VoxelBuilder *b, const VoxelMapInstance *inst, float x, float za, float zb,
                         float h, bool east, float share)
{
    uint32_t first = 0u, end = 0u;
    int32_t upper, foot;

    if (!DoorRange(inst, &first, &end))
        return;
    upper = DoorOfId(first, end, 0x3B0u);
    foot = DoorOfId(first, end, 0x3B8u);
    if (upper < 0 || foot < 0)
        return;

    const float ua = TEX_U(sRoomDoors[upper].sx);
    const float ub = TEX_U((float)sRoomDoors[upper].sx + 16.0f * share);
    const float uc = TEX_U(sRoomDoors[foot].sx);
    const float ud = TEX_U((float)sRoomDoors[foot].sx + 16.0f * share);

    SideQuad(b, x, 1.0f, h, za, zb, ua, TEX_V(sRoomDoors[upper].sy + DOOR_BAND),
             ub, TEX_V(sRoomDoors[upper].sy + 16u), east);
    SideQuad(b, x, 0.0f, 1.0f, za, zb, uc, TEX_V(sRoomDoors[foot].sy),
             ud, TEX_V(sRoomDoors[foot].sy + 16u), east);
}

static void EmitDoor(VoxelBuilder *b, const VoxelMapInstance *inst, int32_t x, int32_t y, int32_t d)
{
    const float wx = (float)x;
    const uint32_t sx = sRoomDoors[d].sx, sy = sRoomDoors[d].sy, rows = sRoomDoors[d].rows;
    const float u0 = TEX_U(sx), u1 = TEX_U(sx + 16u);
    const float v0 = TEX_V(sy), v1 = TEX_V(sy + rows);

    if (sRoomDoors[d].kind < 2u)
    {
        const bool upper = sRoomDoors[d].kind == 0u;
        const float z = (float)y + (upper ? 2.0f : 1.0f);
        const float ya = upper ? 2.0f : 1.0f;

        VoxelBuilder_Quad(b,
            &(VoxelVertex){wx, ya, z, u0, v0, 1.0f}, &(VoxelVertex){wx + 1.0f, ya, z, u1, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, ya - 1.0f, z, u1, v1, 1.0f}, &(VoxelVertex){wx, ya - 1.0f, z, u0, v1, 1.0f});
        if (sRoomDoors[d].set == &gTileset_SilphCo)
        {
            const float zb = z - DOOR_DEPTH;

            VoxelBuilder_Quad(b,
                &(VoxelVertex){wx + 1.0f, ya, zb, u1, v0, 0.7f}, &(VoxelVertex){wx, ya, zb, u0, v0, 0.7f},
                &(VoxelVertex){wx, ya - 1.0f, zb, u0, v1, 0.7f}, &(VoxelVertex){wx + 1.0f, ya - 1.0f, zb, u1, v1, 0.7f});
            if (upper)
            {
                const float ht = (float)ROOM_SOLID_H / 16.0f;
                const float vt = TEX_V(sy + DOOR_BAND), vb = TEX_V(sy + DOOR_BAND + 1u);

                VoxelBuilder_Quad(b,
                    &(VoxelVertex){wx, ht, zb, u0, vt, 1.0f}, &(VoxelVertex){wx + 1.0f, ht, zb, u1, vt, 1.0f},
                    &(VoxelVertex){wx + 1.0f, ht, z, u1, vb, 1.0f}, &(VoxelVertex){wx, ht, z, u0, vb, 1.0f});
            }
        }
    }
    else
    {
        const uint32_t c0 = sRoomDoors[d].c0, c1 = sRoomDoors[d].c1;
        const bool solid = sRoomDoors[d].set == &gTileset_SilphCo;
        const float lh = solid ? (float)ROOM_SOLID_H / 16.0f : DOOR_LID_H;
        float za = (float)y + lh;
        const float zb = za + (float)rows / 16.0f;
        const float xa = wx + (float)c0 / 16.0f, xb = wx + (float)(c1 + 1u) / 16.0f;
        const float su = TEX_U((float)(sx + (c0 + c1) / 2u) + 0.5f);

        if (solid && (sRoomDoors[d].id & ~1u) == 0x3C0u)
            za = (float)y + 1.0f;
        VoxelBuilder_Quad(b,
            &(VoxelVertex){wx, lh, za, u0, v0, 1.0f}, &(VoxelVertex){wx + 1.0f, lh, za, u1, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, lh, zb, u1, v1, 1.0f}, &(VoxelVertex){wx, lh, zb, u0, v1, 1.0f});
        if (c0 > 0u)
        {
            if (solid)
                EmitGateSide(b, inst, xa, za, zb, lh, false, (float)rows / 16.0f);
            else
                SideQuad(b, xa, 0.0f, lh, za, zb, su, v0, su, v1, false);
        }
        if (c1 < 15u)
        {
            if (solid)
                EmitGateSide(b, inst, xb, za, zb, lh, true, (float)rows / 16.0f);
            else
                SideQuad(b, xb, 0.0f, lh, za, zb, su, v0, su, v1, true);
        }
        if (sRoomDoors[d].kind == 3u)
        {
            VoxelBuilder_Quad(b,
                &(VoxelVertex){xa, lh, zb, su, v1, 0.85f}, &(VoxelVertex){xb, lh, zb, su, v1, 0.85f},
                &(VoxelVertex){xb, 0.0f, zb, su, v1, 0.85f}, &(VoxelVertex){xa, 0.0f, zb, su, v1, 0.85f});
        }
    }
}

static void EmitSolid(VoxelBuilder *b, const VoxelMapInstance *inst, uint32_t i, uint32_t height)
{
    const float ox = (float)inst->originX, oz = (float)inst->originY;
    const float h = (float)height / 16.0f;
    const uint32_t sx = sRoomSolids[i].sx, sy = sRoomSolids[i].sy;
    const float wx = ox + (float)sRoomSolids[i].x;
    const float a = (float)sRoomSolids[i].a / 16.0f;
    const float e = (float)sRoomSolids[i].b / 16.0f;
    const float z = oz + (float)sRoomSolids[i].z / 16.0f;
    const float u0 = TEX_U(sx), u1 = TEX_U(sx + 16u), v0 = TEX_V(sy), v1 = TEX_V(sy + 16u);

    switch (sRoomSolids[i].kind)
    {
    case SOLID_LID:
    {
        const float za = oz + (float)sRoomSolids[i].y + h, zb = za + 1.0f;

        VoxelBuilder_Quad(b,
            &(VoxelVertex){wx, h, za, u0, v0, 1.0f}, &(VoxelVertex){wx + 1.0f, h, za, u1, v0, 1.0f},
            &(VoxelVertex){wx + 1.0f, h, zb, u1, v1, 1.0f}, &(VoxelVertex){wx, h, zb, u0, v1, 1.0f});
        break;
    }
    case SOLID_FRONT:
    case SOLID_BEHIND:
    {
        const uint32_t c0 = sRoomSolids[i].c0, c1 = (uint32_t)sRoomSolids[i].c1 + 1u;
        const uint32_t r0 = sRoomSolids[i].r0, r1 = sRoomSolids[i].r1;
        const int32_t top = (int32_t)sRoomSolids[i].y * 16;
        const float xa = wx + (float)c0 / 16.0f, xb = wx + (float)c1 / 16.0f;
        const float zf = oz + a - (sRoomSolids[i].kind == SOLID_BEHIND ? 2.0f / 16.0f : 0.0f);
        const float ya = (float)((int32_t)sRoomSolids[i].a - (top + (int32_t)r0)) / 16.0f;
        const float yb = (float)((int32_t)sRoomSolids[i].a - (top + (int32_t)r1)) / 16.0f;
        const float ua = TEX_U(sx + c0), ub = TEX_U(sx + c1), va = TEX_V(sy + r0), vb = TEX_V(sy + r1);

        VoxelBuilder_Quad(b,
            &(VoxelVertex){xa, ya, zf, ua, va, 1.0f}, &(VoxelVertex){xb, ya, zf, ub, va, 1.0f},
            &(VoxelVertex){xb, yb, zf, ub, vb, 1.0f}, &(VoxelVertex){xa, yb, zf, ua, vb, 1.0f});
        break;
    }
    case SOLID_WEST:
    case SOLID_EAST:
        SideQuad(b, wx + (float)sRoomSolids[i].c0 / 16.0f, 0.0f, h, oz + a, oz + e, u0, v0, u1, v1,
                 sRoomSolids[i].kind == SOLID_EAST);
        break;
    case SOLID_NORTH:
        VoxelBuilder_Quad(b,
            &(VoxelVertex){ox + e, h, z, u0, v0, 0.7f}, &(VoxelVertex){ox + a, h, z, u1, v0, 0.7f},
            &(VoxelVertex){ox + a, 0.0f, z, u1, v1, 0.7f}, &(VoxelVertex){ox + e, 0.0f, z, u0, v1, 0.7f});
        break;
    default:
        VoxelBuilder_Quad(b,
            &(VoxelVertex){ox + a, h, z, u0, v0, 0.9f}, &(VoxelVertex){ox + e, h, z, u1, v0, 0.9f},
            &(VoxelVertex){ox + e, 0.0f, z, u1, v1, 0.9f}, &(VoxelVertex){ox + a, 0.0f, z, u0, v1, 0.9f});
        break;
    }
}

void VoxelRoom_EmitInstance(VoxelBuilder *builder, const VoxelMapInstance *inst,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t first = 0u, count = 0u;
    uint32_t doorFirst = 0u, doorEnd = 0u;

    if (DoorRange(inst, &doorFirst, &doorEnd))
    {
        for (int32_t y = y0; y < y1; ++y)
            for (int32_t x = x0; x < x1; ++x)
            {
                const int32_t d = DoorAt(inst, x, y);
                if (d >= 0)
                    EmitDoor(builder, inst, x, y, d);
            }
    }
    if (inst != NULL && inst->indoor && inst->layoutId >= 0)
    {
        uint32_t lo = 0u, hi = (uint32_t)ROOM_SOLID_LAYOUTS;
        const uint32_t target = (uint32_t)inst->layoutId;
        while (lo < hi)
        {
            const uint32_t mid = lo + (hi - lo) / 2u;
            const uint32_t cur = (uint32_t)sRoomSolidLayouts[mid].layout;
            if (cur == target)
            {
                const uint32_t sFirst = sRoomSolidLayouts[mid].first;
                const uint32_t sEnd = sFirst + sRoomSolidLayouts[mid].count;
                for (uint32_t i = sFirst; i < sEnd; ++i)
                {
                    const int32_t x = inst->originX + (int32_t)sRoomSolids[i].x;
                    const int32_t y = inst->originY + (int32_t)sRoomSolids[i].y;

                    if (x >= x0 && x < x1 && y >= y0 && y < y1
                     && (sRoomSolids[i].live == 0u || VoxelWorld_GetMetatileId(x, y) == (int32_t)sRoomSolids[i].live))
                    {
                        EmitSolid(builder, inst, i, sRoomSolidLayouts[mid].h);
                    }
                }
                break;
            }
            if (cur < target)
                lo = mid + 1u;
            else
                hi = mid;
        }
    }
    if (!RoomRange(inst, &first, &count))
        return;
    for (uint32_t i = first; i < first + count; ++i)
    {
        const int32_t x = inst->originX + (int32_t)sRoomCells[i].x;
        const int32_t y = inst->originY + (int32_t)sRoomCells[i].y;

        if (sRoomCells[i].flags & (ROOM_ATLAS | ROOM_FLAT))
            continue;
        if (x >= x0 && x < x1 && y >= y0 && y < y1 && RowStands(i, x, y))
            EmitCell(builder, inst, i, TEX_U(sRoomCells[i].sx), TEX_V(sRoomCells[i].sy),
                     TEX_U(sRoomCells[i].sx + 16u), TEX_V(sRoomCells[i].sy + 16u));
    }
}
