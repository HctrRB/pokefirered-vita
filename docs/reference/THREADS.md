# Threads and shared data

Type: reference · Audience: developers · Status: Sub-Fase F7b-1 built + Phases F7b-2–F7c remaining
Verified against: Sub-Fase F7b-1 build, ELF & core_boundary check (2026-11-22)

The PS Vita exposes three user Cortex-A9 cores (Core 0, Core 1, Core 2). This document records
how threads coordinate in `pokefirered-vita` today (Sub-Fase F7b-1: parallel PPU, prioritized Core 2
workers, single-clock hardware-vblank 3D pacing, 3D Voxel world active, Stage 2 Native 16:9 Core UI,
and Stage 1 3DS overlay retired) and the remaining target for Phases F7b-2–F7c.

## Current threads (`src/platform/vita/vita_main.c` & `src/platform/voxel/voxel_renderer.c`)

| Thread | Core | Created in | Current work | Planned Phases F7b-2–F7c |
|---|---|---|---|---|
| `MainThread_Run` | **0** (`VitaPlatform_PinCurrentThread`) | `main()` -> `MainThread_Boot` -> `MainThread_Run` | Pins `GameFrame_Pin()`, dispatches `VoxelRenderer_RenderFrame()` (chunks, entities, `VoxelRoom_StandOn`, `VoxelFollower_Update`, `VoxelWild_Update`, `VoxelBattle_StepStage`, `VoxelFly`) / `VoxelRenderer_SwapBuffers()` (3D, hardware-vblank paced) or `VDraw()` -> `RenderGameTexture()` -> `Ui_Render()` (2D), parks in `MainThread_ParkForReset` (`VoxelRenderer_OnReset()`), calls `VitaPerf_RecordPacing()` | Skip Pass 6 in 3D battles once `F7b-2` HUD lands |
| `AgbMain` (`GameThread`) | **1** (`VitaPlatform_PinCurrentThread`) | `SDL_CreateThreadWithStackSize` in `MainThread_Boot` | `GameThread_Register()`, `AgbMain()`, `VBlankIntrWait()` (`GbaEngine_StepVCount()`, `RunVBlank()`, `GbaEngine_StepHBlankScanlines()`, `Ui_Tick()`, `GameFrame_CaptureAndPublish()` incl. `VoxelWorld_CaptureField` / `VoxelBattle_Capture` / `VoxelEntities_Capture` / `VoxelFlyGame_Tick`, `SDL_SemPost`, `FrVitaBridge_FrameDone()`, `PaceFrame()`, `VitaInput_Poll()`), `SoftReset()` (`Ui_OnReset()`, `FrVitaBridge_NoteReset()`, `FastDraw_OnReset()`, `VoxelFlyGame_OnReset()`) | Native battle HUD + full `F7b–F7c` screens |
| `DrawWorkerMain` (`DrawWorker1`, `DrawWorker2`) | **2** (`VitaPlatform_PinCurrentThread`, priority `VITA_THREAD_PRIO_DRAW_WORKER` = `144`) | `StartWorkers()` in `gba_fast_draw.c` | Parallel alternate 288-px scanline draw (`FastDraw_DrawLinesB`, `FastDraw_DrawLinesC`) + NEON BGR555 -> RGB565 conversion (`CopyLineToTexture`) via `VitaPlatform_SetCurrentThreadPriority()` | Active across Stage 2 GBA overlays |
| `VoxelChunkWorkerMain` | **2** (`VitaPlatform_PinCurrentThread`, priority `VITA_THREAD_PRIO_VOXEL_WORKER` = `176`) | `ChunkWorker_Start()` in `VoxelRenderer_Init()` | 3-phase asynchronous chunk mesh + lighting builds (`ComputeChunkJob` incl. indoor `VoxelRoom_Ground` / `VoxelRoom_EmitInstance`) below `DrawWorkerMain` priority | Active |
| `VoxelBuildingIoWorkerMain` | **2** (`VitaPlatform_PinCurrentThread`, priority `VITA_THREAD_PRIO_VOXEL_IO` = `180`) | `VoxelRenderer_Init()` | Asynchronous streaming and Morton->Linear unswizzling of `VXB7` building pages from `app0:voxel/buildings.bin` below chunk worker priority | Active |
| vitaGL GC thread | **2** (`SCE_KERNEL_CPU_MASK_USER_2`, priority `127`) | `vglSetupGarbageCollector` in `InitVideo()` | Deferred GPU resource garbage collection | Keep priority/affinity untouched |
| SDL Audio thread | OS / SDL managed | `SDL_OpenAudioDevice` in `InitAudio()` | Drains SDL audio queue | Keep OS affinity untouched |

Do NOT re-pin or change priority of `SceGxmDisplayQueue`, vitaGL GC/internal workers, or SDL audio
threads; moving the GXM display queue off Core 0 crashes the GPU driver on PS Vita.

## Pacing model

- **Core 1 is the master clock (`VBlankIntrWait`):** `vita_main.c` maintains 60.00 Hz by pacing
  against `SDL_GetPerformanceFrequency()` / 60 and adjusting the deadline by `AUDIO_SKEW_US` via
  SDL queued audio byte count (`AUDIO_TARGET_QUEUE`, `AUDIO_MAX_QUEUE`) so audio neither starves
  nor builds latency.
- **Core 0 presentation pacing (Phase F6 active):**
  - In **2D mode**, Core 0 follows Core 1 via `sFrameReadySem` in `MainThread_Run` (`!prevVoxelActive`)
    with timeout `FRAME_WAIT_TIMEOUT_MS` so `SoftReset` handshake checks remain responsive.
  - In **steady-state 3D + HD-2D mode**, Core 0 skips blocking `sFrameReadySem` waits (draining queued
    posts non-blockingly with `SDL_SemTryWait`) and anchors presentation solely to the 3-buffer
    `SceGxm` display queue via `eglSwapInterval` (`2` at 30 FPS, `1` at 60 FPS / fast-forward) while
    pinning the latest `GameFrame`, eliminating dual-clock 49.8 ms 3-vblank cliffs.

## Cross-thread data & Phase F3–F5b synchronization guarantees

| Data | Writer | Reader | Synchronization / Invariant | Remaining target (Phases F6–F7) |
|---|---|---|---|---|
| `gGameFrameSlots` (`4` x 32-byte aligned `GameFrame`) | Core 1 (`GameFrame_CaptureAndPublish` inside `VBlankIntrWait`) | Core 0 (`GameFrame_Pin` / `GameFrame_Get` inside `MainThread_Run`) | Lock-free 4-slot ring in `vita_game_frame.c`; Core 1 never overwrites `sLatest` or `sPinned`; carries `VOXEL_SUPPORTED` `VoxelEntitySnapshot` (`wilds`, 1x1/1x2 doors, follower) & `GameFrameBattle` flags alongside `sMapMutationLock` | Populate Stage 2 window & HUD mirrors (`Phase F7`) |
| `sSnapshots` (`VOXEL_FLY_SNAP_SLOTS` `VoxelFlySnapshot` ring, `voxel_fly_game.c`) | Core 1 (`VoxelFlyGame_Tick`, `VoxelFlyGame_OnReset`) | Core 0 (`VoxelFly_GetSnapshot`) | Triple-buffered lock-free atomic snapshot (`sSnapIdx`, acquire/release) plus `sDrawnWarpVersion` feedback (`VoxelFly_NotifyFrameDrawn`) | Active under single-clock 3D vblank pacing (`Phase F6`) |
| `sGameTextureRing` (`VITA_GAME_TEXTURE_RING = 3`, `sFrameImage`, `sConverted`) | Core 0/2 (`VDraw` -> `DrawFrameToTexture`) | Core 0 (`RenderGameTexture`) | 3-slot texture ring so `glTexSubImage2D` never touches an in-flight swap texture; `VBlankIntrWait()` runs `GbaEngine_StepVCount()` / `GbaEngine_StepHBlankScanlines()` on Core 1 only | Used for GBA/2D scenes & Pass 6 overlay (`Phases F5–F7`) |
| `UiDrawList` triple-buffered ring (`ui_draw.c`) | Core 1 (`Ui_Tick()` -> `UiDraw_Publish`) | Core 0 (`Ui_Render()` Pass 7 via `UI_VBO_RING_SIZE = 4`) | `__atomic_exchange_n` triple-buffered display list + in-place `vglGetTexDataPointer` 64x64 sprite cells (`UI_SPR_IDLE_TICKS = 4`) | Active (`F7a–F7b-1`), extended to remaining screens in `F7b-2–F7c` |
| `VitaInputState` (`vita_input.c`) | Core 1 (`VitaInput_Poll` in `VBlankIntrWait()`) | Core 1 & Core 0 (`VitaInput_GetSnapshot`) | Single-producer seqlock (`sInputSeq` + ARM `dmb` fences) publishing `sPublishedState`; `VitaControls_MapButtons` + `Ui_TakeInjectedKeys()` feed `Platform_GetKeyInput()` | Active |
| `gamedata` / `gamebss` (`vita_reset.c`) | Core 1 (`SoftReset` -> `VitaReset_RestoreBootSnapshot`) | Core 1 (`AgbMain`) | Two-way atomic barrier (`sResetInProgress` / `sCore0PausedForReset`), `GameFrame_Unpin()` inside `MainThread_ParkForReset()`, `.noinit` `sSegmentEndPad` sentinel, `Ui_OnReset()`, and `FrVitaBridge_NoteReset()` | Active |
| Mutable game globals (`gPlayerParty`, `gSaveBlock1Ptr`, `gSaveBlock2Ptr`, `gTasks`, `gSprites`) | Core 1 only | Core 1 only | Enforced at build time via `tools/core_boundary/check_core_boundary.py --strict --require-draw-workers --require-voxel-workers` (`VITA_CORE_BOUNDARY=ON`, `VITA_PARALLEL_DRAW=ON`, `VITA_VOXEL_3D=ON`) and runtime `GAME_THREAD_ONLY()` (`include/platform/game_thread.h`) | Verified clean (`0` violations across Core 0/1/2) |

## Core boundary rules (mandatory for all new code)

1. **Core 1 owns all game state.** Core 0 and Core 2 may never invoke functions in
   `game_combined.o` nor read/write `gamedata`/`gamebss` directly outside `tools/core_boundary/allow.txt`.
   In particular, NEVER call `GetMonData3` or `GetMonData2` off Core 1 (they decrypt/re-encrypt
   Pokémon substructures in place and call `GAME_THREAD_ONLY()`).
2. **Capture on Core 1, read on Core 0.** Any game telemetry, HUD mirror, widescreen scene policy,
   Help System / Quest Log / Save Failed flag, or 3D world state needed for rendering must be
   captured on Core 1 inside `VBlankIntrWait()` into `GameFrame` and read back purely from the pinned
   `GameFrame` on Core 0.
3. **Never run unbuffered SD I/O on frame loops or save loops.** Unbuffered per-frame boot tracing
   is removed (`vita_main.c`), and flash writes buffer in `FLASH_BASE` for single atomic flush in
   `Platform_StoreSaveFile()`.
