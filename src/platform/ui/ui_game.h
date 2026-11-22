#ifndef UI_GAME_H
#define UI_GAME_H

// Game data and actions for the 960x544 native UI screens (ui_game.c). Core 1 only.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint16_t species;          // SPECIES_EGG for eggs
    bool egg;
    bool shiny;
    uint8_t level;
    uint8_t gender;            // MON_MALE / MON_FEMALE / MON_GENDERLESS
    uint16_t hp;
    uint16_t maxHp;
    uint16_t item;
    uint32_t status;
    uint8_t statusRow;         // 0 none, 1..6 UI_SPR_STATUS_*
    char nickname[16];
    char name[24];
} UiMon;

typedef UiMon UiPartySlotSummary;

typedef struct
{
    char playerName[16];
    uint8_t playerGender;
    uint16_t trainerId;
    uint16_t trainerPicId;
    uint16_t playHours;
    uint8_t playMinutes;
    uint8_t badges;
    uint8_t partyCount;
    bool hasPokedex;
    bool hasParty;
    bool nationalDex;
    uint16_t dexSeen;
    uint16_t dexCaught;
    uint32_t money;
    uint32_t coins;
    bool inSafari;
    uint8_t safariBalls;
    uint16_t safariSteps;
    uint16_t mapSec;
    char mapName[32];
} UiPlayerSummary;

// GBA string (EOS-terminated) -> UTF-8 in the Latin-1 subset of the UI fonts. Returns length.
size_t UiGame_Text(const uint8_t *src, char *dst, size_t cap);
void UiGame_DecodeText(const uint8_t *src, char *dst, size_t cap);
void UiGame_DecodeTextLines(const uint8_t *src, char *dst, size_t cap);

bool UiGame_HasSaveData(void);
bool UiGame_InField(void);
bool UiGame_InOverworld(void);
bool UiGame_InBattle(void);
bool UiGame_PaletteFadeActive(void);

void UiGame_FieldLock(void);
void UiGame_FieldUnlock(void);
void UiGame_BeginSceneFadeOut(void);
void UiGame_CommitSceneSwitch(void (*enterScene)(void));
bool UiGame_ConsumeSceneReturn(void);
void UiGame_MarkSceneReturn(void);
void UiGame_ResetState(void);

void UiGame_EnterPokedexScene(void);
void UiGame_EnterPartyScene(void);
void UiGame_EnterBagScene(void);
void UiGame_EnterTrainerCardScene(void);
void UiGame_EnterGbaOptionsScene(void);
void UiGame_TriggerSafariRetire(void);
bool UiGame_QuickSave(void);
bool UiGame_Save(void);
uint8_t UiGame_SaveSlot(void);
bool UiGame_SaveOverwritesOtherGame(void);

uint32_t UiGame_Money(void);
uint16_t UiGame_Coins(void);
void UiGame_Clock(char *out, size_t cap);
bool UiGame_IsNight(void);
void UiGame_ZoneName(char *out, size_t cap);
void UiGame_PlayerName(char *out, size_t cap);
bool UiGame_PlayerIsMale(void);
uint16_t UiGame_RegisteredItem(void);
void UiGame_ItemName(uint16_t itemId, char *out, size_t cap);
int32_t UiGame_ItemPocket(uint16_t itemId);
uint16_t UiGame_RepelSteps(void);
bool UiGame_InSafari(void);
uint8_t UiGame_SafariBalls(void);
uint16_t UiGame_SafariSteps(void);
bool UiGame_HasFlag(uint16_t flag);
uint8_t UiGame_BadgeCount(void);
uint16_t UiGame_DexOwned(void);
uint16_t UiGame_DexSeen(void);
void UiGame_PlayTime(char *out, size_t cap);

void UiGame_GetMapSectionName(uint16_t mapSec, char *out, size_t cap);
void UiGame_MapSecName(uint16_t sec, char *out, size_t cap);
void UiGame_GetSpeciesName(uint16_t species, char *out, size_t cap);
void UiGame_SpeciesName(uint16_t species, char *out, size_t cap);
void UiGame_GetPlayerSummary(UiPlayerSummary *out);
int32_t UiGame_PartyCount(void);
int32_t UiGame_ViewCount(void);
int32_t UiGame_ViewBox(uint8_t box, int32_t pos);
int32_t UiGame_ViewPosition(int32_t slot);
void UiGame_ViewParty(void);
bool UiGame_Mon(int32_t slot, UiMon *out);
bool UiGame_GetPartySlot(uint8_t slot, UiPartySlotSummary *out);

uint16_t UiGame_SafeMove(uint32_t move);
uint16_t UiGame_SafeSpecies(uint32_t species);
uint8_t UiGame_SafeAbility(uint32_t ability);

typedef struct
{
    uint16_t dexNo, otId, item, hp, stats[6];
    uint8_t types[2], metLevel, level, expPct, ivs[6], evs[6], eggCycles;
    int8_t natureUp, natureDown; // display index (1 ATK .. 5 SPEED), 0 = neutral
    uint32_t exp, expToNext;
    bool showIvEv;
    char species[16], ot[12], nature[12], ability[16], abilityDesc[64], metAt[32];
} UiSummary;

typedef struct
{
    uint16_t move, power;
    uint8_t type, accuracy, pp, maxPp, category; // category: 0 phys, 1 spec, 2 status
    char name[16], desc[96];
} UiMove;

int32_t UiGame_MonStatusIcon(int32_t slot); // 0..6 row from UI_SPR_STATUS_FIRST, or -1
void UiGame_Summary(int32_t slot, UiSummary *s);
void UiGame_Move(int32_t slot, int32_t idx, UiMove *m);
void UiGame_MoveInfo(int32_t slot, int32_t moveIdx, uint16_t *move, uint8_t *pp, uint8_t *maxPp);
void UiGame_MoveName(uint16_t move, char *out, size_t cap);
void UiGame_SwapMoves(int32_t slot, int32_t a, int32_t b);
void UiGame_SwapPartyMons(int32_t a, int32_t b);
uint16_t UiGame_FieldMove(int32_t slot, int32_t idx);
bool UiGame_ShareHp(int32_t from, int32_t to, char *msg, size_t cap);
bool UiGame_TakeItem(int32_t slot, uint16_t *taken);
int32_t UiGame_TakeMail(int32_t slot, bool sendToPc);

uint8_t UiGame_GetTextSpeed(void);
void UiGame_SetTextSpeed(uint8_t value);
uint8_t UiGame_GetBattleScene(void);
void UiGame_SetBattleScene(uint8_t value);
uint8_t UiGame_GetBattleStyle(void);
void UiGame_SetBattleStyle(uint8_t value);
uint8_t UiGame_GetSoundMode(void);
void UiGame_SetSoundMode(uint8_t value);
uint8_t UiGame_GetButtonMode(void);
void UiGame_SetButtonMode(uint8_t value);
uint8_t UiGame_GetWindowFrame(void);
void UiGame_SetWindowFrame(uint8_t value);

// Battle BAG / POKEMON (ui_game_battle.c)
typedef enum { UI_BUSE_FAIL, UI_BUSE_CLOSE, UI_BUSE_TARGET } UiBattleUse;
typedef enum { UI_ITEM_FAIL, UI_ITEM_DONE, UI_ITEM_DELTA, UI_ITEM_CHOOSE_MOVE, UI_ITEM_FORGET_MOVE, UI_ITEM_CLOSE } UiItemResult;
bool UiGame_OpenBattleMenu(const void *screen);
bool UiGame_InBattleMenu(void);
void UiGame_BattleMenuEnd(void);
bool UiGame_MonIsInBattle(int32_t slot);
bool UiGame_IsMultiPartner(int32_t slot);
const char *UiGame_BattleSwitch(int32_t slot);
bool UiGame_BattlePartyCanCancel(void);
bool UiGame_BattleItemUsable(uint16_t item);
UiBattleUse UiGame_BattleUse(uint16_t item, char *msg, size_t cap);
UiItemResult UiGame_BattleItemOnMon(int32_t slot, uint16_t item, int32_t moveIdx, char *msg, size_t cap);

// Bag and items (ui_game_items.c) — 0-based indices into gBagPockets[POCKET_* - 1]
enum
{
    UI_POCKET_ITEMS = 0,
    UI_POCKET_KEY_ITEMS = 1,
    UI_POCKET_POKE_BALLS = 2,
    UI_POCKET_TM_CASE = 3,
    UI_POCKET_BERRY_POUCH = 4,
    UI_POCKET_COUNT = 5
};
typedef struct { uint16_t item; uint16_t qty; } UiBagRow;
enum { UI_SORT_NUMBER = 0, UI_SORT_ALPHA, UI_SORT_MOST, UI_SORT_LEAST, UI_SORT_COUNT };
typedef enum { UI_USE_FAIL, UI_USE_DONE, UI_USE_TARGET, UI_USE_CLOSE } UiUseResult;
typedef struct { int16_t before[6], after[6]; uint8_t levelBefore, levelAfter; } UiStatDelta;

bool UiGame_BagRemove(uint16_t item, uint16_t n);
int32_t UiGame_PocketItems(int32_t pocket, UiBagRow *rows, int32_t max);
void UiGame_SortPocket(int32_t pocket, int32_t mode);
void UiGame_ItemDescription(uint16_t itemId, char *out, size_t cap);
bool UiGame_ItemIsTmHm(uint16_t itemId);
uint16_t UiGame_ItemMove(uint16_t itemId);
bool UiGame_CanToss(uint16_t itemId);
bool UiGame_CanRegister(uint16_t itemId);
bool UiGame_CanGive(uint16_t itemId);
void UiGame_Toss(uint16_t itemId, uint16_t n);
void UiGame_Register(uint16_t itemId);
UiUseResult UiGame_UseFromBag(uint16_t itemId, char *msg, size_t cap, void (**afterClose)(void));
UiItemResult UiGame_ItemOnMon(int32_t slot, uint16_t item, UiStatDelta *delta, char *msg, size_t cap, void (**afterClose)(void));
bool UiGame_ItemOnMove(int32_t slot, uint16_t item, int32_t moveIdx);
bool UiGame_ReplaceMove(int32_t slot, uint16_t item, int32_t moveIdx);
bool UiGame_CanLearnTm(int32_t slot, uint16_t item, bool *known);
bool UiGame_GiveItem(int32_t slot, uint16_t item, uint16_t *previous);
bool UiGame_GiveMailBegin(int32_t slot, uint16_t mailItem, void (**afterClose)(void));

// Town Map, Fly Map, Wild Encounters & Pokédex Habitats (ui_game_nav.c)
enum { UI_WILD_GRASS = 0, UI_WILD_SURF, UI_WILD_FISH, UI_WILD_ROCK, UI_WILD_COUNT };
enum { UI_HABITAT_COUNT = 9 };
typedef struct { uint16_t species; uint8_t minLv, maxLv, rate, rod; } UiWild;

int32_t UiGame_MapRegionCount(void);
uint16_t UiGame_MapSecAt(int32_t region, int32_t layer, int32_t gx, int32_t gy);
uint8_t UiGame_PlayerMapPos(int32_t *gx, int32_t *gy);
bool UiGame_CanFlyTo(uint16_t sec);
void UiGame_ExecuteFly(uint8_t partySlot, uint16_t sec);
int32_t UiGame_Wild(uint16_t sec, int32_t method, int32_t rod, UiWild *out, int32_t max);
uint32_t UiGame_CurrentTimePhase(void);
int32_t UiGame_SpeciesAreas(uint16_t species, uint8_t *out, int32_t count);
uint8_t UiGame_SpeciesHabitat(uint16_t species);
int32_t UiGame_HabitatSpecies(uint8_t habitat, uint16_t *out, int32_t max);

#endif // UI_GAME_H
