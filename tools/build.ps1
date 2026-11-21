[CmdletBinding()]
param(
    [string]$DevkitPro = 'C:\devkitPro',
    [string]$BuildRoot = (Join-Path $env:USERPROFILE '.codex\tmp\ninmagic-shop-build'),
    [int]$Jobs = 4,
    [string]$Python = 'python',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stageRoot = [IO.Path]::GetFullPath($BuildRoot)
$sdkRoot = [IO.Path]::GetFullPath($DevkitPro)
if ($Jobs -lt 1 -or $Jobs -gt 64) { throw 'Jobs must be between 1 and 64.' }
if ($stageRoot -match '\s') { throw 'The staging build path must not contain whitespace (devkitPro makefiles limitation).' }
if ($stageRoot.TrimEnd('\') -eq [IO.Path]::GetPathRoot($stageRoot).TrimEnd('\') -or
    $projectRoot.StartsWith($stageRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $stageRoot.StartsWith($projectRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $stageRoot -eq $projectRoot) { throw 'BuildRoot must be a dedicated directory outside the source project.' }
$bash = Join-Path $sdkRoot 'msys2\usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) { throw "devkitPro MSYS2 bash was not found: $bash" }
foreach ($library in @('glm\glm.hpp', 'mbedtls\ssl.h')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot "portlibs\switch\include\$library") -PathType Leaf)) {
        throw 'Missing portlibs. Run in devkitPro MSYS2: pacman -S --needed switch-glm switch-mbedtls'
    }
}
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
# Copy only build inputs. Never include console data, .local, .git or other projects.
# A manifest removes only files previously staged by this script, never arbitrary files.
$inputs = @('CMakeLists.txt', 'LICENSE')
foreach ($directory in @('cmake', 'hbl', 'sphaira', 'tools')) {
    $inputs += Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -File -Recurse |
        Where-Object { $_.FullName -notmatch '[\\/](__pycache__|\.git|\.local|build)[\\/]' } |
        ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) }
}
$inputs += 'assets\icon.jpg'
foreach ($directory in @('assets\embed', 'assets\romfs')) {
    $inputs += Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -File -Recurse |
        ForEach-Object { $_.FullName.Substring($projectRoot.Length + 1) }
}
$manifest = Join-Path $stageRoot '.ninmagic-shop-staged-files.json'
if (Test-Path -LiteralPath $manifest) {
    foreach ($old in (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json)) {
        if ($old -notin $inputs) {
            $oldPath = [IO.Path]::GetFullPath((Join-Path $stageRoot $old))
            if (-not $oldPath.StartsWith($stageRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Invalid previous build manifest path.'
            }
            if (Test-Path -LiteralPath $oldPath -PathType Leaf) { Remove-Item -LiteralPath $oldPath }
        }
    }
}
foreach ($relative in $inputs) {
    $destination = Join-Path $stageRoot $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $destination -Force
}
$inputs | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
function ConvertTo-MsysPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^([A-Za-z]):[\\/]') { throw 'Use a local drive path for the build.' }
    return '/' + $Matches[1].ToLowerInvariant() + $full.Substring(2).Replace('\', '/')
}
$stageUnix = ConvertTo-MsysPath $stageRoot
$previousDevkit = $env:DEVKITPRO
$previousJobs = $env:NINMAGIC_BUILD_JOBS
$previousClean = $env:NINMAGIC_BUILD_CLEAN
$previousPython = $env:NINMAGIC_PYTHON
$previousSourceCommit = $env:NINMAGIC_SOURCE_COMMIT
$previousSourceBranch = $env:NINMAGIC_SOURCE_BRANCH
$sourceCommit = & git -C $projectRoot rev-parse --short HEAD
$sourceBranch = & git -C $projectRoot symbolic-ref --short HEAD
try {
    $env:DEVKITPRO = ConvertTo-MsysPath $sdkRoot
    $env:NINMAGIC_BUILD_JOBS = [string]$Jobs
    $env:NINMAGIC_BUILD_CLEAN = if ($Package) { '1' } else { '0' }
    $env:NINMAGIC_PYTHON = ConvertTo-MsysPath (Get-Command $Python -CommandType Application | Select-Object -First 1 -ExpandProperty Source)
    $env:NINMAGIC_SOURCE_COMMIT = [string]$sourceCommit
    $env:NINMAGIC_SOURCE_BRANCH = [string]$sourceBranch
    & $bash --login "$stageUnix/tools/build.sh" $stageUnix "$stageUnix/build-release"
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE. No release was packaged." }
} finally {
    $env:DEVKITPRO = $previousDevkit
    $env:NINMAGIC_BUILD_JOBS = $previousJobs
    $env:NINMAGIC_BUILD_CLEAN = $previousClean
    $env:NINMAGIC_PYTHON = $previousPython
    $env:NINMAGIC_SOURCE_COMMIT = $previousSourceCommit
    $env:NINMAGIC_SOURCE_BRANCH = $previousSourceBranch
}
$binary = Join-Path $stageRoot 'build-release\NinMagic-Shop.nro'
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
& $Python (Join-Path $PSScriptRoot 'test-transport.py') --output (Join-Path $stageRoot 'host-tests/transport')
if ($LASTEXITCODE -ne 0) { throw 'HTTP transport regressions failed. No release was packaged.' }
& $Python (Join-Path $PSScriptRoot 'test-curl-legacy.py') --output (Join-Path $stageRoot 'host-tests/curl-legacy') --curl-include (Join-Path $sdkRoot 'portlibs/switch/include') --curl-library (Join-Path $sdkRoot 'msys2/usr/bin/msys-curl-4.dll')
if ($LASTEXITCODE -ne 0) { throw 'Legacy libcurl wire regressions failed. No release was packaged.' }
& $Python (Join-Path $PSScriptRoot 'test-ownfoil.py') --yyjson (Join-Path $stageRoot 'build-release/_deps/yyjson-src/src') --output (Join-Path $stageRoot 'host-tests/ownfoil')
if ($LASTEXITCODE -ne 0) { throw 'Ownfoil parser regressions failed. No release was packaged.' }
& $Python (Join-Path $PSScriptRoot 'test-profiles.py') --minini (Join-Path $stageRoot 'build-release/_deps/minini-src') --output (Join-Path $stageRoot 'host-tests/profiles')
if ($LASTEXITCODE -ne 0) { throw 'Profile persistence regressions failed. No release was packaged.' }
& $Python (Join-Path $PSScriptRoot 'test-updater.py') --yyjson (Join-Path $stageRoot 'build-release/_deps/yyjson-src/src') --output (Join-Path $stageRoot 'host-tests/updater')
if ($LASTEXITCODE -ne 0) { throw 'Updater transaction regressions failed. No release was packaged.' }
& $Python (Join-Path $PSScriptRoot 'verify.py') $binary --output (Join-Path $dist 'verification.json')
if ($LASTEXITCODE -ne 0) { throw 'NRO validation failed. No release was packaged.' }
& $Python (Join-Path $PSScriptRoot 'test-verifier.py') $binary
if ($LASTEXITCODE -ne 0) { throw 'Release verifier regressions failed. No release was packaged.' }
Copy-Item -LiteralPath $binary -Destination (Join-Path $dist 'NinMagic-Shop.nro') -Force
if ($Package) {
    & $Python (Join-Path $PSScriptRoot 'package.py') $binary --build-dir (Join-Path $stageRoot 'build-release') --devkitpro $sdkRoot
    if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed.' }
}
Write-Host "Verified outputs: $dist"
