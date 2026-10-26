# Downloads the melonDS DS libretro core for 64-bit Windows into cores\bin\ (copied next to ooe.exe by the build).
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Url = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/melondsds_libretro.dll.zip"
$Download = Join-Path $Root "cores\download"
$Bin = Join-Path $Root "cores\bin"

New-Item -ItemType Directory -Force -Path $Download, $Bin | Out-Null
$Zip = Join-Path $Download "melondsds_libretro.dll.zip"
Invoke-WebRequest -Uri $Url -OutFile $Zip
Expand-Archive -Force -Path $Zip -DestinationPath $Download
Copy-Item -Force (Join-Path $Download "melondsds_libretro.dll") $Bin
Write-Host "core em $Bin; recompile com cmake --build build-port para copia-lo"
