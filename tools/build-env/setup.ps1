# Downloads the portable build tools for Apollo into tools\build-env.
# Safe to run again: steps that are already done are skipped.
#
#   powershell -ExecutionPolicy Bypass -File tools\build-env\setup.ps1

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
. (Join-Path $PSScriptRoot 'env.ps1')

New-Item -ItemType Directory -Force $Downloads | Out-Null

# --- MSYS2 (compiler, CMake, Ninja, libraries) -------------------------------------------
if (-not (Test-Path $Bash)) {
  $sfx = Join-Path $Downloads 'msys2-base-x86_64-latest.sfx.exe'
  if (-not (Test-Path $sfx)) {
    Write-Host 'Downloading MSYS2 (portable self-extracting archive)...'
    & curl.exe -L --fail --silent --show-error -o $sfx 'https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe'
    if ($LASTEXITCODE -ne 0) { throw 'MSYS2 download failed' }
  }
  Write-Host 'Extracting MSYS2...'
  & $sfx -y "-o$BuildEnv" | Out-Null
  if (-not (Test-Path $Bash)) { throw 'MSYS2 extraction failed' }
}

Enter-BuildSandbox

Write-Host 'Initializing MSYS2...'
& $Bash -lc 'true'

Write-Host 'Updating MSYS2 packages...'
# The first pass can update the MSYS2 runtime itself and then stop; the second pass finishes the job.
& $Bash -lc 'pacman -Syuu --noconfirm'
& $Bash -lc 'pacman -Syuu --noconfirm'
if ($LASTEXITCODE -ne 0) { throw 'pacman update failed' }

Write-Host 'Installing build packages...'
$packages = @(
  'git'
  'mingw-w64-ucrt-x86_64-toolchain'
  'mingw-w64-ucrt-x86_64-cmake'
  'mingw-w64-ucrt-x86_64-ninja'
  'mingw-w64-ucrt-x86_64-boost'
  'mingw-w64-ucrt-x86_64-cppwinrt'
  'mingw-w64-ucrt-x86_64-curl-winssl'
  'mingw-w64-ucrt-x86_64-MinHook'
  'mingw-w64-ucrt-x86_64-miniupnpc'
  'mingw-w64-ucrt-x86_64-nsis'
  'mingw-w64-ucrt-x86_64-onevpl'
  'mingw-w64-ucrt-x86_64-openssl'
  'mingw-w64-ucrt-x86_64-opus'
  'mingw-w64-ucrt-x86_64-nlohmann_json'
) -join ' '
& $Bash -lc "pacman -S --needed --noconfirm $packages"
if ($LASTEXITCODE -ne 0) { throw 'pacman install failed' }

# --- Node.js (official build; the MSYS2 one crashes, see docs/building.md) ----------------
if (-not (Test-Path (Join-Path $NodeDir 'node.exe'))) {
  $zip = Join-Path $Downloads "node-v$NodeVersion-win-x64.zip"
  if (-not (Test-Path $zip)) {
    Write-Host "Downloading Node.js $NodeVersion..."
    & curl.exe -L --fail --silent --show-error -o $zip "https://nodejs.org/dist/v$NodeVersion/node-v$NodeVersion-win-x64.zip"
    if ($LASTEXITCODE -ne 0) { throw 'Node.js download failed' }
  }
  Write-Host 'Extracting Node.js...'
  Expand-Archive $zip -DestinationPath $BuildEnv -Force
  Rename-Item (Join-Path $BuildEnv "node-v$NodeVersion-win-x64") $NodeDir
}

Write-Host ''
Write-Host 'Tool versions:'
Invoke-Msys 'gcc --version | head -n 1; cmake --version | head -n 1; echo "ninja $(ninja --version)"; echo "node $(node --version)"; echo "npm $(npm --version)"'
Write-Host ''
Write-Host 'Done. Build with: powershell -ExecutionPolicy Bypass -File tools\build-env\build.ps1'
