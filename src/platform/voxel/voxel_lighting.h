/* Clock-driven sun, baked into chunk colours; no extra terrain GPU pass. */
#ifndef CTR_VOXEL_LIGHTING_H
#define CTR_VOXEL_LIGHTING_H

#include <stdint.h>
#include "voxel_mesh_builder.h"

#ifndef CTR_VOXEL_LIGHTING
#define CTR_VOXEL_LIGHTING 1
#endif

#if CTR_VOXEL_LIGHTING
typedef struct
{
    float dx, dz, strength;
    uint32_t key;
} VoxelSun;
extern VoxelSun gVoxelSun;
#define VOXEL_SUN_DX     (gVoxelSun.dx)
#define VOXEL_SUN_DZ     (gVoxelSun.dz)
#define VOXEL_LIGHT_REACH 8
#define VOXEL_AMBIENT    0.70f

void VoxelLighting_Reset(void);
bool VoxelLighting_SetSun(float dx, float dz, float strength, uint32_t key);
uint32_t VoxelLighting_Hash(int32_t x0, int32_t z0, int32_t x1, int32_t z1);
float VoxelLighting_Sample(float x, float y, float z);
float VoxelLighting_SampleAlone(float x, float y, float z);
bool VoxelLighting_Warm(int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t *cursor, uint32_t budget);
uint32_t VoxelLighting_Rays(void);
uint32_t VoxelLighting_Steps(void);
uint32_t VoxelLighting_SurfaceTests(void);

typedef struct
{
    uint32_t rays;
    uint32_t steps;
    uint32_t cells;
    uint32_t blocks;
    double rayMs;
    double contactMs;
    double apiMs;
} VoxelLightingWork;
void VoxelLighting_GetWork(VoxelLightingWork *work);

float VoxelLighting_Face(float nx, float ny, float nz);
void VoxelLighting_Quad(VoxelBuilder *builder, const VoxelVertex *a,
                        const VoxelVertex *b, const VoxelVertex *c,
                        const VoxelVertex *d);
void VoxelLighting_Lattice(VoxelBuilder *builder, const VoxelVertex *lattice);
void VoxelLighting_LatticeShades(VoxelBuilder *builder, const VoxelVertex *lattice, float *shade);
void VoxelLighting_Tri(VoxelBuilder *builder, const VoxelVertex *a,
                       const VoxelVertex *b, const VoxelVertex *c);
float VoxelLighting_ModelShade(const VoxelVertex *a, const VoxelVertex *b,
                               const VoxelVertex *c, float drawnShade);
void VoxelLighting_ModelTri(VoxelBuilder *builder, const VoxelVertex *a,
                            const VoxelVertex *b, const VoxelVertex *c, float drawnShade);
#endif

#endif
