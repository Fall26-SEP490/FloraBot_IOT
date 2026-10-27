#pragma once
#include <array>
#include <cstdint>
#include "hardware.h"
// Non-operational fixture, only for linking every hardware path. Never flash this target.
namespace config {
using Channel = hardware::Channel;
inline constexpr char hardwareId[] = "ESP32-COMPILE-CHECK";
inline constexpr char wifiSsid[] = "compile-check-only";
inline constexpr char wifiPassword[] = "not-a-real-password";
inline constexpr char mqttHost[] = "broker.invalid";
inline constexpr uint16_t mqttPort = 8883;
inline constexpr char mqttPassword[] = "not-a-real-password";
inline constexpr char hmacKeyHex[] = "0000000000000000000000000000000000000000000000000000000000000000";
inline constexpr char caCertificate[] = "-----BEGIN CERTIFICATE-----\nINVALID-COMPILE-FIXTURE\n-----END CERTIFICATE-----";
inline constexpr char ntpServer[] = "pool.ntp.org";
// Biên dịch đủ 4 cửa và đường xử lý nút bảo trì; chỉ là fixture, không được nạp.
inline constexpr auto channels = hardware::channels;
inline constexpr int recoveryButtonPin = 23;
inline constexpr uint32_t pulseMs = 500;
inline constexpr uint32_t sensorDebounceMs = 40;
inline constexpr uint32_t doorTimeoutMs = 60000;
}  // namespace config
