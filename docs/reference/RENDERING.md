# Rendering

Type: reference · Audience: developers · Status: Phase F6 + Sub-Fase F7b-1 built (Native 960x544 Core UI + Mirrors active; Battle HUD in F7b-2)
Verified against: Sub-Fase F7b-1 build, `-Probe` build, ELF & `core_boundary` check (2026-11-22)

How frames are drawn by the parallel software GBA PPU on Core 2 or the 3D Voxel + HD-2D pipeline
([VOXEL_ENGINE.md](VOXEL_ENGINE.md)), composed with the Stage 2 Native 960x544 16:9 UI
([VITA_UI.md](VITA_UI.md)), and tuned in Phases F6–F7.

## Active pipelines (`vita_main.c` + `voxel_renderer.c` + `gba_fast_draw.c` + `src/platform/ui/`)

1. **Core 2 parallel software GBA PPU + NEON (`src/platform/gba_fast_draw.c`, `src/platform/gba_fast_draw_b.c`, `src/platform/gba_fast_draw_c.c`):**
   - Core 1 runs hardware timing (`GbaEngine_StepVCount()`, `RunVBlank()`,
     `GbaEngine_StepHBlankScanlines()`) and publishes a pinned `GameFrame`; Core 0 runs `VDraw()` ->
     `DrawFrameToTexture()` via `VITA_PARALLEL_DRAW=ON`.
   - Core 0 calls `Widescreen_PrepareDraw()` (`src/platform/widescreen.c`) and captures per-line
     register/palette snapshots and `gPlttExt`/`gOamExt`/`gPlttExtInUse` while two pinned Core 2
     workers (`DrawWorkerMain` via `FastDraw_DrawLinesB` and `FastDraw_DrawLinesC`) render
     alternate 288-px lines (`FRAME_LINE_WIDTH` x `DISPLAY_HEIGHT`) and run NEON BGR555 -> RGB565
     conversion (`CopyLineToTexture`) straight into `sConverted`.
2. **3D Voxel + HD-2D World (Phases F5a–F6 active when `VITA_VOXEL_3D=ON` and `GameFrameField.voxel` is active):**
   - On `MainThread_Run()`, Core 0 initializes `VoxelRenderer_Init()`, uses scaled pools
     (`VGL_RAM_BYTES = 64 MiB`, `VGL_CDRAM_BYTES = 88 MiB`), paces steady-state 3D frames solely on
     hardware VBlank via `eglSwapInterval` (`1` for 60 FPS/fast-forward, `2` for 30 FPS) without
     blocking on `sFrameReadySem`, and invokes `VoxelRenderer_RenderFrame()`.
   - Core 2 background workers (`VoxelChunkWorkerMain` at priority `176` and
     `VoxelBuildingIoWorkerMain` at `180`, below `DrawWorkerMain` `144`) stream Kanto terrain/relief
     chunks (`assets/voxel/relief.bin`), indoor room geometry (`voxel_room.c`), and 3D buildings
     (`assets/voxel/buildings.bin`) while Core 0 emits entities/followers/`6` visible wilds/1x1 & 1x2
     doors/shadows/reflections (`voxel_entities.c`, `voxel_follower.c`, `voxel_sprite_slots.c`), 3D
     battles (`voxel_battle.c`, rasterized at `480x272` in HD-2D post mode), Kanto/Sevii 3D Fly
     (`voxel_fly.c`), Kanto night beacons/fog/fireflies, and `voxel_hd2d_shader.c` (`VOXEL_POST_TARGET_BUFFERS = 3`).
3. **3-slot Game Texture Ring & Quad (`VDraw` + `RenderGameTexture` in `vita_main.c`):**
   - Allocates `VITA_GAME_TEXTURE_RING` (`3`) `GL_TEXTURE_2D` slots (`288x160`, `GL_RGB`,
     `GL_UNSIGNED_SHORT_5_6_5`). `VDraw()` advances `sGameTextureUploadIdx` each frame before
     calling `glTexSubImage2D` so live GPU swaps are never rewritten mid-flight.
   - `RenderGameTexture()` resets 2D fixed-function GL state cleanly after any 3D shader pass and
     presents either the 288-px 16:9 quad (`WIDESCREEN_VISIBLE_U0`..`WIDESCREEN_VISIBLE_U1` +
     `DrawStageMarginFade` for still stage scenes) or the centered 816x544 GBA quad for Group A /
     Help System / Save Failed / Quest Log scenes (`GameFrameOverlay.gbaFormat`,
     [SCENES_16_9.md](SCENES_16_9.md)).
4. **Stage 2 Native 960x544 16:9 UI (`src/platform/ui/`, Pass 7 active in `F7b-1`):**
   - Core 1 runs `Ui_Tick()` (`ui_core.c`) to drive `gUiHubScreen`, `gUiOptionsScreen`,
     `gUiNuzlockeScreen`, Kanto/Sevii zone signs (`UiSign_Draw()`), and overworld/starter window
     mirrors (`UiMirror_Draw()`), publishing quad commands via `UiDraw_Publish()`.
   - On Core 0, `Ui_Render()` (`ui_render.c`) draws from the 4-slot dynamic VBO ring
     (`UI_VBO_RING_SIZE`) and `1024x2048` RGBA8888 texture (`assets/ui2/ui_atlas.bin` + live 64x64
     sprite cells), skipping Pass 6 GBA overlay whenever `Ui_MirrorCovers()` is true. The Stage 1
     320x240 overlay (`fr_bottom_ui.c` + `fr_vita_ui.c`) is retired from the build.
5. **Swap:** `VoxelRenderer_SwapBuffers()` (when `gUiVoxelFrame` is active) or `vglSwapBuffers(GL_FALSE)`.

## Presentation paths (Phases F5a–F7)

Adopted from `pokeemerald-vita-3ds`:

| Path | Status / Phase | When | Entry Point |
|---|---|---|---|
| **3D + HD-2D** | Active world, interiors, entities, followers, 3D battle & 3D Fly (`F5a–F5b`) + GPU/pacing wins & lamps (`F6`) | Field callbacks (`CB2_Overworld`) when `!Port_SceneShown() && !QL_IS_PLAYBACK_STATE`, shop buy menu, 3D battle | `VoxelRenderer_RenderFrame()` ([VOXEL_ENGINE.md](VOXEL_ENGINE.md)) |
| **2D Widescreen / GBA** | Active (`Phase F4`) | Every 2D screen (288-px 16:9 or 240-px centered 816x544 for Group A scenes) | `VDraw()` + `RenderGameTexture()` ([SCENES_16_9.md](SCENES_16_9.md)) |
| **Native 16:9 UI** | Core Engine, Hub, Options, Nuzlocke, Signs & Overworld/Starter Mirrors active (`F7b-1`); Battle HUD (`F7b-2`) & Bag/Party/Dex (`F7b-3–F7c`) pending | `UiDrawList` not empty | `Ui_Render()` ([VITA_UI.md](VITA_UI.md)), drawn last (Pass 7) |

### GPU rules enforced across Phases F4–F7

1. **Core 2 parallel PPU + NEON (Phase F4 active):** 288-px scanlines and NEON BGR555->RGB565
   conversion (`DrawFrameToTexture`) run on `DrawWorkerMain` threads on Core 2 alongside 16
   extended 4bpp sprite palettes (`gPlttExt`, `gOamExt`).
2. **Texture rings & in-place `vglGetTexDataPointer` writes (Phases F4–F7):**
   - The 2D game framebuffer uses `VITA_GAME_TEXTURE_RING` (`3`) slots to avoid single-texture
     `glTexSubImage2D` 3-deep swapchain stalls.
   - Animated metatile atlas rows (`WriteAtlasRowsDirect`), entity billboard cells
     (`WriteSpriteAtlasSlots`, per dirty 64x64 slot rather than 512x64 bands), and Stage 2 Native
     UI 64x64 sprite cells (`ui_sprites.c`, `UI_SPR_IDLE_TICKS = 4`) write directly via
     `vglGetTexDataPointer` with `UI_VBO_RING_SIZE = 4`.
3. **SGX543 TBDR & `SceGxm` discipline (Phase F6 active, [VOXEL_ENGINE.md](VOXEL_ENGINE.md)):**
   - Keep `GL_ALPHA_TEST` and `GL_BLEND` disabled on opaque passes (Pass `1A`, Pass `2A-0`) for
     hardware early-Z Hidden Surface Removal.
   - Never call `glFinish` or `glFlush` mid-pass (`scene_reset` ends the GXM scene and discards
     depth; `VitaPerf_GpuScene` syncs strictly at `glBindFramebuffer` render-target changes).
   - Skip redundant `glClear` on `bloomA` when the bright pass runs opaque, and skip the bloom/Kawase
     chain whenever `brightGain <= 0.0f` and no sky bloom or light shafts are active (`F1b`).
   - `vglUseLowPrecision(GL_TRUE)` compiles all floats to `half` (never depend on `highp` in
     fragment shaders). Compute post-processing UVs/DoF/vignette in the vertex shader via
     `DrawQuad` (`voxel_hd2d_shader.c`), evaluate light shafts in a 512x1 1D
     LUT (`VoxelHd2dShader_ShaftLut`), use `RGB565` (with `RGBA5551` fallback) for fast scene FBOs
     (`F8`), run quarter-res `240x136` wide blur (`F6`), render `480x272` 3D battle stages (`F10`),
     use 3 post-target FBO buffers (`VOXEL_POST_TARGET_BUFFERS` = `3`), and pace steady-state 3D on
     a single hardware-vblank clock.
4. **Skip Pass 6 GBA overlay with Native 16:9 UI (Phase F7, [VITA_UI.md](VITA_UI.md)):**
   - `ui_mirror.c`, `ui_sign.c`, and `ui_battle_hud.c` render dialogues, choices, Kanto signs, and
     the battle HUD in Pass 7 (`Ui_Render`), skipping Pass 6 scanline rendering (`5–18 ms/frame`
     saved in 3D).
