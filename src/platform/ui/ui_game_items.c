// Bag and item actions of the 960x544 native UI (Core 1), ported from pokeemerald-vita-3ds
// and adapted to Pokémon FireRed v1.1 (POCKET_*, ItemId_*, FieldUseFunc_*, Berry Powder, Quest Log).

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_game.h"
#include "platform/vita_extras.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "berry_powder.h"
#include "bike.h"
#include "coins.h"
#include "data.h"
#include "easy_chat.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "evolution_scene.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "item.h"
#include "item_menu.h"
#include "item_use.h"
#include "mail_data.h"
#include "main.h"
#include "menu_helpers.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "party_menu.h"
#include "pokemon.h"
#include "quest_log.h"
#include "script.h"
#include "sound.h"
#include "task.h"
#pragma GCC diagnostic pop
#include "constants/easy_chat.h"
#include "constants/flags.h"
#include "constants/item_effects.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/pokedex.h"
#include "constants/quest_log.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "constants/vars.h"

extern u16 GetBagItemQuantity(u16 *ptr);
extern void SetBagItemQuantity(u16 *ptr, u16 value);

// ---------- Pockets ----------

bool UiGame_BagRemove(const uint16_t item, const uint16_t n)
{
    return RemoveBagItem(item, n) != FALSE;
}

int32_t UiGame_PocketItems(const int32_t pocket, UiBagRow *const rows, const int32_t max)
{
    if (rows == NULL || max <= 0 || pocket < 0 || pocket >= NUM_BAG_POCKETS)
        return 0;
    struct BagPocket *const bp = &gBagPockets[pocket];
    int32_t n = 0;
    for (uint32_t i = 0; i < bp->capacity && n < max; i++)
    {
        const uint16_t id = bp->itemSlots[i].itemId;
        const uint16_t q = GetBagItemQuantity(&bp->itemSlots[i].quantity);
        if (id != ITEM_NONE && q > 0)
            rows[n++] = (UiBagRow){ id, q };
    }
    return n;
}

void UiGame_SortPocket(const int32_t pocket, const int32_t mode)
{
    if (pocket < 0 || pocket >= NUM_BAG_POCKETS)
        return;
    struct BagPocket *const bp = &gBagPockets[pocket];
    UiBagRow rows[64];
    const int32_t n = UiGame_PocketItems(pocket, rows, 64);
    char na[24], nb[24];
    for (int32_t i = 1; i < n; i++)
    {
        const UiBagRow key = rows[i];
        int32_t j = i - 1;
        while (j >= 0)
        {
            int32_t cmp;
            if (mode == UI_SORT_NUMBER) cmp = (int32_t)rows[j].item - (int32_t)key.item;
            else if (mode == UI_SORT_MOST) cmp = (int32_t)key.qty - (int32_t)rows[j].qty;
            else if (mode == UI_SORT_LEAST) cmp = (int32_t)rows[j].qty - (int32_t)key.qty;
            else
            {
                UiGame_ItemName(rows[j].item, na, sizeof(na));
                UiGame_ItemName(key.item, nb, sizeof(nb));
                cmp = strcmp(na, nb);
            }
            if (cmp <= 0)
                break;
            rows[j + 1] = rows[j];
            j--;
        }
        rows[j + 1] = key;
    }
    for (uint32_t i = 0; i < bp->capacity; i++)
    {
        bp->itemSlots[i].itemId = ((int32_t)i < n) ? rows[i].item : ITEM_NONE;
        SetBagItemQuantity(&bp->itemSlots[i].quantity, ((int32_t)i < n) ? rows[i].qty : 0);
    }
}

void UiGame_ItemDescription(const uint16_t itemId, char *const out, const size_t cap)
{
    if (out == NULL || cap == 0U)
        return;
    if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
    {
        out[0] = '\0';
        return;
    }
    UiGame_Text(ItemId_GetDescription(itemId), out, cap);
}

bool UiGame_ItemIsTmHm(const uint16_t itemId)
{
    return ItemId_GetPocket(itemId) == POCKET_TM_CASE;
}

uint16_t UiGame_ItemMove(const uint16_t itemId)
{
    return ItemIdToBattleMoveId(itemId);
}

bool UiGame_CanToss(const uint16_t itemId)
{
    return ItemId_GetImportance(itemId) == 0 && ItemId_GetPocket(itemId) != POCKET_TM_CASE;
}

bool UiGame_CanRegister(const uint16_t itemId)
{
    const ItemUseFunc fn = ItemId_GetFieldFunc(itemId);
    return ItemId_GetPocket(itemId) == POCKET_KEY_ITEMS && fn != NULL && fn != FieldUseFunc_OakStopsYou;
}

bool UiGame_CanGive(const uint16_t itemId)
{
    return IsHoldingItemAllowed(itemId) != FALSE;
}

void UiGame_Toss(const uint16_t itemId, const uint16_t n)
{
    RemoveBagItem(itemId, n);
}

void UiGame_Register(const uint16_t itemId)
{
    gSaveBlock1Ptr->registeredItem = (gSaveBlock1Ptr->registeredItem == itemId) ? ITEM_NONE : itemId;
}

// ---------- Using an item from the bag in the field ----------

static uint16_t sAfterItem;

static void AfterClose_EscapeRope(void)
{
    Overworld_ResetStateAfterDigEscRope();
    gSpecialVar_ItemId = ITEM_ESCAPE_ROPE;
    CreateTask(Task_UseDigEscapeRopeOnField, 8);
}

static void AfterClose_Bike(void)
{
    if (!TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE))
        PlaySE(SE_BIKE_BELL);
    GetOnOffBike(PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE);
}

static void AfterClose_FieldFunc(void)
{
    const ItemUseFunc fn = ItemId_GetFieldFunc(sAfterItem);
    if (fn == NULL)
        return;
    LockPlayerFieldControls();
    FreezeObjectEvents();
    HandleEnforcedLookDirectionOnPlayerStopMoving();
    StopPlayerAvatar();
    gSpecialVar_ItemId = sAfterItem;
    const u8 tid = CreateTask(fn, 8);
    gTasks[tid].data[3] = 1; // run immediately on the field, matching registered SELECT item use
}

static UiUseResult UseSacredAsh(const uint16_t itemId, char *const msg, const size_t cap)
{
    struct Pokemon *firstMon = NULL;
    for (int32_t i = 0; i < UiGame_PartyCount(); i++)
    {
        struct Pokemon *const mon = &gPlayerParty[i];
        if (GetMonData(mon, MON_DATA_IS_EGG) == 0 && GetMonData(mon, MON_DATA_HP) == 0)
        {
            const uint16_t maxHp = (uint16_t)GetMonData(mon, MON_DATA_MAX_HP);
            const uint32_t zero = 0;
            SetMonData(mon, MON_DATA_HP, &maxHp);
            SetMonData(mon, MON_DATA_STATUS, &zero);
            if (firstMon == NULL)
                firstMon = mon;
        }
    }
    if (firstMon == NULL)
    {
        snprintf(msg, cap, UI_TR("It won't have any effect."));
        return UI_USE_FAIL;
    }
    ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, firstMon, itemId, 0xFFFF);
    RemoveBagItem(itemId, 1);
    snprintf(msg, cap, UI_TR("All fainted POKéMON were revived!"));
    return UI_USE_DONE;
}

static bool IsOnCyclingRoadOrRail(void)
{
    s16 x = 0, y = 0;
    PlayerGetDestCoords(&x, &y);
    const u8 behavior = MapGridGetMetatileBehaviorAt(x, y);
    return FlagGet(FLAG_SYS_ON_CYCLING_ROAD) == TRUE
        || MetatileBehavior_IsVerticalRail(behavior) == TRUE
        || MetatileBehavior_IsHorizontalRail(behavior) == TRUE
        || MetatileBehavior_IsIsolatedVerticalRail(behavior) == TRUE
        || MetatileBehavior_IsIsolatedHorizontalRail(behavior) == TRUE;
}

UiUseResult UiGame_UseFromBag(const uint16_t itemId, char *const msg, const size_t cap, void (**const afterClose)(void))
{
    *afterClose = NULL;
    msg[0] = '\0';

    if (ItemIsMail(itemId))
    {
        snprintf(msg, cap, UI_TR("It can't be used right now."));
        return UI_USE_FAIL;
    }
    if (itemId == ITEM_REPEL || itemId == ITEM_SUPER_REPEL || itemId == ITEM_MAX_REPEL)
    {
        if (VarGet(VAR_REPEL_STEP_COUNT) > 0)
        {
            snprintf(msg, cap, UI_TR("The REPEL is still in effect."));
            return UI_USE_FAIL;
        }
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, NULL, itemId, 0xFFFF);
        VarSet(VAR_REPEL_STEP_COUNT, ItemId_GetHoldEffectParam(itemId));
        RemoveBagItem(itemId, 1);
        snprintf(msg, cap, UI_TR("The REPEL was sprayed!"));
        return UI_USE_DONE;
    }
    if (itemId == ITEM_WHITE_FLUTE || itemId == ITEM_BLACK_FLUTE)
    {
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, NULL, itemId, 0xFFFF);
        if (itemId == ITEM_WHITE_FLUTE)
        {
            FlagSet(FLAG_SYS_WHITE_FLUTE_ACTIVE);
            FlagClear(FLAG_SYS_BLACK_FLUTE_ACTIVE);
            snprintf(msg, cap, UI_TR("Played the WHITE FLUTE. Wild POKéMON are more likely to appear."));
        }
        else
        {
            FlagSet(FLAG_SYS_BLACK_FLUTE_ACTIVE);
            FlagClear(FLAG_SYS_WHITE_FLUTE_ACTIVE);
            snprintf(msg, cap, UI_TR("Played the BLACK FLUTE. Wild POKéMON are less likely to appear."));
        }
        return UI_USE_DONE;
    }
    if (itemId == ITEM_COIN_CASE)
    {
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, NULL, itemId, 0xFFFF);
        snprintf(msg, cap, UI_TR("Your COINS: %u"), (unsigned)GetCoins());
        return UI_USE_DONE;
    }
    if (itemId == ITEM_POWDER_JAR)
    {
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, NULL, itemId, 0xFFFF);
        snprintf(msg, cap, UI_TR("BERRY POWDER: %u"), (unsigned)GetBerryPowder());
        return UI_USE_DONE;
    }
    if (itemId == ITEM_ESCAPE_ROPE)
    {
        if (!CanUseEscapeRopeOnCurrMap())
        {
            snprintf(msg, cap, UI_TR("It can't be used here."));
            return UI_USE_FAIL;
        }
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, NULL, itemId, gMapHeader.regionMapSectionId);
        RemoveBagItem(itemId, 1);
        *afterClose = AfterClose_EscapeRope;
        return UI_USE_CLOSE;
    }
    if (itemId == ITEM_BICYCLE || itemId == ITEM_MACH_BIKE || itemId == ITEM_ACRO_BIKE)
    {
        if (IsOnCyclingRoadOrRail())
        {
            snprintf(msg, cap, UI_TR("You can't dismount your BIKE here."));
            return UI_USE_FAIL;
        }
        if (!Overworld_IsBikingAllowed() || IsBikingDisallowedByPlayer())
        {
            snprintf(msg, cap, UI_TR("You can't ride a BIKE here."));
            return UI_USE_FAIL;
        }
        *afterClose = AfterClose_Bike;
        return UI_USE_CLOSE;
    }
    if (itemId == ITEM_SACRED_ASH)
        return UseSacredAsh(itemId, msg, cap);

    const ItemUseFunc fn = ItemId_GetFieldFunc(itemId);
    if (fn == ItemUseOutOfBattle_EnigmaBerry)
    {
        const u8 eff = GetItemEffectType(itemId);
        if (eff == ITEM_EFFECT_SACRED_ASH)
            return UseSacredAsh(itemId, msg, cap);
        if ((eff >= ITEM_EFFECT_RAISE_LEVEL && eff <= ITEM_EFFECT_HEAL_HP)
            || (eff >= ITEM_EFFECT_CURE_POISON && eff <= ITEM_EFFECT_CURE_PARALYSIS)
            || (eff >= ITEM_EFFECT_CURE_ALL_STATUS && eff <= ITEM_EFFECT_DEF_EV)
            || (eff >= ITEM_EFFECT_PP_UP && eff <= ITEM_EFFECT_HEAL_PP))
            return UI_USE_TARGET;
        snprintf(msg, cap, UI_TR("It can't be used right now."));
        return UI_USE_FAIL;
    }
    if (ItemId_GetPocket(itemId) == POCKET_TM_CASE
        || fn == FieldUseFunc_Medicine
        || fn == FieldUseFunc_RareCandy
        || fn == FieldUseFunc_PpUp
        || fn == FieldUseFunc_Ether
        || fn == FieldUseFunc_EvoItem)
        return UI_USE_TARGET;

    if (fn != NULL && fn != FieldUseFunc_OakStopsYou && fn != FieldUseFunc_Mail)
    {
        sAfterItem = itemId;
        *afterClose = AfterClose_FieldFunc;
        return UI_USE_CLOSE;
    }

    snprintf(msg, cap, UI_TR("It can't be used right now."));
    return UI_USE_FAIL;
}

// ---------- Item on a party POKéMON ----------

static void Consume(const uint16_t item)
{
    if (ItemId_GetPocket(item) == POCKET_TM_CASE)
    {
        const uint16_t move = ItemIdToBattleMoveId(item);
        if (move != MOVE_NONE && (IsMoveHm(move) || VitaExtras_GetFast(FAST_EXTRA_INFINITE_TMS) != 0))
            return;
    }
    RemoveBagItem(item, 1);
}

static void Stats(struct Pokemon *const mon, int16_t out[6], uint8_t *const lv)
{
    static const u8 ids[6] = {
        MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPATK, MON_DATA_SPDEF, MON_DATA_SPEED
    };
    for (int32_t i = 0; i < 6; i++)
        out[i] = (int16_t)GetMonData(mon, ids[i]);
    *lv = (uint8_t)GetMonData(mon, MON_DATA_LEVEL);
}

static uint8_t sEvoSlot;
static uint16_t sEvoSpecies;

static void AfterClose_Evolve(void)
{
    CleanupOverworldWindowsAndTilemaps();
    gCB2_AfterEvolution = CB2_ReturnToField;
    BeginEvolutionScene(&gPlayerParty[sEvoSlot], sEvoSpecies, FALSE, sEvoSlot);
}

UiItemResult UiGame_ItemOnMon(const int32_t slot, const uint16_t item, UiStatDelta *const delta,
                              char *const msg, const size_t cap, void (**const afterClose)(void))
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    *afterClose = NULL;
    msg[0] = '\0';

    if (GetMonData(mon, MON_DATA_IS_EGG) != 0)
    {
        snprintf(msg, cap, UI_TR("It can't be used on an EGG."));
        return UI_ITEM_FAIL;
    }

    const uint16_t move = ItemIdToBattleMoveId(item);
    if (ItemId_GetPocket(item) == POCKET_TM_CASE && move != MOVE_NONE)
    {
        if (MonKnowsMove(mon, move))
        {
            snprintf(msg, cap, UI_TR("It already knows that move."));
            return UI_ITEM_FAIL;
        }
        if (!CanMonLearnTMHM(mon, (u8)(item - ITEM_TM01)))
        {
            snprintf(msg, cap, UI_TR("It can't learn that move."));
            return UI_ITEM_FAIL;
        }
        if (GiveMoveToMon(mon, move) != MON_HAS_MAX_MOVES)
        {
            Consume(item);
            ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, mon, item, 0xFFFF);
            snprintf(msg, cap, UI_TR("It learned the move!"));
            return UI_ITEM_DONE;
        }
        return UI_ITEM_FORGET_MOVE;
    }

    const u8 eff = GetItemEffectType(item);
    if (eff == ITEM_EFFECT_EVO_STONE || ItemId_GetFieldFunc(item) == FieldUseFunc_EvoItem)
    {
        const uint16_t target = GetEvolutionTargetSpecies(mon, EVO_MODE_ITEM_USE, item);
        if (target == SPECIES_NONE || (!IsNationalPokedexViewEnabled() && target > KANTO_DEX_COUNT))
        {
            snprintf(msg, cap, UI_TR("It won't have any effect."));
            return UI_ITEM_FAIL;
        }
        RemoveBagItem(item, 1);
        ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, mon, item, 0xFFFF);
        sEvoSlot = (uint8_t)slot;
        sEvoSpecies = target;
        *afterClose = AfterClose_Evolve;
        return UI_ITEM_CLOSE;
    }

    if (eff == ITEM_EFFECT_PP_UP || eff == ITEM_EFFECT_PP_MAX
        || item == ITEM_ETHER || item == ITEM_MAX_ETHER || item == ITEM_LEPPA_BERRY
        || (item == ITEM_ENIGMA_BERRY && eff == ITEM_EFFECT_HEAL_PP))
        return UI_ITEM_CHOOSE_MOVE;

    Stats(mon, delta->before, &delta->levelBefore);
    if (PokemonUseItemEffects(mon, item, (u8)slot, 0, FALSE))
    {
        snprintf(msg, cap, UI_TR("It won't have any effect."));
        return UI_ITEM_FAIL;
    }
    Consume(item);
    ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, mon, item, 0xFFFF);
    CalculateMonStats(mon);
    Stats(mon, delta->after, &delta->levelAfter);

    bool changed = delta->levelAfter != delta->levelBefore;
    for (int32_t i = 0; i < 6; i++)
        changed |= (delta->after[i] != delta->before[i]);
    snprintf(msg, cap, UI_TR("It was used."));
    return changed ? UI_ITEM_DELTA : UI_ITEM_DONE;
}

bool UiGame_ItemOnMove(const int32_t slot, const uint16_t item, const int32_t moveIdx)
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    if (PokemonUseItemEffects(mon, item, (u8)slot, (u8)moveIdx, FALSE))
        return false;
    Consume(item);
    ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, mon, item, (u16)GetMonData(mon, MON_DATA_MOVE1 + moveIdx));
    return true;
}

bool UiGame_ReplaceMove(const int32_t slot, const uint16_t item, const int32_t moveIdx)
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    if (IsMoveHm((uint16_t)GetMonData(mon, MON_DATA_MOVE1 + moveIdx)))
        return false;
    const uint16_t move = ItemIdToBattleMoveId(item);
    RemoveMonPPBonus(mon, (u8)moveIdx);
    SetMonMoveSlot(mon, move, (u8)moveIdx);
    Consume(item);
    ItemUse_SetQuestLogEvent(QL_EVENT_USED_ITEM, mon, item, move);
    return true;
}

bool UiGame_CanLearnTm(const int32_t slot, const uint16_t item, bool *const known)
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    const uint16_t move = ItemIdToBattleMoveId(item);
    *known = MonKnowsMove(mon, move) != FALSE;
    return GetMonData(mon, MON_DATA_IS_EGG) == 0 && !*known && CanMonLearnTMHM(mon, (u8)(item - ITEM_TM01)) != 0;
}

bool UiGame_GiveItem(const int32_t slot, const uint16_t item, uint16_t *const previous)
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    *previous = (uint16_t)GetMonData(mon, MON_DATA_HELD_ITEM);
    if (!IsHoldingItemAllowed(item) || ItemIsMail(item) || (*previous != ITEM_NONE && ItemIsMail(*previous)))
        return false;
    if (!RemoveBagItem(item, 1))
        return false;
    if (*previous != ITEM_NONE && !AddBagItem(*previous, 1))
    {
        AddBagItem(item, 1);
        return false;
    }
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
    if (*previous == ITEM_NONE)
    {
        ItemUse_SetQuestLogEvent(QL_EVENT_GAVE_HELD_ITEM_BAG, mon, item, 0xFFFF);
    }
    else
    {
        struct QuestLogEvent_SwappedHeldItem ql = {
            .takenItemId = *previous,
            .givenItemId = item,
            .species = (u16)GetMonData(mon, MON_DATA_SPECIES_OR_EGG),
        };
        SetQuestLogEvent(QL_EVENT_SWAPPED_HELD_ITEM, (const u16 *)&ql);
    }
    return true;
}

static uint8_t sMailGiveSlot;

static void CB2_AfterWriteMailFromUi(void)
{
    struct Pokemon *const mon = &gPlayerParty[sMailGiveSlot];
    if (gSpecialVar_Result == FALSE)
    {
        const u16 mailItem = (u16)GetMonData(mon, MON_DATA_HELD_ITEM);
        TakeMailFromMon(mon);
        if (mailItem != ITEM_NONE)
            AddBagItem(mailItem, 1);
    }
    SetMainCallback2(CB2_ReturnToField);
}

static void AfterClose_WriteMail(void)
{
    const u8 mailId = (u8)GetMonData(&gPlayerParty[sMailGiveSlot], MON_DATA_MAIL);
    CleanupOverworldWindowsAndTilemaps();
    DoEasyChatScreen(EASY_CHAT_TYPE_MAIL, gSaveBlock1Ptr->mail[mailId].words, CB2_AfterWriteMailFromUi);
}

bool UiGame_GiveMailBegin(const int32_t slot, const uint16_t mailItem, void (**const afterClose)(void))
{
    struct Pokemon *const mon = &gPlayerParty[slot];
    if (!IsWritingMailAllowed(mailItem) || !ItemIsMail(mailItem)
        || GetMonData(mon, MON_DATA_IS_EGG) != 0 || GetMonData(mon, MON_DATA_HELD_ITEM) != ITEM_NONE)
        return false;
    if (GiveMailToMon(mon, mailItem) == MAIL_NONE)
        return false;
    RemoveBagItem(mailItem, 1);
    sMailGiveSlot = (uint8_t)slot;
    *afterClose = AfterClose_WriteMail;
    return true;
}
