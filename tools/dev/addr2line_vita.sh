#!/bin/bash
# Resolves PS Vita addresses (or crash-dump PC/LR offsets) against build-vita/pokefirered.
# Usage:
#   ./tools/dev/addr2line_vita.sh 0x81012340 0x81056788
#   ./tools/dev/addr2line_vita.sh --base 0x81000000 0x00012340   # subtracts module load base if raw crash address given

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ELF="${VITA_ELF:-$ROOT/build-vita/pokefirered}"
ADDR2LINE="${VITASDK:-/usr/local/vitasdk}/bin/arm-vita-eabi-addr2line"

if [ ! -f "$ELF" ]; then
    echo "ERROR: ELF not found at $ELF" >&2
    exit 1
fi

SUB_BASE=0
if [ "${1:-}" = "--base" ]; then
    SUB_BASE="$2"
    shift 2
fi

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 [--base 0xLOADBASE] <addr>..." >&2
    exit 1
fi

ARGS=()
for addr in "$@"; do
    if [ "$SUB_BASE" != "0" ]; then
        val=$(( addr - SUB_BASE + 0x81000000 ))
        ARGS+=("$(printf '0x%x' "$val")")
    else
        ARGS+=("$addr")
    fi
done

exec "$ADDR2LINE" -f -C -p -e "$ELF" "${ARGS[@]}"
