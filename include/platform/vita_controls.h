#ifndef GUARD_PLATFORM_VITA_CONTROLS_H
#define GUARD_PLATFORM_VITA_CONTROLS_H

/*
 * vita_controls.h - PS Vita physical/touch controls, button remapping, and multi-slot save paths.
 *
 * Also keeps forwarding 3ds_input.h during the Stage 1 -> Stage 2 transition.
 */

#include <stdbool.h>
#include <stdint.h>
#include "3ds_input.h"

typedef enum
{
    VITA_ACTION_A = 0,
    VITA_ACTION_B,
    VITA_ACTION_SELECT,
    VITA_ACTION_START,
    VITA_ACTION_L,
    VITA_ACTION_R,
    VITA_ACTION_FAST_FORWARD,
    VITA_ACTION_VITA_MENU,
    VITA_ACTION_COUNT
} VitaAction;

void VitaControls_Init(void);
void VitaControls_Load(void);
void VitaControls_Save(void);

// Converts raw VITA_BTN_* mask into GBA KEY_* bitmask (using current button remap).
uint16_t VitaControls_MapButtons(uint32_t vitaButtons);
bool VitaControls_FastForwardPressed(uint32_t vitaButtons);
bool VitaControls_MenuPressed(uint32_t vitaButtons);

uint8_t VitaControls_GetButton(uint8_t action);
void VitaControls_SetButton(uint8_t action, uint8_t bitIdx);
uint32_t VitaControls_BitFromSetting(uint8_t bitIdx);
const char *VitaControls_ButtonName(uint8_t bitIdx);
const char *VitaControls_ActionName(uint8_t action);

uint8_t VitaControls_GetSaveSlot(void); // 1..3
void VitaControls_SetSaveSlot(uint8_t slot);
const char *VitaControls_GetSavePath(void);

#endif // GUARD_PLATFORM_VITA_CONTROLS_H
