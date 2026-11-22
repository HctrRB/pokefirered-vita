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

static bool8 UnlockedTanobyOrAreNotInTanoby(void);
static u32 GenerateUnownPersonalityByLetter(u8 letter);
static bool8 IsWildLevelAllowedByRepel(u8 level);
static void ApplyFluteEncounterRateMod(u32 *rate);
static u8 GetFluteEncounterRateModType(void);
static void ApplyCleanseTagEncounterRateMod(u32 *rate);
static bool8 IsLeadMonHoldingCleanseTag(void);
static u16 WildEncounterRandom(void);
static void AddToWildEncounterRateBuff(u8 encouterRate);
#ifdef PLATFORM_VITA
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

static u16 GetCurrentMapWildMonHeaderId(void)
{
    u16 i;

    for (i = 0; ; i++)
    {
        const struct WildPokemonHeader * wildHeader = &gWildMonHeaders[i];
        if (wildHeader->mapGroup == MAP_GROUP(MAP_UNDEFINED))
            break;

        if (gWildMonHeaders[i].mapGroup == gSaveBlock1Ptr->location.mapGroup &&
            gWildMonHeaders[i].mapNum == gSaveBlock1Ptr->location.mapNum)
        {
            if (gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(MAP_SIX_ISLAND_ALTERING_CAVE) &&
                gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SIX_ISLAND_ALTERING_CAVE))
            {
                u16 alteringCaveId = VarGet(VAR_ALTERING_CAVE_WILD_SET);
                if (alteringCaveId >= NUM_ALTERING_CAVE_TABLES)
                    alteringCaveId = 0;

                i += alteringCaveId;
            }

            if (!UnlockedTanobyOrAreNotInTanoby())
                break;
            return i;
        }
    }

    return HEADER_NONE;
}

static bool8 UnlockedTanobyOrAreNotInTanoby(void)
{
    if (FlagGet(FLAG_SYS_UNLOCKED_TANOBY_RUINS))
        return TRUE;
    if (gSaveBlock1Ptr->location.mapGroup != MAP_GROUP(MAP_SEVEN_ISLAND_TANOBY_RUINS_DILFORD_CHAMBER))
        return TRUE;
    if (!(gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_MONEAN_CHAMBER)
    ||  gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_LIPTOO_CHAMBER)
    ||  gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_WEEPTH_CHAMBER)
    ||  gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_DILFORD_CHAMBER)
    ||  gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_SCUFIB_CHAMBER)
    ||  gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_RIXY_CHAMBER)
    ||  gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_VIAPOIS_CHAMBER)
    ))
        return TRUE;
    return FALSE;
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
        static const u8 percent[] = {100, 0, 25, 50, 200}; // NORMAL, OFF, 1/4, 1/2, 2X
        const u32 setting = (u32)VitaExtras_GetFast(FAST_EXTRA_ENCOUNTER_RATE);

        if (setting < ARRAY_COUNT(percent))
            encounterRate = encounterRate * percent[setting] / 100;
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

#define VW_MAX           12
#define VW_MAGIC         0x5657          /* in data[7]: the sprite is one of these */
#define VW_RANGE_X_2D    11
#define VW_RANGE_Y_2D    6
#define VW_FORGET_2D     15
#define VW_RANGE_X_3D    14              /* asymmetric 3D bounds matching Emerald */
#define VW_RANGE_N_3D    20
#define VW_RANGE_S_3D    8
#define VW_FORGET_X_3D   21
#define VW_FORGET_N_3D   26
#define VW_FORGET_S_3D   14
#define VW_NEAREST       3               /* never closer than this when they appear */
#define VW_ASKS          0xFFFF          /* idle: it walked into the player */

struct VisibleWild
{
    bool8 active, water, shiny, hgss;
    u8 spriteId, level, progress;
    s8 dx, dy;
    s8 faceX, faceY;
    u16 species, idle, sparkle;
    s16 x, y;                    /* map coordinates (MAP_OFFSET included) */
    u32 personality;
};

static struct VisibleWild sVisible[VW_MAX];
static u8 sVisibleMapGroup = 0xFF, sVisibleMapNum = 0xFF;
static bool8 sVisibleShifted;
static u16 sVisibleSpawnTimer;

#define VW_ABS(n) ((n) < 0 ? -(n) : (n))

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

static void VisibleFreePaletteIfUnused(const struct VisibleWild *skip, u16 species)
{
    u32 i;
    bool8 shared = FALSE;

    for (i = 0; i < VW_MAX; i++)
        if (&sVisible[i] != skip && sVisible[i].active && !sVisible[i].hgss
         && gMonIconPaletteIndices[sVisible[i].species] == gMonIconPaletteIndices[species])
            shared = TRUE;
    if (!shared)
        FreeMonIconPalette(species);
}

static void VisibleRemoveSlot(u32 slot)
{
    struct VisibleWild *v = &sVisible[slot];
    if (v->active)
    {
        if (v->hgss)
        {
            FollowerMon_WildFree(slot, VisibleSpriteValid(v) ? &gSprites[v->spriteId] : NULL);
        }
        else if (VisibleSpriteValid(v))
        {
            DestroyMonIcon(&gSprites[v->spriteId]);
            VisibleFreePaletteIfUnused(v, v->species);
        }
        else
        {
            VisibleFreePaletteIfUnused(v, v->species);
        }
    }
    v->active = FALSE;
    v->hgss = FALSE;
    v->spriteId = MAX_SPRITES;
}

static void VisibleRemove(struct VisibleWild *v)
{
    VisibleRemoveSlot((u32)(v - sVisible));
}

static void VisibleRemoveAll(void)
{
    u32 i;

    for (i = 0; i < VW_MAX; i++)
        VisibleRemoveSlot(i);
}

static struct VisibleWild *VisibleAt(s16 x, s16 y, const struct VisibleWild *except)
{
    u32 i;

    for (i = 0; i < VW_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];

        if (!v->active || v == except)
            continue;
        if ((v->x == x && v->y == y) || (v->progress && v->x + v->dx == x && v->y + v->dy == y))
            return v;
    }
    return NULL;
}

/* A tile one can stand on: of the right kind (grass or cave floor, or
 * water), free, and no one on it. */
static bool8 VisibleTileFits(s16 x, s16 y, bool8 water)
{
    if (MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE)
        != (water ? TILE_ENCOUNTER_WATER : TILE_ENCOUNTER_LAND))
        return FALSE;
    if (MapGridGetCollisionAt(x, y) != 0)
        return FALSE;
    return GetObjectEventIdByXY(x, y) == OBJECT_EVENTS_COUNT;
}

#define VW_ICON_BYTES 0x200      /* 32x32, 4bpp */
#define VW_ICON_FRAME 14         /* frames per icon frame: a slow bob */

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

static void VisiblePlace(struct VisibleWild *v)
{
    struct Sprite *sprite = &gSprites[v->spriteId];
    s16 x, y, step = v->progress / 2;   /* half a pixel a frame (VisibleMove) */
    s16 screenX, screenY;

    SetSpritePosToMapCoords(v->x, v->y, &x, &y);
    sprite->x = x + 8 + v->dx * step;
    sprite->y = y + (v->hgss ? 16 + sprite->centerToCornerVecY : 0) + v->dy * step;
    screenX = sprite->x + gSpriteCoordOffsetX;
    screenY = sprite->y + gSpriteCoordOffsetY;
    if (v->hgss)
        FollowerMon_WildFace(sprite, v->faceX, v->faceY, v->progress != 0);
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

static u16 VisibleSpawnInterval(void)
{
    switch (VitaExtras_GetFast(FAST_EXTRA_ENCOUNTER_RATE))
    {
    case 2: return 240; // 1/4
    case 3: return 160; // 1/2
    case 4: return 45;  // 2X
    default: return 90; // NORMAL
    }
}

static void VisibleTrySpawn(u16 headerId)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    const struct WildPokemonInfo *landInfo = GetLandMonsInfo(headerId);
    const struct WildPokemonInfo *info;
    u32 i, slotIdx = VW_MAX, tries, slot;
    s16 x, y;
    bool8 water;
    const bool8 is3D = (VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0);
    const s16 rangeX = is3D ? VW_RANGE_X_3D : VW_RANGE_X_2D;
    const s16 rangeNorth = is3D ? VW_RANGE_N_3D : VW_RANGE_Y_2D;
    const s16 rangeSouth = is3D ? VW_RANGE_S_3D : VW_RANGE_Y_2D;
    const s16 mapW = (gMapHeader.mapLayout != NULL) ? (s16)gMapHeader.mapLayout->width : 0;
    const s16 mapH = (gMapHeader.mapLayout != NULL) ? (s16)gMapHeader.mapLayout->height : 0;

    for (i = 0; i < VW_MAX; i++)
    {
        if (!sVisible[i].active)
        {
            slotIdx = i;
            break;
        }
    }
    if (slotIdx >= VW_MAX)
        return;

    struct VisibleWild *v = &sVisible[slotIdx];
    for (tries = 0; tries < 24; tries++)
    {
        x = player->currentCoords.x + (s16)(Random() % (2 * rangeX + 1)) - rangeX;
        y = player->currentCoords.y + (s16)(Random() % (rangeNorth + rangeSouth + 1)) - rangeNorth;
        /* Keep spawns inside current map bounds so neighbor routes don't bleed wrong species. */
        if (mapW > 0 && mapH > 0
         && (x < MAP_OFFSET || x >= MAP_OFFSET + mapW || y < MAP_OFFSET || y >= MAP_OFFSET + mapH))
            continue;
        if (VW_ABS(x - player->currentCoords.x) < VW_NEAREST && VW_ABS(y - player->currentCoords.y) < VW_NEAREST)
            continue;
        if (VisibleAt(x, y, NULL) != NULL)
            continue;
        water = MapGridGetMetatileAttributeAt(x, y, METATILE_ATTRIBUTE_ENCOUNTER_TYPE) == TILE_ENCOUNTER_WATER;
        info = water ? gWildMonHeaders[headerId].waterMonsInfo : landInfo;
        if (info == NULL || !VisibleTileFits(x, y, water))
            continue;
        slot = water ? ChooseWildMonIndex_WaterRock() : ChooseWildMonIndex_Land();
        v->level = ChooseWildMonLevel(&info->wildPokemon[slot]);
        if (!IsWildLevelAllowedByRepel(v->level))
            continue;
        v->species = info->wildPokemon[slot].species;
        if (v->species == SPECIES_UNOWN)
        {
            s32 chamber = gSaveBlock1Ptr->location.mapNum - MAP_NUM(MAP_SEVEN_ISLAND_TANOBY_RUINS_MONEAN_CHAMBER);

            if (water || chamber < 0 || chamber >= (s32)ARRAY_COUNT(sUnownLetterSlots))
                continue;
            v->personality = GenerateUnownPersonalityByLetter(sUnownLetterSlots[chamber][slot]);
        }
        else
        {
            v->personality = CtrShiny_RollPersonality();
        }
        v->shiny = GET_SHINY_VALUE(GetPlayerTrainerId(), v->personality) < SHINY_ODDS;

        /* Prefer HGSS 8-frame overworld sprites (ext palette, normal & shiny); fall back to party icon. */
        v->hgss = FALSE;
        v->spriteId = FollowerMon_WildSprite(slotIdx, v->species, v->personality, v->shiny);
        if (v->spriteId < MAX_SPRITES)
        {
            v->hgss = TRUE;
        }
        else
        {
            if (!VisibleLoadPalette(v->species))
                continue;
            v->spriteId = CreateMonIcon(v->species, SpriteCB_VisibleIcon, 0, 0, 1, v->personality, TRUE);
            if (v->spriteId >= MAX_SPRITES)
            {
                VisibleFreePaletteIfUnused(NULL, v->species);
                return;
            }
        }
        gSprites[v->spriteId].coordOffsetEnabled = TRUE;
        gSprites[v->spriteId].oam.priority = 2;
        gSprites[v->spriteId].data[7] = VW_MAGIC;
        gSprites[v->spriteId].data[5] = 0;
        gSprites[v->spriteId].data[6] = 0;
        v->water = water;
        v->x = x;
        v->y = y;
        v->dx = v->dy = 0;
        v->faceX = 0;
        v->faceY = 1;
        v->progress = 0;
        v->idle = 30 + Random() % 90;
        v->sparkle = 0;
        v->active = TRUE;
        VisiblePlace(v);
        if (v->shiny)
        {
            VisibleSparkle(v);
            PlaySE(SE_SHINY);
        }
        return;
    }
}

/* Touched: the battle with this Pokemon, as a random encounter starts one. */
static void VisibleStartBattle(struct VisibleWild *v)
{
    ZeroEnemyPartyMons();
    CreateMon(&gEnemyParty[0], v->species, v->level, USE_RANDOM_IVS, TRUE, v->personality, OT_ID_PLAYER_ID, 0);
    VisibleRemove(v);
    StartWildBattle();
}

static void VisibleMove(struct VisibleWild *v, s16 px, s16 py, bool8 surfing)
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
        /* Into the player: a battle, if the player stands where it lives. */
        v->idle = v->water == surfing ? VW_ASKS : 60;
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

    for (i = 0; i < VW_MAX; i++)
    {
        sVisible[i].x -= dx;
        sVisible[i].y -= dy;
    }
    sVisibleShifted = TRUE;
}

bool8 VisibleWild_StepOnto(void)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    const bool8 surfing = TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) != 0;
    const s16 px = player->currentCoords.x;
    const s16 py = player->currentCoords.y;
    u32 i;

    if (!VisibleHere())
        return FALSE;

    for (i = 0; i < VW_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];

        if (!v->active || !VisibleSpriteValid(v) || v->water != surfing)
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

    if (i >= VW_MAX)
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

bool8 VisibleWild_SpriteTile(u8 spriteId, s16 *x16, s16 *y16)
{
    u32 i;

    for (i = 0; i < VW_MAX; i++)
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

/* StandardWildEncounter: only the highest density tier (HIGH) replaces random encounters. */
static bool8 VisibleWild_ReplacesRandom(void)
{
    return VisibleHere() && VisibleSetting() == ARRAY_COUNT(sVisibleMost) - 1;
}

void VisibleWild_Update(void)
{
    struct ObjectEvent *player;
    u16 headerId;
    u32 i;
    bool8 surfing, live, idle;
    const bool8 is3D = (VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0);
    const s16 forgetX = is3D ? VW_FORGET_X_3D : VW_FORGET_2D;
    const s16 forgetN = is3D ? VW_FORGET_N_3D : VW_FORGET_2D;
    const s16 forgetS = is3D ? VW_FORGET_S_3D : VW_FORGET_2D;

    /* (CB1_Overworld calls this only on the field itself) */
    if (!VisibleHere())
    {
        VisibleRemoveAll();
        return;
    }
    /* A warp, a battle, a menu: clean up slots whose sprites were reset with the screen. */
    for (i = 0; i < VW_MAX; i++)
        if (sVisible[i].active && !VisibleSpriteValid(&sVisible[i]))
            VisibleRemoveSlot(i);
    if (gSaveBlock1Ptr->location.mapGroup != sVisibleMapGroup || gSaveBlock1Ptr->location.mapNum != sVisibleMapNum)
    {
        if (!sVisibleShifted)
            VisibleRemoveAll();
        sVisibleMapGroup = gSaveBlock1Ptr->location.mapGroup;
        sVisibleMapNum = gSaveBlock1Ptr->location.mapNum;
        sVisibleSpawnTimer = 20;
    }
    sVisibleShifted = FALSE;

    headerId = GetCurrentMapWildMonHeaderId();
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    surfing = TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) != 0;
    live = !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled();
    idle = live && gPlayerAvatar.tileTransitionState == T_NOT_MOVING;

    for (i = 0; i < VW_MAX; i++)
    {
        struct VisibleWild *v = &sVisible[i];
        s16 relX, relY;

        if (!v->active)
            continue;
        relX = v->x - player->currentCoords.x;
        relY = v->y - player->currentCoords.y;
        if (VW_ABS(relX) > forgetX || relY < -forgetN || relY > forgetS)
        {
            VisibleRemoveSlot(i);
            continue;
        }
        if (live)
        {
            const bool8 onPlayer = (v->x == player->currentCoords.x && v->y == player->currentCoords.y && !v->progress);
            const bool8 touched = (v->idle == VW_ASKS && idle) || onPlayer;

            if (touched && v->water == surfing && (v->idle != VW_ASKS || IsWildLevelAllowedByRepel(v->level)))
            {
                VisibleStartBattle(v);
                return;
            }
            if (v->idle == VW_ASKS)
                v->idle = 60;
            VisibleMove(v, player->currentCoords.x, player->currentCoords.y, surfing);
            if (v->shiny && ++v->sparkle >= 150)
            {
                v->sparkle = 0;
                VisibleSparkle(v);
            }
        }
        VisiblePlace(v);
    }

    if (!live || headerId == HEADER_NONE || sWildEncountersDisabled || !FollowerMon_PalettesSettled())
        return;
    if (sVisibleSpawnTimer)
    {
        sVisibleSpawnTimer--;
        return;
    }
    sVisibleSpawnTimer = VisibleSpawnInterval();
    {
        u32 count = 0, most = VisibleMost();

        for (i = 0; i < VW_MAX; i++)
            count += sVisible[i].active;
        if (count < most)
            VisibleTrySpawn(headerId);
    }
}
#endif
