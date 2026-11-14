# Shared settings for setup.ps1 and build.ps1. Dot-source this file, do not run it.
#
# Everything the build tools need lives under tools\build-env:
#   msys64\     portable MSYS2 (compiler, CMake, Ninja, libraries)
#   node\       portable Node.js (builds the web UI)
#   sandbox\    stand-in for the user profile, AppData and the npm cache
#   downloads\  downloaded archives
# Nothing is added to PATH, the registry or the real user profile.

$BuildEnv = $PSScriptRoot
$RepoRoot = (Resolve-Path (Join-Path $BuildEnv '..\..')).Path
$Msys = Join-Path $BuildEnv 'msys64'
$NodeDir = Join-Path $BuildEnv 'node'
$Sandbox = Join-Path $BuildEnv 'sandbox'
$Downloads = Join-Path $BuildEnv 'downloads'
$Bash = Join-Path $Msys 'usr\bin\bash.exe'

$NodeVersion = '20.18.1'

function Enter-BuildSandbox {
  foreach ($d in 'profile', 'appdata', 'localappdata', 'npm-cache') {
    New-Item -ItemType Directory -Force (Join-Path $Sandbox $d) | Out-Null
  }

  # MSYS2 in UCRT64 mode with its own PATH: the Windows PATH is not used, so no system tool leaks in
  $env:MSYSTEM = 'UCRT64'
  $env:CHERE_INVOKING = '1'
  $env:MSYS2_PATH_TYPE = 'minimal'

  # Tools that write to the user profile or AppData write into the sandbox instead.
  # (MSYS2 itself keeps its home in msys64\home and its temp files in msys64\tmp.)
  $env:USERPROFILE = Join-Path $Sandbox 'profile'
  $env:APPDATA = Join-Path $Sandbox 'appdata'
  $env:LOCALAPPDATA = Join-Path $Sandbox 'localappdata'
  $env:HOME = $null
  $env:npm_config_cache = Join-Path $Sandbox 'npm-cache'
  $env:npm_config_update_notifier = 'false'
  $env:npm_config_fund = 'false'
  $env:npm_config_audit = 'false'

  # Read by the bash commands below
  $env:APOLLO_ROOT = $RepoRoot
  $env:APOLLO_NODE_DIR = $NodeDir
}

# Run a command line in the MSYS2 UCRT64 shell, with the portable Node.js in front of PATH
function Invoke-Msys([string]$Command) {
  $prefix = 'export PATH="$(cygpath -u "$APOLLO_NODE_DIR"):$PATH"; cd "$(cygpath -u "$APOLLO_ROOT")"; '
  # The command travels in an environment variable: Windows PowerShell strips double quotes
  # from arguments it passes to programs, which would break the command.
  $env:APOLLO_CMD = $prefix + $Command
  & $Bash -lc 'eval $APOLLO_CMD'
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code ${LASTEXITCODE}: $Command"
  }
}
