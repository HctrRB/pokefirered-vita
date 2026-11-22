# Building

Type: guide · Audience: developers · Status: current (default, `-Probe` and `-Locale es` builds)
Verified against: default, `-Probe` and `-Locale es -Rom` builds, `core_boundary` & `doc_check` (2026-11-23)

> **Phase F6 build status:** Root `CMakeLists.txt` drives Modern target-based Vita builds with
> `VITA_CORE_BOUNDARY=ON`, `VITA_PARALLEL_DRAW=ON` (Core 2 `DrawWorkerMain` + NEON, prioritized
> at `144`), `VITA_VOXEL_3D=ON` (`-DCTR_VOXEL_ENABLED=1 -DVOXEL_SUPPORTED`, `--require-voxel-workers`
> across Core 2 `VoxelChunkWorkerMain` `176` & `VoxelBuildingIoWorkerMain` `180`), and
> `VITA_DOC_CHECK=ON`, producing both `build-vita/pokefirered.vpk` (`7.80 MiB`) and
> `build-vita-probe/pokefirered.vpk` (`7.81 MiB`, `build_vita.ps1 -Probe`) with `app0:voxel/`
> datasets cleanly (pending real PS Vita hardware smoke/bench tests).

The repository path must not contain spaces or non-ASCII accents (`make` and decompilation rules
cannot handle them cleanly), e.g. `C:\FireRed\pokefirered-vita`.

If you are building from a fresh GitHub clone (which excludes copyrighted game media), first run
`./fetch_assets.sh /path/to/firered.gba` (or `build_all.bat` on Windows) to populate `graphics/`,
`sound/`, and `data/` binaries verified against `tools/assets/manifest.txt`.

## Toolchain requirements (Windows + MSYS2)

Matching the proven `pokeemerald-vita-3ds` setup:

1. **MSYS2 (`C:\msys64`):** install build packages (`make`, `git`, `cmake`, `tar`, `python`,
   `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-python`, `mingw-w64-x86_64-python-pillow`, etc.).
2. **VitaSDK `2026.08` + `vdpm` packages:**
   ```sh
   export VITASDK=/usr/local/vitasdk PATH=/usr/local/vitasdk/bin:$PATH VITASDK_CHANNEL=2026.08
   VDPM_NONINTERACTIVE=1 vdpm install vitaGL sdl2
   ```
   Verify with `arm-vita-eabi-gcc --version` and `vdpm list` (`vitaGL` pulls `vitashark`,
   `libmathneon`, `SceShaccCgExt`, and `taihen`).

## Observed Phase F6 build configuration (`build-vita/` & `build-vita-probe/`)

The Phase F6 default build (`VITA_VOXEL_3D=ON`) and `-Probe` build (`-DVITA_GPU_PROBE=ON`) produce:
- App metadata: Title ID `PKFR00001`, Name `"FireRed Vita"`, Version `"00.10"`, `VITA_MKSFOEX_FLAGS`
  including `-d ATTRIBUTE2=12` (extended user memory).
- Split compile targets:
  - `game_side` -> relocatable `game_combined.o` (renaming `.data` -> `gamedata` `0x000083` B, `.bss` ->
    `gamebss` `0x0d52e4` B for `vita_reset.c`).
  - `generated/game_data.o` -> assets object file (`7,355,548` B / `7.01 MiB` with `.rodata` renamed `.data` by
    CMake `arm-vita-eabi-objcopy` for relocatable pointers), linked alongside `vita/vita_link.sh`
    segment gap padding.
  - Default `build-vita/pokefirered` (`.text` `0x316968` B, `.rodata` `0x500634` B, `.bss` `0x1159e78` B, `.noinit` `0x000004` B) -> `pokefirered.velf` -> `eboot.bin`
    (`7,527,776` B) -> `pokefirered.vpk` (`8,179,129` B, `7.80 MiB`).
  - Probe `build-vita-probe/pokefirered` (`.text` `0x31950c` B, `.rodata` `0x5026ec` B, `.bss` `0x116c7b8` B, `.noinit` `0x000004` B) -> `eboot.bin` (`7,541,360` B) -> `pokefirered.vpk` (`8,192,749` B, `7.81 MiB`).
- Active defines: `-DNONMATCHING -DPORTABLE -DPORT_BRIDGE -DPLATFORM_VITA -DRENDERER_FAST_DRAW -DMODERN=1 -DUBFIX -DCTR_VOXEL_ENABLED=1 -DVOXEL_SUPPORTED -DVITA_PARALLEL_DRAW -DFIRERED -DREVISION=1 -DENGLISH`.

## Build workflow (Phase F1 active)

With `charmap.txt`, the 6 `*_rules.mk` files, the restored `asm/` macro includes, `make_tools.mk`,
`CMakeLists.txt`, `build_vita.ps1`, and `build_vita.sh` in place:

1. **Host tools** (MINGW64 shell, once):
   ```sh
   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"
   ```
2. **Game assets** (MINGW64 shell, once and after editing `graphics/`, `sound/`, `data/`):
   ```sh
   make -f vita/assets.mk -j8
   ```
3. **VPK** (PowerShell from repository root):
   ```powershell
   powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1
   powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1 -Probe   # Phase F6 GPU probe & automatic bench build
   ```
   Output: `build-vita/pokefirered.vpk` (alongside unstripped `build-vita/pokefirered` ELF for
   crash decoding, and planned `build-vita-probe/pokefirered.vpk` for Phase F6 GPU benchmark
   sessions).

### Languages (`-Locale`, [ADDING_A_LANGUAGE.md](ADDING_A_LANGUAGE.md))

One build = one language. `tools/locales/languages.json` is the only list of languages; the
repository sources always stay English, and each language builds in its own `build-vita-<code>/`.

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1 -Locale es -UiOnly   # Spanish native UI over English game text (dev)
powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1 -Locale es -Rom <your BPRS .gba>   # full Spanish build (game text from your ROM)
```

A non-English build writes its atlas with `tools/ui2/build_atlas.py --locale <code> --out
build-vita-<code>/ui2/ui_atlas.bin` (needs Pillow), compiles `tools/locales/<code>/ui.json` into
`ui2/locale.bin` with `tools/locales/compile_ui.py` (stops on printf-conversion mismatches, letters
missing from `languages.json`, or hash collisions; writes `locale.bin.txt` with missing and
much-wider strings), and passes `-DGAME_LANGUAGE`, `-DUI_LOCALE_CATALOG` and, for metric
languages, `-DUI_METRIC_UNITS`. After changing any `UI_TR` / `UI_N` string, run
`python tools/locales/extract_ui.py` to refresh `tools/locales/en/ui.json`. With `-Rom`,
`build_vita.sh` first stages the sources into `build-vita-<code>/stage/` with
`tools/locales/localize.py` (ROM SHA-1 checked, manifest spans and graphics replaced, patches
applied) and configures CMake there with `-DVITA_DOC_CHECK=OFF`. See
[ADDING_A_LANGUAGE.md](ADDING_A_LANGUAGE.md).

### CMake options & build-time gates (F1 active + F3–F7 targets)

| Option / Gate | Purpose | Default / Status |
|---|---|---|
| `-DVITA_LOGGING=ON/OFF` | Buffered `VITA_LOG` to `ux0:data/FRVITA/debug.log` | `OFF` (active) |
| `-DVITA_TELEMETRY=ON/OFF` | Buffered `VitaPerf` CSV/summary (`perf_frames.csv`, `perf_summary.txt`, `ui_stall.log`) | `OFF` (active option; F2a `.bss` guard) |
| `-DVITA_GPU_PROBE=ON/OFF` | Scene-boundary `glFinish` GPU timers + automatic `OFF/LOW/MED/HIGH` HD-2D pass benchmark (`build_vita.ps1 -Probe`, implies `VITA_TELEMETRY`) | `OFF` (enabled via `-Probe` in Phase F6) |
| `-DVITA_PARALLEL_DRAW=ON/OFF` | Offload scanline PPU + NEON BGR555->RGB565 to Core 2 workers | `ON` (active from Phase F4) |
| `-DVITA_VOXEL_3D=ON/OFF` | Enable 3D Voxel overworld renderer, `app0:voxel/` packaging & Core 2 voxel workers (`VOXEL_SUPPORTED`, `CTR_VOXEL_ENABLED=1`) | `ON` (active from Phase F5a–F5b) |
| `tools/core_boundary/check_core_boundary.py --strict` | Phase-aware check proving Core 0 / Core 2 never call `game_combined.o` or access `gamedata`/`gamebss` | Mandatory on build (`VITA_CORE_BOUNDARY=ON`, `--require-draw-workers` F4+, `--require-voxel-workers` F5a+) |
| `tools/doc_check/check_docs.py --strict` | Verify doc links, cited files/symbols, headers, zero `file:line` in reference/guides | Mandatory on build (`VITA_DOC_CHECK=ON`) |
| `-DVITA_LOCALE=<code>` | Build language from `tools/locales/languages.json` (`GAME_LANGUAGE`, UI catalog, locale atlas, units); set by `build_vita.ps1 -Locale` | `en` |
| `tools/locales/extract_ui.py --check` | `tools/locales/en/ui.json` matches every `UI_TR` / `UI_N` literal in `src/platform/` | Mandatory on build (`ui_strings` target) |

CMake discipline (enforced from Phase F1):
- `CMAKE_TOOLCHAIN_FILE` set **before** `project()`.
- `-fno-builtin` applied strictly to `game_side` (never to `pokefirered` platform code, restoring GCC math/copy builtins in `src/platform/`).
- `-O2 -fno-section-anchors -Wall -Wextra` on `pokefirered` platform code so `check_core_boundary.py` resolves static symbol tables accurately (`bash -eo pipefail` also wraps `DATA_ASM`/`SONG_ASM` preprocessing rules so missing `.inc` files abort loudly instead of truncating `event_scripts.s.o`).
- Game C preprocessing rules generate `DEPFILE` metadata with `-MT 'generated/${RELATIVE_SOURCE}'` so header edits trigger incremental rebuilds under MSYS2/Make, and write generated outputs (`game_data.o`, `game_combined.o`, preprocessed `.c` / generated special-ID include from `data/specials.inc`) via temporary `.tmp` + rename.
- No duplicate `-O3 ... -O2` optimization flag conflicts.
