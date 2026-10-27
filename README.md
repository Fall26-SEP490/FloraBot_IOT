# FloraBot_IOT - Firmware điều khiển Kiosk Tủ Bán Hoa Tự Động (ESP32)

Dự án IoT điều khiển phần cứng cho hệ thống **FloraBot - Automated Flower Kiosk** (Đồ án Capstone FA26SE164).

Module này chạy trên vi điều khiển **ESP32**, chịu trách nhiệm:
1. Kết nối Wi-Fi và giao tiếp thời gian thực với **FloraBot Backend** qua giao thức **MQTT TLS** (Port 8883, QoS 1).
2. Điều khiển đóng/ngắt các rơ-le (Relay) kích xung mở khóa điện từ (Solenoid Lock 12V) cho từng ô hộc tủ hoa.
3. Giám sát trạng thái cảm biến từ gắn cửa (Magnetic Reed Switch) để phát hiện chính xác thời điểm cửa mở và cửa đóng.
4. Gửi tín hiệu nhịp tim sống (Heartbeat) định kỳ 30 giây để cập nhật trạng thái `ONLINE` của Kiosk trên hệ thống quản trị.
5. Xác thực an toàn bảo mật cấp công nghiệp: chữ ký điện tử **HMAC-SHA256** chống giả mạo lệnh, lưu vết trạng thái chống mất điện vào bộ nhớ flash **NVS Journal (ADR 0021 & ADR 0022)**.

---

## 1. Danh sách thiết bị phần cứng sử dụng

| STT | Tên thiết bị / Linh kiện | Thông số kỹ thuật | Vai trò trong hệ thống FloraBot |
| :---: | :--- | :--- | :--- |
| **1** | **ESP32 Dev Module** | ESP32-D0WD-V3, 4MB Flash, CP2102 | Vi điều khiển trung tâm xử lý logic, Wi-Fi, MQTT TLS, HMAC |
| **2** | **Bo đế mở rộng (Expansion Shield)** | Đế ra chân 3 pin (S - V - G), DC 6.5–16V | Giúp cắm dây tín hiệu và cấp nguồn gọn gàng, chống lỏng dây |
| **3** | **Module Relay 5V 4 kênh** | 5VDC Tongling/Songle, cách ly Quang (Optocoupler) | Đóng cắt mạch điện 12V điều khiển các cuộn hút khóa tủ hoa |
| **4** | **Mạch Nguồn Hạ Áp DC-DC LM2596S 3A** | Input 4–35V, Output 1.25–30V, dòng tải max 3A | Hạ áp từ **12V xuống 5.0V** ổn định cấp nguồn cho ESP32 và Relay |
| **5** | **Khóa Điện Từ Solenoid Lock 12V** | 12V DC, 0.6A (Công suất ~7.2W) | Chốt khóa điện từ giữ cửa hộc tủ, giật mở khi có xung điện |
| **6** | **Diode Chỉnh Lưu Dòng 1N4007** | 1000V / 1A, dạng chân cắm DO-41 | **Dập xung ngược (Flyback Diode)** bảo vệ tiếp điểm relay và ESP32 |
| **7** | **Cảm biến từ gắn cửa (Magnetic Reed Switch)** | Tiếp điểm thường mở, kích hoạt bằng nam châm | Nhận biết trạng thái cửa ô tủ đang ĐÓNG hay MỞ |
| **8** | **Nguồn Adapter 12V DC** | 12V DC, dòng tải tối thiểu 2A – 3A | Cung cấp nguồn tổng cho toàn bộ hệ thống Kiosk |

---

## 2. Sơ đồ nguyên lý đấu nối chi tiết (Wiring Diagram)

```text
       +-------------------------------------------------------------+
       |               NGUỒN ADAPTER 12V (≥ 2A - 3A)                 |
       +-------------------------------------------------------------+
            | (+) 12V                                      | (-) GND
            |                                              |
   +--------+------------------+                           |
   |                           |                           |
   v                           v                           v
[IN+] Mạch Hạ Áp LM2596S    [COM] Cổng Relay 1         [IN-] Mạch Hạ Áp LM2596S
   |                                                       |
   | (Hạ áp về 5.0V)                                       |
   v                                                       v
[OUT+] (5.0V)                                           [OUT-] (GND)
   |                                                       |
   +-----> [DC+] Module Relay 5V 4 kênh                    +---> [DC-] Module Relay 5V
   |                                                       |
   +-----> [VIN] ESP32 Shield (Nuôi ESP32)                 +---> [GND] ESP32 (Chung Mass!)
                                                           |
   [NO] Cổng Relay 1                                       |
        |                                                  |
        | (+) 12V                                          | (-) GND
        +-------------------------+       +----------------+
                                  |       |
                                  v       v
                         +--------------------------+
                         |  Khóa SOLENOID LOCK 12V  |
                         |  Dây ĐỎ (+)  Dây ĐEN (-) |
                         +--------------------------+
                                  ^       ^
                                  |       |
                         +--------+-------+--------+
                         |   Diode 1N4007          |
                         | [Vạch Bạc]   [Đầu Đen]  |
                         | (Cathode)     (Anode)   |
                         +-------------------------+
```

---

## 3. Hướng dẫn lắp đặt & Đấu dây an toàn

### 3.1. Đấu Diode 1N4007 bảo vệ Khóa Solenoid Lock *(Cực kỳ quan trọng)*
Cuộn hút của khóa điện từ 12V là tải cảm ứng (inductive load). Khi ngắt điện đột ngột, từ trường sụp đổ sinh ra dòng điện cảm ứng ngược cực tính (Back-EMF) lên tới hàng trăm Volt, dễ gây tia lửa điện làm cháy tiếp điểm relay và đánh sập nguồn gây reset ESP32.
- **Cách đấu Diode 1N4007:** Đấu **song song ngược cực** ngay tại 2 đầu dây của khóa:
  - **Đầu có VẠCH BẠC (Cathode):** Nối vào **Dây ĐỎ (+)** của khóa Solenoid Lock.
  - **Đầu ĐEN không có vạch (Anode):** Nối vào **Dây ĐEN (-)** của khóa Solenoid Lock.

### 3.2. Căn chỉnh Mạch Nguồn Hạ Áp LM2596S 3A
1. Cấp nguồn 12V từ Adapter vào 2 cọc bên phải: Dây dương vào `IN+`, dây âm vào `IN-`.
2. Dùng đồng hồ VOM (hoặc màn hình LED trên mạch nếu có đồng hồ) đo điện áp ở ngõ ra `OUT+` và `OUT-`.
3. Dùng tua vít dẹt xoay biến trở vi chỉnh (con ốc đồng nhỏ trên khối vuông màu xanh dương) ngược chiều kim đồng hồ cho đến khi điện áp ngõ ra đạt chuẩn **`5.0V DC`**.
4. Ngắt điện trước khi nối sang Relay và ESP32.

### 3.3. Đấu nối Module Relay 5V 4 kênh
1. **Nguồn nuôi cuộn hút:**
   - Cọc **`DC+`**: Nối vào ngõ **`OUT+` (5.0V)** của mạch LM2596S.
   - Cọc **`DC-`**: Nối vào ngõ **`OUT-` (GND)** của mạch LM2596S VÀ nối chung sang chân **`GND`** của bo đế ESP32.
2. **Tín hiệu điều khiển:**
   - Cọc **`IN1`** (Relay 1): Nối vào chân **GPIO 26 (`D26`)** của ESP32 (cây kim `Signal` ngoài cùng của hàng D26).
   - Cọc **`IN2`** (Relay 2): Nối vào chân **GPIO 14 (`D14`)** của ESP32.
3. **Cài đặt Jumper kích (`S1`, `S2`):**
   - Hàng jumper vàng có 3 chân cho mỗi kênh: `H` (High level) và `L` (Low level).
   - Đặt jumper ở vị trí tương ứng với cấu hình trong firmware:
     - Nếu code chạy `relayActiveHigh = true`: Cắm jumper vào vị trí **`H`**.
     - Nếu code chạy kích mức thấp: Cắm jumper vào vị trí **`L`**.
4. **Tiếp điểm đóng cắt 12V (Khối cọc xanh trên Relay 1):**
   - Cọc **`COM`** (ở giữa): Nối nguồn **`+12V`** (từ `IN+` của LM2596S).
   - Cọc **`NO`** (Thường mở): Nối vào **Dây ĐỎ (+)** của khóa Solenoid Lock.
   - Cọc **`NC`**: Để trống.
   - Dây **ĐEN (-)** của khóa: Nối thẳng về **GND 12V** (cọc `IN-` của LM2596S).

---

## 4. Sơ đồ chân ESP32 (Pinout Mapping)

Hàng chân cắm trên bo đế Expansion Shield (chân Signal `S` ngoài cùng):

| Ngoại vi | Chân trên ESP32 | Chức năng trong Kiosk FloraBot | Ghi chú đấu nối |
| :--- | :--- | :--- | :--- |
| **Relay Ô 1** | **GPIO 26 (D26)** | Mở khóa điện từ Ô số 1 (Slot 0) | Cắm kim ngoài cùng D26 sang `IN1` Relay |
| **Cảm biến từ Cửa 1** | **GPIO 27 (D27)** | Báo đóng/mở cửa Ô số 1 | Tiếp điểm nam châm nối giữa GPIO 27 & GND |
| **Relay Ô 2** | **GPIO 14 (D14)** | Mở khóa điện từ Ô số 2 (Slot 1) | Cắm kim ngoài cùng D14 sang `IN2` Relay |
| **Cảm biến từ Cửa 2** | **GPIO 12 (D12)** | Báo đóng/mở cửa Ô số 2 | Tiếp điểm nam châm nối giữa GPIO 12 & GND |
| **Nút bấm BOOT** | **GPIO 0** | Nút bấm vật lý kích mở khóa tại chỗ | Nút nhấn có sẵn trên bo mạch ESP32 |
| **LED trạng thái** | **GPIO 2 (D2)** | Nhấp nháy nhịp tim (Heartbeat) | Đèn LED xanh tích hợp sẵn trên ESP32 |

> **Cảm biến từ cửa (Reed Switch):** Firmware sử dụng chế độ kéo lên nội (`INPUT_PULLUP`):
> - **Cửa ĐÓNG** (nam châm áp sát công tắc): Khép mạch xuống `GND` $\rightarrow$ Đọc mức `LOW`.
> - **Cửa MỞ** (nam châm tách xa): Hở mạch $\rightarrow$ Đọc mức `HIGH`.

---

## 5. Thử nghiệm nhanh phần cứng (Diagnostics Tool)

Trước khi chạy hệ thống với Backend, nạp bản **Diagnostics** để test relay, khóa điện và cảm biến hoàn toàn **Offline (không cần Wi-Fi)**:

1. Chạy file script PowerShell:
   ```powershell
   cd "d:\caption florabot\FloraBot_IOT"
   .\flash-diagnostics.ps1
   ```
   *(Script tự động biên dịch, nạp vào ESP32 trên cổng COM6 và mở Serial Monitor 115200 baud).*

2. Trong cửa sổ Serial Monitor:
   - **Bấm nút `BOOT` trên bo ESP32**: Kích mở khóa Solenoid ngay lập tức (500ms).
   - Nhấn phím **`1`** hoặc **`2`**: Kích nhả Relay Ô 1 / Ô 2.
   - Nhấn phím **`a`**: Bật chế độ tự động kích mở khóa mỗi 3 giây một lần.
   - Nhấn phím **`w`**: Quét danh sách các mạng Wi-Fi 2.4GHz xung quanh.
   - Nhấn phím **`s`**: Đọc trạng thái tất cả cảm biến cửa.
   - Đưa nam châm lại gần cảm biến từ $\rightarrow$ Màn hình log lập tức báo `DOOR CLOSED` hoặc `DOOR OPENED`.

---

## 6. Cấu hình & Chạy chính thức với FloraBot Backend

Mở file [`include/config.h`](include/config.h) và điền các thông số kết nối:

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

Theo dõi log hoạt động của ESP32 qua Serial Monitor:
```powershell
.\monitor.ps1
```

---

## 7. Cấu trúc thư mục dự án

```text
FloraBot_IOT/
├── include/
│   ├── config.h               # File cấu hình Wi-Fi, MQTT, chân GPIO Relay & Cảm biến
│   └── config.example.h       # Cấu hình mẫu ban đầu
├── src/
│   ├── main.cpp               # Firmware chính thức kết nối FloraBot Backend qua MQTT TLS
│   └── diagnostics.cpp        # Firmware kiểm tra phần cứng (Relay, Cảm biến, Quét Wi-Fi, nút BOOT)
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

## 8. Giao thức truyền thông (MQTT Wire Protocol)

Theo thiết kế hệ thống FloraBot:
- **Lệnh mở cửa (Backend -> ESP32)**:
  - Topic: `kiosk/{hardware_id}/cmd` (QoS 1)
  - Dữ liệu JSON có chữ ký HMAC-SHA256, chỉ có hiệu lực trong cửa sổ thời gian (60s).
- **Sự kiện phản hồi (ESP32 -> Backend)**:
  - Topic: `kiosk/{hardware_id}/evt` (QoS 1)
  - Các sự kiện: `HEARTBEAT` (mỗi 30s), `ACK` (đã nhận lệnh), `OPENED` (khách đã mở cửa ô), `CLOSED` (khách đã đóng cửa ô -> hoàn tất chu trình đơn hàng).
