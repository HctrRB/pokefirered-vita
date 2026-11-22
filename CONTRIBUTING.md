# Contributing

Read [docs/reference/ARCHITECTURE.md](docs/reference/ARCHITECTURE.md),
[docs/reference/THREADS.md](docs/reference/THREADS.md),
[docs/reference/VOXEL_ENGINE.md](docs/reference/VOXEL_ENGINE.md), and
[docs/reference/VITA_UI.md](docs/reference/VITA_UI.md) first.
Every change is tested on a real PS Vita before it is considered done.

## Where code goes

| Change | Place |
|---|---|
| Vita backend, GBA hardware emulation (`bios.c`, `dma.c`, `cgb_audio.c`), scanline PPU (`gba_fast_draw*.c`), 16:9 widescreen (`widescreen.c`), main loop | `src/platform/` and `src/platform/vita/` |
| 3D Voxel Overworld, 3D Kanto rooms (`voxel_room.c`), 3D battles & HD-2D shaders (Phases F5–F6) | `src/platform/voxel/` |
| Stage 2 Native 960x544 16:9 touch UI, themes (`RED`, `GBA`), battle HUD, mirrors (Phase F7) | `src/platform/ui/` |
| Stage 1 FireRed 3DS touch overlay & enhancements (`fr_bottom_ui.c`, `fr_extras.c`, retired in F7) | `src/platform/fr3ds/` |
| Public platform headers shared cleanly with game code (Phase F2–F3 target) | `include/platform/` |
| Game behaviour | The decompilation (`src/*.c`), only as small hooks under `#ifdef PLATFORM_VITA` (or `VOXEL_SUPPORTED`). Keep the original code path intact in the `#else` branch |

Never reformat or restyle decompilation files; keep their diffs minimal so upstream `pret/pokefirered`
and `NachoGonzalez2001/Pokemon-FireRed-3DS` patches stay mergeable. Note that in FireRed
`include/gflib.h` lives directly under `include/`, primary tilesets have **640** metatiles/tiles and
**7** palettes (`include/fieldmap.h`), and metatile attributes are **32-bit** (`INCBIN_U32`).

## Code standards (platform code)

Language: **English** for identifiers, comments and documentation.

1. **Types**: fixed-width types (`uint32_t`, `int32_t`, `uint8_t`, `size_t`) instead of bare
   `int`/`unsigned`. `const` on read-only parameters, pointers and locals.
2. **Flow**: guard clauses and early returns; at most two levels of nesting; one responsibility
   per function, aiming at <= 50 lines. Hot scanline or voxel loops may be longer when splitting
   them costs measurable time; document the reason in a comment.
3. **No magic values**: named constants, `enum`s or macros. No duplicated logic blocks.
4. **File header**: a new file, or one split off or rewritten, starts with a comment saying what it
   does, which thread(s) run it, and its origin and licence (`original / MIT`,
   `pokeemerald-multiplatform / MIT`, `pokeemerald-vita-3ds / MIT`,
   `Pokemon-FireRed-3DS / MIT`, or `Emerald 3DS / CC BY-NC-SA 4.0`).
5. **Comments where it is not obvious**: explain *why* (hardware quirks, barriers, ownership,
   invariants); never paraphrase what the syntax already says.
6. **Vita safety**: check every `sce*` return value (negative is an error). No buffers over 4 KB on
   the stack. No `printf`; use `VITA_LOG` (buffered, compiled only under `VITA_LOGGING`). Exit with
   `sceKernelExitProcess(0)`. Never write unbuffered SD logs inside frame loops (`VBlankIntrWait` or
   `PresentFrame`).
7. **Threads**: follow [docs/reference/THREADS.md](docs/reference/THREADS.md). Game logic and
   mutable game state belong strictly to Core 1 (`AgbMain`). Core 0 and Core 2 read only `GameFrame`
   snapshots / emulated display buffers and pass the build-time boundary check (`tools/core_boundary`).
8. **CMake**: Modern target-based CMake only (`target_include_directories`, `target_link_libraries`,
   `target_compile_definitions`, `target_compile_options` with explicit `PRIVATE`/`PUBLIC`/`INTERFACE`
   visibility; `-fno-builtin` and `game_side` defines scoped strictly to `game_side`,
   `-fno-section-anchors` and `-Wall -Wextra` on platform code).

Formatting: `src/platform/.clang-format` and `.editorconfig` (Allman braces, 4 spaces, 110 columns).
Format only lines you modify; never mix formatting churn with logic changes in the same commit.

## Workflow

1. Work on a feature/fix branch.
2. Build cleanly from root with `build_vita.ps1` ([docs/guides/BUILD.md](docs/guides/BUILD.md)).
   Zero new compiler warnings allowed, and `core_boundary` / `doc_check` gates must pass.
3. Test on real PS Vita hardware using the smoke list below; for GPU/HD-2D work, compare
   `perf_frames.csv` (`build_vita.ps1 -Probe`) before and after ([docs/guides/DEBUGGING.md](docs/guides/DEBUGGING.md)).
4. Commit in small steps: `type(scope): summary` (`feat`, `fix`, `perf`, `refactor`, `docs`, `build`,
   `chore`) with a body explaining *why*.
5. Update affected documentation in the same milestone. Reference docs and guides cite symbol names
   and file paths, never `file:line` numbers.

## Smoke test (on real PS Vita hardware)

1. Boot: copyright / Gengar vs Nidorino intro scenes -> Charizard title screen -> main menu.
2. New Game / Continue save -> Pallet Town -> Oak's lab (starter selection & rival battle; in Phase
   F5+, verify 3D room walls and tabletop Poké Balls).
3. Route 1 -> Viridian City -> Oak's Parcel -> Route 2 -> Viridian Forest -> Pewter City (no border
   flicker or camera jumps; in Phase F5–F6, check forest light shafts, motes/fireflies, and night lamps).
4. Wild encounter (including Visible Wild Pokémon) and trainer battles: FIGHT, BAG, POKéMON, RUN;
   verify Move Split category icons, Mega Evolution toggle, weather animations (`NULL` anim arg),
   and battle AI with disabled moves / Transform / Encore / Struggle (no `gBattleMoves` OOB).
5. Help System (`L` / `R`), Quest Log playback, Fame Checker, and Teachy TV transitions: clean
   816x544 GBA framing and return to field.
6. Touch UI (`START` / touchscreen): open Hub, Party, Bag, TM Case, Berry Pouch, Pokédex, Kanto/Sevii
   Map, Trainer Card, PC Boxes (`SELECT` QUICK), Bedroom Item PC, and Options (`RED` & `GBA` themes
   in Phase F7); verify persistence in `ux0:data/FRVITA/settings.txt`.
7. Save game (`ux0:data/FRVITA/saves/firered.sav`), soft reset (`L+R+START+SELECT` or
   `X+O+START+SELECT`), reload save and confirm party/save integrity (no Bad Eggs or torn UI state).
8. In Phase F6+, test `OPTIONS > GRAPHICS`: each HD-2D level (`OFF`, `LOW`, `MED`, `HIGH`) at 30 and
   60 FPS, by day and at night (street lamps, cave lanterns, Lavender Tower fog).
9. Quit cleanly to LiveArea and inspect `ux0:data/FRVITA/` (no crash dump `psp2core-*.psp2dmp`;
   confirm `thread_violations.log` does not exist).
