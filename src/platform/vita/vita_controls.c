#include <stdio.h>
#include <string.h>

#include "global.h"
#include "gba/io_reg.h"
#include "platform.h"
#include "3ds_platform.h"
#include "platform/vita_controls.h"
#include "platform/vita_extras.h"
#include "platform/vita_nuzlocke.h"
#include "platform/vita/vita_platform.h"

#define SAVE_SLOT_MIN 1U
#define SAVE_SLOT_MAX 3U
#define VITA_BIT_COUNT 16U

typedef struct
{
    const char *actionName;
    const char *key;
    uint8_t defaultBit;
} ActionDefault;

static const ActionDefault kActionDefaults[VITA_ACTION_COUNT] = {
    [VITA_ACTION_A]            = { "A BUTTON",     "btn_a",      4U },  // VITA_BTN_CROSS
    [VITA_ACTION_B]            = { "B BUTTON",     "btn_b",      5U },  // VITA_BTN_CIRCLE
    [VITA_ACTION_SELECT]       = { "SELECT",       "btn_select", 11U }, // VITA_BTN_SELECT
    [VITA_ACTION_START]        = { "START",        "btn_start",  10U }, // VITA_BTN_START
    [VITA_ACTION_L]            = { "L SHOULDER",   "btn_l",      8U },  // VITA_BTN_L
    [VITA_ACTION_R]            = { "R SHOULDER",   "btn_r",      9U },  // VITA_BTN_R
    [VITA_ACTION_FAST_FORWARD] = { "FAST FORWARD", "btn_ff",     7U },  // VITA_BTN_TRIANGLE
    [VITA_ACTION_VITA_MENU]    = { "UI / TOUCH",   "btn_menu",   6U },  // VITA_BTN_SQUARE
};

static const char *const kBitNames[VITA_BIT_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE",
    "L", "R", "START", "SELECT",
    "RS UP", "RS DOWN", "RS LEFT", "RS RIGHT",
};

static uint8_t sActionBit[VITA_ACTION_COUNT];
static uint8_t sSaveSlot = 1U;
static char sSavePath[128] = VITA_DATA_DIR "saves/firered.sav";

static void RebuildSavePath(void)
{
    if (sSaveSlot <= 1U)
        snprintf(sSavePath, sizeof(sSavePath), "%ssaves/firered.sav", VITA_DATA_DIR);
    else
        snprintf(sSavePath, sizeof(sSavePath), "%ssaves/firered_%u.sav", VITA_DATA_DIR, (unsigned)sSaveSlot);
}

void VitaControls_Load(void)
{
    for (uint8_t i = 0; i < VITA_ACTION_COUNT; i++)
    {
        const int32_t raw = VitaExtras_Get(kActionDefaults[i].key);
        const bool present = (CtrSettings_GetInt(kActionDefaults[i].key, -1) >= 0);
        if (!present)
            sActionBit[i] = kActionDefaults[i].defaultBit;
        else if (raw == VITA_BTN_UNBOUND || (raw >= 0 && (uint32_t)raw < VITA_BIT_COUNT))
            sActionBit[i] = (uint8_t)raw;
        else
            sActionBit[i] = kActionDefaults[i].defaultBit;
    }

    const int32_t slot = CtrSettings_GetInt("save_file", 1);
    sSaveSlot = (slot >= (int32_t)SAVE_SLOT_MIN && slot <= (int32_t)SAVE_SLOT_MAX) ? (uint8_t)slot : 1U;
    RebuildSavePath();
}

void VitaControls_Init(void)
{
    VitaControls_Load();
}

void VitaControls_Save(void)
{
    for (uint8_t i = 0; i < VITA_ACTION_COUNT; i++)
        VitaExtras_Set(kActionDefaults[i].key, sActionBit[i]);
    VitaExtras_Set("save_file", sSaveSlot);
}

uint32_t VitaControls_BitFromSetting(const uint8_t bitIdx)
{
    if (bitIdx >= VITA_BIT_COUNT)
        return 0U;
    return 1U << bitIdx;
}

uint8_t VitaControls_GetButton(const uint8_t action)
{
    if (action >= VITA_ACTION_COUNT)
        return VITA_BTN_UNBOUND;
    return __atomic_load_n(&sActionBit[action], __ATOMIC_RELAXED);
}

void VitaControls_SetButton(const uint8_t action, const uint8_t bitIdx)
{
    if (action >= VITA_ACTION_COUNT)
        return;
    const uint8_t clean = (bitIdx < VITA_BIT_COUNT) ? bitIdx : VITA_BTN_UNBOUND;
    __atomic_store_n(&sActionBit[action], clean, __ATOMIC_RELAXED);
    VitaExtras_Set(kActionDefaults[action].key, clean);
}

const char *VitaControls_ButtonName(const uint8_t bitIdx)
{
    if (bitIdx < VITA_BIT_COUNT)
        return kBitNames[bitIdx];
    return "NONE";
}

const char *VitaControls_ActionName(const uint8_t action)
{
    if (action >= VITA_ACTION_COUNT)
        return "?";
    return kActionDefaults[action].actionName;
}

uint8_t VitaControls_GetSaveSlot(void)
{
    return __atomic_load_n(&sSaveSlot, __ATOMIC_RELAXED);
}

void VitaControls_SetSaveSlot(const uint8_t slot)
{
    const uint8_t clean = (slot >= SAVE_SLOT_MIN && slot <= SAVE_SLOT_MAX) ? slot : 1U;
    __atomic_store_n(&sSaveSlot, clean, __ATOMIC_RELAXED);
    RebuildSavePath();
    VitaExtras_Set("save_file", clean);
    VitaNuzlocke_Init();
}

const char *VitaControls_GetSavePath(void)
{
    return sSavePath;
}

uint16_t VitaControls_MapButtons(const uint32_t vitaButtons)
{
    uint16_t gba = 0U;

    if (vitaButtons & VITA_BTN_UP)    gba |= DPAD_UP;
    if (vitaButtons & VITA_BTN_DOWN)  gba |= DPAD_DOWN;
    if (vitaButtons & VITA_BTN_LEFT)  gba |= DPAD_LEFT;
    if (vitaButtons & VITA_BTN_RIGHT) gba |= DPAD_RIGHT;

    if (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_A)))      gba |= A_BUTTON;
    if (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_B)))      gba |= B_BUTTON;
    if (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_SELECT))) gba |= SELECT_BUTTON;
    if (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_START)))  gba |= START_BUTTON;
    if (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_L)))      gba |= L_BUTTON;
    if (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_R)))      gba |= R_BUTTON;
    return gba;
}

bool VitaControls_FastForwardPressed(const uint32_t vitaButtons)
{
    static bool sPrevPressed = false;
    static bool sToggleLatched = false;
    const bool pressed = (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_FAST_FORWARD))) != 0U;

    if (Platform_GetSetting(PLATFORM_SETTING_FAST_FORWARD_TOGGLE) != 0U)
    {
        if (pressed && !sPrevPressed)
            sToggleLatched = !sToggleLatched;
        sPrevPressed = pressed;
        return sToggleLatched;
    }

    sPrevPressed = pressed;
    sToggleLatched = false;
    return pressed;
}

bool VitaControls_MenuPressed(const uint32_t vitaButtons)
{
    return (vitaButtons & VitaControls_BitFromSetting(VitaControls_GetButton(VITA_ACTION_VITA_MENU))) != 0U;
}

u8 Platform_GetSetting(const enum PlatformSetting setting)
{
    if (setting >= PLATFORM_SETTING_BUTTON_FIRST && setting <= PLATFORM_SETTING_BUTTON_LAST)
        return VitaControls_GetButton((uint8_t)(setting - PLATFORM_SETTING_BUTTON_FIRST));

    switch (setting)
    {
    case PLATFORM_SETTING_VOLUME:
    {
        const int32_t v = CtrSettings_GetInt("volume", 10);
        return (u8)(v < 0 ? 0 : (v > 10 ? 10 : v));
    }
    case PLATFORM_SETTING_WIDESCREEN:
        return (u8)(CtrSettings_GetInt("widescreen", 1) != 0);
    case PLATFORM_SETTING_FAST_FORWARD_SPEED:
        return (u8)CtrSettings_Speed();
    case PLATFORM_SETTING_FAST_FORWARD_TOGGLE:
        return (u8)(CtrSettings_GetInt("ff_toggle", 0) != 0);
    case PLATFORM_SETTING_SAVE_FILE:
        return VitaControls_GetSaveSlot();
    case PLATFORM_SETTING_AUTO_RUN:
        return (u8)(VitaExtras_GetFast(FAST_EXTRA_AUTO_RUN) != 0);
    case PLATFORM_SETTING_HM_SHORTCUTS:
        return (u8)(VitaExtras_GetFast(FAST_EXTRA_FIELD_HMS) != 0);
    case PLATFORM_SETTING_STATS_INFO:
        return (u8)(VitaExtras_GetFast(FAST_EXTRA_IVS_EVS) != 0);
    case PLATFORM_SETTING_EXP_SHARE:
        return (u8)(VitaExtras_GetFast(FAST_EXTRA_EXP_SHARE) != 0);
    case PLATFORM_SETTING_VOXEL_3D:
        return (u8)(VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0);
    case PLATFORM_SETTING_VOXEL_FPS:
        return (u8)(VitaExtras_GetFast(FAST_EXTRA_VOXEL_FPS) != 0);
    default:
        return 0U;
    }
}

void Platform_SetSetting(const enum PlatformSetting setting, const u8 value)
{
    if (setting >= PLATFORM_SETTING_BUTTON_FIRST && setting <= PLATFORM_SETTING_BUTTON_LAST)
    {
        VitaControls_SetButton((uint8_t)(setting - PLATFORM_SETTING_BUTTON_FIRST), value);
        return;
    }

    switch (setting)
    {
    case PLATFORM_SETTING_VOLUME:
        VitaExtras_Set("volume", value > 10 ? 10 : value);
        break;
    case PLATFORM_SETTING_WIDESCREEN:
        VitaExtras_Set("widescreen", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_FAST_FORWARD_SPEED:
        VitaExtras_Set("speed", value < 1 ? 1 : (value > 4 ? 4 : value));
        break;
    case PLATFORM_SETTING_FAST_FORWARD_TOGGLE:
        VitaExtras_Set("ff_toggle", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_SAVE_FILE:
        VitaControls_SetSaveSlot(value);
        break;
    case PLATFORM_SETTING_AUTO_RUN:
        VitaExtras_Set("auto_run", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_HM_SHORTCUTS:
        VitaExtras_Set("field_hms", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_STATS_INFO:
        VitaExtras_Set("ivs_evs", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_EXP_SHARE:
        VitaExtras_Set("exp_share", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_VOXEL_3D:
        VitaExtras_Set("voxel_3d", value != 0 ? 1 : 0);
        break;
    case PLATFORM_SETTING_VOXEL_FPS:
        VitaExtras_Set("voxel_fps", value != 0 ? 1 : 0);
        break;
    default:
        break;
    }
}
