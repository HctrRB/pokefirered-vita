# Debugging and profiling

Type: guide · Audience: developers · Status: Phase F6 built (telemetry, pacing columns & `-Probe` active; pending real Vita bench)
Verified against: Phase F6 build, `-Probe` build, ELF & `core_boundary` check (2026-11-22)

## Logs (`VITA_LOG`)

- **No unbuffered hot-loop logs:** Unbuffered per-frame `boot.txt` SD tracing was removed in Phase F3 to eliminate 100–400 ms SD flush stalls inside `VBlankIntrWait()`.
- **Buffered runtime log (`VITA_LOG`):** Diagnostic logging goes strictly through `VITA_LOG(...)` (`src/platform/vita/vita_platform.h`) using a 32 KB buffered stream to `ux0:data/FRVITA/debug.log` (`sLogBuffer`), compiled out (`((void)0)`) unless configured with `-DVITA_LOGGING=ON`.
- Never use raw `printf` in platform or game code; never log or flush SD files inside hot loops.

## Per-frame telemetry & GPU probe build (`VitaPerf`, `vita_perf.c` / `vita_perf.h`)

- **Zero overhead in default builds:** `sSummaryBuf`, `sCsvBuf`, `sWindowFrameMs`, and the pacing
  history buffers are compiled strictly under `#ifdef VITA_TELEMETRY` (`vita_platform.c` prints
  zero-overhead logging unless `VITA_LOGGING=ON`).
- **Active in `-DVITA_TELEMETRY=ON` and `build_vita.ps1 -Probe` (Phase F6):**
  - `VitaPerf_Init()` opens 32 KB buffered streams at `ux0:data/FRVITA/perf_summary.txt` and
    `ux0:data/FRVITA/perf_frames.csv` (Core 1 logic/VBlank/pacing, Core 2 scanline & chunk workers,
    Core 0 3D passes/UI/swap, `[MEMORY]` RAM & CDRAM free headroom), flushing only at
    `VitaPerf_Init()` and `VitaPerf_Shutdown()` (**without** calling `fflush` inside
    `WriteSummaryBlock()` on Core 0).
  - **Frame-pacing columns (`pokeemerald-vita-3ds:FRAME_PACING_AUDIT.md`):** `VitaPerf_RecordPacing()`
    and `VitaPerf_EndFrame()` export `present_dt_ms`, `sem_wait_ms`, `dvcount` (integer hardware
    vblank delta from `sceDisplayGetVcount`), `post_comp_ms`, and `c1_seq_delta` per CSV row,
    alongside `VBLANK HIST` (`1v`, `2v`, `3v`, `4v+`) in `perf_summary.txt`.
  - **Scene-boundary GPU probes (`-DVITA_GPU_PROBE=ON`):** vitaGL has no hardware GPU timer queries,
    and calling `glFinish` or `glFlush` in the middle of a render pass calls `scene_reset`
    (`sceGxmEndScene` + `sceGxmBeginScene`), which discards the tile depth buffer (`STORE_DEPTH_STENCIL`
    is off). Therefore, `VitaPerf_GpuScene` runs `glFinish` **only at render-target changes** right
    after `glBindFramebuffer` in `Pass_DepthOfField` and before `vglSwapBuffers`, recording
    `gpu_world_ms`, `gpu_down_ms`, `gpu_blur_ms`, `gpu_bloom_ms`, `gpu_kawase_ms`, `gpu_final_ms`,
    `gpu_total_ms`, and `gxm_scenes`.
  - **Automatic HD-2D benchmark:** Standing still in the 3D field in a probe build automatically
    cycles through `HIGH`, each `VitaPerfPass` mask disabled one by one, `MED`, `LOW`, and `OFF`,
    tagging rows in `perf_frames.csv` with `bench_run`, `bench_step`, `hd2d_level`, `probe_mask`,
    and the Kanto map/weather/daylight/lamps state.

## UI stall log (`ui_stall.log`, Phase F7 target)

Built under `-DVITA_TELEMETRY=ON` once the Stage 2 Native 16:9 UI (`src/platform/ui/`) lands. Writes
a line to `ux0:data/FRVITA/ui_stall.log` (up to 200 per session) whenever Core 1 does not tick the
UI for > 100 ms, Core 0 does not render for > 100 ms, or Core 0 keeps drawing a stale `UiDrawList`
while Core 1 publishes new ones.

## Core boundary & runtime thread tripwire (Phase F3 active)

- **Build-time proof (`tools/core_boundary/check_core_boundary.py`):** Enabled by default (`VITA_CORE_BOUNDARY=ON`). Walks every branch, literal pool reference, and function-pointer table reachable from Core 0 (`MainThread_Run`) and Core 2 (`DrawWorkerMain` when `VITA_PARALLEL_DRAW=ON`, `VoxelChunkWorkerMain` and `VoxelBuildingIoWorkerMain` when `VOXEL_SUPPORTED`) against `game_combined.o` (`gamedata` / `gamebss`), writing `build-vita/core_boundary_report.txt` and failing the build on any violation outside `tools/core_boundary/allow.txt`.
- **Runtime tripwire (`GAME_THREAD_ONLY` in `include/platform/game_thread.h`):** Stack-range check registered by `GameThread_Register()` inside core mutators (`GetMonData3`, `SetMonData`, `FlagSet`, `FlagClear`, `VarSet`), logging caller LR addresses once per call site to `ux0:data/FRVITA/thread_violations.log`. Resolve any recorded LR via:
  ```sh
  arm-vita-eabi-addr2line -f -C -e build-vita/pokefirered <lr>
  ```
  The file must not exist after any normal play session.

## Crash dumps (`psp2core-*.psp2dmp`)

When the game crashes on real PS Vita hardware, the OS saves a core dump under
`ux0:data/psp2core-*.psp2dmp`. Keep the matching unstripped `build-vita/pokefirered` ELF for every
VPK installed on hardware and decode with:

```sh
vita-parse-core <dump.psp2dmp> build-vita/pokefirered
```

If decoding manually from `MODULE_INFO` + `THREAD_REG_INFO` PC/LR: subtract the loaded code module
base and resolve against `0x81000000 + offset` using `arm-vita-eabi-addr2line`.

## Vita3K vs real PS Vita hardware

Vita3K currently produces a black screen with this vitaGL configuration and hides SGX543 GPU timing,
`glTexSubImage2D` 4-swap copy stalls, 30 FPS `eglSwapInterval(2)` vblank cliffs, CDRAM/PHYCONT
exhaustion, and Core 0/1/2 scheduling races. Validate every milestone on real PS Vita hardware using
the smoke checklist in [../../CONTRIBUTING.md](../../CONTRIBUTING.md).
