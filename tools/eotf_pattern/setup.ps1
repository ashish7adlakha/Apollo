# Sets up a portable Python inside tools\eotf_pattern\python for make_eotf_chart.py.
# Nothing is installed system-wide: no PATH change, no registry, no pip cache.
#
#   powershell -ExecutionPolicy Bypass -File tools\eotf_pattern\setup.ps1

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$pythonVersion = '3.12.10'
$here = $PSScriptRoot
$py = Join-Path $here 'python'
$exe = Join-Path $py 'python.exe'

if (-not (Test-Path $exe)) {
  New-Item -ItemType Directory -Force $py | Out-Null
  $zip = Join-Path $py 'python-embed.zip'
  Write-Host "Downloading Python $pythonVersion (embeddable)..."
  Invoke-WebRequest "https://www.python.org/ftp/python/$pythonVersion/python-$pythonVersion-embed-amd64.zip" -OutFile $zip
  Expand-Archive $zip -DestinationPath $py -Force
  Remove-Item $zip
}

# The embeddable Python ignores site-packages until "import site" is enabled in its ._pth file
$pth = Get-ChildItem $py -Filter 'python*._pth' | Select-Object -First 1
(Get-Content $pth.FullName) -replace '^#\s*import site', 'import site' | Set-Content $pth.FullName -Encoding ascii

if (-not (Test-Path (Join-Path $py 'Lib\site-packages\pip'))) {
  $getPip = Join-Path $py 'get-pip.py'
  Write-Host 'Installing pip into the portable Python...'
  Invoke-WebRequest 'https://bootstrap.pypa.io/get-pip.py' -OutFile $getPip
  & $exe $getPip --no-cache-dir --no-warn-script-location
  Remove-Item $getPip
}

Write-Host 'Installing numpy and imageio-ffmpeg...'
& $exe -m pip install --no-cache-dir --no-warn-script-location --disable-pip-version-check numpy imageio-ffmpeg

Write-Host "Done. Run: `"$exe`" `"$(Join-Path $here 'make_eotf_chart.py')`""
