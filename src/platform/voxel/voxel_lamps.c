#include "voxel_lamps.h"

#include <stddef.h>

#include "constants/layouts.h"
#include "voxel_relief.h"
#include "voxel_sign.h"

/*
 * Night lights of one chunk: street lamps from `signposts.bin` plus Kanto-specific fixed beacon
 * coordinates around key landmarks and towns.
 */
typedef struct
{
    uint16_t layout;
    uint16_t x, y;
    float dx, dz, height;
} FixedLight;

static const FixedLight sFixedLights[] = {
    /* Vermilion Harbor pier canopies & Pokecenter */
    { LAYOUT_VERMILION_CITY, 22, 34, 0.5f, 0.50f, 1.35f },
    { LAYOUT_VERMILION_CITY, 24, 34, 0.5f, 0.50f, 1.35f },
    { LAYOUT_VERMILION_CITY, 36, 22, 0.5f, 0.50f, 1.35f },
    { LAYOUT_VERMILION_CITY, 15,  6, 0.5f, 0.85f, 1.15f },
    /* Route 25: Bill's Sea Cottage porch */
    { LAYOUT_ROUTE25,        51,  4, 0.5f, 0.85f, 1.25f },
    /* Saffron City: Silph Co. plaza & Pokecenter */
    { LAYOUT_SAFFRON_CITY,   30, 30, 0.5f, 0.75f, 1.40f },
    { LAYOUT_SAFFRON_CITY,   36, 30, 0.5f, 0.75f, 1.40f },
    { LAYOUT_SAFFRON_CITY,   24, 38, 0.5f, 0.85f, 1.15f },
    /* Cinnabar Island: Pokemon Mansion, Lab & Pokecenter */
    { LAYOUT_CINNABAR_ISLAND,  8,  3, 0.5f, 0.85f, 1.30f },
    { LAYOUT_CINNABAR_ISLAND,  8,  9, 0.5f, 0.85f, 1.20f },
    { LAYOUT_CINNABAR_ISLAND, 14, 11, 0.5f, 0.85f, 1.15f },
    /* Pallet Town, Viridian, Cerulean, Lavender, Celadon landmarks */
    { LAYOUT_PALLET_TOWN,      6,  7, 0.5f, 0.85f, 1.15f },
    { LAYOUT_PALLET_TOWN,     15,  7, 0.5f, 0.85f, 1.15f },
    { LAYOUT_PALLET_TOWN,     16, 13, 0.5f, 0.85f, 1.15f },
    { LAYOUT_VIRIDIAN_CITY,   26, 26, 0.5f, 0.85f, 1.15f },
    { LAYOUT_VIRIDIAN_CITY,   36, 19, 0.5f, 0.85f, 1.15f },
    { LAYOUT_VIRIDIAN_CITY,   36, 10, 0.5f, 0.85f, 1.15f },
    { LAYOUT_CERULEAN_CITY,   22, 19, 0.5f, 0.85f, 1.15f },
    { LAYOUT_CERULEAN_CITY,   13, 28, 0.5f, 0.85f, 1.15f },
    { LAYOUT_CERULEAN_CITY,   31, 21, 0.5f, 0.85f, 1.15f },
    { LAYOUT_LAVENDER_TOWN,   18,  6, 0.5f, 0.85f, 1.30f },
    { LAYOUT_LAVENDER_TOWN,    6,  5, 0.5f, 0.85f, 1.15f },
    { LAYOUT_CELADON_CITY,    11, 14, 0.5f, 0.85f, 1.30f },
    { LAYOUT_CELADON_CITY,    15, 14, 0.5f, 0.85f, 1.30f },
    { LAYOUT_CELADON_CITY,    34, 21, 0.5f, 0.85f, 1.25f },
    { LAYOUT_CELADON_CITY,    48, 11, 0.5f, 0.85f, 1.15f },
    /* Pewter City: Museum, Gym, Mart, Pokecenter */
    { LAYOUT_PEWTER_CITY,     17,  6, 0.5f, 0.85f, 1.30f },
    { LAYOUT_PEWTER_CITY,     15, 16, 0.5f, 0.85f, 1.25f },
    { LAYOUT_PEWTER_CITY,     28, 18, 0.5f, 0.85f, 1.15f },
    { LAYOUT_PEWTER_CITY,     17, 25, 0.5f, 0.85f, 1.15f },
    /* Fuchsia City: Safari Zone entrance, Mart, Gym */
    { LAYOUT_FUCHSIA_CITY,    24,  5, 0.5f, 0.85f, 1.30f },
    { LAYOUT_FUCHSIA_CITY,    11, 15, 0.5f, 0.85f, 1.15f },
    { LAYOUT_FUCHSIA_CITY,     9, 32, 0.5f, 0.85f, 1.25f },
    /* Indigo Plateau: Pokemon League entrance */
    { LAYOUT_INDIGO_PLATEAU_EXTERIOR, 11, 6, 0.5f, 0.85f, 1.35f },
    /* Sevii Islands 1-7: Pokecenters, Marts & Harbors */
    { LAYOUT_ONE_ISLAND,      14,  5, 0.5f, 0.85f, 1.15f },
    { LAYOUT_ONE_ISLAND,      12, 18, 0.5f, 0.85f, 1.25f },
    { LAYOUT_TWO_ISLAND,      21,  7, 0.5f, 0.85f, 1.15f },
    { LAYOUT_TWO_ISLAND,      39,  9, 0.5f, 0.85f, 1.25f },
    { LAYOUT_THREE_ISLAND,    14, 27, 0.5f, 0.85f, 1.15f },
    { LAYOUT_THREE_ISLAND,    18, 12, 0.5f, 0.85f, 1.15f },
    { LAYOUT_FOUR_ISLAND,     18, 20, 0.5f, 0.85f, 1.15f },
    { LAYOUT_FOUR_ISLAND,     12, 13, 0.5f, 0.85f, 1.15f },
    { LAYOUT_FIVE_ISLAND,     18,  6, 0.5f, 0.85f, 1.15f },
    { LAYOUT_FIVE_ISLAND,     12, 14, 0.5f, 0.85f, 1.25f },
    { LAYOUT_SIX_ISLAND,      11, 11, 0.5f, 0.85f, 1.15f },
    { LAYOUT_SIX_ISLAND,      20, 11, 0.5f, 0.85f, 1.15f },
    { LAYOUT_SEVEN_ISLAND,    12,  3, 0.5f, 0.85f, 1.15f },
    { LAYOUT_SEVEN_ISLAND,     5,  9, 0.5f, 0.85f, 1.15f },
};

static uint32_t EmitFixedLights(const VoxelBuilder *place, VoxelBuilder *pool, const VoxelMapInstance *inst,
                                int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                float (*lights)[3], uint32_t lightCapacity)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < sizeof(sFixedLights) / sizeof(sFixedLights[0]) && count < lightCapacity; ++i)
    {
        const FixedLight *const light = &sFixedLights[i];
        if (light->layout == 0u || light->layout != inst->layoutId)
            continue;
        const int32_t wx = inst->originX + light->x;
        const int32_t wz = inst->originY + light->y;
        if (wx < x0 || wx >= x1 || wz < y0 || wz >= y1)
            continue;
        VoxelBuilder at = *place;
        at.base = VoxelRelief_Base(inst);
        at.lift = VoxelRelief_CellLift(inst, wx, wz);
        at.shift = VoxelRelief_CellShift(inst, wx, wz);
        float x = (float)wx + light->dx;
        float y = light->height;
        float z = (float)wz + light->dz;
        VoxelSign_EmitLampPool(pool, inst, x, z);
        VoxelBuilder_Place(&at, &x, &y, &z);
        lights[count][0] = x;
        lights[count][1] = y;
        lights[count][2] = z;
        ++count;
    }
    return count;
}

uint32_t VoxelLamps_EmitChunk(VoxelBuilder *glass, VoxelBuilder *pool, const VoxelMapInstance *inst,
                              int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              float (*lights)[3], uint32_t lightCapacity)
{
    uint32_t count;

    if (glass == NULL || pool == NULL || inst == NULL || inst->indoor)
        return 0;
    count = VoxelSign_EmitNight(glass, pool, inst, x0, y0, x1, y1, lights, lightCapacity);
    return count + EmitFixedLights(glass, pool, inst, x0, y0, x1, y1, lights + count, lightCapacity - count);
}
