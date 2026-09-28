$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$qmake = Get-Command qmake6 -ErrorAction SilentlyContinue
if (-not $qmake) { $qmake = Get-Command qmake -ErrorAction SilentlyContinue }
$make = Get-Command mingw32-make -ErrorAction SilentlyContinue
if (-not $make) { $make = Get-Command nmake -ErrorAction SilentlyContinue }
if (-not $qmake -or -not $make) {
    Write-Host "Qt qmake and MinGW/NMake are not on PATH."
    Write-Host "You can still create the patched source with: python build_kyoto_porymap.py --patch-only"
    exit 1
}
python build_kyoto_porymap.py --qmake $qmake.Source --make $make.Source --jobs $env:NUMBER_OF_PROCESSORS
