#ifndef VOXEL_GRADE_H
#define VOXEL_GRADE_H

#include <stdbool.h>
#include <stdint.h>

void VoxelGrade_Init(void);
uint16_t VoxelGrade_RGBA5551(uint16_t bgr15);
void VoxelGrade_Texels(uint16_t *texels, uint32_t count);
void VoxelGrade_Brighten(uint16_t *texels, uint32_t count, float factor);
void VoxelGrade_SetReliefEnabled(bool enabled);
bool VoxelGrade_ReliefEnabled(void);
void VoxelGrade_ApplyTileRelief16x16(uint16_t *dest, uint32_t stride, uint32_t height,
                                     uint32_t baseX, uint32_t baseY);
void VoxelGrade_ApplyRectRelief(uint16_t *buf, uint32_t stride, uint32_t width, uint32_t height);

#endif
