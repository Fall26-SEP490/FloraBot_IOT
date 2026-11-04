/**
 * FLORABOT - CHẨN ĐOÁN 4 KHÓA, 4 MC-38 VÀ 4 CẢM BIẾN VẬT CẢN LM393.
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
 * LM393 ô 1..4: OUT -> D21, D22, D19, D23; VCC -> 3V3, GND -> GND.
 * Có thể chỉ cắm một cảm biến; chân để hở đọc HIGH nhờ INPUT_PULLUP.
 * HIGH không phân biệt ô không có vật với chưa cắm cảm biến. Không cấp 12V;
 * không đưa OUT 5V vào ESP32. Chỉnh biến trở để bỏ qua vách tủ khi ô trống.
 *
 * THAO TÁC SERIAL 115200: 1..4 mở từng khóa; s đọc cảm biến; a bật/tắt thử lần
 * lượt; i đọc LM393; p bật/tắt theo dõi LM393 mỗi 500 ms; w quét Wi-Fi;
 * b đổi LED; h xem trợ giúp. Nút BOOT thử riêng khóa 1. Khi
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
// Thử tự động: nghỉ 500 ms sau xung trước khi kích khóa kế tiếp.
constexpr uint32_t AUTO_PULSE_INTERVAL_MS = hardware::pulseMs + 500;
static_assert(hardware::pulseMs > 0 && hardware::pulseMs <= hardware::maxPulseMs,
              "Diagnostics relay pulse exceeds configured limit");
// Test độc lập từng ô; không suy ra khoảng cách hoặc trạng thái đơn hàng.
constexpr auto LM393_PINS = hardware::flowerSensorPins;
constexpr size_t NUM_LM393 = LM393_PINS.size();
constexpr int LM393_DETECTED_LEVEL = LOW;
constexpr uint32_t LM393_STABLE_MS = hardware::flowerStableMs;
constexpr uint32_t LM393_TRACE_MS = 500;

constexpr bool lm393PinsAvailable() {
  for (size_t i = 0; i < NUM_LM393; ++i) {
    const int pin = LM393_PINS[i];
    if (pin != 19 && pin != 21 && pin != 22 && pin != 23)
      return false;
    if (pin == PIN_LED || pin == PIN_BOOT_BTN)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (pin == LM393_PINS[j])
        return false;
    for (const auto &channel : CHANNELS)
      if (channel.relayPin == pin || channel.sensorPin == pin)
        return false;
  }
  return true;
}
static_assert(NUM_LM393 == NUM_CHANNELS, "Each door needs one LM393 test channel");
static_assert(lm393PinsAvailable(), "Invalid or conflicting LM393 GPIOs");

struct PresenceSensor {
  bool reading = false;
  int confirmed = -1; // Chưa kết luận cho đến khi OUT ổn định đủ 1 giây.
  uint32_t changed = 0;
};
std::array<PresenceSensor, NUM_LM393> lm393Sensors{};
bool traceLm393 = false;
uint32_t lastLm393Trace = 0;

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

bool lm393Detected(size_t index) {
  return digitalRead(LM393_PINS[index]) == LM393_DETECTED_LEVEL;
}

const char *lm393Status(size_t index) {
  const auto &sensor = lm393Sensors[index];
  if (sensor.confirmed < 0)
    return "CHUA XAC DINH";
  return sensor.confirmed ? "CO VAT tai vi tri dat hoa"
                          : "KHONG PHAT HIEN VAT (HOAC CHUA CAM CAM BIEN)";
}

void readLm393() {
  for (size_t i = 0; i < NUM_LM393; ++i) {
    const auto &sensor = lm393Sensors[i];
    const int raw = digitalRead(LM393_PINS[i]);
    const bool rawDetected = raw == LM393_DETECTED_LEVEL;
    const bool pending = sensor.confirmed < 0 ||
                         static_cast<int>(rawDetected) != sensor.confirmed ||
                         rawDetected != sensor.reading;
    Serial.printf("[LM393] O %u | GPIO %d | raw=%s | on dinh=%s%s\n",
                  static_cast<unsigned>(i + 1), LM393_PINS[i],
                  raw == LOW ? "LOW" : "HIGH", lm393Status(i),
                  pending ? " | DANG CHO ON DINH 1000 ms" : "");
  }
}

void pollLm393(uint32_t now) {
  for (size_t i = 0; i < NUM_LM393; ++i) {
    auto &sensor = lm393Sensors[i];
    const bool reading = lm393Detected(i);
    if (reading != sensor.reading) {
      sensor.reading = reading;
      sensor.changed = now;
    }
    if (static_cast<uint32_t>(now - sensor.changed) >= LM393_STABLE_MS &&
        sensor.confirmed != static_cast<int>(reading)) {
      sensor.confirmed = reading;
      Serial.printf("[LM393] O %u | GPIO %d | ON DINH %lu ms: %s\n",
                    static_cast<unsigned>(i + 1), LM393_PINS[i],
                    static_cast<unsigned long>(LM393_STABLE_MS), lm393Status(i));
    }
  }
  if (traceLm393 &&
      static_cast<uint32_t>(now - lastLm393Trace) >= LM393_TRACE_MS) {
    lastLm393Trace = now;
    readLm393();
  }
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
  Serial.println("\n=== FLORABOT: 4 KHOA + 4 MC-38 + 4 LM393 ===");
  Serial.printf("Chip: %s | Flash: %u MB\n", ESP.getChipModel(),
                ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("1, 2, 3, 4: thu khoa tuong ung (xung %lu ms)\n",
                static_cast<unsigned long>(hardware::pulseMs));
  Serial.printf("BOOT: thu khoa 1 | a: thu lan luot moi %lu ms\n",
                static_cast<unsigned long>(AUTO_PULSE_INTERVAL_MS));
  Serial.println(
      "s: doc tat ca cam bien | w: quet Wi-Fi | b: doi LED | h/?: tro giup");
  Serial.println("i: doc 4 LM393 | p: bat/tat theo doi 4 LM393 moi 500 ms");
  Serial.printf("LM393: VCC=3V3, GND=GND; phat hien=%s; loc rieng tung o %lu ms.\n",
                LM393_DETECTED_LEVEL == LOW ? "LOW" : "HIGH",
                static_cast<unsigned long>(LM393_STABLE_MS));
  Serial.println("Chinh bien tro: o trong/vach 15 cm -> khong phat hien; hoa 5 cm -> co vat.");
  Serial.println("LM393 khong do cm; OUT HIGH cung co the do chua cam cam bien.");
  Serial.println("DOOR OPENED chi xac nhan cua mo; khong xac nhan da lay hoa.");
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    Serial.printf("  O %u: relay D%d / MC-38 D%d / LM393 OUT D%d, cua dong = %s\n",
                  static_cast<unsigned>(i + 1), CHANNELS[i].relayPin,
                  CHANNELS[i].sensorPin, LM393_PINS[i],
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
  readLm393();
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
    // Arduino 3.x bỏ qua digitalWrite trước pinMode; dùng ESP-IDF để
    // nạp mức tắt vào output latch trước khi bật ngõ ra.
    gpio_set_level(static_cast<gpio_num_t>(CHANNELS[i].relayPin),
                   CHANNELS[i].relayActiveHigh ? 0 : 1);
    pinMode(CHANNELS[i].relayPin, OUTPUT);
    digitalWrite(CHANNELS[i].relayPin,
                 CHANNELS[i].relayActiveHigh ? LOW : HIGH);
    // Cảm biến khô chỉ nối GPIO-GND: luôn PULLUP, kể cả khi đảo
    // sensorClosedLow.
    pinMode(CHANNELS[i].sensorPin, INPUT_PULLUP);
  }
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  for (int pin : LM393_PINS)
    pinMode(pin, INPUT_PULLUP);
  delay(50); // Cho điện trở kéo lên và cảm biến ổn định trước lần đọc đầu.
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    const bool closed = sensorClosed(i);
    sensors[i] = {closed, closed, millis()};
  }
  bootReading = bootStable = digitalRead(PIN_BOOT_BTN);
  bootChanged = millis();
  for (size_t i = 0; i < NUM_LM393; ++i)
    lm393Sensors[i] = {lm393Detected(i), -1, millis()};
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
  pollLm393(now);
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
  if (autoPulseEnabled &&
      static_cast<uint32_t>(now - lastAutoPulse) >= AUTO_PULSE_INTERVAL_MS) {
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
        Serial.printf("[AUTO] %s: thu lan luot 4 khoa moi %lu ms.\n",
                      autoPulseEnabled ? "BAT" : "TAT",
                      static_cast<unsigned long>(AUTO_PULSE_INTERVAL_MS));
        break;
      case 's':
      case 'S':
        readAllSensors();
        break;
      case 'i':
      case 'I':
        readLm393();
        break;
      case 'p':
      case 'P':
        traceLm393 = !traceLm393;
        lastLm393Trace = millis();
        Serial.printf("[LM393] Theo doi moi %lu ms: %s.\n",
                      static_cast<unsigned long>(LM393_TRACE_MS),
                      traceLm393 ? "BAT" : "TAT");
        readLm393();
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
