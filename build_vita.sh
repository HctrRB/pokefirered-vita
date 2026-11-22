#!/bin/bash
# Configures and builds build-vita/pokefirered.vpk with VitaSDK (run from MSYS2, or through
# build_vita.ps1 on Windows).
set -e
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:/usr/bin:/bin:$PATH

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-build-vita}"
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

# Drop stale CMake cache if created from a different checkout path.
if [ -f "$CACHE_FILE" ]; then
    CACHED_SOURCE="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE_FILE")"
    if [ "$CACHED_SOURCE" != "$SCRIPT_DIR" ]; then
        echo "=== Stale CMake cache (created in '$CACHED_SOURCE'), reconfiguring ==="
        rm -rf "$CACHE_FILE" "$BUILD_DIR/CMakeFiles"
    fi
fi

echo "=== Configuring CMake in $SCRIPT_DIR/$BUILD_DIR ==="
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
    ${EXTRA_CMAKE_ARGS:-}

JOBS="$(nproc 2>/dev/null || echo 4)"
echo "=== Building pokefirered.vpk (-j$JOBS) ==="
cmake --build "$BUILD_DIR" -j"$JOBS"
