# Script nap firmware FloraBot len ESP32 qua COM6
$ErrorActionPreference = 'Stop'
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "  Nap Firmware FloraBot len ESP32 (COM6)...  " -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan

Push-Location $PSScriptRoot
try {
  pio run -e esp32dev -t upload
  if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[THANH CONG] Da nap firmware thanh cong len ESP32!" -ForegroundColor Green
    Write-Host "De xem Serial Monitor, chay: .\monitor.ps1" -ForegroundColor Yellow
  }
} finally {
  Pop-Location
}
