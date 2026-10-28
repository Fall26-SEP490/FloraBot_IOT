# Script nap firmware FloraBot len ESP32 qua COM3
$ErrorActionPreference = 'Stop'
$env:PYTHONIOENCODING = 'utf-8'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "  Nap Firmware FloraBot len ESP32 (COM3)...  " -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan

Push-Location $PSScriptRoot
try {
  python -m platformio run -e esp32dev -t upload --upload-port COM3
  if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[THANH CONG] Da nap firmware thanh cong len ESP32!" -ForegroundColor Green
    Write-Host "De xem Serial Monitor, chay: .\monitor.ps1" -ForegroundColor Yellow
  }
} finally {
  Pop-Location
}
