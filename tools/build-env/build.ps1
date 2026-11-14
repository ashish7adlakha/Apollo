# Builds Apollo with the portable tools from setup.ps1. The result is in the "build" folder.
#
#   powershell -ExecutionPolicy Bypass -File tools\build-env\build.ps1            # build
#   powershell -ExecutionPolicy Bypass -File tools\build-env\build.ps1 -Package   # build + ZIP package
#   powershell -ExecutionPolicy Bypass -File tools\build-env\build.ps1 -Clean     # delete "build" first
#   powershell -ExecutionPolicy Bypass -File tools\build-env\build.ps1 -Shell     # open a build shell

param(
  [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
  [string]$BuildType = 'Release',
  [switch]$Package,
  [switch]$Clean,
  [switch]$Shell
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1')

if (-not (Test-Path $Bash) -or -not (Test-Path (Join-Path $NodeDir 'node.exe'))) {
  throw 'Build tools are missing. Run tools\build-env\setup.ps1 first.'
}
if (-not (Test-Path (Join-Path $RepoRoot 'third-party\moonlight-common-c\src'))) {
  throw 'Git submodules are missing. Run: git submodule update --init --recursive'
}

Enter-BuildSandbox

if ($Shell) {
  Invoke-Msys 'exec bash -i'
  exit 0
}

if ($Clean) {
  $buildDir = Join-Path $RepoRoot 'build'
  if (Test-Path $buildDir) {
    Write-Host 'Removing the build folder...'
    Remove-Item -LiteralPath $buildDir -Recurse -Force
  }
}

Invoke-Msys "cmake -B build -G Ninja -S . -DCMAKE_BUILD_TYPE=$BuildType"
Invoke-Msys 'ninja -C build'

if ($Package) {
  Invoke-Msys 'cd build && cpack -G ZIP'
}

Write-Host ''
Write-Host "Build finished: $(Join-Path $RepoRoot 'build')"
