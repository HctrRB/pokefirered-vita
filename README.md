# Pokémon FireRed Vita

> **Experimental version in active development.** This port is a work in progress and is still
> undergoing hardware testing. Expect bugs, visual glitches, and unfinished edge cases.

Unofficial native PS Vita port of Pokémon FireRed (v1.1 / `REVISION=1`), built from the
[pret/pokefirered](https://github.com/pret/pokefirered) decompilation. It is not an emulator:
the game's C code is compiled directly for the Vita's ARM CPU, while a multicore platform layer
emulates the GBA hardware, drives a 3D voxel + HD-2D renderer with vitaGL, and presents a native
960×544 16:9 touch interface.

On top of the decompilation, the project combines the Kanto gameplay patches and 3D world/room
generators of [Pokemon-FireRed-3DS](https://github.com/NachoGonzalez2001/Pokemon-FireRed-3DS) with
the multicore Vita engine, 3D/HD-2D pipeline, and native 16:9 Stitch UI from
[pokeemerald-vita-3ds](https://github.com/HctrRB/pokeemerald-vita-3ds) (originally built on
[Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds) and
[pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform), with an
optional **HD-2D** visual mode inspired by
[Bravely Emerald](https://github.com/marcoscodas/bravely-emerald)):

- a 3D Kanto & Sevii voxel overworld and 3D interior rooms built live from the game's maps, with
  sun, shadows, day/night cycle, lit windows, Kanto street lamps, weather, water reflections, and
  an optional **HD-2D** post-processing mode;
- 3D battles on a stage picked from the map around the player, with **Mega Evolution** and
  Physical/Special move split;
- a complete native 960×544 16:9 touch interface over the 3D world replacing the GBA menus, boxes,
  Pokédex, maps, Mart, and battle HUD, with six menu themes (authentic FireRed **GBA** skin as
  default, plus **RED**, **EMERALD**, **CRYSTAL**, **TROPICAL**, and **ROYAL**);
- HGSS follower Pokémon, visible wild Pokémon in the overworld, Kanto/Sevii 3D Fly, and optional
  QoL extras.

The full list is under [Features](#features).

> **AI-assisted project.** The code, build scripts, and documentation were written with AI coding
> assistants (Claude Code by Anthropic and Gemini) under the supervision of HctrRB, who sets the
> direction, reviews the changes, and tests the builds on a real PS Vita.

> **Legal notice.** Pokémon and Pokémon FireRed are © Nintendo, Creatures Inc., and GAME FREAK inc.
> This project is not affiliated with or endorsed by them. This repository does not contain the
> game's graphics, sounds, or binary data, nor any ROM: to build, you check a dump of **your own
> cartridge** and the game files are downloaded from the public decompilation
> ([docs/guides/BUILD_YOUR_OWN.md](docs/guides/BUILD_YOUR_OWN.md)). Like every decompilation, the
> tree still holds the game's code and text in decompiled form. A built VPK contains Nintendo's
> data: **do not distribute builds**.

## Features

### 3D world & HD-2D

- Kanto & Sevii voxel overworld built live from the game's 640-metatile / 7-palette maps, plus 3D
  interior rooms (`voxel_room.c`) with walkable furniture and tabletop Poké Balls in Oak's Lab.
- Day/night cycle with lit windows, Kanto street lamps and cave lanterns, weather, water
  reflections, 3D buildings, trees, and 30 or 60 FPS (`OPTIONS > GRAPHICS`), plus classic 16:9 2D
  mode.
- **HD-2D mode** (`OPTIONS > GRAPHICS`, `OFF` / `LOW` / `MED` / `HIGH`, independent of framerate):
  tilt-shift depth of field, bloom and light emitters, split-tone colour grading and vignette,
  point lights, dynamic sun/moon light shafts, forest fireflies, foliage sway, 16×16 Sobel tile
  relief, soft contact shadows, HD-2D rain, and adjustable camera angle (including 25°).
- 3D battles on a stage picked from the surrounding map, plus interactive Kanto & Sevii 3D Fly.
- Visible wild Pokémon in the overworld (configurable density and encounter rate) with extended
  sprite palettes, plus HGSS follower Pokémon.

### Native 960×544 16:9 interface

Drawn natively at 960×544 over the 3D world. Every screen works with buttons and touch (first tap
selects, a second tap on the selection confirms):

- **START Hub & menus**: 9-habitat Kanto / National Pokédex (list, entry, cry, size, area, and
  post-catch Dex card), Party & 3-page Summary (stats, IV/EV, moves, reorder, field moves),
  5-pocket Bag + TM Case + Berry Pouch + Berry Crush picker, 4-region Kanto / Sevii Town & Fly Map,
  Kanto Trainer Card (badges, front/back, stickers), Nuzlocke tracker, 6-tab Options, and Save.
- **PCs & shops**: Bill's PC 14-box storage (withdraw, deposit, move Pokémon/items, summary,
  release, marks, jump to box, wallpapers, box names, `SELECT` quick cursor), Player's PC (item
  storage, mailbox, mail reader on FireRed stationery), and Poké Mart (buy/sell).
- **Scripts & scenes**: Naming keyboard (player, rival, Pokémon, boxes), Easy Chat, Choose-a-Pokémon
  (tutors, Day Care, minigames), Forget Move & Two Island Move Maniac, plus overworld dialogue
  mirrors, YES/NO, choice lists, money/coins boxes, zone signs, and Oak's Lab starter preview card.
- **Battle**: Native 960×544 HUD with health plates, party balls, Physical/Special/Status move split
  icons, **Mega Evolution** Key Stone (`SELECT`), targets, Battle Party, Battle Bag, level-up stats,
  and Safari Zone plate.
- **Menu themes** (`OPTIONS > GENERAL > MENU COLOR`): `GBA` (FireRed default), `RED`, `EMERALD`,
  `CRYSTAL`, `TROPICAL`, and `ROYAL`.

### Languages

- **English** (default) and **Spanish** (*Pokémon Edición Rojo Fuego*). One build = one language:
  a Spanish build takes the whole game text, the text-bearing graphics and the Easy Chat order from
  **your own** Spanish cartridge, and translates the native interface. No game text is stored in
  this repository. How to build it: [docs/guides/BUILD_YOUR_OWN.md](docs/guides/BUILD_YOUR_OWN.md#spanish-version);
  adding another language: [docs/guides/ADDING_A_LANGUAGE.md](docs/guides/ADDING_A_LANGUAGE.md).

### Extras & multicore architecture

- Physical/Special move split, Mega Evolution, reusable TMs, field HM shortcuts, EXP Share & EXP
  on catch, trade-evolution bypass, shiny odds / encounter rate sliders, 3 save slots, and button
  remapping.
- Strict Core 0 / Core 1 / Core 2 separation (`AgbMain` isolated on Core 1, Core 2 parallel GBA PPU
  with NEON + chunk/I/O workers, Core 0 presentation + HD-2D), verified at build time by
  `tools/core_boundary/check_core_boundary.py`.

## Status

> **Experimental build — work in progress.** This repository is in active development and hardware
> testing. Expect bugs, graphical quirks, and screens or transitions that still need verification on
> real PS Vita hardware. If you find an issue, please [open an issue](../../issues/new/choose) with
> the bug report template: what you did, what happened, and the log files from `ux0:data/FRVITA/`.

- **Hardware validation in progress:** The complete Stage 2 native 960×544 UI suite (`src/platform/ui/`),
  HD-2D shader levels (`OFF` / `LOW` / `MED` / `HIGH` at 30/60 FPS), Kanto 3D rooms, Mega
  Evolution battle HUD, Help System (`L`/`R`), Quest Log playback, Fame Checker, Teachy TV, Bill's
  PC, and Player's PC are built and compiling cleanly, with full end-to-end PS Vita hardware testing
  actively underway.
- **Vita3K note:** Vita3K shows a black screen with vitaGL in this port; test on real PS Vita
  hardware.
- **Clocks:** The port sets CPU 444 MHz and GPU 222 MHz at boot (`VitaPlatform_Init`). Disable
  external overclock plugins that conflict with clock management.

## Repository layout

```text
build_all.bat/.ps1/.sh               one step from your dump to FireRedVita.vpk
CMakeLists.txt, build_vita.ps1/.sh   Vita build (preprocess -> compile -> link -> SELF -> VPK)
make_tools.mk, *_rules.mk, vita/     host tools, asset rules, link wrapper, neutral LiveArea art
src/, include/, asm/, data/,         pokefirered decompilation (game code and text data;
constants/                           graphics/, sound/ and data binaries come from fetch_assets)
fetch_assets.sh/.ps1, tools/assets/  game files download (checks your ROM, then the manifest)
src/platform/                        GBA hardware emulation, parallel scanline PPU, widescreen
src/platform/voxel/                  3D Kanto/Sevii voxel world, 3D rooms, and HD-2D shaders
src/platform/ui/                     native 960x544 16:9 touch UI (screens, battle HUD, themes)
src/platform/vita/                   Vita-specific layer (GameFrame, controls, extras, stage, saves)
assets/                              port data packed into the VPK (voxel models, HGSS followers)
tools/                               pret host tools, UI atlas builder (tools/ui2), follower packer,
                                     build checks (tools/core_boundary, tools/doc_check)
docs/                                documentation: guides (docs/guides) and reference (docs/reference)
```

## Build your own VPK

No builds are distributed: you make your own from your Pokémon FireRed cartridge. Full guide, with
every command and common errors: [docs/guides/BUILD_YOUR_OWN.md](docs/guides/BUILD_YOUR_OWN.md)
(en español: [docs/guides/BUILD_YOUR_OWN.es.md](docs/guides/BUILD_YOUR_OWN.es.md)).

You need a Windows 10/11 PC, your Pokémon FireRed dump (`.gba`, 16 MB), and a PS Vita with HENkaku
or Ensō. Nothing else has to be installed by hand:

1. **Get the repository** (`git clone`, or *Code > Download ZIP*) into a folder with no spaces or
   accents in its path and not synced by OneDrive, e.g. `C:\FireRed\pokefirered-vita`.
2. **Double-click `build_all.bat`** in that folder and choose your `.gba` in the window it opens
   (or run `powershell.exe -ExecutionPolicy Bypass -File .\build_all.ps1 -Rom <file>`). It installs
   what is missing ([MSYS2](https://www.msys2.org) in `C:\msys64`, its packages,
   [VitaSDK](https://vitasdk.org) with vitaGL and SDL2), checks your dump, downloads the game files
   from [pret/pokefirered](https://github.com/pret/pokefirered) verified against
   [tools/assets/manifest.txt](tools/assets/manifest.txt) (nothing is copied from the ROM), builds
   the host tools, converts assets, generates `assets/ui2/ui_atlas.bin`, and packages
   `FireRedVita.vpk`.
3. **Install `FireRedVita.vpk`** with VitaShell (*Enable unsafe homebrew* on in HENkaku settings).
   The game appears as **FireRed Vita** (title ID `PKFR00001`); saves and settings live in
   `ux0:data/FRVITA/`.

Do not share the VPK or the downloaded files: they contain Nintendo's game data.

## Controls

| Button | Action |
|---|---|
| Cross / Circle | A / B |
| D-pad or left stick | Move |
| L / R | L / R (Help System in field / tabs in UI) |
| START | Open or close the touch menu (field) |
| SELECT | Registered item (field) / Mega Evolution (battle) |
| Triangle | Fast forward |
| Square | Options |
| Touch | Menus, PC, Map, Battle action deck |

Buttons can be remapped in Options. Settings and saves live in `ux0:data/FRVITA/`
(see [docs/reference/RUNTIME_FILES.md](docs/reference/RUNTIME_FILES.md)).

## Documentation

| Document | Covers |
|---|---|
| [docs/guides/BUILD_YOUR_OWN.md](docs/guides/BUILD_YOUR_OWN.md) ([español](docs/guides/BUILD_YOUR_OWN.es.md)) | Make your own VPK from your cartridge, step by step |
| [docs/guides/BUILD.md](docs/guides/BUILD.md) | Toolchain, build steps, CMake options, build-time checks, installing |
| [docs/guides/ADDING_A_LANGUAGE.md](docs/guides/ADDING_A_LANGUAGE.md) | Languages: interface catalog, game text from an official ROM, layout patches |
| [docs/guides/DEBUGGING.md](docs/guides/DEBUGGING.md) | Logs, per-frame telemetry, thread violations, crash dumps |
| [docs/README.md](docs/README.md) | Index of the technical documentation |
| [docs/reference/ARCHITECTURE.md](docs/reference/ARCHITECTURE.md) | Game side and platform side, boot, frame loop, FireRed specifics |
| [docs/reference/THREADS.md](docs/reference/THREADS.md) | Cores, pacing, `GameFrame`, data ownership |
| [docs/reference/RENDERING.md](docs/reference/RENDERING.md) | Presentation paths, voxel passes, GBA overlay, GPU rules |
| [docs/reference/VITA_UI.md](docs/reference/VITA_UI.md) | Native 960×544 16:9 touch user interface |
| [docs/reference/VOXEL_ENGINE.md](docs/reference/VOXEL_ENGINE.md) | Kanto/Sevii 3D voxel overworld, 3D rooms, and HD-2D pipeline |
| [docs/reference/SCENES_16_9.md](docs/reference/SCENES_16_9.md) | How GBA scenes fill 16:9 and Group A 816×544 scenes |
| [docs/reference/ASSETS.md](docs/reference/ASSETS.md) | Where every data file comes from and how it is built |
| [docs/reference/RUNTIME_FILES.md](docs/reference/RUNTIME_FILES.md) | Files the game reads and writes on the memory card |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Code standards and workflow |
| [NOTICE.md](NOTICE.md) | Licences of the ported code and full third-party credits |

## Credits

- **pret** and contributors: the [pokefirered](https://github.com/pret/pokefirered) decompilation
  and its host tools (`preproc`, `gbagfx`, `aif2pcm`, `mid2agb`, `mapjson`, `jsonproc`, `scaninc`,
  `ramscrgen`, `rsfont`, `wav2agb`, `bin2c`, `gbafix`).
- **NachoGonzalez2001**: [Pokemon-FireRed-3DS](https://github.com/NachoGonzalez2001/Pokemon-FireRed-3DS),
  the source of the FireRed gameplay hooks and patches (Mega Evolution, Physical/Special move split,
  renewed & visible wild Pokémon, QoL extras), Mega Evolution item/FX data, and Kanto 3D voxel
  room/world generators.
- **Zallax** and the **Emerald 3DS** contributors:
  [Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds) and
  [pokeemerald-3Ds-dualscreen](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen), the 3D
  voxel overworld, lighting, 3D battles, 3D Fly, day/night, followers, and UI layout foundations.
- **marcoscodas**: [Bravely Emerald](https://github.com/marcoscodas/bravely-emerald), visual
  design and shader reference for the HD-2D post-processing mode, point lights, light shafts,
  foliage sway, tile relief, 25° camera tilt, and 3D Pokécenter heal balls.
- **gradenGnostic** and contributors:
  [pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform), the SDL2
  GBA hardware emulation layer, fast scanline PPU, and original voxel renderer foundation.
- **MinZe25**: [pokeemerald-rogue-vita](https://github.com/MinZe25/pokeemerald-rogue-vita),
  reference for low-level PS Vita techniques (boot-snapshot reset, link padding, DC-blocking audio
  filter, and renderer optimizations).
- **PokéCommunity DS-Style 64×64 sprite contributors**:
  [Gen VI DS-Style 64×64 Resource](https://www.pokecommunity.com/threads/gen-vi-ds-style-64x64-pokemon-sprite-resource.314422/)
  (Chaos Rush, MrDollSteak, Spherical Ice, leparagon, Cynda, Sky High, aXl, Bozster, Zerudez,
  MilanesaCosmic, Quanyails, Wobblebuns, BlackWhiteRobin, The Cynical Poet, Criminon, Fridave,
  HeXeR, Layell) and
  [Gen VII+ DS-Style 64×64 Repository](https://www.pokecommunity.com/threads/ds-style-gen-vii-and-beyond-pok%C3%A9mon-sprite-repository-in-64x64.368703/)
  (Axel_CodeInfinity, Lykeron, leParagon, Red7246, Alxndre~, Cesare_Cbass) for the fan-made Mega
  Evolution battle sprites.
- **pret/pokeheartgold** and **abcboy101** (**Poké Corpus**): follower Pokémon overworld sprite
  and message groundwork.
- **Rinnegatamante** ([vitaGL](https://github.com/Rinnegatamante/vitaGL)), the **VitaSDK**,
  **SDL**, and **Vita3K** teams, and **Niels Lohmann** (`nlohmann/json`).
- **HctrRB**: [pokeemerald-vita-3ds](https://github.com/HctrRB/pokeemerald-vita-3ds) and
  `pokefirered-vita` project lead, PS Vita multicore/HD-2D/UI architecture, supervision, and hardware
  testing.

## Legal

- **Intellectual Property:** Pokémon, Pokémon FireRed, Poké Ball, Kanto, Sevii Islands, and all
  related names, characters, sprites, artwork, music, sound effects, and text are trademarks and
  copyrights of **Nintendo Co., Ltd.**, **Creatures Inc.**, and **GAME FREAK inc.** All rights
  belong to their respective owners. This is a strictly non-commercial, fan-made educational and
  preservation project with no affiliation with, sponsorship by, or endorsement from Nintendo,
  Creatures Inc., or GAME FREAK inc.
- **No Copyrighted Game Media or ROMs Distributed:** This repository does not include any ROM,
  BIOS, or Nintendo game graphics, audio, or binary data. `fetch_assets` validates a dump of the
  user's own legitimately owned Pokémon FireRed cartridge locally and downloads the upstream
  decompilation media files from `pret/pokefirered` verified against
  [tools/assets/manifest.txt](tools/assets/manifest.txt); those files remain the property of their
  respective rights holders. The decompiled source and script text in this repository originate
  from the public `pret/pokefirered` decompilation.
- **No Binary Distribution:** Compiled builds (`.vpk`, `eboot.bin`, `pokefirered` ELF) bundle
  game code and assets derived from the user's build step. Do not upload, distribute, mirror, or
  sell compiled builds. Use this project solely with a legitimately owned copy of Pokémon FireRed.
- **Code Licensing:** Code licences differ by origin: **MIT** ([LICENSE](LICENSE)) for the
  original PS Vita platform code, multicore `GameFrame` framework, HD-2D shaders, native 16:9 UI,
  `pokeemerald-multiplatform` layer, and `Pokemon-FireRed-3DS` port code; **CC BY-NC-SA 4.0**
  (non-commercial, attribution, share-alike) for files derived from `Emerald 3DS` v0.5.0+. Upstream
  decompilation code and third-party libraries retain their own terms; see [NOTICE.md](NOTICE.md)
  for the complete breakdown.
- **Disclaimer & Takedown:** This software is provided *"as is"*, without warranty of any kind,
  express or implied. Rights holders may request modifications or removal at any time by opening an
  issue on this repository.
