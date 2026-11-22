/*
 * voxel_tuning.h - the voxel renderer's tuning values in one place.
 */
#ifndef VOXEL_TUNING_H
#define VOXEL_TUNING_H

/* ---- Procedural camera frustum & chunk reach (tiles) ---------------------------------- */

#define VOXEL_VIEW_REACH_MAX 48.0f
#define VOXEL_VIEW_SLACK_TILES 6.0f
#define VOXEL_FRUSTUM_PLANE_SLACK 3.5f
#define VOXEL_VIEW_NEAR_MIN 1.0f
#define VOXEL_VIEW_NEAR_MAX 3.5f
#define VOXEL_VIEW_FAR_MIN 64.0f
#define VOXEL_VIEW_FAR_MAX 200.0f
#define VOXEL_VIEW_FAR_MARGIN 16.0f

/* ---- Follow camera & 3D Fly cinematic (tiles / degrees / seconds) --------------------- */

#define VOXEL_FOLLOW_TAU 0.09f
#define VOXEL_FOLLOW_DT_MAX 0.10f
#define VOXEL_FLY_PITCH 62.0f
#define VOXEL_FLY_DISTANCE 24.0f
#define VOXEL_FLY_RISE 22.0f
#define VOXEL_FLY_LEAD 8.0f

/* ---- Chunk builds and pre-warming (RENDERING.md, step 5) ------------------------------ */

#define VOXEL_CHUNK_WORKER 1
#define VOXEL_BUILD_TAIL_MS 6.0
#define VOXEL_BUILD_COST_INIT_MS 8.0
#define VOXEL_HOLE_BUILDS_PER_FRAME 1
#define VOXEL_PREFETCH_MAX 48
#define VOXEL_PREFETCH_LOOKAHEAD 12.0f
#define VOXEL_PREFETCH_SPEED_GAIN 45.0f
#define VOXEL_PREFETCH_LOOKAHEAD_MAX 22.0f
#define VOXEL_FAST_MOVE_SPEED 0.16f
#define VOXEL_EPOCH_HASH_BUDGET 8
#define VOXEL_VIEW_AHEAD_TILES 8.0f
#define VOXEL_MOVE_THRESHOLD 0.02f
#define VOXEL_STALE_MAX 32

/* ---- Atlas, VBOs and textures rewritten at run time (RENDERING.md) -------------------- */

#define VOXEL_ATLAS_EXTEND_BUDGET_MS 4.0
#define VOXEL_ATLAS_EXTEND_STEP_METATILES 64u
#define VOXEL_NEIGHBOR_MARGIN_TILES 8
#define VOXEL_VITAGL_PURGE_FRAMES 4u

/* ---- Map crossings -------------------------------------------------------------------- */

#define VOXEL_CROSSING_STEP 16.0f

/* ---- Colour grade and haze ------------------------------------------------------------ */

#define VOXEL_GRADE_SHADE_LOW 0.62f
#define VOXEL_HAZE_START 1.05f
#define VOXEL_HAZE_RAMP 0.60f
#define VOXEL_HAZE_MAX 0.14f
#define VOXEL_HAZE_COLOUR 0xFFFAE6D2u
#define VOXEL_TREE_BRIGHTNESS 1.12f

/* ---- Night building windows (Pass_BuildingGlow, VoxelBuildings_ReadGlow) -------------- */

#define VOXEL_WINDOW_GLOW_R 1.00f
#define VOXEL_WINDOW_GLOW_G 0.80f
#define VOXEL_WINDOW_GLOW_B 0.38f
#define VOXEL_WINDOW_GLOW_STRENGTH_STD  0.90f
#define VOXEL_WINDOW_GLOW_STRENGTH_HD2D 0.68f

/* ---- Night lamps (Pass_NightLamps, UpdatePointLights, VoxelLamps_EmitChunk) ----------- */

#define VOXEL_LAMP_R 1.00f
#define VOXEL_LAMP_G 0.75f
#define VOXEL_LAMP_B 0.42f
#define VOXEL_LAMP_GLASS_GAIN 1.00f
#define VOXEL_LAMP_POOL_GAIN 0.80f
#define VOXEL_LAMP_POOL_LIFT 0.02f
#define VOXEL_LAMP_HALO_LIFT 0.30f
#define VOXEL_LAMP_LIGHT_RADIUS 3.5f
#define VOXEL_LAMP_LIGHT_GAIN 0.85f
#define VOXEL_LAMP_HALO_MAX 64

/* ---- Fog and sand banks --------------------------------------------------------------- */

#define VOXEL_FOG_DIM 64
#define VOXEL_FOG_SPAN_X 24.0f
#define VOXEL_FOG_SOUTH 12.0f
#define VOXEL_FOG_NORTH 44.0f
#define VOXEL_FOG_PERIOD_X 18.0f
#define VOXEL_FOG_PERIOD_Z 12.0f
#define VOXEL_FOG_GRID 12
#define VOXEL_FOG_BANK_VERTICES (VOXEL_FOG_GRID * VOXEL_FOG_GRID * 6)

/* ---- Weather particles ---------------------------------------------------------------- */

#define VOXEL_WEATHER_COUNT 256
#define VOXEL_WEATHER_SPAN_X 24.0f
#define VOXEL_WEATHER_SPAN_Y 8.5f
#define VOXEL_WEATHER_SPAN_Z 28.0f
#define VOXEL_WEATHER_LOW -0.4f
#define VOXEL_WEATHER_NORTH 20.0f
#define VOXEL_WEATHER_FADE_XZ 3.0f
#define VOXEL_WEATHER_FADE_Y 1.2f

/* ---- Dust motes ----------------------------------------------------------------------- */

#define VOXEL_MOTE_COUNT 32
#define VOXEL_MOTE_SPAN_X 22.0f
#define VOXEL_MOTE_SPAN_Z 18.0f
#define VOXEL_MOTE_ALPHA 0.85f
#define VOXEL_MOTE_RADIUS 0.062f

/* ---- Depth of field: blurred bands at 544 rows ---------------------------------------- */

#define VOXEL_DOF_TOP_ROWS 150.0f
#define VOXEL_DOF_BOTTOM_ROWS 84.0f
#define VOXEL_DOF_TOP_ALPHA 0.60f
#define VOXEL_DOF_BOTTOM_ALPHA 0.50f

/* ---- Sun rays ------------------------------------------------------------------------- */

#define VOXEL_RAYS_OPACITY 0.75f

#endif /* VOXEL_TUNING_H */
