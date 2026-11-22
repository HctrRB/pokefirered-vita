# Voxel engine and HD-2D pipeline

Type: reference · Audience: developers · Status: Phase F5a-F6 built + planned Phase F7 target
Verified against: Phase F6 build, `-Probe` ELF, `core_boundary` & `doc_check` (2026-11-22)

How the 3D overworld ("diorama"), 3D battle stage, and PS Vita HD-2D post-processing pipeline
work across our two reference codebases — `pokeemerald-vita-3ds` (`src/platform/voxel/`, the
multicore vitaGL/SGX543 renderer and HD-2D shader pipeline) and
`NachoGonzalez2001/Pokemon-FireRed-3DS` (`3ds_port/src/voxel/` and `3ds_port/scripts/fr_*.py`,
the Kanto/Sevii geometry generators and interior room models) — and what Phases F5a–F5b now build
into `pokefirered-vita` ahead of Phase F6.

## Current state in `pokefirered-vita` (Phase F5b built)

Phases F5a–F5b enable `VITA_VOXEL_3D=ON` by default (`-DCTR_VOXEL_ENABLED=1 -DVOXEL_SUPPORTED`) and
compile `src/platform/voxel/` alongside `VITA_CORE_BOUNDARY=ON` (`--require-voxel-workers`):

- **Kanto 3D asset generation (`voxelgen/` -> `assets/voxel/`):** Imported `fr_*.py` generators
  from `Pokemon-FireRed-3DS` (`2d7c8ec`) into `voxelgen/scripts/` with `voxelgen/regen.sh` and
  generated `assets/voxel/trees.rgba5551` (`512x1024` Morton-swizzled RGBA5551),
  `assets/voxel/buildings.bin` (`VXB7`, `46` pages, `129` models, `235` placements),
  `assets/voxel/relief.bin` (`VXL4`..`CUTS`, `143` layouts), and generated tables
  `src/platform/voxel/voxel_props_table.h` and `src/platform/voxel/voxel_room_table.h`.
- **Active in Phase F5a–F5b:** `VoxelRenderer_Init()` / `VoxelRenderer_RenderFrame()` /
  `VoxelRenderer_SwapBuffers()` / `VoxelRenderer_OnReset()`, Core 2 `VoxelChunkWorkerMain` and
  `VoxelBuildingIoWorkerMain`, `voxel_world.c` (`640`/`7`/`u32` attributes, `VMap`, FireRed layout
  swaps, `VoxelWorld_CaptureField`), `voxel_atlas.c` (`VOXEL_BARE_FIRST`, `VOXEL_ATLAS_PAGES = 4`),
  `voxel_mesh_builder.c` (terrain, cliffs, slopes, indoor harbor water borders, `VoxelTree_Crown` /
  `VoxelTree_InWood` / underground cave flat borders), `voxel_tree.c`, `voxel_building.c` (`VXB7`),
  `voxel_relief.c`, `voxel_lighting.c` (with `VOXEL_TREE_WOOD` / `VOXEL_TREE_WOOD_FLOOR`),
  `voxel_camera.c`, `voxel_grade.c`, `voxel_hd2d_shader.c`, `vita_daynight.c`, `vita_perf.c`,
  full stateless Kanto 3D interiors in `voxel_room.c` (`VoxelRoom_Ground`, `VoxelRoom_EmitInstance`,
  `VoxelRoom_EmitAtlasCell`, Core 0 `VoxelRoom_StandOn`), `voxel_sprite_slots.c/h` (`512x512` `64`-slot
  sprite atlas with `uint64_t` dirty mask in `WriteSpriteAtlasSlots`), full `voxel_entities.c` (1x1 &
  1x2 animated doors via `PortDoor_Get`/`PortDoor_GetTwin`, Pokécenter heal balls excluded from 2D
  OAM via `FieldEffect_IsHealMachineBall`/`FieldEffect_IsHealMachineMonitor`, shadows, water
  reflections, clouds), `voxel_follower.c/h` (PMD follower + up to `6` visible wilds via
  `VisibleWild_IsSprite` and `VoxelWild_Update`), full 3D battle stage & terrain in `voxel_battle.c`
  (`VoxelBattle_Capture` with `CtrBattleBg_MoveBgShown`/`CtrBattleIntro_Sliding`), and full
  Kanto/Sevii 3D Fly (`voxel_fly.c` + `voxel_fly_game.c` using `RegionMap_GetMapSectionCenter`,
  `Port_FlyHover`, `Port_FlyBusy`, `VoxelFlyGame_OnWarpLoaded`, `VoxelFlyGame_Tick`, and
  `VoxelFly_NotifyFrameDrawn`).
- **Active in Phase F6:** Full HD-2D GPU and frame-pacing pipeline (`DrawQuad` full-screen pass
  `F12`, 1-tap `GL_LINEAR` downsample `F4`, `RGB565` fast scene FBO with `RGBA5551` fallback `F8`,
  `240x136` wide blur `F6`, skipped bloom chain when `bloomNeeded` is false `F1b`, no `bloomA` clear
  `F7`, 8-tap `VoxelHd2dShader_Kawase8` `F5`, 512x1 1D LUT `VoxelHd2dShader_ShaftLut` `K3`,
  `OverlayInPost` `F9/J3`, `480x272` battle stage `battleHalfRes` `F10`, `VOXEL_POST_TARGET_BUFFERS = 3`,
  single-clock hardware-vblank 3D pacing without `sFrameReadySem` starvation, Core 2 priorities
  `144`/`176`/`180`), Kanto night beacons in `voxel_lamps.c`, indoor Pokémon Tower & Seafoam
  fog (`Frame_ReadAtmosphere` / `Pass_Atmosphere3D`), Viridian/Berry Forest fireflies, and `VitaPerf`
  5 pacing CSV columns + `VBLANK HIST` + `build_vita.ps1 -Probe`.
- **Deferred to Phase F7:** Stage 2 Native 16:9 UI Pass 6 skip (`Ui_MirrorCovers()`).

## Hybrid architecture (Phase F5-F6 target)

To build a first-class 3D + HD-2D FireRed port on PS Vita without repeating single-threaded 3DS
bottlenecks, `pokefirered-vita` combines:

1. **PS Vita multicore GPU/worker runtime from `pokeemerald-vita-3ds` (`src/platform/voxel/`):**
   - Core 1 captures immutable field, entity, and battle state at VBlank into `GameFrame`
     (`VoxelWorld_CaptureField`, `VoxelEntities_Capture`, `VoxelBattle_Capture`).
   - Core 2 runs `VoxelChunkWorkerMain` (3-phase asynchronous chunk mesh + raycast lighting builds
     with cooperative abort `sAsyncAbortBatch` and frozen lookup memos `VoxelWorld_FreezeMemos`)
     and `VoxelBuildingIoWorkerMain` (streaming building texture pages from `app0:voxel/buildings.bin`).
   - Core 0 runs `VoxelRenderer_RenderFrame` (`Frame_*` setup + batched `Pass_*` submission over
     vitaGL / `SceGxm`, per-chunk static VBOs, direct texture memory writes via
     `vglGetTexDataPointer`, and the `voxel_hd2d_shader.c` post-processing pipeline).
2. **Kanto & Sevii world generators and indoor room system from `Pokemon-FireRed-3DS`:**
   - Python generators (`fr_art.py`, `fr_building_specs.py`, `fr_gen_buildings.py`,
     `fr_gen_relief.py`, `fr_gen_rooms.py`, `fr_gen_trees.py`) reading the `pret/pokefirered` tree.
   - Kanto indoor standing walls, furniture boxes/cards, and tabletop item elevation
     (`voxel_room.c`, `voxel_room_table.h`, `voxel_props_table.h`).
   - PMD-style 8-directional follower and wandering visible wild Pokémon billboards
     (`voxel_follower.c`).

## Critical FireRed vs Emerald engine differences in 3D

Every voxel module ported from `pokeemerald-vita-3ds` MUST adapt both the FireRed fieldmap format
constants (`include/fieldmap.h`, `include/global.fieldmap.h`) and **7 Hoenn-specific couplings**
verified in `pokeemerald-vita-3ds/src/platform/voxel/`:

### A. Fieldmap layout and attribute constants

| Constant / Format | `pokeemerald` | `pokefirered` | Affected voxel modules |
|---|---|---|---|
| Primary tileset metatiles (`NUM_METATILES_IN_PRIMARY`) | `512` | **`640`** | `voxel_atlas.c`, `voxel_world.c`, `voxel_building.c`, `voxel_relief.c`, `fr_art.py` |
| Primary tileset 8x8 tiles (`NUM_TILES_IN_PRIMARY`) | `512` | **`640`** | `voxel_atlas.c`, `fr_art.py` |
| Primary tileset palettes (`NUM_PALS_IN_PRIMARY`) | `6` | **`7`** (`NUM_PALS_TOTAL` = `13`) | `voxel_atlas.c` (animated tile composition & palette lookup), `gba_fast_draw.c` |
| Metatile attributes table (`gTileset_*` attributes) | `u16` (`INCBIN_U16`) | **`u32` (`INCBIN_U32`)**: behavior bits `0..8` (`0x000001FF`), terrain bits `9..13` (`0x00003E00 >> 9`), encounter bits `24..26` (`0x07000000 >> 24`), layer bits `29..30` (`0x60000000 >> 29`) | `voxel_world.c`, `voxel_mesh_builder.c`, `voxel_relief.c`, `voxel_regions.c`, `voxel_entities.c` |

### B. Seven Hoenn-to-Kanto module decouplings

1. **`voxel_tree.c`:** Replace Hoenn's 32 hardcoded secondary tileset checks (*Dewford*, *Fortree*,
   *Mauville*, *Lavaridge*, *Rustboro*, *Lilycove*, *EverGrande*, *BattleFrontierOutsideWest*,
   *Mossdeep*, *Fallarbor*) with Kanto/Sevii tileset rules from `Pokemon-FireRed-3DS`.
2. **`voxel_renderer.c`:** Remove Hoenn's *gTileset_Fallarbor* / ash-grass unpack special-case.
3. **`voxel_world.c`:** Replace *gTileset_GenericBuilding* with `gTileset_Building`, remove
   *gTileset_Fortree* puddle overrides, replace Hoenn's 3-entry Mirage/Sky Pillar `setmaplayoutindex`
   table with FireRed's **4 verified layout swaps** (`LAYOUT_SEAFOAM_ISLANDS_B3F_CURRENT_STOPPED`,
   `LAYOUT_SEAFOAM_ISLANDS_B4F_CURRENT_STOPPED`, `LAYOUT_SEVEN_ISLAND_HOUSE_ROOM1_DOOR_OPEN`,
   `LAYOUT_THREE_ISLAND_DUNSPARCE_TUNNEL_DUG_OUT`), map FireRed `MB_*` and active `WEATHER_*`
   constants (`WEATHER_NONE`, `WEATHER_SUNNY`, `WEATHER_FOG_HORIZONTAL`, `WEATHER_FOG_DIAGONAL`,
   `WEATHER_SHADE`), and guard `VoxelWorld_FieldShownLive()` with `!Port_SceneShown()` and
   `!QL_IS_PLAYBACK_STATE` (so Help System and Quest Log sepia playback stay 2D 816x544).
4. **`voxel_atlas.c`:** Drive animated tileset slots from FireRed's 6 tileset animation loops in
   `src/tileset_anims.c` (`TilesetAnim_General`, `TilesetAnim_CeladonCity`, `TilesetAnim_SilphCo`,
   `TilesetAnim_MtEmber`, `TilesetAnim_VermilionGym`, `TilesetAnim_CeladonGym`).
5. **`voxel_room.c` in Core 2:** Execute `VoxelRoom_Ground`, `VoxelRoom_EmitInstance`, and
   `VoxelRoom_EmitAtlasCell` purely over `ComputeChunkJob` CPU staging buffers (`sChunkStaging`),
   never reading game globals directly, and run `VoxelRoom_StandOn` on Core 0 (`voxel_entities.c`).
6. **`voxel_lamps.c`:** Replace Hoenn's *LAYOUT_LILYCOVE_CITY* beacons with verified Kanto
   landmarks (Vermilion Harbor, Bill's Sea Cottage, Silph Co., Cinnabar Mansion/Lab, Pallet, Viridian,
   Cerulean, Lavender, Celadon) plus optional `signposts.bin` street lamps.
7. **`voxel_fly.c` / `voxel_fly_game.c`:** Adapt Hoenn map groups/numbers and `FLDEFF_*` to
   Kanto & Sevii Fly targets.

## Target module layout (`src/platform/voxel/`)

| Module | Thread(s) | Role |
|---|---|---|
| `voxel_world.c/h` | Core 1 (capture), Core 0 & Core 2 (read) | Map instances, 4 FireRed layout swaps, 640-split metatile and 32-bit attribute queries. Core 1 runs `VoxelWorld_CaptureField`; Core 0/2 check `GameFrame_MapSettled()` |
| `voxel_camera.c/h` | Core 0 | Sub-tile follow camera, battle camera, and map-crossing coordinate shift (`VoxelCamera_Shift`) |
| `voxel_atlas.c/h` | Core 0 | Composes 640+secondary metatiles into `VOXEL_ATLAS_SLOTS` LRU pages; animated metatile state cache (**C1**); direct changed-row atlas writes (`WriteAtlasRowsDirect`, **C4**) |
| `voxel_mesh_builder.c/h` | Core 0 & Core 2 | Builds ground rows, cliffs, seam skirts, water, and delegates Kanto room pieces to `voxel_room.c` into `sChunkStaging` |
| `voxel_relief.c/h` | Core 0 & Core 2 | Reads `app0:voxel/relief.bin` (Kanto outdoor plateaus, Mt. Moon / Rock Tunnel / Seafoam / Cerulean Cave / Victory Road faces, S.S. Anne pier rocks); pure binary-search `VoxelRelief_Cut` (no shared mutable memo) and restored Kanto shadow `drawn` flag (`0x8000` bit or non-zero cells) |
| `voxel_regions.c/h` | Core 0 & Core 2 | Precomputed 4-bit cell roles from `app0:voxel/regions.bin` |
| `voxel_building.c/h` | Core 0 & Core 2 (`VoxelBuildingIoWorkerMain`) | Kanto & Sevii 3D building models and `VOXEL_BUILDING_CACHE_SLOTS` LRU texture pages from `app0:voxel/buildings.bin`; `LIVE_CACHE_SLOTS` / `ROW_INDEXES` (`16`) caches protected by `sBuildingCacheMutex` + `VoxelBuildings_PrepareWorld()` pre-warmed on Core 0 before Core 2 chunk work |
| `voxel_room.c/h`, `voxel_room_table.h`, `voxel_props_table.h` | Core 0 & Core 2 | Kanto indoor solid back walls, standing furniture boxes/cards (`VoxelRoom_EmitInstance`, `VoxelRoom_EmitAtlasCell`), floor replacement (`VoxelRoom_Ground`), and tabletop object elevation (`VoxelRoom_StandOn`) |
| `voxel_tree.c/h` | Core 0 & Core 2 | Trees, tall grass, flowers, and packed Kanto room art (`app0:voxel/trees.rgba5551`) |
| `voxel_sign.c/h` | Core 0 & Core 2 | Pixel-masked signposts and street lamps (`app0:voxel/signposts.bin`) |
| `voxel_lighting.c/h` | Core 0 & Core 2 | Sun direction from `VitaDayNightState`, cast shadow rays, contact AO, face shading via isolated `LightContext` instances (`sChunkCtx` for Core 2 chunk baking and `sEntityCtx` for Core 0 `VoxelLighting_SampleAlone`) |
| `voxel_lamps.c/h`, `voxel_pointlights.c/h` | Core 2 (emit), Core 0 (draw) | Per-chunk static night lamp pools, glass, and halos (`EmitChunkLamps`), Kanto fixed beacons, additive character lamp tinting (`Pass_EntityLamps`), and flickering cave lantern |
| `voxel_grade.c/h` | Core 0 & Core 2 | Day/dusk/night split-tone color grading and CPU-baked 16x16 Sobel tile relief |
| `voxel_hd2d_shader.c/h` | Core 0 | Optimized GLSL/CG post-processing shaders (`DrawQuad` full-screen pass): 1-tap `GL_LINEAR` downsample (`VoxelHd2dShader_Downsample`), single 8-tap Kawase bloom (`VoxelHd2dShader_Kawase8`), 512x1 1D LUT light shafts (`VoxelHd2dShader_ShaftLut` / `VoxelHd2dShader_Shafts`, fp16-safe `hh()` hash and clamped `pow()` under `vglUseLowPrecision(GL_TRUE)`), and vertex-UV strip-mesh tilt-shift DoF + split-tone composite (`VoxelHd2dShader_Composite`) |
| `voxel_entities.c/h`, `voxel_follower.c/h`, `voxel_sprite_slots.c/h` | Core 1 (capture), Core 0 (emit/draw) | Player, NPCs, tabletop Poké Balls, 8-directional follower, and up to 6 visible wild Pokémon (`EmitMysteryDungeonCard`), writing only dirty 64x64 slots (`WriteSpriteAtlasSlots`, **C2**) |
| `voxel_battle.c/h` | Core 1 (capture), Core 0 (draw) | 3D battle stage at Vita `960x544` / `WIDESCREEN_SCALE` viewport matching `VoxelCamera_Frame`, `480x272` HD-2D stage target (`battleHalfRes`, **F10**), centered shadow `glOrtho`, `dt`-scaled `BattleCamera`, and `Pass_Overlay2D` `introSliding` BG1/BG2 mask in front of the live voxel overworld (`CtrBattleBg_MoveBgShown`, `CtrBattleIntro_Sliding`) |
| `voxel_fly.c/h`, `voxel_fly_game.c/h` | Core 1 (`VoxelFlyGame_Tick`), Core 0 | 3D Fly cinematic over Kanto / Sevii (`VoxelFlySnapshot` ring) |
| `voxel_renderer.c/h`, `voxel_tuning.h`, `voxel_arena.c/h` | Core 0 | Chunk slot VBO cache, frustum culling, Core 2 worker queues (`ComputeChunkJob`, priorities `176`/`180`), render passes (`F1b`/`F6`/`F8`/`F10`), 3-buffer post FBOs (`VOXEL_POST_TARGET_BUFFERS = 3`), and fixed VRAM arenas |

## Render passes and SGX543 / vitaGL rules (Phase F6)

Per 3D frame in `VoxelRenderer_RenderFrame`:

1. **Pass 1A / 1B (Ground & Relief):** Opaque ground and relief drawn first with `GL_ALPHA_TEST`
   and `GL_BLEND` disabled for PowerVR SGX543MP4+ TBDR early-Z HSR; cliff cutouts follow in `1B`.
2. **Pass 2 (`2A-0`, `2A-1`, `2B` — Trees, Foliage & Kanto Room Pieces):**
   - `2A-0`: Static opaque tree cores and opaque room walls (`GL_ALPHA_TEST` off).
   - `2A-1`: Alpha-tested tree border strips and room furniture cutouts.
   - `2B`: GLSL vertex wind-sway and player-bend foliage.
3. **Pass 3 (Buildings & Props):** Drawn from per-chunk VBOs and LRU building texture pages.
4. **Pass 4 & 4.5 (Shadows & Water Reflections):** Cast/decal shadows and water reflections.
5. **Pass 5 (Entities, Follower & Visible Wilds):** Billboards from `GameFrame` entities, tabletop
   elevation via `VoxelRoom_StandOn`, and player roof silhouette (`GL_GREATER`).
5. **Pass 5.1 & 5.2 (Day/Night & Night Lamps):** Driven by a single per-frame `VitaDayNightState`
   (`vita_daynight.c`). Static lamp pools/glass/halos draw from Kanto fixed beacons (`voxel_lamps.c`:
   Vermilion Harbor, Bill's Sea Cottage, Silph Co., Cinnabar Mansion/Lab, Pallet, Viridian, Cerulean,
   Lavender, Celadon) plus optional `signposts.bin` into chunk VBOs, followed by `Pass_EntityLamps`.
6. **Pass 5.4–5.9 (Atmosphere, Fog Banks, Fireflies, Weather & 1D-LUT Light Shafts):**
   - Fog banks run outdoors and indoors (Lavender Pokémon Tower `WEATHER_FOG_HORIZONTAL` and a `0.35`
     icy sea-mist floor in Seafoam Islands) snapping to cached terrain heights (`SmoothedFogWorldY`);
     shaded forests (`ViridianForest`, `ThreeIsland_BerryForest`) trigger fireflies from `nightGlow > 0.10f`.
   - Sun and moon light shafts evaluate a 512x1 1D LUT (`VoxelHd2dShader_ShaftLut`) read by a
     single full-screen quad (**K3**) via `DrawQuad` (**F12**), eliminating diagonal
     triangle-strip tile-setup stalls.
   - Night veil / fade / lightning / cave gloom fold into `VoxelHd2dShader_Downsample` and
     `VoxelHd2dShader_Composite` whenever HD-2D post runs (`OverlayInPost`, **F9/J3**).
7. **HD-2D Post-Processing (`Pass_DepthOfField`):**
   - World renders into `VOXEL_DIORAMA_FBO_W x VOXEL_DIORAMA_FBO_H` (`960x544` RGBA8 in `HIGH`;
     `RGB565` with automatic `RGBA5551` fallback in `MED`/`LOW`/`OFF`, **F8**), or `480x272` in 3D
     battles (`battleHalfRes`, **F10**), downsampled with 1 `GL_LINEAR` tap (**F4**; `GL_NEAREST` for
     `480x272` battle stages) into `VOXEL_SCRATCH_FBO_W x VOXEL_SCRATCH_FBO_H` (`480x272`),
     wide-blurred at `240x136` (**F6**), bloomed with single 8-tap `VoxelHd2dShader_Kawase8` (`240x136`,
     **F5**, skipping redundant `glClear` on `bloomA` **F7** and skipping the bloom chain when
     `bloomNeeded` is false indoors with `WORLD BLOOM == 0` **F1b**), and composited with
     vertex-shader pre-scaled UVs/DoF/vignette, field vs battle program variants, and CPU-precomputed
     grade algebra (**F1/I2/I4/J1**) via `DrawQuad` (**F12**).
   - **Precision invariant (`I1`):** `vglUseLowPrecision(GL_TRUE)` forces all shader floats to
     `half`; never rely on `highp` in fragment shaders.
   - **Triple-buffered post targets (`VOXEL_POST_TARGET_BUFFERS` = `3`):** Matches vitaGL's 3-buffer
     `SceGxm` display queue so `Pass_DepthOfField` never stalls waiting on a busy backbuffer.
   - **Single-clock 30/60 FPS pacing (`pokeemerald-vita-3ds:FRAME_PACING_AUDIT.md`):** Steady-state
     3D frames never block on `sFrameReadySem`; Core 0 stays anchored solely to hardware vblank via
     `eglSwapInterval` (`2` at 30 FPS, `1` at 60 FPS / fast-forward) while reading the latest
     pinned `GameFrame`.
9. **Pass 6 (GBA 2D Overlay) & Pass 7 (Native 16:9 UI):**
   - Once Native 16:9 UI ([VITA_UI.md](VITA_UI.md)) lands in Phase F7, Pass 6 is skipped whenever
     `Ui_MirrorCovers()` is true or during 3D battles with the native battle HUD (`5–18 ms/frame`
     saved in 3D).
