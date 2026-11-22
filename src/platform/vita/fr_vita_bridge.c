/*
 * fr_vita_bridge.c - the Vita side of the port bridge (PORT_BRIDGE hooks of the patched
 * pokefirered tree, declared in src/platform/compat/port_platform.h).
 *
 * The Vita build links every asset into the executable (INCBIN through preproc), so the 0005
 * asset/script/profiler hooks are static inline identity helpers in port_platform.h / port_prof.h.
 * Saves live in FLASH_BASE, updated in RAM by Port_WriteFlash and flushed atomically to
 * ux0:data/FRVITA/saves/firered.sav (via firered.sav.tmp + rename) by Platform_StoreSaveFile at
 * save boundaries. Stage 1 3DS touch overlay has been retired in favour of the native
 * 960x544 Stage 2 UI (src/platform/ui/).
 *
 * Thread: the game thread (Core 1), except the two FrVitaBridge_* functions, which main()
 * calls before the game thread starts.
 *
 * Origin: written for this port; the API is the one of Pokemon FireRed 3DS's bridge.
 * Licence: MIT.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <psp2/io/fcntl.h>

#include "global.h"
#include "gba/flash_internal.h"
#include "main.h"
#include "platform.h"
#include "port_platform.h"
#include "port_log.h"
#include "port_prof.h"
#include "3ds_extras.h"
#include "3ds_log.h"
#include "3ds_platform.h"

#include "vita_input.h"
#include "vita_platform.h"
#include "platform/vita_controls.h"
#include "platform/vita_extras.h"

static void BuildSaveAuxPath(char *out, size_t outSize, const char *savePath, const char *suffix)
{
    snprintf(out, outSize, "%s%s", savePath, suffix);
}

/* ------------------------------------------------------------------------------------------ */
/* Emulated hardware                                                                            */
/* ------------------------------------------------------------------------------------------ */

/* src/crt0.s's interrupt dispatcher, which main.c copies to IntrMain_Buffer and nothing runs
 * here (VBlankIntrWait calls the handlers itself): zeroes of the copy's size. */
const u32 intr_main[0x200];

/* src/rom_header.s, not assembled: the cartridge identity a link partner would compare. */
const char RomHeaderGameCode[4] = {'B', 'P', 'R', 'E'};
const char RomHeaderSoftwareVersion = 1;

void FrVitaBridge_ResetEmulatedHardware(void)
{
    memset(REG_BASE, 0, 0x400);
    memset(PLTT, 0, PLTT_SIZE);
    memset(gPlttExt, 0, sizeof(gPlttExt));
    memset(VRAM_, 0, VRAM_SIZE);
    memset(OAM, 0, OAM_SIZE);
    memset(gOamExt, 0, sizeof(gOamExt));
    /* The BIOS's reset leaves the screen in forced blank. The game relies on it: SetGpuReg writes
     * at once only in VBlank or forced blank, and otherwise keeps one buffered value per
     * register until the next VBlank. Without it, AgbMain's DISPSTAT write for the line-150
     * VCount interrupt (which drives the sound mixer) is overwritten in the buffer by
     * EnableInterrupts' own DISPSTAT write before it lands, and there is no sound. */
    REG_DISPCNT = DISPCNT_FORCED_BLANK;
    /* The BIOS leaves the affine backgrounds at the identity, and the game relies on it. */
    REG_BG2PA = 0x100;
    REG_BG2PD = 0x100;
    REG_BG3PA = 0x100;
    REG_BG3PD = 0x100;
    REG_IE = 0;
    REG_IF = 0;
    REG_DISPSTAT = 0;
    REG_IME = 0;
    REG_KEYINPUT = KEYS_MASK;
    INTR_CHECK = 0;
    INTR_VECTOR = NULL;
}

/* ------------------------------------------------------------------------------------------ */
/* Save storage                                                                                 */
/* ------------------------------------------------------------------------------------------ */

static bool sSaveAvailable;

void FrVitaBridge_SaveInit(void)
{
    Port_SaveInit();
}

void Port_SaveInit(void)
{
    const char *const savePath = VitaControls_GetSavePath();
    char tmpPath[144];
    char corruptPath[144];
    FILE *file;
    long size = -1;
    bool loaded;

    BuildSaveAuxPath(tmpPath, sizeof(tmpPath), savePath, ".tmp");
    BuildSaveAuxPath(corruptPath, sizeof(corruptPath), savePath, ".corrupt");

    memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
    file = fopen(savePath, "rb");
    if (file == NULL)
    {
        const int saveErr = errno;

        /* Recover completed tmp save if power loss interrupted rename(tmp, sav). */
        if (saveErr == ENOENT)
        {
            FILE *tmp = fopen(tmpPath, "rb");
            if (tmp != NULL)
            {
                long tmpSize = -1;
                if (fseek(tmp, 0, SEEK_END) == 0)
                    tmpSize = ftell(tmp);
                fclose(tmp);
                if (tmpSize == (long)sizeof(FLASH_BASE) && rename(tmpPath, savePath) == 0)
                {
                    VITA_LOG("[SAVE] recovered orphaned tmp save\n");
                    file = fopen(savePath, "rb");
                }
            }
        }
        if (file == NULL)
        {
            /* Created by the first save, not by starting a new game. */
            sSaveAvailable = (saveErr == ENOENT);
            VITA_LOG("[SAVE] %s: %s\n", savePath, sSaveAvailable ? "will be created" : "unavailable");
            return;
        }
    }
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    loaded = size >= 0 && (unsigned long)size <= sizeof(FLASH_BASE) && fseek(file, 0, SEEK_SET) == 0
          && fread(FLASH_BASE, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!loaded)
    {
        memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
        remove(corruptPath);
        rename(savePath, corruptPath);
        sSaveAvailable = true;
        VITA_LOG("[SAVE] read failed: %s (size %ld); backed up and unlocked blank save\n", savePath, size);
        return;
    }
    sSaveAvailable = true;
    VITA_LOG("[SAVE] loaded %s (%ld bytes)\n", savePath, size);
}

bool Port_SaveIsAvailable(void)
{
    return sSaveAvailable;
}

u16 Port_WriteFlash(u32 offset, const void *data, u32 size)
{
    if (!sSaveAvailable || data == NULL || offset > sizeof(FLASH_BASE) || size > sizeof(FLASH_BASE) - offset)
        return 1;
    memmove(FLASH_BASE + offset, data, size);
    return 0;
}

bool Platform_StoreSaveFile(void)
{
    const char *const savePath = VitaControls_GetSavePath();
    char tmpPath[144];
    FILE *file;
    bool ok;

    if (!sSaveAvailable)
        return false;
    BuildSaveAuxPath(tmpPath, sizeof(tmpPath), savePath, ".tmp");
    file = fopen(tmpPath, "wb");
    if (file == NULL)
    {
        VITA_LOG("[SAVE] tmp open failed: %s errno %d\n", tmpPath, errno);
        return false;
    }
    ok = fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), file) == sizeof(FLASH_BASE)
      && fflush(file) == 0;
    if (ok)
        fsync(fileno(file));
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
    {
        remove(tmpPath);
        VITA_LOG("[SAVE] tmp write failed: errno %d\n", errno);
        return false;
    }
    /* Commit .tmp sectors to FAT/exFAT before unlinking the existing save file. */
    sceIoSync("ux0:", 0);
    if (rename(tmpPath, savePath) != 0)
    {
        /* Vita sceIoRename fails when destination exists; replace after tmp flush succeeds. */
        remove(savePath);
        if (rename(tmpPath, savePath) != 0)
        {
            VITA_LOG("[SAVE] rename failed: errno %d\n", errno);
            return false;
        }
    }
    sceIoSync("ux0:", 0);
    return true;
}

static u32 CompareSaveBytes(const u8 *src, const u8 *target, u32 size)
{
    for (u32 i = 0; i < size; i++)
    {
        if (src[i] != target[i])
            return (u32)(uintptr_t)(target + i);
    }
    return 0;
}

/* The GBA ran its comparison loop from IWRAM for the cartridge bus; here it is plain RAM. */
u32 VerifyFlashSectorNBytes(u16 sectorNum, u8 *src, u32 n)
{
    u32 base;

    if (gFlash == NULL || sectorNum >= gFlash->sector.count || n == 0 || n > gFlash->sector.size)
        return 1;
    base = (u32)sectorNum << gFlash->sector.shift;
    if (base >= sizeof(FLASH_BASE) || n > sizeof(FLASH_BASE) - base)
        return 1;
    return CompareSaveBytes(src, FLASH_BASE + base, n);
}

u32 VerifyFlashSector(u16 sectorNum, u8 *src)
{
    return VerifyFlashSectorNBytes(sectorNum, src, gFlash != NULL ? gFlash->sector.size : 0);
}

/* ------------------------------------------------------------------------------------------ */
/* Resources: identity/no-op lookups are static inline in port_platform.h                     */
/* ------------------------------------------------------------------------------------------ */

/* The executable starts with .init (_init) and its initialised data ends at __bss_start; every
 * buffer the game fills at run time is in .bss (its heap, the decompression buffer) or in the
 * newlib heap above the executable. */
extern void _init(void);
extern char __bss_start[];

bool Port_IsLinkedData(const void *ptr)
{
    const uintptr_t address = (uintptr_t)ptr;

    return address >= (uintptr_t)&_init && address < (uintptr_t)__bss_start;
}

/* ------------------------------------------------------------------------------------------ */
/* Logging, debug channels and profiling                                                        */
/* ------------------------------------------------------------------------------------------ */

unsigned char g_PortLogActive = 1;

void Port_Log_Printf(const char *fmt, ...)
{
#ifdef VITA_LOGGING
    char line[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    VITA_LOG("%s\n", line);
#else
    (void)fmt;
#endif
}

void Port_Log_Init(void) {}
void Port_Log_SetEnabled(unsigned char enabled) { g_PortLogActive = enabled; }
unsigned char Port_Log_IsEnabled(void) { return g_PortLogActive; }
void Port_Log_VBlankProtect(void) {}

volatile u16 gPortDbgBirchTag, gPortDbgBirchA, gPortDbgBirchB, gPortDbgBirchC, gPortDbgBirchD;
volatile u16 gPortDbgDmaTag, gPortDbgDmaA, gPortDbgDmaB, gPortDbgDmaC, gPortDbgDmaD;
volatile u16 gPortDbgDma2Tag, gPortDbgDma2A, gPortDbgDma2B, gPortDbgDma2C, gPortDbgDma2D;
volatile u16 gPortDbgAudioTag, gPortDbgAudioA, gPortDbgAudioB, gPortDbgAudioC, gPortDbgAudioD;
volatile u16 gPortDbgAssetTag, gPortDbgAssetA, gPortDbgAssetB, gPortDbgAssetC;

void Port_SetBirchDebugEx(u16 tag, u16 a, u16 b, u16 c, u16 d)
{
    gPortDbgBirchTag = tag; gPortDbgBirchA = a; gPortDbgBirchB = b; gPortDbgBirchC = c; gPortDbgBirchD = d;
}
void Port_SetBirchDebug(u16 tag, u16 a, u16 b, u16 c) { Port_SetBirchDebugEx(tag, a, b, c, 0); }
void Port_SetDmaDebugEx(u16 tag, u16 a, u16 b, u16 c, u16 d)
{
    gPortDbgDmaTag = tag; gPortDbgDmaA = a; gPortDbgDmaB = b; gPortDbgDmaC = c; gPortDbgDmaD = d;
}
void Port_SetDmaDebug(u16 tag, u16 a, u16 b, u16 c) { Port_SetDmaDebugEx(tag, a, b, c, 0); }
void Port_SetDmaDebug2(u16 tag, u16 a, u16 b, u16 c, u16 d)
{
    gPortDbgDma2Tag = tag; gPortDbgDma2A = a; gPortDbgDma2B = b; gPortDbgDma2C = c; gPortDbgDma2D = d;
}
void Port_SetAudioDebugEx(u16 tag, u16 a, u16 b, u16 c, u16 d)
{
    gPortDbgAudioTag = tag; gPortDbgAudioA = a; gPortDbgAudioB = b; gPortDbgAudioC = c; gPortDbgAudioD = d;
}
void Port_SetAssetDebugEx(u16 tag, u16 a, u16 b, u16 c, u16 d)
{
    (void)d;
    gPortDbgAssetTag = tag; gPortDbgAssetA = a; gPortDbgAssetB = b; gPortDbgAssetC = c;
}
void Port_SetAssetDebug(u16 tag, u16 a, u16 b, u16 c) { Port_SetAssetDebugEx(tag, a, b, c, 0); }

/* ------------------------------------------------------------------------------------------ */
/* Frame counters, scene tracking, logging and retired Stage 1 3DS UI stubs                    */
/* ------------------------------------------------------------------------------------------ */

#define FRAME_NEVER 0xFFFFFFF0u
#define FREE_MOVE_DEADZONE 28

static uint32_t sFrames;
static uint32_t sResets;
static uint32_t sSceneFrame = FRAME_NEVER;

void CtrLog_Write(CtrLogCategory category, const char *format, ...)
{
#ifdef VITA_LOGGING
    char line[256];
    va_list args;

    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    VITA_LOG("[UI %d] %s\n", (int)category, line);
#else
    (void)category;
    (void)format;
#endif
}

uint32_t CtrGame_Frames(void)
{
    return sFrames;
}

uint32_t CtrGame_Resets(void)
{
    return sResets;
}

void FrVitaBridge_FrameDone(void)
{
    sFrames++;
}

void FrVitaBridge_NoteReset(void)
{
    sFrames = 0U;
    sResets++;
    sSceneFrame = FRAME_NEVER;
}

void Port_SceneOnTop(void)
{
    sSceneFrame = sFrames;
}

bool Port_SceneShown(void)
{
    return (sFrames - sSceneFrame) <= 1U;
}

bool8 CtrPlayer_Follower(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_FOLLOWER) != 0;
}

bool8 CtrPlayer_RunAlways(void)
{
    return CtrSettings_RunAlways() != 0;
}

bool8 CtrPlayer_FreeMove(void)
{
    return CtrSettings_GetInt("free_move", 0) != 0;
}

bool8 CtrPlayer_FreeStick(s16 *x, s16 *y)
{
    if (!CtrPlayer_FreeMove())
        return FALSE;

    VitaInputState snap;
    VitaInput_GetSnapshot(&snap);
    const int dx = (int)snap.leftStickX - 128;
    const int dy = 128 - (int)snap.leftStickY;
    if (dx * dx + dy * dy < FREE_MOVE_DEADZONE * FREE_MOVE_DEADZONE)
        return FALSE;

    if (x != NULL)
        *x = (s16)dx;
    if (y != NULL)
        *y = (s16)dy;
    return TRUE;
}

void Port_MenuOnBottom(void) {}

void Port_NativePage(u8 page)
{
    (void)page;
}

void Port_RegionMapOnBottom(void) {}

s8 Port_BattleMenu(u8 kind, const void *moveInfo)
{
    (void)kind;
    (void)moveInfo;
    return -1;
}

void CtrExtras_ShowScreen(unsigned page, unsigned screen)
{
    (void)page;
    (void)screen;
}

/* ------------------------------------------------------------------------------------------ */
/* Entry points of GBA-only units that are not built                                            */
/* ------------------------------------------------------------------------------------------ */

u32 JoyBusRead(void) { return 0; }
void JoyBusWrite(void) {}
void CB2_InitBerryFixProgram(void)
{
    VITA_LOG("[STUB] the GBA berry-fix program is not available\n");
}

/* data/multiboot_ereader.s (the program sent to an e-Reader) is not assembled: an empty one. */
const u8 gMultiBootProgram_EReader_Start[1];
const u8 gMultiBootProgram_EReader_End[1];
