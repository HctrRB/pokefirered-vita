#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle_mega.h"
#include "event_data.h"
#include "item.h"
#include "money.h"
#include "pokedex.h"
#include "sound.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/pokedex.h"
#include "constants/songs.h"
#include "3ds_platform.h"
#include "platform/vita_extras.h"
#include "platform/vita/vita_platform.h"
#include "platform/ui/ui_locale.h"   // UI_N: option rows, translated where drawn

#define SETTINGS_PATH VITA_DATA_DIR "settings.txt"
#define SETTINGS_MAX 96
#define SETTINGS_KEY_MAX 32
#define SPEED_MIN 1
#define SPEED_MAX 4

typedef struct
{
    char key[SETTINGS_KEY_MAX];
    int32_t value;
} SettingEntry;

static SettingEntry sSettings[SETTINGS_MAX];
static int32_t sSettingCount;

static const int sPitches[] = { 35, 25 };
static const int sZooms[] = { 90, 100, 110, 120 };
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int32_t sFastSettings[FAST_EXTRA_COUNT];
static int32_t sFastPitch = 35;
static int32_t sFastZoom = 100;
static int32_t sFastShowFps = 0;

static const char *CanonicalKey(const char *const key)
{
    if (key == NULL)
        return NULL;
    if (strcmp(key, "voxel_3d") == 0)     return "voxel";
    if (strcmp(key, "auto_run") == 0)     return "run_always";
    if (strcmp(key, "infinite_tms") == 0) return "reusable_tms";
    if (strcmp(key, "day_night") == 0)    return "daynight";
    if (strcmp(key, "voxel_pitch") == 0)  return "voxel_fov";
    return key;
}

static SettingEntry *FindSetting(const char *const key)
{
    const char *const canon = CanonicalKey(key);
    if (canon == NULL)
        return NULL;
    for (int32_t i = 0; i < sSettingCount; i++)
    {
        if (strcmp(sSettings[i].key, canon) == 0)
            return &sSettings[i];
    }
    return NULL;
}

static int FindIntInList(const int *const values, const int count, const int value, const int fallbackIdx)
{
    for (int i = 0; i < count; i++)
    {
        if (values[i] == value)
            return i;
    }
    return fallbackIdx;
}

static void SyncFastSettings(void)
{
    const SettingEntry *s;
    const int32_t rawFov = (s = FindSetting("voxel_fov")) != NULL ? s->value : 0;
    // Support both Emerald 0/1 (35/25 deg) and legacy degree values in settings.txt.
    const int32_t fovMode = (rawFov >= 20) ? ((rawFov < 30) ? 1 : 0) : ((rawFov != 0) ? 1 : 0);
    const int32_t pitch = sPitches[fovMode];
    const int32_t rawZoom = (s = FindSetting("voxel_zoom")) != NULL ? s->value : 100;
    const int32_t zoom = sZooms[FindIntInList(sZooms, ARRAY_LEN(sZooms), rawZoom, 1)];
    const int32_t daynightMode = (s = FindSetting("daynight")) != NULL ? s->value : 1;
    int32_t clockSpeed = (s = FindSetting("clock_speed")) != NULL ? s->value : 0;
    if (clockSpeed < 0)
        clockSpeed = 0;
    else if (clockSpeed >= 7)
        clockSpeed = 6;
    int32_t volume = (s = FindSetting("volume")) != NULL ? s->value : 10;
    if (volume < 0)
        volume = 0;
    else if (volume > 10)
        volume = 10;

    __atomic_store_n(&sFastPitch, pitch, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastZoom, zoom, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastShowFps, (s = FindSetting("show_fps")) != NULL ? (s->value != 0 ? 1 : 0) : 0, __ATOMIC_RELAXED);

    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_3D],
                     (s = FindSetting("voxel")) != NULL ? (s->value != 0 ? 1 : 0) : 1, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_BLUR],
                     (s = FindSetting("voxel_blur")) != NULL ? (s->value != 0 ? 1 : 0) : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_BATTLE],
                     (s = FindSetting("voxel_battle")) != NULL ? (s->value != 0 ? 1 : 0) : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_FPS],
                     (s = FindSetting("voxel_fps")) != NULL ? (s->value != 0 ? 1 : 0) : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_HD2D],
                     (s = FindSetting("voxel_hd2d")) != NULL ? s->value : VOXEL_HD2D_OFF, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_BLOOM],
                     (s = FindSetting("voxel_bloom")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_SKY_BLOOM],
                     (s = FindSetting("voxel_sky_bloom")) != NULL ? s->value : 5, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_SHAFTS],
                     (s = FindSetting("voxel_shafts")) != NULL ? s->value : 5, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOXEL_FOV], fovMode, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_FOLLOWER],
                     (s = FindSetting("follower")) != NULL ? (s->value != 0 ? 1 : 0) : 1, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_DAY_NIGHT], (daynightMode != 0) ? 1 : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_CLOCK_SPEED], clockSpeed, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VOLUME], volume, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_TIMED_ENCOUNTERS],
                     (s = FindSetting("timed_encounters")) != NULL ? s->value : 1, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_RUN_INDOORS],
                     (s = FindSetting("run_indoors")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_MOVE_SPLIT],
                     (s = FindSetting("move_split")) != NULL ? s->value : 1, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_INFINITE_TMS],
                     (s = FindSetting("reusable_tms")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_REPEL_PROMPT],
                     (s = FindSetting("repel_prompt")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_IVS_EVS],
                     (s = FindSetting("ivs_evs")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_EXP_SHARE],
                     (s = FindSetting("exp_share")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_EXP_CATCH],
                     (s = FindSetting("exp_catch")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_AUTO_RUN],
                     (s = FindSetting("run_always")) != NULL ? (s->value != 0 ? 1 : 0) : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_FIELD_HMS],
                     (s = FindSetting("field_hms")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_BATTLE_SPEED],
                     (s = FindSetting("battle_speed")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_FAST_EGGS],
                     (s = FindSetting("fast_eggs")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_NUZLOCKE],
                     (s = FindSetting("nuzlocke")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_LEVEL_CAP],
                     (s = FindSetting("level_cap")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_SHINY_ODDS],
                     (s = FindSetting("shiny_odds")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_ALWAYS_CATCH],
                     (s = FindSetting("always_catch")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_TRADE_EVO],
                     (s = FindSetting("trade_evo")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_UI_THEME],
                     (s = FindSetting("ui_theme")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_VISIBLE_WILD],
                     (s = FindSetting("visible_wild")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sFastSettings[FAST_EXTRA_ENCOUNTER_RATE],
                     (s = FindSetting("encounter_rate")) != NULL ? s->value : 0, __ATOMIC_RELAXED);
}

void VitaExtras_Init(void)
{
    FILE *file = fopen(SETTINGS_PATH, "r");
    char line[SETTINGS_KEY_MAX + 16];

    sSettingCount = 0;
    if (file == NULL)
    {
        SyncFastSettings();
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *equals = strchr(line, '=');
        if (equals == NULL || equals == line)
            continue;
        *equals = '\0';
        const char *const key = CanonicalKey(line);
        if (key == NULL || strlen(key) >= SETTINGS_KEY_MAX)
            continue;
        const int32_t val = atoi(equals + 1);
        SettingEntry *existing = FindSetting(key);
        if (existing != NULL)
        {
            existing->value = val;
            continue;
        }
        if (sSettingCount >= SETTINGS_MAX)
            continue;
        snprintf(sSettings[sSettingCount].key, SETTINGS_KEY_MAX, "%s", key);
        sSettings[sSettingCount].value = val;
        sSettingCount++;
    }
    fclose(file);
    SyncFastSettings();
}

void VitaExtras_Save(void)
{
    FILE *file = fopen(SETTINGS_PATH, "w");
    if (file == NULL)
    {
        VITA_LOG("[EXTRAS] failed to write %s\n", SETTINGS_PATH);
        return;
    }
    for (int32_t i = 0; i < sSettingCount; i++)
        fprintf(file, "%s=%d\n", sSettings[i].key, (int)sSettings[i].value);
    fclose(file);
}

// Strict Core 0/2 guarantee: only performs a relaxed atomic load; never calls VitaExtras_Init().
int32_t VitaExtras_GetFast(const FastExtraId id)
{
    if ((uint32_t)id >= (uint32_t)FAST_EXTRA_COUNT)
        return 0;
    return __atomic_load_n(&sFastSettings[id], __ATOMIC_RELAXED);
}

int CtrSettings_GetInt(const char *const key, const int fallback)
{
    const SettingEntry *const setting = FindSetting(key);
    return setting != NULL ? (int)setting->value : fallback;
}

void CtrSettings_SetInt(const char *const key, const int value)
{
    const char *const canon = CanonicalKey(key);
    if (canon == NULL || strlen(canon) >= SETTINGS_KEY_MAX)
        return;

    SettingEntry *setting = FindSetting(canon);
    if (setting == NULL)
    {
        if (sSettingCount >= SETTINGS_MAX)
            return;
        setting = &sSettings[sSettingCount++];
        snprintf(setting->key, SETTINGS_KEY_MAX, "%s", canon);
    }
    else if (setting->value == value)
    {
        return;
    }
    setting->value = value;
    SyncFastSettings();
    VitaExtras_Save();
}

int32_t VitaExtras_Get(const char *const key)
{
    return (int32_t)CtrSettings_GetInt(key, 0);
}

void VitaExtras_Set(const char *const key, const int32_t value)
{
    CtrSettings_SetInt(key, (int)value);
}

void CtrSettings_Load(void) { VitaExtras_Init(); }
void CtrSettings_Shutdown(void) { VitaExtras_Save(); }
bool CtrSettings_RunAlways(void) { return VitaExtras_GetFast(FAST_EXTRA_AUTO_RUN) != 0; }
void CtrSettings_SetRunAlways(bool on) { CtrSettings_SetInt("run_always", on ? 1 : 0); }

int CtrSettings_Speed(void)
{
    const int speed = CtrSettings_GetInt("speed", SPEED_MIN);
    return (speed < SPEED_MIN || speed > SPEED_MAX) ? SPEED_MIN : speed;
}

void CtrSettings_StepSpeed(int direction, bool wrap)
{
    int next = CtrSettings_Speed() + (direction < 0 ? -1 : 1);
    if (next < SPEED_MIN)
        next = wrap ? SPEED_MAX : SPEED_MIN;
    else if (next > SPEED_MAX)
        next = wrap ? SPEED_MIN : SPEED_MAX;
    CtrSettings_SetInt("speed", next);
}

bool CtrSettings_Voxel(void) { return VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0; }
void CtrSettings_SetVoxel(bool on) { CtrSettings_SetInt("voxel", on ? 1 : 0); }
int CtrSettings_VoxelPitch(void) { return __atomic_load_n(&sFastPitch, __ATOMIC_RELAXED); }
int CtrSettings_VoxelZoom(void) { return __atomic_load_n(&sFastZoom, __ATOMIC_RELAXED); }

void CtrSettings_StepVoxelPitch(int direction)
{
    (void)direction;
    const int cur = VitaExtras_GetFast(FAST_EXTRA_VOXEL_FOV) ? 1 : 0;
    CtrSettings_SetInt("voxel_fov", cur ^ 1);
}

void CtrSettings_StepVoxelZoom(int direction)
{
    const int idx = FindIntInList(sZooms, ARRAY_LEN(sZooms), CtrSettings_VoxelZoom(), 1);
    const int next = (idx + (direction < 0 ? ARRAY_LEN(sZooms) - 1 : 1)) % ARRAY_LEN(sZooms);
    CtrSettings_SetInt("voxel_zoom", sZooms[next]);
}

bool CtrSettings_VoxelBlur(void) { return VitaExtras_GetFast(FAST_EXTRA_VOXEL_BLUR) != 0; }
void CtrSettings_SetVoxelBlur(bool on) { CtrSettings_SetInt("voxel_blur", on ? 1 : 0); }
bool CtrSettings_VoxelBattle(void) { return VitaExtras_GetFast(FAST_EXTRA_VOXEL_BATTLE) != 0; }
void CtrSettings_SetVoxelBattle(bool on) { CtrSettings_SetInt("voxel_battle", on ? 1 : 0); }
bool CtrSettings_ShowFps(void) { return __atomic_load_n(&sFastShowFps, __ATOMIC_RELAXED) != 0; }
void CtrSettings_SetShowFps(bool on) { CtrSettings_SetInt("show_fps", on ? 1 : 0); }

// ---------------- FireRed Core 1 cheat / enhancement actions & Options table ----------------

const char *const gVitaExtrasOffOn[2] = { UI_N("OFF"), UI_N("ON") };
static const char *const sHd2dVals[VOXEL_HD2D_COUNT] = { UI_N("OFF"), UI_N("LOW"), UI_N("MED"), UI_N("HIGH") };
static const char *const sFpsVals[2] = { UI_N("30 FPS"), UI_N("60 FPS") };
static const char *const sThemeVals[6] = { UI_N("GBA"), UI_N("RED"), UI_N("EMERALD"), UI_N("CRYSTAL"), UI_N("TROPICAL"), UI_N("ROYAL") };
static const char *const sClockSpeedVals[7] = { UI_N("1X REAL"), "6X", "12X", "24X", "60X", "120X", "1000X" };
static const char *const sVisibleWildVals[4] = { UI_N("OFF"), UI_N("LOW"), UI_N("MED"), UI_N("HIGH") };
static const char *const sEncounterVals[5] = { UI_N("NORMAL"), UI_N("OFF"), "1/4", "1/2", "2X" };
static const char *const sShinyVals[6] = { "1/8192", "1/4096", "1/1024", "1/256", "1/64", UI_N("ALWAYS") };
static const char *const sActionTapVals[1] = { UI_N("ACTIVATE") };
static const char *const sMoneyVals[1] = { UI_N("MAX MONEY") };
static const char *const sPocketVals[5] = { UI_N("ITEMS"), UI_N("KEY ITEMS"), UI_N("POKE BALLS"), UI_N("TM CASE"), UI_N("BERRIES") };
static const char *const sCountVals[5] = { "1", "5", "10", "50", "99" };
static const u8 sCountAmounts[5] = { 1, 5, 10, 50, 99 };
static const u8 sPocketIds[5] = { POCKET_ITEMS, POCKET_KEY_ITEMS, POCKET_POKE_BALLS, POCKET_TM_CASE, POCKET_BERRY_POUCH };

static const char *const sBloomVals[11] = { UI_N("OFF"), "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
static const char *const sScaleVals[11] = { UI_N("OFF"), "0.2X", "0.4X", "0.6X", "0.8X", "1.0X", "1.2X", "1.4X", "1.6X", "1.8X", "2.0X" };

static void ActSetDexRange(const u16 maxNational)
{
    for (u16 nat = 1; nat <= maxNational; nat++)
    {
        GetSetPokedexFlag(nat, FLAG_SET_SEEN);
        GetSetPokedexFlag(nat, FLAG_SET_CAUGHT);
    }
    PlaySE(SE_SAVE);
}

static void ActCompleteKantoDex(void) { ActSetDexRange(KANTO_DEX_COUNT); }

static void ActUnlockNationalDex(void)
{
    EnableNationalPokedex();
    PlaySE(SE_SAVE);
}

static void ActCompleteNationalDex(void)
{
    EnableNationalPokedex();
    ActSetDexRange(NATIONAL_DEX_COUNT);
}

static void ActGiveMegaKit(void)
{
    FlagSet(FLAG_MEGA_KEY_STONE);
    AddBagItem(ITEM_MEGA_STONE, 1);
    AddBagItem(ITEM_MEGA_STONE_Y, 1);
    PlaySE(SE_SAVE);
}

static void ActMaxMoney(void)
{
    if (gSaveBlock1Ptr != NULL)
    {
        SetMoney(&gSaveBlock1Ptr->money, 999999);
        PlaySE(SE_SAVE);
    }
}

static void ActInfiniteMoneyToggle(void)
{
    if (CtrSettings_GetInt("infinite_money", 0) != 0)
        ActMaxMoney();
}

static bool IsValidGiveItem(const u16 item, const u8 wantPocket)
{
    if (item == ITEM_NONE || item >= ITEMS_COUNT)
        return false;
    if (ItemId_GetPocket(item) != wantPocket)
        return false;
    const u8 *const name = ItemId_GetName(item);
    /* Filter unused/dummy slots whose name starts with "??" (CHAR_QUESTION_MARK = 0xAC). */
    if (name == NULL || (name[0] == 0xACu && name[1] == 0xACu))
        return false;
    return true;
}

static u16 FindValidGiveItem(u16 item, int dir, int pocketIdx)
{
    const u8 wantPocket = sPocketIds[(pocketIdx >= 0 && pocketIdx < 5) ? pocketIdx : 0];
    const int step = (dir < 0) ? -1 : 1;
    const int span = ITEMS_COUNT - 1;
    for (int tries = 1; tries < ITEMS_COUNT; tries++)
    {
        const int offset = ((int)item - 1 + step * tries) % span;
        const int cand = 1 + (offset < 0 ? offset + span : offset);
        if (IsValidGiveItem((u16)cand, wantPocket))
            return (u16)cand;
    }
    return ITEM_POTION;
}

static u16 CurrentGiveItem(void)
{
    const int pocketIdx = CtrSettings_GetInt("give_pocket", 0);
    const u16 item = (u16)CtrSettings_GetInt("give_item", ITEM_POTION);
    const u8 wantPocket = sPocketIds[(pocketIdx >= 0 && pocketIdx < 5) ? pocketIdx : 0];
    if (IsValidGiveItem(item, wantPocket))
        return item;
    return FindValidGiveItem(1, 1, pocketIdx);
}

static void StepGiveItem(int direction)
{
    const int pocketIdx = CtrSettings_GetInt("give_pocket", 0);
    const u16 next = FindValidGiveItem(CurrentGiveItem(), direction, pocketIdx);
    CtrSettings_SetInt("give_item", next);
}

static const uint8_t *TextGiveItem(void)
{
    return ItemId_GetName(CurrentGiveItem());
}

static void ActGiveItemsNow(void)
{
    const u16 item = CurrentGiveItem();
    const int countIdx = CtrSettings_GetInt("give_count", 0);
    u16 qty = sCountAmounts[(countIdx >= 0 && countIdx < 5) ? countIdx : 0];
    if (ItemId_GetPocket(item) == POCKET_KEY_ITEMS)
        qty = 1;
    if (item != ITEM_NONE && CheckBagHasSpace(item, qty) && AddBagItem(item, qty))
        PlaySE(SE_SAVE);
    else
        PlaySE(SE_FAILURE);
}

static void ActStepZoom(int direction) { CtrSettings_StepVoxelZoom(direction); }

static const char *const sPitchLabels[2] = { UI_N("35 DEG"), UI_N("25 DEG") };
static const char *const sZoomLabels[4]  = { "90%", "100%", "110%", "120%" };

const VitaExtra gVitaExtras[] = {
    // GRAPHICS
    { VITA_TAB_GRAPHICS, UI_N("DISPLAY"), UI_N("UI THEME"),     "ui_theme",        6, 0, sThemeVals, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("3D WORLD"),UI_N("3D OVERWORLD"), "voxel",           2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("3D WORLD"),UI_N("3D FRAMERATE"), "voxel_fps",       2, 0, sFpsVals, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("HD-2D PRESET"), "voxel_hd2d",      VOXEL_HD2D_COUNT, VOXEL_HD2D_OFF, sHd2dVals, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("CAMERA ANGLE"), "voxel_fov",       2, 0, sPitchLabels, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("CAMERA ZOOM"),  "voxel_zoom",      4, 1, sZoomLabels, NULL, ActStepZoom, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("WORLD BLOOM"),  "voxel_bloom",    11, 0, sBloomVals, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("SKY BLOOM"),    "voxel_sky_bloom",11, 5, sScaleVals, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("LIGHT SHAFTS"), "voxel_shafts",   11, 5, sScaleVals, NULL, NULL, NULL },
    { VITA_TAB_GRAPHICS, UI_N("HD-2D"),   UI_N("DEPTH BLUR"),   "voxel_blur",      2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },

    // WORLD
    { VITA_TAB_WORLD,    UI_N("FIELD"),   UI_N("AUTO RUN"),     "run_always",      2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("FIELD"),   UI_N("RUN INDOORS"),  "run_indoors",     2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("FIELD"),   UI_N("FOLLOWER MON"), "follower",        2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("FIELD"),   UI_N("FIELD HMS"),    "field_hms",       2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("FIELD"),   UI_N("REPEL PROMPT"), "repel_prompt",    2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("TIME"),    UI_N("DAY / NIGHT"),  "daynight",        2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("TIME"),    UI_N("TIMED WILDS"),  "timed_encounters",2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("TIME"),    UI_N("CLOCK SPEED"),  "clock_speed",     7, 0, sClockSpeedVals, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("WILD"),    UI_N("VISIBLE WILD"), "visible_wild",    4, 0, sVisibleWildVals, NULL, NULL, NULL },
    { VITA_TAB_WORLD,    UI_N("WILD"),    UI_N("ENCOUNTERS"),   "encounter_rate",  5, 0, sEncounterVals, NULL, NULL, NULL },

    // BATTLE
    { VITA_TAB_BATTLE,   UI_N("COMBAT"),  UI_N("3D BATTLES"),   "voxel_battle",    2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("COMBAT"),  UI_N("PHYS/SPEC"),    "move_split",      2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("COMBAT"),  UI_N("RENEW TEAMS"),  "trainer_teams",   2, 1, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("GROWTH"),  UI_N("PARTY EXP"),    "exp_share",       2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("GROWTH"),  UI_N("CATCH EXP"),    "exp_catch",       2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("GROWTH"),  UI_N("TRADE EVO 40"), "trade_evo",       2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("GROWTH"),  UI_N("FAST EGGS"),    "fast_eggs",       2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_BATTLE,   UI_N("GROWTH"),  UI_N("REUSABLE TMS"), "reusable_tms",    2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },

    // MODES
    { VITA_TAB_MODES,    UI_N("CHALLENGE"),UI_N("NUZLOCKE"),    "nuzlocke",        2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_MODES,    UI_N("CHALLENGE"),UI_N("LEVEL CAP"),   "level_cap",       2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },

    // CHEATS
    { VITA_TAB_CHEATS,   UI_N("RATES"),   UI_N("SHINY ODDS"),   "shiny_odds",      6, 0, sShinyVals, NULL, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("BATTLE"),  UI_N("ALWAYS CATCH"), "always_catch",    2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("BATTLE"),  UI_N("INSTANT WIN"),  "instant_victory", 2, 0, gVitaExtrasOffOn, NULL, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("BAG"),     UI_N("INF MONEY"),    "infinite_money",  2, 0, gVitaExtrasOffOn, ActInfiniteMoneyToggle, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("BAG"),     UI_N("MONEY TO MAX"), "money_max",       0, 0, sMoneyVals, ActMaxMoney, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("DEX"),     UI_N("KANTO DEX"),    "dex_kanto",       0, 0, sActionTapVals, ActCompleteKantoDex, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("DEX"),     UI_N("NAT DEX ON"),   "dex_national_on", 0, 0, sActionTapVals, ActUnlockNationalDex, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("DEX"),     UI_N("NAT DEX FULL"), "dex_national",    0, 0, sActionTapVals, ActCompleteNationalDex, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("DEX"),     UI_N("MEGA KIT"),     "mega_kit",        0, 0, sActionTapVals, ActGiveMegaKit, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("ITEMS"),   UI_N("GIVE POCKET"),  "give_pocket",     5, 0, sPocketVals, NULL, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("ITEMS"),   UI_N("GIVE ITEM"),    "give_item",       1, 0, NULL, NULL, StepGiveItem, TextGiveItem },
    { VITA_TAB_CHEATS,   UI_N("ITEMS"),   UI_N("GIVE COUNT"),   "give_count",      5, 0, sCountVals, NULL, NULL, NULL },
    { VITA_TAB_CHEATS,   UI_N("ITEMS"),   UI_N("GIVE NOW"),     "give_now",        0, 0, sActionTapVals, ActGiveItemsNow, NULL, NULL },
};

const unsigned gVitaExtraCount = (unsigned)(sizeof(gVitaExtras) / sizeof(gVitaExtras[0]));

static int FindPitchOrZoomIndex(const VitaExtra *const extra)
{
    if (strcmp(extra->key, "voxel_fov") == 0)
        return VitaExtras_GetFast(FAST_EXTRA_VOXEL_FOV) ? 1 : 0;
    if (strcmp(extra->key, "voxel_zoom") == 0)
        return FindIntInList(sZooms, ARRAY_LEN(sZooms), CtrSettings_VoxelZoom(), 1);
    return -1;
}

uint8_t VitaExtras_ValueIndex(const VitaExtra *const extra)
{
    if (extra == NULL || extra->count == 0)
        return 0U;
    const int mapped = FindPitchOrZoomIndex(extra);
    if (mapped >= 0)
        return (uint8_t)mapped;
    const int raw = CtrSettings_GetInt(extra->key, extra->fallback);
    if (raw < 0)
        return extra->fallback % extra->count;
    return (uint8_t)(raw % extra->count);
}

void VitaExtras_Step(const VitaExtra *const extra, const int direction)
{
    if (extra == NULL)
        return;

    if (extra->step != NULL)
    {
        extra->step(direction);
        return;
    }
    if (extra->count == 0)
    {
        if (extra->act != NULL)
            extra->act();
        return;
    }

    const int cur = VitaExtras_ValueIndex(extra);
    const int next = (cur + (direction < 0 ? (int)extra->count - 1 : 1)) % (int)extra->count;
    CtrSettings_SetInt(extra->key, next);
    if (extra->act != NULL)
        extra->act();
}

unsigned VitaExtras_GetTabExtraCount(const uint8_t tab)
{
    unsigned count = 0;
    for (unsigned i = 0; i < gVitaExtraCount; i++)
    {
        if (gVitaExtras[i].tab == tab)
            count++;
    }
    return count;
}

const VitaExtra *VitaExtras_GetTabExtra(const uint8_t tab, const unsigned index)
{
    unsigned seen = 0;
    for (unsigned i = 0; i < gVitaExtraCount; i++)
    {
        if (gVitaExtras[i].tab != tab)
            continue;
        if (seen == index)
            return &gVitaExtras[i];
        seen++;
    }
    return NULL;
}
