#!/bin/bash
# Gets the game's graphics, sounds and binary data, which this repository does not carry.
# Run it once before the first build:
#
#     ./fetch_assets.sh /path/to/your/pokemon_firered.gba
#
# 1. Checks that the file is a Pokémon FireRed cartridge dump (any language): size, title, game
#    code and header checksum. The ROM is only read for this check; nothing is copied from it,
#    stored or uploaded.
# 2. Downloads the files listed in tools/assets/manifest.txt from the public pret/pokefirered
#    decompilation at a fixed commit, and checks each one against the hash in the manifest.
# 3. Generates assets/ui2/ui_atlas.bin if the converted region map tiles already exist; otherwise
#    build_all.sh / build_vita.sh generates it automatically right after make -f vita/assets.mk.
#
# Needs: git, and Python 3 with Pillow (pip install pillow).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

SRC_URL="https://github.com/pret/pokefirered.git"
SRC_COMMIT="037335f4c725d7c9aecdac87066f2002b4bd7e14"
MANIFEST="tools/assets/manifest.txt"
WORK=".assets_src"
FIRERED_US11_SHA1="dd5945db9b930750cb39d00c84da8571feebf417"
FIRERED_US10_SHA1="41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc"

die() { echo "ERROR: $*" >&2; exit 1; }

ROM="${1:-}"
[ -n "$ROM" ] || die "usage: ./fetch_assets.sh <path to your Pokémon FireRed .gba dump>
       (or ./fetch_assets.sh --update, after a first run, to get the files a new version needs)"

# ---------------------------------------------------------------- 1. the ROM
if [ "$ROM" = "--update" ]; then
    # The dump was checked by an earlier run; .assets_ok holds its game code
    [ -s .assets_ok ] || die "--update needs a first run with your Pokémon FireRed dump"
    code=$(cat .assets_ok)
    echo "=== Updating the game files (dump checked before: $code) ==="
else
[ -f "$ROM" ] || die "'$ROM' does not exist"
echo "=== Checking the ROM ==="
size=$(wc -c < "$ROM" | tr -d ' ')
[ "$size" -eq 16777216 ] || die "not a Pokémon FireRed dump (size $size bytes, expected 16 MB)"
title=$(dd if="$ROM" bs=1 skip=160 count=12 2>/dev/null | tr -d '\000')
code=$(dd if="$ROM" bs=1 skip=172 count=4 2>/dev/null)
[ "$title" = "POKEMON FIRE" ] || die "not a Pokémon FireRed dump (title '$title')"
case "$code" in BPR?) ;; *) die "not a Pokémon FireRed dump (game code '$code')" ;; esac
sum=0
for b in $(od -An -tu1 -j160 -N29 "$ROM"); do sum=$((sum + b)); done
expected=$(( (0 - (sum + 25)) & 255 ))
stored=$(od -An -tu1 -j189 -N1 "$ROM" | tr -d ' ')
[ "$expected" -eq "$stored" ] || die "the ROM header is damaged (checksum)"
sha=$( (sha1sum "$ROM" 2>/dev/null || shasum "$ROM") | cut -d' ' -f1)
if [ "$sha" = "$FIRERED_US11_SHA1" ]; then
    echo "Pokémon FireRed ($code), USA/Europe 1.1: verified"
elif [ "$sha" = "$FIRERED_US10_SHA1" ]; then
    echo "Pokémon FireRed ($code), USA/Europe 1.0: verified"
else
    echo "Pokémon FireRed ($code): header verified"
fi
fi

# ---------------------------------------------------------------- 2. the decompilation's files
echo "=== Downloading the game files (pret/pokefirered ${SRC_COMMIT:0:7}) ==="
command -v git >/dev/null || die "git is needed (MSYS2: pacman -S git)"
if [ ! -d "$WORK/.git" ]; then
    git clone --quiet --filter=blob:none --no-checkout "$SRC_URL" "$WORK"
fi
if ! git -C "$WORK" cat-file -e "$SRC_COMMIT^{commit}" 2>/dev/null; then
    git -C "$WORK" fetch --quiet --filter=blob:none origin "$SRC_COMMIT"
fi
# (MSYS_NO_PATHCONV: MSYS2 / Git Bash would turn "/graphics/" into a Windows path)
MSYS_NO_PATHCONV=1 git -C "$WORK" sparse-checkout set --no-cone /graphics/ /sound/ /data/
git -C "$WORK" checkout --quiet --force "$SRC_COMMIT"

echo "=== Checking them against $MANIFEST ==="
git -C "$WORK" ls-tree -r "$SRC_COMMIT" -- graphics sound data \
    | awk -F'\t' '{ split($1, a, " "); print a[3] "  " $2 }' > "$WORK/.tree"
bad=$(awk 'NR == FNR { t[$2] = $1; next } t[$2] != $1 { print $2 }' "$WORK/.tree" "$MANIFEST" | head -5)
[ -z "$bad" ] || die "files missing or different in the download, e.g.: $bad"

echo "=== Copying $(wc -l < "$MANIFEST" | tr -d ' ') files ==="
awk '{ print $2 }' "$MANIFEST" > "$WORK/.list"
(cd "$WORK" && tar cf - -T .list) | tar xf -

# ---------------------------------------------------------------- 3. port files made from them
if [ -f graphics/region_map/region_map.4bpp ]; then
    echo "=== Generating the UI atlas ==="
    PY=""
    for c in $(type -ap python3 python 2>/dev/null); do
        if "$c" -c "import PIL" 2>/dev/null; then PY="$c"; break; fi
    done
    [ -n "$PY" ] || die "Python 3 with Pillow is needed (MSYS2: pacman -S mingw-w64-x86_64-python-pillow, or pip install pillow)"
    "$PY" tools/ui2/build_atlas.py
else
    echo "=== Note: assets/ui2/ui_atlas.bin will be generated after make -f vita/assets.mk ==="
fi

echo "$code" > .assets_ok
if [ "$ROM" != "--update" ]; then
    echo "$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")" > .assets_rom
fi
# Which manifest these files match: build_all.sh fetches again (--update) when it changes
(sha1sum "$MANIFEST" 2>/dev/null || shasum "$MANIFEST") | cut -d' ' -f1 > .assets_manifest
echo "=== Done: you can build now (build_all.sh or make -f vita/assets.mk && build_vita.sh) ==="
