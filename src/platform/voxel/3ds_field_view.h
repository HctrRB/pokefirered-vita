#ifndef CTR_FIELD_VIEW_H
#define CTR_FIELD_VIEW_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    float north;
    float south;
    float farHalfWidth;
    float nearHalfWidth;
    int32_t shiftX, shiftY;
} CtrFieldView;

#define CTR_FIELD_EDGE_LOW 1u
#define CTR_FIELD_EDGE_HIGH 2u
#define CTR_FIELD_EDGE_WEST CTR_FIELD_EDGE_LOW
#define CTR_FIELD_EDGE_EAST CTR_FIELD_EDGE_HIGH
#define CTR_FIELD_EDGE_NORTH (CTR_FIELD_EDGE_LOW << 2)
#define CTR_FIELD_EDGE_SOUTH (CTR_FIELD_EDGE_HIGH << 2)

void CtrFieldView_Flat(CtrFieldView *view);
void CtrFieldView_Voxel(CtrFieldView *view, float pitchDegrees, float zoomPercent);
float CtrFieldView_Outside(const CtrFieldView *view, int32_t dx, int32_t dy);
float CtrFieldView_Score(const CtrFieldView *view, int32_t dx, int32_t dy);
void CtrFieldView_Window(const CtrFieldView *view, int32_t margin, int32_t *west, int32_t *east, int32_t *north, int32_t *south);
float CtrFieldView_Limit(float at, float before, float after, float low, float high, uint32_t closed);
const CtrFieldView *CtrFieldView_Current(void);
uint32_t CtrFieldView_Generation(void);
void CtrFieldView_SetFlatShift(int32_t x, int32_t y);
void CtrFieldView_SetVoxelShift(int32_t x, int32_t y);

#endif // CTR_FIELD_VIEW_H
