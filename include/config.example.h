#pragma once
#include <array>
#include <cstdint>
#include "hardware.h"

namespace config {
using Channel = hardware::Channel;
inline constexpr char hardwareId[] = "";
inline constexpr char wifiSsid[] = "";
inline constexpr char wifiPassword[] = "";
inline constexpr char mqttHost[] = "";
inline constexpr uint16_t mqttPort = 8883;
inline constexpr char mqttPassword[] = "";
inline constexpr char hmacKeyHex[] = "";
inline constexpr char caCertificate[] = "";
inline constexpr char ntpServer[] = "pool.ntp.org";
// Mặc định 4 khóa + 4 cảm biến MC-38 theo bảng chân trong hardware.h.
// Điền đầy đủ thông tin kết nối bên trên trước khi chạy cùng Backend.
inline constexpr auto channels = hardware::channels;
inline constexpr int recoveryButtonPin = -1;
inline constexpr uint32_t pulseMs = 500;
inline constexpr uint32_t sensorDebounceMs = 40;
inline constexpr uint32_t doorTimeoutMs = 60000;
}  // namespace config
