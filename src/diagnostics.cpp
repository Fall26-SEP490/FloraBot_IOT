/**
 * ============================================================================
 * FLORABOT - ESP32 HARDWARE DIAGNOSTICS & SELF-TEST TOOL
 * Đồ án Capstone: FA26SE164
 * 
 * Mục đích:
 *   Công cụ chẩn đoán phần cứng chạy độc lập (Offline) để kiểm tra:
 *   1. Kích thử các kênh Rơ-le (Relay) và khóa điện từ Solenoid Lock (12V).
 *   2. Kiểm tra cảm biến từ gắn cửa (Magnetic Reed Switch) theo thời gian thực.
 *   3. Nút bấm vật lý 'BOOT' (GPIO 0) trên bo ESP32 để kích mở khóa trực tiếp bằng tay.
 *   4. Quét sóng mạng Wi-Fi 2.4GHz xung quanh và đo cường độ sóng RSSI.
 *   5. Giao tiếp lệnh điều khiển 1 chạm qua Serial Monitor (115200 baud).
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

// Pins tailored for ESP32 Expansion Shield
static constexpr int PIN_LED = 2;       // Built-in blue LED
static constexpr int PIN_BOOT_BTN = 0;  // Physical BOOT button on ESP32 board

struct TestChannel {
  const char* name;
  int relayPin;
  int sensorPin;
};

static constexpr TestChannel CHANNELS[] = {
  {"Slot 1 (Channel 0)", 26, 27},
  {"Slot 2 (Channel 1)", 14, 12},
  {"Slot 3 (Channel 2)", 25, 32},
  {"Slot 4 (Channel 3)", 33, 35}
};

static constexpr size_t NUM_CHANNELS = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

static bool lastSensorState[NUM_CHANNELS] = {false};
static unsigned long lastBlink = 0;
static unsigned long lastAutoPulse = 0;
static bool autoPulseEnabled = false;
static size_t autoPulseIndex = 0;
static bool ledState = false;
static bool lastBootBtnState = HIGH;

void printBanner() {
  Serial.println("\n=======================================================");
  Serial.println("       FLORABOT ESP32 HARDWARE DIAGNOSTICS TOOL        ");
  Serial.println("     (TEST HOAN TOAN OFFLINE - KHONG CAN WI-FI!)      ");
  Serial.println("=======================================================");
  
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  
  Serial.printf("Chip Model     : %s (Rev %d)\n", ESP.getChipModel(), chip_info.revision);
  Serial.printf("Cores          : %d\n", chip_info.cores);
  Serial.printf("CPU Frequency  : %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("MAC Address    : %s\n", WiFi.macAddress().c_str());
  Serial.printf("Flash Size     : %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("Free Heap      : %u bytes\n", ESP.getFreeHeap());
  Serial.println("-------------------------------------------------------");
  Serial.println("Cach test khoa Solenoid & Relay:");
  Serial.println("  * Cach 1: Bam nut 'BOOT' vat ly ngay tren bo ESP32");
  Serial.println("  * Cach 2: Gui phim '1', '2' qua Serial Monitor");
  Serial.println("  * Cach 3: Gui phim 'a' de bat tu dong mo khoa moi 3 giay");
  Serial.println("-------------------------------------------------------");
  Serial.println("Test cam bien cua (Reed Switch):");
  Serial.println("  * Dua nam cham lai gan cam bien -> Log bao DOOR CLOSED");
  Serial.println("  * Tach nam cham ra xa -> Log bao DOOR OPENED");
  Serial.println("=======================================================\n");
}

void scanWiFi() {
  Serial.println("\n[WiFi] Scanning nearby networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("[WiFi] No networks found.");
  } else {
    Serial.printf("[WiFi] Found %d networks:\n", n);
    for (int i = 0; i < n; ++i) {
      Serial.printf("  %2d: %-32s | RSSI: %3d dBm | Ch: %2d | %s\n",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    WiFi.channel(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured");
    }
  }
  Serial.println("[WiFi] Scan finished.\n");
}

void pulseRelay(size_t index) {
  if (index >= NUM_CHANNELS) return;
  const auto& ch = CHANNELS[index];
  Serial.printf("[RELAY] Actuating %s (GPIO %d) for 500ms... ", ch.name, ch.relayPin);
  digitalWrite(ch.relayPin, HIGH);
  digitalWrite(PIN_LED, HIGH);
  delay(500);
  digitalWrite(ch.relayPin, LOW);
  digitalWrite(PIN_LED, LOW);
  Serial.println("DONE (Relay closed/de-energized)");
}

void readAllSensors() {
  Serial.println("\n[SENSORS] Current states:");
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    // Pin configured with INPUT_PULLUP: LOW = Magnet touching (Door Closed), HIGH = Magnet separated (Door Opened)
    int val = digitalRead(CHANNELS[i].sensorPin);
    bool isClosed = (val == LOW);
    Serial.printf("  %s (GPIO %2d): %s (Raw Pin: %s)\n",
                  CHANNELS[i].name, CHANNELS[i].sensorPin,
                  isClosed ? "CLOSED [Magnet Attached]" : "OPENED [Door Open]",
                  isClosed ? "LOW" : "HIGH");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Initialize LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // Initialize Relays and Sensors
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    pinMode(CHANNELS[i].relayPin, OUTPUT);
    digitalWrite(CHANNELS[i].relayPin, LOW);

    // Sensor pins with internal pullup
    pinMode(CHANNELS[i].sensorPin, INPUT_PULLUP);
    lastSensorState[i] = (digitalRead(CHANNELS[i].sensorPin) == LOW);
  }

  // Initialize BOOT button
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  // Set WiFi to Station mode for scanning
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  printBanner();
  readAllSensors();

  Serial.println("[KHOI DONG] Kich test thu Relay 1 (Slot 1) mot lan...");
  delay(1000);
  pulseRelay(0);
}

void loop() {
  unsigned long now = millis();

  // Monitor physical BOOT button on ESP32 (GPIO 0)
  bool bootBtn = digitalRead(PIN_BOOT_BTN);
  if (bootBtn == LOW && lastBootBtnState == HIGH) {
    Serial.println("\n[NUT NHAN BOOT] Ban vua bam nut BOOT tren ESP32! Dang mo khoa Solenoid...");
    pulseRelay(0);
    delay(250); // Debounce
  }
  lastBootBtnState = bootBtn;

  // Heartbeat blink built-in LED
  if (now - lastBlink >= 1000) {
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(PIN_LED, ledState ? HIGH : LOW);
  }

  // Auto pulse cycle if enabled
  if (autoPulseEnabled && (now - lastAutoPulse >= 3000)) {
    lastAutoPulse = now;
    pulseRelay(autoPulseIndex);
    autoPulseIndex = (autoPulseIndex + 1) % NUM_CHANNELS;
  }

  // Monitor sensor pin state changes
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    int val = digitalRead(CHANNELS[i].sensorPin);
    bool isClosed = (val == LOW);
    if (isClosed != lastSensorState[i]) {
      lastSensorState[i] = isClosed;
      Serial.printf("[SENSOR EVENT] %s (GPIO %d) changed -> %s\n",
                    CHANNELS[i].name, CHANNELS[i].sensorPin,
                    isClosed ? "DOOR CLOSED [Magnet Detected]" : "DOOR OPENED [Lock Released]");
    }
  }

  // Check incoming Serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    // Ignore newlines
    if (cmd == '\r' || cmd == '\n' || cmd == ' ') return;

    Serial.printf("\n[CMD] Received command: '%c'\n", cmd);
    switch (cmd) {
      case '1': pulseRelay(0); break;
      case '2': pulseRelay(1); break;
      case '3': pulseRelay(2); break;
      case '4': pulseRelay(3); break;
      case 'a':
      case 'A':
        autoPulseEnabled = !autoPulseEnabled;
        Serial.printf("[AUTO] Auto-pulse cycle is now %s\n", autoPulseEnabled ? "ENABLED (every 3s)" : "DISABLED");
        break;
      case 'w':
      case 'W':
        scanWiFi();
        break;
      case 's':
      case 'S':
        readAllSensors();
        break;
      case 'b':
      case 'B':
        ledState = !ledState;
        digitalWrite(PIN_LED, ledState ? HIGH : LOW);
        Serial.printf("[LED] Built-in LED set to %s\n", ledState ? "ON" : "OFF");
        break;
      case 'h':
      case 'H':
      case '?':
        printBanner();
        break;
      default:
        Serial.println("[CMD] Unknown command. Press 'h' or '?' for menu.");
        break;
    }
  }

  delay(20);
}
