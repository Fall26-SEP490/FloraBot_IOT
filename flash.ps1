# Nap firmware cho ESP32 classic (30/38 chan), mac dinh COM6.
param([string]$Port = 'COM6')

$ErrorActionPreference = 'Stop'
$env:PYTHONIOENCODING = 'utf-8'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "  Nap Firmware FloraBot len ESP32 ($Port)...  " -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan

Push-Location $PSScriptRoot
try {
  python -m platformio run -e esp32dev -t upload --upload-port $Port
  if ($LASTEXITCODE -ne 0) {
    throw "Nap firmware that bai (exit code $LASTEXITCODE)."
  } else {
    Write-Host "`n[THANH CONG] Da nap firmware thanh cong len ESP32!" -ForegroundColor Green
    Write-Host "De xem Serial Monitor, chay: .\monitor.ps1 -Port $Port" -ForegroundColor Yellow
  }
} finally {
  Pop-Location
}
