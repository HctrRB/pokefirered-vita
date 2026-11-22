/*
 * voxel_fly.c - The Fly cinematic pure state machine (60 Hz).
 *
 * Origin: derived from pokeemerald-vita-3ds / Emerald 3DS (CC BY-NC-SA 4.0,
 * see NOTICE.md).
 */
#include <math.h>

#include "voxel_fly.h"

#define VOXEL_FLY_DIVE_FRAMES     18u
#define VOXEL_FLY_DIP             0.0f
#define VOXEL_FLY_CLIMB_FRAMES    60u
#define VOXEL_FLY_WISPS_FROM      0.10f
#define VOXEL_FLY_WISPS_OVER      0.30f
#define VOXEL_FLY_RISE_FROM       0.50f
#define VOXEL_FLY_COVER_FROM      0.70f
#define VOXEL_FLY_RELEASE_FROM    0.70f
#define VOXEL_FLY_CLOUD_MIN       12u
#define VOXEL_FLY_CLOUD_MAX       240u
#define VOXEL_FLY_DESCEND_FRAMES  60u
#define VOXEL_FLY_UNCOVER_OVER    0.28f
#define VOXEL_FLY_SINK_OVER       0.45f
#define VOXEL_FLY_WISPS_GONE_FROM 0.45f
#define VOXEL_FLY_WISPS_GONE_OVER 0.35f

static float Smoother(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float Smooth(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float Over(float u, float from, float over)
{
    return Smooth((u - from) / over);
}

static float Ramp(uint32_t frame, uint32_t frames)
{
    return frames == 0u ? 1.0f : (float)frame / (float)frames;
}

static void Go(VoxelFly *f, VoxelFlyPhase phase)
{
    f->phase = phase;
    f->frame = 0u;
}

void VoxelFly_Begin(VoxelFly *f, float dirX, float dirZ)
{
    const float length = sqrtf(dirX * dirX + dirZ * dirZ);

    if (length < 0.001f)
    {
        dirX = 0.0f;
        dirZ = -1.0f;
    }
    else
    {
        dirX /= length;
        dirZ /= length;
    }
    f->dirX = dirX;
    f->dirZ = dirZ;
    f->gone = false;
    Go(f, VOXEL_FLY_DIVE);
}

void VoxelFly_Cancel(VoxelFly *f)
{
    Go(f, VOXEL_FLY_IDLE);
}

bool VoxelFly_Active(const VoxelFly *f)
{
    return f->phase != VOXEL_FLY_IDLE;
}

bool VoxelFly_Hovers(const VoxelFly *f)
{
    return f->phase == VOXEL_FLY_DIVE
        || (f->phase == VOXEL_FLY_CLIMB
            && (float)f->frame < VOXEL_FLY_RELEASE_FROM * (float)VOXEL_FLY_CLIMB_FRAMES);
}

bool VoxelFly_Busy(const VoxelFly *f)
{
    return f->phase == VOXEL_FLY_CLOUD || f->phase == VOXEL_FLY_DESCEND;
}

void VoxelFly_Frame(const VoxelFly *f, VoxelFlyFrame *out)
{
    const uint32_t n = f->frame;

    out->lift = 0.0f;
    out->rise = 0.0f;
    out->whiteout = 0.0f;
    out->wisps = 0.0f;
    out->dirX = f->dirX;
    out->dirZ = f->dirZ;
    out->oldMap = true;
    out->noFade = false;
    switch (f->phase)
    {
    case VOXEL_FLY_IDLE:
        break;
    case VOXEL_FLY_DIVE:
        out->lift = -VOXEL_FLY_DIP * Smooth(Ramp(n + 1u, VOXEL_FLY_DIVE_FRAMES));
        break;
    case VOXEL_FLY_CLIMB:
    {
        const float u = Ramp(n + 1u, VOXEL_FLY_CLIMB_FRAMES);

        out->lift = -VOXEL_FLY_DIP + (1.0f + VOXEL_FLY_DIP) * Smoother(u);
        out->wisps = Over(u, VOXEL_FLY_WISPS_FROM, VOXEL_FLY_WISPS_OVER);
        out->rise = Over(u, VOXEL_FLY_RISE_FROM, 1.0f - VOXEL_FLY_RISE_FROM);
        out->whiteout = Over(u, VOXEL_FLY_COVER_FROM, 1.0f - VOXEL_FLY_COVER_FROM);
        out->noFade = (u >= VOXEL_FLY_COVER_FROM);
        break;
    }
    case VOXEL_FLY_CLOUD:
        out->lift = 1.0f;
        out->rise = 1.0f;
        out->wisps = 1.0f;
        out->whiteout = 1.0f;
        out->oldMap = !f->gone;
        out->noFade = true;
        break;
    case VOXEL_FLY_DESCEND:
    {
        const float u = Ramp(n, VOXEL_FLY_DESCEND_FRAMES);

        out->lift = 1.0f - Smoother(u);
        out->rise = 1.0f - Over(u, 0.0f, VOXEL_FLY_SINK_OVER);
        out->whiteout = 1.0f - Over(u, 0.0f, VOXEL_FLY_UNCOVER_OVER);
        out->wisps = 1.0f - Over(u, VOXEL_FLY_WISPS_GONE_FROM, VOXEL_FLY_WISPS_GONE_OVER);
        out->oldMap = false;
        out->noFade = true;
        break;
    }
    }
}

void VoxelFly_Step(VoxelFly *f, bool fieldShown, bool fieldGone)
{
    ++f->frame;
    switch (f->phase)
    {
    case VOXEL_FLY_IDLE:
        f->frame = 0u;
        break;
    case VOXEL_FLY_DIVE:
        if (f->frame >= VOXEL_FLY_DIVE_FRAMES)
            Go(f, VOXEL_FLY_CLIMB);
        break;
    case VOXEL_FLY_CLIMB:
        if (f->frame >= VOXEL_FLY_CLIMB_FRAMES)
            Go(f, VOXEL_FLY_CLOUD);
        break;
    case VOXEL_FLY_CLOUD:
        if (fieldGone)
            f->gone = true;
        if (f->gone && fieldShown && f->frame >= VOXEL_FLY_CLOUD_MIN)
            Go(f, VOXEL_FLY_DESCEND);
        else if (f->frame >= VOXEL_FLY_CLOUD_MAX)
            Go(f, VOXEL_FLY_IDLE);
        break;
    case VOXEL_FLY_DESCEND:
        if (f->frame >= VOXEL_FLY_DESCEND_FRAMES)
            Go(f, VOXEL_FLY_IDLE);
        break;
    }
}
