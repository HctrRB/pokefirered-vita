#!/bin/bash
# Configures and builds build-vita/pokefirered.vpk with VitaSDK (run from MSYS2, or through
# build_vita.ps1 on Windows).
set -e
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:/usr/bin:/bin:$PATH

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# VITA_LOCALE from the environment: the build's language, a code of tools/locales/languages.json
# (build_vita.ps1 -Locale). Each language builds in its own folder, build-vita-<code>: the game's
# sources are preprocessed with the language's defines, which a folder never mixes.
VITA_LOCALE="${VITA_LOCALE:-en}"
if [ "$VITA_LOCALE" = "en" ]; then
    BUILD_DIR="${BUILD_DIR:-build-vita}"
else
    BUILD_DIR="${BUILD_DIR:-build-vita-$VITA_LOCALE}"
fi
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"
cd "$SCRIPT_DIR"

if [ ! -f tools/preproc/preproc ] && [ ! -f tools/preproc/preproc.exe ]; then
    echo "ERROR: Host tools missing. Run: make -f make_tools.mk -j8 CC=\"gcc -static\" CXX=\"g++ -static\""
    exit 1
fi
if [ ! -f charmap.txt ] || [ ! -f graphics_file_rules.mk ]; then
    echo "ERROR: Root pret/pokefirered charmap.txt or *_rules.mk files are missing."
    exit 1
fi
if [ ! -f include/constants/map_groups.h ]; then
    echo "ERROR: Generated game assets missing. Run: make -f vita/assets.mk -j8"
    exit 1
fi
if [ ! -f assets/ui2/ui_atlas.bin ]; then
    echo "=== Generating assets/ui2/ui_atlas.bin ==="
    PY=""
    for c in $(type -ap python3 python 2>/dev/null); do
        if "$c" -c "import PIL" 2>/dev/null; then PY="$c"; break; fi
    done
    if [ -z "$PY" ]; then
        echo "ERROR: assets/ui2/ui_atlas.bin is missing and Python 3 with Pillow was not found (pacman -S mingw-w64-x86_64-python-pillow)."
        exit 1
    fi
    "$PY" tools/ui2/build_atlas.py
fi

# A language whose game text comes from its official ROM (tools/locales/languages.json, "source":
# "rom") is built from a staging tree, the sources with that language's patches and text applied
# by tools/locales/localize.py: the repository's own sources always stay English.
# VITA_ROM is the player's ROM of that language (build_vita.ps1 -Rom). VITA_UI_ONLY=1
# (build_vita.ps1 -UiOnly) builds the translated native UI over the English game text instead: a
# development build for checking the UI on hardware before the game text is staged.
SRC_DIR="$SCRIPT_DIR"
PYTHON=python3; command -v python3 >/dev/null || PYTHON=python
TEXT_SOURCE="$("$PYTHON" -c "import json,sys; print(next((l['source'] for l in json.load(open('tools/locales/languages.json', encoding='utf-8'))['languages'] if l['code'] == sys.argv[1]), 'unknown'))" "$VITA_LOCALE")"
if [ "$TEXT_SOURCE" = "unknown" ]; then
    echo "ERROR: VITA_LOCALE '$VITA_LOCALE' is not in tools/locales/languages.json"
    exit 1
fi
if [ "$TEXT_SOURCE" = "rom" ]; then
    if [ "${VITA_UI_ONLY:-0}" = "1" ]; then
        echo "=== '$VITA_LOCALE' UI-only development build: the game's own text stays English ==="
    elif [ -z "${VITA_ROM:-}" ] || [ ! -f "$VITA_ROM" ]; then
        echo "ERROR: the '$VITA_LOCALE' build takes its game text from your ROM of that language: set VITA_ROM (build_vita.ps1 -Rom <file>),"
        echo "       or VITA_UI_ONLY=1 (build_vita.ps1 -UiOnly) for the translated interface over English game text."
        exit 1
    else
        "$PYTHON" tools/locales/localize.py --locale "$VITA_LOCALE" --rom "$VITA_ROM" --stage "$BUILD_DIR/stage"
        SRC_DIR="$SCRIPT_DIR/$BUILD_DIR/stage"
        EXTRA_CMAKE_ARGS="${EXTRA_CMAKE_ARGS:-} -DVITA_DOC_CHECK=OFF"
    fi
fi

# A non-English build's UI atlas carries its language's letters (tools/ui2/build_atlas.py --locale)
if [ "$VITA_LOCALE" != "en" ]; then
    PYPIL=""
    for c in $(type -ap python3 python 2>/dev/null) /mingw64/bin/python3; do
        if [ -x "$c" ] && "$c" -c "import PIL" 2>/dev/null; then PYPIL="$c"; break; fi
    done
    if [ -z "$PYPIL" ]; then
        echo "ERROR: a non-English build needs Python 3 with Pillow (pacman -S mingw-w64-x86_64-python-pillow)"
        exit 1
    fi
    mkdir -p "$BUILD_DIR/ui2"
    "$PYPIL" tools/ui2/build_atlas.py --locale "$VITA_LOCALE" --out "$BUILD_DIR/ui2/ui_atlas.bin"
fi

# Drop stale CMake cache if created from a different source tree (checkout path or staging tree).
if [ -f "$CACHE_FILE" ]; then
    CACHED_SOURCE="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE_FILE")"
    if [ "$CACHED_SOURCE" != "$SRC_DIR" ]; then
        echo "=== Stale CMake cache (created in '$CACHED_SOURCE'), reconfiguring ==="
        rm -rf "$CACHE_FILE" "$BUILD_DIR/CMakeFiles"
    fi
fi

echo "=== Configuring CMake in $SCRIPT_DIR/$BUILD_DIR ==="
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DVITA_LOCALE="$VITA_LOCALE" \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
    ${EXTRA_CMAKE_ARGS:-}

JOBS="$(nproc 2>/dev/null || echo 4)"
echo "=== Building pokefirered.vpk (-j$JOBS) ==="
cmake --build "$BUILD_DIR" -j"$JOBS"
