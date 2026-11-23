#include <stdint.h>
#include "global.h"
#include "wild_encounter.h"
#include "constants/maps.h"
#include "constants/species.h"
#include "platform/vita_daynight.h"
#include "platform/vita_encounters.h"
#include "platform/vita_extras.h"

#define ENCOUNTER_RULE(route, slotIndex, expected, morning, day, night) \
    { \
        (uint8_t)MAP_GROUP(MAP_##route), \
        (uint8_t)MAP_NUM(MAP_##route), \
        (uint8_t)(slotIndex), \
        (uint16_t)SPECIES_##expected, \
        { (uint16_t)SPECIES_##morning, (uint16_t)SPECIES_##day, (uint16_t)SPECIES_##night } \
    }

// Complete Kanto + Safari Zone + Sevii Islands outdoor time-of-day land encounter overrides.
// Expected species match src/data/wild_encounters.h slots so rules never corrupt unrelated tables.
static const VitaTimedEncounterRule sRules[] = {
    // ---- Kanto Routes & Forests ----
    // Route 1
    ENCOUNTER_RULE(ROUTE1, 0, RATTATA,   PIDGEY,    RATTATA,   RATTATA),
    ENCOUNTER_RULE(ROUTE1, 1, PIDGEY,    PIDGEY,    PIDGEY,    HOOTHOOT),
    ENCOUNTER_RULE(ROUTE1, 2, SENTRET,   SENTRET,   SENTRET,   POOCHYENA),
    ENCOUNTER_RULE(ROUTE1, 4, HOOTHOOT,  LEDYBA,    PIDGEY,    HOOTHOOT),
    ENCOUNTER_RULE(ROUTE1, 5, POOCHYENA, SENTRET,   ZIGZAGOON, POOCHYENA),

    // Route 2
    ENCOUNTER_RULE(ROUTE2, 0, RATTATA,   LEDYBA,    RATTATA,   SPINARAK),
    ENCOUNTER_RULE(ROUTE2, 2, PIDGEY,    PIDGEY,    PIDGEY,    HOOTHOOT),
    ENCOUNTER_RULE(ROUTE2, 4, LEDYBA,    LEDYBA,    CATERPIE,  SPINARAK),
    ENCOUNTER_RULE(ROUTE2, 6, RATTATA,   PIDGEY,    RATTATA,   ZUBAT),

    // Viridian Forest
    ENCOUNTER_RULE(VIRIDIAN_FOREST, 0, CATERPIE,  LEDYBA,    CATERPIE,  SPINARAK),
    ENCOUNTER_RULE(VIRIDIAN_FOREST, 2, SPINARAK,  LEDYBA,    WEEDLE,    SPINARAK),
    ENCOUNTER_RULE(VIRIDIAN_FOREST, 5, WURMPLE,   WURMPLE,   WURMPLE,   ODDISH),
    ENCOUNTER_RULE(VIRIDIAN_FOREST, 7, LEDYBA,    LEDYBA,    CATERPIE,  HOOTHOOT),

    // Route 3
    ENCOUNTER_RULE(ROUTE3, 0, SPEAROW,   SPEAROW,   SPEAROW,   ZUBAT),
    ENCOUNTER_RULE(ROUTE3, 1, JIGGLYPUFF,PIDGEY,    JIGGLYPUFF,JIGGLYPUFF),
    ENCOUNTER_RULE(ROUTE3, 4, TAILLOW,   TAILLOW,   SPEAROW,   HOOTHOOT),

    // Route 4
    ENCOUNTER_RULE(ROUTE4, 0, SPEAROW,   SPEAROW,   SPEAROW,   ZUBAT),
    ENCOUNTER_RULE(ROUTE4, 2, RATTATA,   MANKEY,    RATTATA,   EKANS),

    // Route 5
    ENCOUNTER_RULE(ROUTE5, 0, ODDISH,    PIDGEY,    BELLSPROUT,ODDISH),
    ENCOUNTER_RULE(ROUTE5, 3, PIDGEY,    PIDGEY,    PIDGEY,    MEOWTH),

    // Route 6
    ENCOUNTER_RULE(ROUTE6, 0, ODDISH,    PIDGEY,    BELLSPROUT,ODDISH),
    ENCOUNTER_RULE(ROUTE6, 3, PIDGEY,    PIDGEY,    PIDGEY,    MEOWTH),

    // Route 7
    ENCOUNTER_RULE(ROUTE7, 0, PIDGEY,    PIDGEY,    PIDGEY,    MURKROW),
    ENCOUNTER_RULE(ROUTE7, 1, MEOWTH,    VULPIX,    MEOWTH,    MEOWTH),
    ENCOUNTER_RULE(ROUTE7, 4, MURKROW,   PIDGEY,    SNUBBULL,  MURKROW),

    // Route 8
    ENCOUNTER_RULE(ROUTE8, 0, PIDGEY,    PIDGEY,    PIDGEY,    HOUNDOUR),
    ENCOUNTER_RULE(ROUTE8, 1, MEOWTH,    GROWLITHE, MEOWTH,    MEOWTH),
    ENCOUNTER_RULE(ROUTE8, 6, HOUNDOUR,  GROWLITHE, VULPIX,    HOUNDOUR),

    // Route 9
    ENCOUNTER_RULE(ROUTE9, 0, SANDSHREW, SPEAROW,   SANDSHREW, GLIGAR),
    ENCOUNTER_RULE(ROUTE9, 5, SPEAROW,   SPEAROW,   SPEAROW,   ZUBAT),

    // Route 10
    ENCOUNTER_RULE(ROUTE10, 1, SPEAROW,  SPEAROW,   SPEAROW,   ZUBAT),
    ENCOUNTER_RULE(ROUTE10, 3, VOLTORB,  MAREEP,    VOLTORB,   VOLTORB),

    // Route 11
    ENCOUNTER_RULE(ROUTE11, 3, DROWZEE,  SPEAROW,   DROWZEE,   DROWZEE),
    ENCOUNTER_RULE(ROUTE11, 4, SPEAROW,  SPEAROW,   SPEAROW,   DROWZEE),

    // Route 12
    ENCOUNTER_RULE(ROUTE12, 0, PIDGEOTTO,PIDGEOTTO, PIDGEOTTO, GLOOM),
    ENCOUNTER_RULE(ROUTE12, 3, VENONAT,  FARFETCHD, WEEPINBELL,VENONAT),

    // Route 13
    ENCOUNTER_RULE(ROUTE13, 0, GLOOM,    PIDGEOTTO, SUNKERN,   GLOOM),
    ENCOUNTER_RULE(ROUTE13, 1, VENONAT,  FARFETCHD, ROSELIA,   VENONAT),

    // Route 14
    ENCOUNTER_RULE(ROUTE14, 0, GLOOM,    JUMPLUFF,  WEEPINBELL,GLOOM),
    ENCOUNTER_RULE(ROUTE14, 2, VENONAT,  YANMA,     NATU,      VENONAT),

    // Route 15
    ENCOUNTER_RULE(ROUTE15, 0, GLOOM,    MILTANK,   WEEPINBELL,GLOOM),
    ENCOUNTER_RULE(ROUTE15, 2, VENONAT,  SPINDA,    NUZLEAF,   VENONAT),

    // Route 16
    ENCOUNTER_RULE(ROUTE16, 2, TAILLOW,  TAILLOW,   DODUO,     MURKROW),
    ENCOUNTER_RULE(ROUTE16, 5, SPEAROW,  SPEAROW,   SPEAROW,   HOOTHOOT),

    // Route 17
    ENCOUNTER_RULE(ROUTE17, 4, DODUO,    DODUO,     PONYTA,    MIGHTYENA),
    ENCOUNTER_RULE(ROUTE17, 5, FEAROW,   FEAROW,    FEAROW,    SEVIPER),

    // Route 18
    ENCOUNTER_RULE(ROUTE18, 1, FEAROW,   FEAROW,    DODUO,     MURKROW),
    ENCOUNTER_RULE(ROUTE18, 2, SWABLU,   SWABLU,    SWELLOW,   NOCTOWL),

    // Route 21 North & South
    ENCOUNTER_RULE(ROUTE21_NORTH, 2, WINGULL,  WINGULL,   WINGULL,   LOMBRE),
    ENCOUNTER_RULE(ROUTE21_NORTH, 3, ROSELIA,  ROSELIA,   TANGELA,   SHROOMISH),
    ENCOUNTER_RULE(ROUTE21_SOUTH, 2, WINGULL,  WINGULL,   WINGULL,   LOMBRE),
    ENCOUNTER_RULE(ROUTE21_SOUTH, 4, SKIPLOOM, SKIPLOOM,  HOPPIP,    NUZLEAF),

    // Route 22
    ENCOUNTER_RULE(ROUTE22, 0, RATTATA,  SPEAROW,   RATTATA,   POOCHYENA),
    ENCOUNTER_RULE(ROUTE22, 2, SPEAROW,  SPEAROW,   MANKEY,    HOOTHOOT),

    // Route 23
    ENCOUNTER_RULE(ROUTE23, 1, FEAROW,   FEAROW,    FEAROW,    GLIGAR),
    ENCOUNTER_RULE(ROUTE23, 2, PRIMEAPE, PRIMEAPE,  SANDSLASH, ARBOK),

    // Route 24
    ENCOUNTER_RULE(ROUTE24, 0, PIDGEY,   PIDGEY,    PIDGEY,    ODDISH),
    ENCOUNTER_RULE(ROUTE24, 4, ODDISH,   BELLSPROUT,SUNKERN,   ODDISH),

    // Route 25
    ENCOUNTER_RULE(ROUTE25, 1, PIDGEY,   PIDGEY,    PIDGEY,    ODDISH),
    ENCOUNTER_RULE(ROUTE25, 7, ODDISH,   BELLSPROUT,MAREEP,    ODDISH),

    // ---- Safari Zone ----
    ENCOUNTER_RULE(SAFARI_ZONE_CENTER, 2, RHYHORN,  STANTLER,  RHYHORN,   VENONAT),
    ENCOUNTER_RULE(SAFARI_ZONE_EAST,   0, DODUO,    DODUO,     DODUO,     PARAS),
    ENCOUNTER_RULE(SAFARI_ZONE_NORTH,  0, NIDORINO, TEDDIURSA, NIDORINO,  GLIGAR),
    ENCOUNTER_RULE(SAFARI_ZONE_WEST,   0, DODUO,    SWABLU,    DODUO,     VENONAT),

    // ---- Sevii Islands Outdoor Routes & Forests ----
    // One Island: Kindle Road, Treasure Beach & Mt. Ember Exterior
    ENCOUNTER_RULE(ONE_ISLAND_KINDLE_ROAD,    1, FEAROW,   FEAROW,    PONYTA,    PERSIAN),
    ENCOUNTER_RULE(ONE_ISLAND_KINDLE_ROAD,    2, VULPIX,   PONYTA,    VULPIX,    SLUGMA),
    ENCOUNTER_RULE(ONE_ISLAND_TREASURE_BEACH, 0, PELIPPER, PELIPPER,  PELIPPER,  CRAWDAUNT),
    ENCOUNTER_RULE(ONE_ISLAND_TREASURE_BEACH, 1, FEAROW,   FEAROW,    SLOWPOKE,  PERSIAN),
    ENCOUNTER_RULE(MT_EMBER_EXTERIOR,         0, FEAROW,   FEAROW,    PONYTA,    VULPIX),
    ENCOUNTER_RULE(MT_EMBER_EXTERIOR,         2, FEAROW,   NUMEL,     TORKOAL,   GRAVELER),

    // Two Island: Cape Brink
    ENCOUNTER_RULE(TWO_ISLAND_CAPE_BRINK, 0, JUMPLUFF, JUMPLUFF,  SUNKERN,   GLOOM),
    ENCOUNTER_RULE(TWO_ISLAND_CAPE_BRINK, 7, FEAROW,   FEAROW,    ROSELIA,   PERSIAN),

    // Three Island: Bond Bridge & Berry Forest
    ENCOUNTER_RULE(THREE_ISLAND_BOND_BRIDGE,  0, GLOOM,    SWABLU,    PIDGEOTTO, GLOOM),
    ENCOUNTER_RULE(THREE_ISLAND_BOND_BRIDGE,  3, PIDGEOTTO,PIDGEOTTO, XATU,      VENONAT),
    ENCOUNTER_RULE(THREE_ISLAND_BERRY_FOREST, 1, PIDGEOTTO,PIDGEOTTO, BRELOOM,   NOCTOWL),
    ENCOUNTER_RULE(THREE_ISLAND_BERRY_FOREST, 3, NOCTOWL,  AIPOM,     EXEGGCUTE, NOCTOWL),

    // Five Island: Meadow & Memorial Pillar
    ENCOUNTER_RULE(FIVE_ISLAND_MEADOW,          0, JUMPLUFF, JUMPLUFF,  SENTRET,   MEOWTH),
    ENCOUNTER_RULE(FIVE_ISLAND_MEADOW,          5, PIDGEOT,  PIDGEOT,   STANTLER,  GOLDUCK),
    ENCOUNTER_RULE(FIVE_ISLAND_MEMORIAL_PILLAR, 0, HOPPIP,   HOPPIP,    SWABLU,    NATU),
    ENCOUNTER_RULE(FIVE_ISLAND_MEMORIAL_PILLAR, 3, TAILLOW,  TAILLOW,   HOPPIP,    NATU),

    // Six Island: Water Path, Ruin Valley & Pattern Bush
    ENCOUNTER_RULE(SIX_ISLAND_WATER_PATH,   0, FEAROW,   FEAROW,    MASQUERAIN,GLOOM),
    ENCOUNTER_RULE(SIX_ISLAND_WATER_PATH,   1, SEEDOT,   LOTAD,     SEEDOT,    PERSIAN),
    ENCOUNTER_RULE(SIX_ISLAND_RUIN_VALLEY,  0, SPEAROW,  SPEAROW,   YANMA,     PERSIAN),
    ENCOUNTER_RULE(SIX_ISLAND_RUIN_VALLEY,  1, XATU,     XATU,      CACNEA,    BALTOY),
    ENCOUNTER_RULE(SIX_ISLAND_PATTERN_BUSH, 5, LEDYBA,   LEDYBA,    METAPOD,   SPINARAK),
    ENCOUNTER_RULE(SIX_ISLAND_PATTERN_BUSH, 6, SPINARAK, LEDYBA,    NINCADA,   SPINARAK),

    // Seven Island: Canyon Entrance & Sevault Canyon
    ENCOUNTER_RULE(SEVEN_ISLAND_SEVAULT_CANYON_ENTRANCE, 5, FURRET, FURRET,   TEDDIURSA, GLIGAR),
    ENCOUNTER_RULE(SEVEN_ISLAND_SEVAULT_CANYON_ENTRANCE, 6, FEAROW, FEAROW,   FEAROW,    MEOWTH),
    ENCOUNTER_RULE(SEVEN_ISLAND_SEVAULT_CANYON,          4, FEAROW, FEAROW,   SKARMORY,  GLIGAR),
    ENCOUNTER_RULE(SEVEN_ISLAND_SEVAULT_CANYON,          5, MAROWAK,DONPHAN,  ARON,      MAROWAK),
};

uint32_t VitaEncounters_Phase(const uint32_t seconds)
{
    const uint32_t minute = (seconds % 86400u) / 60u;

    if (minute >= VITA_DAYNIGHT_SUNRISE_MIN && minute < VITA_DAYNIGHT_NOON_START_MIN)
        return VITA_TIME_MORNING;
    return (minute >= VITA_DAYNIGHT_NOON_START_MIN && minute < VITA_DAYNIGHT_NIGHT_MIN)
        ? VITA_TIME_DAY : VITA_TIME_NIGHT;
}

uint16_t VitaEncounters_Resolve(const VitaTimedEncounterRule *const rules, const size_t count,
                                const uint32_t mapGroup, const uint32_t mapNum, const uint32_t slot,
                                const uint16_t original, const uint32_t phase)
{
    if (phase >= VITA_TIME_COUNT || rules == NULL)
        return original;

    for (size_t index = 0; index < count; ++index)
    {
        if (rules[index].mapGroup == mapGroup
         && rules[index].mapNum == mapNum
         && rules[index].slot == slot
         && rules[index].expectedSpecies == original)
        {
            return rules[index].species[phase];
        }
    }

    return original;
}

#define VITA_TIMED_BANKS 8

struct TimedBank
{
    uint8_t mapGroup;
    uint8_t mapNum;
    uint8_t phase;
    bool inUse;
    struct WildPokemon slots[12];
    struct WildPokemonInfo info;
};

static struct TimedBank sTimedBanks[VITA_TIMED_BANKS];
static uint32_t sTimedBankRoundRobin = 0;

const struct WildPokemonInfo *VitaEncounters_LandPhase(const struct WildPokemonInfo *const original,
                                                       const uint32_t mapGroup, const uint32_t mapNum, const uint32_t phase)
{
    if (original == NULL || original->wildPokemon == NULL || VitaExtras_GetFast(FAST_EXTRA_TIMED_ENCOUNTERS) != 1)
        return original;

    for (size_t b = 0; b < VITA_TIMED_BANKS; ++b)
    {
        if (sTimedBanks[b].inUse
         && sTimedBanks[b].mapGroup == (uint8_t)mapGroup
         && sTimedBanks[b].mapNum == (uint8_t)mapNum
         && sTimedBanks[b].phase == (uint8_t)phase)
        {
            return &sTimedBanks[b].info;
        }
    }

    bool changed = false;
    for (size_t index = 0; index < 12; ++index)
    {
        uint16_t resolved = VitaEncounters_Resolve(sRules, ARRAY_COUNT(sRules),
                                                   mapGroup, mapNum,
                                                   (uint32_t)index, original->wildPokemon[index].species, phase);
        if (resolved != original->wildPokemon[index].species)
        {
            changed = true;
            break;
        }
    }

    if (!changed)
        return original;

    const uint32_t bank = sTimedBankRoundRobin;
    sTimedBankRoundRobin = (sTimedBankRoundRobin + 1) % VITA_TIMED_BANKS;

    for (size_t index = 0; index < 12; ++index)
    {
        sTimedBanks[bank].slots[index] = original->wildPokemon[index];
        sTimedBanks[bank].slots[index].species = VitaEncounters_Resolve(sRules, ARRAY_COUNT(sRules),
                                                                       mapGroup, mapNum,
                                                                       (uint32_t)index, original->wildPokemon[index].species, phase);
    }

    sTimedBanks[bank].info = *original;
    sTimedBanks[bank].info.wildPokemon = sTimedBanks[bank].slots;
    sTimedBanks[bank].mapGroup = (uint8_t)mapGroup;
    sTimedBanks[bank].mapNum = (uint8_t)mapNum;
    sTimedBanks[bank].phase = (uint8_t)phase;
    sTimedBanks[bank].inUse = true;

    return &sTimedBanks[bank].info;
}

const struct WildPokemonInfo *VitaEncounters_Land(const struct WildPokemonInfo *const original,
                                                  const uint32_t mapGroup, const uint32_t mapNum)
{
    uint32_t seconds = 0;
    uint32_t rate = 0;

    if (original == NULL || original->wildPokemon == NULL
     || VitaExtras_GetFast(FAST_EXTRA_TIMED_ENCOUNTERS) != 1
     || !VitaDayNight_Time(&seconds, &rate))
    {
        return original;
    }

    const uint32_t phase = VitaEncounters_Phase(seconds);
    return VitaEncounters_LandPhase(original, mapGroup, mapNum, phase);
}
