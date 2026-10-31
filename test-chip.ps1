# Doc thong tin chip/flash, mac dinh COM6. Chi dung khi bo chi cam USB.
param([string]$Port = 'COM6')
$ErrorActionPreference = 'Stop'
python -m esptool --port $Port chip-id
if ($LASTEXITCODE -ne 0) { throw "Khong doc duoc chip (exit code $LASTEXITCODE)." }
python -m esptool --port $Port flash-id
if ($LASTEXITCODE -ne 0) { throw "Khong doc duoc flash (exit code $LASTEXITCODE)." }
