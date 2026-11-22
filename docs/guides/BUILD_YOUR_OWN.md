# Build your own VPK

Type: guide · Audience: players · Status: current
Verified against: `pokefirered-vita` source snapshot

> **Experimental version in active development.** Please report the bugs you find (see the end of
> this guide).

This repository does not contain the game's graphics, sounds, or binary data, and it does not
distribute builds. You make your own VPK from your own copy of the game:

1. You need a dump of **your Pokémon FireRed cartridge** (`.gba`, 16 MB). The script reads its
   header locally only to verify that it is Pokémon FireRed; nothing is copied from your ROM or
   uploaded anywhere.
2. The script then downloads the game files of the
   [pret/pokefirered](https://github.com/pret/pokefirered) decompilation and checks every file
   against `tools/assets/manifest.txt`.
3. It builds the tools, converts the assets, generates `assets/ui2/ui_atlas.bin`, and builds
   `FireRedVita.vpk` so you can install it on your PS Vita.

Do not share the VPK or the downloaded files: they contain Nintendo's game data.

Spanish version: [BUILD_YOUR_OWN.es.md](BUILD_YOUR_OWN.es.md).

## What you need

- A Windows 10 or 11 PC with an internet connection and about 5 GB free.
- Your Pokémon FireRed dump (`.gba`, 16 MB).
- A PS Vita with HENkaku or Ensō and VitaShell.

Everything else (MSYS2, VitaSDK, Python, Git) is installed by the script.

## 1. Get the repository

Either clone it with Git:

```powershell
git clone https://github.com/HctrRB/pokefirered-vita.git C:\FireRed\pokefirered-vita
```

or, without Git, use **Code > Download ZIP** on GitHub and extract it.

Put it in a folder whose path has **no spaces or accents**, for example
`C:\FireRed\pokefirered-vita`. The build tools cannot handle them (the script checks and stops),
so `C:\Users\Your Name\Downloads\...` will not work. Avoid folders that OneDrive syncs (often
*Documents* and *Desktop*): the build writes thousands of files and the sync can lock them.

## 2. Build

Double-click **`build_all.bat`** in the repository folder. A window asks for your `.gba`, and the
console window stays open at the end with the result. (If Windows warns that the file comes from
the internet, choose *More info > Run anyway*.)

Or, from PowerShell in the repository folder:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\build_all.ps1 -Rom "C:\path\to\firered.gba"
```

Any ROM path works, spaces included; without `-Rom` the window asks for it.

The script:

1. installs MSYS2 in `C:\msys64` if it is missing, and the packages the build needs;
2. installs VitaSDK in MSYS2 (`/usr/local/vitasdk`) and the libraries the game uses (vitaGL,
   SDL2) if they are missing;
3. checks your dump and downloads the game files listed in `tools/assets/manifest.txt`;
4. builds the host tools, converts the game files, builds `assets/ui2/ui_atlas.bin`, and builds the
   VPK.

The first run downloads 1–2 GB and can take 20–40 minutes; later runs take a few minutes. The
result is **`FireRedVita.vpk`** in the repository folder. Every step that is already done is
skipped, so you can run the script again after a failure.

**Updating:** `git pull` (or download the ZIP again over the same folder) and run `build_all.bat`
again. The ROM is only asked for the first time: when a new version needs other game files, the
script downloads them by itself.

## 3. Install

1. Copy `FireRedVita.vpk` to the Vita (USB or FTP with VitaShell).
2. In VitaShell, press X on the VPK and install it (*Enable unsafe homebrew* must be on in
   HENkaku settings).
3. The game shows up as **FireRed Vita**. Saves and settings live in `ux0:data/FRVITA/`.

Your own LiveArea art can go in `vita/sce_sys_custom/` with the same file names as
`vita/sce_sys/` (8-bit indexed PNGs); it is used instead of the neutral images.

## Common errors

| Message | Fix |
|---|---|
| `This folder's path has spaces or special characters` | Move the repository folder to a simple path such as `C:\FireRed\pokefirered-vita`. |
| `Permission denied` / `Device or resource busy` while building | Often OneDrive or an antivirus locking files: move the folder out of OneDrive, or run the script again. |
| `Could not download MSYS2` / `MSYS2 did not install` | Install MSYS2 by hand from [msys2.org](https://www.msys2.org) in `C:\msys64`, then run the script again. |
| `not a Pokémon FireRed dump` | The file is not a Pokémon FireRed ROM, or it is damaged. It must be 16 MB. |
| `VitaSDK did not install` / `... is still missing after vdpm install` | Usually a download that failed: run the script again. Otherwise see [BUILD.md](BUILD.md#toolchain-requirements-windows--msys2). |
| `running scripts is disabled on this system` | Start it exactly as shown above (`powershell.exe -ExecutionPolicy Bypass -File ...`). |

## Building by hand

The script runs these steps; you can run them yourself in the *MSYS2 MINGW64* shell, from the
repository folder (`cd /c/FireRed/pokefirered-vita`):

1. Packages and VitaSDK, once (the exact commands are in
   [BUILD.md](BUILD.md#toolchain-requirements-windows--msys2)).
2. The game files, once:
   ```sh
   ./fetch_assets.sh /c/path/to/firered.gba
   ```
3. The tools, converted game files, and UI atlas:
   ```sh
   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"
   make -f vita/assets.mk -j8
   python3 tools/ui2/build_atlas.py
   ```
4. The VPK: `./build_vita.sh` -> `build-vita/pokefirered.vpk`.

## Reporting bugs

This port is an **experimental work in progress**. When something goes wrong, open an issue on
GitHub with the *Bug report* template and include:

- what you were doing and what happened (a photo of the screen helps);
- the files `perf_summary.txt`, `perf_frames.csv`, and any `*.log` from `ux0:data/FRVITA/`;
- for a crash, the `.psp2dmp` file from `ux0:data/` (see [DEBUGGING.md](DEBUGGING.md));
- the commit you built (`git log -1 --oneline`);
- if the build failed, the last lines the script printed.
