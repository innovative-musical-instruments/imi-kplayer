<#
.SYNOPSIS
    Clean Release build of Kadabra K-Player for Windows distribution.

.DESCRIPTION
    The Windows counterpart of scripts/build_release.sh. It forces a fresh
    CMake configure (cmake --fresh) so JUCE's generated version resource is
    rebuilt from the current project(... VERSION ...). A plain
    `cmake --build --config Release` after a version bump can otherwise
    link a stale FILEVERSION - it did once (2026-08-28): a 0.9.7-stamped
    "0.9.8" binary went all the way into a signed installer. CMakeLists.txt's
    WIN32 post-build guard (cmake/AssertExeVersion.cmake) then hard-fails
    on any remaining mismatch.

    Output: build\IMI_KPlayer_artefacts\Release\Kadabra K-Player.exe

    Signing stays a separate, documented step (it needs a cached az login):
      ..\imi-windows-installer\scripts\sign-windows-binary.ps1 -Path <exe>
    then zip as  Release\Kadabra K-Player v<version> (Win64).zip  to match
    the KSamplers' convention.

.PARAMETER Generator
    CMake generator string. Defaults to the newest "Visual Studio NN YYYY"
    that `cmake --help` lists as available.

.EXAMPLE
    scripts\build_release_windows.ps1

.EXAMPLE
    scripts\build_release_windows.ps1 -Generator "Visual Studio 17 2022"
#>
[CmdletBinding()]
param(
    [string]$Generator
)
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$buildDir = "build"
$config   = "Release"

if (-not $Generator) {
    $genLine = (& cmake --help | Select-String -Pattern '^\*\s+Visual Studio ').Line | Select-Object -First 1
    if (-not $genLine) {
        throw "No 'Visual Studio' generator found in 'cmake --help' - pass -Generator explicitly."
    }
    $Generator = ($genLine -replace '^\*\s+', '' -replace '\s+\[.*$', '' -replace '\s+=.*$', '').Trim()
}
Write-Output "==> Generator: $Generator"

# --fresh wipes CMakeCache.txt + CMakeFiles/ so the JUCE version resource
# is regenerated from the current project(VERSION). Shares build/ with
# Debug per this repo's convention (no separate build-release/ on Windows),
# so the next Debug build reconfigures too - acceptable at release cadence.
Write-Output "==> Fresh configure ($buildDir)"
& cmake --fresh -S . -B $buildDir -G $Generator
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }

Write-Output "==> Building IMI_KPlayer ($config)"
& cmake --build $buildDir --config $config --target IMI_KPlayer
if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }

$exe = Join-Path $repoRoot "$buildDir\IMI_KPlayer_artefacts\$config\Kadabra K-Player.exe"
if (-not (Test-Path $exe)) { throw "Build succeeded but no exe at: $exe" }

$fvi     = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
$version = "$($fvi.FileMajorPart).$($fvi.FileMinorPart).$($fvi.FileBuildPart)"

Write-Output ""
Write-Output "Done: $exe"
Write-Output "     FILEVERSION $version  (POST_BUILD guard passed - matches project VERSION)"
Write-Output ""
Write-Output "Next: sign it, then zip -"
Write-Output "  ..\imi-windows-installer\scripts\sign-windows-binary.ps1 -Path `"$exe`""
Write-Output "  Compress-Archive `"$exe`" `"Release\Kadabra K-Player v$version (Win64).zip`""
