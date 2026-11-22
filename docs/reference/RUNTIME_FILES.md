# Runtime files

Type: reference · Audience: players and developers · Status: Phases F1–F6 + Sub-Fase F7b-1 built
Verified against: Sub-Fase F7b-1 build, VPK & ELF inspection (2026-11-22)

What the game reads inside the VPK (`app0:`) and reads or writes on the PS Vita memory card
(`ux0:data/FRVITA/`).

## Inside the VPK (`app0:`)

| Path | Status | Read by | Content |
|---|---|---|---|
| `eboot.bin` | Current | PS Vita OS | Signed SELF binary compiled from `pokefirered.velf` |
| `sce_sys/` | Current | LiveArea | Application icon (`icon0.png`), background (`pic0.png`), and LiveArea template (`livearea/contents/template.xml`, `bg.png`, `startup.png`) |
| `voxel/` | Current (`Phase F5a+`) | `src/platform/voxel/` | Kanto 3D voxel world assets (`buildings.bin`, `relief.bin`, `regions.bin`, `signposts.bin`, `trees.rgba5551`; [ASSETS.md](ASSETS.md)) |
| `ui2/ui_atlas.bin` | Current (`Phase F7a+`) | `src/platform/ui/ui_assets.c` | Fixed atlas (baked theme fonts and sprites) for the 960x544 Native 16:9 UI ([VITA_UI.md](VITA_UI.md)) |
| `followers/` | Optional (`Phase F5b+`) | `voxel_follower.c` | Optional bundled follower sprite container (also searched in `ux0:data/FRVITA/followers/`) |

## On the memory card (`ux0:data/FRVITA/`)

Directory root constant: `VITA_DATA_DIR` (`src/platform/vita/vita_platform.h`), initialized by
`VitaPlatform_Init()` (`src/platform/vita/vita_platform.c`).

| File | Written / Read by | When | Content |
|---|---|---|---|
| `saves/firered.sav` | `fr_vita_bridge.c` (`Port_SaveInit`, `Port_WriteFlash`, `Platform_StoreSaveFile`) | Always | 128 KB GBA flash save layout (slot 1 default, plus slots 2–3 resolved by `VitaControls_GetSavePath()`). `Port_WriteFlash` updates `FLASH_BASE` in RAM and `Platform_StoreSaveFile()` flushes once atomically via `.tmp` + `rename()` at save boundaries in `src/save.c` (`Port_SaveInit` also recovers orphaned `.tmp` images) |
| `settings.txt` | `vita_extras.c`, `vita_controls.c`, `vita_platform.c` | Always | Graphics options (3D, HD-2D level, DoF, 30/60 FPS, Day/Night, Menu Theme `GBA`/`RED`/`EMERALD`/`CRYSTAL`/`TROPICAL`/`ROYAL`), physical button remaps, active save slot (`1..3`), and gameplay enhancements (`trainer_teams`, `move_split`, `visible_wild`, `exp_share`, `exp_catch`, `reusable_tms`, `field_hms`, `trade_evo`, `shiny_odds`, `encounter_rate`, `infinite_money`, `instant_victory`) |
| `debug.log` | `vita_platform.c` (`VitaPlatform_Log` / `VITA_LOG`) | Builds with `-DVITA_LOGGING=ON` | 32 KB buffered runtime diagnostic log (replaced unbuffered per-frame `boot.txt` removed in Phase F3) |
| `thread_violations.log` | `vita_game_thread.c` (Phase F3 active) | Only on violation | Caller LR addresses caught by `GAME_THREAD_ONLY()` off Core 1 ([../guides/DEBUGGING.md](../guides/DEBUGGING.md)) |
| `perf_summary.txt`, `perf_frames.csv` | `vita_perf.c` (Phase F6 target) | Builds with `-DVITA_TELEMETRY=ON` or `-DVITA_GPU_PROBE=ON` | Buffered per-frame CPU/GPU telemetry and automatic HD-2D pass-mask benchmark results |
| `ui_stall.log` | `ui_stall.c` (Phase F7 target) | Builds with `-DVITA_TELEMETRY=ON` | Logs Native 16:9 UI stalls exceeding 100 ms and identifies whether Core 0 or Core 1 held the frame |

OS crash dumps are written by the Vita kernel to `ux0:data/psp2core-*.psp2dmp`
([../guides/DEBUGGING.md](../guides/DEBUGGING.md)).
