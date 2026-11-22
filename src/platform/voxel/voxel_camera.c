/*
 * Follow camera for the voxel overworld. See voxel_camera.h and NOTICE.md.
 */

#include <stdint.h>
#include <math.h>

#include "3ds_field_view.h"
#include "3ds_platform.h"
#include "platform/vita_extras.h"
#include "voxel_camera.h"
#include "voxel_relief.h"
#include "voxel_tuning.h"
#include "voxel_world.h"

#define VOXEL_DEG_TO_RAD (3.14159265358979323846f / 180.0f)

static inline float FollowAlpha(float dt)
{
    if (dt <= 0.0f)
        return 0.0f;
    if (dt > VOXEL_FOLLOW_DT_MAX)
        dt = VOXEL_FOLLOW_DT_MAX;
    return 1.0f - expf(-dt / VOXEL_FOLLOW_TAU);
}

#define VOXEL_CAMERA_FOV 35.0f
#define VOXEL_CAMERA_PITCH_DEFAULT 35.0f
#define VOXEL_CAMERA_PITCH_LOW 25.0f

static float DesiredPitch(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_FOV) ? VOXEL_CAMERA_PITCH_LOW : VOXEL_CAMERA_PITCH_DEFAULT;
}

#define VOXEL_SWOOP_HALF_TILES 7.5f
#define VOXEL_SWOOP_SHIFT_X -0.125f
#define VOXEL_SWOOP_SHIFT_Z 0.4f
#define VOXEL_ASPECT (960.0f / 544.0f)
#define VOXEL_EDGE_UP 0.4f
#define VOXEL_EDGE_DOWN 0.25f
#define VOXEL_EDGE_SIDE 0.45f
#define VOXEL_TILE_MIDDLE 0.5f

void VoxelCamera_Init(VoxelCamera *cam)
{
    cam->x = 0.0f;
    cam->y = 8.0f;
    cam->z = 0.0f;
    cam->targetX = 0.0f;
    cam->targetY = 0.0f;
    cam->targetZ = 0.0f;
    cam->pitch = DesiredPitch();
    cam->yaw = 0.0f;
    cam->distance = 9.0f;
    cam->fov = VOXEL_CAMERA_FOV;
    cam->ground = 0.0f;
    cam->swoopX = cam->swoopZ = 0.0f;
    cam->lay = 0.0f;
}

void VoxelCamera_SetGround(VoxelCamera *cam, float ground, int32_t snap, float dt)
{
    const float k = snap ? 1.0f : FollowAlpha(dt);
    cam->ground += (ground - cam->ground) * k;
}

static void Place(VoxelCamera *cam)
{
    float pitchRad = cam->pitch * VOXEL_DEG_TO_RAD;
    float yawRad = cam->yaw * VOXEL_DEG_TO_RAD;

    cam->x = cam->targetX + sinf(yawRad) * cam->distance;
    cam->y = cam->ground + tanf(pitchRad) * cam->distance;
    cam->z = cam->targetZ + cosf(yawRad) * cam->distance;
    cam->targetY = cam->ground;
}

static void AdaptDistance(VoxelCamera *cam)
{
    int32_t mapW = 0, mapH = 0;
    float scale = 5.0f;

    if (!VoxelWorld_MapIsConnected())
    {
        VoxelWorld_GetMapDimensions(&mapW, &mapH);
        scale = (float)(mapW > mapH ? mapW : mapH) * 0.2f;
        if (scale > 5.0f)
            scale = 5.0f;
    }
    cam->fov = VOXEL_CAMERA_FOV;
    cam->distance = ((8.0f + scale) * 0.90f) * 100.0f / (float)CtrSettings_VoxelZoom();
    cam->pitch = DesiredPitch();
}

static float Between(float value, float lo, float hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static void StopAtEdges(const VoxelCamera *cam, float *x, float *z)
{
    const VoxelMapInstance *map = VoxelWorld_Instance(0);
    uint32_t closed = VoxelWorld_ClosedEdges();
    float pitch = cam->pitch * VOXEL_DEG_TO_RAD, half = cam->fov * 0.5f * VOXEL_DEG_TO_RAD;
    float height = tanf(pitch) * cam->distance;
    float north, south, side;
    float playerX = *x, playerZ = *z;

    if (map == NULL || closed == 0)
        return;
    if (pitch - half < 0.02f)
        closed &= ~CTR_FIELD_EDGE_NORTH;
    north = pitch - half >= 0.02f ? height / tanf(pitch - half) - cam->distance : 0.0f;
    south = cam->distance - height / tanf(pitch + half);
    side = cam->distance / cosf(pitch) * tanf(half) * VOXEL_ASPECT;

    *x = CtrFieldView_Limit(*x, side, side, (float)map->originX, (float)(map->originX + map->width), closed & 3);
    *z = CtrFieldView_Limit(*z, north, south, (float)map->originY + VoxelRelief_Base(map),
                            (float)(map->originY + map->height) + VoxelRelief_Base(map),
                            (closed >> 2) & 3);
    *x = Between(*x, playerX - VOXEL_EDGE_SIDE * cam->distance, playerX + VOXEL_EDGE_SIDE * cam->distance);
    *z = Between(*z, playerZ - VOXEL_EDGE_DOWN * cam->distance, playerZ + VOXEL_EDGE_UP * cam->distance);
}

static void Aim(const VoxelCamera *cam, float playerWorldX, float playerWorldZ, float *x, float *z)
{
    *x = playerWorldX + VOXEL_TILE_MIDDLE;
    *z = playerWorldZ;
    StopAtEdges(cam, x, z);
    CtrFieldView_SetVoxelShift((int32_t)floorf(*x - playerWorldX - VOXEL_TILE_MIDDLE + 0.5f),
                               (int32_t)floorf(*z - playerWorldZ + 0.5f));
}

void VoxelCamera_Snap(VoxelCamera *cam, float playerWorldX, float playerWorldZ)
{
    cam->swoopX = cam->swoopZ = 0.0f;
    AdaptDistance(cam);
    Aim(cam, playerWorldX, playerWorldZ, &cam->targetX, &cam->targetZ);
    Place(cam);
}

void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldZ, float dt)
{
    float x, z;
    const float k = FollowAlpha(dt);

    cam->targetX -= cam->swoopX;
    cam->targetZ -= cam->swoopZ;
    cam->swoopX = cam->swoopZ = 0.0f;
    AdaptDistance(cam);
    Aim(cam, playerWorldX, playerWorldZ, &x, &z);
    cam->targetX += (x - cam->targetX) * k;
    cam->targetZ += (z - cam->targetZ) * k;
    Place(cam);
}

void VoxelCamera_Shift(VoxelCamera *cam, float dx, float dz)
{
    cam->targetX += dx;
    cam->targetZ += dz;
    Place(cam);
}

void VoxelCamera_Swoop(VoxelCamera *cam, float t)
{
    float pitch0 = cam->pitch, fov0 = VOXEL_CAMERA_FOV;
    float length0 = cam->distance / cosf(pitch0 * VOXEL_DEG_TO_RAD);
    float length1 = VOXEL_SWOOP_HALF_TILES * sinf(VOXEL_SWOOP_PITCH * VOXEL_DEG_TO_RAD)
                  / tanf(fov0 * 0.5f * VOXEL_DEG_TO_RAD);
    float pitch, length;

    if (t <= 0.0f)
    {
        cam->fov = fov0;
        cam->lay = 0.0f;
        return;
    }
    if (t > 1.0f) t = 1.0f;
    cam->lay = t;
    pitch = pitch0 + (VOXEL_SWOOP_PITCH - pitch0) * t;
    length = length0 + (length1 - length0) * t;
    cam->fov = fov0;
    cam->pitch = pitch;
    cam->distance = length * cosf(pitch * VOXEL_DEG_TO_RAD);
    cam->swoopX = VOXEL_SWOOP_SHIFT_X * t;
    cam->swoopZ = VOXEL_SWOOP_SHIFT_Z * t;
    cam->targetX += cam->swoopX;
    cam->targetZ += cam->swoopZ;
    Place(cam);
}

void VoxelCamera_Frame(VoxelCamera *cam, float targetX, float targetZ, float ground,
                       float pitch, float distance)
{
    cam->targetX = targetX;
    cam->targetZ = targetZ;
    cam->ground = ground;
    cam->pitch = pitch;
    cam->distance = distance;
    cam->lay = 0.0f;
    Place(cam);
}
