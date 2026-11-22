# GBA screens at 16:9

Type: reference · Audience: developers · Status: Phase F4 built + planned Phase F5-F7 3D/UI consumers
Verified against: Phase F4 build, ELF & core_boundary check (2026-11-22)

The PS Vita screen is 960x544 (16:9). An unstretched 240x160 GBA frame scaled by exact vertical fit
(x3.4) is 816x544, leaving 72 screen pixels (about 21 GBA pixels) on each side.

## 16:9 widescreen pipeline (Phase F4 active)

Under `VITA_PARALLEL_DRAW=ON` (`WIDESCREEN_SUPPORTED`), `pokefirered-vita` renders 288-px scanlines
(`FRAME_LINE_WIDTH = 288` in `src/platform/gba_fast_draw.c` + `src/platform/widescreen.c`) and
presents the visible 282.35-px span (`WIDESCREEN_VISIBLE_U0`..`WIDESCREEN_VISIBLE_U1`) across 960x544
so the GBA's 240 px stay aligned to screen x `72..888` at uniform x3.4 aspect without stretching.

### Per-scene rules in FireRed

- **Overworld field (`src/overworld.c`, `src/field_camera.c`, `src/fieldmap.c`, `src/event_object_movement.c`):**
  - `InitOverworldBgs()` and `InitOverworldBgs_NoResetHeap()` back `gBGTilemapBuffers1`,
    `gBGTilemapBuffers2`, and `gBGTilemapBuffers3` with static 64x32 buffers (`sWideTilemapBg1`,
    `sWideTilemapBg2`, `sWideTilemapBg3`), preserving FireRed's BG1->`gBGTilemapBuffers2` /
    BG2->`gBGTilemapBuffers1` / BG3->`gBGTilemapBuffers3` mapping inside `Widescreen_SetFieldFrame()`.
  - `field_camera.c` maintains a 64-tile (`32`-metatile) horizontal ring (`RingOffset`,
    `RING_FIRST_METATILE_X = -8`), while `fieldmap.c` (`GetMapGridBlockAt`) resolves cross-map
    connections beyond `MAP_OFFSET` and `event_object_movement.c` widens off-screen sprite culling
    by `WIDESCREEN_EXTRA_X`.
- **Battle (`src/battle_main.c`):**
  - `Widescreen_SetBattleFrame()` extends BG1–BG3 backgrounds and OAM sprites (`gFastDrawWide.extendObj`)
    across the 24-px sidebars and dynamically stretches the BG0 32x64 battle textbox (`BuildBattleTextboxMap`
    from `gBattleInterface_Textbox_Tilemap`).
- **Stage scenes (`src/platform/vita/vita_stage.c` + `src/platform/vita/compat/ctr_gba_stage.h`):**
  - `CMakeLists.txt` redirects `SetVBlankCallback` via forced include for `src/intro.c`,
    `src/title_screen.c`, `src/credits.c`, and `src/seagallop.c`. `widescreen.c` detects panning vs
    still layers (`WIDE_BG_WIDE` vs `WIDE_BG_EDGE_TILE`) and applies `DrawStageMarginFade()` on
    still stage frames.
- **Centered 816x544 Group A & stealth overlays (`src/platform/vita/vita_game_frame.c`):**
  - **Help System** (`HelpSystem_IsActiveForUi` -> `RunHelpSystemCallback`) and **Save Failed**
    (`SaveFailed_IsActiveForUi` -> `RunSaveFailedScreen`) run inside `CallCallbacks` without
    changing `gMain.callback2`, and **Quest Log playback** (`QL_IS_PLAYBACK_STATE`) runs inside
    `CB2_Overworld`. `CaptureOverlay()` explicitly checks those flags alongside `Port_SceneShown()`
    and Group A callback queries (`FameChecker_IsGbaFormatForUi`, `TeachyTv_IsGbaFormatForUi`,
    `RegionMap_IsGbaFormatForUi`, `BerryCrush_IsGbaFormatForUi`, `PokemonJump_IsGbaFormatForUi`,
    `DodrioBerryPicking_IsGbaFormatForUi`, `ClearSaveData_IsGbaFormatForUi`,
    `TrainerCard_IsGbaFormatForUi`, `TradeMenu_IsGbaFormatForUi`, `UnionRoomChat_IsGbaFormatForUi`,
    `MysteryGift_IsGbaFormatForUi`) to latch `GameFrameOverlay.gbaFormat` cleanly.
  - Core 0 reads only the pinned `GameFrame` inside `Widescreen_PrepareDraw()` and
    `RenderGameTexture()` — never `gMain.callback2` directly.
