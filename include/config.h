#pragma once
#include <array>
#include <cstdint>

namespace config {

struct Channel {
  uint8_t relayChannel; // Kiosk locker slot relay channel (0, 1, 2...)
  int relayPin;         // ESP32 GPIO pin connected to Relay IN
  int sensorPin;        // ESP32 GPIO pin connected to Door Reed Switch
  bool relayActiveHigh; // true: HIGH energizes lock, false: LOW energizes lock
  bool sensorClosedLow; // true: door closed pulls pin LOW to GND, false: HIGH
};

// Hardware ID - Must match database kiosk record and Mosquitto ACL
// Default FloraBot Kiosk: ESP32-A1B2C3
inline constexpr char hardwareId[] = "ESP32-A1B2C3";

// Wi-Fi Configuration
inline constexpr char wifiSsid[] = "YOUR_WIFI_SSID";         // <-- Thay ten WiFi nha/lop ban
inline constexpr char wifiPassword[] = "YOUR_WIFI_PASSWORD"; // <-- Thay mat khau WiFi

// MQTT Broker Configuration (May tinh chay FloraBot_BE / Docker Mosquitto)
// Luu y: Phai dung dia chi IP LAN cua may tinh (vd: 192.168.1.15), khong dung localhost/127.0.0.1
inline constexpr char mqttHost[] = "192.168.1.100";          // <-- Thay IP LAN cua may tinh
inline constexpr uint16_t mqttPort = 8883;                   // Port MQTT TLS (8883)
inline constexpr char mqttPassword[] = "florabot-secret-password"; // Mat khau thiet bi tren broker

// 32-byte HMAC secret key (64 hex characters) - Dung de xac thuc chu ky lenh mo cua v1
inline constexpr char hmacKeyHex[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// CA Certificate (PEM string) - Dung de bao mat TLS ket noi toi Mosquitto Broker
inline constexpr char caCertificate[] = R"(-----BEGIN CERTIFICATE-----
MIIDazCCAlOgAwIBAgIUQz3lK8Zf9V3x1WvQ2m5s0Y4Z1QIwDQYJKoZIhvcNAQEL
BQAwIjEgMB4GA1UEAwwXRmxvcmFCb3QgTG9jYWwgTVFUVCBDQTAeFw0yNjEwMjUw
...
-----END CERTIFICATE-----)";

inline constexpr char ntpServer[] = "pool.ntp.org";

// Anh xa kenh dieu khien o khoa tu (Locker slots):
// D26 -> Relay o 1, D27 -> Cam bien tu o 1
// D14 -> Relay o 2, D12 -> Cam bien tu o 2
inline constexpr std::array<Channel, 2> channels{{{0, 26, 27, true, true}, {1, 14, 12, true, true}}};

inline constexpr int recoveryButtonPin = -1; // -1 neu khong dung nut bao tri phan cung
inline constexpr uint32_t pulseMs = 500;     // Thoi gian kich xung mo khoa dien tu (500ms)
inline constexpr uint32_t sensorDebounceMs = 40; // Chong rung tin hieu cam bien (40ms)
inline constexpr uint32_t doorTimeoutMs = 60000; // Thoi gian toi da cho khach lay hoa (60 giay)

}  // namespace config
