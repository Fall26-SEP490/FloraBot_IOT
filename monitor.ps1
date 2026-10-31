# Serial Monitor 115200 baud, mac dinh COM6.
param([string]$Port = 'COM6')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
  Write-Host "Dang ket noi Serial Monitor $Port (115200 baud)... Nhan Ctrl+C de thoat." -ForegroundColor Cyan
  python -m platformio device monitor -b 115200 -p $Port
  if ($LASTEXITCODE -ne 0) { throw "Serial Monitor that bai (exit code $LASTEXITCODE)." }
} finally {
  Pop-Location
}
