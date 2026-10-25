# FloraBot_IOT - Firmware điều khiển Kiosk Tủ Bán Hoa Tự Động (ESP32)

Dự án IoT điều khiển phần cứng cho hệ thống **FloraBot - Automated Flower Kiosk** (Đồ án Capstone FA26SE164).

Module này chạy trên vi điều khiển **ESP32**, chịu trách nhiệm:
1. Kết nối Wi-Fi và giao tiếp thời gian thực với **FloraBot Backend** qua giao thức **MQTT TLS**.
2. Điều khiển đóng/ngắt các rơ-le (Relay) kích xung mở khóa điện từ (Solenoid Lock) cho từng ô hộc tủ hoa.
3. Giám sát trạng thái cảm biến từ gắn cửa (Magnetic Reed Switch) để phát hiện chính xác thời điểm cửa mở và cửa đóng.
4. Gửi tín hiệu nhịp tim sống (Heartbeat) định kỳ 30 giây để cập nhật trạng thái `ONLINE` của Kiosk trên hệ thống quản trị.
5. Xác thực an toàn bảo mật cấp công nghiệp: chữ ký điện tử **HMAC-SHA256** chống giả mạo lệnh, lưu vết trạng thái chống mất điện vào bộ nhớ flash **NVS Journal (ADR 0021 & ADR 0022)**.

---

## 1. Thông tin phần cứng đã nhận diện

Hệ thống đã nhận diện thành công bo mạch ESP32 của bạn kết nối qua cổng USB:
- **Vi điều khiển**: ESP32-D0WD-V3 (Dual Core 240MHz, 4MB Flash)
- **Chip nạp / Serial**: Silicon Labs CP2102
- **Cổng COM kết nối**: `COM6` (Tốc độ baud: `115200`)
- **Đế mở rộng (Expansion Shield)**: Bo đế ra chân 3 pin (Signal - VCC - GND) kèm jack cắm nguồn DC 6.5V - 16V và cổng cấp nguồn USB.

---

## 2. Sơ đồ chân kết nối (Pinout & Wiring)

Bo mạch mở rộng của bạn có từng cụm 3 chân ký hiệu **G** (Ground) - **V** (VCC 5V/3.3V) - **S** (Signal/GPIO):

| Thiết bị ngoại vi | Chân trên Shield (ESP32) | Chức năng trong FloraBot | Ghi chú |
| :--- | :--- | :--- | :--- |
| **Relay Ô 1** | **GPIO 26 (D26)** | Kích mở khóa hộc tủ số 1 (Slot 0) | Chân Signal vào IN của module Relay |
| **Cảm biến từ Cửa 1** | **GPIO 27 (D27)** | Báo đóng/mở cửa hộc tủ số 1 | Tiếp điểm nam châm nối GPIO 27 & GND |
| **Relay Ô 2** | **GPIO 14 (D14)** | Kích mở khóa hộc tủ số 2 (Slot 1) | Chân Signal vào IN của module Relay |
| **Cảm biến từ Cửa 2** | **GPIO 12 (D12)** | Báo đóng/mở cửa hộc tủ số 2 | Tiếp điểm nam châm nối GPIO 12 & GND |
| **LED trạng thái** | **GPIO 2 (D2)** | Nhấp nháy nhịp tim (Heartbeat) | LED tích hợp sẵn trên bo ESP32 |

> **Lưu ý đấu nối cảm biến từ (Reed Switch):**
> Firmware đã bật điện trở kéo lên nội (**INPUT_PULLUP**).
> - Khi cửa **ĐÓNG**: Nam châm áp sát công tắc từ -> Khép mạch xuống mass GND (`LOW`).
> - Khi cửa **MỞ**: Nam châm cách xa -> Hở mạch (`HIGH`).

---

## 3. Thử nghiệm nhanh phần cứng (Diagnostics Tool)

Trước khi cấu hình backend, bạn có thể nạp ngay bản **Diagnostics** để test relay và cảm biến:

1. Chạy file script PowerShell:
   ```powershell
   .\flash-diagnostics.ps1
   ```
   *(Script sẽ tự động biên dịch, nạp vào ESP32 trên cổng COM6 và mở Serial Monitor).*

2. Trong cửa sổ Serial Monitor (115200 baud):
   - Nhấn phím `1`: Kích nhả Relay Ô 1 (GPIO 26) trong 500ms (bạn sẽ nghe tiếng Relay tạch tạch).
   - Nhấn phím `2`: Kích nhả Relay Ô 2 (GPIO 14) trong 500ms.
   - Nhấn phím `a`: Chế độ tự động test đảo relay mỗi 3 giây.
   - Nhấn phím `w`: Quét và hiển thị toàn bộ sóng Wi-Fi 2.4GHz xung quanh.
   - Nhấn phím `s`: Đọc trạng thái tất cả cảm biến cửa.
   - Đưa nam châm lại gần cảm biến -> màn hình log sẽ lập tức báo `DOOR CLOSED` hoặc `DOOR OPENED`.

---

## 4. Cấu hình kết nối FloraBot Backend (Production)

Mở file [`include/config.h`](include/config.h) và điền các thông số:

```cpp
// 1. Tên và mật khẩu Wi-Fi
inline constexpr char wifiSsid[] = "Tên_WiFi_Của_Bạn";
inline constexpr char wifiPassword[] = "Mật_Khẩu_WiFi";

// 2. Địa chỉ IP máy tính đang chạy Backend / Mosquitto MQTT
// Lưu ý: Dùng địa chỉ IP nội bộ mạng LAN của máy tính (ví dụ 192.168.1.15), không dùng localhost!
inline constexpr char mqttHost[] = "192.168.1.15";
inline constexpr uint16_t mqttPort = 8883;

// 3. Định danh Hardware ID (Mặc định trong DB và Mosquitto là ESP32-A1B2C3)
inline constexpr char hardwareId[] = "ESP32-A1B2C3";
```

Sau khi cấu hình xong, nạp firmware chính thức:
```powershell
.\flash.ps1
```

Để theo dõi log hoạt động của ESP32:
```powershell
.\monitor.ps1
```

---

## 5. Cấu trúc thư mục dự án

```text
FloraBot_IOT/
├── include/
│   ├── config.h               # File cấu hình Wi-Fi, MQTT, chân GPIO Relay & Cảm biến
│   └── config.example.h       # Cấu hình mẫu ban đầu
├── src/
│   ├── main.cpp               # Firmware chính thức kết nối FloraBot Backend qua MQTT TLS
│   └── diagnostics.cpp        # Firmware kiểm tra phần cứng (Relay, Cảm biến, Quét Wi-Fi)
├── lib/
│   ├── FloraProtocol/         # Thư viện xác thực lệnh HMAC-SHA256, NVS Journal chống mất điện
│   └── PubSubClient/          # Thư viện client MQTT hỗ trợ QoS 1
├── platformio.ini             # Cấu hình PlatformIO cho ESP32 Dev Module, COM6, 115200 baud
├── partitions.csv             # Bảng phân vùng bộ nhớ Flash (OTA, App, NVS Journal)
├── flash.ps1                  # Script nạp firmware chính thức (1 click)
├── flash-diagnostics.ps1      # Script nạp firmware chẩn đoán test phần cứng (1 click)
├── monitor.ps1                # Script mở Serial Monitor
├── test-chip.ps1              # Script kiểm tra thông tin chip ESP32
└── README.md                  # Hướng dẫn chi tiết
```

---

## 6. Giao thức truyền thông (MQTT Wire Protocol)

Theo thiết kế hệ thống FloraBot:
- **Lệnh mở cửa (Backend -> ESP32)**:
  - Topic: `kiosk/{hardware_id}/cmd` (QoS 1)
  - Dữ liệu JSON có chữ ký HMAC-SHA256, chỉ có hiệu lực trong cửa sổ thời gian (60s).
- **Sự kiện phản hồi (ESP32 -> Backend)**:
  - Topic: `kiosk/{hardware_id}/evt` (QoS 1)
  - Các sự kiện: `HEARTBEAT` (mỗi 30s), `ACK` (đã nhận lệnh), `OPENED` (khách đã mở cửa ô), `CLOSED` (khách đã đóng cửa ô -> kết thúc đơn hàng).
