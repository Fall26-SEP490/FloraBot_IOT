# Nap firmware chan doan, mac dinh COM6.
param([string]$Port = 'COM6')

$ErrorActionPreference = 'Stop'
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Nap Firmware Chan Doan Phan Cung (Diagnostics) $Port...  " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

Push-Location $PSScriptRoot
try {
  python -m platformio run -e diagnostics -t upload --upload-port $Port
  if ($LASTEXITCODE -ne 0) {
    throw "Nap firmware chan doan that bai (exit code $LASTEXITCODE)."
  } else {
    Write-Host "`n[THANH CONG] Da nap firmware Chan doan thanh cong!" -ForegroundColor Green
    Write-Host "Dang mo Serial Monitor de xem ket qua test..." -ForegroundColor Yellow
    python -m platformio device monitor -b 115200 -p $Port
    if ($LASTEXITCODE -ne 0) { throw "Serial Monitor that bai (exit code $LASTEXITCODE)." }
  }
} finally {
  Pop-Location
}
