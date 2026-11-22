#!/bin/bash
# One step from your Pokémon FireRed dump to an installable VPK (run in MSYS2, or through
# build_all.ps1 on Windows):
#
#     ./build_all.sh /path/to/your/firered.gba
#
# Installs what is missing (MSYS2 packages with Python and Pillow, VitaSDK with vitaGL and
# SDL2), gets the game files (fetch_assets.sh: the first time with the dump, and again without
# it when a new version's manifest asks for other files), builds the host tools, converts the
# game files, rebuilds the interface atlas (assets/ui2/ui_atlas.bin), builds the VPK and
# copies it to ./FireRedVita.vpk.
#
#     ./build_all.sh --missing-packages   exits 1 when MSYS2 packages are missing (build_all.ps1)
#
# Experimental work in progress: please report bugs as GitHub issues.
set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
ROM="${1:-}"
# build_all.ps1 passes the ROM's Windows path in BUILD_ALL_ROM
if [ -z "$ROM" ] && [ -n "${BUILD_ALL_ROM:-}" ]; then
    ROM="$(cygpath -u "$BUILD_ALL_ROM" 2>/dev/null || echo "$BUILD_ALL_ROM")"
fi
JOBS=$(nproc 2>/dev/null || echo 4)

step() { echo; echo "=== $* ==="; }
die() { echo "ERROR: $*" >&2; exit 1; }

rotate_msys_mirrors() {
    for ml in /etc/pacman.d/mirrorlist.*; do
        [ -f "$ml" ] || continue
        awk '
            !moved && /^Server[[:space:]]*=/ { first = $0; moved = 1; next }
            { print }
            END { if (moved) print first }
        ' "$ml" > "$ml.tmp" && mv "$ml.tmp" "$ml"
    done
}

pacman_with_retry() {
    local attempt=1 max_attempts=4
    while [ "$attempt" -le "$max_attempts" ]; do
        if pacman "$@"; then
            return 0
        fi
        if [ "$attempt" -eq "$max_attempts" ]; then
            return 1
        fi
        echo "pacman failed (attempt $attempt/$max_attempts); rotating MSYS2 mirror and retrying..."
        rotate_msys_mirrors
        pacman -Syy --noconfirm || true
        attempt=$((attempt + 1))
    done
}

export VITASDK=/usr/local/vitasdk
export VITASDK_CHANNEL=2026.08
export TAR_OPTIONS=--force-local
export PATH="$VITASDK/bin:/mingw64/bin:$PATH"

# ---------------------------------------------------------------- 1. MSYS2 packages
command -v pacman >/dev/null || die "run this in MSYS2 (or use build_all.ps1 on Windows)"
PKGS="make git cmake tar bzip2 xz wget curl python patch pkgconf diffutils
      mingw-w64-x86_64-gcc mingw-w64-x86_64-libpng mingw-w64-x86_64-pkgconf
      mingw-w64-x86_64-python mingw-w64-x86_64-python-pillow"
missing=""
for p in $PKGS; do pacman -Q "$p" >/dev/null 2>&1 || missing="$missing $p"; done
if [ "$ROM" = "--missing-packages" ]; then
    [ -n "$missing" ] && echo "Missing MSYS2 packages:$missing"
    [ -z "$missing" ]; exit $?
fi
if [ -n "$missing" ]; then
    step "Installing MSYS2 packages:$missing"
    pacman_with_retry -Syu --noconfirm
    pacman_with_retry -S --needed --noconfirm $missing
fi

# ---------------------------------------------------------------- 2. VitaSDK and its libraries
if [ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]; then
    step "Installing VitaSDK ($VITASDK_CHANNEL)"
    mkdir -p /c/vdpm-tmp
    export TMPDIR="$(cygpath -m /c/vdpm-tmp)"
    rm -rf /tmp/vdpm
    git clone --quiet --depth 1 https://github.com/vitasdk/vdpm /tmp/vdpm
    (cd /tmp/vdpm && ./bootstrap-vitasdk.sh)
fi
[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK did not install; see docs/guides/BUILD.md"
VITA_LIB="$VITASDK/arm-vita-eabi/lib"
VITA_INC="$VITASDK/arm-vita-eabi/include"
need_gl=""; need_sdl=""
for f in "$VITA_INC/vitaGL.h" "$VITA_LIB/libvitaGL.a" "$VITA_LIB/libvitashark.a" \
         "$VITA_LIB/libmathneon.a" "$VITA_LIB/libSceShaccCgExt.a" "$VITA_LIB/libtaihen_stub.a"; do
    [ -f "$f" ] || need_gl=" vitaGL"
done
for f in "$VITA_LIB/libSDL2.a"; do
    [ -f "$f" ] || need_sdl=" sdl2"
done
need_pkgs="$need_gl$need_sdl"
if [ -n "$need_pkgs" ]; then
    step "Installing the Vita libraries:$need_pkgs"
    mkdir -p /c/vdpm-tmp
    export TMPDIR="$(cygpath -m /c/vdpm-tmp)"
    if ! VDPM_NONINTERACTIVE=1 vdpm install $need_pkgs; then
        FALLBACK_VITA_PKG_URL="https://github.com/vitasdk/vitasdk-autobuild/releases/download/packages-2026.08-snapshot-20261115.68.1"
        echo "vdpm install failed; falling back to $FALLBACK_VITA_PKG_URL"
        [ -f "$VITASDK/etc/pacman.conf" ] || die "missing $VITASDK/etc/pacman.conf; see docs/guides/BUILD.md"
        sed -i "s|^Server = https://github.com/vitasdk/vitasdk-autobuild/releases/download/.*|Server = $FALLBACK_VITA_PKG_URL|" "$VITASDK/etc/pacman.conf"
        rm -f "$VITASDK/var/lib/pacman/sync/vita.db"
        vdpm pacman -- -Syy --noconfirm $need_pkgs
    fi
    for f in "$VITA_INC/vitaGL.h" "$VITA_LIB/libvitaGL.a" "$VITA_LIB/libSDL2.a"; do
        [ -f "$f" ] || die "$(basename "$f") is still missing after vdpm install $need_pkgs; see docs/guides/BUILD.md"
    done
fi

# ---------------------------------------------------------------- 3. the game files
manifest_sha=$(sha1sum tools/assets/manifest.txt | cut -d' ' -f1)
if [ ! -s .assets_ok ] || [ ! -f graphics/pokemon/bulbasaur/front.png ]; then
    [ -n "$ROM" ] || die "the game files are missing: give the path of your Pokémon FireRed dump"
    step "Getting the game files"
    ./fetch_assets.sh "$ROM"
elif [ "$(cat .assets_manifest 2>/dev/null)" != "$manifest_sha" ]; then
    step "Updating the game files"
    ./fetch_assets.sh --update
fi

# ---------------------------------------------------------------- 4. host tools and conversion
step "Building the host tools"
make -f make_tools.mk -j"$JOBS" CC="gcc -static" CXX="g++ -static"
step "Converting the game files"
make -f vita/assets.mk -j"$JOBS"

# ---------------------------------------------------------------- 5. interface atlas
# build_atlas.py reads converted graphics/region_map/region_map.4bpp produced by vita/assets.mk,
# so it runs after asset conversion. Rebuild on every run so ui_atlas.bin matches ui_sprite_ids.h.
step "Building the interface atlas"
PY=""
for c in $(type -ap python3 python 2>/dev/null); do
    if "$c" -c "import PIL" 2>/dev/null; then PY="$c"; break; fi
done
[ -n "$PY" ] || die "Python 3 with Pillow is missing (pacman -S mingw-w64-x86_64-python-pillow)"
"$PY" tools/ui2/build_atlas.py

# ---------------------------------------------------------------- 6. the VPK
step "Building the VPK"
./build_vita.sh
cp build-vita/pokefirered.vpk FireRedVita.vpk
echo
echo "=== Done: $ROOT/FireRedVita.vpk ==="
echo "Install it with VitaShell. This is an experimental work in progress: please report bugs"
echo "as GitHub issues (see docs/guides/BUILD_YOUR_OWN.md, 'Reporting bugs')."
