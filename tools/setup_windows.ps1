# One-shot build of everything on Windows: the DS core (from source), the Order of Ecclesia
# port and the launcher. Expects ooe-decomp\ next to launcher\.
# Needs: Git, CMake, Visual Studio 2022 with "Desktop development with C++", and vcpkg
# (git clone https://github.com/microsoft/vcpkg; .\vcpkg\bootstrap-vcpkg.bat; set VCPKG_ROOT).
# Run from a "Developer PowerShell for VS 2022".
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $env:VCPKG_ROOT) { throw "Defina VCPKG_ROOT apontando para a pasta do vcpkg." }
$Toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"

& (Join-Path $Root "ooe-decomp\tools\build_core.ps1")

$Ooe = Join-Path $Root "ooe-decomp"
cmake -S $Ooe -B (Join-Path $Ooe "build-port") "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
cmake --build (Join-Path $Ooe "build-port") --config Release

$Launcher = Join-Path $Root "launcher"
cmake -S $Launcher -B (Join-Path $Launcher "build") "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
cmake --build (Join-Path $Launcher "build") --config Release

Write-Host "Pronto: $Launcher\build\launcher.exe"
