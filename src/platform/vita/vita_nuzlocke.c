#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "constants/flags.h"
#include "platform/vita_controls.h"
#include "platform/vita_nuzlocke.h"
#include "platform/vita/vita_platform.h"

#define NUZ_MAGIC_V1 0x4B5A554Eu // "NUZK" (legacy 64-record blob)
#define NUZ_MAGIC    0x325A554Eu // "NUZ2" (128-record blob)
#define NUZ_V1_MAX   64U

typedef struct
{
    uint32_t magic;
    uint32_t count;
    VitaNuzRecord records[NUZ_V1_MAX];
} NuzlockeSaveBlobV1;

typedef struct
{
    uint32_t magic;
    uint32_t count;
    VitaNuzRecord records[VITA_NUZ_MAX_RECORDS];
} NuzlockeSaveBlob;

static NuzlockeSaveBlob sBlob;
static bool sLoaded;

// FireRed Kanto boss level caps matching Gym Leaders' ace levels + Champion ace.
static const uint8_t kKantoLevelCaps[9] = { 14, 21, 24, 29, 43, 43, 47, 50, 63 };
static const char *const kKantoBossNames[9] = {
    "BROCK", "MISTY", "LT. SURGE", "ERIKA",
    "KOGA", "SABRINA", "BLAINE", "GIOVANNI", "CHAMPION"
};

static void BuildNuzPath(char *out, const size_t outSize)
{
    snprintf(out, outSize, "%snuzlocke_slot%u.bin", VITA_DATA_DIR, (unsigned)VitaControls_GetSaveSlot());
}

void VitaNuzlocke_Init(void)
{
    char path[128];
    BuildNuzPath(path, sizeof(path));

    memset(&sBlob, 0, sizeof(sBlob));
    sBlob.magic = NUZ_MAGIC;
    sLoaded = true;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return;

    NuzlockeSaveBlob disk;
    const size_t n = fread(&disk, 1, sizeof(disk), fp);
    fclose(fp);
    if (n == sizeof(disk) && disk.magic == NUZ_MAGIC && disk.count <= VITA_NUZ_MAX_RECORDS)
    {
        sBlob = disk;
    }
    else if (n == sizeof(NuzlockeSaveBlobV1) && disk.magic == NUZ_MAGIC_V1 && disk.count <= NUZ_V1_MAX)
    {
        sBlob.magic = NUZ_MAGIC;
        sBlob.count = disk.count;
        memcpy(sBlob.records, disk.records, disk.count * sizeof(VitaNuzRecord));
    }
}

void VitaNuzlocke_Save(void)
{
    if (!sLoaded)
        return;

    char path[128];
    char tmpPath[144];
    BuildNuzPath(path, sizeof(path));
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);

    FILE *fp = fopen(tmpPath, "wb");
    if (fp == NULL)
        return;

    sBlob.magic = NUZ_MAGIC;
    const size_t written = fwrite(&sBlob, 1, sizeof(sBlob), fp);
    fclose(fp);
    if (written == sizeof(sBlob))
    {
        remove(path);
        rename(tmpPath, path);
    }
    else
    {
        remove(tmpPath);
    }
}

void VitaNuzlocke_Reset(void)
{
    memset(&sBlob, 0, sizeof(sBlob));
    sBlob.magic = NUZ_MAGIC;
    sLoaded = true;
    VitaNuzlocke_Save();
}

uint8_t VitaNuzlocke_GetBadgesCount(void)
{
    uint8_t badges = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            badges++;
    }
    return badges;
}

uint8_t VitaNuzlocke_GetLevelCap(void)
{
    const uint8_t badges = VitaNuzlocke_GetBadgesCount();
    return kKantoLevelCaps[badges < 9 ? badges : 8];
}

const char *VitaNuzlocke_GetNextBossName(void)
{
    const uint8_t badges = VitaNuzlocke_GetBadgesCount();
    return kKantoBossNames[badges < 9 ? badges : 8];
}

bool VitaNuzlocke_CanCatch(const uint16_t mapSec)
{
    if (!sLoaded)
        VitaNuzlocke_Init();

    for (uint32_t i = 0; i < sBlob.count; i++)
    {
        if (sBlob.records[i].mapSec == mapSec)
            return false;
    }
    return true;
}

void VitaNuzlocke_RecordCatch(const uint16_t mapSec, const uint16_t species, const uint8_t level, const uint32_t personality)
{
    if (!sLoaded)
        VitaNuzlocke_Init();
    if (!VitaNuzlocke_CanCatch(mapSec) || sBlob.count >= VITA_NUZ_MAX_RECORDS)
        return;

    VitaNuzRecord *rec = &sBlob.records[sBlob.count++];
    rec->mapSec = mapSec;
    rec->species = species;
    rec->personality = personality;
    rec->level = level;
    rec->status = VITA_NUZ_ALIVE;
    VitaNuzlocke_Save();
}

void VitaNuzlocke_MarkDead(const uint32_t personality, const uint16_t species)
{
    if (!sLoaded)
        VitaNuzlocke_Init();

    for (uint32_t i = 0; i < sBlob.count; i++)
    {
        if (sBlob.records[i].status != VITA_NUZ_ALIVE)
            continue;
        const bool match = (personality != 0U)
            ? (sBlob.records[i].personality == personality)
            : (sBlob.records[i].species == species);
        if (match)
        {
            sBlob.records[i].status = VITA_NUZ_DEAD;
            VitaNuzlocke_Save();
            return;
        }
    }
}

uint32_t VitaNuzlocke_GetRecordCount(void)
{
    if (!sLoaded)
        VitaNuzlocke_Init();
    return sBlob.count;
}

const VitaNuzRecord *VitaNuzlocke_GetRecord(const uint32_t index)
{
    if (!sLoaded)
        VitaNuzlocke_Init();
    if (index >= sBlob.count)
        return NULL;
    return &sBlob.records[index];
}
