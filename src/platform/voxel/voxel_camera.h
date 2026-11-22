#ifndef CTR_VOXEL_CAMERA_H
#define CTR_VOXEL_CAMERA_H
#include <stdint.h>

#define VOXEL_SWOOP_PITCH 45.0f
#define VOXEL_SWOOP_HEIGHT 0.55f

typedef struct
{
    float x, y, z;
    float targetX, targetY, targetZ;
    float pitch;
    float yaw;
    float distance;
    float fov;
    float ground;
    float swoopX, swoopZ;
    float lay;
} VoxelCamera;

void VoxelCamera_Init(VoxelCamera *cam);
void VoxelCamera_Snap(VoxelCamera *cam, float playerWorldX, float playerWorldZ);
void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldZ, float dt);
void VoxelCamera_SetGround(VoxelCamera *cam, float ground, int32_t snap, float dt);
void VoxelCamera_Shift(VoxelCamera *cam, float dx, float dz);
void VoxelCamera_Frame(VoxelCamera *cam, float targetX, float targetZ, float ground,
                       float pitch, float distance);
void VoxelCamera_Swoop(VoxelCamera *cam, float t);

#endif
