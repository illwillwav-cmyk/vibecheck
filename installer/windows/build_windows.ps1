<#
.SYNOPSIS
  Builds VibeCheck on Windows and packages the installer.

.DESCRIPTION
  Produces, in dist\ :
    VibeCheck-<version>-Windows-Setup.exe   the installer (Inno Setup)
    VibeCheck-<version>-Windows-x64.zip     portable folder: VibeCheck.exe, vibecheck.cmd, readme
    VibeCheck-<version>-Windows.sha256      checksums for both

  Needs: Visual Studio 2022 (Desktop development with C++), CMake 3.22+, Inno Setup 6.3 or newer (iscc).
  Run from a "x64 Native Tools Command Prompt" or any PowerShell with those on PATH.

  Optional signing (removes the SmartScreen warning once the certificate has reputation):
    $env:SIGN_PFX = "C:\path\cert.pfx"; $env:SIGN_PASSWORD = "..."
#>
[CmdletBinding()]
param(
  [string]$Config = "Release",
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root

$cmake = Get-Content CMakeLists.txt -Raw
$found = [regex]::Match($cmake, 'project\(VibeCheck VERSION (\d+\.\d+\.\d+)')
if (-not $found.Success) { throw "Could not read the version from CMakeLists.txt" }
$version = $found.Groups[1].Value
$buildDir = Join-Path $root "build-win"
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Write-Host "==> VibeCheck $version ($Config)" -ForegroundColor Cyan

if (-not $SkipBuild) {
  # CMAKE_EXTRA_ARGS lets a release build pass extra options, such as -DVIBECHECK_UPDATE_URL=...
  $extra = @(); if ($env:CMAKE_EXTRA_ARGS) { $extra = $env:CMAKE_EXTRA_ARGS -split '\s+' | Where-Object { $_ } }
  # No generator is named, so CMake uses the newest Visual Studio it finds.
  cmake -S . -B $buildDir -A x64 @extra
  if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
  cmake --build $buildDir --config $Config --parallel
  if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

# The folder depends on the generator (Visual Studio adds the configuration, Ninja may not).
$exe = (Get-ChildItem $buildDir -Recurse -Filter VibeCheck.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'VibeCheck_artefacts' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
if (-not $exe -or -not (Test-Path $exe)) { throw "VibeCheck.exe was not found under $buildDir" }

Write-Host "==> Self-test" -ForegroundColor Cyan
& $exe --selftest
if ($LASTEXITCODE -ne 0) { throw "Self-test failed" }

# Optional signing of the executable, then the installer.
$sign = $null
if ($env:SIGN_PFX) {
  $signtool = (Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" | Sort-Object FullName | Select-Object -Last 1).FullName
  if (-not $signtool) { throw "SIGN_PFX is set but signtool.exe was not found (install the Windows SDK)" }
  & $signtool sign /f $env:SIGN_PFX /p $env:SIGN_PASSWORD /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $exe
  if ($LASTEXITCODE -ne 0) { throw "Signing the executable failed" }
  $sign = "`"$signtool`" sign /f `"$env:SIGN_PFX`" /p `"$env:SIGN_PASSWORD`" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `$f"
}

Write-Host "==> Installer" -ForegroundColor Cyan
$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $iscc)) { throw "Inno Setup 6 not found. Install it from https://jrsoftware.org/isinfo.php" }

$isccArgs = @("/DAppVersion=$version", "/DSourceDir=$(Split-Path $exe)", "/DOutputDir=$dist")
if ($sign) { $isccArgs += "/DSignTool=signer"; $isccArgs += "/Ssigner=$sign" }
& $iscc @isccArgs (Join-Path $PSScriptRoot "VibeCheck.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

Write-Host "==> Portable zip" -ForegroundColor Cyan
$stage = Join-Path $buildDir "portable\VibeCheck"
Remove-Item -Recurse -Force (Split-Path $stage) -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $PSScriptRoot "vibecheck.cmd") $stage
Copy-Item (Join-Path $PSScriptRoot "README-Windows.txt") $stage
$zip = Join-Path $dist "VibeCheck-$version-Windows-x64.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip

Write-Host "==> Checksums" -ForegroundColor Cyan
$setup = Join-Path $dist "VibeCheck-$version-Windows-Setup.exe"
$lines = foreach ($f in @($setup, $zip)) { "{0}  {1}" -f (Get-FileHash $f -Algorithm SHA256).Hash.ToLower(), (Split-Path $f -Leaf) }
$lines | Set-Content (Join-Path $dist "VibeCheck-$version-Windows.sha256")

Write-Host ""
Write-Host "Done:" -ForegroundColor Green
Get-ChildItem $dist | Where-Object { $_.Name -like "VibeCheck-$version-Windows*" } | ForEach-Object { "  {0}  ({1:N1} MB)" -f $_.Name, ($_.Length / 1MB) }
