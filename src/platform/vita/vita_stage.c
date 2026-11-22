#include <stddef.h>
#include "global.h"
#include "main.h"
#include "platform/game_frame.h"
#include "platform/vita/vita_stage.h"

static void (*sStageCallback)(void) = NULL;

void VitaStage_SetVBlankCallback(void (*callback)(void))
{
    sStageCallback = callback;
    SetVBlankCallback(callback);
}

void VitaStage_CaptureFrame(bool8 *const stageActive)
{
    if (gMain.vblankCallback != sStageCallback)
        sStageCallback = NULL;
    *stageActive = (sStageCallback != NULL && gMain.vblankCallback == sStageCallback) ? TRUE : FALSE;
}

bool VitaStage_IsStageActive(void)
{
    return GameFrame_Get()->overlay.stageActive != FALSE;
}

void VitaStage_OnReset(void)
{
    sStageCallback = NULL;
}
