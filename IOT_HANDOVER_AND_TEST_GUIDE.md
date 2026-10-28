# HƯỚNG DẪN BÀN GIAO & TEST PHẦN CỨNG IOT FLORABOT (DÀNH CHO DEV IOT / CODEX AGENT)
> **Dành riêng cho kỹ sư IoT hoàn thiện mạch phần cứng ESP32, rơ-le và cảm biến**  
> **Không cần cài đặt, không cần chạy và không cần đụng tới source code Backend/Frontend!**  
> Cập nhật: 28/10/2026

---

## 1. DÀNH CHO BẠN (HOẶC CODEX AGENT TIẾP QUẢN)
Nếu bạn chỉ phụ trách phần cứng IoT và chưa đụng tới Backend (BE) hay Frontend (FE), **đừng lo lắng!**
- Toàn bộ backend, database và broker MQTT đã được deploy sẵn và đang chạy 24/7 trên Cloud VPS (`api.florabot.site`).
- Bạn chỉ cần làm việc **100% trong thư mục `FloraBot_IOT` này**.
- Tất cả công cụ test mạch, script kích rơ-le độc lập và chứng chỉ bảo mật đã được đặt sẵn trong thư mục [`tools/`](file:///d:/academy/KY9/SEP490/FLorabot/FloraBot_IOT/tools).

---

## 2. SƠ ĐỒ ĐẤU DÂY PHẦN CỨNG (PINOUT & WIRING)

### A. Bảng chân kết nối 4 hộc tủ:
| Ô Kiosk | Kênh Relay | Chân Relay (ESP32) | Chân Cảm biến MC-38 (ESP32) | Ghi chú |
| :---: | :---: | :---: | :---: | :--- |
| **Ô 1** | Channel `0` | **GPIO 26** | **GPIO 27** | |
| **Ô 2** (Đang test ô A02) | Channel `1` | **GPIO 14** | **GPIO 13** | **Chân chính dùng để test** |
| **Ô 3** | Channel `2` | **GPIO 25** | **GPIO 32** | |
| **Ô 4** | Channel `3` | **GPIO 33** | **GPIO 18** | |

### B. Nguyên lý đấu nối:
1. **Module Relay 4 kênh (Kích mức HIGH - Active HIGH):**
   - VCC nối nguồn 5V, GND nối GND chung với ESP32.
   - IN1 -> GPIO 26, IN2 -> GPIO 14, IN3 -> GPIO 25, IN4 -> GPIO 33.
   - Tiếp điểm COM và NO của rơ-le nối tiếp với nguồn 12V cấp cho khóa nam châm/chốt điện từ.
   - ⚠️ **TUYỆT ĐỐI KHÔNG ĐƯA NGUỒN 12V VÀO CHÂN GPIO CỦA ESP32.**
2. **Cảm biến cửa từ MC-38 (Tiếp điểm khô 2 dây):**
   - Dây 1 nối vào chân GPIO cảm biến (ví dụ **GPIO 13** cho ô 2).
   - Dây 2 nối vào chân **GND** của ESP32.
   - Firmware sử dụng `pinMode(pin, INPUT_PULLUP)`:
     * **Cửa ĐÓNG (nam châm áp sát công tắc):** Tiếp điểm thông mạch -> Chân GPIO nối đất -> Đọc mức **LOW**.
     * **Cửa MỞ (nam châm tách xa):** Tiếp điểm hở mạch -> Trở kéo lên nội bộ 3.3V -> Đọc mức **HIGH**.
   - ⚠️ **LƯU Ý CỰC KỲ QUAN TRỌNG:** Firmware có cơ chế an toàn: Nếu cảm biến báo cửa đang MỞ (hoặc dây D13 bị lỏng/chưa chạm GND), ESP32 sẽ **TỪ CHỐI LỆNH MỞ KHÓA**. Vì vậy, **khi test mở khóa, luôn đảm bảo dây cảm biến đang được cắm chắc chắn vào chân GND**.

---

## 3. CẤU HÌNH FILE `include/config.local.h`

Vì file cấu hình chứa mật khẩu Wi-Fi và key bí mật nên mặc định bị Git bỏ qua (`.gitignore`).  
Nếu máy của bạn chưa có file này, hãy tạo file: `FloraBot_IOT/include/config.local.h` với nội dung sau:

```cpp
#pragma once
#include <array>
#include <cstdint>
#include "hardware.h"

namespace config {

using Channel = hardware::Channel;

inline constexpr char hardwareId[] = "ESP32-A1B2C3";

// CẤU HÌNH WI-FI (Điền tên và mật khẩu Wi-Fi tại nơi bạn đang làm việc - Băng tần 2.4GHz)
inline constexpr char wifiSsid[] = "FPT_Music";
inline constexpr char wifiPassword[] = "thanhtu7";

// Cấu hình MQTT Broker kết nối lên Cloud VPS Production
inline constexpr char mqttHost[] = "api.florabot.site";
inline constexpr uint16_t mqttPort = 8883;
inline constexpr char mqttPassword[] = "E106D8416D942A5056EA2694BEF07DB0179EE0B9464BD895B5218C46239EE25F";

// Khóa bí mật HMAC-SHA256 (32 bytes = 64 hex characters) để xác thực chữ ký lệnh mở cửa
inline constexpr char hmacKeyHex[] = "46EDF9F65A5CAE35E70C8C7D13B63AFAAE2CEA98FB2EDBC1406A4E837620B101";

// Chứng chỉ CA để xác thực TLS với Mosquitto Broker trên VPS
inline constexpr char caCertificate[] = R"(-----BEGIN CERTIFICATE-----
MIIEIzCCAougAwIBAgIUcoFa3BitnC3FmzpTHtS8yjXWcIIwDQYJKoZIhvcNAQEL
BQAwITEfMB0GA1UEAwwWRmxvcmFCb3QgTG9jYWwgTVFUVCBDQTAeFw0yNjEwMTIw
OTU4MTlaFw0yNzEwMTIwOTU4MTlaMCExHzAdBgNVBAMMFkZsb3JhQm90IExvY2Fs
IE1RVFQgQ0EwggGiMA0GCSqGSIb3DQEBAQUAA4IBjwAwggGKAoIBgQCgV68bOzIs
aNyA4WC3gDCFmSfjUdixDDcRGouIz9n+vUtDdRVL8a1CIa0Guz8eXeDDbsmhvfXi
oBNHZCLZiLhNan2Je7KL0rNMaWb+HBDKO0zGDN1kYTDJDIQ/eWeuVERuvBOEdR0k
Wx9eHVWUC1Gh2kKWeT0uNeapD5nA+nnLe4dIozAcgC1VUQgD7jOyQ5ISdwmuOmX9
ujr5So3MEowlH/Kr0yUhmBlDArDoIWoam2nAYu0EabLhLY4gdV2TcynUv400fOhY
BLTwT44w4YaKK4jmoOzl8XqhI3o4NQseH/wlNTOrT2ahqjm3RzNdv8w4GGG+dipn
TfYFt81UYK/TIBNCmHFX/FD+/yUtq27BslSrFMVZFPgdkdlkcmLPlDLwzg8/k51x
l5GxOO7NAnAIiXH4SS02BBwXDQS9PJky360QI0Yw5C34u7GpmCqICYnyFBXPuTRO
5RfQd7JhfTgcp4n59oVJmtir6Uh/N3fodfajCIF/N53jP6umNSTObUUCAwEAAaNT
MFEwHQYDVR0OBBYEFOPLWTFpogP+pqKdZFAB/3LI/czYMB8GA1UdIwQYMBaAFOPL
WTFpogP+pqKdZFAB/3LI/czYMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEL
BQADggGBACbgEntz1agGaLQ7wHW9ULwxdd7vhRJ/fL/R81/B+5m242mTPx8PpK4S
UpaEQESkkfLXLxGDP398MXW50XM1CPAo0yDawb4TpfeiICBajJpUqS/rUm3DWRYm
VgMs8x51YjAgQterruh90Mwrx2nEbL3PSHyiFslobabD5mbABM6llNo3x83RXOYU
wuhPVINBGYWMHvEyab/c6jW4x+Gw4qY+Zvod/fxjWnwa4ugss51gzqYDbtQFueyd
tDXUwE6LcGMyxKSBQb4HMU1Iek3kGZZaFL7SGzOjhWq0wO/JGTohKHdZADElMce7
jHvic8oejGIWQZuUxrcAYp6KYLbv3/IyLj6YV+msdZ90Fm6zNHkwRj15rTyQljM2
MoOq0r6WzUXQGH2qLZZM9BWZTeeiKrsaPBs1EPXY8gIY8nlXSUWhE1F2HiHKJDrW
GKIkSoUFIVGRGqkH5Ip/MMDj3ZWf4zyy4ZRtTcjLnp1j9pKWhjVwDw1Njsy7/NkX
gN6Vcd4HvQ==
-----END CERTIFICATE-----)";

inline constexpr char ntpServer[] = "pool.ntp.org";
inline constexpr auto channels = hardware::channels;
inline constexpr int recoveryButtonPin = -1;
inline constexpr uint32_t pulseMs = 500;
inline constexpr uint32_t sensorDebounceMs = 40;
inline constexpr uint32_t doorTimeoutMs = 60000;

}  // namespace config
```

---

## 4. BẢN VÁ FIRMWARE QUAN TRỌNG (ĐÃ FIX SẴN TRONG CODE)

- **Lỗi cũ:** Khi rơ-le kích mở mà không có thao tác rút dây cảm biến ra (mở cửa) rồi cắm lại GND (đóng cửa) trong 60 giây, ESP32 sẽ tự động chuyển ô đó sang chế độ kẹt `requireInspection` và lưu vào Flash NVS. Vì không có nút bấm cứu hộ, cửa bị kẹt vĩnh viễn dù có rút nguồn.
- **Bản vá mới:** Đã bổ sung tính năng **Auto-Clear Recovery** vào `main.cpp`. Chỉ cần chân cảm biến được nối lại GND (cửa đóng), ESP32 sẽ lập tức tự động xóa lỗi kẹt và sẵn sàng nhận lệnh mới.

---

## 5. CÁCH NẠP FIRMWARE (FLASHING)

1. Cắm cáp USB nối ESP32 với máy tính.
2. Cài thư viện Python cần thiết (chỉ cần chạy 1 lần):
   ```powershell
   pip install -r tools/requirements.txt
   ```
3. Mở PowerShell tại thư mục `FloraBot_IOT` và chạy:
   ```powershell
   pio run -t upload -e esp32dev
   ```
   *(Hoặc chạy script: `./flash.ps1`)*
4. Mở Serial Monitor để theo dõi:
   ```powershell
   pio device monitor -b 115200
   ```
   *(Hoặc chạy script: `./monitor.ps1`)*
5. Khi thấy ESP32 in ra:
   - `[WiFi] Connected!`
   - `[NTP] Time synchronized`
   - `[MQTT] Subscription to cmd topic confirmed (SUBACK).`
   -> Mạch đã sẵn sàng 100%!

---

## 6. QUY TRÌNH TEST PHẦN CỨNG (KHÔNG CẦN CODE BE/FE)

### 🚀 KỊCH BẢN CHÍNH: TEST NHẢY RƠ-LE & CẢM BIẾN TRỰC TIẾP (KHÔNG CẦN TIỀN)

Đây là kịch bản chuẩn nhất để nghiệm thu phần cứng, rơ-le và cảm biến đóng mở:

1. Đảm bảo chân **GPIO 13 đang nối với chân GND** (mô phỏng cửa đang đóng bình thường).
2. Chạy script test độc lập:
   ```powershell
   python tools/test_unlock.py
   ```
3. **Diễn biến quan sát được:**
   - Script gửi lệnh mở khóa có chữ ký HMAC lên Cloud Broker.
   - Rơ-le 2 (nối chân GPIO 14) **nhảy "TÁCH" trong 500ms** rồi nhả.
   - Script Python nhận được phản hồi `ACK` từ ESP32:  
     `=> ESP32 ĐÃ NHẬN LỆNH VÀ KÍCH RƠ-LE THÀNH CÔNG!`
4. **Kiểm tra tiếp cảm biến từ MC-38:**
   - Rút dây GPIO 13 ra khỏi GND (mô phỏng cửa bị đẩy mở) -> Script in:  
     `=> ESP32 XÁC NHẬN CỬA ĐÃ MỞ (OPENED)!`
   - Cắm lại dây GPIO 13 vào GND (mô phỏng cửa được đóng lại) -> Script in:  
     `=> ESP32 XÁC NHẬN CỬA ĐÃ ĐÓNG HOÀN TẤT CHU TRÌNH (CLOSED)!`

-> Nếu chạy trọn vẹn được 4 bước trên, **mạch phần cứng và Firmware đã hoàn hảo 100%!**

---

### 💳 KỊCH BẢN PHỤ: TEST QUÉT TIỀN THẬT (VIETQR PAYOS -> MỞ CỬA)
Nếu muốn test trải nghiệm thực tế như một khách hàng:

1. Chạy script tạo mã QR thanh toán 2.000 VNĐ:
   ```powershell
   python tools/create_checkout_2k.py
   ```
   *(Script sẽ in ra đường link thanh toán PayOS VietQR kèm mã Checkout ID).*
2. Mở một terminal khác chạy lệnh giám sát:
   ```powershell
   python tools/monitor_paid_flow.py <CHECKOUT_ID>
   ```
3. Dùng app ngân hàng quét mã QR 2.000 VNĐ.
4. Tiền trừ -> Backend Cloud tự động phát lệnh -> Khóa nhảy "TÁCH" -> Thao tác mở/đóng cửa cảm biến -> Đơn hàng báo `COMPLETED`!

*(Lưu ý: Nếu ô A02 đang bị FAULT trên Database VPS, hãy nhờ bạn quản lý VPS chạy 1 lệnh SQL reset slot A02 về `STOCKED` trước khi tạo checkout)*.
