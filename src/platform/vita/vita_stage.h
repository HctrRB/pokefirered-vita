#ifndef GUARD_VITA_STAGE_H
#define GUARD_VITA_STAGE_H

#include <stdbool.h>
#include "global.h"

void VitaStage_SetVBlankCallback(void (*callback)(void));
void VitaStage_CaptureFrame(bool8 *stageActive);
bool VitaStage_IsStageActive(void);
void VitaStage_OnReset(void);

#endif // GUARD_VITA_STAGE_H
