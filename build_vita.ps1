# Builds build-vita/pokefirered.vpk from PowerShell by running build_vita.sh in MSYS2.
# -Probe builds the GPU probe + telemetry binary into build-vita-probe\ (-DVITA_GPU_PROBE=ON).
# -Locale <code> builds in that language (tools/locales/languages.json) into build-vita-<code>\;
# a language made from its official ROM also needs -Rom <your ROM of that language>,
# or -UiOnly for a development build of the translated native UI over the English game text.
param([switch]$Probe, [string]$Locale = "en", [string]$Rom = "", [switch]$UiOnly)
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
if ($Locale -ne "en") {
    $env:VITA_LOCALE = $Locale
}
if ($UiOnly) {
    $env:VITA_UI_ONLY = "1"
}
if ($Rom -ne "") {
    # MSYS2 path of the ROM, passed through the environment (no quotes in the command)
    $env:VITA_ROM = (& "C:\msys64\usr\bin\cygpath.exe" -u (Resolve-Path -LiteralPath $Rom).Path)
}
$outDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } elseif ($Locale -ne "en") { "build-vita-$Locale" } else { "build-vita" }

& $bashExe -lc 'set -f; IFS=; cd $(cygpath -u $BUILD_VITA_DIR) && ./build_vita.sh'
$buildExit = $LASTEXITCODE

Remove-Item Env:BUILD_VITA_DIR -ErrorAction SilentlyContinue
if ($Probe) {
    Remove-Item Env:BUILD_DIR, Env:EXTRA_CMAKE_ARGS -ErrorAction SilentlyContinue
}
Remove-Item Env:VITA_LOCALE, Env:VITA_UI_ONLY, Env:VITA_ROM -ErrorAction SilentlyContinue

if ($buildExit -eq 0) {
    Write-Host "=== Done: $outDir\pokefirered.vpk ===" -ForegroundColor Green
} else {
    Write-Host "The build failed (exit code $buildExit)." -ForegroundColor Red
}
exit $buildExit
