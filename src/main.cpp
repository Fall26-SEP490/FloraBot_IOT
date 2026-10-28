/**
 * ============================================================================
 * FLORABOT - AI POWERED AUTOMATED FLOWER KIOSK (ESP32 FIRMWARE)
 * Đồ án Capstone: FA26SE164
 * 
 * Mô tả:
 *   Firmware điều khiển phần cứng Kiosk bán hoa tự động chạy trên ESP32:
 *   1. Kết nối Wi-Fi & MQTT TLS (bảo mật chứng chỉ CA x509, cổng 8883).
 *   2. Nhận và giải mã lệnh mở hộc tủ (Unlock Command) theo chuẩn v1:
 *      - Xác thực chữ ký HMAC-SHA256 với khóa bí mật riêng của từng Kiosk.
 *      - Kiểm tra tính hợp lệ thời gian thực qua NTP (chống tấn công replay/hết hạn).
 *   3. Điều khiển độc lập 4 khóa điện từ qua relay, xung mở mặc định 500 ms.
 *   4. FreeRTOS Task giám sát 4 cảm biến cửa MC-38 và lọc nhiễu 40 ms.
 *   5. Gửi sự kiện có chữ ký về Backend: ACK, OPENED, CLOSED, HEARTBEAT (định kỳ 30s).
 *   6. Tuân thủ kiến trúc ADR 0021 (Durable Core) & ADR 0022 (ESP32 Application):
 *      Lưu vết trạng thái vào bộ nhớ flash NVS Journal để chống mất điện đột ngột.
 *
 * Ghi chú đấu dây: bảng chân mặc định nằm trong include/hardware.h:
 *   Ô 1: relay D26 / cảm biến D27; ô 2: relay D14 / cảm biến D13.
 *   Ô 3: relay D25 / cảm biến D32; ô 4: relay D33 / cảm biến D18.
 * Mỗi cảm biến tiếp điểm khô nối giữa GPIO và GND, dùng INPUT_PULLUP.
 * Không đưa nguồn khóa 12V vào GPIO. Các kênh relay 0..3 phải khớp Backend.
 * Nếu có config.local.h, kiểm tra channels trong file đó vì nó được ưu tiên.
 * ============================================================================
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <driver/gpio.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <atomic>
#include <memory>
#include <new>
#include "Journal.h"
#include "DoorBank.h"
#include "Storage.h"
#include "MbedHmac.h"
#if defined(FLORA_COMPILE_CHECK)
#include "../test/config.compilecheck.h"
#elif __has_include("config.local.h")
#include "config.local.h"
#elif __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

namespace {
Preferences preferences;
WiFiClientSecure tls;
PubSubClient mqtt(tls);
flora::Digest deviceKey{};
flora::DoorBank doors;
SemaphoreHandle_t journalMutex = nullptr;
esp_timer_handle_t pulseTimer = nullptr;
portMUX_TYPE pulseMutex = portMUX_INITIALIZER_UNLOCKED;
struct Pulse { bool active = false; int64_t deadline = 0; };
std::array<Pulse, config::channels.size()> pulses{};
std::atomic<bool> synced{false}, fatal{false};
std::atomic<uint32_t> lastSync{0};
bool configured = false, subscribed = false, subscriptionFailed = false;
uint32_t lastConnect = 0, lastHeartbeat = 0, sentAt = 0, subscribeAt = 0;
uint16_t inflightPacket = 0, acknowledgedPacket = 0;
std::string inflightEvent, inflightPayload, commandTopic, eventTopic;
uint8_t inflightRelay = 0;
size_t eventCursor = 0;
// Mỗi cửa có bộ đếm thời gian, cảm biến và journal riêng; cửa này mở không chặn cửa khác.
std::array<uint32_t, config::channels.size()> cycleStarted{};
struct Sensor { bool reading = false; bool closed = false; uint32_t changed = 0; };
std::array<Sensor, config::channels.size()> sensors{};

class Lock {
 public:
  Lock() { xSemaphoreTake(journalMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(journalMutex); }
};

bool hmac(const flora::Digest& key, const std::string& message, flora::Digest& output) {
  return flora::mbedHmac(key, message, output);
}

std::string uuid() {
  uint8_t bytes[16]; esp_fill_random(bytes, sizeof(bytes));
  bytes[6] = (bytes[6] & 15) | 0x40; bytes[8] = (bytes[8] & 63) | 0x80;
  char result[37];
  snprintf(result, sizeof(result), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
      bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return result;
}

void clockSynchronized(timeval*) { lastSync = millis(); synced = true; }
bool clockReady() {
  return synced && static_cast<uint32_t>(millis() - lastSync.load()) < 86400000 && time(nullptr) > 1700000000;
}

bool commit(const std::string& key, const flora::Snapshot& snapshot) {
  const auto encoded = flora::encodeSnapshot(snapshot);
  return !encoded.empty() && preferences.putBytes(key.c_str(), encoded.data(), encoded.size()) == encoded.size();
}

bool loadJournal() {
  if (nvs_flash_init_partition("journal") != ESP_OK || !preferences.begin("florabot", false, "journal")) return false;
  // Giữ nguyên dấu chống mở lặp khi nâng cấp từ 1/2 cửa lên 4 cửa.
  // Nếu NVS đã ghi bộ kênh cũ, dừng để bảo trì/migrate có kiểm soát; không tự xóa flash.
  if (preferences.isKey("journal")) return false;
  uint64_t relayMask = 0;
  for (const auto& channel : config::channels) relayMask |= uint64_t{1} << channel.relayChannel;
  const bool initialized = preferences.getBool("initialized", false);
  if (initialized && (!preferences.isKey("relays") || preferences.getULong64("relays") != relayMask)) {
    Serial.println("[NVS] Bo kenh relay da thay doi; can bao tri journal, khong tu xoa du lieu cu.");
    return false;
  }
  auto saved = std::unique_ptr<flora::Snapshot>(new (std::nothrow) flora::Snapshot{});
  if (!saved) return false;
  for (const auto& channel : config::channels) {
    const std::string key = "door" + std::to_string(channel.relayChannel);
    *saved = flora::Snapshot{};
    const size_t size = preferences.getBytesLength(key.c_str());
    if (size == 0) {
      if (initialized || !commit(key, *saved)) return false;
    } else {
      if (size > 4096) return false;
      std::vector<uint8_t> encoded(size);
      if (preferences.getBytes(key.c_str(), encoded.data(), size) != size ||
          !flora::decodeSnapshot(encoded.data(), size, *saved)) return false;
      if (!initialized && (saved->clockFloor != 0 || saved->phase != flora::Phase::Idle)) return false;
    }
    if (!doors.add(channel.relayChannel, *saved, [key](const flora::Snapshot& snapshot) { return commit(key, snapshot); })) return false;
  }
  if (!initialized && (preferences.putULong64("relays", relayMask) != sizeof(relayMask) ||
      preferences.putBool("initialized", true) != 1)) return false;
  return doors.healthy();
}

bool safePin(int pin) {
  // Classic ESP32 only: exclude flash, strapping, UART and input-only pins.
  constexpr int allowed[] = {13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
  for (int candidate : allowed) if (candidate == pin) return true;
  return false;
}
bool validConfiguration() {
  if (!flora::hardwareId(config::hardwareId)) {
    Serial.printf("[Config Error] Invalid hardwareId: '%s'\n", config::hardwareId);
    return false;
  }
  if (!config::wifiSsid[0]) {
    Serial.println("[Config Error] wifiSsid is empty in config.h!");
    return false;
  }
  if (!config::mqttHost[0]) {
    Serial.println("[Config Error] mqttHost is empty in config.h!");
    return false;
  }
  if (!config::mqttPassword[0]) {
    Serial.println("[Config Error] mqttPassword is empty in config.h!");
    return false;
  }
  if (!strstr(config::caCertificate, "BEGIN CERTIFICATE")) {
    Serial.println("[Config Error] caCertificate is missing or lacks 'BEGIN CERTIFICATE'!");
    return false;
  }
  if (!flora::decodeHex(config::hmacKeyHex, deviceKey)) {
    Serial.println("[Config Error] hmacKeyHex must be a valid 64-hex-char HMAC key!");
    return false;
  }
  if (config::channels.empty()) {
    Serial.println("[Config Error] config::channels list is empty!");
    return false;
  }
  if (config::pulseMs == 0 || config::pulseMs > 1000) {
    Serial.printf("[Config Error] pulseMs must be 1..1000 ms (current: %u)\n", config::pulseMs);
    return false;
  }
  if (config::sensorDebounceMs < 20) {
    Serial.printf("[Config Error] sensorDebounceMs must be >= 20 ms (current: %u)\n", config::sensorDebounceMs);
    return false;
  }
  if (config::doorTimeoutMs < 1000 || config::doorTimeoutMs > 120000) {
    Serial.printf("[Config Error] doorTimeoutMs must be 1000..120000 ms (current: %u)\n", config::doorTimeoutMs);
    return false;
  }
  bool pins[40] = {}, relays[64] = {};
  for (const auto& channel : config::channels) {
    if (!safePin(channel.relayPin)) {
      Serial.printf("[Config Error] relayPin %d is not an allowed safe GPIO pin!\n", channel.relayPin);
      return false;
    }
    if (!safePin(channel.sensorPin)) {
      Serial.printf("[Config Error] sensorPin %d is not an allowed safe GPIO pin!\n", channel.sensorPin);
      return false;
    }
    if (channel.relayPin == channel.sensorPin) {
      Serial.printf("[Config Error] relayPin and sensorPin cannot be the same pin (%d)!\n", channel.relayPin);
      return false;
    }
    if (pins[channel.relayPin] || pins[channel.sensorPin]) {
      Serial.println("[Config Error] Conflicting/duplicate GPIO pins across channels!");
      return false;
    }
    if (channel.relayChannel > 63 || relays[channel.relayChannel]) {
      Serial.printf("[Config Error] Invalid or duplicate relayChannel %u (must be 0..63)!\n", channel.relayChannel);
      return false;
    }
    pins[channel.relayPin] = pins[channel.sensorPin] = true;
    relays[channel.relayChannel] = true;
  }
  if (config::recoveryButtonPin != -1 && (!safePin(config::recoveryButtonPin) || pins[config::recoveryButtonPin])) {
    Serial.printf("[Config Error] recoveryButtonPin %d is invalid or conflicts with channel pins!\n", config::recoveryButtonPin);
    return false;
  }
  return true;
}

void stopPulse(size_t index) {
  portENTER_CRITICAL(&pulseMutex);
  const auto& channel = config::channels[index];
  gpio_set_level(static_cast<gpio_num_t>(channel.relayPin), channel.relayActiveHigh ? 0 : 1);
  pulses[index].active = false;
  portEXIT_CRITICAL(&pulseMutex);
}
void stopAllPulses() { for (size_t i = 0; i < pulses.size(); ++i) stopPulse(i); }
void pulseTick(void*) {
  portENTER_CRITICAL(&pulseMutex);
  for (size_t i = 0; i < pulses.size(); ++i) {
    if (pulses[i].active && esp_timer_get_time() >= pulses[i].deadline) {
      const auto& channel = config::channels[i];
      gpio_set_level(static_cast<gpio_num_t>(channel.relayPin), channel.relayActiveHigh ? 0 : 1);
      pulses[i].active = false;
    }
  }
  portEXIT_CRITICAL(&pulseMutex);
}
void startPulse(size_t index) {
  portENTER_CRITICAL(&pulseMutex);
  const auto& channel = config::channels[index];
  pulses[index] = {true, esp_timer_get_time() + static_cast<int64_t>(config::pulseMs) * 1000};
  gpio_set_level(static_cast<gpio_num_t>(channel.relayPin), channel.relayActiveHigh ? 1 : 0);
  portEXIT_CRITICAL(&pulseMutex);
}
bool closedNow(const config::Channel& channel) {
  // sensorClosedLow chỉ đảo cách hiểu đóng/mở, không thay đổi cách đấu GPIO-GND.
  return digitalRead(channel.sensorPin) == (channel.sensorClosedLow ? LOW : HIGH);
}

void doorTask(void*) {
  uint32_t buttonStarted = 0;
  bool buttonHandled = false;
  for (;;) {
    {
      Lock lock;
      const uint32_t tick = millis();
      for (size_t i = 0; i < sensors.size(); ++i) {
        const bool reading = closedNow(config::channels[i]);
        if (reading != sensors[i].reading) { sensors[i].reading = reading; sensors[i].changed = tick; }
        if (static_cast<uint32_t>(tick - sensors[i].changed) >= config::sensorDebounceMs) sensors[i].closed = reading;
      }
      if (fatal || !doors.healthy()) { fatal = true; stopAllPulses(); }
      else for (size_t i = 0; i < config::channels.size(); ++i) {
        if (!doors.healthy()) { fatal = true; stopAllPulses(); break; }
        const auto& channel = config::channels[i];
        auto* journal = doors.door(channel.relayChannel);
        if (journal->recoveryRequired() && sensors[i].closed && closedNow(channel)) {
          journal->clearRecoveryAfterInspection(true);
          Serial.printf("[Auto-Clear] Door %u recovery cleared (sensor closed).\n", channel.relayChannel);
        }
        const auto phase = journal->snapshot().phase;
        if (journal->recoveryRequired() || phase == flora::Phase::Idle) continue;
        const auto currentTime = static_cast<int64_t>(time(nullptr));
        if (!clockReady() || static_cast<uint32_t>(tick - cycleStarted[i]) > config::doorTimeoutMs) {
          stopPulse(i); journal->requireInspection(); Serial.println("Door requires local inspection.");
        } else if (phase == flora::Phase::Prepared) {
          if (sensors[i].closed && closedNow(channel) && journal->beginPulse(currentTime)) startPulse(i);
          else { stopPulse(i); journal->requireInspection(); }
        } else if (phase == flora::Phase::WaitingOpen && !sensors[i].closed) {
          // Chỉ cảm biến thật mới xác nhận cửa mở; relay nhả không đồng nghĩa cửa đã mở.
          if (!journal->opened(uuid(), currentTime)) { stopPulse(i); journal->requireInspection(); }
        } else if (phase == flora::Phase::WaitingClose && sensors[i].closed) {
          if (journal->closed(uuid(), currentTime)) stopPulse(i);
          else { stopPulse(i); journal->requireInspection(); }
        }
      }
      if (config::recoveryButtonPin >= 0 && digitalRead(config::recoveryButtonPin) == LOW) {
        if (!buttonStarted) buttonStarted = tick;
        if (!buttonHandled && static_cast<uint32_t>(tick - buttonStarted) >= 5000) {
          buttonHandled = true;
          bool allClosed = true;
          for (size_t i = 0; i < sensors.size(); ++i) allClosed &= sensors[i].closed && closedNow(config::channels[i]);
          if (!fatal && allClosed) {
            for (size_t i = 0; i < config::channels.size(); ++i) {
              if (doors.door(config::channels[i].relayChannel)->clearRecoveryAfterInspection(true)) stopPulse(i);
            }
            Serial.println("Local inspection acknowledged; previous command claims retained.");
          }
        }
      } else { buttonStarted = 0; buttonHandled = false; }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool textField(JsonVariantConst value, std::string& output) {
  if (!value.is<const char*>()) return false;
  const auto text = value.as<JsonString>();
  if (text.size() != strlen(text.c_str()) || text.size() > 64) return false;
  output.assign(text.c_str(), text.size()); return true;
}
void onCommand(char* topic, uint8_t* payload, unsigned length) {
  if (!subscribed || fatal || !clockReady() || commandTopic != topic || length > 1024 ||
      (mqtt.incomingFlags() & 1) || ((mqtt.incomingFlags() >> 1) & 3) != 1) return;
  JsonDocument json;
  if (deserializeJson(json, payload, length) || !json.is<JsonObject>() || json.size() != 9 ||
      !json["version"].is<int>() || !json["relay_channel"].is<int>() ||
      !json["issued_at"].is<int64_t>() || !json["expires_at"].is<int64_t>()) return;
  flora::Command command;
  command.version = json["version"].as<int>(); command.relay = json["relay_channel"].as<int>();
  command.issued = json["issued_at"].as<int64_t>(); command.expires = json["expires_at"].as<int64_t>();
  if (!textField(json["hardware_id"], command.hardware) || !textField(json["cmd_id"], command.id) ||
      !textField(json["slot_id"], command.slot) || !textField(json["purpose"], command.purpose) ||
      !textField(json["signature"], command.signature)) return;
  flora::Digest identity{};
  const auto currentTime = static_cast<int64_t>(time(nullptr));
  if (!flora::verify(command, config::hardwareId, deviceKey, currentTime, hmac, identity)) return;
  Lock lock;
  for (size_t i = 0; i < config::channels.size(); ++i) {
    if (config::channels[i].relayChannel != command.relay || !sensors[i].closed || !closedNow(config::channels[i])) continue;
    const auto result = doors.claim(command, identity, uuid(), currentTime);
    if (result == flora::ClaimResult::Accepted) cycleStarted[i] = millis();
    return;
  }
}

std::string eventPayload(const std::string& id, const std::string& command, const char* event, int64_t at) {
  std::string canonical; flora::Digest signature{};
  if (!flora::eventCanonical(config::hardwareId, id, command, event, at, canonical) || !hmac(deviceKey, canonical, signature)) return {};
  JsonDocument json;
  json["version"] = 1; json["hardware_id"] = config::hardwareId; json["event_id"] = id;
  json["cmd_id"] = command; json["event"] = event; json["occurred_at"] = at; json["signature"] = flora::hex(signature);
  std::string result; serializeJson(json, result); return result;
}

void publishNext() {
  if (inflightPacket) {
    if (acknowledgedPacket == inflightPacket) {
      if (!inflightEvent.empty()) { Lock lock; if (!doors.acknowledge(inflightRelay, inflightEvent)) fatal = true; }
      inflightPacket = acknowledgedPacket = 0; inflightEvent.clear(); inflightPayload.clear();
    } else if (static_cast<uint32_t>(millis() - sentAt) > 5000) mqtt.disconnect();
    return;
  }
  flora::Event pending{}; bool hasEvent = false;
  {
    Lock lock;
    hasEvent = doors.nextEvent(eventCursor, pending, inflightRelay);
  }
  if (hasEvent) {
    constexpr const char* names[] = {"ACK", "OPENED", "CLOSED"};
    inflightEvent = pending.id.data();
    inflightPayload = eventPayload(inflightEvent, pending.command.data(), names[static_cast<unsigned>(pending.kind)], pending.at);
  } else if (static_cast<uint32_t>(millis() - lastHeartbeat) >= 30000 || lastHeartbeat == 0) {
    inflightEvent.clear(); lastHeartbeat = millis();
    inflightPayload = eventPayload(uuid(), "00000000-0000-0000-0000-000000000000", "HEARTBEAT", time(nullptr));
  } else return;
  if (inflightPayload.empty()) { fatal = true; return; }
  inflightPacket = mqtt.reservePacketId(); sentAt = millis();
  if (!mqtt.publishQos1(eventTopic.c_str(), reinterpret_cast<const uint8_t*>(inflightPayload.data()), inflightPayload.size(), inflightPacket)) mqtt.disconnect();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=======================================================");
  Serial.println("           FLORABOT KIOSK FIRMWARE (ESP32)             ");
  Serial.println("=======================================================");
  if (!validConfiguration()) {
    Serial.println("\n[FATAL] Configuration check failed! Please review config.h or config.local.h.");
    return;
  }
  Serial.printf("[INIT] Hardware ID   : %s\n", config::hardwareId);
  Serial.printf("[INIT] Configured Doors: %u\n", static_cast<unsigned>(config::channels.size()));
  for (size_t i = 0; i < config::channels.size(); ++i) {
    const auto& channel = config::channels[i];
    Serial.printf("  Slot %u (Relay Ch %u) -> Relay GPIO: %d, Sensor GPIO: %d\n",
                  static_cast<unsigned>(i + 1), channel.relayChannel, channel.relayPin, channel.sensorPin);
    pinMode(channel.relayPin, OUTPUT); digitalWrite(channel.relayPin, channel.relayActiveHigh ? LOW : HIGH);
    // MC-38 tiếp điểm khô: hở mạch được kéo lên 3,3V, thông mạch kéo xuống GND.
    // Luôn dùng PULLUP kể cả khi loại cảm biến có trạng thái cửa đóng là HIGH.
    pinMode(channel.sensorPin, INPUT_PULLUP);
  }
  if (config::recoveryButtonPin >= 0) pinMode(config::recoveryButtonPin, INPUT_PULLUP);
  journalMutex = xSemaphoreCreateMutex();
  if (!journalMutex || !loadJournal()) { Serial.println("[FATAL] NVS unavailable or invalid; outputs remain off."); return; }
  esp_timer_create_args_t timer{}; timer.callback = pulseTick; timer.name = "relay-limit";
  if (esp_timer_create(&timer, &pulseTimer) != ESP_OK || esp_timer_start_periodic(pulseTimer, 10000) != ESP_OK ||
      xTaskCreate(doorTask, "door-sensors", 8192, nullptr, 3, nullptr) != pdPASS) {
    stopAllPulses(); Serial.println("[FATAL] Door task unavailable; outputs remain off."); return;
  }
  tls.setCACert(config::caCertificate); tls.setHandshakeTimeout(5);
  mqtt.setServer(config::mqttHost, config::mqttPort); mqtt.setSocketTimeout(2); mqtt.setKeepAlive(30);
  if (!mqtt.setBufferSize(2048)) { fatal = true; return; }
  mqtt.setCallback(onCommand);
  mqtt.setPubackCallback([](uint16_t id) { acknowledgedPacket = id; });
  mqtt.setSubackCallback([](uint16_t, uint8_t granted) {
    subscribed = granted == 1; subscriptionFailed = granted != 1;
    if (subscribed) Serial.println("[MQTT] Subscription to cmd topic confirmed (SUBACK).");
  });
  commandTopic = std::string("kiosk/") + config::hardwareId + "/cmd";
  eventTopic = std::string("kiosk/") + config::hardwareId + "/evt";
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  Serial.printf("[WiFi] Connecting to '%s'...\n", config::wifiSsid);
  WiFi.begin(config::wifiSsid, config::wifiPassword);
  sntp_set_time_sync_notification_cb(clockSynchronized);
  Serial.printf("[NTP] Initializing time sync via '%s'...\n", config::ntpServer);
  configTime(0, 0, config::ntpServer);
  configured = true;
  Serial.println("[FloraBot] Firmware initialized; waiting for WiFi, trusted TLS and synchronized time.");
}

void loop() {
  if (!configured || fatal) { delay(100); return; }

  static bool wasWiFiConnected = false;
  static bool wasClockReady = false;

  bool isWiFiConnected = (WiFi.status() == WL_CONNECTED);
  if (isWiFiConnected != wasWiFiConnected) {
    wasWiFiConnected = isWiFiConnected;
    if (isWiFiConnected) {
      Serial.printf("[WiFi] Connected! IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.println("[WiFi] Lost connection. Reconnecting...");
    }
  }

  bool isClockReady = clockReady();
  if (isClockReady != wasClockReady) {
    wasClockReady = isClockReady;
    if (isClockReady) {
      time_t now = time(nullptr);
      Serial.printf("[NTP] Clock synchronized! Current epoch: %lld\n", static_cast<long long>(now));
    }
  }

  if (!isWiFiConnected || !isClockReady) { delay(20); return; }

  if (!mqtt.connected()) {
    subscribed = false; subscriptionFailed = false; inflightPacket = acknowledgedPacket = 0;
    inflightEvent.clear(); inflightPayload.clear();
    if (static_cast<uint32_t>(millis() - lastConnect) < 5000) { delay(10); return; }
    lastConnect = millis();
    const std::string clientId = std::string("esp32-") + config::hardwareId;
    Serial.printf("[MQTT] Connecting to broker %s:%d (Client: %s)...\n", config::mqttHost, config::mqttPort, clientId.c_str());
    if (mqtt.connect(clientId.c_str(), config::hardwareId, config::mqttPassword, nullptr, 0, false, nullptr, false)) {
      Serial.printf("[MQTT] Connected! Subscribing to topic: %s\n", commandTopic.c_str());
      subscribeAt = millis();
      if (!mqtt.subscribe(commandTopic.c_str(), 1)) {
        Serial.println("[MQTT] Subscribe failed. Disconnecting.");
        mqtt.disconnect();
      }
    } else {
      Serial.printf("[MQTT] Connect failed (rc=%d). Will retry in 5s.\n", mqtt.state());
    }
  } else {
    mqtt.loop();
    if (subscriptionFailed || (!subscribed && static_cast<uint32_t>(millis() - subscribeAt) > 5000)) {
      Serial.println("[MQTT] Subscription timeout or rejected. Disconnecting.");
      mqtt.disconnect();
    } else if (subscribed) {
      publishNext();
    }
  }
  delay(5);
}
