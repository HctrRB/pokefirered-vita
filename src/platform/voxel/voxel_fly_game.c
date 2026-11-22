/*
 * voxel_fly_game.c - Core 1 owner & Core 0 snapshot bridge for Kanto/Sevii 3D Fly.
 *
 * Origin: derived from pokeemerald-vita-3ds (src/platform/voxel/voxel_fly_game.c,
 * MIT) and adapted to FireRed Kanto/Sevii region map coordinates; see NOTICE.md.
 */
#include "global.h"
#include "constants/field_effects.h"
#include "constants/region_map_sections.h"
#include "field_effect.h"
/* Upstream include/overworld.h declares `*const` return qualifiers ignored under -Wextra. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "overworld.h"
#pragma GCC diagnostic pop
#include "platform/vita_extras.h"
#include "region_map.h"

#include "voxel_fly_game.h"
#include "voxel_world.h"

#define VOXEL_FLY_SNAP_SLOTS 3

/* Core 1 state */
static VoxelFly sFly;
static bool sFlyAsked;
static uint32_t sWarpVersion = 1u;
static uint32_t sFlyStartWarpVersion = 0u;

/* Core 1 -> Core 0 triple-buffered snapshot */
static VoxelFlySnapshot sSnapshots[VOXEL_FLY_SNAP_SLOTS];
static volatile int32_t sSnapIdx = 0;

/* Core 0 -> Core 1 atomic feedback: last warpVersion drawn in 3D */
static volatile uint32_t sDrawnWarpVersion = 0u;

static bool VoxelFieldReady(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0
        && VoxelWorld_FieldShownLive();
}

static void FlyDirection(float *dirX, float *dirZ)
{
    const struct MapHeader *const to = GetDestinationWarpMapHeader();
    const u8 from = gMapHeader.regionMapSectionId;
    const u8 dest = (to != NULL) ? to->regionMapSectionId : (u8)MAPSEC_NONE;
    float fx = 0.0f, fy = 0.0f, tx = 0.0f, ty = 0.0f;

    *dirX = 0.0f;
    *dirZ = -1.0f;
    if (!RegionMap_GetMapSectionCenter(from, &fx, &fy)
     || !RegionMap_GetMapSectionCenter(dest, &tx, &ty))
        return;

    *dirX = tx - fx;
    *dirZ = ty - fy;
}

static void PublishSnapshot(void)
{
    const int32_t cur = __atomic_load_n(&sSnapIdx, __ATOMIC_RELAXED);
    const int32_t next = (cur + 1 >= VOXEL_FLY_SNAP_SLOTS) ? 0 : (cur + 1);
    VoxelFlySnapshot *const dst = &sSnapshots[next];

    dst->active = VoxelFly_Active(&sFly);
    dst->warpVersion = sWarpVersion;
    VoxelFly_Frame(&sFly, &dst->frame);
    __atomic_store_n(&sSnapIdx, next, __ATOMIC_RELEASE);
}

bool8 Port_FlyHover(void)
{
    if (!FieldEffectActiveListContains(FLDEFF_FLY_OUT)
     || FieldEffectActiveListContains(FLDEFF_FLY_IN))
        return FALSE;

    if (!sFlyAsked)
    {
        float dirX = 0.0f, dirZ = -1.0f;

        sFlyAsked = true;
        if (VoxelFly_Active(&sFly) || !VoxelFieldReady())
            return FALSE;

        FlyDirection(&dirX, &dirZ);
        sFlyStartWarpVersion = sWarpVersion;
        VoxelFly_Begin(&sFly, dirX, dirZ);
        PublishSnapshot();
    }
    return VoxelFly_Hovers(&sFly) ? TRUE : FALSE;
}

bool8 Port_FlyBusy(void)
{
    return VoxelFly_Busy(&sFly) ? TRUE : FALSE;
}

void VoxelFlyGame_OnWarpLoaded(void)
{
    sWarpVersion++;
    if (sWarpVersion == 0u)
        sWarpVersion = 1u;
}

void VoxelFlyGame_Tick(void)
{
    if (!FieldEffectActiveListContains(FLDEFF_FLY_OUT))
        sFlyAsked = false;

    if (VoxelFly_Active(&sFly))
    {
        const bool voxelEnabled = (VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0);
        const bool voxelAvail = voxelEnabled && VoxelWorld_FieldShownLive();

        if (!voxelEnabled || (VoxelFly_Hovers(&sFly) && !voxelAvail))
        {
            VoxelFly_Cancel(&sFly);
        }
        else
        {
            const bool warped = (sWarpVersion != sFlyStartWarpVersion);
            const bool gone = warped || !voxelAvail;
            const uint32_t drawnWarp = __atomic_load_n(&sDrawnWarpVersion, __ATOMIC_ACQUIRE);
            const bool fieldShown = warped && voxelAvail && (drawnWarp == sWarpVersion);

            VoxelFly_Step(&sFly, fieldShown, gone);
        }
    }

    PublishSnapshot();
}

void VoxelFlyGame_OnReset(void)
{
    VoxelFly_Cancel(&sFly);
    sFlyAsked = false;
    sWarpVersion = 1u;
    sFlyStartWarpVersion = 0u;
    __atomic_store_n(&sDrawnWarpVersion, 0u, __ATOMIC_RELEASE);
    PublishSnapshot();
}

const VoxelFlySnapshot *VoxelFly_GetSnapshot(void)
{
    const int32_t idx = __atomic_load_n(&sSnapIdx, __ATOMIC_ACQUIRE);
    return &sSnapshots[idx];
}

void VoxelFly_NotifyFrameDrawn(uint32_t warpVersion)
{
    if (warpVersion != 0u)
        __atomic_store_n(&sDrawnWarpVersion, warpVersion, __ATOMIC_RELEASE);
}
