# Windows build: check out <ref> from a bare repository into a work tree, configure and build
# the Windows preset(s) with Visual Studio's bundled CMake, then run the unit and host tests.
# Usage: powershell -File build.ps1 -ref main -arch x86|x64|all [-root DIR] [-bare DIR] [-Golden DIR -Dicts DIR]
# Defaults: HDTRANS_CPP_ROOT / HDTRANS_CPP_BARE, else the script's repository and no fetch.
# Golden sets are optional; HDTRANS_GOLDEN / HDTRANS_DICTS supply their locations.
param(
    [string]$ref = 'main',
    [ValidateSet('x86', 'x64', 'all')]
    [string]$arch = 'all',
    [string]$root = $env:HDTRANS_CPP_ROOT,
    [string]$bare = $env:HDTRANS_CPP_BARE,
    [string]$Golden = $env:HDTRANS_GOLDEN,
    [string]$Dicts = $env:HDTRANS_DICTS
)
$ErrorActionPreference = 'Continue'
if ([bool]$Golden -ne [bool]$Dicts) { 'Golden and Dicts must be supplied together'; exit 2 }
if ($Golden -and -not (Test-Path "$PSScriptRoot\golden.ps1" -PathType Leaf)) { 'no golden.ps1 beside build.ps1'; exit 2 }
if (-not $root) { $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
if ($bare) {
    if (-not (Test-Path "$root\.git")) { git clone -q $bare $root 2>&1 | Out-Null }
    git -C $root fetch -q origin 2>&1 | Out-Null
    git -C $root checkout -q --detach "origin/$ref" 2>&1 | Out-Null
    git -C $root reset -q --hard 2>&1 | Out-Null
}
"source: " + (git -C $root rev-parse --short HEAD)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { 'no Visual Studio with the C++ tools'; exit 1 }
$vsVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
$generator = if ([version]$vsVersion -ge [version]'18.0') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$cmakeDir = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$cmake = Join-Path $cmakeDir 'cmake.exe'
$ctest = Join-Path $cmakeDir 'ctest.exe'
$archs = if ($arch -eq 'all') { @('x86', 'x64') } else { @($arch) }
$exit = 0
Push-Location $root
foreach ($a in $archs) {
    $preset = "windows-$a"
    & $cmake --preset $preset -G $generator "-DCMAKE_GENERATOR_INSTANCE=$vs"
    $buildExit = $LASTEXITCODE
    if ($buildExit -eq 0) {
        & $cmake --build --preset $preset
        $buildExit = $LASTEXITCODE
    }
    if ($buildExit -eq 0) {
        & $ctest --preset $preset
        $buildExit = $LASTEXITCODE
    }
    "$preset exit=$buildExit"
    if ($buildExit -ne 0) { $exit = 1 }
    $buildDir = "$root\build\$preset\Release"
    if ($buildExit -eq 0 -and $Golden) {
        $timer = [System.Diagnostics.Stopwatch]::StartNew()
        & "$PSScriptRoot\golden.ps1" -Build $buildDir -Golden $Golden -Dicts $Dicts
        $goldenExit = $LASTEXITCODE
        "$preset golden exit=$goldenExit elapsed=$([Math]::Round($timer.Elapsed.TotalSeconds, 2))s"
        if ($goldenExit -ne 0) { $exit = 1 }
    }
    foreach ($dll in "$buildDir\hdTransEngine.dll", "$buildDir\J2KEngine.dll") {
        if (Test-Path $dll) { "$preset $(Split-Path $dll -Leaf) " + (Get-FileHash $dll).Hash + ' ' + (Get-Item $dll).Length }
    }
}
Pop-Location
if ($exit -eq 0) { git -C $root rev-parse HEAD | Set-Content "$root\build\SOURCE" } else { Remove-Item "$root\build\SOURCE" -ErrorAction SilentlyContinue }
"build exit=$exit"
exit $exit
