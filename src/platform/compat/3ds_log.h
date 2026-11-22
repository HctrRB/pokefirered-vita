/* Origin: Pokemon FireRed 3DS, commit 7e957ef, 3ds_port/include/3ds_log.h: the interface its UI
 * expects; src/platform/vita/fr_vita_ui.c implements it on the Vita. */
#ifndef CTR_LOG_H
#define CTR_LOG_H
#include <stdbool.h>

typedef enum
{
    CTR_LOG_BOOT, CTR_LOG_VIDEO, CTR_LOG_INPUT, CTR_LOG_FS,
    CTR_LOG_AUDIO, CTR_LOG_GAME, CTR_LOG_ERROR
} CtrLogCategory;

void CtrLog_Init(void);
void CtrLog_Close(void);
/* The last drain of the log to the card: how long it took and how long ago it
 * ended, in ms (0 and a large age before the first). */
void CtrLog_LastDrain(float *ms, float *agoMs);
void CtrLog_SetOverlay(bool enabled);
void CtrLog_DrawOverlay(const char *text);
void CtrLog_ShowFatal(const char *reason);
/* Whether there is a log to write to (debug.txt on the SD card). */
bool CtrLog_Enabled(void);
void CtrLog_Write(CtrLogCategory category, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
