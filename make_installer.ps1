#Requires -Version 5.1
<#
.SYNOPSIS
  Configure CMake, build BoutiqueRumble_All (Release), stage installer_dist, and compile installer_win.iss.

.PARAMETER CleanInstallerDistAfter
  If set, removes the installer_dist folder after a successful installer build.
#>
param(
    [switch]$CleanInstallerDistAfter
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
Set-Location $Root

function Stop-Step {
    param([string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

Write-Host "==> Boutique Rumble - Windows installer build" -ForegroundColor Cyan
Write-Host "    Root: $Root"

# --- 1. Clean & build ---
Write-Host "`n==> [1/4] CMake configure (Release)..." -ForegroundColor Yellow
try {
    & cmake -B build -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { Stop-Step "CMake configure failed (exit $LASTEXITCODE)." }
}
catch {
    Stop-Step "CMake configure threw: $($_.Exception.Message)"
}

Write-Host "`n==> [2/4] Build BoutiqueRumble_All (Release)..." -ForegroundColor Yellow
try {
    & cmake --build build --config Release --target BoutiqueRumble_All
    if ($LASTEXITCODE -ne 0) { Stop-Step "Build BoutiqueRumble_All failed (exit $LASTEXITCODE)." }
}
catch {
    Stop-Step "Build threw: $($_.Exception.Message)"
}

$vst3SearchRoot = Join-Path $Root 'build\BoutiqueRumble_artefacts\Release\VST3'
if (-not (Test-Path -LiteralPath $vst3SearchRoot)) {
    Stop-Step "VST3 output folder not found: $vst3SearchRoot"
}

$bundles = @(Get-ChildItem -LiteralPath $vst3SearchRoot -Directory -Filter '*.vst3' -ErrorAction SilentlyContinue)
if ($bundles.Count -eq 0) {
    Stop-Step "No .vst3 bundle found under $vst3SearchRoot"
}

# --- 2. Stage ---
Write-Host "`n==> [3/4] Stage installer_dist..." -ForegroundColor Yellow
$dist = Join-Path $Root 'installer_dist'
$rumbleVst3 = Join-Path $dist 'Rumble.vst3'

try {
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    if (Test-Path -LiteralPath $rumbleVst3) {
        Remove-Item -LiteralPath $rumbleVst3 -Recurse -Force
    }

    # Mirror CI: copy whichever *.vst3 CMake produced into installer_dist\Rumble.vst3
    $primaryBundle = $bundles[0]
    Write-Host "    Using bundle: $($primaryBundle.Name)"
    Copy-Item -LiteralPath $primaryBundle.FullName -Destination $rumbleVst3 -Recurse -Force

    $presetsSrc = Join-Path $Root 'Presets'
    $presetsDst = Join-Path $dist 'Presets'
    if (Test-Path -LiteralPath $presetsSrc) {
        if (Test-Path -LiteralPath $presetsDst) {
            Remove-Item -LiteralPath $presetsDst -Recurse -Force
        }
        Copy-Item -LiteralPath $presetsSrc -Destination $presetsDst -Recurse -Force
        Write-Host "    Copied Presets -> installer_dist\Presets"
    }
    else {
        Write-Host '    (No Presets folder at repo root - skipped; Inno uses skipifsourcedoesntexist)' -ForegroundColor DarkYellow
    }

    $readmePdf = Join-Path $Root 'README.pdf'
    if (Test-Path -LiteralPath $readmePdf) {
        Copy-Item -LiteralPath $readmePdf -Destination (Join-Path $dist 'README.pdf') -Force
        Write-Host "    Copied README.pdf"
    }
    else {
        Write-Host '    (README.pdf not found - skipping)' -ForegroundColor DarkYellow
    }

    $stagedFiles = @(Get-ChildItem -LiteralPath $rumbleVst3 -Recurse -File -ErrorAction SilentlyContinue)
    if ($stagedFiles.Count -eq 0) {
        Stop-Step "Staging produced an empty installer_dist\Rumble.vst3 (no files). Check the VST3 build output."
    }
    Write-Host "    Staged $($stagedFiles.Count) file(s) under installer_dist\Rumble.vst3"
}
catch {
    Stop-Step "Staging failed: $($_.Exception.Message)"
}

# --- 3. Inno Setup ---
Write-Host "`n==> [4/4] Compile installer (ISCC)..." -ForegroundColor Yellow
$isccExe = $null
try {
    $cmd = Get-Command 'iscc.exe' -ErrorAction SilentlyContinue
    if ($cmd) {
        $isccExe = $cmd.Source
    }
}
catch { }

if (-not $isccExe) {
    $fallback = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
    if (Test-Path -LiteralPath $fallback) {
        $isccExe = $fallback
    }
}

if (-not $isccExe) {
    Stop-Step "ISCC.exe not found in PATH and not at `"C:\Program Files (x86)\Inno Setup 6\ISCC.exe`". Install Inno Setup or add it to PATH."
}

Write-Host "    Using: $isccExe"
$iss = Join-Path $Root 'installer_win.iss'
if (-not (Test-Path -LiteralPath $iss)) {
    Stop-Step "Missing installer_win.iss at $iss"
}

try {
    New-Item -ItemType Directory -Path (Join-Path $Root 'Output') -Force | Out-Null
    & $isccExe $iss
    if ($LASTEXITCODE -ne 0) { Stop-Step "ISCC failed (exit $LASTEXITCODE)." }
}
catch {
    Stop-Step "ISCC threw: $($_.Exception.Message)"
}

$setupOut = Join-Path $Root 'Output\Rumble_Setup.exe'
if (-not (Test-Path -LiteralPath $setupOut)) {
    Stop-Step "Expected installer not found: $setupOut"
}

Write-Host "`nDone. Installer: $setupOut" -ForegroundColor Green

if ($CleanInstallerDistAfter) {
    Write-Host "`n==> Removing installer_dist [CleanInstallerDistAfter]..." -ForegroundColor Yellow
    try {
        Remove-Item -LiteralPath $dist -Recurse -Force
        Write-Host "    Removed $dist"
    }
    catch {
        Write-Host "WARNING: Could not remove installer_dist: $($_.Exception.Message)" -ForegroundColor DarkYellow
        exit 2
    }
}

exit 0
