#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include "vita_input.h"
#include "vita_platform.h"

// Analog stick deflection thresholds (0-255, center 128)
static const uint8_t STICK_DEADZONE_LOW  = 64U;
static const uint8_t STICK_DEADZONE_HIGH = 192U;

// Touch screen dimensions
static const float TOUCH_PANEL_MAX_X = 1920.0f;
static const float TOUCH_PANEL_MAX_Y = 1088.0f;
static const float SCREEN_WIDTH_F    = 960.0f;
static const float SCREEN_HEIGHT_F   = 544.0f;

// GBA centered viewport on Vita display (816x544, offset 72px)
static const int32_t GBA_VIEWPORT_OFFSET_X = 72;
static const int32_t GBA_VIEWPORT_WIDTH    = 816;
static const int32_t GBA_VIEWPORT_HEIGHT   = 544;
static const int32_t GBA_NATIVE_WIDTH      = 240;
static const int32_t GBA_NATIVE_HEIGHT     = 160;

// Single-producer state updated only by VitaInput_Poll (Core 1), then published via seqlock.
static VitaInputState sProducerState;
static uint32_t sPreviousButtons = 0U;
static VitaInputState sPublishedState;
static uint32_t sInputSeq = 0U;

bool VitaInput_Init(void)
{
    // Configure sampling mode for both analog sticks
    const int32_t ctrlResult = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    if (ctrlResult < 0)
    {
        VITA_LOG("[INPUT] sceCtrlSetSamplingMode failed: 0x%08X\n", ctrlResult);
        return false;
    }

    // Start sampling the front capacitive touch panel
    const int32_t touchResult = sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    if (touchResult < 0)
    {
        VITA_LOG("[INPUT] sceTouchSetSamplingState failed: 0x%08X\n", touchResult);
        return false;
    }

    memset(&sProducerState, 0, sizeof(sProducerState));
    memset(&sPublishedState, 0, sizeof(sPublishedState));
    sProducerState.touchGbaX = -1;
    sProducerState.touchGbaY = -1;
    sPublishedState.touchGbaX = -1;
    sPublishedState.touchGbaY = -1;
    sPreviousButtons = 0U;
    __atomic_store_n(&sInputSeq, 0U, __ATOMIC_RELEASE);

    VITA_LOG("[INPUT] Initialized controller and front touch panel.\n");
    return true;
}

static uint32_t DecodeButtons(const SceCtrlData *const pad)
{
    uint32_t buttons = VITA_BTN_NONE;

    if (pad->buttons & SCE_CTRL_CROSS)    buttons |= VITA_BTN_A;
    if (pad->buttons & SCE_CTRL_CIRCLE)   buttons |= VITA_BTN_B;
    if (pad->buttons & SCE_CTRL_SELECT)   buttons |= VITA_BTN_SELECT;
    if (pad->buttons & SCE_CTRL_START)    buttons |= VITA_BTN_START;
    if (pad->buttons & SCE_CTRL_SQUARE)   buttons |= VITA_BTN_SQUARE;
    if (pad->buttons & SCE_CTRL_TRIANGLE) buttons |= VITA_BTN_TRIANGLE;
    if (pad->buttons & SCE_CTRL_LTRIGGER) buttons |= VITA_BTN_L;
    if (pad->buttons & SCE_CTRL_RTRIGGER) buttons |= VITA_BTN_R;

    // Map D-pad and left analog stick
    if ((pad->buttons & SCE_CTRL_UP)    || pad->ly < STICK_DEADZONE_LOW)  buttons |= VITA_BTN_UP;
    if ((pad->buttons & SCE_CTRL_DOWN)  || pad->ly > STICK_DEADZONE_HIGH) buttons |= VITA_BTN_DOWN;
    if ((pad->buttons & SCE_CTRL_LEFT)  || pad->lx < STICK_DEADZONE_LOW)  buttons |= VITA_BTN_LEFT;
    if ((pad->buttons & SCE_CTRL_RIGHT) || pad->lx > STICK_DEADZONE_HIGH) buttons |= VITA_BTN_RIGHT;

    if (pad->ry < STICK_DEADZONE_LOW)  buttons |= VITA_BTN_RS_UP;
    if (pad->ry > STICK_DEADZONE_HIGH) buttons |= VITA_BTN_RS_DOWN;
    if (pad->rx < STICK_DEADZONE_LOW)  buttons |= VITA_BTN_RS_LEFT;
    if (pad->rx > STICK_DEADZONE_HIGH) buttons |= VITA_BTN_RS_RIGHT;

    // Hardware safety: a genuine GBA d-pad never reports opposing directions simultaneously
    if ((buttons & (VITA_BTN_UP | VITA_BTN_DOWN)) == (VITA_BTN_UP | VITA_BTN_DOWN))
    {
        buttons &= ~(VITA_BTN_UP | VITA_BTN_DOWN);
    }
    if ((buttons & (VITA_BTN_LEFT | VITA_BTN_RIGHT)) == (VITA_BTN_LEFT | VITA_BTN_RIGHT))
    {
        buttons &= ~(VITA_BTN_LEFT | VITA_BTN_RIGHT);
    }

    return buttons;
}

void VitaInput_Poll(VitaInputState *const outState)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));

    sProducerState.buttonsPressed = 0U;
    sProducerState.buttonsReleased = 0U;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0)
    {
        const uint32_t currentButtons = DecodeButtons(&pad);
        uint32_t physicalDpad = 0U;
        if (pad.buttons & SCE_CTRL_UP)    physicalDpad |= VITA_BTN_UP;
        if (pad.buttons & SCE_CTRL_DOWN)  physicalDpad |= VITA_BTN_DOWN;
        if (pad.buttons & SCE_CTRL_LEFT)  physicalDpad |= VITA_BTN_LEFT;
        if (pad.buttons & SCE_CTRL_RIGHT) physicalDpad |= VITA_BTN_RIGHT;
        if ((physicalDpad & (VITA_BTN_UP | VITA_BTN_DOWN)) == (VITA_BTN_UP | VITA_BTN_DOWN))
            physicalDpad &= ~(VITA_BTN_UP | VITA_BTN_DOWN);
        if ((physicalDpad & (VITA_BTN_LEFT | VITA_BTN_RIGHT)) == (VITA_BTN_LEFT | VITA_BTN_RIGHT))
            physicalDpad &= ~(VITA_BTN_LEFT | VITA_BTN_RIGHT);

        sProducerState.buttonsHeld     = currentButtons;
        sProducerState.buttonsPressed  = currentButtons & ~sPreviousButtons;
        sProducerState.buttonsReleased = ~currentButtons & sPreviousButtons;
        sProducerState.dpadHeld        = physicalDpad;
        sPreviousButtons               = currentButtons;

        sProducerState.leftStickX  = pad.lx;
        sProducerState.leftStickY  = pad.ly;
        sProducerState.rightStickX = pad.rx;
        sProducerState.rightStickY = pad.ry;
    }

    // Poll front touch screen
    SceTouchData touch;
    memset(&touch, 0, sizeof(touch));
    sProducerState.touchActive = false;
    sProducerState.touchGbaX = -1;
    sProducerState.touchGbaY = -1;

    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0 && touch.reportNum > 0)
    {
        int32_t rawX = (int32_t)((float)touch.report[0].x * (SCREEN_WIDTH_F / TOUCH_PANEL_MAX_X));
        int32_t rawY = (int32_t)((float)touch.report[0].y * (SCREEN_HEIGHT_F / TOUCH_PANEL_MAX_Y));
        if (rawX < 0) rawX = 0; else if (rawX > 959) rawX = 959;
        if (rawY < 0) rawY = 0; else if (rawY > 543) rawY = 543;
        sProducerState.touchActive = true;
        sProducerState.touchScreenX = rawX;
        sProducerState.touchScreenY = rawY;

        // Map to GBA coords if inside the active viewport
        const int32_t sx = sProducerState.touchScreenX;
        const int32_t sy = sProducerState.touchScreenY;

        if (sx >= GBA_VIEWPORT_OFFSET_X && sx < (GBA_VIEWPORT_OFFSET_X + GBA_VIEWPORT_WIDTH) &&
            sy >= 0 && sy < GBA_VIEWPORT_HEIGHT)
        {
            sProducerState.touchGbaX = (sx - GBA_VIEWPORT_OFFSET_X) * GBA_NATIVE_WIDTH / GBA_VIEWPORT_WIDTH;
            sProducerState.touchGbaY = sy * GBA_NATIVE_HEIGHT / GBA_VIEWPORT_HEIGHT;
        }
    }

    __atomic_fetch_add(&sInputSeq, 1U, __ATOMIC_ACQ_REL);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    sPublishedState = sProducerState;
    __atomic_fetch_add(&sInputSeq, 1U, __ATOMIC_RELEASE);

    if (outState != NULL)
    {
        *outState = sProducerState;
    }
}

void VitaInput_GetSnapshot(VitaInputState *const outState)
{
    VitaInputState copy;

    if (outState == NULL)
    {
        return;
    }

    for (;;)
    {
        const uint32_t seq1 = __atomic_load_n(&sInputSeq, __ATOMIC_ACQUIRE);
        if ((seq1 & 1U) != 0U)
        {
            continue;
        }
        copy = sPublishedState;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t seq2 = __atomic_load_n(&sInputSeq, __ATOMIC_ACQUIRE);
        if (seq1 == seq2)
        {
            *outState = copy;
            return;
        }
    }
}

uint32_t VitaInput_GetButtonsHeld(void)
{
    VitaInputState snap;
    VitaInput_GetSnapshot(&snap);
    return snap.buttonsHeld;
}

bool VitaInput_ReadTouchScreen(int32_t *const outX, int32_t *const outY)
{
    VitaInputState snap;
    VitaInput_GetSnapshot(&snap);
    if (!snap.touchActive)
    {
        return false;
    }

    if (outX != NULL) *outX = snap.touchScreenX;
    if (outY != NULL) *outY = snap.touchScreenY;
    return true;
}

bool VitaInput_ReadTouchGBA(int32_t *const outGbaX, int32_t *const outGbaY)
{
    VitaInputState snap;
    VitaInput_GetSnapshot(&snap);
    if (!snap.touchActive || snap.touchGbaX < 0 || snap.touchGbaY < 0)
    {
        return false;
    }

    if (outGbaX != NULL) *outGbaX = snap.touchGbaX;
    if (outGbaY != NULL) *outGbaY = snap.touchGbaY;
    return true;
}
