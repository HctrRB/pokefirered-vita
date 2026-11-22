#ifndef GUARD_PLATFORM_VITA_EXTRAS_H
#define GUARD_PLATFORM_VITA_EXTRAS_H

/*
 * vita_extras.h - unified persistent settings and FireRed gameplay/3D/cheat extras.
 *
 * Read access via VitaExtras_GetFast(id) is a lock-free atomic read safe on Core 0/1/2.
 * Table queries (gVitaExtras, VitaExtras_Step) and cheat callbacks belong to Core 1 only.
 */

#include <stdbool.h>
#include <stdint.h>

enum
{
    VITA_TAB_GENERAL = 0,
    VITA_TAB_GRAPHICS,
    VITA_TAB_WORLD,
    VITA_TAB_BATTLE,
    VITA_TAB_MODES,
    VITA_TAB_CHEATS,
    VITA_TAB_COUNT
};

typedef struct
{
    uint8_t tab;                  // VITA_TAB_*
    const char *group;            // Section group label
    const char *name;             // Option display name
    const char *key;              // Key in ux0:data/FRVITA/settings.txt
    uint8_t count;                // Cyclic option count (0: executable action)
    uint8_t fallback;             // Default index when absent
    const char *const *values;    // Value labels (NULL uses custom text callback)
    void (*act)(void);            // Invoked on value change or action activation
    void (*step)(int direction);  // Custom cyclic stepper
    const uint8_t *(*text)(void); // Dynamic value label in GBA string encoding
} VitaExtra;

extern const VitaExtra gVitaExtras[];
extern const unsigned gVitaExtraCount;
extern const char *const gVitaExtrasOffOn[2];

void VitaExtras_Init(void);
void VitaExtras_Save(void);

typedef enum
{
    FAST_EXTRA_DAY_NIGHT = 0,
    FAST_EXTRA_TIMED_ENCOUNTERS,
    FAST_EXTRA_CLOCK_SPEED,
    FAST_EXTRA_RUN_INDOORS,
    FAST_EXTRA_MOVE_SPLIT,
    FAST_EXTRA_FOLLOWER,
    FAST_EXTRA_VOXEL_3D,
    FAST_EXTRA_INFINITE_TMS,
    FAST_EXTRA_REPEL_PROMPT,
    FAST_EXTRA_IVS_EVS,
    FAST_EXTRA_EXP_SHARE,
    FAST_EXTRA_EXP_CATCH,
    FAST_EXTRA_AUTO_RUN,
    FAST_EXTRA_FIELD_HMS,
    FAST_EXTRA_VOXEL_BATTLE,
    FAST_EXTRA_BATTLE_SPEED,
    FAST_EXTRA_FAST_EGGS,
    FAST_EXTRA_NUZLOCKE,
    FAST_EXTRA_LEVEL_CAP,
    FAST_EXTRA_SHINY_ODDS,
    FAST_EXTRA_ALWAYS_CATCH,
    FAST_EXTRA_TRADE_EVO,
    FAST_EXTRA_VOXEL_FPS,       // 0 = 30 FPS, 1 = 60 FPS
    FAST_EXTRA_VOXEL_BLUR,
    FAST_EXTRA_VOXEL_BLOOM,     // 0 = OFF, 1..10 = bright-pass world bloom
    FAST_EXTRA_VOXEL_SKY_BLOOM, // 0 = OFF, 5 = 1.0x, 10 = 2.0x directional sky bloom
    FAST_EXTRA_VOXEL_SHAFTS,    // 0 = OFF, 5 = 1.0x, 10 = 2.0x procedural light shafts
    FAST_EXTRA_UI_THEME,        // UI_THEME_* (default 0 = GBA)
    FAST_EXTRA_VISIBLE_WILD,    // 0 = OFF, 1..3 visible wild Pokemon density
    FAST_EXTRA_ENCOUNTER_RATE,  // 0 = NORMAL, 1 = OFF, 2 = 1/4, 3 = 1/2, 4 = 2X
    FAST_EXTRA_VOXEL_HD2D,      // VOXEL_HD2D_*: 0 = OFF, 1 = LOW, 2 = MED, 3 = HIGH
    FAST_EXTRA_VOXEL_FOV,       // camera pitch preset
    FAST_EXTRA_VOLUME,          // 0..10 master audio volume
    FAST_EXTRA_COUNT
} FastExtraId;

enum
{
    VOXEL_HD2D_OFF = 0,
    VOXEL_HD2D_LOW,
    VOXEL_HD2D_MED,
    VOXEL_HD2D_HIGH,
    VOXEL_HD2D_COUNT
};

int32_t VitaExtras_GetFast(FastExtraId id);
int32_t VitaExtras_Get(const char *key);
void VitaExtras_Set(const char *key, int32_t value);
void VitaExtras_Step(const VitaExtra *extra, int direction);
uint8_t VitaExtras_ValueIndex(const VitaExtra *extra);
unsigned VitaExtras_GetTabExtraCount(uint8_t tab);
const VitaExtra *VitaExtras_GetTabExtra(uint8_t tab, unsigned index);

#endif // GUARD_PLATFORM_VITA_EXTRAS_H
