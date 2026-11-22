/*
 * Core 0 follower and visible wild Pokemon billboard decoder/animator (see
 * voxel_follower.h). Reads only VoxelEntitySnapshot from GameFrame_Get().
 *
 * Origin: derived from Pokemon-FireRed-3DS (3ds_port/src/voxel/voxel_follower.c,
 * MIT) & pokeemerald-vita-3ds; see NOTICE.md.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "platform/voxel_snapshot.h"
#include "3ds_log.h"
#include "voxel_entities.h"
#include "voxel_file.h"
#include "voxel_follower.h"
#include "voxel_grade.h"
#include "voxel_unswizzle.h"

#define FOLLOWERS_COUNT 387u
#define FOLLOW_DISTANCE 1.0f      /* minimum tiles behind player along path */
#define PATH_STEP       0.05f     /* minimum tile distance between path samples */
#define PATH_POINTS     128
#define WARP_JUMP       3.0f
#define HEADING_AHEAD   0.4f
#define HEADING_EASE    0.25f
#define TURN_MARGIN     10.0f
#define ANIM_HEAD       52u
#define DIRECTIONS      8u

static FILE *sFile;
static bool sFileTried;
static bool sFileBad;
static uint32_t sOffset[FOLLOWERS_COUNT];
static uint32_t sSize[FOLLOWERS_COUNT];

static uint8_t *sEntry;
static int32_t sSpecies = -1;
static int32_t sFoot;
static float sDistance = FOLLOW_DISTANCE;

static float sPathX[PATH_POINTS];
static float sPathZ[PATH_POINTS];
static int32_t sPathCount;
static float sX, sZ, sDirX, sDirZ = 1.0f;
static bool sPlaced;
static float sAnimTime;
static int32_t sScreenDir = -1;
static int32_t sStill;
static uint16_t sMapKey = 0xFFFFu;
static uint32_t sLastFollowerFrameNum;
static uint32_t sLastWildFrameNum;

static int32_t sDrawnKey = -1;
static int32_t sDrawnW, sDrawnH;
static uint32_t sColumn[VOXEL_FOLLOWER_CELL_DIM];
static bool sColumnsBuilt;

static const uint16_t sWildCell[VOXEL_WILD_MAX][2] = {
    { 128u, 256u }, { 192u, 256u },
    { 128u, 320u }, { 192u, 320u },
    {   0u, 384u }, {  64u, 384u },
};
static const uint8_t sWildDirtySlot[VOXEL_WILD_MAX] = { 34u, 35u, 42u, 43u, 48u, 49u };

typedef struct
{
    uint8_t *entry;
    int32_t national;
    int32_t spriteId;
    int32_t foot;
    int32_t scale;
    int32_t drawnKey;
    int32_t drawnW;
    int32_t drawnH;
    int32_t screenDir;
    int32_t still;
    float dirX, dirZ, animTime;
    uint32_t column[VOXEL_WILD_CELL_DIM];
    bool columnsBuilt;
} WildDraw;

static WildDraw sWild[VOXEL_WILD_MAX];

static uint32_t Le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool OpenFile(void)
{
    uint8_t head[12];

    if (sFileTried)
        return sFile != NULL;
    sFileTried = true;
    sFile = VoxelFile_Open("voxel/followers.bin");
    if (sFile == NULL)
        sFile = VoxelFile_Open("followers.bin");
    if (sFile == NULL)
    {
        CtrLog_Write(CTR_LOG_VIDEO, "follower: no followers.bin found (optional pack)");
        return false;
    }
    if (fread(head, 1u, sizeof(head), sFile) != sizeof(head)
     || memcmp(head, "FRFW", 4) != 0
     || Le32(head + 4) != 1u
     || Le32(head + 8) != FOLLOWERS_COUNT
     || fread(sOffset, 4u, FOLLOWERS_COUNT, sFile) != FOLLOWERS_COUNT
     || fread(sSize, 4u, FOLLOWERS_COUNT, sFile) != FOLLOWERS_COUNT)
    {
        CtrLog_Write(CTR_LOG_VIDEO, "follower: invalid followers.bin header");
        fclose(sFile);
        sFile = NULL;
        sFileBad = true;
        return false;
    }
    return true;
}

static bool ValidateEntry(const uint8_t *entry, uint32_t size)
{
    uint32_t offset = 0u;

    for (int32_t anim = 0; anim < 2; ++anim)
    {
        if (size < offset || size - offset < ANIM_HEAD)
            return false;
        const uint8_t *const a = entry + offset;
        const uint32_t frames = a[0];
        if (frames == 0u || frames > 16u)
            return false;
        const uint32_t tableBytes = 8u * DIRECTIONS * frames;
        if (size - offset - ANIM_HEAD < tableBytes)
            return false;
        for (uint32_t i = 0u; i < DIRECTIONS * frames; ++i)
        {
            const uint8_t *const r = a + ANIM_HEAD + 8u * i;
            const uint32_t pxBytes = ((uint32_t)r[2] * (uint32_t)r[3] + 1u) >> 1;
            const uint32_t pxOff = Le32(r + 4);
            if (pxOff > size || size - pxOff < pxBytes)
                return false;
        }
        offset += ANIM_HEAD + tableBytes;
    }
    return true;
}

static const uint8_t *AnimOf(const uint8_t *entry, int32_t anim)
{
    return anim == 0 ? entry : entry + ANIM_HEAD + 8u * DIRECTIONS * entry[0];
}

static const uint8_t *FrameOf(const uint8_t *entry, int32_t anim, int32_t dir, int32_t frame)
{
    const uint8_t *const a = AnimOf(entry, anim);

    return a + ANIM_HEAD + 8u * ((uint32_t)dir * a[0] + (uint32_t)frame);
}

static bool LoadFollower(int32_t national)
{
    if (national == sSpecies)
        return sEntry != NULL;
    free(sEntry);
    sEntry = NULL;
    sSpecies = national;
    sDrawnKey = -1;
    if (national <= 0 || (uint32_t)national >= FOLLOWERS_COUNT || sSize[national] < ANIM_HEAD * 2u)
        return false;
    sEntry = malloc(sSize[national]);
    if (sEntry == NULL
     || fseek(sFile, (long)sOffset[national], SEEK_SET) != 0
     || fread(sEntry, 1u, sSize[national], sFile) != sSize[national]
     || !ValidateEntry(sEntry, sSize[national]))
    {
        CtrLog_Write(CTR_LOG_VIDEO, "follower: #%d could not be read", national);
        free(sEntry);
        sEntry = NULL;
        return false;
    }

    sFoot = 0;
    sDistance = FOLLOW_DISTANCE;
    const int32_t walkFrames = (int32_t)AnimOf(sEntry, 0)[0];
    for (int32_t f = 0; f < walkFrames; ++f)
    {
        const float reach = 0.5f + (float)FrameOf(sEntry, 0, 2, f)[2] / 16.0f * 0.5f;
        if (reach > sDistance)
            sDistance = reach;
    }
    for (int32_t f = 0; f < walkFrames; ++f)
    {
        const uint8_t *const r = FrameOf(sEntry, 0, 0, f);
        const int32_t bottom = (int8_t)r[1] + (int32_t)r[3];
        if (f == 0 || bottom > sFoot)
            sFoot = bottom;
    }
    return true;
}

static void FacingVector(uint8_t facing, float *x, float *z)
{
    *x = (facing == DIR_WEST) ? -1.0f : (facing == DIR_EAST ? 1.0f : 0.0f);
    *z = (facing == DIR_NORTH) ? -1.0f : (facing == DIR_SOUTH ? 1.0f : 0.0f);
}

static void StartPath(uint8_t facing, float px, float pz)
{
    float fx = 0.0f, fz = 1.0f;

    FacingVector(facing, &fx, &fz);
    if (fx == 0.0f && fz == 0.0f)
        fz = 1.0f;
    sPathX[0] = px - fx * sDistance;
    sPathZ[0] = pz - fz * sDistance;
    sPathX[1] = px;
    sPathZ[1] = pz;
    sPathCount = 2;
    sX = sPathX[0];
    sZ = sPathZ[0];
    sDirX = fx;
    sDirZ = fz;
    sScreenDir = -1;
    sPlaced = true;
    sStill = 1000;
}

static void WildForget(WildDraw *w)
{
    free(w->entry);
    w->entry = NULL;
    w->national = 0;
    w->spriteId = -1;
    w->drawnKey = -1;
}

void VoxelFollower_Reset(void)
{
    sPlaced = false;
    sPathCount = 0;
    sDrawnKey = -1;
    sMapKey = 0xFFFFu;
    sLastFollowerFrameNum = 0u;
    sLastWildFrameNum = 0u;
    for (uint32_t i = 0u; i < VOXEL_WILD_MAX; ++i)
        WildForget(&sWild[i]);
}

static void PathPoint(float px, float pz, float distance, float *x, float *z)
{
    float tx = px, tz = pz, left = distance;

    for (int32_t i = sPathCount - 1; i >= 0; --i)
    {
        const float sx = sPathX[i] - tx, sz = sPathZ[i] - tz;
        const float len = sqrtf(sx * sx + sz * sz);

        if (len >= left && len > 0.0001f)
        {
            tx += sx * (left / len);
            tz += sz * (left / len);
            break;
        }
        left -= len;
        tx = sPathX[i];
        tz = sPathZ[i];
    }
    *x = tx;
    *z = tz;
}

static void Follow(float px, float pz, uint32_t frameDelta)
{
    const float lastX = sPathX[sPathCount - 1], lastZ = sPathZ[sPathCount - 1];
    float dx = px - lastX, dz = pz - lastZ;
    const float ox = sX, oz = sZ;

    if (dx * dx + dz * dz >= PATH_STEP * PATH_STEP)
    {
        if (sPathCount == PATH_POINTS)
        {
            memmove(sPathX, sPathX + 1, sizeof(float) * (PATH_POINTS - 1));
            memmove(sPathZ, sPathZ + 1, sizeof(float) * (PATH_POINTS - 1));
            --sPathCount;
        }
        sPathX[sPathCount] = px;
        sPathZ[sPathCount] = pz;
        ++sPathCount;
    }

    PathPoint(px, pz, sDistance, &sX, &sZ);
    dx = sX - ox;
    dz = sZ - oz;
    if (dx * dx + dz * dz > 0.0004f * 0.0004f)
    {
        const float step = sqrtf(dx * dx + dz * dz);
        float ax = sX, az = sZ;

        PathPoint(px, pz, sDistance > HEADING_AHEAD ? sDistance - HEADING_AHEAD : 0.0f, &ax, &az);
        float hx = ax - sX;
        float hz = az - sZ;
        float len = sqrtf(hx * hx + hz * hz);
        if (len < 0.01f)
        {
            hx = dx;
            hz = dz;
            len = step;
        }
        hx = sDirX + (hx / len - sDirX) * HEADING_EASE;
        hz = sDirZ + (hz / len - sDirZ) * HEADING_EASE;
        len = sqrtf(hx * hx + hz * hz);
        if (len > 0.001f)
        {
            sDirX = hx / len;
            sDirZ = hz / len;
        }
        const float animStep = step * 16.0f;
        sAnimTime += animStep < 0.5f ? 0.5f : (animStep > 3.0f ? 3.0f : animStep);
        if (sStill > 8)
            sAnimTime = 0.0f;
        sStill = 0;
    }
    else if (frameDelta > 0u)
    {
        if (sStill < 8 && sStill + (int32_t)frameDelta >= 8)
            sAnimTime = 0.0f;
        sStill += (int32_t)frameDelta;
        sAnimTime += (float)frameDelta;
    }
}

static int32_t ComputeScreenDirection(float dirX, float dirZ, float yawDegrees, int32_t *prevDir)
{
    const float pi = 3.14159265358979323846f;
    const float yaw = yawDegrees * (pi / 180.0f);
    const float sx = dirX * cosf(yaw) - dirZ * sinf(yaw);
    const float sy = dirX * sinf(yaw) + dirZ * cosf(yaw);
    const float angle = atan2f(sx, sy) * (180.0f / pi);
    int32_t k = (int32_t)lroundf(angle / 45.0f);

    k = ((k % (int32_t)DIRECTIONS) + (int32_t)DIRECTIONS) % (int32_t)DIRECTIONS;
    if (*prevDir >= 0 && k != *prevDir)
    {
        const float off = fabsf(fmodf(angle - (float)(*prevDir) * 45.0f + 540.0f, 360.0f) - 180.0f);
        if (off < 22.5f + TURN_MARGIN)
            k = *prevDir;
    }
    *prevDir = k;
    return k;
}

static int32_t FrameAt(const uint8_t *anim, float time)
{
    const int32_t frames = (int32_t)anim[0];
    int32_t total = 0;

    for (int32_t f = 0; f < frames; ++f)
        total += anim[36 + f] ? (int32_t)anim[36 + f] : 1;
    int32_t t = (int32_t)time % (total > 0 ? total : 1);
    for (int32_t f = 0; f < frames; ++f)
    {
        t -= anim[36 + f] ? (int32_t)anim[36 + f] : 1;
        if (t < 0)
            return f;
    }
    return 0;
}

static void DrawFollowerFrame(uint16_t *atlas, int32_t anim, int32_t dir, int32_t frame)
{
    const uint8_t *const a = AnimOf(sEntry, anim);
    const uint8_t *const r = FrameOf(sEntry, anim, dir, frame);
    int32_t w = (int32_t)r[2], h = (int32_t)r[3];
    const uint8_t *const px = sEntry + Le32(r + 4);
    uint16_t graded[16];
    int32_t clearW = sDrawnW > w ? sDrawnW : w;
    int32_t clearH = sDrawnH > h ? sDrawnH : h;

    if (w > (int32_t)VOXEL_FOLLOWER_CELL_DIM) w = (int32_t)VOXEL_FOLLOWER_CELL_DIM;
    if (h > (int32_t)VOXEL_FOLLOWER_CELL_DIM) h = (int32_t)VOXEL_FOLLOWER_CELL_DIM;
    if (clearW > (int32_t)VOXEL_FOLLOWER_CELL_DIM) clearW = (int32_t)VOXEL_FOLLOWER_CELL_DIM;
    if (clearH > (int32_t)VOXEL_FOLLOWER_CELL_DIM) clearH = (int32_t)VOXEL_FOLLOWER_CELL_DIM;

    graded[0] = 0u;
    for (int32_t c = 1; c < 16; ++c)
        graded[c] = VoxelGrade_RGBA5551((uint16_t)(a[4 + 2 * c] | (a[5 + 2 * c] << 8)));

    if (!sColumnsBuilt)
    {
        for (uint32_t x = 0u; x < VOXEL_FOLLOWER_CELL_DIM; ++x)
            sColumn[x] = CtrVideo_Texel(VOXEL_FOLLOWER_CELL_X + x, 0u, VOXEL_SPRITE_ATLAS_W);
        sColumnsBuilt = true;
    }
    for (int32_t y = 0; y < clearH; ++y)
    {
        uint16_t *const row = atlas + (CtrVideo_Texel(VOXEL_FOLLOWER_CELL_X,
                                                       VOXEL_FOLLOWER_CELL_Y + (uint32_t)y,
                                                       VOXEL_SPRITE_ATLAS_W) - sColumn[0]);
        int32_t x = 0;
        int32_t k = y * (int32_t)r[2];

        if (y < h)
        {
            for (; x < w; ++x, ++k)
                row[sColumn[x]] = graded[(px[k >> 1] >> ((k & 1) * 4)) & 15u];
        }
        for (; x < clearW; ++x)
            row[sColumn[x]] = 0u;
    }
    sDrawnW = w;
    sDrawnH = h;
}

bool VoxelFollower_Update(uint16_t *atlas, float yawDegrees, VoxelFollowerCard *card,
                          uint64_t *dirtySlots)
{
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    float px = 0.0f, pz = 0.0f;

    if (snap == NULL || !snap->followerEnabled)
    {
        sPlaced = false;
        sPathCount = 0;
        sDrawnKey = -1;
        return false;
    }
    if (sFileBad || !OpenFile())
        return false;
    if (!LoadFollower((int32_t)snap->followerSpecies))
        return false;

    VoxelEntities_GetPlayerWorldPos(&px, &pz);
    const uint16_t mapKey = (uint16_t)(((uint16_t)(uint8_t)snap->mapGroup << 8)
                                     | (uint16_t)(uint8_t)snap->mapNum);
    if (!sPlaced || mapKey != sMapKey
     || fabsf(px - sPathX[sPathCount - 1]) > WARP_JUMP
     || fabsf(pz - sPathZ[sPathCount - 1]) > WARP_JUMP)
    {
        StartPath(snap->playerFacing, px, pz);
    }
    sMapKey = mapKey;
    uint32_t frameDelta = (sLastFollowerFrameNum != 0u && snap->frameNum >= sLastFollowerFrameNum)
                        ? (snap->frameNum - sLastFollowerFrameNum) : 1u;
    if (frameDelta > 4u)
        frameDelta = 4u;
    sLastFollowerFrameNum = snap->frameNum;
    Follow(px, pz, frameDelta);

    const int32_t anim = sStill > 8 ? 1 : 0;
    const int32_t dir = ComputeScreenDirection(sDirX, sDirZ, yawDegrees, &sScreenDir);
    const int32_t frame = FrameAt(AnimOf(sEntry, anim), sAnimTime);
    const int32_t key = (((int32_t)snap->followerSpecies * 2 + anim) * (int32_t)DIRECTIONS + dir) * 16 + frame;
    if (key != sDrawnKey)
    {
        DrawFollowerFrame(atlas, anim, dir, frame);
        sDrawnKey = key;
        if (dirtySlots != NULL)
            *dirtySlots |= VOXEL_FOLLOWER_DIRTY_MASK;
    }
    const uint8_t *const r = FrameOf(sEntry, anim, dir, frame);
    card->x = sX;
    card->z = sZ;
    card->dx = (int8_t)r[0];
    card->dy = (int8_t)r[1];
    card->w = sDrawnW;
    card->h = sDrawnH;
    card->foot = sFoot;
    return true;
}

static bool WildLoad(WildDraw *w, int32_t national)
{
    int32_t biggest = 0;

    if (national == w->national && w->entry != NULL)
        return true;
    WildForget(w);
    if (national <= 0 || (uint32_t)national >= FOLLOWERS_COUNT || sSize[national] < ANIM_HEAD * 2u)
        return false;
    w->entry = malloc(sSize[national]);
    if (w->entry == NULL
     || fseek(sFile, (long)sOffset[national], SEEK_SET) != 0
     || fread(w->entry, 1u, sSize[national], sFile) != sSize[national]
     || !ValidateEntry(w->entry, sSize[national]))
    {
        CtrLog_Write(CTR_LOG_VIDEO, "visible wild: #%d could not be read", national);
        WildForget(w);
        return false;
    }
    w->national = national;
    for (int32_t anim = 0; anim < 2; ++anim)
    {
        const int32_t frames = (int32_t)AnimOf(w->entry, anim)[0];
        for (int32_t dir = 0; dir < (int32_t)DIRECTIONS; ++dir)
            for (int32_t f = 0; f < frames; ++f)
            {
                const uint8_t *const r = FrameOf(w->entry, anim, dir, f);
                if ((int32_t)r[2] > biggest) biggest = (int32_t)r[2];
                if ((int32_t)r[3] > biggest) biggest = (int32_t)r[3];
            }
    }
    w->scale = biggest > (int32_t)VOXEL_WILD_CELL_DIM ? 2 : 1;
    w->foot = 0;
    const int32_t walkFrames = (int32_t)AnimOf(w->entry, 0)[0];
    for (int32_t f = 0; f < walkFrames; ++f)
    {
        const uint8_t *const r = FrameOf(w->entry, 0, 0, f);
        const int32_t bottom = (int8_t)r[1] + (int32_t)r[3];
        if (f == 0 || bottom > w->foot)
            w->foot = bottom;
    }
    return true;
}

static void WildDrawFrame(WildDraw *w, uint32_t cell, uint16_t *atlas,
                          int32_t anim, int32_t dir, int32_t frame)
{
    const uint8_t *const a = AnimOf(w->entry, anim);
    const uint8_t *const r = FrameOf(w->entry, anim, dir, frame);
    const int32_t fw = (int32_t)r[2], fh = (int32_t)r[3], s = w->scale;
    int32_t width = (fw + s - 1) / s;
    int32_t height = (fh + s - 1) / s;
    const uint8_t *const px = w->entry + Le32(r + 4);
    uint16_t graded[16];

    if (width > (int32_t)VOXEL_WILD_CELL_DIM) width = (int32_t)VOXEL_WILD_CELL_DIM;
    if (height > (int32_t)VOXEL_WILD_CELL_DIM) height = (int32_t)VOXEL_WILD_CELL_DIM;
    const int32_t clearW = w->drawnW > width ? w->drawnW : width;
    const int32_t clearH = w->drawnH > height ? w->drawnH : height;

    graded[0] = 0u;
    for (int32_t c = 1; c < 16; ++c)
        graded[c] = VoxelGrade_RGBA5551((uint16_t)(a[4 + 2 * c] | (a[5 + 2 * c] << 8)));
    if (!w->columnsBuilt)
    {
        for (uint32_t x = 0u; x < VOXEL_WILD_CELL_DIM; ++x)
            w->column[x] = CtrVideo_Texel(sWildCell[cell][0] + x, 0u, VOXEL_SPRITE_ATLAS_W);
        w->columnsBuilt = true;
    }
    for (int32_t y = 0; y < clearH; ++y)
    {
        uint16_t *const row = atlas + (CtrVideo_Texel(sWildCell[cell][0],
                                                       sWildCell[cell][1] + (uint32_t)y,
                                                       VOXEL_SPRITE_ATLAS_W) - w->column[0]);
        int32_t x = 0;

        if (y < height)
        {
            for (; x < width; ++x)
            {
                const int32_t k = y * s * fw + x * s;
                row[w->column[x]] = graded[(px[k >> 1] >> ((k & 1) * 4)) & 15u];
            }
        }
        for (; x < clearW; ++x)
            row[w->column[x]] = 0u;
    }
    w->drawnW = width;
    w->drawnH = height;
}

uint32_t VoxelWild_Update(uint16_t *atlas, float yawDegrees, float originX, float originZ,
                          VoxelWildCard cards[VOXEL_WILD_MAX], uint64_t *dirtySlots)
{
    const VoxelEntitySnapshot *const snap = VoxelEntities_GetVisibleSnapshot();
    uint32_t count = 0u;

    if (snap == NULL || !snap->wildSpritesAllowed || sFileBad || !OpenFile())
        return 0u;

    uint32_t wildDelta = (sLastWildFrameNum != 0u && snap->frameNum >= sLastWildFrameNum)
                       ? (snap->frameNum - sLastWildFrameNum) : 1u;
    if (wildDelta > 4u)
        wildDelta = 4u;
    sLastWildFrameNum = snap->frameNum;

    for (uint32_t i = 0u; i < VOXEL_WILD_MAX; ++i)
    {
        WildDraw *const w = &sWild[i];
        const VoxelWildSnapshot *const ws = &snap->wilds[i];
        VoxelWildCard *const card = &cards[count];

        if (!ws->active)
        {
            if (w->entry != NULL)
                WildForget(w);
            continue;
        }
        if (!WildLoad(w, (int32_t)ws->nationalSpecies))
            continue;
        if (w->spriteId != (int32_t)ws->spriteId)
        {
            w->spriteId = (int32_t)ws->spriteId;
            w->screenDir = -1;
            w->dirX = 0.0f;
            w->dirZ = 1.0f;
            w->animTime = 0.0f;
            w->still = 1000;
        }
        if (ws->progress != 0u)
        {
            w->dirX = (float)ws->dx;
            w->dirZ = (float)ws->dy;
            if (w->still > 8)
                w->animTime = 0.0f;
            w->still = 0;
            w->animTime += 0.5f * (float)wildDelta;
        }
        else if (wildDelta > 0u)
        {
            if (w->still < 8 && w->still + (int32_t)wildDelta >= 8)
                w->animTime = 0.0f;
            w->still += (int32_t)wildDelta;
            w->animTime += (float)wildDelta;
        }
        const int32_t anim = w->still > 8 ? 1 : 0;
        const int32_t dir = ComputeScreenDirection(w->dirX, w->dirZ, yawDegrees, &w->screenDir);
        const int32_t frame = FrameAt(AnimOf(w->entry, anim), w->animTime);
        const int32_t key = ((w->national * 2 + anim) * (int32_t)DIRECTIONS + dir) * 16 + frame;
        if (key != w->drawnKey)
        {
            WildDrawFrame(w, i, atlas, anim, dir, frame);
            w->drawnKey = key;
            if (dirtySlots != NULL)
                *dirtySlots |= (1ULL << sWildDirtySlot[i]);
        }
        const uint8_t *const r = FrameOf(w->entry, anim, dir, frame);
        card->x = originX + (float)ws->x + (float)ws->dx * ((float)ws->progress / 32.0f);
        card->z = originZ + (float)ws->y + (float)ws->dy * ((float)ws->progress / 32.0f);
        card->dx = (int8_t)r[0];
        card->dy = (int8_t)r[1];
        card->w = w->drawnW;
        card->h = w->drawnH;
        card->foot = w->foot;
        card->scale = w->scale;
        card->cellX = sWildCell[i][0];
        card->cellY = sWildCell[i][1];
        card->spriteId = (int32_t)ws->spriteId;
        ++count;
    }
    return count;
}
