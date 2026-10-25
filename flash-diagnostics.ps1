# Script nap firmware Chan Doan Phan Cung (Diagnostics) len ESP32 qua COM6
$ErrorActionPreference = 'Stop'
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Nap Firmware Chan Doan Phan Cung (Diagnostics) COM6...  " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

Push-Location $PSScriptRoot
try {
  pio run -e diagnostics -t upload
  if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[THANH CONG] Da nap firmware Chan doan thanh cong!" -ForegroundColor Green
    Write-Host "Dang mo Serial Monitor de xem ket qua test..." -ForegroundColor Yellow
    pio device monitor -b 115200 -p COM6
  }
} finally {
  Pop-Location
}
