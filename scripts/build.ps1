# Configures and builds WHY? with the Visual Studio C++ tools, wherever they are installed.
param(
    [ValidateSet('release', 'debug', 'asan')] [string] $Preset = 'release',
    [string] $Target = 'all'
)
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools were not found.' }

Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = "$vs\VC\vcpkg" }
Set-Location (Split-Path $PSScriptRoot -Parent)

cmake --preset $Preset
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build "build/$Preset" --target $Target
exit $LASTEXITCODE
