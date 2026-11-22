// Game side of native battle sub-menus invoked from battle_controller_player.c
// on Core 1 while the battle controller waits for Ui_IsOpen() to return false.

#include <stdio.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "platform/ui.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "item.h"
#include "item_menu.h"
#include "item_use.h"
#include "main.h"
#include "party_menu.h"
#include "pokemon.h"
#include "string_util.h"
#include "strings.h"
#pragma GCC diagnostic pop
#include "constants/battle.h"
#include "constants/item_effects.h"
#include "constants/items.h"
#include "constants/party_menu.h"
#include "constants/quest_log.h"

static bool sBattleMenu;

bool UiGame_InBattleMenu(void)
{
    return sBattleMenu;
}

void UiGame_BattleMenuEnd(void)
{
    sBattleMenu = false;
}

bool UiGame_OpenBattleMenu(const void *screen)
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    sBattleMenu = true;
    UiCore_OpenFromScript((const UiScreen *)screen, NULL);
    return true;
}

bool Ui_OpenBattleBag(void)
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    gSpecialVar_ItemId = ITEM_NONE;
    sBattleMenu = true;
    UiCore_OpenFromScript(&gUiBagScreen, NULL);
    return true;
}

bool Ui_OpenBattleParty(void)
{
    if (!Ui_IsEnabled() || Ui_IsOpen())
        return false;
    gSelectedMonPartyId = PARTY_SIZE;
    gPartyMenuUseExitCallback = FALSE;
    sBattleMenu = true;
    UiCore_OpenFromScript(&gUiPartyScreen, NULL);
    return true;
}

static u8 PartyAction(void)
{
    return gBattleBufferA[gBattlerInMenuId][1] & 0xF;
}

bool UiGame_MonIsInBattle(int32_t slot)
{
    for (u32 b = 0; b < gBattlersCount; b++)
        if (GetBattlerSide(b) == B_SIDE_PLAYER && gBattlerPartyIndexes[b] == slot)
            return true;
    return false;
}

bool UiGame_IsMultiPartner(int32_t slot)
{
    return gMain.inBattle && IsMultiBattle() && slot >= MULTI_PARTY_SIZE;
}

const char *UiGame_BattleSwitch(int32_t slot)
{
    static char sWhy[96];
    if (UiGame_IsMultiPartner(slot))
    {
        StringCopy(gStringVar1, GetTrainerPartnerName());
        StringExpandPlaceholders(gStringVar4, gText_CantSwitchWithAlly);
        UiGame_Text(gStringVar4, sWhy, sizeof(sWhy));
        return sWhy;
    }
    struct Pokemon *const mon = &gPlayerParty[slot];
    if (GetMonData(mon, MON_DATA_IS_EGG))
        return UI_TR("An EGG can't battle!");
    if (GetMonData(mon, MON_DATA_HP) == 0)
        return UI_TR("It has no energy left to battle!");
    if (UiGame_MonIsInBattle(slot))
        return UI_TR("It's already in battle!");
    if (slot == gBattleStruct->playerPartyIdx)
        return UI_TR("It has already been selected.");
    if (PartyAction() == PARTY_ACTION_ABILITY_PREVENTS)
        return UI_TR("An ability prevents switching!");
    if (PartyAction() == PARTY_ACTION_CANT_SWITCH)
        return UI_TR("It can't be switched out!");
    gSelectedMonPartyId = (u8)slot;
    gPartyMenuUseExitCallback = TRUE;
    return NULL;
}

bool UiGame_BattlePartyCanCancel(void)
{
    if (PartyAction() == PARTY_ACTION_SEND_OUT)
        return false;
    return GetMonData(&gPlayerParty[gBattlerPartyIndexes[gBattlerInMenuId]], MON_DATA_HP) != 0;
}

bool UiGame_BattleItemUsable(const uint16_t item)
{
    return ItemId_GetBattleUsage(item) != 0 && ItemId_GetBattleFunc(item) != NULL;
}

UiBattleUse UiGame_BattleUse(const uint16_t item, char *const msg, const size_t cap)
{
    const ItemUseFunc fn = ItemId_GetBattleFunc(item);
    msg[0] = '\0';
    if (!UiGame_BattleItemUsable(item))
    {
        snprintf(msg, cap, UI_TR("It can't be used now."));
        return UI_BUSE_FAIL;
    }
    if (fn == BattleUseFunc_PokeBallEtc)
    {
        if (IsPlayerPartyAndPokemonStorageFull())
        {
            snprintf(msg, cap, UI_TR("The BOX is full! You can't throw it."));
            return UI_BUSE_FAIL;
        }
        RemoveBagItem(item, 1);
    }
    else if (fn == BattleUseFunc_PokeFlute)
    {
        gSpecialVar_ItemId = item;
        return UI_BUSE_CLOSE;
    }
    else if (fn == BattleUseFunc_StatBooster
             || (fn == ItemUseInBattle_EnigmaBerry && GetItemEffectType(item) == ITEM_EFFECT_X_ITEM))
    {
        const u16 partyId = gBattlerPartyIndexes[gBattlerInMenuId];
        if (ExecuteTableBasedItemEffect(&gPlayerParty[partyId], item, (u8)partyId, 0))
        {
            snprintf(msg, cap, UI_TR("It won't have any effect."));
            return UI_BUSE_FAIL;
        }
        RemoveBagItem(item, 1);
    }
    else if (fn == BattleUseFunc_PokeDoll)
    {
        if (gBattleTypeFlags & BATTLE_TYPE_TRAINER)
        {
            snprintf(msg, cap, UI_TR("This isn't the time to use that!"));
            return UI_BUSE_FAIL;
        }
        RemoveBagItem(item, 1);
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, NULL, item, 0xFFFF);
    }
    else if (fn == BattleUseFunc_Medicine || fn == BattleUseFunc_Ether)
    {
        return UI_BUSE_TARGET;
    }
    else if (fn == ItemUseInBattle_EnigmaBerry)
    {
        const u8 eff = GetItemEffectType(item);
        if (eff == ITEM_EFFECT_HEAL_HP
            || (eff >= ITEM_EFFECT_CURE_POISON && eff <= ITEM_EFFECT_CURE_INFATUATION)
            || eff == ITEM_EFFECT_CURE_ALL_STATUS
            || eff == ITEM_EFFECT_HEAL_PP)
            return UI_BUSE_TARGET;
        snprintf(msg, cap, UI_TR("It can't be used now."));
        return UI_BUSE_FAIL;
    }
    else
    {
        // BattleUseFunc_BerryPouch switches directly to the Berries tab in ui_scr_bag.c.
        snprintf(msg, cap, UI_TR("It can't be used now."));
        return UI_BUSE_FAIL;
    }
    gSpecialVar_ItemId = item;
    return UI_BUSE_CLOSE;
}

UiItemResult UiGame_BattleItemOnMon(const int32_t slot, const uint16_t item, const int32_t moveIdx,
                                    char *const msg, const size_t cap)
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    msg[0] = '\0';
    if (GetMonData(mon, MON_DATA_IS_EGG))
    {
        snprintf(msg, cap, UI_TR("It can't be used on an EGG."));
        return UI_ITEM_FAIL;
    }
    if (moveIdx < 0
        && (item == ITEM_ETHER || item == ITEM_MAX_ETHER || item == ITEM_LEPPA_BERRY
            || (item == ITEM_ENIGMA_BERRY && GetItemEffectType(item) == ITEM_EFFECT_HEAL_PP)))
        return UI_ITEM_CHOOSE_MOVE;

    if (ExecuteTableBasedItemEffect(mon, item, (u8)slot, (u8)(moveIdx < 0 ? 0 : moveIdx)))
    {
        snprintf(msg, cap, UI_TR("It won't have any effect."));
        return UI_ITEM_FAIL;
    }
    if (item != ITEM_BLUE_FLUTE && item != ITEM_RED_FLUTE && item != ITEM_YELLOW_FLUTE && item != ITEM_POKE_FLUTE)
        RemoveBagItem(item, 1);
    gSpecialVar_ItemId = item;
    return UI_ITEM_CLOSE;
}
