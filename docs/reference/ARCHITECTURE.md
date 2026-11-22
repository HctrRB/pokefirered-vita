# Architecture

Type: reference · Audience: developers · Status: Phase F6 + Sub-Fase F7b-1 built (Stage 1 3DS UI retired; native 16:9 Hub/Options/Nuzlocke/Mirrors active)
Verified against: Sub-Fase F7b-1 ELF, `-Probe` ELF, `core_boundary` & `doc_check` (2026-11-22)

How `pokefirered-vita` is structured today, how it combines the PS Vita engine of
`pokeemerald-vita-3ds` with the Kanto patches and 3D generators of
`NachoGonzalez2001/Pokemon-FireRed-3DS`. Thread safety rules are in
[THREADS.md](THREADS.md), presentation passes in [RENDERING.md](RENDERING.md), the 3D Voxel &
HD-2D world in [VOXEL_ENGINE.md](VOXEL_ENGINE.md), and the native 16:9 UI in
[VITA_UI.md](VITA_UI.md).

## Layers (Sub-Fase F7b-1 baseline)

```
+----------------------------------------------------------------------------+
| Game side: game_side -> game_combined.o (runs on Core 1 only in AgbMain)   |
|   pret/pokefirered decompilation (037335f4) + Pokemon-FireRed-3DS patches: |
|     src/*.c, include/ (incl. gflib.h), asm/, data/, graphics/, sound/      |
|   GBA hardware emulated in RAM: src/platform/bios.c, dma.c, cgb_audio.c    |
|   Gameplay extras metadata: src/platform/fr3ds/fr_extras.c                 |
+----------------------------------------------------------------------------+
| Platform side: src/platform/{gba_fast_draw*,widescreen}.c, vita/, voxel/,  |
|                ui/ (Stage 2 active)                                        |
|   vita_main.c      main() -> MainThread_Boot / MainThread_Run (Core 0),    |
|                    GameThread / VBlankIntrWait (Core 1), SoftReset barrier |
|   gba_fast_draw*.c software GBA PPU (288-px lines, Core 2 workers + NEON)  |
|   widescreen.c     16:9 margin policy per scene vs Group A 816x544 GBA     |
+-------------------------------------+--------------------------------------+
| Voxel 3D & HD-2D src/platform/voxel | Vita features & Native 16:9 UI       |
|   Kanto 640-metatile / 7-palette /  |   src/platform/vita/: GameFrame,     |
|   32-bit attr world, chunks, atlas, |   GAME_THREAD_ONLY tripwire, clocks, |
|   Kanto VXB7 buildings, relief,     |   controls, stage scenes, day/night, |
|   trees, signs, lamps, lighting,    |   extras, VitaPerf, saves, Nuzlocke  |
|   HD-2D shaders, voxel_room.c,      |   src/platform/ui/ (Stage 2 active): |
|   entities, follower, 6 wilds,      |   960x544 Native 16:9 Stitch UI      |
|   3D battle & Kanto/Sevii 3D Fly    |   (FireRed GBA + 5 themes, UiDrawList)|
+-------------------------------------+--------------------------------------+
| VitaSDK · vitaGL (SceGxm) · SDL2 (audio device, threads, timers)           |
+----------------------------------------------------------------------------+
```

### Target split and memory budgets (Phases F1-F7)

Modeled after `pokeemerald-vita-3ds`:

1. Root-driven Modern CMake (`CMakeLists.txt`), keeping `-fno-builtin` scoped strictly to
   `game_side` and `-O2 -fno-section-anchors -Wall -Wextra` on `platform` targets.
2. Clean separation between `game_combined.o` (`.data` -> `gamedata`, `.bss` -> `gamebss`) and
   platform code, verified on every build by phase-aware `tools/core_boundary/check_core_boundary.py`.
3. `generated/game_data.o` converts its `.rodata` to `.data` via `arm-vita-eabi-objcopy` during
   CMake linking because `maps.s`, `event_scripts.s`, and `sound_data.s` embed relocatable pointers
   that cannot live in the `R E` segment (`check_core_boundary.py` treats it as read-only game data).
   `vita/vita_link.sh` separately pads the executable `LOAD` segment gap (`NEEDED`) so
   `vita-elf-create` can append SCE module info safely.
4. `src/platform/vita/vita_reset.c` requires a non-zero `.noinit` sentinel (`sSegmentEndPad`) and
   safe stop-pointer arithmetic after `gamebss` so `vita-elf-create` never drops the `__stop_gamebss`
   relocation at the end of the `RW` program header segment.
5. Public cross-layer headers under `include/platform/` (`game_frame.h`, `voxel_snapshot.h`,
   `game_thread.h`, `ui.h`, `vita_daynight.h`, `vita_extras.h`, `vita_controls.h`, `widescreen.h`),
   keeping `#ifdef PORT_BRIDGE` sites in `src/*.c` intact for clean upstream merges while folding
   no-op `0005-port-hooks.patch` stubs into `static inline` identity helpers in
   `src/platform/compat/port_platform.h`.
6. **Memory pools (upgraded in Phase F5a):** `vita_platform.c` reserves `_newlib_heap_size_user`
   (`64 MiB`), and `vita_main.c` calls `vglInitWithCustomSizes` with `VGL_POOL_BYTES` (`8 MiB`),
   `VGL_RAM_BYTES` (`64 MiB`), `VGL_CDRAM_BYTES` (`88 MiB`), and `VGL_PHYCONT_BYTES` (`8 MiB`) to
   accommodate simultaneous 3D Voxel caches, triple-buffered HD-2D FBOs
   (`VOXEL_POST_TARGET_BUFFERS`), and the future Stage 2 Native 16:9 UI RGBA8888 texture
   (`UI_TEX_W` x `UI_TEX_H`).
7. Upstream base pinned to `pret/pokefirered` (`037335f4c725d7c9aecdac87066f2002b4bd7e14`)
   and `NachoGonzalez2001/Pokemon-FireRed-3DS` (`2d7c8ec`, v0.5.2).

## Boot sequence (`src/platform/vita/vita_main.c`)

Entry point `main()` lives in [`src/platform/vita/vita_main.c`](../../src/platform/vita/vita_main.c)
and splits one-time startup (`MainThread_Boot`) from the steady-state Core 0 loop (`MainThread_Run`
checked by `tools/core_boundary/check_core_boundary.py`):

1. `VitaPlatform_PinCurrentThread("MainThread", 0)` pins Core 0, then `VitaPlatform_Init()` creates
   `ux0:data/FRVITA/` and `ux0:data/FRVITA/saves/`, configures 444/222/222/166 MHz clocks via
   `scePower*`, and initializes `VitaInput_Init()` (`VitaPerf_Init()` follows immediately).
2. `InitVideo()` configures vitaGL (`vglSetupGarbageCollector`, `vglSetupRuntimeShaderCompiler`,
   `vglUseLowPrecision`, `vglUseVramForUSSE`, `vglUseCachedMem`, `vglUseTripleBuffering`), calls
   `vglInitWithCustomSizes(...)` (`64 MiB` RAM / `88 MiB` CDRAM), and creates the 3-slot
   `GL_TEXTURE_2D` ring (`sGameTextureRing`, `288x160`).
3. `VoxelRenderer_Init()` initializes Core 2 voxel workers, chunk VBO pool, and Kanto datasets.
4. `InitAudio()` opens an SDL2 48 kHz stereo audio stream (`SDL_OpenAudioDevice` +
   `SDL_NewAudioStream`) feeding `cgb_audio_init` and `Platform_QueueAudio`.
5. `VitaExtras_Init()` loads `settings.txt` and populates `sFastSettings` (`VitaExtras_GetFast`),
   `VitaControls_Init()` loads physical button remaps and active save slot (`1..3`),
   `FrVitaBridge_ResetEmulatedHardware()` clears emulated GBA registers/VRAM/OAM/PLTT,
   `FrVitaBridge_SaveInit()` (`Port_SaveInit`) loads `VitaControls_GetSavePath()` into `FLASH_BASE`,
   `VitaNuzlocke_Init()` loads `ux0:data/FRVITA/nuzlocke_slotN.bin`, and `Ui_Init()` loads
   `assets/ui2/ui_atlas.bin` (`UiAssets_Load`) and allocates the `1024x2048` RGBA8888 texture.
6. `VitaReset_TakeBootSnapshot()` captures `gamedata` and `gamebss` immediately before spawning
   `SDL_CreateThreadWithStackSize(GameThread, "AgbMain", GAME_THREAD_STACK_SIZE, NULL)` on Core 1,
   where `GameThread_Register()` records the Core 1 stack range for `GAME_THREAD_ONLY()`.

## Frame lifecycle (Sub-Fase F7b-1 active + Phases F7b-2–F7c targets)

On Core 1 inside `VBlankIntrWait()` (`src/platform/vita/vita_main.c`):
- `GbaEngine_StepVCount()` triggers VCount match interrupts (`m4aSoundVSync`), `RunVBlank()` sets
  `REG_VCOUNT` / `DISPSTAT_VBLANK` / `INTR_FLAG_VBLANK`, runs VBlank DMAs, invokes `gIntrTable`
  VBlank handler, and clears VBlank, and `GbaEngine_StepHBlankScanlines()` dispatches per-scanline
  HBlank DMAs/IRQs and publishes Core 1 register captures.
- `Ui_Tick()` drives the Stage 2 960x544 native UI screen stack (`gUiHubScreen`, `gUiOptionsScreen`,
  `gUiNuzlockeScreen`), Kanto/Sevii zone signs (`UiSign_Draw`), and overworld/starter window mirrors
  (`UiMirror_Draw`), publishing lock-free vertex quads (`UiDraw_Publish`) for Core 0 `Ui_Render()`.
- `VoxelFlyGame_Tick()` advances Kanto/Sevii 3D Fly state and publishes the triple-buffered
  `VoxelFlySnapshot` ring.
- `GameFrame_CaptureAndPublish()` populates the 4-slot `GameFrame` ring (`vita_game_frame.c`
  calling `VoxelWorld_CaptureField`, `CaptureOverlay`, `VoxelBattle_Capture`, and
  `VoxelEntities_Capture` when 3D is active), and `SDL_SemPost` wakes Core 0 in 2D modes.
- `FrVitaBridge_FrameDone()`, `PaceFrame()` (60.00 Hz audio-locked pacing, updating atomic
  `sSpeedUpActive`), and `VitaInput_Poll()` (atomic seqlock snapshot in `vita_input.c`) finish
  the Core 1 frame.

Core 0 (`MainThread_Run` in `vita_main.c`) waits on `sFrameReadySem` only in 2D modes or initial
bring-up (`!prevVoxelActive`), running steady-state 3D frames strictly on hardware VBlank cadence
without semaphore starvation, checks `MainThread_ParkForReset()` (which also resets
`VoxelRenderer_OnReset()`) during `SoftReset`, calls `GameFrame_Pin()`, records `VitaPerf_SetInput()`
and `VitaPerf_RecordPacing()`, and dispatches:
- **3D path (`VOXEL_SUPPORTED` active):** When `VoxelWorld_IsMapAvailable()` or 3D battle is active,
  `VoxelRenderer_RenderFrame()` draws chunks (plus Core 2 indoor `VoxelRoom_EmitInstance` geometry),
  trees, Kanto VXB7 buildings, entities (`VoxelRoom_StandOn`, 1x1 & 1x2 doors, follower & 6 visible
  wilds via `VoxelFollower_Update` / `VoxelWild_Update`, Pokécenter heal balls, shadows, water
  reflections), 3D battles, 3D Fly, HD-2D passes, and Pass 6/7 UI (`Ui_Render`), followed by
  `eglSwapInterval` (`2` at 30 FPS, `1` at 60 FPS) and `VoxelRenderer_SwapBuffers()`.
- **2D fallback path:** `VDraw()` drives Core 2 `DrawWorkerMain` parallel scanline rendering with
  NEON conversion into `sGameTextureRing`, `RenderGameTexture()` resets fixed-function GL state and
  draws the 288-px 16:9 or centered 816x544 Group A quad, `Ui_Render()` draws the Stage 2 960x544
  native UI draw list (Pass 7), and `vglSwapBuffers(GL_FALSE)` presents at `eglSwapInterval(..., 1)`.

See [THREADS.md](THREADS.md), [VOXEL_ENGINE.md](VOXEL_ENGINE.md), and [VITA_UI.md](VITA_UI.md)
for the complete multicore, HD-2D, and Native 960x544 16:9 UI details.

## FireRed engine differences vs Emerald

- `include/gflib.h` sits directly in `include/` rather than a top-level `gflib/` directory.
- Primary tilesets split at **`640` metatiles/tiles** (`NUM_METATILES_IN_PRIMARY`,
  `NUM_TILES_IN_PRIMARY`) and **`7` palettes** (`NUM_PALS_IN_PRIMARY`), and use **32-bit
  metatile attributes (`INCBIN_U32`)** (`include/fieldmap.h`, `include/global.fieldmap.h`).
- Triple overworld tilemap buffers (`gBGTilemapBuffers1`, `gBGTilemapBuffers2`,
  `gBGTilemapBuffers3` in `src/overworld.c`).
- Help System (`src/help_system.c`, `src/help_system_util.c`, `RunHelpSystemCallback`), Save Failed
  (`src/save_failed_screen.c`, `RunSaveFailedScreen`), and Quest Log (`src/quest_log.c`,
  `QL_IS_PLAYBACK_STATE`, `QL_STATE_RECORDING`) run inside `CallCallbacks` (`src/main.c`) or
  `CB2_Overworld` without switching `gMain.callback2` away from the field/battle.
- Fame Checker (`src/fame_checker.c`) and Teachy TV (`src/teachy_tv.c`).
- 3 main Bag pockets (`NUM_BAG_POCKETS_NO_CASES` in `include/item_menu.h`) plus separate
  **TM Case** (`src/tm_case.c`) and **Berry Pouch** (`src/berry_pouch.c`), **Player's Bedroom
  Item PC** (`src/item_pc.c`), **9-Habitat Pokédex** (`src/pokedex_screen.c`), and **4 Region Maps**
  (Kanto + 3 Sevii Island maps in `src/region_map.c`).
- Wireless Adapter RFU hooks in `src/main.c` (`rfu_REQ_stopMode`, `rfu_waitREQComplete`) and
  `REVISION >= 0xA` saveblock helpers.
