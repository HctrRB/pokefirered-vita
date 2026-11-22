#ifndef GUARD_VITA_ENCOUNTERS_H
#define GUARD_VITA_ENCOUNTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    VITA_TIME_MORNING,
    VITA_TIME_DAY,
    VITA_TIME_NIGHT,
    VITA_TIME_COUNT
};

typedef struct
{
    uint8_t mapGroup;
    uint8_t mapNum;
    uint8_t slot;
    uint16_t expectedSpecies;
    uint16_t species[VITA_TIME_COUNT];
} VitaTimedEncounterRule;

uint32_t VitaEncounters_Phase(uint32_t seconds);

uint16_t VitaEncounters_Resolve(const VitaTimedEncounterRule *rules, size_t count,
                                uint32_t mapGroup, uint32_t mapNum, uint32_t slot,
                                uint16_t original, uint32_t phase);

struct WildPokemonInfo;
const struct WildPokemonInfo *VitaEncounters_Land(const struct WildPokemonInfo *original,
                                                  uint32_t mapGroup, uint32_t mapNum);

const struct WildPokemonInfo *VitaEncounters_LandPhase(const struct WildPokemonInfo *original,
                                                       uint32_t mapGroup, uint32_t mapNum, uint32_t phase);

#endif // GUARD_VITA_ENCOUNTERS_H
