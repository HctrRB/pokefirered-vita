# Builds build-vita/pokefirered.vpk from PowerShell by running build_vita.sh in MSYS2.
# -Probe builds the GPU probe + telemetry binary into build-vita-probe\ (-DVITA_GPU_PROBE=ON).
param([switch]$Probe)
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $scriptDir

$bashExe = "C:\msys64\usr\bin\bash.exe"
if (-not (Test-Path -LiteralPath $bashExe)) {
    Write-Host "MSYS2 not found in C:\msys64. See docs/guides/BUILD.md." -ForegroundColor Red
    exit 1
}

Write-Host "=== Building pokefirered-vita in $scriptDir ===" -ForegroundColor Cyan

$env:BUILD_VITA_DIR = $scriptDir
$env:CHERE_INVOKING = "1"
if ($Probe) {
    $env:BUILD_DIR = "build-vita-probe"
    $env:EXTRA_CMAKE_ARGS = "-DVITA_GPU_PROBE=ON"
}
$outDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-vita" }

& $bashExe -lc 'set -f; IFS=; cd $(cygpath -u $BUILD_VITA_DIR) && ./build_vita.sh'
$buildExit = $LASTEXITCODE

Remove-Item Env:BUILD_VITA_DIR -ErrorAction SilentlyContinue
if ($Probe) {
    Remove-Item Env:BUILD_DIR, Env:EXTRA_CMAKE_ARGS -ErrorAction SilentlyContinue
}

if ($buildExit -eq 0) {
    Write-Host "=== Done: $outDir\pokefirered.vpk ===" -ForegroundColor Green
} else {
    Write-Host "The build failed (exit code $buildExit)." -ForegroundColor Red
}
exit $buildExit
