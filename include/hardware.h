#pragma once
#include <array>
#include <cstdint>

// Bảng đấu dây dùng chung cho firmware chính và chương trình chẩn đoán.
// Áp dụng cho ESP32 Dev Module, relay kích mức HIGH và 4 cảm biến MC-38
// loại tiếp điểm khô 2 dây. Mỗi cửa cần một bộ công tắc từ + nam châm riêng.
namespace hardware {
struct Channel {
  uint8_t relayChannel;  // Kênh Backend: 0..3 tương ứng ô vật lý 1..4; không phải UUID slot_id.
  int relayPin;         // Nối vào IN1..IN4 của module relay, không nối trực tiếp vào khóa 12V.
  int sensorPin;        // Một dây cảm biến vào GPIO này, dây còn lại vào GND.
  bool relayActiveHigh; // true: jumper H; false: jumper L trên module relay tương thích.
  bool sensorClosedLow; // true: cửa đóng đọc LOW; false: cửa đóng đọc HIGH.
};

// Đo thông mạch trước khi lắp: mặc định nam châm áp sát -> tiếp điểm thông -> LOW.
// Nếu cảm biến thực tế ngược lại, đổi sensorClosedLow của đúng cửa thành false.
// Dù đảo trạng thái, cảm biến 2 dây vẫn nối GPIO-GND và dùng INPUT_PULLUP.
// Không cấp 5V/12V vào GPIO. Bản cảm biến có mạch điện tử cần kiểm tra riêng.
inline constexpr std::array<Channel, 4> channels{{
    {0, 26, 27, true, true}, // Ô 1: IN1 = D26, cảm biến = D27.
    {1, 14, 13, true, true}, // Ô 2: IN2 = D14, cảm biến = D13 (thay D12 cũ).
    {2, 25, 32, true, true}, // Ô 3: IN3 = D25, cảm biến = D32.
    {3, 33, 18, true, true}, // Ô 4: IN4 = D33, cảm biến = D18 (thay D35 cũ).
}};

// Tránh GPIO12 vì là chân strapping; GPIO35 không có điện trở kéo lên nội.
// Tham khảo: https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/peripherals/gpio.html
inline constexpr uint32_t pulseMs = 500;          // Chỉ kích xung, không giữ điện khóa liên tục.
inline constexpr uint32_t sensorDebounceMs = 40; // Chỉ công nhận tín hiệu ổn định ít nhất 40 ms.
}  // namespace hardware
