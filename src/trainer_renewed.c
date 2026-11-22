/*
 * Renewed trainer teams (3DS port, ENHANCEMENTS > TRAINER TEAMS).
 *
 * The Gym Leaders keep their own Pokémon and get a few more, and from the
 * end of the journey on (Koga, Sabrina, Blaine, Giovanni, the Elite Four,
 * the Champion) one Pokémon in each team holds a Mega Stone; so does
 * Erika's Victreebel. The Elite Four's rematches and Team Rocket's Admins
 * on Five Island bring the Mega Evolutions of Johto and Hoenn. A team is made
 * here when its battle starts: the enemy party is never saved, so the save
 * stays a plain FireRed save. Every move is one the Pokémon can learn in
 * FireRed (level up, TM, tutor or egg).
 */
#include "global.h"
#include "3ds_extras.h"
#include "battle.h"
#include "battle_mega.h"
#include "characters.h"
#include "data.h"
#include "pokemon.h"
#include "trainer_renewed.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/species.h"

struct RenewedMon
{
    u16 species;
    u8 lvl;
    u8 iv;
    u16 heldItem;
    u16 moves[MAX_MON_MOVES];
};

#define MON(sp, lv, iv_, item, m1, m2, m3, m4) \
    { SPECIES_##sp, lv, iv_, ITEM_##item, { MOVE_##m1, MOVE_##m2, MOVE_##m3, MOVE_##m4 } }

static const struct RenewedMon sBrock[] =
{
    MON(GEODUDE,    12, 0, NONE, TACKLE, DEFENSE_CURL, NONE, NONE),
    MON(NOSEPASS,   13, 0, NONE, TACKLE, HARDEN, ROCK_THROW, NONE),
    MON(ONIX,       14, 0, NONE, TACKLE, BIND, ROCK_TOMB, NONE),
};

static const struct RenewedMon sMisty[] =
{
    MON(STARYU,     18, 0, NONE, TACKLE, HARDEN, RECOVER, WATER_PULSE),
    MON(HORSEA,     18, 0, NONE, BUBBLE, SMOKESCREEN, LEER, WATER_PULSE),
    MON(CORSOLA,    19, 0, NONE, TACKLE, HARDEN, BUBBLE, RECOVER),
    MON(STARMIE,    21, 0, NONE, SWIFT, RECOVER, RAPID_SPIN, WATER_PULSE),
};

static const struct RenewedMon sLtSurge[] =
{
    MON(VOLTORB,    21, 0, NONE, SONIC_BOOM, TACKLE, SCREECH, SHOCK_WAVE),
    MON(PIKACHU,    20, 0, NONE, QUICK_ATTACK, THUNDER_WAVE, DOUBLE_TEAM, SHOCK_WAVE),
    MON(ELECTRIKE,  22, 0, NONE, QUICK_ATTACK, SPARK, HOWL, THUNDER_WAVE),
    MON(RAICHU,     24, 0, NONE, QUICK_ATTACK, THUNDER_WAVE, DOUBLE_TEAM, SHOCK_WAVE),
};

static const struct RenewedMon sErika[] =
{
    MON(TANGELA,    24, 0, NONE, POISON_POWDER, CONSTRICT, INGRAIN, GIGA_DRAIN),
    MON(ROSELIA,    26, 0, NONE, MEGA_DRAIN, LEECH_SEED, MAGICAL_LEAF, STUN_SPORE),
    MON(JUMPLUFF,   27, 0, NONE, SLEEP_POWDER, LEECH_SEED, GIGA_DRAIN, SYNTHESIS),
    MON(VILEPLUME,  29, 0, NONE, SLEEP_POWDER, ACID, STUN_SPORE, GIGA_DRAIN),
    MON(VICTREEBEL, 29, 0, MEGA_STONE, STUN_SPORE, ACID, POISON_POWDER, GIGA_DRAIN),
};

/* Koga Mega Evolves even before the player has a Key Stone. */
static const struct RenewedMon sKoga[] =
{
    MON(KOFFING,    37, 0, NONE, SELF_DESTRUCT, SLUDGE, SMOKESCREEN, TOXIC),
    MON(ARIADOS,    38, 0, NONE, NIGHT_SHADE, LEECH_LIFE, SCARY_FACE, TOXIC),
    MON(MUK,        39, 0, NONE, MINIMIZE, SLUDGE, ACID_ARMOR, TOXIC),
    MON(BEEDRILL,   41, 0, MEGA_STONE, TWINEEDLE, PIN_MISSILE, AGILITY, PURSUIT),
    MON(WEEZING,    43, 0, NONE, TACKLE, SLUDGE, SMOKESCREEN, TOXIC),
};

static const struct RenewedMon sSabrina[] =
{
    MON(MR_MIME,    38, 0, NONE, BARRIER, PSYBEAM, BATON_PASS, CALM_MIND),
    MON(VENOMOTH,   38, 0, NONE, PSYBEAM, GUST, LEECH_LIFE, SUPERSONIC),
    MON(XATU,       40, 0, NONE, PSYCHIC, NIGHT_SHADE, AERIAL_ACE, CALM_MIND),
    MON(ESPEON,     40, 0, NONE, PSYBEAM, SWIFT, QUICK_ATTACK, REFLECT),
    MON(ALAKAZAM,   43, 0, MEGA_STONE, PSYCHIC, RECOVER, FUTURE_SIGHT, CALM_MIND),
};

static const struct RenewedMon sBlaine[] =
{
    MON(NINETALES,  44, 0, NONE, FLAMETHROWER, CONFUSE_RAY, QUICK_ATTACK, SAFEGUARD),
    MON(MAGMAR,     44, 0, NONE, FIRE_PUNCH, SMOKESCREEN, BRICK_BREAK, PSYCHIC),
    MON(RAPIDASH,   45, 0, NONE, STOMP, BOUNCE, FIRE_SPIN, FIRE_BLAST),
    MON(ARCANINE,   47, 0, NONE, BITE, ROAR, TAKE_DOWN, FIRE_BLAST),
    MON(CHARIZARD,  47, 0, MEGA_STONE_Y, FLAMETHROWER, DRAGON_CLAW, AERIAL_ACE, SCARY_FACE),
};

static const struct RenewedMon sGiovanni[] =
{
    MON(DUGTRIO,    42, 0, NONE, SLASH, SAND_TOMB, MUD_SLAP, EARTHQUAKE),
    MON(MAROWAK,    43, 0, NONE, BONEMERANG, HEADBUTT, FOCUS_ENERGY, ROCK_TOMB),
    MON(NIDOQUEEN,  44, 0, NONE, BODY_SLAM, DOUBLE_KICK, POISON_STING, EARTHQUAKE),
    MON(NIDOKING,   45, 0, NONE, THRASH, DOUBLE_KICK, POISON_STING, EARTHQUAKE),
    MON(KANGASKHAN, 47, 0, MEGA_STONE, MEGA_PUNCH, DIZZY_PUNCH, EARTHQUAKE, ROCK_TOMB),
    MON(RHYDON,     50, 0, NONE, TAKE_DOWN, ROCK_BLAST, SCARY_FACE, EARTHQUAKE),
};

static const struct RenewedMon sLorelei[] =
{
    MON(DEWGONG,    52, 250, NONE, ICE_BEAM, SURF, HAIL, SAFEGUARD),
    MON(CLOYSTER,   51, 250, NONE, SPIKES, PROTECT, HAIL, DIVE),
    MON(SLOWBRO,    52, 250, MEGA_STONE, ICE_BEAM, SURF, AMNESIA, YAWN),
    MON(JYNX,       54, 250, NONE, ICE_PUNCH, DOUBLE_SLAP, LOVELY_KISS, ATTRACT),
    MON(LAPRAS,     54, 250, SITRUS_BERRY, CONFUSE_RAY, ICE_BEAM, SURF, BODY_SLAM),
};

static const struct RenewedMon sBruno[] =
{
    MON(ONIX,       51, 250, NONE, EARTHQUAKE, ROCK_TOMB, IRON_TAIL, ROAR),
    MON(HITMONCHAN, 53, 250, NONE, SKY_UPPERCUT, MACH_PUNCH, ROCK_TOMB, COUNTER),
    MON(HITMONLEE,  53, 250, NONE, MEGA_KICK, FORESIGHT, BRICK_BREAK, FACADE),
    MON(STEELIX,    54, 250, NONE, DOUBLE_EDGE, EARTHQUAKE, IRON_TAIL, SAND_TOMB),
    MON(HERACROSS,  54, 250, MEGA_STONE, MEGAHORN, BRICK_BREAK, FURY_ATTACK, ROCK_TOMB),
    MON(MACHAMP,    56, 250, SITRUS_BERRY, CROSS_CHOP, BULK_UP, SCARY_FACE, ROCK_TOMB),
};

static const struct RenewedMon sAgatha[] =
{
    MON(GENGAR,     54, 250, NONE, SHADOW_PUNCH, CONFUSE_RAY, TOXIC, DOUBLE_TEAM),
    MON(CROBAT,     54, 250, NONE, CONFUSE_RAY, POISON_FANG, AIR_CUTTER, BITE),
    MON(HAUNTER,    53, 250, NONE, HYPNOSIS, DREAM_EATER, CURSE, MEAN_LOOK),
    MON(MISDREAVUS, 55, 250, NONE, PSYBEAM, CONFUSE_RAY, PAIN_SPLIT, SHADOW_BALL),
    MON(ARBOK,      56, 250, NONE, SLUDGE_BOMB, SCREECH, IRON_TAIL, BITE),
    MON(GENGAR,     58, 250, MEGA_STONE, SHADOW_BALL, SLUDGE_BOMB, HYPNOSIS, NIGHTMARE),
};

static const struct RenewedMon sLance[] =
{
    MON(GYARADOS,   56, 250, NONE, HYPER_BEAM, DRAGON_RAGE, TWISTER, BITE),
    MON(DRAGONAIR,  54, 250, NONE, HYPER_BEAM, SAFEGUARD, DRAGON_RAGE, OUTRAGE),
    MON(DRAGONAIR,  54, 250, NONE, HYPER_BEAM, SAFEGUARD, THUNDER_WAVE, OUTRAGE),
    MON(AERODACTYL, 58, 250, NONE, HYPER_BEAM, ANCIENT_POWER, WING_ATTACK, SCARY_FACE),
    MON(DRAGONITE,  60, 250, MEGA_STONE, HYPER_BEAM, SAFEGUARD, OUTRAGE, WING_ATTACK),
};

/* The Elite Four's second round (after the Sevii Islands). */
static const struct RenewedMon sLorelei2[] =
{
    MON(DEWGONG,    64, 255, NONE, ICE_BEAM, SURF, SIGNAL_BEAM, DOUBLE_TEAM),
    MON(GLALIE,     63, 255, MEGA_STONE, DOUBLE_EDGE, CRUNCH, EARTHQUAKE, ICE_BEAM),
    MON(PILOSWINE,  63, 255, NONE, BLIZZARD, EARTHQUAKE, DOUBLE_EDGE, ROCK_SLIDE),
    MON(JYNX,       66, 255, NONE, ICE_BEAM, PSYCHIC, LOVELY_KISS, ATTRACT),
    MON(SLOWBRO,    65, 255, NONE, SURF, PSYCHIC, ICE_BEAM, AMNESIA),
    MON(LAPRAS,     66, 255, CHERI_BERRY, ICE_BEAM, SURF, PSYCHIC, THUNDER),
};

static const struct RenewedMon sBruno2[] =
{
    MON(STEELIX,    65, 255, NONE, EARTHQUAKE, IRON_TAIL, CRUNCH, ROCK_TOMB),
    MON(HITMONCHAN, 65, 255, NONE, SKY_UPPERCUT, MACH_PUNCH, ROCK_SLIDE, COUNTER),
    MON(HITMONLEE,  65, 255, NONE, MEGA_KICK, FORESIGHT, EARTHQUAKE, ROCK_SLIDE),
    MON(STEELIX,    66, 255, MEGA_STONE, EARTHQUAKE, IRON_TAIL, CRUNCH, DRAGON_BREATH),
    MON(HERACROSS,  66, 255, NONE, MEGAHORN, BRICK_BREAK, EARTHQUAKE, FURY_ATTACK),
    MON(MACHAMP,    68, 255, PERSIM_BERRY, CROSS_CHOP, EARTHQUAKE, BRICK_BREAK, ROCK_SLIDE),
};

/* The Elite Four's rematch teams with the Mega Evolutions of Johto and
 * Hoenn; the rest of each team is the game's own. */
static const struct RenewedMon sAgatha2[] =
{
    MON(GENGAR,     66, 255, NONE, SHADOW_BALL, PSYCHIC, CONFUSE_RAY, HYPNOSIS),
    MON(CROBAT,     66, 255, NONE, SLUDGE_BOMB, AIR_CUTTER, SHADOW_BALL, CONFUSE_RAY),
    MON(BANETTE,    65, 255, MEGA_STONE, SHADOW_BALL, KNOCK_OFF, WILL_O_WISP, CURSE),
    MON(ARBOK,      68, 255, NONE, SLUDGE_BOMB, EARTHQUAKE, GIGA_DRAIN, DOUBLE_TEAM),
    MON(GENGAR,     70, 255, CHESTO_BERRY, SHADOW_BALL, PSYCHIC, THUNDERBOLT, SLUDGE_BOMB),
};

static const struct RenewedMon sLance2[] =
{
    MON(GYARADOS,   68, 255, NONE, HYPER_BEAM, DRAGON_DANCE, EARTHQUAKE, THUNDER_WAVE),
    MON(SALAMENCE,  66, 255, MEGA_STONE, DOUBLE_EDGE, DRAGON_CLAW, EARTHQUAKE, FLAMETHROWER),
    MON(KINGDRA,    66, 255, NONE, HYPER_BEAM, DRAGON_DANCE, SURF, ICE_BEAM),
    MON(AERODACTYL, 70, 255, NONE, HYPER_BEAM, ANCIENT_POWER, AERIAL_ACE, EARTHQUAKE),
    MON(DRAGONITE,  72, 255, PERSIM_BERRY, HYPER_BEAM, OUTRAGE, THUNDERBOLT, ICE_BEAM),
};

/* Team Rocket's warehouse on Five Island: a Mega Victreebel in place of the
 * Vileplume (the other Admin's Houndoom gets its stone in sStones). */
static const struct RenewedMon sRocketAdmin[] =
{
    MON(MUK,        52, 150, NONE, SLUDGE_BOMB, SCREECH, MINIMIZE, ROCK_TOMB),
    MON(ARBOK,      53, 150, NONE, SLUDGE_BOMB, BITE, EARTHQUAKE, IRON_TAIL),
    MON(VICTREEBEL, 54, 150, MEGA_STONE, SLUDGE_BOMB, GIGA_DRAIN, SLEEP_POWDER, RAZOR_LEAF),
};

struct RenewedTeam
{
    u16 trainer;
    u8 count;
    const struct RenewedMon *mons;
};

#define TEAM(id, mons) { TRAINER_##id, ARRAY_COUNT(mons), mons }

static const struct RenewedTeam sTeams[] =
{
    TEAM(LEADER_BROCK, sBrock),
    TEAM(LEADER_MISTY, sMisty),
    TEAM(LEADER_LT_SURGE, sLtSurge),
    TEAM(LEADER_ERIKA, sErika),
    TEAM(LEADER_KOGA, sKoga),
    TEAM(LEADER_SABRINA, sSabrina),
    TEAM(LEADER_BLAINE, sBlaine),
    TEAM(LEADER_GIOVANNI, sGiovanni),
    TEAM(ELITE_FOUR_LORELEI, sLorelei),
    TEAM(ELITE_FOUR_BRUNO, sBruno),
    TEAM(ELITE_FOUR_AGATHA, sAgatha),
    TEAM(ELITE_FOUR_LANCE, sLance),
    TEAM(ELITE_FOUR_LORELEI_2, sLorelei2),
    TEAM(ELITE_FOUR_BRUNO_2, sBruno2),
    TEAM(ELITE_FOUR_AGATHA_2, sAgatha2),
    TEAM(ELITE_FOUR_LANCE_2, sLance2),
    TEAM(TEAM_ROCKET_ADMIN, sRocketAdmin),
};

/* Teams kept as they are, with a Mega Stone for one Pokémon (the last of
 * that species: a trainer's ace comes last). */
struct RenewedStone
{
    u16 trainer;
    u16 species;
    u16 item;
};

static const struct RenewedStone sStones[] =
{
    { TRAINER_BOSS_GIOVANNI_2,               SPECIES_KANGASKHAN, ITEM_MEGA_STONE },
    { TRAINER_RIVAL_ROUTE22_LATE_SQUIRTLE,   SPECIES_BLASTOISE,  ITEM_MEGA_STONE },
    { TRAINER_RIVAL_ROUTE22_LATE_BULBASAUR,  SPECIES_VENUSAUR,   ITEM_MEGA_STONE },
    { TRAINER_RIVAL_ROUTE22_LATE_CHARMANDER, SPECIES_CHARIZARD,  ITEM_MEGA_STONE },
    { TRAINER_CHAMPION_FIRST_SQUIRTLE,       SPECIES_BLASTOISE,  ITEM_MEGA_STONE },
    { TRAINER_CHAMPION_FIRST_BULBASAUR,      SPECIES_VENUSAUR,   ITEM_MEGA_STONE },
    { TRAINER_CHAMPION_FIRST_CHARMANDER,     SPECIES_CHARIZARD,  ITEM_MEGA_STONE },
    /* the rematch: the rival's Tyranitar Mega Evolves instead */
    { TRAINER_CHAMPION_REMATCH_SQUIRTLE,     SPECIES_TYRANITAR,  ITEM_MEGA_STONE },
    { TRAINER_CHAMPION_REMATCH_BULBASAUR,    SPECIES_TYRANITAR,  ITEM_MEGA_STONE },
    { TRAINER_CHAMPION_REMATCH_CHARMANDER,   SPECIES_TYRANITAR,  ITEM_MEGA_STONE },
    { TRAINER_TEAM_ROCKET_ADMIN_2,           SPECIES_HOUNDOOM,   ITEM_MEGA_STONE },
};

static bool8 Renewed(void)
{
    return CtrExtras_Get("trainer_teams") != 0;
}

/* The team for this trainer, made as CreateNPCTrainerParty makes the
 * game's own (the same personalities, so the same natures and genders).
 * 0 when the trainer keeps the game's team. */
u8 Renewed_CreateParty(struct Pokemon *party, u16 trainerNum)
{
    const struct RenewedTeam *team = NULL;
    u32 nameHash = 0, personality;
    s32 i, j;

    if (!Renewed())
        return 0;
    for (i = 0; i < (s32)ARRAY_COUNT(sTeams); i++)
        if (sTeams[i].trainer == trainerNum)
            team = &sTeams[i];
    if (team == NULL)
        return 0;

    ZeroEnemyPartyMons();
    for (i = 0; i < team->count; i++)
    {
        const struct RenewedMon *mon = &team->mons[i];

        if (gTrainers[trainerNum].doubleBattle == TRUE)
            personality = 0x80;
        else if (gTrainers[trainerNum].encounterMusic_gender & F_TRAINER_FEMALE)
            personality = 0x78;
        else
            personality = 0x88;
        for (j = 0; gTrainers[trainerNum].trainerName[j] != EOS; j++)
            nameHash += gTrainers[trainerNum].trainerName[j];
        for (j = 0; gSpeciesNames[mon->species][j] != EOS; j++)
            nameHash += gSpeciesNames[mon->species][j];
        personality += nameHash << 8;

        CreateMon(&party[i], mon->species, mon->lvl, mon->iv * MAX_PER_STAT_IVS / 255, TRUE, personality, OT_ID_RANDOM_NO_SHINY, 0);
        SetMonData(&party[i], MON_DATA_HELD_ITEM, &mon->heldItem);
        for (j = 0; j < MAX_MON_MOVES; j++)
        {
            SetMonData(&party[i], MON_DATA_MOVE1 + j, &mon->moves[j]);
            SetMonData(&party[i], MON_DATA_PP1 + j, &gBattleMoves[mon->moves[j]].pp);
        }
    }
    gBattleTypeFlags |= gTrainers[trainerNum].doubleBattle;
    return team->count;
}

/* After the game's own team is made: its Mega Stone, if it has one. */
void Renewed_GiveMegaStone(struct Pokemon *party, u16 trainerNum, u8 count)
{
    s32 i, k;

    if (!Renewed())
        return;
    for (k = 0; k < (s32)ARRAY_COUNT(sStones); k++)
    {
        if (sStones[k].trainer != trainerNum)
            continue;
        for (i = count - 1; i >= 0; i--)
            if (GetMonData(&party[i], MON_DATA_SPECIES) == sStones[k].species)
            {
                SetMonData(&party[i], MON_DATA_HELD_ITEM, &sStones[k].item);
                return;
            }
    }
}
