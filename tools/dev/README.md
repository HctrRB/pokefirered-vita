# Development helpers

## PS Vita helpers (current)

| Script | Use |
|---|---|
| `addr2line_vita.sh [--base 0xLOADBASE] <addr>...` | Resolve addresses or `psp2core` crash offsets against `build-vita/pokefirered` using `arm-vita-eabi-addr2line` |
| `sync_fr3ds.py [--upstream <dir> --list] [--patch <file> --out-dir <dir>]` | Inspect `upstream.lock`, compare against a local `Pokemon-FireRed-3DS` clone, and remap `#ifdef PLATFORM_3DS` -> `PLATFORM_VITA` cleanly |
