#ifndef GUARD_BATTLE_MEGA_H
#define GUARD_BATTLE_MEGA_H

/*
 * Mega Evolution (3DS port).
 *
 * Battle only: the stored Pokémon never changes. The form lives in the
 * battle's own state, so nothing new ever reaches the save, which stays a
 * plain FireRed save for the cartridge and for emulators.
 *
 * Stones are two holdable items FireRed has but never uses (the contest
 * scarves), shown here under new names; on the cartridge they read as the
 * scarves. The Key Stone is a spare event flag.
 */

#include "constants/items.h"
#include "constants/flags.h"

/* ITEM_MEGA_STONE(_Y) and FLAG_MEGA_KEY_STONE are in constants/items.h and
 * constants/flags.h, for the event scripts too. */

/* The forms, in the order tools/dev/mega_sprites.py writes their pictures. */
enum {
    MEGA_FORM_VENUSAUR,
    MEGA_FORM_CHARIZARD_X,
    MEGA_FORM_CHARIZARD_Y,
    MEGA_FORM_BLASTOISE,
    MEGA_FORM_BEEDRILL,
    MEGA_FORM_PIDGEOT,
    MEGA_FORM_ALAKAZAM,
    MEGA_FORM_SLOWBRO,
    MEGA_FORM_GENGAR,
    MEGA_FORM_KANGASKHAN,
    MEGA_FORM_PINSIR,
    MEGA_FORM_GYARADOS,
    MEGA_FORM_AERODACTYL,
    MEGA_FORM_MEWTWO_X,
    MEGA_FORM_MEWTWO_Y,
    MEGA_FORM_HERACROSS,
    MEGA_FORM_DRAGONITE,
    MEGA_FORM_AMPHAROS,
    MEGA_FORM_STEELIX,
    MEGA_FORM_SCIZOR,
    MEGA_FORM_HOUNDOOM,
    MEGA_FORM_TYRANITAR,
    MEGA_FORM_SCEPTILE,
    MEGA_FORM_BLAZIKEN,
    MEGA_FORM_SWAMPERT,
    MEGA_FORM_GARDEVOIR,
    MEGA_FORM_SABLEYE,
    MEGA_FORM_MAWILE,
    MEGA_FORM_AGGRON,
    MEGA_FORM_MEDICHAM,
    MEGA_FORM_MANECTRIC,
    MEGA_FORM_SHARPEDO,
    MEGA_FORM_CAMERUPT,
    MEGA_FORM_ALTARIA,
    MEGA_FORM_BANETTE,
    MEGA_FORM_ABSOL,
    MEGA_FORM_GLALIE,
    MEGA_FORM_SALAMENCE,
    MEGA_FORM_METAGROSS,
    MEGA_FORM_LATIAS,
    MEGA_FORM_LATIOS,
    MEGA_FORM_RAYQUAZA,
    MEGA_FORM_MEGANIUM,
    MEGA_FORM_FERALIGATR,
    MEGA_FORM_SKARMORY,
    MEGA_FORM_CLEFABLE,
    MEGA_FORM_VICTREEBEL,
    MEGA_FORM_STARMIE,
    MEGA_FORM_COUNT
};

/* a battle */
void Mega_ResetBattle(void);
bool8 Mega_TryStartTurn(void);
void Mega_TurnDone(void);
void Mega_OnSwitchIn(u8 battler);
void Mega_RefreshStats(u8 battler);
void Mega_Cmd(void);
void Mega_LoadBattlerGfx(u8 battler);
u8 Mega_PicYOffset(u8 battler, u16 species, u8 base);
bool8 Mega_IsEvolved(u8 battler);
bool8 Mega_ItemLocked(u8 battler);

/* the player's choice on the move menu */
bool8 Mega_CanToggle(u8 battler);
bool8 Mega_IsToggled(u8 battler);
void Mega_Toggle(u8 battler);

/* items and text */
const u8 *Mega_ItemName(u16 itemId);
const u8 *Mega_ItemDescription(u16 itemId);
u16 Mega_ItemPrice(u16 itemId);
const u32 *Mega_ItemIcon(u16 itemId, u8 attr);
void Mega_OnContinue(void);
const u8 *Mega_AbilityName(u8 ability);
const u8 *Mega_BattleString(u16 stringId);

/* the forms' abilities */
u16 Mega_ModifyPower(u8 battlerAtk, u16 move, u16 power);
s32 Mega_ModifyDamage(u8 battlerAtk, u8 battlerDef, u16 move, s32 damage);
bool8 Mega_SkillLink(u8 battlerAtk);
u8 Mega_StabTenths(u8 battlerAtk);
bool8 Mega_NoGuard(u8 battlerAtk, u8 battlerDef);
void Mega_BeforeMove(void);
void Mega_AfterMove(void);
bool8 Mega_Steadfast(u8 battler);
u8 Mega_CategoryType(u8 battlerAtk, u16 move, u8 type);
bool8 Mega_SheerForce(u8 battlerAtk, u16 move);
u16 Mega_ModifySpAttack(u8 battlerAtk, u16 spAttack);
s32 Mega_AfterTypeCalc(u8 battlerDef, s32 damage, u8 flags);
u8 Mega_TypeMultiplier(u8 defType, u8 multiplier);
s8 Mega_MovePriority(u8 battler, u16 move);
bool8 Mega_MagicBounce(u8 battlerDef);
bool8 Mega_Stalwart(u8 battlerAtk);

#endif // GUARD_BATTLE_MEGA_H
