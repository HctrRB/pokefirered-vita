#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "battle_main.h"
#include "characters.h"
#include "coins.h"
#include "data.h"
#include "event_data.h"
#include "event_object_lock.h"
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_fadetransition.h"
#include "field_player_avatar.h"
#include "field_specials.h"
#include "field_weather.h"
#include "gba/m4a_internal.h"
#include "help_system.h"
#include "item.h"
#include "item_menu.h"
#include "item_use.h"
#include "link.h"
#include "mail_data.h"
#include "main.h"
#include "menu_helpers.h"
#include "money.h"
#include "move_split.h"
#include "option_menu.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokedex.h"
#include "pokedex_screen.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "pokemon_summary_screen.h"
#include "quest_log.h"
#include "region_map.h"
#include "safari_zone.h"
#include "save.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "text.h"
#include "trainer_card.h"
#include "trainer_pokemon_sprites.h"
#pragma GCC diagnostic pop
#include "constants/abilities.h"
#include "constants/field_weather.h"
#include "constants/flags.h"
#include "constants/global.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/pokemon.h"
#include "constants/quest_log.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "constants/vars.h"

#include "ui_game.h"
#include "3ds_platform.h"
#include "platform/vita_controls.h"
#include "platform/vita_daynight.h"
#include "platform/vita_extras.h"
#include "platform/vita_nuzlocke.h"

extern void PartyMenu_QuestLogSwitchedOrderForUi(u8 slot1, u8 slot2);

static bool sReturningFromScene;

static void AppendStr(char *const dst, const size_t cap, size_t *const pos, const char *const s)
{
    if (s == NULL || *pos + 1 >= cap)
        return;
    for (size_t i = 0; s[i] != '\0' && *pos + 1 < cap; i++)
        dst[(*pos)++] = s[i];
}

static void AppendChar(char *const dst, const size_t cap, size_t *const pos, const char c)
{
    if (*pos + 1 < cap)
        dst[(*pos)++] = c;
}

// UTF-8 text of one GBA symbol or accented letter (charmap.txt, international layout shared by
// every Western FireRed release), or NULL for glyphs the native UI never shows (arrows, BLOCK,
// empty rectangle). Ellipsis, bullet and gender signs keep their historical ASCII stand-ins so the
// English build renders exactly as before; accented letters are real UTF-8 so a Spanish/European
// build shows them through the locale glyph sheets (or the folded base letter, see ui_assets.c).
static const char *GbaSymbolUtf8(const uint8_t c)
{
    switch (c)
    {
    case CHAR_EXCL_MARK:         return "!";
    case CHAR_QUESTION_MARK:     return "?";
    case CHAR_PERIOD:            return ".";
    case CHAR_HYPHEN:            return "-";
    case CHAR_BULLET:            return "*";
    case CHAR_ELLIPSIS:          return "...";
    case CHAR_DBL_QUOTE_LEFT:
    case CHAR_DBL_QUOTE_RIGHT:   return "\"";
    case CHAR_SGL_QUOTE_LEFT:
    case CHAR_SGL_QUOTE_RIGHT:   return "'";
    case CHAR_MALE:              return "M";
    case CHAR_FEMALE:            return "F";
    case CHAR_CURRENCY:          return "$";
    case CHAR_COMMA:             return ",";
    case CHAR_MULT_SIGN:         return "x";
    case CHAR_SLASH:             return "/";
    case CHAR_COLON:             return ":";
    case CHAR_SEMICOLON:         return ";";
    case CHAR_AMPERSAND:         return "&";
    case CHAR_PLUS:              return "+";
    case CHAR_EQUALS:            return "=";
    case CHAR_PERCENT:           return "%";
    case CHAR_LEFT_PAREN:        return "(";
    case CHAR_RIGHT_PAREN:       return ")";
    case CHAR_LESS_THAN:         return "<";
    case CHAR_GREATER_THAN:      return ">";
    case CHAR_LV:                return "Lv.";
    case CHAR_PK:                return "PK";
    case CHAR_MN:                return "MN";
    case CHAR_PO:                return "PO";
    case CHAR_KE:                return "KE";
    case CHAR_SUPER_ER:          return "er";
    case CHAR_SUPER_E:           return "e";
    case CHAR_SUPER_RE:          return "re";
    case CHAR_INV_EXCL_MARK:     return "¡";  // ¡
    case CHAR_INV_QUESTION_MARK: return "¿";  // ¿
    case CHAR_FEMININE_ORDINAL:  return "ª";  // ª
    case CHAR_MASCULINE_ORDINAL: return "º";  // º
    case CHAR_A_GRAVE:           return "À";
    case CHAR_A_ACUTE:           return "Á";
    case CHAR_A_CIRCUMFLEX:      return "Â";
    case CHAR_A_DIAERESIS:       return "Ä";
    case CHAR_C_CEDILLA:         return "Ç";
    case CHAR_E_GRAVE:           return "È";
    case CHAR_E_ACUTE:           return "É";
    case CHAR_E_CIRCUMFLEX:      return "Ê";
    case CHAR_E_DIAERESIS:       return "Ë";
    case CHAR_I_GRAVE:           return "Ì";
    case CHAR_I_ACUTE:           return "Í";
    case CHAR_I_CIRCUMFLEX:      return "Î";
    case CHAR_I_DIAERESIS:       return "Ï";
    case CHAR_N_TILDE:           return "Ñ";
    case CHAR_O_GRAVE:           return "Ò";
    case CHAR_O_ACUTE:           return "Ó";
    case CHAR_O_CIRCUMFLEX:      return "Ô";
    case CHAR_O_DIAERESIS:       return "Ö";
    case CHAR_U_GRAVE:           return "Ù";
    case CHAR_U_ACUTE:           return "Ú";
    case CHAR_U_CIRCUMFLEX:      return "Û";
    case CHAR_U_DIAERESIS:       return "Ü";
    case CHAR_ESZETT:            return "ß";
    case CHAR_a_GRAVE:           return "à";
    case CHAR_a_ACUTE:           return "á";
    case CHAR_a_CIRCUMFLEX:      return "â";
    case CHAR_a_DIAERESIS:       return "ä";
    case CHAR_c_CEDILLA:         return "ç";
    case CHAR_e_GRAVE:           return "è";
    case CHAR_e_ACUTE:           return "é";
    case CHAR_e_CIRCUMFLEX:      return "ê";
    case CHAR_e_DIAERESIS:       return "ë";
    case CHAR_i_GRAVE:           return "ì";
    case CHAR_i_ACUTE:           return "í";
    case CHAR_i_CIRCUMFLEX:      return "î";
    case CHAR_i_DIAERESIS:       return "ï";
    case CHAR_n_TILDE:           return "ñ";
    case CHAR_o_GRAVE:           return "ò";
    case CHAR_o_ACUTE:           return "ó";
    case CHAR_o_CIRCUMFLEX:      return "ô";
    case CHAR_o_DIAERESIS:       return "ö";
    case CHAR_u_GRAVE:           return "ù";
    case CHAR_u_ACUTE:           return "ú";
    case CHAR_u_CIRCUMFLEX:      return "û";
    case CHAR_u_DIAERESIS:       return "ü";
    case CHAR_OE:                return "Œ";  // Œ
    case CHAR_oe:                return "œ";  // œ
    default:                     return NULL;
    }
}

// Appends a whole UTF-8 sequence or nothing: a multi-byte letter that does not fit before the
// terminator is dropped, never cut, so NextUtf8() never sees a truncated sequence.
static void AppendUtf8(char *const dst, const size_t cap, size_t *const pos, const char *const s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    if ((uint8_t)s[0] >= 0x80U && *pos + n >= cap)
        return;
    AppendStr(dst, cap, pos, s);
}

static size_t DecodeGbaString(const uint8_t *src, char *const dst, const size_t cap, const bool keepNewlines)
{
    if (dst == NULL || cap == 0)
        return 0U;
    dst[0] = '\0';
    if (src == NULL)
        return 0U;

    size_t pos = 0U;
    while (*src != EOS && pos + 1 < cap)
    {
        const uint8_t c = *src++;
        if (c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR)
        {
            if (keepNewlines)
            {
                AppendChar(dst, cap, &pos, '\n');
            }
            else if (pos > 0 && dst[pos - 1] != ' ')
            {
                AppendChar(dst, cap, &pos, ' ');
            }
            continue;
        }
        if (c == EXT_CTRL_CODE_BEGIN)
        {
            if (*src != EOS)
                src += GetExtCtrlCodeLength(*src);
            continue;
        }
        if (c == PLACEHOLDER_BEGIN || c == CHAR_KEYPAD_ICON || c == CHAR_DYNAMIC)
        {
            if (*src != EOS)
                src++;
            continue;
        }
        if (c == CHAR_EXTRA_SYMBOL)
        {
            const uint8_t sub = (*src != EOS) ? *src++ : 0U;
            if (sub == CHAR_LV_2)
                AppendStr(dst, cap, &pos, "Lv.");
            else if (sub == CHAR_PP)
                AppendStr(dst, cap, &pos, "PP");
            else if (sub == CHAR_ID)
                AppendStr(dst, cap, &pos, "ID");
            else if (sub == CHAR_NO)
                AppendStr(dst, cap, &pos, "No.");
            else if (sub == CHAR_PLUS_2)
                AppendChar(dst, cap, &pos, '+');
            else if (sub == CHAR_UNDERSCORE)
                AppendChar(dst, cap, &pos, '_');
            continue;
        }
        if (c == CHAR_SPACE || c == CHAR_BARD_WORD_DELIMIT || c == CHAR_SPACER)
        {
            AppendChar(dst, cap, &pos, ' ');
        }
        else if (c >= CHAR_0 && c <= CHAR_9)
        {
            AppendChar(dst, cap, &pos, (char)('0' + (c - CHAR_0)));
        }
        else if (c >= CHAR_A && c <= CHAR_Z)
        {
            AppendChar(dst, cap, &pos, (char)('A' + (c - CHAR_A)));
        }
        else if (c >= CHAR_a && c <= CHAR_z)
        {
            AppendChar(dst, cap, &pos, (char)('a' + (c - CHAR_a)));
        }
        else
        {
            const char *const seq = GbaSymbolUtf8(c);
            if (seq != NULL)
                AppendUtf8(dst, cap, &pos, seq);
        }
    }
    dst[pos] = '\0';
    return pos;
}

size_t UiGame_Text(const uint8_t *const src, char *const dst, const size_t cap)
{
    return DecodeGbaString(src, dst, cap, false);
}

void UiGame_DecodeText(const uint8_t *const src, char *const dst, const size_t cap)
{
    DecodeGbaString(src, dst, cap, false);
}

void UiGame_DecodeTextLines(const uint8_t *const src, char *const dst, const size_t cap)
{
    DecodeGbaString(src, dst, cap, true);
}

bool UiGame_HasSaveData(void)
{
    return gSaveBlock1Ptr != NULL && gSaveBlock2Ptr != NULL
        && gSaveBlock2Ptr->playerName[0] != 0
        && gSaveBlock2Ptr->playerName[0] != EOS;
}

bool UiGame_InField(void)
{
    if (!UiGame_HasSaveData())
        return false;
    if (gMain.callback2 != CB2_Overworld || gMain.inBattle || gPaletteFade.active)
        return false;
    if (ArePlayerFieldControlsLocked() || ScriptContext_IsEnabled())
        return false;
    if (MenuHelpers_IsLinkActive() || InUnionRoom() || IsUpdateLinkStateCBActive())
        return false;
    if (QL_IS_PLAYBACK_STATE || HelpSystem_IsActiveForUi())
        return false;
    return true;
}

bool UiGame_InOverworld(void)
{
    if (!UiGame_HasSaveData())
        return false;
    return (gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic)
        && !gMain.inBattle
        && !QL_IS_PLAYBACK_STATE;
}

bool UiGame_InBattle(void)
{
    return gMain.inBattle != 0;
}

bool UiGame_PaletteFadeActive(void)
{
    return gPaletteFade.active != 0;
}

void UiGame_FieldLock(void)
{
    FreezeObjectEvents();
    HandleEnforcedLookDirectionOnPlayerStopMoving();
    StopPlayerAvatar();
    LockPlayerFieldControls();
}

void UiGame_FieldUnlock(void)
{
    ClearPlayerHeldMovementAndUnfreezeObjectEvents();
    UnlockPlayerFieldControls();
}

void UiGame_BeginSceneFadeOut(void)
{
    UiGame_FieldLock();
    StopPokemonLeagueLightingEffectTask();
    FadeScreen(FADE_TO_BLACK, 0);
}

void UiGame_CommitSceneSwitch(void (*enterScene)(void))
{
    if (enterScene == NULL)
        return;
    PlayRainStoppingSoundEffect();
    CleanupOverworldWindowsAndTilemaps();
    sReturningFromScene = true;
    SetMainCallback2(enterScene);
}

void UiGame_MarkSceneReturn(void)
{
    sReturningFromScene = true;
}

bool UiGame_ConsumeSceneReturn(void)
{
    if (!sReturningFromScene)
        return false;
    sReturningFromScene = false;
    return true;
}

void UiGame_ResetState(void)
{
    sReturningFromScene = false;
}

void UiGame_EnterPokedexScene(void)
{
    CB2_OpenPokedexFromStartMenu();
}

void UiGame_EnterPartyScene(void)
{
    CB2_PartyMenuFromStartMenu();
}

void UiGame_EnterBagScene(void)
{
    CB2_BagMenuFromStartMenu();
}

void UiGame_EnterTrainerCardScene(void)
{
    ShowPlayerTrainerCard(CB2_ReturnToFieldWithOpenMenu);
}

void UiGame_EnterGbaOptionsScene(void)
{
    CB2_OptionsMenuFromStartMenu();
}

void UiGame_TriggerSafariRetire(void)
{
    SafariZoneRetirePrompt();
}

bool UiGame_Save(void)
{
    if (!UiGame_HasSaveData() || InUnionRoom() || MenuHelpers_IsLinkActive())
        return false;
    const uint8_t status = TrySavingData(SAVE_NORMAL);
    VitaNuzlocke_Save();
    return status == SAVE_STATUS_OK;
}

bool UiGame_QuickSave(void)
{
    if (UiGame_Save())
    {
        PlaySE(SE_SAVE);
        return true;
    }
    PlaySE(SE_FAILURE);
    return false;
}

uint8_t UiGame_SaveSlot(void)
{
    return VitaControls_GetSaveSlot();
}

bool UiGame_SaveOverwritesOtherGame(void)
{
    return UiGame_HasSaveData() && gSaveFileStatus == SAVE_STATUS_EMPTY;
}

uint32_t UiGame_Money(void)
{
    return UiGame_HasSaveData() ? GetMoney(&gSaveBlock1Ptr->money) : 0U;
}

uint16_t UiGame_Coins(void)
{
    return UiGame_HasSaveData() ? (uint16_t)GetCoins() : 0U;
}

void UiGame_Clock(char *const out, const size_t cap)
{
    if (out == NULL || cap == 0U)
        return;
    uint32_t seconds = 0U, rate = 0U;
    if (!VitaDayNight_Time(&seconds, &rate))
    {
        snprintf(out, cap, "--:--");
        return;
    }
    const uint32_t hour = (seconds / 3600U) % 24U;
    const uint32_t minute = (seconds % 3600U) / 60U;
    snprintf(out, cap, "%02u:%02u", (unsigned)hour, (unsigned)minute);
}

bool UiGame_IsNight(void)
{
    uint32_t seconds = 0U, rate = 0U;
    if (!VitaDayNight_Time(&seconds, &rate))
        return false;
    const uint32_t hour = (seconds / 3600U) % 24U;
    return hour < 6U || hour >= 20U;
}

void UiGame_ZoneName(char *const out, const size_t cap)
{
    if (!UiGame_HasSaveData())
    {
        if (out != NULL && cap > 0U)
            snprintf(out, cap, "KANTO");
        return;
    }
    UiGame_GetMapSectionName(gMapHeader.regionMapSectionId, out, cap);
}

void UiGame_PlayerName(char *const out, const size_t cap)
{
    if (out == NULL || cap == 0U)
        return;
    if (!UiGame_HasSaveData())
    {
        snprintf(out, cap, "RED");
        return;
    }
    UiGame_DecodeText(gSaveBlock2Ptr->playerName, out, cap);
}

bool UiGame_PlayerIsMale(void)
{
    return !UiGame_HasSaveData() || gSaveBlock2Ptr->playerGender == MALE;
}

uint16_t UiGame_RegisteredItem(void)
{
    if (!UiGame_HasSaveData())
        return ITEM_NONE;
    const uint16_t item = gSaveBlock1Ptr->registeredItem;
    if (item == ITEM_NONE || !CheckBagHasItem(item, 1))
        return ITEM_NONE;
    return item;
}

void UiGame_ItemName(const uint16_t itemId, char *const out, const size_t cap)
{
    if (out == NULL || cap == 0U)
        return;
    if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
    {
        out[0] = '\0';
        return;
    }
    UiGame_DecodeText(ItemId_GetName(itemId), out, cap);
}

int32_t UiGame_ItemPocket(const uint16_t itemId)
{
    if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
        return 0;
    return (int32_t)GetPocketByItemId(itemId);
}

uint16_t UiGame_RepelSteps(void)
{
    return UiGame_HasSaveData() ? VarGet(VAR_REPEL_STEP_COUNT) : 0U;
}

bool UiGame_InSafari(void)
{
    return UiGame_HasSaveData() && (GetSafariZoneFlag() != 0);
}

uint8_t UiGame_SafariBalls(void)
{
    return gNumSafariBalls;
}

uint16_t UiGame_SafariSteps(void)
{
    return gSafariZoneStepCounter;
}

bool UiGame_HasFlag(const uint16_t flag)
{
    return UiGame_HasSaveData() && (FlagGet(flag) != 0);
}

uint8_t UiGame_BadgeCount(void)
{
    return VitaNuzlocke_GetBadgesCount();
}

uint16_t UiGame_DexOwned(void)
{
    if (!UiGame_HasSaveData() || !FlagGet(FLAG_SYS_POKEDEX_GET))
        return 0U;
    return IsNationalPokedexEnabled()
        ? GetNationalPokedexCount(FLAG_GET_CAUGHT)
        : GetKantoPokedexCount(FLAG_GET_CAUGHT);
}

uint16_t UiGame_DexSeen(void)
{
    if (!UiGame_HasSaveData() || !FlagGet(FLAG_SYS_POKEDEX_GET))
        return 0U;
    return IsNationalPokedexEnabled()
        ? GetNationalPokedexCount(FLAG_GET_SEEN)
        : GetKantoPokedexCount(FLAG_GET_SEEN);
}

void UiGame_PlayTime(char *const out, const size_t cap)
{
    if (out == NULL || cap == 0U)
        return;
    if (!UiGame_HasSaveData())
    {
        snprintf(out, cap, "0:00");
        return;
    }
    snprintf(out, cap, "%u:%02u",
             (unsigned)gSaveBlock2Ptr->playTimeHours,
             (unsigned)gSaveBlock2Ptr->playTimeMinutes);
}

void UiGame_GetMapSectionName(const uint16_t mapSec, char *const out, const size_t cap)
{
    if (out == NULL || cap == 0)
        return;
    uint8_t gbaBuf[32];
    memset(gbaBuf, EOS, sizeof(gbaBuf));
    GetMapNameGeneric(gbaBuf, mapSec);
    UiGame_DecodeText(gbaBuf, out, cap);
}

void UiGame_MapSecName(const uint16_t sec, char *const out, const size_t cap)
{
    UiGame_GetMapSectionName(sec, out, cap);
}

void UiGame_GetSpeciesName(const uint16_t species, char *const out, const size_t cap)
{
    if (out == NULL || cap == 0)
        return;
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
    {
        snprintf(out, cap, "---");
        return;
    }
    UiGame_DecodeText(gSpeciesNames[species], out, cap);
}

void UiGame_SpeciesName(const uint16_t species, char *const out, const size_t cap)
{
    UiGame_GetSpeciesName(species, out, cap);
}

void UiGame_GetPlayerSummary(UiPlayerSummary *const out)
{
    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
    if (!UiGame_HasSaveData())
        return;

    UiGame_DecodeText(gSaveBlock2Ptr->playerName, out->playerName, sizeof(out->playerName));
    out->playerGender = gSaveBlock2Ptr->playerGender;
    out->trainerId = (uint16_t)gSaveBlock2Ptr->playerTrainerId[0]
                   | ((uint16_t)gSaveBlock2Ptr->playerTrainerId[1] << 8);
    out->trainerPicId = PlayerGenderToFrontTrainerPicId(gSaveBlock2Ptr->playerGender, TRUE);
    out->playHours = gSaveBlock2Ptr->playTimeHours;
    out->playMinutes = gSaveBlock2Ptr->playTimeMinutes;
    out->badges = VitaNuzlocke_GetBadgesCount();
    out->partyCount = gPlayerPartyCount;
    out->hasPokedex = FlagGet(FLAG_SYS_POKEDEX_GET) != 0;
    out->hasParty = (FlagGet(FLAG_SYS_POKEMON_GET) != 0) && (gPlayerPartyCount > 0);
    out->nationalDex = IsNationalPokedexEnabled() != 0;
    if (out->hasPokedex)
    {
        out->dexSeen = out->nationalDex ? GetNationalPokedexCount(FLAG_GET_SEEN)
                                        : GetKantoPokedexCount(FLAG_GET_SEEN);
        out->dexCaught = out->nationalDex ? GetNationalPokedexCount(FLAG_GET_CAUGHT)
                                          : GetKantoPokedexCount(FLAG_GET_CAUGHT);
    }
    out->money = GetMoney(&gSaveBlock1Ptr->money);
    out->coins = GetCoins();
    out->inSafari = GetSafariZoneFlag() != 0;
    out->safariBalls = gNumSafariBalls;
    out->safariSteps = gSafariZoneStepCounter;
    out->mapSec = gMapHeader.regionMapSectionId;
    UiGame_GetMapSectionName(out->mapSec, out->mapName, sizeof(out->mapName));
}

static uint8_t StatusToBadgeRow(const uint32_t status, const uint16_t hp)
{
    if (hp == 0)
        return 6; // FNT
    if ((status & STATUS1_SLEEP) != 0)
        return 3; // SLP
    if ((status & (STATUS1_POISON | STATUS1_TOXIC_POISON)) != 0)
        return 1; // PSN
    if ((status & STATUS1_BURN) != 0)
        return 5; // BRN
    if ((status & STATUS1_FREEZE) != 0)
        return 4; // FRZ
    if ((status & STATUS1_PARALYSIS) != 0)
        return 2; // PAR
    return 0;
}

// View abstraction for party (sBoxViewBox == -1) or a non-empty PC box summary view.
static struct Pokemon sBoxView[IN_BOX_COUNT];
static uint8_t sBoxViewPos[IN_BOX_COUNT];
static int32_t sBoxViewCount;
static int32_t sBoxViewBox = -1;

static struct Pokemon *ViewMon(int32_t slot)
{
    if (slot < 0 || slot >= (sBoxViewBox >= 0 ? sBoxViewCount : PARTY_SIZE))
        slot = 0;
    return sBoxViewBox >= 0 ? &sBoxView[slot] : &gPlayerParty[slot];
}

int32_t UiGame_PartyCount(void)
{
    if (!UiGame_HasSaveData())
        return 0;
    return (int32_t)CalculatePlayerPartyCount();
}

int32_t UiGame_ViewCount(void)
{
    return sBoxViewBox >= 0 ? sBoxViewCount : UiGame_PartyCount();
}

int32_t UiGame_ViewBox(const uint8_t box, const int32_t pos)
{
    int32_t start = 0;
    sBoxViewCount = 0;
    for (int32_t i = 0; i < IN_BOX_COUNT; i++)
    {
        struct BoxPokemon *const bm = GetBoxedMonPtr(box, (u8)i);
        if (GetBoxMonData(bm, MON_DATA_SPECIES) == SPECIES_NONE)
            continue;
        if (i == pos)
            start = sBoxViewCount;
        BoxMonToMon(bm, &sBoxView[sBoxViewCount]);
        sBoxViewPos[sBoxViewCount++] = (uint8_t)i;
    }
    sBoxViewBox = (sBoxViewCount > 0) ? (int32_t)box : -1;
    return start;
}

int32_t UiGame_ViewPosition(const int32_t slot)
{
    if (sBoxViewBox < 0 || slot < 0 || slot >= sBoxViewCount)
        return slot;
    return sBoxViewPos[slot];
}

void UiGame_ViewParty(void)
{
    sBoxViewBox = -1;
    sBoxViewCount = 0;
}

bool UiGame_Mon(const int32_t slot, UiMon *const out)
{
    if (out == NULL)
        return false;
    memset(out, 0, sizeof(*out));
    if (slot < 0 || slot >= UiGame_ViewCount())
        return false;

    struct Pokemon *const mon = ViewMon(slot);
    const uint16_t speciesOrEgg = (uint16_t)GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    if (speciesOrEgg == SPECIES_NONE)
        return false;

    out->egg = GetMonData(mon, MON_DATA_IS_EGG) != 0;
    out->species = out->egg ? SPECIES_EGG : (uint16_t)GetMonData(mon, MON_DATA_SPECIES);
    out->hp = (uint16_t)GetMonData(mon, MON_DATA_HP);
    out->maxHp = (uint16_t)GetMonData(mon, MON_DATA_MAX_HP);
    out->level = (uint8_t)GetMonData(mon, MON_DATA_LEVEL);
    out->item = (uint16_t)GetMonData(mon, MON_DATA_HELD_ITEM);
    out->gender = out->egg ? MON_GENDERLESS : GetMonGender(mon);
    out->shiny = !out->egg && (IsMonShiny(mon) != 0);

    if (out->egg)
    {
        snprintf(out->nickname, sizeof(out->nickname), "EGG");
        snprintf(out->name, sizeof(out->name), "EGG");
        out->status = 0U;
        out->statusRow = 0U;
    }
    else
    {
        uint8_t nick[POKEMON_NAME_LENGTH + 2];
        memset(nick, EOS, sizeof(nick));
        GetMonData(mon, MON_DATA_NICKNAME, nick);
        nick[POKEMON_NAME_LENGTH] = EOS;
        UiGame_DecodeText(nick, out->nickname, sizeof(out->nickname));
        snprintf(out->name, sizeof(out->name), "%s", out->nickname);
        out->status = GetMonData(mon, MON_DATA_STATUS);
        out->statusRow = StatusToBadgeRow(out->status, out->hp);
    }
    return true;
}

uint16_t UiGame_SafeMove(const uint32_t move)
{
    return move < MOVES_COUNT ? (uint16_t)move : MOVE_NONE;
}

uint16_t UiGame_SafeSpecies(const uint32_t species)
{
    return (species < NUM_SPECIES || species == SPECIES_EGG) ? (uint16_t)species : SPECIES_NONE;
}

uint8_t UiGame_SafeAbility(const uint32_t ability)
{
    return ability < ABILITIES_COUNT ? (uint8_t)ability : ABILITY_NONE;
}

int32_t UiGame_MonStatusIcon(const int32_t slot)
{
    if (slot < 0 || slot >= UiGame_ViewCount())
        return -1;
    struct Pokemon *const mon = ViewMon(slot);
    if (GetMonData(mon, MON_DATA_IS_EGG))
        return -1;
    if (GetMonData(mon, MON_DATA_HP) == 0)
        return 6; // UI_SPR_STATUS_FNT
    const uint32_t st = GetMonData(mon, MON_DATA_STATUS);
    if (st & (STATUS1_POISON | STATUS1_TOXIC_POISON))
        return 0; // PSN
    if (st & STATUS1_PARALYSIS)
        return 1; // PAR
    if (st & STATUS1_SLEEP)
        return 2; // SLP
    if (st & STATUS1_FREEZE)
        return 3; // FRZ
    if (st & STATUS1_BURN)
        return 4; // BRN
    if (CheckPartyPokerus(mon, 1))
        return 5; // PKRS
    return -1;
}

void UiGame_Summary(const int32_t slot, UiSummary *const s)
{
    if (s == NULL)
        return;
    memset(s, 0, sizeof(*s));
    if (slot < 0 || slot >= UiGame_ViewCount())
        return;

    struct Pokemon *const mon = ViewMon(slot);
    const uint16_t sp = UiGame_SafeSpecies(GetMonData(mon, MON_DATA_SPECIES));
    u8 buf[64];

    const uint16_t dex = SpeciesToPokedexNum(sp);
    s->dexNo = (dex == 0xFFFF) ? 0U : dex;
    GetSpeciesName(buf, sp);
    UiGame_Text(buf, s->species, sizeof(s->species));
    s->types[0] = gSpeciesInfo[sp].types[0];
    s->types[1] = gSpeciesInfo[sp].types[1];

    GetMonData(mon, MON_DATA_OT_NAME, buf);
    buf[PLAYER_NAME_LENGTH] = EOS;
    UiGame_Text(buf, s->ot, sizeof(s->ot));
    s->otId = (uint16_t)(GetMonData(mon, MON_DATA_OT_ID) & 0xFFFF);

    const uint8_t nature = GetNature(mon);
    UiGame_Text(gNatureNamePointers[nature], s->nature, sizeof(s->nature));
    static const int8_t toDisplay[5] = { 1, 2, 5, 3, 4 };
    if (nature / 5 != nature % 5)
    {
        s->natureUp = toDisplay[nature / 5];
        s->natureDown = toDisplay[nature % 5];
    }

    const uint8_t ability = UiGame_SafeAbility(GetAbilityBySpecies(sp, (u8)GetMonData(mon, MON_DATA_ABILITY_NUM)));
    UiGame_Text(gAbilityNames[ability], s->ability, sizeof(s->ability));
    UiGame_Text(gAbilityDescriptionPointers[ability], s->abilityDesc, sizeof(s->abilityDesc));

    const uint16_t metLoc = (uint16_t)GetMonData(mon, MON_DATA_MET_LOCATION);
    if (metLoc == METLOC_FATEFUL_ENCOUNTER)
        snprintf(s->metAt, sizeof(s->metAt), "Fateful Encounter");
    else if (metLoc == METLOC_IN_GAME_TRADE)
        snprintf(s->metAt, sizeof(s->metAt), "In-Game Trade");
    else
    {
        GetMapNameGeneric(buf, metLoc);
        UiGame_Text(buf, s->metAt, sizeof(s->metAt));
    }

    s->metLevel = (uint8_t)GetMonData(mon, MON_DATA_MET_LEVEL);
    s->item = (uint16_t)GetMonData(mon, MON_DATA_HELD_ITEM);
    s->level = (uint8_t)GetMonData(mon, MON_DATA_LEVEL);

    const uint8_t rate = gSpeciesInfo[sp].growthRate;
    const uint32_t exp = GetMonData(mon, MON_DATA_EXP);
    if (s->level < MAX_LEVEL && rate <= GROWTH_SLOW)
    {
        const uint32_t lo = gExperienceTables[rate][s->level];
        const uint32_t hi = gExperienceTables[rate][s->level + 1];
        s->expToNext = hi > exp ? hi - exp : 0;
        s->expPct = hi > lo ? (uint8_t)((exp - lo) * 100U / (hi - lo)) : 100U;
    }
    s->exp = exp;

    static const u8 statIds[6] = { MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPATK, MON_DATA_SPDEF, MON_DATA_SPEED };
    static const u8 ivIds[6] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV, MON_DATA_SPEED_IV };
    static const u8 evIds[6] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV, MON_DATA_SPEED_EV };
    for (int32_t i = 0; i < 6; i++)
    {
        s->stats[i] = (uint16_t)GetMonData(mon, statIds[i]);
        s->ivs[i] = (uint8_t)GetMonData(mon, ivIds[i]);
        s->evs[i] = (uint8_t)GetMonData(mon, evIds[i]);
    }
    s->hp = (uint16_t)GetMonData(mon, MON_DATA_HP);
    s->showIvEv = VitaExtras_GetFast(FAST_EXTRA_IVS_EVS) != 0;
    s->eggCycles = (uint8_t)GetMonData(mon, MON_DATA_FRIENDSHIP);
}

void UiGame_Move(const int32_t slot, const int32_t idx, UiMove *const m)
{
    if (m == NULL)
        return;
    memset(m, 0, sizeof(*m));
    if (slot < 0 || slot >= UiGame_ViewCount() || idx < 0 || idx >= MAX_MON_MOVES)
        return;

    struct Pokemon *const mon = ViewMon(slot);
    if (GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG))
        return;

    m->move = UiGame_SafeMove(GetMonData(mon, MON_DATA_MOVE1 + idx));
    if (m->move == MOVE_NONE)
        return;

    const struct BattleMove *const bm = &gBattleMoves[m->move];
    UiGame_Text(gMoveNames[m->move], m->name, sizeof(m->name));
    UiGame_Text(gMoveDescriptionPointers[m->move - 1], m->desc, sizeof(m->desc));
    m->type = bm->type;
    m->power = bm->power;
    m->accuracy = bm->accuracy;
    m->pp = (uint8_t)GetMonData(mon, MON_DATA_PP1 + idx);
    m->maxPp = (uint8_t)CalculatePPWithBonus(m->move, (u8)GetMonData(mon, MON_DATA_PP_BONUSES), (u8)idx);
    m->category = MoveSplit_Category(m->move);
}

void UiGame_MoveInfo(const int32_t slot, const int32_t moveIdx, uint16_t *const move, uint8_t *const pp, uint8_t *const maxPp)
{
    struct Pokemon *const mon = ViewMon(slot);
    if (GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG))
    {
        if (move != NULL) *move = MOVE_NONE;
        if (pp != NULL) *pp = 0;
        if (maxPp != NULL) *maxPp = 0;
        return;
    }
    const uint16_t mv = UiGame_SafeMove(GetMonData(mon, MON_DATA_MOVE1 + moveIdx));
    if (move != NULL) *move = mv;
    if (pp != NULL) *pp = (uint8_t)GetMonData(mon, MON_DATA_PP1 + moveIdx);
    if (maxPp != NULL) *maxPp = mv != MOVE_NONE ? (uint8_t)CalculatePPWithBonus(mv, (u8)GetMonData(mon, MON_DATA_PP_BONUSES), (u8)moveIdx) : 0;
}

void UiGame_MoveName(const uint16_t move, char *const out, const size_t cap)
{
    if (out == NULL || cap == 0)
        return;
    if (move == MOVE_NONE || move >= MOVES_COUNT)
    {
        out[0] = '\0';
        return;
    }
    UiGame_Text(gMoveNames[move], out, cap);
}

void UiGame_SwapMoves(const int32_t slot, const int32_t a, const int32_t b)
{
    if (slot < 0 || slot >= UiGame_ViewCount() || a < 0 || a >= MAX_MON_MOVES || b < 0 || b >= MAX_MON_MOVES)
        return;
    struct Pokemon *const mon = ViewMon(slot);
    const uint16_t ma = (uint16_t)GetMonData(mon, MON_DATA_MOVE1 + a);
    const uint16_t mb = (uint16_t)GetMonData(mon, MON_DATA_MOVE1 + b);
    const uint8_t pa = (uint8_t)GetMonData(mon, MON_DATA_PP1 + a);
    const uint8_t pb = (uint8_t)GetMonData(mon, MON_DATA_PP1 + b);
    uint8_t bonuses = (uint8_t)GetMonData(mon, MON_DATA_PP_BONUSES);
    const uint8_t ba = (bonuses >> (a * 2)) & 3;
    const uint8_t bb = (bonuses >> (b * 2)) & 3;
    bonuses = (uint8_t)((bonuses & ~((3 << (a * 2)) | (3 << (b * 2)))) | (bb << (a * 2)) | (ba << (b * 2)));
    SetMonData(mon, MON_DATA_MOVE1 + a, &mb);
    SetMonData(mon, MON_DATA_MOVE1 + b, &ma);
    SetMonData(mon, MON_DATA_PP1 + a, &pb);
    SetMonData(mon, MON_DATA_PP1 + b, &pa);
    SetMonData(mon, MON_DATA_PP_BONUSES, &bonuses);
    if (sBoxViewBox >= 0)
        *GetBoxedMonPtr((u8)sBoxViewBox, sBoxViewPos[slot]) = mon->box;
}

void UiGame_SwapPartyMons(const int32_t a, const int32_t b)
{
    if (a < 0 || b < 0 || a >= UiGame_PartyCount() || b >= UiGame_PartyCount() || a == b)
        return;
    struct Pokemon tmp = gPlayerParty[a];
    gPlayerParty[a] = gPlayerParty[b];
    gPlayerParty[b] = tmp;
    PartyMenu_QuestLogSwitchedOrderForUi((u8)a, (u8)b);
}

uint16_t UiGame_FieldMove(const int32_t slot, const int32_t idx)
{
    if (slot < 0 || slot >= UiGame_PartyCount() || idx < 0 || idx >= MAX_MON_MOVES)
        return MOVE_NONE;
    struct Pokemon *const mon = &gPlayerParty[slot];
    if (GetMonData(mon, MON_DATA_IS_EGG))
        return MOVE_NONE;
    const uint16_t mv = (uint16_t)GetMonData(mon, MON_DATA_MOVE1 + idx);
    switch (mv)
    {
    case MOVE_FLASH:
    case MOVE_CUT:
    case MOVE_FLY:
    case MOVE_STRENGTH:
    case MOVE_SURF:
    case MOVE_ROCK_SMASH:
    case MOVE_WATERFALL:
    case MOVE_TELEPORT:
    case MOVE_DIG:
    case MOVE_MILK_DRINK:
    case MOVE_SOFT_BOILED:
    case MOVE_SWEET_SCENT:
        return mv;
    default:
        return MOVE_NONE;
    }
}

bool UiGame_ShareHp(const int32_t from, const int32_t to, char *const msg, const size_t cap)
{
    if (from < 0 || from >= UiGame_PartyCount() || to < 0 || to >= UiGame_PartyCount())
    {
        snprintf(msg, cap, "It can't be used on that POKéMON.");
        return false;
    }
    struct Pokemon *const a = &gPlayerParty[from];
    struct Pokemon *const b = &gPlayerParty[to];
    const uint16_t cost = (uint16_t)(GetMonData(a, MON_DATA_MAX_HP) / 5);
    const uint16_t hpA = (uint16_t)GetMonData(a, MON_DATA_HP);
    const uint16_t hpB = (uint16_t)GetMonData(b, MON_DATA_HP);
    const uint16_t maxB = (uint16_t)GetMonData(b, MON_DATA_MAX_HP);
    if (from == to || GetMonData(b, MON_DATA_IS_EGG) || hpB == 0 || hpB >= maxB)
    {
        snprintf(msg, cap, "It can't be used on that POKéMON.");
        return false;
    }
    if (hpA <= cost)
    {
        snprintf(msg, cap, "Not enough HP left.");
        return false;
    }
    const uint16_t give = (uint16_t)((maxB - hpB) < cost ? (maxB - hpB) : cost);
    const uint16_t na = (uint16_t)(hpA - cost);
    const uint16_t nb = (uint16_t)(hpB + give);
    SetMonData(a, MON_DATA_HP, &na);
    SetMonData(b, MON_DATA_HP, &nb);
    snprintf(msg, cap, "HP was restored by %u point(s).", (unsigned)give);
    return true;
}

bool UiGame_TakeItem(const int32_t slot, uint16_t *const taken)
{
    if (slot < 0 || slot >= UiGame_PartyCount())
        return false;
    struct Pokemon *const mon = &gPlayerParty[slot];
    const uint16_t item = (uint16_t)GetMonData(mon, MON_DATA_HELD_ITEM);
    if (item == ITEM_NONE || ItemIsMail(item))
        return false;
    if (!AddBagItem(item, 1))
        return false;
    const uint16_t none = ITEM_NONE;
    SetMonData(mon, MON_DATA_HELD_ITEM, &none);
    ItemUse_SetQuestLogEvent(QL_EVENT_TOOK_HELD_ITEM, mon, item, 0xFFFF);
    if (taken != NULL)
        *taken = item;
    return true;
}

int32_t UiGame_TakeMail(const int32_t slot, const bool sendToPc)
{
    if (slot < 0 || slot >= UiGame_PartyCount())
        return 0;
    struct Pokemon *const mon = &gPlayerParty[slot];
    const uint16_t item = (uint16_t)GetMonData(mon, MON_DATA_HELD_ITEM);
    if (!ItemIsMail(item))
        return 0;
    if (sendToPc)
        return (TakeMailFromMon2(mon) != 0xFF) ? 1 : -1;
    if (AddBagItem(item, 1) != TRUE)
        return -1;
    TakeMailFromMon(mon);
    return 1;
}

bool UiGame_GetPartySlot(const uint8_t slot, UiPartySlotSummary *const out)
{
    return UiGame_Mon((int32_t)slot, out);
}

uint8_t UiGame_GetTextSpeed(void)
{
    return UiGame_HasSaveData() ? (uint8_t)(gSaveBlock2Ptr->optionsTextSpeed % 3U) : OPTIONS_TEXT_SPEED_MID;
}

void UiGame_SetTextSpeed(const uint8_t value)
{
    const uint8_t v = value % 3U;
    if (UiGame_HasSaveData())
        gSaveBlock2Ptr->optionsTextSpeed = v;
    CtrSettings_SetInt("gba_text_speed", (int)v);
}

uint8_t UiGame_GetBattleScene(void)
{
    return UiGame_HasSaveData() ? (uint8_t)(gSaveBlock2Ptr->optionsBattleSceneOff & 1U) : 0U;
}

void UiGame_SetBattleScene(const uint8_t value)
{
    const uint8_t v = value & 1U;
    if (UiGame_HasSaveData())
        gSaveBlock2Ptr->optionsBattleSceneOff = v;
    CtrSettings_SetInt("gba_battle_scene", (int)v);
}

uint8_t UiGame_GetBattleStyle(void)
{
    return UiGame_HasSaveData() ? (uint8_t)(gSaveBlock2Ptr->optionsBattleStyle & 1U) : OPTIONS_BATTLE_STYLE_SHIFT;
}

void UiGame_SetBattleStyle(const uint8_t value)
{
    const uint8_t v = value & 1U;
    if (UiGame_HasSaveData())
        gSaveBlock2Ptr->optionsBattleStyle = v;
    CtrSettings_SetInt("gba_battle_style", (int)v);
}

uint8_t UiGame_GetSoundMode(void)
{
    return UiGame_HasSaveData() ? (uint8_t)(gSaveBlock2Ptr->optionsSound & 1U) : OPTIONS_SOUND_MONO;
}

void UiGame_SetSoundMode(const uint8_t value)
{
    const uint8_t v = value & 1U;
    if (UiGame_HasSaveData())
    {
        gSaveBlock2Ptr->optionsSound = v;
        SetPokemonCryStereo(gSaveBlock2Ptr->optionsSound);
    }
    CtrSettings_SetInt("gba_sound", (int)v);
}

uint8_t UiGame_GetButtonMode(void)
{
    return UiGame_HasSaveData() ? (uint8_t)(gSaveBlock2Ptr->optionsButtonMode % 3U) : OPTIONS_BUTTON_MODE_HELP;
}

void UiGame_SetButtonMode(const uint8_t value)
{
    const uint8_t v = value % 3U;
    if (UiGame_HasSaveData())
        gSaveBlock2Ptr->optionsButtonMode = v;
    CtrSettings_SetInt("gba_button_mode", (int)v);
}

uint8_t UiGame_GetWindowFrame(void)
{
    return UiGame_HasSaveData() ? (uint8_t)(gSaveBlock2Ptr->optionsWindowFrameType % 10U) : 0U;
}

void UiGame_SetWindowFrame(const uint8_t value)
{
    const uint8_t v = value % 10U;
    if (UiGame_HasSaveData())
        gSaveBlock2Ptr->optionsWindowFrameType = v;
    CtrSettings_SetInt("gba_window_frame", (int)v);
}

void UiGame_ApplySavedGbaOptions(void)
{
    if (!UiGame_HasSaveData())
        return;
    int v;
    if ((v = CtrSettings_GetInt("gba_text_speed", -1)) >= 0)
        gSaveBlock2Ptr->optionsTextSpeed = (u16)(v % 3);
    if ((v = CtrSettings_GetInt("gba_battle_scene", -1)) >= 0)
        gSaveBlock2Ptr->optionsBattleSceneOff = (u16)(v & 1);
    if ((v = CtrSettings_GetInt("gba_battle_style", -1)) >= 0)
        gSaveBlock2Ptr->optionsBattleStyle = (u16)(v & 1);
    if ((v = CtrSettings_GetInt("gba_sound", -1)) >= 0)
    {
        gSaveBlock2Ptr->optionsSound = (u16)(v & 1);
        SetPokemonCryStereo(gSaveBlock2Ptr->optionsSound);
    }
    if ((v = CtrSettings_GetInt("gba_button_mode", -1)) >= 0)
        gSaveBlock2Ptr->optionsButtonMode = (u16)(v % 3);
    if ((v = CtrSettings_GetInt("gba_window_frame", -1)) >= 0)
        gSaveBlock2Ptr->optionsWindowFrameType = (u16)(v % 10);
}
