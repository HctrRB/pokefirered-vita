#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include <stdbool.h>
#include "global.h"

// FireRed has no real-time clock (no siirtc.h); the RTC entry points of the
// platform layer keep their signatures over an opaque struct.
struct SiiRtcInfo;

bool Platform_StoreSaveFile(void);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
/* Interleaved stereo in fixed point, 1 << 23 to full scale; the count is in bytes. */
void Platform_QueueAudio(s32 *audioBuffer, s32 samplesPerFrame);
u16 Platform_GetKeyInput(void);

enum PlatformSetting
{
    PLATFORM_SETTING_FULLSCREEN,
    PLATFORM_SETTING_WINDOW_SCALE,
    PLATFORM_SETTING_INTEGER_SCALE,
    PLATFORM_SETTING_VSYNC,
    PLATFORM_SETTING_BORDER,
    PLATFORM_SETTING_VOLUME,
    PLATFORM_SETTING_WIDESCREEN,
    PLATFORM_SETTING_FAST_FORWARD_SPEED,  // 1-4 (game speed multiplier)
    PLATFORM_SETTING_FAST_FORWARD_TOGGLE, // 0 = while held, 1 = press toggles
    PLATFORM_SETTING_SAVE_FILE,           // 1-3 (saves/firered.sav, _2.sav, _3.sav)
    PLATFORM_SETTING_BUTTON_FIRST,        // physical VITA_BTN_* bit index (0..15) or VITA_BTN_UNBOUND
    PLATFORM_SETTING_BUTTON_LAST = PLATFORM_SETTING_BUTTON_FIRST + 7,
    PLATFORM_SETTING_FILTER,
    PLATFORM_SETTING_OVERLAY,
    PLATFORM_SETTING_AUTO_RUN,
    PLATFORM_SETTING_HM_SHORTCUTS,
    PLATFORM_SETTING_STATS_INFO,
    PLATFORM_SETTING_EXP_SHARE,
    PLATFORM_SETTING_EXP_MULTIPLIER,
    PLATFORM_SETTING_VOXEL_3D,
    PLATFORM_SETTING_UI_SCALE,
    PLATFORM_SETTING_VOXEL_RES,
    PLATFORM_SETTING_VOXEL_FPS,
    PLATFORM_SETTING_COUNT,
};

u8 Platform_GetSetting(enum PlatformSetting setting);
void Platform_SetSetting(enum PlatformSetting setting, u8 value);

void Platform_GetStatus(struct SiiRtcInfo *rtc);
void Platform_SetStatus(struct SiiRtcInfo *rtc);
void Platform_GetDateTime(struct SiiRtcInfo *rtc);
void Platform_SetDateTime(struct SiiRtcInfo *rtc);
void Platform_GetTime(struct SiiRtcInfo *rtc);
void Platform_SetTime(struct SiiRtcInfo *rtc);
void Platform_SetAlarm(u8 *alarmData);

#endif