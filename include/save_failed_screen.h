#ifndef GUARD_SAVE_FAILED_SCREEN_H
#define GUARD_SAVE_FAILED_SCREEN_H

extern void DoSaveFailedScreen(u8 saveType); // save_failed_screen
void SetNotInSaveFailedScreen(void);
bool32 RunSaveFailedScreen(void);
bool8 SaveFailed_IsActiveForUi(void);

#endif //GUARD_SAVE_FAILED_SCREEN_H
