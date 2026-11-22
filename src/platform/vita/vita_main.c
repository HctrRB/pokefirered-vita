/*
 * vita_main.c - process entry point and frame loop of the FireRed Vita port.
 *
 * Threads:
 *   - Core 0 (MainThread_Run): vitaGL presentation, driving parallel 288-px GBA scanline
 *     rendering (DrawFrameToTexture) via Core 2 DrawWorkerMain workers and reading GameFrame.
 *   - Core 1 (AgbMain): the game. VBlankIntrWait drives VCount/HBlank/VBlank interrupts,
 *     m4a audio mixing, captures GameFrame, signals Core 0, and paces the loop to 60 Hz.
 *   - Core 2 (DrawWorkerMain x2): parallel scanlines and NEON BGR555 -> RGB565 conversion.
 *
 * Origin: written for this port, after pokeemerald-vita's sdl2.c (MIT). Licence: MIT.
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <vitaGL.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "global.h"
#include "main.h"
#include "platform.h"
#include "cgb_audio.h"
#include "gba/flash_internal.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/game_frame.h"
#include "platform/game_thread.h"
#include "platform/widescreen.h"
#include "platform/vita/vita_stage.h"
#include "port_platform.h"

#include "vita_platform.h"
#include "vita_perf.h"
#include "vita_reset.h"
#include "3ds_platform.h"
#include "platform/vita_daynight.h"
#include "platform/ui.h"
#include "platform/vita_controls.h"
#include "platform/vita_extras.h"
#include "platform/vita_nuzlocke.h"
#ifdef VOXEL_SUPPORTED
#include "platform/voxel/voxel_world.h"
#include "platform/voxel/voxel_battle.h"
#include "platform/voxel/voxel_fly_game.h"
#include "platform/voxel/voxel_renderer.h"
#endif

/* ------------------------------------------------------------------------------------------ */
/* Constants                                                                                    */
/* ------------------------------------------------------------------------------------------ */

#define SCREEN_W 960
#define SCREEN_H 544
/* GBA 3:2 picture at full height: 816x544, centred. */
#define PICTURE_W ((SCREEN_H * DISPLAY_WIDTH) / DISPLAY_HEIGHT)
#define PICTURE_X0 ((SCREEN_W - PICTURE_W) / 2)

/* The mixer runs 701 samples per frame (m4a.c, PORTABLE): 42060 Hz. */
#define GAME_AUDIO_RATE 42060
#define OUTPUT_AUDIO_RATE 48000
#define AUDIO_FULL_SCALE ((float)(1 << 23))
#define AUDIO_MAX_SAMPLES 8192
/* Bytes of 48 kHz float stereo per 60 Hz frame, and the queue depths the pacing aims at. */
#define AUDIO_FRAME_BYTES ((OUTPUT_AUDIO_RATE / 60) * 2 * (int32_t)sizeof(float))
#define AUDIO_TARGET_QUEUE (3 * AUDIO_FRAME_BYTES)
#define AUDIO_MAX_QUEUE (6 * AUDIO_FRAME_BYTES)
#define AUDIO_DC_BLOCK_POLE 0.9995f
#define AUDIO_SKEW_US 500ULL

#define FRAME_WAIT_TIMEOUT_MS 50
#define FAST_FORWARD_SPEED 3.0

/* vitaGL memory: legacy pool, RAM, CDRAM and physically contiguous RAM. */
#define VGL_POOL_BYTES (8 * 1024 * 1024)
#define VGL_RAM_BYTES (64 * 1024 * 1024)
#define VGL_CDRAM_BYTES (88 * 1024 * 1024)
#define VGL_PHYCONT_BYTES (8 * 1024 * 1024)
#define VBLANK_INTR_INDEX 4

/* 3-slot texture ring so glTexSubImage2D never touches a texture active in the 3-deep swapchain. */
#define VITA_GAME_TEXTURE_RING 3

/* ------------------------------------------------------------------------------------------ */
/* State                                                                                        */
/* ------------------------------------------------------------------------------------------ */

static SDL_sem *sFrameReadySem;

static GLuint sGameTextureRing[VITA_GAME_TEXTURE_RING];
static uint8_t sGameTextureUploadIdx;
static uint16_t sFrameImage[FRAME_LINE_WIDTH * DISPLAY_HEIGHT];
static uint16_t sConverted[FRAME_LINE_WIDTH * DISPLAY_HEIGHT];

static SDL_AudioDeviceID sAudioDevice;
static SDL_AudioStream *sAudioStream;
static float sAudioPrevIn[2];
static float sAudioPrevOut[2];

static SDL_atomic_t sQuitRequested;
static SDL_atomic_t sResetRequested;
volatile bool gUiVoxelFrame = false;

void VitaPlatform_RequestReset(void)
{
    SDL_AtomicSet(&sResetRequested, 1);
}

void VitaPlatform_RequestQuit(void)
{
    SDL_AtomicSet(&sQuitRequested, 1);
    VitaPlatform_Exit();
}
/* Two-way handshake so Core 0 is unpinned and idle before Core 1 overwrites gamedata/gamebss. */
static SDL_atomic_t sResetInProgress;
static SDL_atomic_t sCore0PausedForReset;
static jmp_buf sGameResetJump;
static bool sGameResetJumpValid;
static bool sFastForward;
static SDL_atomic_t sSpeedUpActive;
#ifdef VITA_TELEMETRY
static double sCore1LogicStartMs;
#endif

/* bios.c / main.c */
extern void AgbMain(void);
extern IntrFunc gIntrTable[];
/* fr_vita_bridge.c */
void FrVitaBridge_SaveInit(void);
void FrVitaBridge_ResetEmulatedHardware(void);
void FrVitaBridge_FrameDone(void);
void FrVitaBridge_NoteReset(void);

/* ------------------------------------------------------------------------------------------ */
/* Input                                                                                        */
/* ------------------------------------------------------------------------------------------ */

/* Cross A, Circle B, L, R, START, SELECT, d-pad or left stick; Triangle held = fast forward. */
u16 Platform_GetKeyInput(void)
{
    const uint32_t vitaBtns = VitaPlatform_ReadButtons();
    sFastForward = VitaControls_FastForwardPressed(vitaBtns);

    uint16_t keys = 0;
    if (!Ui_ConsumesInput())
        keys = VitaControls_MapButtons(vitaBtns) | Ui_TakeInjectedKeys();

    /* Code that reads the register directly (the e-Reader screen) sees the same keys. */
    REG_KEYINPUT = keys ^ KEYS_MASK;
    return keys;
}

/* ------------------------------------------------------------------------------------------ */
/* Audio                                                                                        */
/* ------------------------------------------------------------------------------------------ */

static void InitAudio(void)
{
    SDL_AudioSpec want;

    SDL_zero(want);
    want.freq = OUTPUT_AUDIO_RATE;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 512;
    cgb_audio_init(GAME_AUDIO_RATE);

    sAudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (sAudioDevice == 0)
    {
        VITA_LOG("[AUDIO] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    sAudioStream = SDL_NewAudioStream(AUDIO_F32, 2, GAME_AUDIO_RATE, AUDIO_F32, 2, OUTPUT_AUDIO_RATE);
    if (sAudioStream == NULL)
    {
        VITA_LOG("[AUDIO] SDL_NewAudioStream failed: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(sAudioDevice);
        sAudioDevice = 0;
        return;
    }
    SDL_PauseAudioDevice(sAudioDevice, 0);
    VITA_LOG("[AUDIO] %d Hz mixer resampled to %d Hz\n", GAME_AUDIO_RATE, OUTPUT_AUDIO_RATE);
}

/* Core 1, from m4aSoundVSync: one frame of interleaved stereo, 1 << 23 to full scale. */
void Platform_QueueAudio(s32 *audioBuffer, s32 byteCount)
{
    static float sSamples[AUDIO_MAX_SAMPLES];
    static float sResampled[AUDIO_MAX_SAMPLES];
    int32_t count = byteCount / (int32_t)sizeof(s32);
    const int32_t vol = VitaExtras_GetFast(FAST_EXTRA_VOLUME);
    const float gain = (float)vol / (10.0f * AUDIO_FULL_SCALE);

    if (sAudioDevice == 0 || sAudioStream == NULL || audioBuffer == NULL || count <= 0)
        return;
    if (SDL_GetQueuedAudioSize(sAudioDevice) > AUDIO_MAX_QUEUE)
    {
        /* Drop the incoming frame rather than clearing already queued audio. */
        return;
    }
    if (count > AUDIO_MAX_SAMPLES)
        count = AUDIO_MAX_SAMPLES;

    for (int32_t i = 0; i < count; i++)
    {
        /* DC-blocking high-pass, like the capacitor on the GBA's output. */
        const int32_t channel = i & 1;
        const float in = (float)audioBuffer[i] * gain;
        const float out = in - sAudioPrevIn[channel] + AUDIO_DC_BLOCK_POLE * sAudioPrevOut[channel];

        sAudioPrevIn[channel] = in;
        sAudioPrevOut[channel] = out;
        sSamples[i] = out;
    }

    if (SDL_AudioStreamPut(sAudioStream, sSamples, count * (int32_t)sizeof(float)) < 0)
        return;
    for (int32_t got; (got = SDL_AudioStreamGet(sAudioStream, sResampled, sizeof(sResampled))) > 0;)
        SDL_QueueAudio(sAudioDevice, sResampled, (Uint32)got);
}

/* ------------------------------------------------------------------------------------------ */
/* Game thread (Core 1)                                                                         */
/* ------------------------------------------------------------------------------------------ */

static void RunVBlank(void)
{
    REG_VCOUNT = DISPLAY_HEIGHT;
    REG_DISPSTAT |= DISPSTAT_VBLANK;
    REG_IF |= INTR_FLAG_VBLANK;
    RunDMAs(DMA_VBLANK);
    if (REG_IME && (REG_IE & INTR_FLAG_VBLANK) && gIntrTable[VBLANK_INTR_INDEX] != NULL)
        gIntrTable[VBLANK_INTR_INDEX]();
    REG_DISPSTAT &= ~DISPSTAT_VBLANK;
    REG_IF &= ~INTR_FLAG_VBLANK;
    REG_VCOUNT = 0;
}

/* Waits for the next 60 Hz tick, skewed by up to 0.5 ms to keep the audio queue near its
 * target (the CPU timer and the audio DAC drift apart). */
static void PaceFrame(void)
{
    static uint64_t sLastTick;
    const uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t target = freq / 60;
    const int32_t speedSetting = CtrSettings_Speed();

    SDL_AtomicSet(&sSpeedUpActive, (sFastForward || speedSetting > 1) ? 1 : 0);
    if (sFastForward)
    {
        const double ffMult = (speedSetting > 1) ? (double)speedSetting : FAST_FORWARD_SPEED;
        target = (uint64_t)((double)target / ffMult);
    }
    else if (speedSetting > 1)
        target /= (uint64_t)speedSetting;   /* the UI's SPEED option */
    else if (sAudioDevice != 0)
    {
        const uint32_t queued = SDL_GetQueuedAudioSize(sAudioDevice);

        if (queued > AUDIO_TARGET_QUEUE + AUDIO_FRAME_BYTES)
            target += (freq * AUDIO_SKEW_US) / 1000000ULL;
        else if (queued + AUDIO_FRAME_BYTES < AUDIO_TARGET_QUEUE)
            target -= (freq * AUDIO_SKEW_US) / 1000000ULL;
    }

    if (sLastTick != 0)
    {
        const uint64_t elapsed = SDL_GetPerformanceCounter() - sLastTick;

        if (elapsed < target)
        {
            const uint32_t delayUs = (uint32_t)(((target - elapsed) * 1000000ULL) / freq);

            if (delayUs > 600)
                VitaPlatform_DelayMicroseconds(delayUs - 300);
            while (SDL_GetPerformanceCounter() - sLastTick < target)
            {
                /* the last fraction of a millisecond, spun for accuracy */
            }
        }
    }
    sLastTick = SDL_GetPerformanceCounter();
}

/* The game's frame boundary: every AgbMain iteration ends here. */
void VBlankIntrWait(void)
{
    const double tVblankStart = VitaPerf_NowMs();
#ifdef VITA_TELEMETRY
    if (sCore1LogicStartMs > 0.0)
        VitaPerf_Core1LogicDone(tVblankStart - sCore1LogicStartMs);
#endif
    GbaEngine_StepVCount();
    RunVBlank();
    GbaEngine_StepHBlankScanlines();
    VitaPerf_Core1VBlankDone(VitaPerf_NowMs() - tVblankStart);
    Ui_Tick();
#ifdef VOXEL_SUPPORTED
    VoxelFlyGame_Tick();
#endif
    GameFrame_CaptureAndPublish();
    VitaPerf_Core1LogicTick();
    if (sFrameReadySem != NULL)
        SDL_SemPost(sFrameReadySem);
    FrVitaBridge_FrameDone();
    {
        const double tWaitStart = VitaPerf_NowMs();
        PaceFrame();
        VitaPerf_Core1WaitDone(VitaPerf_NowMs() - tWaitStart);
    }
#ifdef VITA_TELEMETRY
    sCore1LogicStartMs = VitaPerf_NowMs();
#endif

    if (SDL_AtomicGet(&sQuitRequested))
    {
        for (;;)
            VitaPlatform_DelayMicroseconds(1000000);
    }
    if (SDL_AtomicGet(&sResetRequested))
    {
        SDL_AtomicSet(&sResetRequested, 0);
        SoftReset(RESET_ALL);
    }
}

/* Power cycle: park Core 0, restore boot snapshot, re-read save, restart AgbMain on Core 1. */
void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    if (sGameResetJumpValid)
    {
        SDL_AtomicSet(&sResetInProgress, 1);
        if (sFrameReadySem != NULL)
            SDL_SemPost(sFrameReadySem);
        while (!SDL_AtomicGet(&sQuitRequested) && !SDL_AtomicGet(&sCore0PausedForReset))
            VitaPlatform_DelayMicroseconds(200);

        /* Free heap caches held in gamebss before restoring the boot snapshot. */
        {
            extern void FollowerMon_ReleaseAll(void);
            FollowerMon_ReleaseAll();
        }
        const bool restored = VitaReset_RestoreBootSnapshot();
        if (restored)
        {
            FrVitaBridge_ResetEmulatedHardware();
            FrVitaBridge_SaveInit();
            VitaNuzlocke_Init();
            FrVitaBridge_NoteReset();
            Ui_OnReset();
            VitaStage_OnReset();
#ifdef WIDESCREEN_SUPPORTED
            Widescreen_OnReset();
#endif
            FastDraw_OnReset();
            GameFrame_OnReset();
            VitaDayNight_Reset();
#ifdef VOXEL_SUPPORTED
            VoxelFlyGame_OnReset();
#endif
            cgb_audio_init(GAME_AUDIO_RATE);
            if (sAudioStream != NULL)
                SDL_AudioStreamClear(sAudioStream);
            sAudioPrevIn[0] = sAudioPrevIn[1] = 0.0f;
            sAudioPrevOut[0] = sAudioPrevOut[1] = 0.0f;
            sFastForward = false;
            SDL_AtomicSet(&sSpeedUpActive, 0);
            if (sAudioDevice != 0)
                SDL_ClearQueuedAudio(sAudioDevice);
#ifdef VITA_TELEMETRY
            sCore1LogicStartMs = 0.0;
#endif
        }

        SDL_AtomicSet(&sResetInProgress, 0);
        while (!SDL_AtomicGet(&sQuitRequested) && SDL_AtomicGet(&sCore0PausedForReset))
            VitaPlatform_DelayMicroseconds(100);

        if (restored)
            longjmp(sGameResetJump, 1);
    }
    VITA_LOG("[RESET] no boot snapshot; exiting\n");
    VitaPlatform_Exit();
}

static int32_t GameThread(void *data)
{
    (void)data;
    VitaPlatform_PinCurrentThread("AgbMain", 1);
    GameThread_Register();
    VITA_LOG("[INIT] game thread started\n");
    /* SoftReset comes back here from wherever the game called it. */
    if (setjmp(sGameResetJump) != 0)
        VITA_LOG("[RESET] soft reset: entering AgbMain again\n");
    sGameResetJumpValid = true;
    AgbMain();
    VITA_LOG("[EXIT] AgbMain returned\n");
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Presentation (Core 0 & Core 2 workers)                                                       */
/* ------------------------------------------------------------------------------------------ */

static bool InitVideo(void)
{
    vglSetupGarbageCollector(127, SCE_KERNEL_CPU_MASK_USER_2);
    vglSetupRuntimeShaderCompiler(SHARK_OPT_FAST, GL_TRUE, GL_FALSE, GL_TRUE);
    vglUseLowPrecision(GL_TRUE);
    vglUseVramForUSSE(GL_TRUE);
    vglUseCachedMem(GL_TRUE);
    vglUseTripleBuffering(GL_TRUE);
    /*
     * In VitaSDK's libvitaGL.a, vglInitWithCustomSizes returns `res_fallback`
     * (GL_FALSE when 960x544 fits sceDisplayGetMaximumFrameBufResolution without
     * clamping, GL_TRUE only when resolution fallback occurred). Verify actual
     * CDRAM pool initialization via vglMemTotal(VGL_MEM_VRAM) instead.
     */
    const GLboolean resFallback = vglInitWithCustomSizes(VGL_POOL_BYTES, SCREEN_W, SCREEN_H, VGL_RAM_BYTES,
                                                         VGL_CDRAM_BYTES, VGL_PHYCONT_BYTES, 0,
                                                         SCE_GXM_MULTISAMPLE_NONE);
    (void)resFallback;
    VITA_LOG("[VIDEO] vglInitWithCustomSizes fallback=%d pools KB: vram=%u ram=%u phycont=%u\n", resFallback,
             (unsigned)(vglMemTotal(VGL_MEM_VRAM) / 1024u), (unsigned)(vglMemTotal(VGL_MEM_RAM) / 1024u),
             (unsigned)(vglMemTotal(VGL_MEM_PHYCONT) / 1024u));
    if (vglMemTotal(VGL_MEM_VRAM) == 0)
        return false;

    glGenTextures(VITA_GAME_TEXTURE_RING, sGameTextureRing);
    for (int32_t i = 0; i < VITA_GAME_TEXTURE_RING; i++)
    {
        glBindTexture(GL_TEXTURE_2D, sGameTextureRing[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, FRAME_LINE_WIDTH, DISPLAY_HEIGHT, 0, GL_RGB,
                     GL_UNSIGNED_SHORT_5_6_5, NULL);
    }

    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, SCREEN_W, SCREEN_H, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

#ifdef WIDESCREEN_SUPPORTED
#define STAGE_FADE_ACROSS_GBA_PX 56.0f

static void DrawStageMarginFade(void)
{
    const float marginGbaPx = WIDESCREEN_PICTURE_X0 / WIDESCREEN_SCALE;
    const float outerAlpha = (marginGbaPx >= STAGE_FADE_ACROSS_GBA_PX)
                           ? 1.0f
                           : (marginGbaPx / STAGE_FADE_ACROSS_GBA_PX);
    const float x0 = WIDESCREEN_PICTURE_X0;
    const float x1 = WIDESCREEN_PICTURE_X1;
    const float w = WIDESCREEN_SCREEN_W;
    const float h = WIDESCREEN_SCREEN_H;
    const float verts[16] = {
        0.0f, 0.0f, x0, 0.0f, x0, h, 0.0f, h,
        x1, 0.0f, w, 0.0f, w, h, x1, h,
    };
    const float colors[32] = {
        0.0f, 0.0f, 0.0f, outerAlpha, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,       0.0f, 0.0f, 0.0f, outerAlpha,
        0.0f, 0.0f, 0.0f, 0.0f,       0.0f, 0.0f, 0.0f, outerAlpha,
        0.0f, 0.0f, 0.0f, outerAlpha, 0.0f, 0.0f, 0.0f, 0.0f,
    };

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glColorPointer(4, GL_FLOAT, 0, colors);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDrawArrays(GL_TRIANGLE_FAN, 4, 4);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
}
#endif

static void VDraw(void)
{
    gFastDrawDispcntClear = Ui_BattleDispcntClear();
    DrawFrameToTexture(sFrameImage, sConverted, FRAME_LINE_WIDTH * (int32_t)sizeof(uint16_t));
    sGameTextureUploadIdx = (uint8_t)((sGameTextureUploadIdx + 1U) % VITA_GAME_TEXTURE_RING);
    glBindTexture(GL_TEXTURE_2D, sGameTextureRing[sGameTextureUploadIdx]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FRAME_LINE_WIDTH, DISPLAY_HEIGHT, GL_RGB,
                    GL_UNSIGNED_SHORT_5_6_5, sConverted);
    gFastDrawDispcntClear = 0;
}

static void RenderGameTexture(void)
{
    const GameFrame *const gf = GameFrame_Get();
    float vx0, vx1;
    float vy0 = 0.0f;
    float vy1 = (float)SCREEN_H;
    float u0, u1;

    /* Reset GL state cleanly in case the previous frame took the 3D/HD-2D shader + FBO path. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, SCREEN_W, SCREEN_H, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

#ifdef WIDESCREEN_SUPPORTED
    if (gFastDrawWide.output)
    {
        vx0 = 0.0f;
        vx1 = WIDESCREEN_SCREEN_W;
        u0 = WIDESCREEN_VISIBLE_U0;
        u1 = WIDESCREEN_VISIBLE_U1;
    }
    else
#endif
    {
        vx0 = (float)PICTURE_X0;
        vx1 = (float)(PICTURE_X0 + PICTURE_W);
#ifdef WIDESCREEN_SUPPORTED
        u0 = (float)FRAME_LINE_X0 / (float)FRAME_LINE_WIDTH;
        u1 = (float)(FRAME_LINE_X0 + DISPLAY_WIDTH) / (float)FRAME_LINE_WIDTH;
#else
        u0 = 0.0f;
        u1 = 1.0f;
#endif
    }

    if (gf->overlay.gbaWindow)
    {
        vx0 = (float)UI_SCENE_WIN_X;
        vx1 = (float)(UI_SCENE_WIN_X + UI_SCENE_WIN_W);
        vy0 = (float)UI_SCENE_WIN_Y;
        vy1 = (float)(UI_SCENE_WIN_Y + UI_SCENE_WIN_H);
#ifdef WIDESCREEN_SUPPORTED
        u0 = (float)FRAME_LINE_X0 / (float)FRAME_LINE_WIDTH;
        u1 = (float)(FRAME_LINE_X0 + DISPLAY_WIDTH) / (float)FRAME_LINE_WIDTH;
#else
        u0 = 0.0f;
        u1 = 1.0f;
#endif
    }
    else if (gf->overlay.fieldMoveShowMon)
    {
        vx0 = 0.0f;
        vx1 = (float)SCREEN_W;
#ifdef WIDESCREEN_SUPPORTED
        u0 = (float)FRAME_LINE_X0 / (float)FRAME_LINE_WIDTH;
        u1 = (float)(FRAME_LINE_X0 + DISPLAY_WIDTH) / (float)FRAME_LINE_WIDTH;
#else
        u0 = 0.0f;
        u1 = 1.0f;
#endif
    }

    const float uvs[8] = {u0, 0.0f, u1, 0.0f, u1, 1.0f, u0, 1.0f};
    const float verts[8] = {
        vx0, vy0,
        vx1, vy0,
        vx1, vy1,
        vx0, vy1,
    };

    glBindTexture(GL_TEXTURE_2D, sGameTextureRing[sGameTextureUploadIdx]);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

#ifdef WIDESCREEN_SUPPORTED
    if (gFastDrawWide.output && Widescreen_StageFadesMargins())
        DrawStageMarginFade();
#endif
}

static void __attribute__((noinline)) MainThread_ParkForReset(void)
{
#ifdef VOXEL_SUPPORTED
    VoxelRenderer_OnReset();
#endif
    __atomic_store_n(&gUiVoxelFrame, false, __ATOMIC_RELAXED);
    GameFrame_Unpin();
    SDL_AtomicSet(&sCore0PausedForReset, 1);
    while (!SDL_AtomicGet(&sQuitRequested) && SDL_AtomicGet(&sResetInProgress))
        VitaPlatform_DelayMicroseconds(200);
    while (sFrameReadySem != NULL && SDL_SemTryWait(sFrameReadySem) == 0)
    {
    }
    SDL_AtomicSet(&sCore0PausedForReset, 0);
}

/* Steady-state Core 0 loop: entry point checked by tools/core_boundary/check_core_boundary.py. */
static void __attribute__((noinline)) MainThread_Run(void)
{
    bool prevVoxelActive = false;

    for (uint32_t presented = 0; !SDL_AtomicGet(&sQuitRequested);)
    {
        if (SDL_AtomicGet(&sResetInProgress))
        {
            prevVoxelActive = false;
            MainThread_ParkForReset();
            continue;
        }
        /* Single-clock hardware-vblank 3D pacing: only sleep on Core 1's semaphore in 2D modes or
         * initial bring-up; in steady-state 3D, let the 3-buffer SceGxm display queue pace swaps. */
        double semWaitMs = 0.0;
        if (!prevVoxelActive)
        {
            const double waitStartMs = VitaPerf_NowMs();
            const int32_t waitRes = SDL_SemWaitTimeout(sFrameReadySem, FRAME_WAIT_TIMEOUT_MS);
            semWaitMs = VitaPerf_NowMs() - waitStartMs;
            if (waitRes != 0)
                continue;
            if (SDL_AtomicGet(&sResetInProgress))
            {
                prevVoxelActive = false;
                MainThread_ParkForReset();
                continue;
            }
        }
        /* Drain any queued posts non-blockingly so Core 0 pins the newest GameFrame. */
        while (SDL_SemTryWait(sFrameReadySem) == 0)
        {
        }

        VitaPerf_BeginFrame();
        GameFrame_Pin();

        const bool speedUp = (SDL_AtomicGet(&sSpeedUpActive) != 0);
        const bool is30FpsLocked = (VitaExtras_GetFast(FAST_EXTRA_VOXEL_FPS) == 0) && !speedUp;
        VitaPerf_SetInput(speedUp, Ui_IsOpen());
#ifdef VOXEL_SUPPORTED
        const bool voxelActive = (VitaExtras_GetFast(FAST_EXTRA_VOXEL_3D) != 0)
            && GameFrame_Get()->voxel
            && (VoxelWorld_IsMapAvailable()
                || (VoxelWorld_IsBattleMapAvailable()
                    && VitaExtras_GetFast(FAST_EXTRA_VOXEL_BATTLE) != 0
                    && !VoxelBattle_IsLink()));
#else
        const bool voxelActive = false;
#endif
        __atomic_store_n(&gUiVoxelFrame, voxelActive, __ATOMIC_RELAXED);

        bool didPresent3D = false;
#ifdef VOXEL_SUPPORTED
        if (voxelActive && VoxelRenderer_RenderFrame())
            didPresent3D = true;
#endif
        if (didPresent3D)
        {
            eglSwapInterval(NULL, is30FpsLocked ? 2 : 1);
#ifdef VOXEL_SUPPORTED
            VoxelRenderer_SwapBuffers();
#endif
        }
        else
        {
            VDraw();
            RenderGameTexture();
#ifdef VOXEL_SUPPORTED
            VoxelRenderer_DrawFlyCloudCover();
#endif
            Ui_Render(SCREEN_W, SCREEN_H);
            eglSwapInterval(NULL, 1);
            vglSwapBuffers(GL_FALSE);
        }

        VitaPerf_RecordPacing(semWaitMs, GameFrame_Get()->sequence);
        prevVoxelActive = didPresent3D;
        VitaPerf_EndFrame(didPresent3D, (didPresent3D && is30FpsLocked) ? 30u : 60u);
        if (presented++ == 0)
            VITA_LOG("[INIT] first frame presented\n");
    }
}

static void MainThread_Boot(void)
{
    VitaPlatform_PinCurrentThread("MainThread", 0);
    VitaPlatform_Init();
    VitaPerf_Init();
    VITA_LOG("[INIT] FireRed Vita starting (built " __DATE__ " " __TIME__ ")\n");
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        VITA_LOG("[INIT] SDL_Init failed: %s\n", SDL_GetError());
        VitaPlatform_Exit();
    }
    {
        SceKernelFreeMemorySizeInfo mem;

        mem.size = sizeof(mem);
        if (sceKernelGetFreeMemorySize(&mem) >= 0)
            VITA_LOG("[MEM] free user=%d KB cdram=%d KB phycont=%d KB\n", mem.size_user / 1024,
                     mem.size_cdram / 1024, mem.size_phycont / 1024);
    }
    if (!InitVideo())
    {
        VITA_LOG("[INIT] vitaGL init failed\n");
        VitaPlatform_Exit();
    }
    /* Video setup before the boot snapshot is captured cleanly. */
#ifdef VOXEL_SUPPORTED
    if (!VoxelRenderer_Init())
        VITA_LOG("[VOXEL ERROR] VoxelRenderer_Init returned false; 3D disabled\n");
#endif
    /* A purple screen until the first game frame: shows that vitaGL presents. */
    glClearColor(0.25f, 0.0f, 0.35f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_FALSE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    sFrameReadySem = SDL_CreateSemaphore(0);
    if (sFrameReadySem == NULL)
    {
        VITA_LOG("[INIT] SDL_CreateSemaphore failed: %s\n", SDL_GetError());
        VitaPlatform_Exit();
    }
    InitAudio();

    VitaExtras_Init();
    VitaControls_Init();
    FrVitaBridge_ResetEmulatedHardware();
    FrVitaBridge_SaveInit();
    VitaNuzlocke_Init();
    /* Before the boot snapshot: the UI's tables are part of the game's memory. */
    Ui_Init();
    VITA_LOG("[INIT] save: %s\n", Port_SaveIsAvailable() ? "available" : "unavailable");
    /* The game's memory at power on, for resets. */
    VitaReset_TakeBootSnapshot();

    if (SDL_CreateThreadWithStackSize(GameThread, "AgbMain", GAME_THREAD_STACK_SIZE, NULL) == NULL)
    {
        VITA_LOG("[INIT] game thread not created: %s\n", SDL_GetError());
        VitaPlatform_Exit();
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    MainThread_Boot();
    MainThread_Run();
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* Save storage used by the non-bridge flash path (agb_flash.c, PORTABLE)                       */
/* ------------------------------------------------------------------------------------------ */

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    if (gFlash == NULL || dest == NULL || sectorNum >= gFlash->sector.count)
        return;
    const u32 start = ((u32)sectorNum << gFlash->sector.shift) + offset;

    if (start > sizeof(FLASH_BASE) || size > sizeof(FLASH_BASE) - start)
        return;
    memcpy(dest, FLASH_BASE + start, size);
}
