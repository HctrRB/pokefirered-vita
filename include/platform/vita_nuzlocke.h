#ifndef GUARD_PLATFORM_VITA_NUZLOCKE_H
#define GUARD_PLATFORM_VITA_NUZLOCKE_H

#include <stdbool.h>
#include <stdint.h>

/* Fits all 108 Kanto + Sevii Islands mapsecs with headroom. */
#define VITA_NUZ_MAX_RECORDS 128U

enum
{
    VITA_NUZ_ALIVE = 0,
    VITA_NUZ_DEAD = 1,
};

typedef struct
{
    uint16_t mapSec;
    uint16_t species;
    uint32_t personality;
    uint8_t level;
    uint8_t status;
} VitaNuzRecord;

void VitaNuzlocke_Init(void);
void VitaNuzlocke_Save(void);
void VitaNuzlocke_Reset(void);

uint8_t VitaNuzlocke_GetBadgesCount(void);
uint8_t VitaNuzlocke_GetLevelCap(void);
const char *VitaNuzlocke_GetNextBossName(void);

bool VitaNuzlocke_CanCatch(uint16_t mapSec);
void VitaNuzlocke_RecordCatch(uint16_t mapSec, uint16_t species, uint8_t level, uint32_t personality);
void VitaNuzlocke_MarkDead(uint32_t personality, uint16_t species);

uint32_t VitaNuzlocke_GetRecordCount(void);
const VitaNuzRecord *VitaNuzlocke_GetRecord(uint32_t index);

#endif // GUARD_PLATFORM_VITA_NUZLOCKE_H
