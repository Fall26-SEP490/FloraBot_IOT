# Script mo Serial Monitor 115200 baud tren COM6
Push-Location $PSScriptRoot
try {
  Write-Host "Dang ket noi Serial Monitor COM6 (115200 baud)... Nhan Ctrl+C de thoat." -ForegroundColor Cyan
  pio device monitor -b 115200 -p COM6
} finally {
  Pop-Location
}
