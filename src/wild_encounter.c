#include "global.h"
#ifdef PLATFORM_VITA
#include "follower_mon.h"
#include "platform/vita_encounters.h"
#include "platform/vita_extras.h"
#endif
#ifdef WIDESCREEN_SUPPORTED
#include "platform/widescreen.h"
#endif
#include "random.h"
#include "wild_encounter.h"
#include "event_data.h"
#include "fieldmap.h"
#include "random.h"
#include "roamer.h"
#include "field_player_avatar.h"
#include "battle_setup.h"
#include "overworld.h"
#include "metatile_behavior.h"
#include "event_scripts.h"
#include "script.h"
#include "link.h"
#include "quest_log.h"
#include "constants/maps.h"
#include "constants/abilities.h"
#include "constants/items.h"

#define MAX_ENCOUNTER_RATE 1600

#define HEADER_NONE 0xFFFF

struct WildEncounterData
{
    u32 rngState;
    u16 prevMetatileBehavior;
    u16 encounterRateBuff;
    u8 stepsSinceLastEncounter;
    u8 abilityEffect;
    u16 leadMonHeldItem;
};

static EWRAM_DATA struct WildEncounterData sWildEncounterData = {};
static EWRAM_DATA bool8 sWildEncountersDisabled = FALSE;

static bool8 UnlockedTanobyOrAreNotInTanoby(u8 mapGroup, u8 mapNum);
static u16 GetWildMonHeaderIdForMap(u8 mapGroup, u8 mapNum);
static u32 GenerateUnownPersonalityByLetter(u8 letter);
static bool8 IsWildLevelAllowedByRepel(u8 level);
static void ApplyFluteEncounterRateMod(u32 *rate);
static u8 GetFluteEncounterRateModType(void);
static void ApplyCleanseTagEncounterRateMod(u32 *rate);
static bool8 IsLeadMonHoldingCleanseTag(void);
static u16 WildEncounterRandom(void);
static void AddToWildEncounterRateBuff(u8 encouterRate);
#ifdef PLATFORM_VITA
static u32 EncounterRatePercent(void);
static bool8 VisibleWild_ReplacesRandom(void);
static const struct WildPokemonInfo *GetLandMonsInfo(u16 headerId);
#endif

#include "data/wild_encounters.h"

#ifdef PLATFORM_VITA
static const struct WildPokemonInfo *GetLandMonsInfo(u16 headerId)
{
    const struct WildPokemonHeader *const header = &gWildMonHeaders[headerId];
    return VitaEncounters_Land(header->landMonsInfo, header->mapGroup, header->mapNum);
}
#endif

static const u8 sUnownLetterSlots[][LAND_WILD_COUNT] = {
  //  A   A   A   A   A   A   A   A   A   A   A   ?
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 27},
  //  C   C   C   D   D   D   H   H   H   U   U   O
    { 2,  2,  2,  3,  3,  3,  7,  7,  7, 20, 20, 14},
  //  N   N   N   N   S   S   S   S   I   I   E   E
    {13, 13, 13, 13, 18, 18, 18, 18,  8,  8,  4,  4},
  //  P   P   L   L   J   J   R   R   R   Q   Q   Q
    {15, 15, 11, 11,  9,  9, 17, 17, 17, 16, 16, 16},
  //  Y   Y   T   T   G   G   G   F   F   F   K   K
    {24, 24, 19, 19,  6,  6,  6,  5,  5,  5, 10, 10},
  //  V   V   V   W   W   W   X   X   M   M   B   B
    {21, 21, 21, 22, 22, 22, 23, 23, 12, 12,  1,  1},
  //  Z   Z   Z   Z   Z   Z   Z   Z   Z   Z   Z   !
    {25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 26},
};

void DisableWildEncounters(bool8 state)
{
    sWildEncountersDisabled = state;
}

static u8 ChooseWildMonIndex_Land(void)
{
    u8 rand = Random() % ENCOUNTER_CHANCE_LAND_MONS_TOTAL;

    if (rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_0)
        return 0;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_0 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_1)
        return 1;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_1 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_2)
        return 2;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_2 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_3)
        return 3;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_3 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_4)
        return 4;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_4 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_5)
        return 5;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_5 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_6)
        return 6;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_6 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_7)
        return 7;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_7 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_8)
        return 8;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_8 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_9)
        return 9;
    else if (rand >= ENCOUNTER_CHANCE_LAND_MONS_SLOT_9 && rand < ENCOUNTER_CHANCE_LAND_MONS_SLOT_10)
        return 10;
    else
        return 11;
}

static u8 ChooseWildMonIndex_WaterRock(void)
{
    u8 rand = Random() % ENCOUNTER_CHANCE_WATER_MONS_TOTAL;

    if (rand < ENCOUNTER_CHANCE_WATER_MONS_SLOT_0)
        return 0;
    else if (rand >= ENCOUNTER_CHANCE_WATER_MONS_SLOT_0 && rand < ENCOUNTER_CHANCE_WATER_MONS_SLOT_1)
        return 1;
    else if (rand >= ENCOUNTER_CHANCE_WATER_MONS_SLOT_1 && rand < ENCOUNTER_CHANCE_WATER_MONS_SLOT_2)
        return 2;
    else if (rand >= ENCOUNTER_CHANCE_WATER_MONS_SLOT_2 && rand < ENCOUNTER_CHANCE_WATER_MONS_SLOT_3)
        return 3;
    else
        return 4;
}

static u8 ChooseWildMonIndex_Fishing(u8 rod)
{
    u8 wildMonIndex = 0;
    u8 rand = Random() % max(max(ENCOUNTER_CHANCE_FISHING_MONS_OLD_ROD_TOTAL, ENCOUNTER_CHANCE_FISHING_MONS_GOOD_ROD_TOTAL),
                             ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_TOTAL);

    switch (rod)
    {
    case OLD_ROD:
        if (rand < ENCOUNTER_CHANCE_FISHING_MONS_OLD_ROD_SLOT_0)
            wildMonIndex = 0;
        else
            wildMonIndex = 1;
        break;
    case GOOD_ROD:
        if (rand < ENCOUNTER_CHANCE_FISHING_MONS_GOOD_ROD_SLOT_2)
            wildMonIndex = 2;
        if (rand >= ENCOUNTER_CHANCE_FISHING_MONS_GOOD_ROD_SLOT_2 && rand < ENCOUNTER_CHANCE_FISHING_MONS_GOOD_ROD_SLOT_3)
            wildMonIndex = 3;
        if (rand >= ENCOUNTER_CHANCE_FISHING_MONS_GOOD_ROD_SLOT_3 && rand < ENCOUNTER_CHANCE_FISHING_MONS_GOOD_ROD_SLOT_4)
            wildMonIndex = 4;
        break;
    case SUPER_ROD:
        if (rand < ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_5)
            wildMonIndex = 5;
        if (rand >= ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_5 && rand < ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_6)
            wildMonIndex = 6;
        if (rand >= ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_6 && rand < ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_7)
            wildMonIndex = 7;
        if (rand >= ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_7 && rand < ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_8)
            wildMonIndex = 8;
        if (rand >= ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_8 && rand < ENCOUNTER_CHANCE_FISHING_MONS_SUPER_ROD_SLOT_9)
            wildMonIndex = 9;
        break;
    }
    return wildMonIndex;
}

static u8 ChooseWildMonLevel(const struct WildPokemon * info)
{
    u8 lo;
    u8 hi;
    u8 mod;
    u8 res;
    if (info->maxLevel >= info->minLevel)
    {
        lo = info->minLevel;
        hi = info->maxLevel;
    }
    else
    {
        lo = info->maxLevel;
        hi = info->minLevel;
    }
    mod = hi - lo + 1;
    res = Random() % mod;
    return lo + res;
}

static bool8 UnlockedTanobyOrAreNotInTanoby(u8 mapGroup, u8 mapNum)
{
    if (FlagGet(FLAG_SYS_UNLOCKED_TANOBY_RUINS))
        return TRUE;
    if (mapGroup != MAP_GROUP(MAP_SEVEN_ISLAND_TANOBY_RUINS_DILFORD_CHAMBER))
        return TRUE;
    if (!(mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_MONEAN_CHAMBER)
    ||  mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_LIPTOO_CHAMBER)
    ||  mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_WEEPTH_CHAMBER)
    ||  mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_DILFORD_CHAMBER)
    ||  mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_SCUFIB_CHAMBER)
    ||  mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_RIXY_CHAMBER)
    ||  mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_VIAPOIS_CHAMBER)
    ))
        return TRUE;
    return FALSE;
}

static u16 GetWildMonHeaderIdForMap(u8 mapGroup, u8 mapNum)
{
    u16 i;

    for (i = 0; ; i++)
    {
        const struct WildPokemonHeader *wildHeader = &gWildMonHeaders[i];
        if (wildHeader->mapGroup == MAP_GROUP(MAP_UNDEFINED))
            break;

        if (wildHeader->mapGroup == mapGroup && wildHeader->mapNum == mapNum)
        {
            if (mapGroup == MAP_GROUP(MAP_SIX_ISLAND_ALTERING_CAVE) &&
                mapNum == MAP_NUM(MAP_SIX_ISLAND_ALTERING_CAVE))
            {
                u16 alteringCaveId = VarGet(VAR_ALTERING_CAVE_WILD_SET);
                if (alteringCaveId >= NUM_ALTERING_CAVE_TABLES)
                    alteringCaveId = 0;

                i += alteringCaveId;
            }

            if (!UnlockedTanobyOrAreNotInTanoby(mapGroup, mapNum))
                break;
            return i;
        }
    }

    return HEADER_NONE;
}

static u16 GetCurrentMapWildMonHeaderId(void)
{
    return GetWildMonHeaderIdForMap(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
}

static void GenerateWildMon(u16 species, u8 level, u8 slot)
{
    u32 personality;
    s8 chamber;
    ZeroEnemyPartyMons();
    if (species != SPECIES_UNOWN)
    {
        CreateMonWithNature(&gEnemyParty[0], species, level, USE_RANDOM_IVS, Random() % NUM_NATURES);
    }
    else
    {
        chamber = gSaveBlock1Ptr->location.mapNum - MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_MONEAN_CHAMBER);
        personality = GenerateUnownPersonalityByLetter(sUnownLetterSlots[chamber][slot]);
        CreateMon(&gEnemyParty[0], species, level, USE_RANDOM_IVS, TRUE, personality, FALSE, 0);
    }
}

static u32 GenerateUnownPersonalityByLetter(u8 letter)
{
    u32 personality;
    do
    {
        personality = (Random() << 16) | Random();
    } while (GetUnownLetterByPersonalityLoByte(personality) != letter);
    return personality;
}

u8 GetUnownLetterByPersonalityLoByte(u32 personality)
{
    return GET_UNOWN_LETTER(personality);
}

enum
{
    WILD_AREA_LAND,
    WILD_AREA_WATER,
    WILD_AREA_ROCKS,
    WILD_AREA_FISHING,
};

#define WILD_CHECK_REPEL    0x1
#define WILD_CHECK_KEEN_EYE 0x2

static bool8 TryGenerateWildMon(const struct WildPokemonInfo * info, u8 area, u8 flags)
{
    u8 slot = 0;
    u8 level;
    switch (area)
    {
    case WILD_AREA_LAND:
        slot = ChooseWildMonIndex_Land();
        break;
    case WILD_AREA_WATER:
        slot = ChooseWildMonIndex_WaterRock();
        break;
    case WILD_AREA_ROCKS:
        slot = ChooseWildMonIndex_WaterRock();
        break;
    }
    level = ChooseWildMonLevel(&info->wildPokemon[slot]);
    if (flags == WILD_CHECK_REPEL && !IsWildLevelAllowedByRepel(level))
    {
        return FALSE;
    }
    GenerateWildMon(info->wildPokemon[slot].species, level, slot);
    return TRUE;
}

static u16 GenerateFishingEncounter(const struct WildPokemonInfo * info, u8 rod)
{
    u8 slot = ChooseWildMonIndex_Fishing(rod);
    u8 level = ChooseWildMonLevel(&info->wildPokemon[slot]);
    GenerateWildMon(info->wildPokemon[slot].species, level, slot);
    return info->wildPokemon[slot].species;
}

static bool8 DoWildEncounterRateDiceRoll(u16 encounterRate)
{
    if (WildEncounterRandom() % MAX_ENCOUNTER_RATE < encounterRate)
        return TRUE;
    return FALSE;
}

#ifdef PLATFORM_VITA
// Wild encounter rate (OPTIONS, CHEATS) in percent: random encounters while
// walking and surfing, and how often visible wild Pokemon appear.
static u32 EncounterRatePercent(void)
{
    static const u8 percent[] = {100, 0, 25, 50, 200}; // NORMAL, OFF, 1/4, 1/2, 2X
    const u32 setting = (u32)VitaExtras_GetFast(FAST_EXTRA_ENCOUNTER_RATE);

    return setting < ARRAY_COUNT(percent) ? percent[setting] : 100;
}
#endif

static bool8 DoWildEncounterRateTest(u32 encounterRate, bool8 ignoreAbility)
{
    encounterRate *= 16;
    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE))
        encounterRate = encounterRate * 80 / 100;
    encounterRate += sWildEncounterData.encounterRateBuff * 16 / 200;
    ApplyFluteEncounterRateMod(&encounterRate);
    ApplyCleanseTagEncounterRateMod(&encounterRate);
    if (!ignoreAbility)
    {
        switch (sWildEncounterData.abilityEffect)
        {
        case 1:
            encounterRate /= 2;
            break;
        case 2:
            encounterRate *= 2;
            break;
        }
    }
#ifdef PLATFORM_VITA
    // Wild encounter rate (OPTIONS, CHEATS): walking and surfing only; Rock
    // Smash (ignoreAbility), fishing and set encounters keep the game's.
    if (!ignoreAbility)
    {
        encounterRate = encounterRate * EncounterRatePercent() / 100;
    }
#endif
    if (encounterRate > MAX_ENCOUNTER_RATE)
        encounterRate = MAX_ENCOUNTER_RATE;
    return DoWildEncounterRateDiceRoll(encounterRate);
}

static u8 GetAbilityEncounterRateModType(void)
{
    sWildEncounterData.abilityEffect = 0;
    if (!GetMonData(&gPlayerParty[0], MON_DATA_SANITY_IS_EGG))
    {
        u8 ability = GetMonAbility(&gPlayerParty[0]);
        if (ability == ABILITY_STENCH)
            sWildEncounterData.abilityEffect = 1;
        else if (ability == ABILITY_ILLUMINATE)
            sWildEncounterData.abilityEffect = 2;
    }
    return sWildEncounterData.abilityEffect;
}

static bool8 DoGlobalWildEncounterDiceRoll(void)
{
    if ((Random() % 100) >= 60)
        return FALSE;
    return TRUE;
}

bool8 StandardWildEncounter(u32 currMetatileAttrs, u16 previousMetatileBehavior)
{
    u16 headerId;
    struct Roamer * roamer;

    if (sWildEncountersDisabled == TRUE)
        return FALSE;
#ifdef PLATFORM_VITA
    if (VitaExtras_GetFast(FAST_EXTRA_ENCOUNTER_RATE) == 1)
        return FALSE;
#endif

    headerId = GetCurrentMapWildMonHeaderId();
    if (headerId != HEADER_NONE)
    {
        if (ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_ENCOUNTER_TYPE) == TILE_ENCOUNTER_LAND)
        {
#ifdef PLATFORM_VITA
            const struct WildPokemonInfo *landInfo = GetLandMonsInfo(headerId);
#else
            const struct WildPokemonInfo *landInfo = gWildMonHeaders[headerId].landMonsInfo;
#endif
            if (landInfo == NULL)
                return FALSE;
            else if (previousMetatileBehavior != ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_BEHAVIOR) && !DoGlobalWildEncounterDiceRoll())
                return FALSE;
            if (DoWildEncounterRateTest(landInfo->encounterRate, FALSE) != TRUE)
            {
                AddToWildEncounterRateBuff(landInfo->encounterRate);
                return FALSE;
            }

            else if (TryStartRoamerEncounter() == TRUE)
            {
                roamer = &gSaveBlock1Ptr->roamer;
                if (!IsWildLevelAllowedByRepel(roamer->level))
                {
                    return FALSE;
                }

                StartRoamerBattle();
                return TRUE;
            }
            else
            {

#ifdef PLATFORM_VITA
                if (VisibleWild_ReplacesRandom())
                    return FALSE; // they walk the grass instead (VisibleWild_Update)
#endif
                // try a regular wild land encounter
                if (TryGenerateWildMon(landInfo, WILD_AREA_LAND, WILD_CHECK_REPEL) == TRUE)
                {
                    StartWildBattle();
                    return TRUE;
                }
                else
                {
                    AddToWildEncounterRateBuff(landInfo->encounterRate);
                }
            }
        }
        else if (ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_ENCOUNTER_TYPE) == TILE_ENCOUNTER_WATER
                 || (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) && MetatileBehavior_IsBridge(ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_BEHAVIOR)) == TRUE))
        {
            if (gWildMonHeaders[headerId].waterMonsInfo == NULL)
                return FALSE;
            else if (previousMetatileBehavior != ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_BEHAVIOR) && !DoGlobalWildEncounterDiceRoll())
                return FALSE;
            else if (DoWildEncounterRateTest(gWildMonHeaders[headerId].waterMonsInfo->encounterRate, FALSE) != TRUE)
            {
                AddToWildEncounterRateBuff(gWildMonHeaders[headerId].waterMonsInfo->encounterRate);
                return FALSE;
            }

            if (TryStartRoamerEncounter() == TRUE)
            {
                roamer = &gSaveBlock1Ptr->roamer;
                if (!IsWildLevelAllowedByRepel(roamer->level))
                {
                    return FALSE;
                }

                StartRoamerBattle();
                return TRUE;
            }
            else // try a regular surfing encounter
            {
#ifdef PLATFORM_VITA
                if (VisibleWild_ReplacesRandom())
                    return FALSE;
#endif
                if (TryGenerateWildMon(gWildMonHeaders[headerId].waterMonsInfo, WILD_AREA_WATER, WILD_CHECK_REPEL) == TRUE)
                {
                    StartWildBattle();
                    return TRUE;
                }
                else
                {
                    AddToWildEncounterRateBuff(gWildMonHeaders[headerId].waterMonsInfo->encounterRate);
                }
            }
        }
    }

    return FALSE;
}

void RockSmashWildEncounter(void)
{
    u16 headerIdx = GetCurrentMapWildMonHeaderId();
    if (headerIdx == HEADER_NONE)
        gSpecialVar_Result = FALSE;
    else if (gWildMonHeaders[headerIdx].rockSmashMonsInfo == NULL)
        gSpecialVar_Result = FALSE;
    else if (DoWildEncounterRateTest(gWildMonHeaders[headerIdx].rockSmashMonsInfo->encounterRate, TRUE) != TRUE)
        gSpecialVar_Result = FALSE;
    else if (TryGenerateWildMon(gWildMonHeaders[headerIdx].rockSmashMonsInfo, WILD_AREA_ROCKS, WILD_CHECK_REPEL) == TRUE)
    {
        StartWildBattle();
        gSpecialVar_Result = TRUE;
    }
    else
        gSpecialVar_Result = FALSE;
}

bool8 SweetScentWildEncounter(void)
{
    s16 x, y;
    u16 headerId;

    PlayerGetDestCoords(&x, &y);
    headerId = GetCurrentMapWildMonHeaderId();
    if (headerId != HEADER_NONE)
    {
        if (MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE) == TILE_ENCOUNTER_LAND)
        {
            if (TryStartRoamerEncounter() == TRUE)
            {
                StartRoamerBattle();
                return TRUE;
            }

#ifdef PLATFORM_VITA
            const struct WildPokemonInfo *landInfo = GetLandMonsInfo(headerId);
#else
            const struct WildPokemonInfo *landInfo = gWildMonHeaders[headerId].landMonsInfo;
#endif
            if (landInfo == NULL)
                return FALSE;

            TryGenerateWildMon(landInfo, WILD_AREA_LAND, 0);

            StartWildBattle();
            return TRUE;
        }
        else if (MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE) == TILE_ENCOUNTER_WATER)
        {
            if (TryStartRoamerEncounter() == TRUE)
            {
                StartRoamerBattle();
                return TRUE;
            }

            if (gWildMonHeaders[headerId].waterMonsInfo == NULL)
                return FALSE;

            TryGenerateWildMon(gWildMonHeaders[headerId].waterMonsInfo, WILD_AREA_WATER, 0);
            StartWildBattle();
            return TRUE;
        }
    }

    return FALSE;
}

bool8 DoesCurrentMapHaveFishingMons(void)
{
    u16 headerIdx = GetCurrentMapWildMonHeaderId();
    if (headerIdx == HEADER_NONE)
        return FALSE;
    if (gWildMonHeaders[headerIdx].fishingMonsInfo == NULL)
        return FALSE;
    return TRUE;
}

void FishingWildEncounter(u8 rod)
{
    GenerateFishingEncounter(gWildMonHeaders[GetCurrentMapWildMonHeaderId()].fishingMonsInfo, rod);
    IncrementGameStat(GAME_STAT_FISHING_CAPTURES);
    StartWildBattle();
}

u16 GetLocalWildMon(bool8 *isWaterMon)
{
    u16 headerId;
    const struct WildPokemonInfo * landMonsInfo;
    const struct WildPokemonInfo * waterMonsInfo;

    *isWaterMon = FALSE;
    headerId = GetCurrentMapWildMonHeaderId();
    if (headerId == HEADER_NONE)
        return SPECIES_NONE;
    landMonsInfo = gWildMonHeaders[headerId].landMonsInfo;
    waterMonsInfo = gWildMonHeaders[headerId].waterMonsInfo;
    // Neither
    if (landMonsInfo == NULL && waterMonsInfo == NULL)
        return SPECIES_NONE;
        // Land Pokemon
    else if (landMonsInfo != NULL && waterMonsInfo == NULL)
        return landMonsInfo->wildPokemon[ChooseWildMonIndex_Land()].species;
        // Water Pokemon
    else if (landMonsInfo == NULL && waterMonsInfo != NULL)
    {
        *isWaterMon = TRUE;
        return waterMonsInfo->wildPokemon[ChooseWildMonIndex_WaterRock()].species;
    }
    // Either land or water Pokemon
    if ((Random() % 100) < 80)
    {
        return landMonsInfo->wildPokemon[ChooseWildMonIndex_Land()].species;
    }
    else
    {
        *isWaterMon = TRUE;
        return waterMonsInfo->wildPokemon[ChooseWildMonIndex_WaterRock()].species;
    }
}

u16 GetLocalWaterMon(void)
{
    u16 headerId = GetCurrentMapWildMonHeaderId();

    if (headerId != HEADER_NONE)
    {
        const struct WildPokemonInfo * waterMonsInfo = gWildMonHeaders[headerId].waterMonsInfo;

        if (waterMonsInfo)
            return waterMonsInfo->wildPokemon[ChooseWildMonIndex_WaterRock()].species;
    }
    return SPECIES_NONE;
}

bool8 UpdateRepelCounter(void)
{
    u16 steps;

    if (InUnionRoom() == TRUE)
        return FALSE;

    if (gQuestLogState == QL_STATE_PLAYBACK)
        return FALSE;

    steps = VarGet(VAR_REPEL_STEP_COUNT);

    if (steps != 0)
    {
        steps--;
        VarSet(VAR_REPEL_STEP_COUNT, steps);
        if (steps == 0)
        {
            ScriptContext_SetupScript(EventScript_RepelWoreOff);
            return TRUE;
        }
    }
    return FALSE;
}

static bool8 IsWildLevelAllowedByRepel(u8 wildLevel)
{
    u8 i;

    if (!VarGet(VAR_REPEL_STEP_COUNT))
        return TRUE;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&gPlayerParty[i], MON_DATA_HP) && !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG))
        {
            u8 ourLevel = GetMonData(&gPlayerParty[i], MON_DATA_LEVEL);

            if (wildLevel < ourLevel)
                return FALSE;
            else
                return TRUE;
        }
    }

    return FALSE;
}

static void ApplyFluteEncounterRateMod(u32 *encounterRate)
{
    switch (GetFluteEncounterRateModType())
    {
    case 1:
        *encounterRate += *encounterRate / 2;
        break;
    case 2:
        *encounterRate = *encounterRate / 2;
        break;
    }
}

static u8 GetFluteEncounterRateModType(void)
{
    if (FlagGet(FLAG_SYS_WHITE_FLUTE_ACTIVE) == TRUE)
        return 1;
    else if (FlagGet(FLAG_SYS_BLACK_FLUTE_ACTIVE) == TRUE)
        return 2;
    else
        return 0;
}

static void ApplyCleanseTagEncounterRateMod(u32 *encounterRate)
{
    if (IsLeadMonHoldingCleanseTag())
        *encounterRate = *encounterRate * 2 / 3;
}

static bool8 IsLeadMonHoldingCleanseTag(void)
{
    if (sWildEncounterData.leadMonHeldItem == ITEM_CLEANSE_TAG)
        return TRUE;
    else
        return FALSE;
}

void SeedWildEncounterRng(u16 seed)
{
    sWildEncounterData.rngState = seed;
    ResetEncounterRateModifiers();
}

static u16 WildEncounterRandom(void)
{
    sWildEncounterData.rngState = ISO_RANDOMIZE2(sWildEncounterData.rngState);
    return sWildEncounterData.rngState >> 16;
}

static u8 GetMapBaseEncounterCooldown(u8 encounterType)
{
    u16 headerIdx = GetCurrentMapWildMonHeaderId();
    if (headerIdx == HEADER_NONE)
        return 0xFF;
    if (encounterType == TILE_ENCOUNTER_LAND)
    {
        if (gWildMonHeaders[headerIdx].landMonsInfo == NULL)
            return 0xFF;
        if (gWildMonHeaders[headerIdx].landMonsInfo->encounterRate >= 80)
            return 0;
        if (gWildMonHeaders[headerIdx].landMonsInfo->encounterRate < 10)
            return 8;
        return 8 - (gWildMonHeaders[headerIdx].landMonsInfo->encounterRate / 10);
    }
    if (encounterType == TILE_ENCOUNTER_WATER)
    {
        if (gWildMonHeaders[headerIdx].waterMonsInfo == NULL)
            return 0xFF;
        if (gWildMonHeaders[headerIdx].waterMonsInfo->encounterRate >= 80)
            return 0;
        if (gWildMonHeaders[headerIdx].waterMonsInfo->encounterRate < 10)
            return 8;
        return 8 - (gWildMonHeaders[headerIdx].waterMonsInfo->encounterRate / 10);
    }
    return 0xFF;
}

void ResetEncounterRateModifiers(void)
{
    sWildEncounterData.encounterRateBuff = 0;
    sWildEncounterData.stepsSinceLastEncounter = 0;
}

static bool8 HandleWildEncounterCooldown(u32 currMetatileAttrs)
{
    u8 encounterType = ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_ENCOUNTER_TYPE);
    u32 minSteps;
    u32 encRate;
    if (encounterType == TILE_ENCOUNTER_NONE)
        return FALSE;
    minSteps = GetMapBaseEncounterCooldown(encounterType);
    if (minSteps == 0xFF)
        return FALSE;
    minSteps *= 256;
    encRate = 5 * 256;
    switch (GetFluteEncounterRateModType())
    {
    case 1:
        minSteps -= minSteps / 2;
        encRate += encRate / 2;
        break;
    case 2:
        minSteps *= 2;
        encRate /= 2;
        break;
    }
    sWildEncounterData.leadMonHeldItem = GetMonData(&gPlayerParty[0], MON_DATA_HELD_ITEM);
    if (IsLeadMonHoldingCleanseTag() == TRUE)
    {
        minSteps += minSteps / 3;
        encRate -= encRate / 3;
    }
    switch (GetAbilityEncounterRateModType())
    {
    case 1:
        minSteps *= 2;
        encRate /= 2;
        break;
    case 2:
        minSteps /= 2;
        encRate *= 2;
        break;
    }
    minSteps /= 256;
    encRate /= 256;
#ifdef PLATFORM_VITA
    {
        const u32 rateOpt = (u32)VitaExtras_GetFast(FAST_EXTRA_ENCOUNTER_RATE);
        if (rateOpt == 1)
            return FALSE;
        if (rateOpt == 4) // 2X: halve step cooldown and double early-roll chance
        {
            minSteps /= 2;
            encRate *= 2;
        }
    }
#endif
    if (sWildEncounterData.stepsSinceLastEncounter >= minSteps)
        return TRUE;
    sWildEncounterData.stepsSinceLastEncounter++;
    if ((Random() % 100) < encRate)
        return TRUE;
    return FALSE;
}

bool8 TryStandardWildEncounter(u32 currMetatileAttrs)
{
    if (!HandleWildEncounterCooldown(currMetatileAttrs))
    {
        sWildEncounterData.prevMetatileBehavior = ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_BEHAVIOR);
        return FALSE;
    }
    else if (StandardWildEncounter(currMetatileAttrs, sWildEncounterData.prevMetatileBehavior) == TRUE)
    {
        sWildEncounterData.encounterRateBuff = 0;
        sWildEncounterData.stepsSinceLastEncounter = 0;
        sWildEncounterData.prevMetatileBehavior = ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_BEHAVIOR);
        return TRUE;
    }
    else
    {
        sWildEncounterData.prevMetatileBehavior = ExtractMetatileAttribute(currMetatileAttrs, METATILE_ATTRIBUTE_BEHAVIOR);
        return FALSE;
    }
}

static void AddToWildEncounterRateBuff(u8 encounterRate)
{
    if (VarGet(VAR_REPEL_STEP_COUNT) == 0)
        sWildEncounterData.encounterRateBuff += encounterRate;
    else
        sWildEncounterData.encounterRateBuff = 0;
}

#ifdef PLATFORM_VITA
/*
 * Visible wild Pokemon (OPTIONS, ENHANCEMENTS): instead of random encounters
 * in grass, caves and water, the map's wild Pokemon wander its encounter
 * tiles as their party icons, and touching one starts a battle with exactly
 * that Pokemon. Species and level come from the map's own table; the
 * personality (so a shiny, or an Unown's letter) is rolled when it appears,
 * and a shiny one sparkles. Roamers, fishing, Rock Smash and Sweet Scent keep
 * the game's random encounters, and so does the Pokemon Tower until the
 * SILPH SCOPE shows what its ghosts are.
 *
 * Adapted from the Emerald 3DS port (ZallaxDev, MIT: patch 0014-visible-wild,
 * see NOTICE.md). They are map-anchored sprites (coordOffsetEnabled), placed
 * each frame from their tile as object events are, so the voxel view draws
 * them as cards. Called every overworld frame (overworld.c, CB1_Overworld).
 */
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "field_specials.h"
#include "fieldmap.h"
#include "item.h"
#include "pokemon_icon.h"
#include "port_platform.h"
#include "script.h"
#include "sound.h"
#include "constants/field_effects.h"
#include "constants/songs.h"

u32 CtrShiny_RollPersonality(void); /* pokemon.c: the SHINY ODDS setting */

/* The setting: off, or how many may be about at once (matches Emerald: 0, 4, 8, 12). */
static const u8 sVisibleMost[] = {0, 4, 8, 12};

#define VW_POOL_MAX      48
#define VW_SLOTS         12
#define VW_SLOT_NONE     0xFF
#define VW_MAGIC         0x5657
#define VW_VIEW_X        14
#define VW_VIEW_NORTH    20
#define VW_VIEW_SOUTH    8
#define VW_SHOW_X        18
#define VW_SHOW_NORTH    24
#define VW_SHOW_SOUTH    11
#define VW_HIDE_X        21
#define VW_HIDE_NORTH    26
#define VW_HIDE_SOUTH    14
#define VW_NEAREST       3
#define VW_MIN_SEP       2
#define VW_FORGET_MAP    80
#define VW_TRIES         48
#define VW_GROW          12
#define VW_VANISH        12
#define VW_MAT_PER_FRAME 2
#define VW_RES_LAND      64
#define VW_RES_WATER     32
#define VW_CONN_CAP      8
#define VW_CONN_DEPTH    24
#define VW_NONE          MAX_SPRITES
#define VW_ICON_BYTES    0x200
#define VW_ICON_FRAME    14

struct VisibleWild
{
    bool8 active, water, shiny;
    bool8 hgss;
    bool8 vanishing;
    bool8 seenShiny;
    u8 spriteId;
    u8 wildSlot;
    u8 matrix;
    u8 scaleTime, scaleFrames;
    u8 level, progress;
    s8 dx, dy;
    s8 faceX, faceY;
    u16 species, idle, sparkle;
    s16 x, y;
    u32 personality;
};

struct VisibleCoord
{
    s16 x;
    s16 y;
};

static struct VisibleWild sVisible[VW_POOL_MAX];
static struct VisibleCoord sLandCand[VW_RES_LAND];
static struct VisibleCoord sWaterCand[VW_RES_WATER];
static u16 sWildSlotMask;
static u16 sVisibleTargetCount;
static u8 sVisibleMapGroup = 0xFF, sVisibleMapNum = 0xFF;
static bool8 sVisibleShifted;
static bool8 sVisibleFromWarp;
static u16 sVisibleSpawnTimer, sVisibleRetry;
static u32 sVisibleFrames;

#define VW_ABS(n) ((n) < 0 ? -(n) : (n))

static bool8 VisibleInView(s16 x, s16 y, s16 px, s16 py)
{
    s16 dx = VW_ABS(x - px);
    s16 dy = y - py;

    return dx <= VW_VIEW_X && dy >= -VW_VIEW_NORTH && dy <= VW_VIEW_SOUTH;
}

static bool8 VisibleInShowRange(s16 x, s16 y, s16 px, s16 py)
{
    s16 dx = VW_ABS(x - px);
    s16 dy = y - py;

    return dx <= VW_SHOW_X && dy >= -VW_SHOW_NORTH && dy <= VW_SHOW_SOUTH;
}

static bool8 VisibleOutHideRange(s16 x, s16 y, s16 px, s16 py)
{
    s16 dx = VW_ABS(x - px);
    s16 dy = y - py;

    return dx > VW_HIDE_X || dy < -VW_HIDE_NORTH || dy > VW_HIDE_SOUTH;
}

static u32 VisibleSetting(void)
{
    const u32 setting = (u32)VitaExtras_GetFast(FAST_EXTRA_VISIBLE_WILD);
    return setting < ARRAY_COUNT(sVisibleMost) ? setting : 0;
}

static u32 VisibleMost(void)
{
    const u32 setting = VisibleSetting();

    if (setting == 0)
        return 0;
    /* WILD ENCOUNTERS (CHEATS) set to OFF keeps them away too */
    if (VitaExtras_GetFast(FAST_EXTRA_ENCOUNTER_RATE) == 1)
        return 0;
    return sVisibleMost[setting];
}

/* Visible here: the option on, and not where the game hides what a wild
 * Pokemon is (the Pokemon Tower's ghosts without the SILPH SCOPE). */
static bool8 VisibleHere(void)
{
    if (!VisibleMost())
        return FALSE;
    if (gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(MAP_POKEMON_TOWER_1F)
     && gSaveBlock1Ptr->location.mapNum >= MAP_NUM(MAP_POKEMON_TOWER_1F)
     && gSaveBlock1Ptr->location.mapNum <= MAP_NUM(MAP_POKEMON_TOWER_7F)
     && !CheckBagHasItem(ITEM_SILPH_SCOPE, 1))
        return FALSE;
    return TRUE;
}

static u8 VisibleAllocSlot(void)
{
    u32 i;

    for (i = 0; i < VW_SLOTS; i++)
    {
        if (!(sWildSlotMask & (1u << i)))
        {
            sWildSlotMask |= (1u << i);
            return (u8)i;
        }
    }
    return VW_SLOT_NONE;
}

static void VisibleFreeSlot(struct VisibleWild *v)
{
    if (v->wildSlot < VW_SLOTS)
        sWildSlotMask &= ~(1u << v->wildSlot);
    v->wildSlot = VW_SLOT_NONE;
}

static bool8 VisibleSpriteValid(const struct VisibleWild *v)
{
    return v->spriteId < MAX_SPRITES && gSprites[v->spriteId].inUse
        && gSprites[v->spriteId].data[7] == VW_MAGIC;
}

/* Fallback icon palette (used only when an HGSS sprite file is unavailable). */
static u16 VisiblePaletteTag(u16 species)
{
    return gMonIconPaletteTable[gMonIconPaletteIndices[species]].tag;
}

static bool8 VisibleLoadPalette(u16 species)
{
    if (IndexOfSpritePaletteTag(VisiblePaletteTag(species)) == 0xFF)
        LoadMonIconPalette(species);
    return IndexOfSpritePaletteTag(VisiblePaletteTag(species)) != 0xFF;
}

static void VisibleDropSprite(struct VisibleWild *v)
{
    if (VisibleSpriteValid(v))
    {
        struct Sprite *sprite = &gSprites[v->spriteId];

        if (v->matrix != 0xFF)
        {
            sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
            sprite->oam.matrixNum = 0;
            FreeOamMatrix(v->matrix);
        }
        if (v->hgss)
        {
            FollowerMon_WildFree(v->wildSlot, sprite);
        }
        else
        {
            u32 i;
            bool8 shared = FALSE;

            DestroyMonIcon(sprite);
            for (i = 0; i < VW_POOL_MAX; i++)
                if (&sVisible[i] != v && sVisible[i].active && !sVisible[i].hgss && VisibleSpriteValid(&sVisible[i])
                 && gMonIconPaletteIndices[sVisible[i].species] == gMonIconPaletteIndices[v->species])
                    shared = TRUE;
            if (!shared)
                FreeMonIconPalette(v->species);
        }
    }
    else if (v->hgss)
    {
        FollowerMon_WildFree(v->wildSlot, NULL);
    }
    VisibleFreeSlot(v);
    v->spriteId = VW_NONE;
    v->matrix = 0xFF;
    v->scaleFrames = 0;
    v->vanishing = FALSE;
}

static void VisibleForgetSprite(struct VisibleWild *v)
{
    if (v->hgss)
        FollowerMon_WildFree(v->wildSlot, NULL);
    VisibleFreeSlot(v);
    v->spriteId = VW_NONE;
    v->matrix = 0xFF;
    v->scaleFrames = 0;
}

static void VisibleRemove(struct VisibleWild *v)
{
    if (v->active)
        VisibleDropSprite(v);
    v->active = FALSE;
}

static void VisibleRemoveAll(void)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
        VisibleRemove(&sVisible[i]);
    sWildSlotMask = 0;
    sVisibleTargetCount = 0;
}

static u32 VisibleActiveCount(void)
{
    u32 i, count = 0;

    for (i = 0; i < VW_POOL_MAX; i++)
        count += sVisible[i].active && !sVisible[i].vanishing;
    return count;
}

static u32 VisibleSpriteCount(void)
{
    u32 i, count = 0;

    for (i = 0; i < VW_POOL_MAX; i++)
        count += sVisible[i].active && !sVisible[i].vanishing && sVisible[i].spriteId != VW_NONE;
    return count;
}

static struct VisibleWild *VisibleAt(s16 x, s16 y, const struct VisibleWild *except)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];

        if (!v->active || v == except)
            continue;
        if ((v->x == x && v->y == y) || (v->progress && v->x + v->dx == x && v->y + v->dy == y))
            return v;
    }
    return NULL;
}

static bool8 VisibleNearAny(s16 x, s16 y, s16 minDist)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        const struct VisibleWild *v = &sVisible[i];

        if (!v->active)
            continue;
        if (VW_ABS(v->x - x) + VW_ABS(v->y - y) < minDist)
            return TRUE;
    }
    return FALSE;
}

/* A tile one can stand on: of the right kind (grass or cave floor, or
 * water), free, and no one on it. */
static bool8 VisibleTileKind(s16 x, s16 y, bool8 water)
{
    u32 encType = MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE);

    if (water ? (encType != TILE_ENCOUNTER_WATER) : (encType != TILE_ENCOUNTER_LAND))
        return FALSE;
    return MapGridGetCollisionAt(x, y) == 0;
}

static bool8 VisibleTileFits(s16 x, s16 y, bool8 water)
{
    return VisibleTileKind(x, y, water) && GetObjectEventIdByXY(x, y) == OBJECT_EVENTS_COUNT;
}

static bool8 VisiblePlayerInWater(void)
{
    return TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) != 0;
}

static void SpriteCB_VisibleIcon(struct Sprite *sprite)
{
    if (sprite->data[6] > 0)
    {
        sprite->data[6]--;
        return;
    }
    {
        const u8 *icon = Port_ResolveAssetPointer(sprite->images);

        if (icon != NULL)
            CpuCopy16(icon + VW_ICON_BYTES * sprite->data[5],
                      (void *)(OBJ_VRAM0 + sprite->oam.tileNum * TILE_SIZE_4BPP), VW_ICON_BYTES);
    }
    sprite->data[5] ^= 1;
    sprite->data[6] = VW_ICON_FRAME;
}

static void VisibleSparkle(const struct VisibleWild *v)
{
    gFieldEffectArguments[0] = v->x - MAP_OFFSET;
    gFieldEffectArguments[1] = v->y - MAP_OFFSET;
    gFieldEffectArguments[2] = 2;
    FieldEffectStart(FLDEFF_SPARKLE);
}

static void VisibleRustle(const struct VisibleWild *v)
{
    u32 behavior = MapGridGetMetatileBehaviorAt(v->x, v->y);
    u8 effect;

    if (v->water)
        effect = FLDEFF_JUMP_SMALL_SPLASH;
    else if (MetatileBehavior_IsTallGrass(behavior))
        effect = FLDEFF_JUMP_TALL_GRASS;
    else if (MetatileBehavior_IsLongGrass(behavior))
        effect = FLDEFF_JUMP_LONG_GRASS;
    else
        effect = FLDEFF_DUST;
    gFieldEffectArguments[0] = v->x;
    gFieldEffectArguments[1] = v->y;
    gFieldEffectArguments[2] = gObjectEvents[gPlayerAvatar.objectEventId].previousElevation;
    gFieldEffectArguments[3] = 2;
    FieldEffectStart(effect);
}

static void VisibleScaleStep(struct VisibleWild *v)
{
    struct Sprite *sprite = &gSprites[v->spriteId];
    u32 t, inv, scale;

    if (v->scaleFrames == 0)
        return;
    if (v->scaleTime < v->scaleFrames)
        v->scaleTime++;
    t = v->scaleTime * 256 / v->scaleFrames;
    if (v->vanishing)
        t = 256 - t;
    inv = 256 - t;
    scale = 256 - inv * inv / 256;
    if (v->scaleTime >= v->scaleFrames && !v->vanishing)
    {
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
        sprite->oam.matrixNum = 0;
        if (v->matrix != 0xFF)
            FreeOamMatrix(v->matrix);
        v->matrix = 0xFF;
        v->scaleFrames = 0;
        if (v->hgss)
        {
            FollowerMon_WildFace(sprite, v->faceX, v->faceY, FALSE);
            StartSpriteAnim(sprite, sprite->animNum);
        }
        return;
    }
    if (v->matrix == 0xFF)
        return;
    if (scale < 16)
        scale = 16;
    SetOamMatrix(v->matrix, 0x10000 / scale, 0, 0, 0x10000 / scale);
}

static void VisibleStartScale(struct VisibleWild *v, u8 frames)
{
    struct Sprite *sprite = &gSprites[v->spriteId];

    v->scaleTime = 0;
    v->scaleFrames = frames;
    if (v->matrix == 0xFF)
        v->matrix = AllocOamMatrix();
    if (v->matrix == 0xFF)
    {
        v->scaleTime = frames;
        return;
    }
    if (v->hgss)
        FollowerMon_WildFace(sprite, 0, 1, FALSE);
    sprite->oam.affineMode = ST_OAM_AFFINE_NORMAL;
    sprite->oam.matrixNum = v->matrix;
    VisibleScaleStep(v);
}

static void VisibleVanish(struct VisibleWild *v)
{
    if (v->vanishing)
        return;
    if (!VisibleSpriteValid(v))
    {
        VisibleRemove(v);
        return;
    }
    v->vanishing = TRUE;
    v->dx = v->dy = 0;
    v->progress = 0;
    VisibleStartScale(v, VW_VANISH);
}

static void VisiblePlace(struct VisibleWild *v)
{
    struct Sprite *sprite = &gSprites[v->spriteId];
    s16 x, y, step = v->progress / 2;   /* half a pixel a frame (VisibleMove) */
    s16 screenX, screenY;

    SetSpritePosToMapCoords(v->x, v->y, &x, &y);
    sprite->x = x + 8 + v->dx * step;
    sprite->y = y + v->dy * step;
    if (v->hgss)
    {
        sprite->y += 16 + sprite->centerToCornerVecY;
        if (v->scaleFrames == 0)
            FollowerMon_WildFace(sprite, v->faceX, v->faceY, v->progress != 0);
    }
    screenX = sprite->x + gSpriteCoordOffsetX;
    screenY = sprite->y + gSpriteCoordOffsetY;
#ifdef VOXEL_SUPPORTED
    if (VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0)
    {
        sprite->invisible = FALSE;
    }
    else
#endif
    {
#ifdef WIDESCREEN_SUPPORTED
        const s16 offXPad = WIDESCREEN_MARGIN + 32;
#else
        const s16 offXPad = 32;
#endif
        sprite->invisible = screenX < -offXPad
                         || screenX >= DISPLAY_WIDTH + offXPad
                         || screenY < -32
                         || screenY >= DISPLAY_HEIGHT + 32;
    }
    /* Further down the screen, drawn over: as the map's objects are. */
    sprite->subpriority = 200 - (((screenY + 64) >> 1) & 0x7F);
}

static bool8 VisibleMakeSprite(struct VisibleWild *v, u8 grow)
{
    struct Sprite *sprite;

    v->matrix = 0xFF;
    v->wildSlot = VisibleAllocSlot();
    if (v->wildSlot == VW_SLOT_NONE)
    {
        v->spriteId = VW_NONE;
        return FALSE;
    }
    v->spriteId = FollowerMon_WildSprite(v->wildSlot, v->species, v->personality, v->shiny);
    v->hgss = v->spriteId < MAX_SPRITES;
    if (!v->hgss)
    {
        VisibleFreeSlot(v);
        if (!VisibleLoadPalette(v->species))
        {
            v->spriteId = VW_NONE;
            return FALSE;
        }
        v->spriteId = CreateMonIcon(v->species, SpriteCB_VisibleIcon, 0, 0, 1, v->personality, TRUE);
        if (v->spriteId >= MAX_SPRITES)
        {
            v->spriteId = VW_NONE;
            return FALSE;
        }
        gSprites[v->spriteId].affineAnims = gDummySpriteAffineAnimTable;
    }
    sprite = &gSprites[v->spriteId];
    sprite->coordOffsetEnabled = TRUE;
    sprite->oam.priority = 2;
    sprite->data[7] = VW_MAGIC;
    sprite->data[5] = 0;
    sprite->data[6] = 0;
    v->scaleFrames = 0;
    VisiblePlace(v);
    if (grow > 0)
    {
        VisibleStartScale(v, grow);
        VisiblePlace(v);
    }
    else if (v->hgss)
    {
        FollowerMon_WildFace(sprite, v->faceX, v->faceY, FALSE);
    }
    if (v->shiny && !v->seenShiny)
    {
        v->seenShiny = TRUE;
        VisibleSparkle(v);
        PlaySE(SE_SHINY);
    }
    return TRUE;
}

static bool8 VisibleInitMon(struct VisibleWild *v, u16 headerId, s16 x, s16 y, bool8 water, u8 idleDelay)
{
    const struct WildPokemonInfo *info;
    const struct WildPokemon *wild;
    u8 slot;

    info = water ? gWildMonHeaders[headerId].waterMonsInfo : GetLandMonsInfo(headerId);
    if (info == NULL)
        return FALSE;
    slot = water ? ChooseWildMonIndex_WaterRock() : ChooseWildMonIndex_Land();
    wild = &info->wildPokemon[slot];
    v->level = ChooseWildMonLevel(wild);
    v->species = wild->species;
    if (v->species == SPECIES_UNOWN)
    {
        s32 chamber = gSaveBlock1Ptr->location.mapNum - MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_MONEAN_CHAMBER);

        if (water || chamber < 0 || chamber >= (s32)ARRAY_COUNT(sUnownLetterSlots))
            return FALSE;
        v->personality = GenerateUnownPersonalityByLetter(sUnownLetterSlots[chamber][slot]);
    }
    else
    {
        v->personality = CtrShiny_RollPersonality();
    }
    v->shiny = GET_SHINY_VALUE(GetPlayerTrainerId(), v->personality) < SHINY_ODDS;
    v->seenShiny = FALSE;
    v->hgss = FALSE;
    v->faceX = 0;
    v->faceY = 1;
    v->water = water;
    v->x = x;
    v->y = y;
    v->dx = v->dy = 0;
    v->progress = 0;
    v->vanishing = FALSE;
    v->spriteId = VW_NONE;
    v->wildSlot = VW_SLOT_NONE;
    v->matrix = 0xFF;
    v->scaleTime = 0;
    v->scaleFrames = 0;
    v->idle = idleDelay + Random() % 90;
    v->sparkle = 0;
    v->active = TRUE;
    return TRUE;
}

static struct VisibleWild *VisibleAllocEntity(void)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
        if (!sVisible[i].active)
            return &sVisible[i];
    return NULL;
}

static u32 VisibleCountInRect(s16 x0, s16 y0, s16 x1, s16 y1)
{
    u32 i, count = 0;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        const struct VisibleWild *v = &sVisible[i];

        if (!v->active || v->vanishing)
            continue;
        if (v->x >= x0 && v->x <= x1 && v->y >= y0 && v->y <= y1)
            count++;
    }
    return count;
}

static u32 VisiblePopulateRegion(u16 headerId, s16 x0, s16 y0, s16 x1, s16 y1, u32 maxCap, s16 px, s16 py, bool8 skipInView)
{
    const struct WildPokemonInfo *landInfo, *waterInfo;
    u32 landSeen = 0, waterSeen = 0, landCount = 0, waterCount = 0;
    u32 totalTiles, desired, existing, wantLand, wantWater, spawned = 0;
    u32 percent, most, i;
    s16 x, y;
    s16 mapW, mapH;

    if (headerId == HEADER_NONE || maxCap == 0 || gMapHeader.mapLayout == NULL)
        return 0;
    percent = EncounterRatePercent();
    most = VisibleMost();
    if (percent == 0 || most == 0)
        return 0;

    landInfo = GetLandMonsInfo(headerId);
    waterInfo = gWildMonHeaders[headerId].waterMonsInfo;
    if (landInfo == NULL && waterInfo == NULL)
        return 0;

    mapW = gMapHeader.mapLayout->width;
    mapH = gMapHeader.mapLayout->height;
    if (x0 < MAP_OFFSET)
        x0 = MAP_OFFSET;
    if (y0 < MAP_OFFSET)
        y0 = MAP_OFFSET;
    if (x1 >= MAP_OFFSET + mapW)
        x1 = MAP_OFFSET + mapW - 1;
    if (y1 >= MAP_OFFSET + mapH)
        y1 = MAP_OFFSET + mapH - 1;

    for (y = y0; y <= y1; y++)
    {
        for (x = x0; x <= x1; x++)
        {
            u32 encType = MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE);

            if (VW_ABS(x - px) < VW_NEAREST && VW_ABS(y - py) < VW_NEAREST)
                continue;
            if (skipInView && VisibleInView(x, y, px, py))
                continue;
            if (MapGridGetCollisionAt(x, y) != 0 || GetObjectEventIdByXY(x, y) != OBJECT_EVENTS_COUNT)
                continue;
            if (landInfo != NULL && encType == TILE_ENCOUNTER_LAND)
            {
                landSeen++;
                if (landCount < VW_RES_LAND)
                {
                    sLandCand[landCount].x = x;
                    sLandCand[landCount].y = y;
                    landCount++;
                }
                else
                {
                    u32 r = Random() % landSeen;
                    if (r < VW_RES_LAND)
                    {
                        sLandCand[r].x = x;
                        sLandCand[r].y = y;
                    }
                }
            }
            else if (waterInfo != NULL && encType == TILE_ENCOUNTER_WATER)
            {
                waterSeen++;
                if (waterCount < VW_RES_WATER)
                {
                    sWaterCand[waterCount].x = x;
                    sWaterCand[waterCount].y = y;
                    waterCount++;
                }
                else
                {
                    u32 r = Random() % waterSeen;
                    if (r < VW_RES_WATER)
                    {
                        sWaterCand[r].x = x;
                        sWaterCand[r].y = y;
                    }
                }
            }
        }
    }

    totalTiles = landSeen + waterSeen;
    if (totalTiles == 0)
        return 0;

    desired = (totalTiles * percent * most) / (16u * 100u * 8u);
    if (desired < 2 && totalTiles >= 4)
        desired = 2;
    if (desired < most && totalTiles >= most * 6u)
        desired = most;
    if (desired > maxCap)
        desired = maxCap;
    existing = VisibleCountInRect(x0, y0, x1, y1);
    if (existing >= desired)
        return 0;
    desired -= existing;

    wantLand = (desired * landSeen + totalTiles / 2) / totalTiles;
    if (landSeen > 0 && wantLand == 0 && desired > 0)
        wantLand = 1;
    if (wantLand > landCount)
        wantLand = landCount;
    wantWater = desired > wantLand ? desired - wantLand : 0;
    if (wantWater > waterCount)
        wantWater = waterCount;

    for (i = 0; i < landCount && spawned < wantLand; i++)
    {
        u32 j = i + (Random() % (landCount - i));
        struct VisibleCoord tmp = sLandCand[i];
        struct VisibleWild *v;

        sLandCand[i] = sLandCand[j];
        sLandCand[j] = tmp;
        if (VisibleNearAny(sLandCand[i].x, sLandCand[i].y, VW_MIN_SEP))
            continue;
        v = VisibleAllocEntity();
        if (v == NULL)
            break;
        if (VisibleInitMon(v, headerId, sLandCand[i].x, sLandCand[i].y, FALSE, 30))
            spawned++;
    }

    for (i = 0; i < waterCount && spawned < wantLand + wantWater; i++)
    {
        u32 j = i + (Random() % (waterCount - i));
        struct VisibleCoord tmp = sWaterCand[i];
        struct VisibleWild *v;

        sWaterCand[i] = sWaterCand[j];
        sWaterCand[j] = tmp;
        if (VisibleNearAny(sWaterCand[i].x, sWaterCand[i].y, VW_MIN_SEP))
            continue;
        v = VisibleAllocEntity();
        if (v == NULL)
            break;
        if (VisibleInitMon(v, headerId, sWaterCand[i].x, sWaterCand[i].y, TRUE, 30))
            spawned++;
    }

    return spawned;
}

static void VisiblePrefetchTables(u16 headerId)
{
    const struct WildPokemonInfo *land, *water;
    u16 species[LAND_WILD_COUNT + WATER_WILD_COUNT];
    u32 i, j, count = 0;

    if (headerId == HEADER_NONE)
        return;
    land = GetLandMonsInfo(headerId);
    water = gWildMonHeaders[headerId].waterMonsInfo;
    for (i = 0; land != NULL && i < LAND_WILD_COUNT; i++)
        species[count++] = land->wildPokemon[i].species;
    for (i = 0; water != NULL && i < WATER_WILD_COUNT; i++)
        species[count++] = water->wildPokemon[i].species;
    for (i = 0; i < count; i++)
    {
        for (j = 0; j < i && species[j] != species[i]; j++)
            ;
        if (j == i)
            FollowerMon_WildPrefetch(species[i]);
    }
}

static u32 VisibleLayoutEncounterType(const struct MapLayout *layout, u16 metatile)
{
    const u32 *attributes;

    if (metatile < NUM_METATILES_IN_PRIMARY)
    {
        if (layout->primaryTileset == NULL)
            return TILE_ENCOUNTER_NONE;
        attributes = PORT_RES(layout->primaryTileset->metatileAttributes);
        if (attributes == NULL)
            return TILE_ENCOUNTER_NONE;
        return ExtractMetatileAttribute(attributes[metatile], METATILE_ATTRIBUTE_ENCOUNTER_TYPE);
    }
    else if (metatile < NUM_METATILES_TOTAL)
    {
        if (layout->secondaryTileset == NULL)
            return TILE_ENCOUNTER_NONE;
        attributes = PORT_RES(layout->secondaryTileset->metatileAttributes);
        if (attributes == NULL)
            return TILE_ENCOUNTER_NONE;
        return ExtractMetatileAttribute(attributes[metatile - NUM_METATILES_IN_PRIMARY], METATILE_ATTRIBUTE_ENCOUNTER_TYPE);
    }
    return TILE_ENCOUNTER_NONE;
}

static u32 VisiblePopulateNeighbour(const struct MapConnection *conn, u16 headerId, u32 maxCap, s16 px, s16 py, bool8 skipInView)
{
    const struct MapHeader *neighbour = GetMapHeaderFromConnection(conn);
    const struct MapLayout *layout;
    const struct WildPokemonInfo *landInfo, *waterInfo;
    const u16 *layoutMap;
    u32 landSeen = 0, waterSeen = 0, landCount = 0, waterCount = 0;
    u32 totalTiles, desired, existing, wantLand, wantWater, spawned = 0;
    u32 percent, most, i;
    s16 nx0, ny0, nx1, ny1, nx, ny;
    s16 gx0, gy0, gx1, gy1;
    s16 mapW, mapH;

    if (neighbour == NULL || neighbour->mapLayout == NULL || gMapHeader.mapLayout == NULL)
        return 0;
    layout = neighbour->mapLayout;
    if (layout->map == NULL || layout->primaryTileset == NULL || layout->secondaryTileset == NULL)
        return 0;

    percent = EncounterRatePercent();
    most = VisibleMost();
    if (percent == 0 || most == 0 || maxCap == 0)
        return 0;

    landInfo = GetLandMonsInfo(headerId);
    waterInfo = gWildMonHeaders[headerId].waterMonsInfo;
    if (landInfo == NULL && waterInfo == NULL)
        return 0;

    mapW = gMapHeader.mapLayout->width;
    mapH = gMapHeader.mapLayout->height;
    nx0 = 0;
    ny0 = 0;
    nx1 = layout->width - 1;
    ny1 = layout->height - 1;

    switch (conn->direction)
    {
    case CONNECTION_NORTH:
        if (layout->height > VW_CONN_DEPTH)
            ny0 = layout->height - VW_CONN_DEPTH;
        break;
    case CONNECTION_SOUTH:
        if (layout->height > VW_CONN_DEPTH)
            ny1 = VW_CONN_DEPTH - 1;
        break;
    case CONNECTION_WEST:
        if (layout->width > VW_CONN_DEPTH)
            nx0 = layout->width - VW_CONN_DEPTH;
        break;
    case CONNECTION_EAST:
        if (layout->width > VW_CONN_DEPTH)
            nx1 = VW_CONN_DEPTH - 1;
        break;
    default:
        return 0;
    }

    layoutMap = PORT_RES(layout->map);
    if (layoutMap == NULL)
        return 0;

    for (ny = ny0; ny <= ny1; ny++)
    {
        for (nx = nx0; nx <= nx1; nx++)
        {
            u16 raw = layoutMap[ny * layout->width + nx];
            u16 metatile = raw & MAPGRID_METATILE_ID_MASK;
            u32 collision = (raw & MAPGRID_COLLISION_MASK) >> MAPGRID_COLLISION_SHIFT;
            u32 encType = VisibleLayoutEncounterType(layout, metatile);
            s16 gx, gy;

            if (collision != 0)
                continue;
            switch (conn->direction)
            {
            case CONNECTION_NORTH:
                gx = MAP_OFFSET + conn->offset + nx;
                gy = MAP_OFFSET - (layout->height - ny);
                break;
            case CONNECTION_SOUTH:
                gx = MAP_OFFSET + conn->offset + nx;
                gy = MAP_OFFSET + mapH + ny;
                break;
            case CONNECTION_WEST:
                gx = MAP_OFFSET - (layout->width - nx);
                gy = MAP_OFFSET + conn->offset + ny;
                break;
            default:
                gx = MAP_OFFSET + mapW + nx;
                gy = MAP_OFFSET + conn->offset + ny;
                break;
            }
            if (VW_ABS(gx - px) < VW_NEAREST && VW_ABS(gy - py) < VW_NEAREST)
                continue;
            if (skipInView && VisibleInView(gx, gy, px, py))
                continue;

            if (landInfo != NULL && encType == TILE_ENCOUNTER_LAND)
            {
                landSeen++;
                if (landCount < VW_RES_LAND)
                {
                    sLandCand[landCount].x = gx;
                    sLandCand[landCount].y = gy;
                    landCount++;
                }
                else
                {
                    u32 r = Random() % landSeen;
                    if (r < VW_RES_LAND)
                    {
                        sLandCand[r].x = gx;
                        sLandCand[r].y = gy;
                    }
                }
            }
            else if (waterInfo != NULL && encType == TILE_ENCOUNTER_WATER)
            {
                waterSeen++;
                if (waterCount < VW_RES_WATER)
                {
                    sWaterCand[waterCount].x = gx;
                    sWaterCand[waterCount].y = gy;
                    waterCount++;
                }
                else
                {
                    u32 r = Random() % waterSeen;
                    if (r < VW_RES_WATER)
                    {
                        sWaterCand[r].x = gx;
                        sWaterCand[r].y = gy;
                    }
                }
            }
        }
    }

    totalTiles = landSeen + waterSeen;
    if (totalTiles == 0)
        return 0;

    VisiblePrefetchTables(headerId);
    desired = (totalTiles * percent * most) / (16u * 100u * 8u);
    if (desired < 2 && totalTiles >= 4)
        desired = 2;
    if (desired > maxCap)
        desired = maxCap;
    switch (conn->direction)
    {
    case CONNECTION_NORTH:
        gx0 = MAP_OFFSET + conn->offset + nx0;
        gx1 = MAP_OFFSET + conn->offset + nx1;
        gy0 = MAP_OFFSET - (layout->height - ny0);
        gy1 = MAP_OFFSET - (layout->height - ny1);
        break;
    case CONNECTION_SOUTH:
        gx0 = MAP_OFFSET + conn->offset + nx0;
        gx1 = MAP_OFFSET + conn->offset + nx1;
        gy0 = MAP_OFFSET + mapH + ny0;
        gy1 = MAP_OFFSET + mapH + ny1;
        break;
    case CONNECTION_WEST:
        gx0 = MAP_OFFSET - (layout->width - nx0);
        gx1 = MAP_OFFSET - (layout->width - nx1);
        gy0 = MAP_OFFSET + conn->offset + ny0;
        gy1 = MAP_OFFSET + conn->offset + ny1;
        break;
    default:
        gx0 = MAP_OFFSET + mapW + nx0;
        gx1 = MAP_OFFSET + mapW + nx1;
        gy0 = MAP_OFFSET + conn->offset + ny0;
        gy1 = MAP_OFFSET + conn->offset + ny1;
        break;
    }
    existing = VisibleCountInRect(gx0, gy0, gx1, gy1);
    if (existing >= desired)
        return 0;
    desired -= existing;

    wantLand = (desired * landSeen + totalTiles / 2) / totalTiles;
    if (landSeen > 0 && wantLand == 0 && desired > 0)
        wantLand = 1;
    if (wantLand > landCount)
        wantLand = landCount;
    wantWater = desired > wantLand ? desired - wantLand : 0;
    if (wantWater > waterCount)
        wantWater = waterCount;

    for (i = 0; i < landCount && spawned < wantLand; i++)
    {
        u32 j = i + (Random() % (landCount - i));
        struct VisibleCoord tmp = sLandCand[i];
        struct VisibleWild *v;

        sLandCand[i] = sLandCand[j];
        sLandCand[j] = tmp;
        if (VisibleNearAny(sLandCand[i].x, sLandCand[i].y, VW_MIN_SEP))
            continue;
        v = VisibleAllocEntity();
        if (v == NULL)
            break;
        if (VisibleInitMon(v, headerId, sLandCand[i].x, sLandCand[i].y, FALSE, 30))
            spawned++;
    }

    for (i = 0; i < waterCount && spawned < wantLand + wantWater; i++)
    {
        u32 j = i + (Random() % (waterCount - i));
        struct VisibleCoord tmp = sWaterCand[i];
        struct VisibleWild *v;

        sWaterCand[i] = sWaterCand[j];
        sWaterCand[j] = tmp;
        if (VisibleNearAny(sWaterCand[i].x, sWaterCand[i].y, VW_MIN_SEP))
            continue;
        v = VisibleAllocEntity();
        if (v == NULL)
            break;
        if (VisibleInitMon(v, headerId, sWaterCand[i].x, sWaterCand[i].y, TRUE, 30))
            spawned++;
    }

    return spawned;
}

static void VisiblePopulateMap(u16 mainHeaderId, s16 px, s16 py, bool8 shifted)
{
    u32 total = 0;

    if (gMapHeader.mapLayout == NULL)
        return;

    sVisibleFromWarp = !shifted;

    if (mainHeaderId != HEADER_NONE)
    {
        s16 x1 = MAP_OFFSET + gMapHeader.mapLayout->width - 1;
        s16 y1 = MAP_OFFSET + gMapHeader.mapLayout->height - 1;

        total += VisiblePopulateRegion(mainHeaderId, MAP_OFFSET, MAP_OFFSET, x1, y1, VW_POOL_MAX - VW_CONN_CAP, px, py, shifted);
    }

    if (gMapHeader.connections != NULL)
    {
        s32 i;
        const struct MapConnection *conn = gMapHeader.connections->connections;

        for (i = 0; i < gMapHeader.connections->count && total < VW_POOL_MAX; i++, conn++)
        {
            u16 connHeaderId;
            u32 cap = VW_POOL_MAX - total;

            if (conn->direction == CONNECTION_DIVE || conn->direction == CONNECTION_EMERGE)
                continue;
            connHeaderId = GetWildMonHeaderIdForMap(conn->mapGroup, conn->mapNum);
            if (connHeaderId == HEADER_NONE)
                continue;
            if (cap > VW_CONN_CAP)
                cap = VW_CONN_CAP;
            total += VisiblePopulateNeighbour(conn, connHeaderId, cap, px, py, shifted);
        }
    }

    sVisibleTargetCount = (u16)VisibleActiveCount();
}

static bool8 VisibleTrySpawn(u16 headerId)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    struct VisibleWild *v = VisibleAllocEntity();
    u32 tries;
    s16 x, y;
    bool8 water;

    if (v == NULL)
        return FALSE;
    for (tries = 0; tries < VW_TRIES; tries++)
    {
        x = player->currentCoords.x + (s16)(Random() % (2 * VW_VIEW_X + 1)) - VW_VIEW_X;
        y = player->currentCoords.y + (s16)(Random() % (VW_VIEW_NORTH + VW_VIEW_SOUTH + 1)) - VW_VIEW_NORTH;
        if (VW_ABS(x - player->currentCoords.x) < VW_NEAREST && VW_ABS(y - player->currentCoords.y) < VW_NEAREST)
            continue;
        if (VisibleAt(x, y, NULL) != NULL)
            continue;
        water = MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE) == TILE_ENCOUNTER_WATER;
        if (!VisibleTileFits(x, y, water))
            continue;
        if (!VisibleInitMon(v, headerId, x, y, water, VW_GROW + 30))
            continue;
        if (!IsWildLevelAllowedByRepel(v->level))
            return TRUE;
        if (VisibleSpriteCount() < VisibleMost() && VisibleMakeSprite(v, VW_GROW))
            VisibleRustle(v);
        return TRUE;
    }
    return FALSE;
}

static void VisiblePrefetch(u16 headerId)
{
    static u16 sHeader = HEADER_NONE;
    static u32 sSignature, sLastLook;
    const struct WildPokemonInfo *land, *water;
    u16 species[LAND_WILD_COUNT + WATER_WILD_COUNT];
    u32 i, j, count = 0, signature = 0;

    if (headerId == sHeader && sVisibleFrames - sLastLook < 60)
        return;
    sLastLook = sVisibleFrames;
    land = headerId != HEADER_NONE ? GetLandMonsInfo(headerId) : NULL;
    water = headerId != HEADER_NONE ? gWildMonHeaders[headerId].waterMonsInfo : NULL;
    for (i = 0; land != NULL && i < LAND_WILD_COUNT; i++)
        species[count++] = land->wildPokemon[i].species;
    for (i = 0; water != NULL && i < WATER_WILD_COUNT; i++)
        species[count++] = water->wildPokemon[i].species;
    for (i = 0; i < count; i++)
        signature = signature * 31 + species[i];
    if (headerId == sHeader && signature == sSignature)
        return;
    sHeader = headerId;
    sSignature = signature;
    for (i = 0; i < count; i++)
    {
        for (j = 0; j < i && species[j] != species[i]; j++)
            ;
        if (j == i)
            FollowerMon_WildPrefetch(species[i]);
    }
}

static void VisibleStartBattle(struct VisibleWild *v)
{
    ZeroEnemyPartyMons();
    CreateMon(&gEnemyParty[0], v->species, v->level, USE_RANDOM_IVS, TRUE, v->personality, OT_ID_PLAYER_ID, 0);
    VisibleRemove(v);
    StartWildBattle();
}

/*
 * One wandering step. `playerStill`: the player stands on (px, py), not between tiles. A
 * walking player holds both the tile it left (ppx, ppy) and the one it is entering, and is
 * never marked to be attacked: the mark would be answered after the step, tiles away.
 */
static void VisibleMove(struct VisibleWild *v, s16 px, s16 py, s16 ppx, s16 ppy, bool8 inWater, bool8 playerStill)
{
    static const s8 sSteps[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    u32 dir;
    s16 tx, ty;

    if (v->progress)
    {
        /* Half a pixel a frame: a stroll. */
        if (++v->progress >= 32)
        {
            v->x += v->dx;
            v->y += v->dy;
            v->dx = v->dy = 0;
            v->progress = 0;
            v->idle = 40 + Random() % 120;
        }
        return;
    }
    if (v->idle && --v->idle)
        return;
    dir = Random() % 4;
    tx = v->x + sSteps[dir][0];
    ty = v->y + sSteps[dir][1];
    if (tx == px && ty == py)
    {
        if (!playerStill)
            v->idle = 30;
        else if (v->water == inWater)
            v->idle = 0xFFFF;
        else
            v->idle = 60;
        return;
    }
    if (!playerStill && tx == ppx && ty == ppy)
    {
        v->idle = 30;
        return;
    }
    if (!VisibleTileFits(tx, ty, v->water) || VisibleAt(tx, ty, v) != NULL)
    {
        v->idle = 30;
        return;
    }
    v->dx = v->faceX = sSteps[dir][0];
    v->dy = v->faceY = sSteps[dir][1];
    v->progress = 1;
}

void VisibleWild_CameraShift(s16 dx, s16 dy)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        sVisible[i].x -= dx;
        sVisible[i].y -= dy;
    }
    sVisibleShifted = TRUE;
}

bool8 VisibleWild_StepOnto(void)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    bool8 inWater = VisiblePlayerInWater();
    s16 px = player->currentCoords.x, py = player->currentCoords.y;
    u32 i;

    if (!VisibleHere())
        return FALSE;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];

        if (!v->active || v->vanishing || v->spriteId == VW_NONE || v->water != inWater)
            continue;
        if ((v->x == px && v->y == py) || (v->progress && v->x + v->dx == px && v->y + v->dy == py))
        {
            VisibleStartBattle(v);
            return TRUE;
        }
    }
    return FALSE;
}

bool8 VisibleWild_View(u32 i, u16 *species, s16 *x, s16 *y, s8 *dx, s8 *dy, u8 *progress, u8 *spriteId)
{
    const struct VisibleWild *v;

    if (i >= VW_POOL_MAX)
        return FALSE;
    v = &sVisible[i];
    if (!v->active || !VisibleSpriteValid(v))
        return FALSE;
    *species = v->species;
    *x = v->x - MAP_OFFSET;
    *y = v->y - MAP_OFFSET;
    *dx = v->dx;
    *dy = v->dy;
    *progress = v->progress;
    *spriteId = v->spriteId;
    return TRUE;
}

bool8 VisibleWild_IsSprite(u8 spriteId)
{
    return spriteId < MAX_SPRITES && gSprites[spriteId].inUse
        && gSprites[spriteId].data[7] == VW_MAGIC;
}

/* Where VisiblePlace stands sprite `spriteId`, on the map without MAP_OFFSET, in 1/16 tile.
 * The 3D view places it from this instead of from the player's picture, which is not on the
 * map while Fly carries the player or a warp hides it. */
bool8 VisibleWild_SpriteTile(u8 spriteId, s16 *x16, s16 *y16)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        const struct VisibleWild *v = &sVisible[i];
        s16 step;

        if (!v->active || v->spriteId != spriteId)
            continue;
        step = v->progress / 2;
        *x16 = (s16)((v->x - MAP_OFFSET) * 16 + v->dx * step);
        *y16 = (s16)((v->y - MAP_OFFSET) * 16 + v->dy * step);
        return TRUE;
    }
    return FALSE;
}

static void VisibleStreamSprites(s16 px, s16 py, u32 most, bool8 settled)
{
    u32 i, materialized = 0;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];

        if (!v->active || v->spriteId == VW_NONE || v->vanishing)
            continue;
        if (VisibleOutHideRange(v->x, v->y, px, py) || !IsWildLevelAllowedByRepel(v->level))
            VisibleDropSprite(v);
    }

    while (VisibleSpriteCount() > most)
    {
        struct VisibleWild *farthest = NULL;
        s32 maxDist = -1;

        for (i = 0; i < VW_POOL_MAX; i++)
        {
            struct VisibleWild *v = &sVisible[i];
            s32 d;

            if (!v->active || v->spriteId == VW_NONE || v->vanishing)
                continue;
            d = VW_ABS(v->x - px) + VW_ABS(v->y - py);
            if (d > maxDist)
            {
                maxDist = d;
                farthest = v;
            }
        }
        if (farthest == NULL)
            break;
        VisibleVanish(farthest);
    }

    if (!settled || sVisibleRetry)
        return;

    while (materialized < VW_MAT_PER_FRAME && VisibleSpriteCount() < most)
    {
        struct VisibleWild *best = NULL;
        s32 bestDist = 0x7FFFFFFF;

        for (i = 0; i < VW_POOL_MAX; i++)
        {
            struct VisibleWild *v = &sVisible[i];
            s16 adx, ady;
            s32 d;

            if (!v->active || v->spriteId != VW_NONE || v->vanishing)
                continue;
            if (!VisibleInShowRange(v->x, v->y, px, py))
                continue;
            if (!IsWildLevelAllowedByRepel(v->level))
                continue;
            adx = VW_ABS(v->x - px);
            ady = VW_ABS(v->y - py);
            d = adx + ady;
            if (d < bestDist)
            {
                bestDist = d;
                best = v;
            }
        }
        if (best == NULL)
            break;
        {
            bool8 inView = VisibleInView(best->x, best->y, px, py);
            u8 grow = (inView && !sVisibleFromWarp) ? VW_GROW : 0;

            if (!VisibleMakeSprite(best, grow))
            {
                sVisibleRetry = 30;
                break;
            }
            if (grow > 0)
                VisibleRustle(best);
        }
        materialized++;
    }
    if (materialized == 0 && settled)
        sVisibleFromWarp = FALSE;
}

/*
 * `live`: no script or field lock - they wander, also while the player walks (Emerald 3DS froze
 * them then). `idle`: live with the player standing still - only then do they attack, at once,
 * never later. (ppx, ppy): the tile the player is leaving.
 */
static void VisibleUpdateActive(s16 px, s16 py, s16 ppx, s16 ppy, bool8 inWater, bool8 live, bool8 idle)
{
    u32 i;

    for (i = 0; i < VW_POOL_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];

        if (!v->active)
            continue;
        if (VW_ABS(v->x - px) > VW_FORGET_MAP || VW_ABS(v->y - py) > VW_FORGET_MAP)
        {
            VisibleRemove(v);
            continue;
        }
        if (v->spriteId == VW_NONE)
            continue;
        if (v->scaleFrames)
        {
            VisibleScaleStep(v);
            if (v->vanishing && v->scaleTime >= v->scaleFrames)
            {
                VisibleRemove(v);
                continue;
            }
        }
        else if (live)
        {
            /* The mark to attack (VisibleMove) stands only beside a player still standing there. */
            bool8 marked = v->idle == 0xFFFF && idle && VW_ABS(v->x - px) + VW_ABS(v->y - py) == 1;
            bool8 touched = marked || (idle && v->x == px && v->y == py && !v->progress);

            if (touched && v->water == inWater && (!marked || IsWildLevelAllowedByRepel(v->level)))
            {
                VisibleStartBattle(v);
                return;
            }
            if (v->idle == 0xFFFF)
                v->idle = 60;
            if (v->x >= 0 && v->x < VMap.Xsize && v->y >= 0 && v->y < VMap.Ysize)
                VisibleMove(v, px, py, ppx, ppy, inWater, idle);
            if (v->shiny && ++v->sparkle >= 40)
            {
                v->sparkle = 0;
                VisibleSparkle(v);
            }
        }
        VisiblePlace(v);
    }
}

static void VisibleRespawnTick(u16 headerId, bool8 canSpawn, bool8 settled, u32 percent)
{
    if (!canSpawn || !settled || headerId == HEADER_NONE || percent == 0 || sWildEncountersDisabled)
        return;
    if (VisibleActiveCount() >= sVisibleTargetCount)
        return;
    if (sVisibleSpawnTimer)
    {
        sVisibleSpawnTimer--;
        return;
    }
    if (VisibleTrySpawn(headerId))
        sVisibleSpawnTimer = 60 * 100 / percent;
    else
        sVisibleSpawnTimer = 15;
}

/* StandardWildEncounter: only the highest density tier (HIGH / MANY) replaces random encounters. */
static bool8 VisibleWild_ReplacesRandom(void)
{
    return VisibleHere() && VisibleSetting() == ARRAY_COUNT(sVisibleMost) - 1;
}

void VisibleWild_Update(void)
{
    struct ObjectEvent *player;
    u16 headerId;
    u32 i, percent, most;
    bool8 inWater, idle, canSpawn, settled;

    sVisibleFrames++;
    most = VisibleMost();
    if (!most || !VisibleHere() || gMain.callback2 != CB2_Overworld)
    {
        if (!most || !VisibleHere())
            VisibleRemoveAll();
        return;
    }
    for (i = 0; i < VW_POOL_MAX; i++)
        if (sVisible[i].active && sVisible[i].spriteId != VW_NONE && !VisibleSpriteValid(&sVisible[i]))
        {
            VisibleForgetSprite(&sVisible[i]);
            sVisibleFromWarp = TRUE;
        }

    headerId = GetCurrentMapWildMonHeaderId();
    player = &gObjectEvents[gPlayerAvatar.objectEventId];

    if (gSaveBlock1Ptr->location.mapGroup != sVisibleMapGroup || gSaveBlock1Ptr->location.mapNum != sVisibleMapNum)
    {
        if (sVisibleShifted)
        {
            for (i = 0; i < VW_POOL_MAX; i++)
            {
                if (!sVisible[i].active)
                    continue;
                if (sVisible[i].x >= 0 && sVisible[i].x < VMap.Xsize
                 && sVisible[i].y >= 0 && sVisible[i].y < VMap.Ysize
                 && !VisibleTileKind(sVisible[i].x, sVisible[i].y, sVisible[i].water))
                    VisibleVanish(&sVisible[i]);
            }
        }
        else
        {
            VisibleRemoveAll();
        }
        sVisibleMapGroup = gSaveBlock1Ptr->location.mapGroup;
        sVisibleMapNum = gSaveBlock1Ptr->location.mapNum;
        if (!sWildEncountersDisabled)
            VisiblePopulateMap(headerId, player->currentCoords.x, player->currentCoords.y, sVisibleShifted);
        sVisibleSpawnTimer = 0;
    }
    sVisibleShifted = FALSE;

    VisiblePrefetch(headerId);
    inWater = VisiblePlayerInWater();
    canSpawn = !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled();
    idle = canSpawn && gPlayerAvatar.tileTransitionState == T_NOT_MOVING;
    settled = FollowerMon_PalettesSettled();
    percent = EncounterRatePercent();

    if (sVisibleRetry)
        sVisibleRetry--;

    VisibleStreamSprites(player->currentCoords.x, player->currentCoords.y, most, settled);
    VisibleUpdateActive(player->currentCoords.x, player->currentCoords.y,
                        player->previousCoords.x, player->previousCoords.y, inWater, canSpawn, idle);
    VisibleRespawnTick(headerId, canSpawn, settled, percent);
}
#endif
