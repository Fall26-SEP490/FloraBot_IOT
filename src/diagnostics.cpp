/**
 * FLORABOT - CHẨN ĐOÁN 4 KHÓA ĐIỆN TỪ VÀ 4 CẢM BIẾN CỬA MC-38.
 * Chạy offline, không dùng mật khẩu Wi-Fi/MQTT và không gửi sự kiện đơn hàng.
 *
 * ĐẤU DÂY (xem thêm include/hardware.h):
 *   Ô 1: relay IN1 -> D26, cảm biến -> D27 + GND.
 *   Ô 2: relay IN2 -> D14, cảm biến -> D13 + GND (không dùng D12 cũ).
 *   Ô 3: relay IN3 -> D25, cảm biến -> D32 + GND.
 *   Ô 4: relay IN4 -> D33, cảm biến -> D18 + GND (không dùng D35 cũ).
 * Cảm biến là tiếp điểm khô 2 dây, không cấp 5V/12V vào GPIO.
 * Mặc định: nam châm gần -> thông mạch -> LOW -> cửa đóng; tách ra -> HIGH ->
 * cửa mở. Nếu cảm biến thực tế ngược lại, đổi sensorClosedLow của cửa đó trong
 * hardware.h. Mỗi khóa 12V đi qua COM/NO của một relay, có diode dập xung riêng
 * tại khóa.
 *
 * THAO TÁC SERIAL 115200: 1..4 mở từng khóa; s đọc cảm biến; a bật/tắt thử lần
 * lượt; w quét Wi-Fi; b đổi LED; h xem trợ giúp. Nút BOOT thử riêng khóa 1. Khi
 * khởi động không tự kích bất kỳ khóa nào.
 */
#include "hardware.h"
#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_timer.h>

namespace {
constexpr int PIN_LED = 2;
constexpr int PIN_BOOT_BTN = 0;
constexpr auto &CHANNELS = hardware::channels;
constexpr size_t NUM_CHANNELS = CHANNELS.size();

struct Sensor {
  bool reading = false;
  bool closed = false;
  uint32_t changed = 0;
};
std::array<Sensor, NUM_CHANNELS> sensors{};
struct Pulse {
  bool active = false;
  int64_t deadline = 0;
};
std::array<Pulse, NUM_CHANNELS> pulses{};
portMUX_TYPE pulseMutex = portMUX_INITIALIZER_UNLOCKED;
esp_timer_handle_t pulseTimer = nullptr;
bool ready = false, autoPulseEnabled = false, ledState = false,
     scanning = false;
uint32_t lastBlink = 0, lastAutoPulse = 0;
size_t autoPulseIndex = 0;
bool bootReading = HIGH, bootStable = HIGH;
uint32_t bootChanged = 0;

bool sensorClosed(size_t index) {
  const auto &channel = CHANNELS[index];
  return digitalRead(channel.sensorPin) ==
         (channel.sensorClosedLow ? LOW : HIGH);
}

// Timer riêng ngắt relay kể cả khi Serial/Wi-Fi đang bận; không delay trong lúc
// mở khóa. Mỗi kênh có thời hạn riêng, nên có thể thử nhiều khóa mà không bỏ
// qua cảm biến.
void pulseTick(void *) {
  portENTER_CRITICAL(&pulseMutex);
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    if (pulses[i].active && esp_timer_get_time() >= pulses[i].deadline) {
      gpio_set_level(static_cast<gpio_num_t>(CHANNELS[i].relayPin),
                     CHANNELS[i].relayActiveHigh ? 0 : 1);
      pulses[i].active = false;
    }
  }
  portEXIT_CRITICAL(&pulseMutex);
}

void pulseRelay(size_t index) {
  if (!ready || index >= NUM_CHANNELS)
    return;
  portENTER_CRITICAL(&pulseMutex);
  const bool busy = pulses[index].active;
  if (!busy) {
    pulses[index] = {true, esp_timer_get_time() +
                               static_cast<int64_t>(hardware::pulseMs) * 1000};
    gpio_set_level(static_cast<gpio_num_t>(CHANNELS[index].relayPin),
                   CHANNELS[index].relayActiveHigh ? 1 : 0);
  }
  portEXIT_CRITICAL(&pulseMutex);
  // Nhấn lặp không kéo dài xung đang chạy, tránh giữ cuộn khóa quá lâu.
  if (busy)
    Serial.printf("[RELAY] O %u dang kich; bo qua lenh lap.\n",
                  static_cast<unsigned>(index + 1));
  else
    Serial.printf("[RELAY] O %u / kenh %u / GPIO %d: xung %lu ms.\n",
                  static_cast<unsigned>(index + 1),
                  CHANNELS[index].relayChannel, CHANNELS[index].relayPin,
                  static_cast<unsigned long>(hardware::pulseMs));
}

void printBanner() {
  Serial.println("\n=== FLORABOT: 4 KHOA + 4 CAM BIEN MC-38 ===");
  Serial.printf("Chip: %s | Flash: %u MB\n", ESP.getChipModel(),
                ESP.getFlashChipSize() / (1024 * 1024));
  Serial.println("1, 2, 3, 4: thu khoa tuong ung (xung 500 ms)");
  Serial.println("BOOT: thu khoa 1 | a: bat/tat thu lan luot moi 3 giay");
  Serial.println(
      "s: doc 4 cam bien | w: quet Wi-Fi | b: doi LED | h/?: tro giup");
  Serial.println("DOOR OPENED chi xac nhan cua mo; khong xac nhan da lay hoa.");
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    Serial.printf("  O %u: relay D%d / cam bien D%d, cua dong = %s\n",
                  static_cast<unsigned>(i + 1), CHANNELS[i].relayPin,
                  CHANNELS[i].sensorPin,
                  CHANNELS[i].sensorClosedLow ? "LOW" : "HIGH");
  }
}

void readAllSensors() {
  Serial.println("\n[SENSORS] Trang thai da loc nhieu va muc GPIO tuc thoi:");
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    Serial.printf("  O %u (GPIO %d): %s | raw=%s\n",
                  static_cast<unsigned>(i + 1), CHANNELS[i].sensorPin,
                  sensors[i].closed ? "CLOSED" : "OPENED",
                  digitalRead(CHANNELS[i].sensorPin) == LOW ? "LOW" : "HIGH");
  }
}

// Quét bất đồng bộ để vẫn theo dõi cửa trong khi tìm mạng Wi-Fi.
void scanWiFi() {
  if (scanning) {
    Serial.println("[WiFi] Dang quet, vui long doi.");
    return;
  }
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true);
  scanning = result != WIFI_SCAN_FAILED;
  Serial.println(scanning ? "[WiFi] Bat dau quet..."
                          : "[WiFi] Khong khoi dong duoc quet.");
}

void pollWiFiScan() {
  if (!scanning)
    return;
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING)
    return;
  if (count < 0)
    Serial.println("[WiFi] Quet that bai.");
  else {
    Serial.printf("[WiFi] Tim thay %d mang:\n", count);
    for (int i = 0; i < count; ++i)
      Serial.printf("  %-32s | %d dBm | Ch %d\n", WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i), WiFi.channel(i));
  }
  WiFi.scanDelete();
  scanning = false;
}
} // namespace

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    // Ghi mức tắt trước khi chuyển OUTPUT, hỗ trợ cả relay kích HIGH hoặc LOW.
    digitalWrite(CHANNELS[i].relayPin,
                 CHANNELS[i].relayActiveHigh ? LOW : HIGH);
    pinMode(CHANNELS[i].relayPin, OUTPUT);
    // Cảm biến khô chỉ nối GPIO-GND: luôn PULLUP, kể cả khi đảo
    // sensorClosedLow.
    pinMode(CHANNELS[i].sensorPin, INPUT_PULLUP);
  }
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  delay(50); // Cho điện trở kéo lên và cảm biến ổn định trước lần đọc đầu.
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    const bool closed = sensorClosed(i);
    sensors[i] = {closed, closed, millis()};
  }
  bootReading = bootStable = digitalRead(PIN_BOOT_BTN);
  bootChanged = millis();
  esp_timer_create_args_t timer{};
  timer.callback = pulseTick;
  timer.name = "diag-relay-limit";
  if (esp_timer_create(&timer, &pulseTimer) != ESP_OK ||
      esp_timer_start_periodic(pulseTimer, 10000) != ESP_OK) {
    Serial.println("[FATAL] Khong tao duoc timer gioi han xung; khong cho phep "
                   "kich khoa.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  ready = true;
  printBanner();
  readAllSensors();
  Serial.println("[SAN SANG] Khong tu mo khoa luc khoi dong. Bam 1..4 de thu.");
}

void loop() {
  if (!ready) {
    delay(20);
    return;
  }
  const uint32_t now = millis();
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    auto &sensor = sensors[i];
    const bool reading = sensorClosed(i);
    if (reading != sensor.reading) {
      sensor.reading = reading;
      sensor.changed = now;
    }
    // Chỉ báo thay đổi khi tiếp điểm ổn định; tránh log mở/đóng giả do rung
    // tiếp điểm.
    if (reading != sensor.closed &&
        static_cast<uint32_t>(now - sensor.changed) >=
            hardware::sensorDebounceMs) {
      sensor.closed = reading;
      Serial.printf("[SENSOR] O %u (GPIO %d): %s\n",
                    static_cast<unsigned>(i + 1), CHANNELS[i].sensorPin,
                    reading ? "DOOR CLOSED" : "DOOR OPENED");
    }
  }
  const bool button = digitalRead(PIN_BOOT_BTN);
  if (button != bootReading) {
    bootReading = button;
    bootChanged = now;
  }
  if (button != bootStable &&
      static_cast<uint32_t>(now - bootChanged) >= hardware::sensorDebounceMs) {
    bootStable = button;
    if (button == LOW)
      pulseRelay(0);
  }
  if (static_cast<uint32_t>(now - lastBlink) >= 1000) {
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(PIN_LED, ledState ? HIGH : LOW);
  }
  if (autoPulseEnabled && static_cast<uint32_t>(now - lastAutoPulse) >= 3000) {
    lastAutoPulse = now;
    pulseRelay(autoPulseIndex);
    autoPulseIndex = (autoPulseIndex + 1) % NUM_CHANNELS;
  }
  pollWiFiScan();
  if (Serial.available()) {
    const char cmd = Serial.read();
    if (cmd >= '1' && cmd <= '4')
      pulseRelay(static_cast<size_t>(cmd - '1'));
    else
      switch (cmd) {
      case 'a':
      case 'A':
        autoPulseEnabled = !autoPulseEnabled;
        lastAutoPulse = millis();
        autoPulseIndex = 0;
        Serial.printf("[AUTO] %s: thu lan luot 4 khoa moi 3 giay.\n",
                      autoPulseEnabled ? "BAT" : "TAT");
        break;
      case 's':
      case 'S':
        readAllSensors();
        break;
      case 'w':
      case 'W':
        scanWiFi();
        break;
      case 'b':
      case 'B':
        ledState = !ledState;
        digitalWrite(PIN_LED, ledState ? HIGH : LOW);
        break;
      case 'h':
      case 'H':
      case '?':
        printBanner();
        break;
      case '\r':
      case '\n':
      case ' ':
        break;
      default:
        Serial.println("[CMD] Lenh khong hop le. Bam h de xem tro giup.");
        break;
      }
  }
  delay(5);
}
