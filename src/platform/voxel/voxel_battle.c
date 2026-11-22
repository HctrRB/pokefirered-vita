/*
 * The 3D battle stage placement search (Core 0) and battle state/shadow capture
 * (Core 1). See voxel_battle.h and NOTICE.md.
 *
 * Origin: derived from pokeemerald-vita-3ds / Pokemon-FireRed-3DS (MIT);
 * see NOTICE.md.
 */

/* Before global.h, which redefines abs() as a macro over stdlib's prototype. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "constants/battle.h"
#include "constants/moves.h"
#include "constants/pokemon.h"
#include "data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "main.h"
#include "metatile_behavior.h"
#include "sprite.h"

#include "platform/game_frame.h"
#include "platform/widescreen.h"
#include "voxel_battle.h"
#include "voxel_building.h"
#include "voxel_relief.h"
#include "voxel_tree.h"
#include "voxel_world.h"

#define DEG_TO_RAD       (3.14159265358979323846f / 180.0f)
#ifdef WIDESCREEN_SUPPORTED
#define VITA_GAME_WIDTH  WIDESCREEN_SCREEN_W
#define VITA_GAME_HEIGHT WIDESCREEN_SCREEN_H
#define VITA_BATTLE_ZOOM WIDESCREEN_SCALE
#else
#define VITA_GAME_WIDTH  ((float)DISPLAY_WIDTH)
#define VITA_GAME_HEIGHT ((float)DISPLAY_HEIGHT)
#define VITA_BATTLE_ZOOM 1.0f
#endif

/* Core 1 battle UI flags */
extern bool8 CtrBattleBg_MoveBgShown(void);
extern bool8 CtrBattleIntro_Sliding(void);

bool VoxelBattle_GameInBattle(void)
{
    return GameFrame_Get()->battle.inBattle;
}

bool VoxelBattle_IsLink(void)
{
    return GameFrame_Get()->battle.isLink;
}

/* ── Core 0: battle camera & stage search ────────────────────────────────── */

typedef struct
{
    float eyeY, eyeZ, sinP, cosP, tanX, tanY;
} BattleView;

static void ViewInit(BattleView *v)
{
    const float p = VOXEL_BATTLE_PITCH * DEG_TO_RAD;

    v->sinP = sinf(p);
    v->cosP = cosf(p);
    /* Match VoxelCamera_Frame / Place(): horizontal distance along +Z, height = distance * tan(pitch). */
    v->eyeY = VOXEL_BATTLE_DISTANCE * tanf(p);
    v->eyeZ = VOXEL_BATTLE_DISTANCE;
    v->tanY = tanf(VOXEL_BATTLE_FOV * 0.5f * DEG_TO_RAD);
    v->tanX = v->tanY * VITA_GAME_WIDTH / VITA_GAME_HEIGHT;
}

static float GbaToScreenX(float x)
{
    return VITA_GAME_WIDTH * 0.5f + (x - (float)DISPLAY_WIDTH * 0.5f) * VITA_BATTLE_ZOOM;
}

static float GbaToScreenY(float y)
{
    return y * VITA_BATTLE_ZOOM;
}

static void ScreenToGround(const BattleView *v, float sx, float sy, float *x, float *z)
{
    const float nx = (sx - VITA_GAME_WIDTH * 0.5f) / (VITA_GAME_WIDTH * 0.5f);
    const float ny = (VITA_GAME_HEIGHT * 0.5f - sy) / (VITA_GAME_HEIGHT * 0.5f);
    const float dx = nx * v->tanX;
    const float dy = -v->sinP + ny * v->tanY * v->cosP;
    const float dz = -v->cosP - ny * v->tanY * v->sinP;
    const float t = (dy < -0.01f) ? (v->eyeY / -dy) : 100.0f;

    *x = dx * t;
    *z = v->eyeZ + dz * t;
}

static bool Project(const BattleView *v, float x, float h, float z, float *sx, float *sy)
{
    const float ry = h - v->eyeY;
    const float rz = z - v->eyeZ;
    const float depth = -ry * v->sinP - rz * v->cosP;
    const float up = ry * v->cosP - rz * v->sinP;

    if (depth < 0.1f)
        return false;
    *sx = VITA_GAME_WIDTH * 0.5f + x / (depth * v->tanX) * (VITA_GAME_WIDTH * 0.5f);
    *sy = VITA_GAME_HEIGHT * 0.5f - up / (depth * v->tanY) * (VITA_GAME_HEIGHT * 0.5f);
    return true;
}

enum
{
    GROUND_PLAIN,
    GROUND_GRASS,
    GROUND_SAND,
    GROUND_WATER,
    GROUND_VOID,
};

typedef struct
{
    uint8_t kind;
    bool blocked, slope;
    float top, lift;
} StageCell;

#define STAGE_REACH 6
#define GRID_X0     (-STAGE_REACH - 11)
#define GRID_X1     (STAGE_REACH + 11)
#define GRID_Z0     (-STAGE_REACH - 10)
#define GRID_Z1     (STAGE_REACH + 8)
#define GRID_W      (GRID_X1 - GRID_X0 + 1)
#define GRID_H      (GRID_Z1 - GRID_Z0 + 1)
#define STAGE_SIDE  (2 * STAGE_REACH + 1)

static StageCell sGrid[GRID_H][GRID_W];
static int32_t sGridX, sGridZ;

static uint8_t KindOf(uint32_t behavior)
{
    if (MetatileBehavior_IsTallGrass((u8)behavior) || MetatileBehavior_IsLongGrass((u8)behavior))
        return GROUND_GRASS;
    if (MetatileBehavior_IsSand((u8)behavior) || MetatileBehavior_IsDeepSand((u8)behavior))
        return GROUND_SAND;
    if (MetatileBehavior_IsSurfable((u8)behavior) || MetatileBehavior_IsDeepWaterTerrain((u8)behavior))
        return GROUND_WATER;
    return GROUND_PLAIN;
}

static void ReadCell(StageCell *cell, int32_t x, int32_t z, bool indoor)
{
    const VoxelMapInstance *const inst = VoxelWorld_GetInstanceAt(x, z);
    float top = 0.0f;

    memset(cell, 0, sizeof(*cell));
    cell->lift = VoxelRelief_LiftAt((float)x + 0.5f, (float)z + 0.5f);
    if (inst == NULL)
    {
        cell->kind = indoor ? GROUND_VOID : GROUND_PLAIN;
        cell->top = indoor ? 0.0f : 2.0f;
        cell->blocked = true;
        return;
    }
    const VoxelVisualShape shape = VoxelWorld_ClassifyTile(x, z);
    if (shape == VOXEL_SHAPE_VOID)
    {
        cell->kind = GROUND_VOID;
        cell->blocked = true;
        return;
    }
    cell->kind = KindOf(VoxelWorld_GetMetatileBehavior(x, z));
    cell->blocked = VoxelWorld_GetCollision(x, z) != 0u;
    const int32_t metatile = VoxelWorld_GetMetatileId(x, z);
    if (VoxelBuildings_CellAt(inst, x, z, NULL, &top) && top > cell->top)
        cell->top = top;
    if (VoxelWorld_UsesTreeSprites(inst))
    {
        const int32_t part = VoxelTree_Part(inst->secondaryTileset, metatile);
        float treeTop = 0.0f;

        if (part >= 0)
            treeTop = (part == VOXEL_TREE_SMALL) ? 1.8f : 2.4f;
        else if (VoxelTree_GroundMetatile(inst->secondaryTileset, metatile) != metatile)
            treeTop = 2.4f;
        if (treeTop > cell->top)
            cell->top = treeTop;
    }
    switch (shape)
    {
    case VOXEL_SHAPE_SIGN:      top = 1.0f; break;
    case VOXEL_SHAPE_FURNITURE:
    case VOXEL_SHAPE_TABLE:
    case VOXEL_SHAPE_COUNTER:
    case VOXEL_SHAPE_BED:       top = 0.8f; break;
    default:                    top = 0.0f; break;
    }
    if (top > cell->top)
        cell->top = top;
    const int16_t *const relief = VoxelRelief_Cell(inst, x, z);
    cell->slope = relief != NULL && VoxelRelief_IsSlope(relief);
}

static const StageCell *CellAtCell(int32_t x, int32_t z)
{
    static const StageCell sPlain = { GROUND_PLAIN, false, false, 0.0f, 0.0f };
    const int32_t gx = x - sGridX - GRID_X0;
    const int32_t gz = z - sGridZ - GRID_Z0;

    if (gx < 0 || gx >= GRID_W || gz < 0 || gz >= GRID_H)
        return &sPlain;
    return &sGrid[gz][gx];
}

static const StageCell *CellAt(float x, float z)
{
    return CellAtCell((int32_t)floorf(x), (int32_t)floorf(z));
}

#define FEET_PLAYER_X 72.0f
#define FEET_PLAYER_Y 110.0f
#define FEET_ENEMY_X  176.0f
#define FEET_ENEMY_Y  70.0f

static const float sSideRect[2][4] = {
    {  16.0f, 40.0f, 128.0f, 112.0f },
    { 128.0f,  8.0f, 224.0f,  76.0f },
};

typedef struct
{
    BattleView view;
    float feetX[2], feetZ[2];
    float rect[2][4];
    uint8_t want[2];
} StageSearch;

static float Overlap(const float a[4], float x0, float y0, float x1, float y1)
{
    const float w = fminf(a[2], x1) - fmaxf(a[0], x0);
    const float h = fminf(a[3], y1) - fmaxf(a[1], y0);
    return (w > 0.0f && h > 0.0f) ? (w * h / ((a[2] - a[0]) * (a[3] - a[1]))) : 0.0f;
}

static float FeetScore(const StageSearch *s, int32_t side, float tx, float tz, float groundT)
{
    const float fx = tx + s->feetX[side], fz = tz + s->feetZ[side];
    const StageCell *const cell = CellAt(fx, fz);
    const uint8_t want = s->want[side];
    float score = 0.0f;

    if (cell->kind == GROUND_VOID)
        return -60.0f;
    if (cell->top > 0.4f)
        score -= 16.0f;
    if (want == GROUND_WATER)
    {
        score += (cell->kind == GROUND_WATER) ? 8.0f : -8.0f;
    }
    else
    {
        if (cell->kind == GROUND_WATER)
            score -= 12.0f;
        else if (cell->blocked)
            score -= 6.0f;
        if (cell->kind == want)
            score += 4.0f;
    }
    if (cell->slope)
        score -= 4.0f;
    score -= 3.0f * fabsf(cell->lift - groundT);

    for (int32_t dz = -1; dz <= 1; ++dz)
        for (int32_t dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dz == 0)
                continue;
            const StageCell *const near = CellAt(fx + (float)dx, fz + (float)dz);
            if (near->kind == GROUND_VOID)
                score -= 3.0f;
            else if (near->top > 0.4f)
                score -= 1.0f;
            if (near->kind == want)
                score += 0.5f;
            else if ((near->kind == GROUND_WATER) != (want == GROUND_WATER))
                score -= 0.8f;
            if (near->slope)
                score -= 0.4f;
        }
    return score;
}

static float StageScore(const StageSearch *s, float tx, float tz, float distance)
{
    const BattleView *const v = &s->view;
    const float groundT = CellAt(tx, tz)->lift;
    float score = -0.5f * distance;
    const int32_t ix = (int32_t)floorf(tx), iz = (int32_t)floorf(tz);

    for (int32_t side = 0; side < 2; ++side)
        score += FeetScore(s, side, tx, tz, groundT);

    for (int32_t dz = -3; dz <= 7; ++dz)
        for (int32_t dx = -6; dx <= 6; ++dx)
        {
            const StageCell *const cell = CellAtCell(ix + dx, iz + dz);
            if (cell->top <= 0.4f)
                continue;
            const float cx = (float)(ix + dx), cz = (float)(iz + dz);
            const float h = cell->lift - groundT;
            float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;

            if (!Project(v, cx - tx, h + cell->top, cz + 0.5f - tz, &x0, &y0)
             || !Project(v, cx + 1.0f - tx, h, cz + 1.0f - tz, &x1, &y1))
                continue;
            for (int32_t side = 0; side < 2; ++side)
                if (cz + 1.0f - tz > s->feetZ[side] - 0.5f)
                    score -= 24.0f * Overlap(s->rect[side], x0, y0, x1, y1);
        }

    for (int32_t dz = -10; dz <= 4; ++dz)
        for (int32_t dx = -10; dx <= 10; ++dx)
        {
            const float cx = (float)(ix + dx) + 0.5f, cz = (float)(iz + dz) + 0.5f;
            float sx = 0.0f, sy = 0.0f;

            if (CellAtCell(ix + dx, iz + dz)->kind != GROUND_VOID)
                continue;
            if (!Project(v, cx - tx, 0.0f, cz - tz, &sx, &sy)
             || sx < 0.0f || sx > VITA_GAME_WIDTH
             || sy < 0.0f || sy > VITA_GAME_HEIGHT - 48.0f * VITA_BATTLE_ZOOM)
                continue;
            score -= sy > 90.0f * (VITA_GAME_HEIGHT / 240.0f) ? 1.2f : 0.5f;
        }
    return score;
}

static uint8_t KindAt(int32_t x, int32_t z)
{
    return KindOf(VoxelWorld_GetMetatileBehavior(x, z));
}

static struct
{
    StageSearch search;
    float baseX, baseZ, bestScore, bestX, bestZ;
    uint32_t cells, candidates;
    bool indoor, done;
} sStage;

void VoxelBattle_BeginStage(void)
{
    const GameFrame *const frame = GameFrame_Get();
    const VoxelMapInstance *const current = VoxelWorld_Instance(0u);
    const bool surfing = frame->field.playerSurfing != 0;
    const bool wild = frame->battle.isWild != 0;
    StageSearch *const search = &sStage.search;
    int32_t frontX = 0, frontZ = 0;
    float px = 0.0f, pz = 0.0f;

    memset(&sStage, 0, sizeof(sStage));
    sStage.indoor = (current != NULL && current->indoor);
    ViewInit(&search->view);
    for (int32_t side = 0; side < 2; ++side)
    {
        const float gx = (side == 0) ? FEET_PLAYER_X : FEET_ENEMY_X;
        const float gy = (side == 0) ? FEET_PLAYER_Y : FEET_ENEMY_Y;

        ScreenToGround(&search->view, GbaToScreenX(gx), GbaToScreenY(gy),
                       &search->feetX[side], &search->feetZ[side]);
        search->rect[side][0] = GbaToScreenX(sSideRect[side][0]);
        search->rect[side][1] = GbaToScreenY(sSideRect[side][1]);
        search->rect[side][2] = GbaToScreenX(sSideRect[side][2]);
        search->rect[side][3] = GbaToScreenY(sSideRect[side][3]);
    }

    VoxelWorld_GetPlayerWorldCoords(&px, &pz);
    switch (frame->field.playerFacing)
    {
    case DIR_NORTH: frontZ = -1; break;
    case DIR_SOUTH: frontZ =  1; break;
    case DIR_WEST:  frontX = -1; break;
    case DIR_EAST:  frontX =  1; break;
    }
    const uint8_t here = KindAt((int32_t)px, (int32_t)pz);
    const uint8_t front = KindAt((int32_t)px + frontX, (int32_t)pz + frontZ);

    search->want[0] = surfing ? GROUND_WATER : here;
    if (surfing)
        search->want[1] = GROUND_WATER;
    else if (wild)
        search->want[1] = (here != GROUND_GRASS && front == GROUND_WATER) ? GROUND_WATER : here;
    else
        search->want[1] = front;

    for (int32_t side = 0; side < 2; ++side)
        if (search->want[side] == GROUND_VOID)
            search->want[side] = GROUND_PLAIN;

    sStage.baseX = px + 0.5f - search->feetX[0];
    sStage.baseZ = pz + 0.5f - search->feetZ[0];
    sStage.bestX = sStage.baseX;
    sStage.bestZ = sStage.baseZ;
    sStage.bestScore = -1.0e9f;
    sGridX = (int32_t)floorf(sStage.baseX);
    sGridZ = (int32_t)floorf(sStage.baseZ);
}

bool VoxelBattle_StepStage(uint32_t cells, uint32_t candidates, float *targetX, float *targetZ,
                           float *ground)
{
    if (!sStage.done)
    {
        for (; cells != 0u && sStage.cells < (uint32_t)(GRID_W * GRID_H); --cells, ++sStage.cells)
        {
            const uint32_t gx = sStage.cells % (uint32_t)GRID_W;
            const uint32_t gz = sStage.cells / (uint32_t)GRID_W;

            ReadCell(&sGrid[gz][gx], sGridX + GRID_X0 + (int32_t)gx,
                     sGridZ + GRID_Z0 + (int32_t)gz, sStage.indoor);
        }
        if (sStage.cells < (uint32_t)(GRID_W * GRID_H))
            return false;
        for (; candidates != 0u && sStage.candidates < (uint32_t)(STAGE_SIDE * STAGE_SIDE);
             --candidates, ++sStage.candidates)
        {
            const int32_t dx = (int32_t)(sStage.candidates % (uint32_t)STAGE_SIDE) - STAGE_REACH;
            const int32_t dz = (int32_t)(sStage.candidates / (uint32_t)STAGE_SIDE) - STAGE_REACH;
            const float tx = sStage.baseX + (float)dx, tz = sStage.baseZ + (float)dz;
            const float score = StageScore(&sStage.search, tx, tz, sqrtf((float)(dx * dx + dz * dz)));

            if (score > sStage.bestScore)
            {
                sStage.bestScore = score;
                sStage.bestX = tx;
                sStage.bestZ = tz;
            }
        }
        if (sStage.candidates < (uint32_t)(STAGE_SIDE * STAGE_SIDE))
            return false;
        sStage.done = true;
    }
    if (targetX != NULL)
        *targetX = sStage.bestX;
    if (targetZ != NULL)
        *targetZ = sStage.bestZ + VoxelRelief_ShiftAt(sStage.bestX, sStage.bestZ);
    if (ground != NULL)
        *ground = VoxelRelief_LiftAt(sStage.bestX, sStage.bestZ);
    return true;
}

/* ── Core 0 / Core 1: shadows & state capture ────────────────────────────── */

static uint32_t ShadowsLive(VoxelBattleShadow *out, uint32_t max)
{
    uint32_t count = 0u;

    if (!gMain.inBattle || gBattleSpritesDataPtr == NULL || gBattleSpritesDataPtr->healthBoxesData == NULL)
        return 0u;
    for (uint32_t b = 0u; b < gBattlersCount && b < MAX_BATTLERS_COUNT && count < max; ++b)
    {
        const uint32_t id = gBattlerSpriteIds[b];
        if ((gAbsentBattlerFlags & (1u << b)) || id >= MAX_SPRITES)
            continue;
        const struct Sprite *const sprite = &gSprites[id];
        if (!sprite->inUse || sprite->invisible)
            continue;
        const bool enemy = (GetBattlerSide((u8)b) != B_SIDE_PLAYER);
        if (!enemy && sprite->anims != gAnims_MonPic)
            continue;
        const uint32_t shadow = gBattleSpritesDataPtr->healthBoxesData[b].shadowSpriteId;
        if (enemy && shadow < MAX_SPRITES && gSprites[shadow].inUse && !gSprites[shadow].invisible)
            continue;

        out[count].x = (float)(sprite->x + sprite->x2);
        out[count].y = (float)GetBattlerSpriteCoord((u8)b, BATTLER_COORD_Y) + 30.0f;
        out[count].rx = enemy ? 24.0f : 30.0f;
        out[count].ry = enemy ? 5.0f : 6.0f;
        ++count;
    }
    return count;
}

uint32_t VoxelBattle_Shadows(VoxelBattleShadow *out, uint32_t max)
{
    const GameFrameBattle *const battle = &GameFrame_Get()->battle;
    const uint32_t count = battle->shadowCount < max ? battle->shadowCount : max;

    for (uint32_t i = 0u; i < count; ++i)
    {
        out[i].x = battle->shadows[i].x;
        out[i].y = battle->shadows[i].y;
        out[i].rx = battle->shadows[i].rx;
        out[i].ry = battle->shadows[i].ry;
    }
    return count;
}

void VoxelBattle_Capture(GameFrameBattle *battle, bool8 voxel)
{
    VoxelBattleShadow shadows[GAME_FRAME_BATTLERS];

    if (battle == NULL)
        return;
    memset(battle, 0, sizeof(*battle));
    battle->inBattle = gMain.inBattle;
    battle->isLink = (gBattleTypeFlags & BATTLE_TYPE_LINK) != 0;
    battle->isWild = !(gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK
                                           | BATTLE_TYPE_BATTLE_TOWER | BATTLE_TYPE_TRAINER_TOWER
                                           | BATTLE_TYPE_EREADER_TRAINER));
    battle->moveType = TYPE_NORMAL;
    if (!gMain.inBattle)
        return;

    for (uint32_t b = 0u; b < GAME_FRAME_BATTLERS; ++b)
    {
        battle->hp[b] = gBattleMons[b].hp;
        battle->maxHp[b] = gBattleMons[b].maxHP;
    }
    battle->critical = gCritMultiplier > 1u;
    if (gBattleStruct != NULL && (gBattleStruct->dynamicMoveType & DYNAMIC_TYPE_MASK) != 0u)
        battle->moveType = (u8)(gBattleStruct->dynamicMoveType & DYNAMIC_TYPE_MASK);
    else if (gCurrentMove != 0u && gCurrentMove < MOVES_COUNT)
        battle->moveType = gBattleMoves[gCurrentMove].type;
    battle->moveBgShown = CtrBattleBg_MoveBgShown();
    battle->introSliding = CtrBattleIntro_Sliding();

    if (!voxel)
        return;
    battle->shadowCount = ShadowsLive(shadows, GAME_FRAME_BATTLERS);
    for (uint32_t i = 0u; i < battle->shadowCount; ++i)
    {
        battle->shadows[i].x = shadows[i].x;
        battle->shadows[i].y = shadows[i].y;
        battle->shadows[i].rx = shadows[i].rx;
        battle->shadows[i].ry = shadows[i].ry;
    }
}
