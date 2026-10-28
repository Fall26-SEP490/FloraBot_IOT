# Script mo Serial Monitor 115200 baud tren COM3
Push-Location $PSScriptRoot
try {
  Write-Host "Dang ket noi Serial Monitor COM3 (115200 baud)... Nhan Ctrl+C de thoat." -ForegroundColor Cyan
  python -m platformio device monitor -b 115200 -p COM3
} finally {
  Pop-Location
}
