# Script kiem tra ket noi chip ESP32 tren COM6
python -m esptool --port COM6 chip-id
python -m esptool --port COM6 flash-id
