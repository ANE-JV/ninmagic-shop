[CmdletBinding()]
param([string]$DevkitPro='C:\devkitPro',[string]$YyjsonSource='')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot=[IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.codex/tmp/ninmagic-shop-updater-tests'))
$null=New-Item -ItemType Directory -Path $testRoot -Force
if (-not $YyjsonSource) {
    $YyjsonSource=Join-Path $env:USERPROFILE '.codex/tmp/ninmagic-shop-build/build-release/_deps/yyjson-src/src'
}
if (-not (Test-Path -LiteralPath (Join-Path $YyjsonSource 'yyjson.c'))) { throw 'Pass -YyjsonSource pointing to the yyjson src directory from the configured Shop build.' }
$cc=Join-Path $DevkitPro 'msys2/usr/bin/gcc.exe'
$cxx=Join-Path $DevkitPro 'msys2/usr/bin/g++.exe'
function Invoke-Checked([string]$Executable,[string[]]$Arguments) {
    $normalized=@($Arguments | ForEach-Object { $_.Replace('\','/') })
    & $Executable @normalized
    if ($LASTEXITCODE -ne 0) { throw "Updater test failed (exit $LASTEXITCODE)." }
}
$jsonObject=Join-Path $testRoot 'yyjson.o'
Invoke-Checked $cc @('-std=c11','-O2','-I',$YyjsonSource,'-c',(Join-Path $YyjsonSource 'yyjson.c'),'-o',$jsonObject)
$testExe=Join-Path $testRoot 'shop-update-test.exe'
Invoke-Checked $cxx @('-std=c++23','-O2','-Wall','-Wextra','-Werror','-DNMSHOP_UPDATE_HOST_TEST','-I',(Join-Path $projectRoot 'sphaira/include'),'-I',$YyjsonSource,(Join-Path $projectRoot 'sphaira/source/shop_update.cpp'),(Join-Path $projectRoot 'tools/tests/shop_update_test.cpp'),$jsonObject,'-o',$testExe)
Invoke-Checked $testExe @()
