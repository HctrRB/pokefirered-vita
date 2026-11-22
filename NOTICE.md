# Notices

This repository combines the `pret/pokefirered` decompilation, code adapted from earlier ports,
original PS Vita platform code, and third-party libraries and tools. Each component retains its
own terms. [LICENSE](LICENSE) is the MIT licence and covers only the parts named below as MIT.

## Game content

Pokémon, Pokémon FireRed, and all related code, data, graphics, audio, characters, and text are
© Nintendo, Creatures Inc., and GAME FREAK inc. The decompiled game material in this tree
(`src/` outside `src/platform/`, `include/` outside `include/platform/`, `asm/`, `constants/`,
`data/`, `graphics/`, `sound/`) is not licensed by this project. This repository does not include
the game's graphics, sounds, or binary data, nor any ROM; built VPKs contain compiled game assets:
**do not distribute builds**.

## Upstream decompilation

- **pret/pokefirered** — <https://github.com/pret/pokefirered> (FireRed Rev 1 base, pinned at
  commit `037335f4c725d7c9aecdac87066f2002b4bd7e14`). The upstream repository does not state an
  open-source licence; this project does not relicense it.

## Code derived from other ports

| Component | Source | Licence | Used in |
|---|---|---|---|
| **pokeemerald-multiplatform** | <https://github.com/gradenGnostic/pokeemerald-multiplatform> (`db1cab3`) | MIT ([LICENSE](LICENSE)) | GBA software PPU and hardware emulation layer (`src/platform/gba_fast_draw*.c`, `widescreen.c`, `bios.c`, `dma.c`, `cgb_audio.c`) and the original voxel renderer logic (map instances, metatile classification, atlas composition, follow camera) |
| **Pokemon-FireRed-3DS** | <https://github.com/NachoGonzalez2001/Pokemon-FireRed-3DS> (`7e957ef` through `2d7c8ec`, v0.5.2) | MIT (per its `NOTICE.md`, derived from `ZallaxDev/pokeemerald-3Ds-dualscreen` commit `d724c5d` and `pokeemerald-multiplatform`) | FireRed gameplay hooks and patches (`0003`..`0030`: diagonal/free movement, QoL extras, GBA save layout, Mega Evolution, Renewed & Visible Wild Pokémon, Physical/Special Move Split), Mega Evolution art/item icons/FX (`src/platform/fr3ds/fr_mega_*.c`, `fr_extras.c`, `fr_ui_icons.h`), and Kanto 3D voxel room logic & generators (`voxel_room.c/h`, `voxel_follower.c/h`, Kanto building/relief/tree assets in `assets/voxel/`) |
| **pokeemerald-vita-3ds** | <https://github.com/HctrRB/pokeemerald-vita-3ds> | MIT for original PS Vita platform, `GameFrame` multicore framework, HD-2D vitaGL shaders, Native 16:9 UI (`src/platform/ui/`), and tooling (`tools/core_boundary/`, `tools/doc_check/`, `tools/ui2/`); **CC BY-NC-SA 4.0** for files derived from `ZallaxDev/emerald-3ds` v0.5.0+ | PS Vita multicore architecture (`GameFrame`, `GameThread`, `vita_reset` `.noinit` + barrier, Core 2 `DrawWorker`s + NEON, `VoxelChunkWorker`, `VoxelBuildingIoWorker`), vitaGL 3D & HD-2D post-processing pipeline (`src/platform/voxel/voxel_hd2d_shader.c`, `voxel_grade.c`, `voxel_lamps.c`, `voxel_pointlights.c`), Native 960×544 16:9 Touch UI (`src/platform/ui/`, `tools/ui2/`), and build verification gates |
| **Pokémon Emerald 3Ds Dual Screen / Emerald 3DS** | <https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen> (`d724c5d`, MIT) / <https://github.com/ZallaxDev/emerald-3ds> (v0.5.0, **CC BY-NC-SA 4.0**) | MIT (`d724c5d`) / **CC BY-NC-SA 4.0** (v0.5.0+) | Upstream 3DS dual-screen and voxel groundwork on which both `Pokemon-FireRed-3DS` and `pokeemerald-vita-3ds` stand. Files derived from `emerald-3ds` v0.5.0+ carry **CC BY-NC-SA 4.0** (<https://creativecommons.org/licenses/by-nc-sa/4.0/>): attribution to Zallax and Emerald 3DS contributors, **non-commercial use only**, and share-alike |
| **Bravely Emerald** (reference) | <https://github.com/marcoscodas/bravely-emerald> | — | Visual design and shader reference for the PS Vita **HD-2D** mode (tilt-shift depth of field, bloom, split-tone grading, point lights, light shafts, fireflies, foliage sway, 16×16 Sobel tile relief, 25° camera tilt, and 3D Pokécenter healing balls) |
| **pokeemerald-rogue-vita** (reference) | <https://github.com/MinZe25/pokeemerald-rogue-vita> | — | Reference for low-level Vita techniques: resetting the game from a boot snapshot (`vita_reset.c`), link padding, DC-blocking audio filter, and renderer optimizations |

## Mega Evolution and Follower Pokémon assets

- **Mega Evolution battle sprites** (`src/platform/fr3ds/fr_mega_art.c`): generated from the
  [Gen VI: DS-Style 64x64 Pokémon Sprite Resource](https://www.pokecommunity.com/threads/gen-vi-ds-style-64x64-pokemon-sprite-resource.314422/)
  (continuing Chaos Rush's resource; credits: MrDollSteak, Spherical Ice, leparagon, Cynda,
  Sky High, aXl, Bozster, Zerudez, MilanesaCosmic, Quanyails, Wobblebuns, BlackWhiteRobin,
  The Cynical Poet, Criminon, Fridave, HeXeR, Layell) and the
  [DS-style Gen VII and Beyond Pokémon Sprite Repository in 64x64](https://www.pokecommunity.com/threads/ds-style-gen-vii-and-beyond-pok%C3%A9mon-sprite-repository-in-64x64.368703/)
  for Pokémon Legends: Z-A Mega Evolutions (credits: Axel_CodeInfinity, Lykeron, leParagon,
  Red7246, Alxndre~, Cesare_Cbass). Free for fan projects with credit to those threads.
- **Follower Pokémon assets** (`assets/followers/`): the overworld sprites and messages come from
  `pret/pokeheartgold` (commit `3291d88a7e3e264b503024ead80f4d8b55b835cd`) and Poké Corpus by
  abcboy101 (commit `5d1da078d70dea5c9feaaba7575626eab4f10a08`), through the forks listed in
  `assets/followers/sources.txt`, packed into `followers.pak` by `tools/pack_followers.py`. They
  are Nintendo, Creatures Inc., and GAME FREAK inc.'s work and are not relicensed.

## Original PS Vita code

Original PS Vita port code written for this project and `pokeemerald-vita-3ds` that is not derived
from `emerald-3ds` v0.5.0+ (the VitaSDK/vitaGL backend, build/asset scripts, telemetry, multicore
`GameFrame` pipeline, HD-2D shaders, and Native 960×544 16:9 UI) is offered under the MIT licence
in [LICENSE](LICENSE).

## Third-party libraries and tools

| Component | Licence | Use |
|---|---|---|
| VitaSDK toolchain, newlib, libraries | Various open source | Cross-compilation and runtime |
| vitaGL (Rinnegatamante) | LGPL-2.1 | OpenGL over `SceGxm` |
| SDL2 | zlib | Audio output, threading primitives, high-resolution timer |
| nlohmann/json (Niels Lohmann), in `tools/jsonproc/` | MIT | Host tool (`jsonproc`) |
| pret host tools (`tools/preproc`, `gbagfx`, `aif2pcm`, `mid2agb`, `mapjson`, `jsonproc`, `ramscrgen`, `scaninc`, `rsfont`, `wav2agb`, `bin2c`, `gbafix`) | pret / various open source | Asset conversion at build time |
