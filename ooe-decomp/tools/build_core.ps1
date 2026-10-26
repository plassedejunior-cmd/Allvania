# Builds the melonDS DS libretro core from its official source (a pinned release tag)
# and puts it in cores\bin\, which the port build copies next to ooe.exe.
# Needs Git, CMake and Visual Studio 2022 (Desktop C++) on PATH, e.g. from a Developer PowerShell.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Tag = "v1.4.0"
$Src = Join-Path $Root "cores\melonds-ds"

if (-not (Test-Path (Join-Path $Src ".git"))) {
    git clone https://github.com/JesseTG/melonds-ds.git $Src
}
git -C $Src fetch --tags --quiet
git -C $Src checkout --quiet $Tag

cmake -S $Src -B (Join-Path $Src "build") -DENABLE_OPENGL=OFF 
cmake --build (Join-Path $Src "build") --config Release

$Bin = Join-Path $Root "cores\bin"
New-Item -ItemType Directory -Force -Path $Bin | Out-Null
$Dll = Get-ChildItem -Recurse -Path (Join-Path $Src "build") -Filter "melondsds_libretro.dll" | Select-Object -First 1
Copy-Item -Force $Dll.FullName $Bin
Write-Host "nucleo em $Bin; rode cmake --build build-port --config Release para copia-lo ao lado do ooe.exe"
