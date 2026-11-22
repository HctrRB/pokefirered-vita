# Documentation

Type: index · Audience: everyone · Status: current
Verified against: Phase F7 build, ELF & `doc_check` (2026-11-22)

Start with [guides/BUILD_YOUR_OWN.md](guides/BUILD_YOUR_OWN.md) if you want to build your own VPK,
or [reference/ARCHITECTURE.md](reference/ARCHITECTURE.md) for the codebase overview, then read the
reference document for the subsystem you are changing. Contribution rules live in
[../CONTRIBUTING.md](../CONTRIBUTING.md).

## Guides: how to build and debug

| Document | Audience | Covers |
|---|---|---|
| [guides/BUILD_YOUR_OWN.md](guides/BUILD_YOUR_OWN.md) ([español](guides/BUILD_YOUR_OWN.es.md)) | Players | One-step build from your Pokémon FireRed cartridge (`build_all.bat`), installing, reporting bugs |
| [guides/ADDING_A_LANGUAGE.md](guides/ADDING_A_LANGUAGE.md) | Developers, translators | One build = one language: registry, UI catalog, BPRS text manifest (`gen_manifest.py`), staging (`localize.py`), layout patches |
| [guides/BUILD.md](guides/BUILD.md) | Developers | MSYS2/VitaSDK toolchain, host tools, game assets, CMake options, `core_boundary` and `doc_check` gates |
| [guides/DEBUGGING.md](guides/DEBUGGING.md) | Developers | Buffered logging (`VITA_LOG`), telemetry (`VitaPerf`), GPU probes (`VitaPerf_GpuScene`), SD card stall avoidance, boundary checks, crash dumps (`vita-parse-core`) |

## Reference: how the port works

| Document | Covers |
|---|---|
| [reference/ARCHITECTURE.md](reference/ARCHITECTURE.md) | Game side vs platform side split, boot sequence, frame loop, FireRed-specific modules, and memory pools |
| [reference/THREADS.md](reference/THREADS.md) | Core 0 / Core 1 / Core 2 responsibilities, audio PLL pacing, `GameFrame` contract |
| [reference/RENDERING.md](reference/RENDERING.md) | GBA software PPU, `VITA_GAME_TEXTURE_RING`, 3D + HD-2D passes, GPU rules |
| [reference/VOXEL_ENGINE.md](reference/VOXEL_ENGINE.md) | Kanto 3D overworld (`640` metatiles, `7` palettes, 32-bit attributes), 3D interior rooms (`voxel_room.c`), Core 2 `VoxelChunkWorkerMain`, PS Vita HD-2D shaders & 30/60 FPS pacing |
| [reference/VITA_UI.md](reference/VITA_UI.md) | Complete Stage 2 Native 960×544 16:9 UI (`src/platform/ui/`: Core UI, FireRed `GBA` & 5 modern themes, Hub, Options, Nuzlocke, Mirrors, Signs, Battle HUD, Party, Bag, Pokédex, Map, PCs, Mart, Naming, Easy Chat) |
| [reference/SCENES_16_9.md](reference/SCENES_16_9.md) | 960×544 presentation vs 240×160 GBA framebuffer, 288-px widescreen, Group A 816×544 GBA scenes, and Help System / Quest Log stealth overlays |
| [reference/ASSETS.md](reference/ASSETS.md) | Upstream decompilation data (`fetch_assets.sh` + `tools/assets/manifest.txt`), linked `INCBIN` model, Kanto voxel binaries, HGSS followers, and UI atlas (`ui_atlas.bin`) |
| [reference/RUNTIME_FILES.md](reference/RUNTIME_FILES.md) | VPK paths (`app0:`) and memory card files (`ux0:data/FRVITA/`) read and written at runtime, including atomic save flushing |

## Rules for documentation

- **English** for every reference document and code file (player guides may have localized
  translations kept in sync with the English source).
- **Reference documents describe verified code accurately**: cite symbol names and repository file
  paths, never `file:line` numbers or copied magic constants (`check_docs` strict rule).
- Every document under `docs/` starts with the `Type · Audience · Status` and `Verified against`
  header lines.
- Any commit that alters architecture, threads, memory pools, build options, or runtime files must
  update the affected reference document in the same milestone.
