// Town Map, Fly Map, Wild Encounters & Pokédex Habitats for the native PS Vita UI (Core 1).

#include <string.h>
#include "ui_game.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "event_data.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon.h"
#include "region_map.h"
#include "roamer.h"
#include "wild_encounter.h"
#pragma GCC diagnostic pop
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "constants/species.h"
#include "constants/vars.h"
#include "platform/vita_daynight.h"
#include "platform/vita_encounters.h"
#include "platform/vita_extras.h"

extern u16 RegionMap_GetMapSecForUi(u8 region, u8 layer, s16 x, s16 y);
extern u8 RegionMap_GetUnlockedCountForUi(void);
extern u8 RegionMap_GetPlayerPosForUi(s16 *outX, s16 *outY);
extern bool8 RegionMap_CanFlyToForUi(u16 mapsec);
extern void RegionMap_ExecuteFlyForUi(u8 partySlot, u16 mapsec);
extern u8 Pokedex_GetHabitatForSpecies(u16 species);
extern u16 Pokedex_GetHabitatSpecies(u8 habitat, u16 *outSpecies, u16 maxOut);

int32_t UiGame_MapRegionCount(void)
{
    return (int32_t)RegionMap_GetUnlockedCountForUi();
}

uint16_t UiGame_MapSecAt(const int32_t region, const int32_t layer, const int32_t gx, const int32_t gy)
{
    return RegionMap_GetMapSecForUi((u8)region, (u8)layer, (s16)gx, (s16)gy);
}

uint8_t UiGame_PlayerMapPos(int32_t *const gx, int32_t *const gy)
{
    s16 x = 0;
    s16 y = 0;
    const u8 region = RegionMap_GetPlayerPosForUi(&x, &y);
    if (gx != NULL)
        *gx = (int32_t)x;
    if (gy != NULL)
        *gy = (int32_t)y;
    return region;
}

bool UiGame_CanFlyTo(const uint16_t sec)
{
    return RegionMap_CanFlyToForUi(sec) != FALSE;
}

void UiGame_ExecuteFly(const uint8_t partySlot, const uint16_t sec)
{
    RegionMap_ExecuteFlyForUi(partySlot, sec);
}

static uint16_t NormalizeWildSec(const uint16_t sec)
{
    if (sec >= MAPSEC_MONEAN_CHAMBER && sec <= MAPSEC_VIAPOIS_CHAMBER)
        return MAPSEC_TANOBY_CHAMBERS;
    if (sec == MAPSEC_ROUTE_4_POKECENTER)
        return MAPSEC_ROUTE_4;
    if (sec == MAPSEC_ROUTE_10_POKECENTER)
        return MAPSEC_ROUTE_10;
    if (sec == MAPSEC_EMBER_SPA)
        return MAPSEC_MT_EMBER;
    return sec;
}

static uint16_t HeaderSec(const int32_t idx)
{
    const struct MapHeader *const mh = Overworld_GetMapHeaderByGroupAndId(gWildMonHeaders[idx].mapGroup, gWildMonHeaders[idx].mapNum);
    return mh != NULL ? mh->regionMapSectionId : MAPSEC_NONE;
}

static int32_t PushWild(UiWild *const out, int32_t n, const int32_t max, const uint16_t species,
                        const uint8_t lo, const uint8_t hi, const uint8_t rate, const uint8_t rod)
{
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return n;
    for (int32_t i = 0; i < n; i++)
    {
        if (out[i].species == species && out[i].rod == rod)
        {
            if (lo < out[i].minLv) out[i].minLv = lo;
            if (hi > out[i].maxLv) out[i].maxLv = hi;
            const uint32_t sum = (uint32_t)out[i].rate + (uint32_t)rate;
            out[i].rate = (uint8_t)(sum > 100u ? 100u : sum);
            return n;
        }
    }
    if (n < max)
        out[n++] = (UiWild){ species, lo, hi, rate, rod };
    return n;
}

uint32_t UiGame_CurrentTimePhase(void)
{
    return VitaEncounters_Phase((uint32_t)(VitaDayNight_GetTimeMs() / 1000U));
}

int32_t UiGame_Wild(const uint16_t rawSec, const int32_t method, const int32_t rod, UiWild *const out, const int32_t max)
{
    static const uint8_t kLandRate[LAND_WILD_COUNT] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
    static const uint8_t kWaterRate[WATER_WILD_COUNT] = { 60, 30, 5, 4, 1 };
    static const uint8_t kFishRate[FISH_WILD_COUNT] = { 70, 30, 60, 20, 20, 40, 40, 15, 4, 1 };
    static const uint8_t kFishRod[FISH_WILD_COUNT] = { 1, 1, 2, 2, 2, 3, 3, 3, 3, 3 };

    if (rawSec >= MAPSEC_NONE || out == NULL || max <= 0)
        return 0;

    const uint16_t targetSec = NormalizeWildSec(rawSec);
    uint16_t alteringCaveNum = VarGet(VAR_ALTERING_CAVE_WILD_SET);
    if (alteringCaveNum >= NUM_ALTERING_CAVE_TABLES)
        alteringCaveNum = 0;
    int32_t alteringCaveCount = 0;
    int32_t n = 0;

    for (int32_t i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        const struct WildPokemonHeader *const h = &gWildMonHeaders[i];
        const uint16_t sec = HeaderSec(i);
        if (sec == MAPSEC_ALTERING_CAVE)
        {
            if (alteringCaveCount != (int32_t)alteringCaveNum)
            {
                alteringCaveCount++;
                continue;
            }
            alteringCaveCount++;
        }
        if (NormalizeWildSec(sec) != targetSec)
            continue;

        if (method == UI_WILD_GRASS && h->landMonsInfo != NULL)
        {
            const struct WildPokemonInfo *land = h->landMonsInfo;
            if (VitaExtras_GetFast(FAST_EXTRA_TIMED_ENCOUNTERS) != 0)
            {
                const uint32_t phase = (rod >= 0 && rod < VITA_TIME_COUNT) ? (uint32_t)rod : UiGame_CurrentTimePhase();
                land = VitaEncounters_LandPhase(land, h->mapGroup, h->mapNum, phase);
            }
            for (int32_t s = 0; s < LAND_WILD_COUNT; s++)
            {
                const struct WildPokemon *const w = &land->wildPokemon[s];
                n = PushWild(out, n, max, w->species, w->minLevel, w->maxLevel, kLandRate[s], 0);
            }
        }
        else if (method == UI_WILD_SURF && h->waterMonsInfo != NULL)
        {
            for (int32_t s = 0; s < WATER_WILD_COUNT; s++)
            {
                const struct WildPokemon *const w = &h->waterMonsInfo->wildPokemon[s];
                n = PushWild(out, n, max, w->species, w->minLevel, w->maxLevel, kWaterRate[s], 0);
            }
        }
        else if (method == UI_WILD_ROCK && h->rockSmashMonsInfo != NULL)
        {
            for (int32_t s = 0; s < ROCK_WILD_COUNT; s++)
            {
                const struct WildPokemon *const w = &h->rockSmashMonsInfo->wildPokemon[s];
                n = PushWild(out, n, max, w->species, w->minLevel, w->maxLevel, kWaterRate[s], 0);
            }
        }
        else if (method == UI_WILD_FISH && h->fishingMonsInfo != NULL)
        {
            for (int32_t s = 0; s < FISH_WILD_COUNT; s++)
            {
                const uint8_t tier = kFishRod[s];
                if (rod == 0 || rod == tier)
                {
                    const struct WildPokemon *const w = &h->fishingMonsInfo->wildPokemon[s];
                    n = PushWild(out, n, max, w->species, w->minLevel, w->maxLevel, kFishRate[s], tier);
                }
            }
        }
    }

    if (method == UI_WILD_GRASS && gSaveBlock1Ptr->roamer.active
        && NormalizeWildSec(GetRoamerLocationMapSectionId()) == targetSec)
    {
        n = PushWild(out, n, max, gSaveBlock1Ptr->roamer.species,
                     gSaveBlock1Ptr->roamer.level, gSaveBlock1Ptr->roamer.level, 1, 0);
    }
    return n;
}

int32_t UiGame_SpeciesAreas(const uint16_t species, uint8_t *const out, const int32_t count)
{
    if (out == NULL || count <= 0)
        return 0;
    memset(out, 0, (size_t)count);
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return 0;

    uint16_t alteringCaveNum = VarGet(VAR_ALTERING_CAVE_WILD_SET);
    if (alteringCaveNum >= NUM_ALTERING_CAVE_TABLES)
        alteringCaveNum = 0;
    int32_t alteringCaveCount = 0;
    int32_t n = 0;

    for (int32_t i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        const struct WildPokemonHeader *const h = &gWildMonHeaders[i];
        const uint16_t rawSec = HeaderSec(i);
        if (rawSec == MAPSEC_ALTERING_CAVE)
        {
            if (alteringCaveCount != (int32_t)alteringCaveNum)
            {
                alteringCaveCount++;
                continue;
            }
            alteringCaveCount++;
        }
        const uint16_t sec = NormalizeWildSec(rawSec);
        if (sec >= (uint16_t)count || out[sec])
            continue;

        const struct WildPokemonInfo *const infos[4] = {
            h->landMonsInfo, h->waterMonsInfo, h->rockSmashMonsInfo, h->fishingMonsInfo
        };
        const int32_t sizes[4] = {
            LAND_WILD_COUNT, WATER_WILD_COUNT, ROCK_WILD_COUNT, FISH_WILD_COUNT
        };
        for (int32_t k = 0; k < 4 && !out[sec]; k++)
        {
            if (infos[k] == NULL)
                continue;
            for (int32_t s = 0; s < sizes[k]; s++)
            {
                if (infos[k]->wildPokemon[s].species == species)
                {
                    out[sec] = 1;
                    n++;
                    break;
                }
            }
        }
    }

    if (gSaveBlock1Ptr->roamer.active && gSaveBlock1Ptr->roamer.species == species)
    {
        const uint16_t roamerSec = NormalizeWildSec(GetRoamerLocationMapSectionId());
        if (roamerSec < (uint16_t)count && !out[roamerSec])
        {
            out[roamerSec] = 1;
            n++;
        }
    }
    return n;
}

uint8_t UiGame_SpeciesHabitat(const uint16_t species)
{
    return Pokedex_GetHabitatForSpecies(species);
}

int32_t UiGame_HabitatSpecies(const uint8_t habitat, uint16_t *const out, const int32_t max)
{
    if (out == NULL || max <= 0)
        return 0;
    return (int32_t)Pokedex_GetHabitatSpecies(habitat, out, (u16)max);
}
