/*
 * The 3D battle: the voxel world drawn as a battle's scenery.
 *
 * Phase F5a provides Core 1 capture + Core 0 minimal stage/shadow queries; full
 * 3D battle stage placement and battler shadows land in Phase F5b.
 *
 * Origin: derived from pokeemerald-vita-3ds (MIT); see NOTICE.md.
 */
#ifndef CTR_VOXEL_BATTLE_H
#define CTR_VOXEL_BATTLE_H

#include <stdbool.h>
#include <stdint.h>

#define VOXEL_BATTLE_PITCH    30.0f
#define VOXEL_BATTLE_DISTANCE 7.0f
#define VOXEL_BATTLE_FOV      35.0f

bool VoxelBattle_GameInBattle(void);
bool VoxelBattle_IsLink(void);

void VoxelBattle_BeginStage(void);
bool VoxelBattle_StepStage(uint32_t cells, uint32_t candidates, float *targetX, float *targetZ,
                           float *ground);

typedef struct
{
    float x, y, rx, ry;
} VoxelBattleShadow;

uint32_t VoxelBattle_Shadows(VoxelBattleShadow *out, uint32_t max);

#endif // CTR_VOXEL_BATTLE_H
