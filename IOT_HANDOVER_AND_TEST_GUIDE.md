# HƯỚNG DẪN BÀN GIAO & TEST PHẦN CỨNG IOT FLORABOT (DÀNH CHO DEV / AI AGENT)
> **Tài liệu bàn giao phần cứng ESP32 & Quy trình kiểm thử mở khóa thực tế**  
> Cập nhật: 28/10/2026

---

## 1. TỔNG QUAN HỆ THỐNG
Dự án **FloraBot** sử dụng một mạch **ESP32 Dev Module** để điều khiển 4 hộc tủ bán hoa tự động độc lập tại Kiosk (`K-Q1-01`, Hardware ID: `ESP32-A1B2C3`).
Mạch giao tiếp với Production Backend VPS (`api.florabot.site`) thông qua giao thức **MQTT bảo mật qua TLS (Port 8883)**.

### Luồng vận hành mở cửa chuẩn:
1. **Khách thanh toán:** Khách quét VietQR PayOS trên màn hình Kiosk (hoặc app).
2. **Backend xác nhận:** Webhook PayOS báo tiền về tài khoản -> Backend chuyển đơn sang `DISPENSING`.
3. **Phát lệnh mở khóa (Unlock Command):** Backend tạo lệnh chứa thông tin hộc tủ, ký số HMAC-SHA256 bằng khóa bí mật riêng của Kiosk, rồi publish tới topic:
   `kiosk/ESP32-A1B2C3/cmd` (QoS 1).
4. **ESP32 nhận và mở khóa:**
   - ESP32 xác thực chữ ký HMAC và kiểm tra thời gian thực (NTP).
   - ESP32 kiểm tra cảm biến cửa: **Cửa phải đang ĐÓNG (tiếp điểm thông GND)** thì ESP32 mới chấp nhận mở.
   - ESP32 kích xung Relay 500ms (Relay nhảy "TÁCH", mở chốt điện 12V).
   - ESP32 gửi sự kiện xác nhận `ACK` về Backend qua topic:
     `kiosk/ESP32-A1B2C3/evt`.
5. **Khách lấy hoa & đóng cửa:**
   - Cửa mở ra -> Cảm biến từ hở mạch -> ESP32 gửi sự kiện `OPENED`.
   - Khách lấy hoa xong đóng cửa lại -> Cảm biến thông mạch lại -> ESP32 gửi sự kiện `CLOSED`.
   - Backend nhận đủ `OPENED` và `CLOSED` -> Chuyển trạng thái đơn hàng sang `COMPLETED`.

---

## 2. SƠ ĐỒ ĐẤU DÂY PHẦN CỨNG (PINOUT & WIRING)

### A. Bảng chân kết nối 4 hộc tủ:
| Ô Kiosk | Kênh Relay (Backend) | Chân Relay (ESP32) | Chân Cảm biến MC-38 (ESP32) | Ghi chú |
| :---: | :---: | :---: | :---: | :--- |
| **Ô 1** | Relay Channel `0` | **GPIO 26** | **GPIO 27** | |
| **Ô 2** (Đang test ô A02) | Relay Channel `1` | **GPIO 14** | **GPIO 13** | **Chân chính dùng để test** |
| **Ô 3** | Relay Channel `2` | **GPIO 25** | **GPIO 32** | |
| **Ô 4** | Relay Channel `3` | **GPIO 33** | **GPIO 18** | |

### B. Quy tắc đấu dây điện quan trọng:
1. **Module Relay (Kích mức HIGH - Active HIGH):**
   - VCC nối 5V (hoặc nguồn ngoài 5V cấp chung GND với ESP32).
   - GND nối GND chung của ESP32.
   - IN1 -> GPIO 26, IN2 -> GPIO 14, IN3 -> GPIO 25, IN4 -> GPIO 33.
   - Tiếp điểm COM - NO của rơ-le đóng/ngắt nguồn 12V cấp cho khóa điện từ. **TUYỆT ĐỐI KHÔNG ĐƯA NGUỒN 12V VÀO CHÂN GPIO CỦA ESP32.**
2. **Cảm biến cửa MC-38 (Loại công tắc từ tiếp điểm khô 2 dây):**
   - Dây 1 nối vào chân GPIO cảm biến (ví dụ **GPIO 13** cho ô 2).
   - Dây 2 nối vào chân **GND** của ESP32.
   - Firmware sử dụng `pinMode(pin, INPUT_PULLUP)`:
     * **Cửa ĐÓNG (nam châm áp sát):** Tiếp điểm đóng -> Nối thông xuống GND -> Đọc mức **LOW**.
     * **Cửa MỞ (nam châm tách ra):** Tiếp điểm hở -> Điện trở kéo lên 3.3V -> Đọc mức **HIGH**.
   - ⚠️ **LƯU Ý CỰC KỲ QUAN TRỌNG:** ESP32 kiểm tra an toàn trước khi mở khóa: Nếu chân cảm biến đang hở (chưa nối GND hoặc dây lỏng) -> ESP32 hiểu là cửa đã mở sẵn, và sẽ **BỎ QUA LỆNH MỞ KHÓA**! Do đó, **phải đảm bảo cảm biến đang đóng (hoặc dây cắm chắc chắn vào GND) trước khi gửi lệnh mở khóa**.

---

## 3. THÔNG SỐ CẤU HÌNH BẢO MẬT & MQTT

Tất cả cấu hình nằm trong file: [`FloraBot_IOT/include/config.local.h`](file:///d:/academy/KY9/SEP490/FLorabot/FloraBot_IOT/include/config.local.h)

- **Hardware ID:** `ESP32-A1B2C3`
- **Wi-Fi:**
  - SSID mặc định: `FPT_Music` (Nếu đổi mạng Wi-Fi khác, sửa dòng `wifiSsid` và `wifiPassword` trong `config.local.h`).
- **MQTT Broker (VPS Production):**
  - Host: `api.florabot.site`
  - Port: `8883` (SSL/TLS v1.2)
  - Username: `kiosk_device`
  - Password: `E106D8416D942A5056EA2694BEF07DB0179EE0B9464BD895B5218C46239EE25F`
- **Khóa bí mật HMAC-SHA256 (32 bytes):**
  - Hex: `46EDF9F65A5CAE35E70C8C7D13B63AFAAE2CEA98FB2EDBC1406A4E837620B101`
- **Chứng chỉ CA:**
  - Nhúng trực tiếp dạng chuỗi PEM `caCertificate` trong `config.local.h`.
- **Nhịp tim (Heartbeat):**
  - ESP32 tự động gửi bản tin Heartbeat có ký số mỗi **30 giây** lên topic `kiosk/ESP32-A1B2C3/evt`.
  - Backend nhận được nhịp tim sẽ hiển thị Kiosk ở trạng thái `ONLINE` trên trang quản trị.

---

## 4. BẢN VÁ FIRMWARE QUAN TRỌNG (ĐÃ FIX SẴN TRONG CODE)

### Hiện tượng lỗi trước đây (Deadlock Inspection):
Khi rơ-le kích mở cửa, nếu cảm biến không ghi nhận thao tác MỞ rồi ĐÓNG trong vòng 60 giây (`config::doorTimeoutMs = 60000`), Firmware ESP32 sẽ chuyển ô cửa đó sang trạng thái `requireInspection` (cần kiểm tra) và lưu vào Flash NVS. Vì mạch không có nút cứu hộ phần cứng, cửa bị kẹt vĩnh viễn ở trạng thái này và từ chối mọi lệnh mở khóa tiếp theo (`ClaimResult::Busy`).

### Bản vá đã thêm vào [`FloraBot_IOT/src/main.cpp`](file:///d:/academy/KY9/SEP490/FLorabot/FloraBot_IOT/src/main.cpp):
Tính năng **Auto-Clear Recovery**:
```cpp
if (journal->recoveryRequired() && sensors[i].closed && closedNow(channel)) {
    journal->clearRecoveryAfterInspection(true);
    Serial.printf("[Auto-Clear] Door %u recovery cleared (sensor closed).\n", channel.relayChannel);
}
```
-> **Tác dụng:** Chỉ cần cửa được đóng lại (chân cảm biến chạm GND), ESP32 sẽ tự động xóa cờ kẹt và đưa cửa về trạng thái sẵn sàng (`Phase::Idle`), không bao giờ bị kẹt cứng nữa!

---

## 5. CÁCH NẠP FIRMWARE (FLASHING GUIDE)

1. Cắm cáp USB nối ESP32 với máy tính.
2. Kiểm tra máy tính đã nhận cổng COM (thường là `COM3` qua chip CP210x hoặc CH340).
3. Mở PowerShell tại thư mục `FloraBot_IOT` và chạy:
   ```powershell
   pio run -t upload -e esp32dev
   ```
   *(Hoặc chạy script: `./flash.ps1`)*
4. Để xem log Serial thời gian thực (Baudrate 115200):
   ```powershell
   pio device monitor -b 115200
   ```
   *(Hoặc chạy script: `./monitor.ps1`)*
5. **Dấu hiệu Firmware chạy tốt trên Serial Monitor:**
   - Kết nối Wi-Fi thành công: `[WiFi] Connected!`
   - Đồng bộ giờ NTP thành công: `[NTP] Time synchronized`
   - Bắt tay TLS thành công và kết nối MQTT Broker `api.florabot.site:8883`.
   - Nhận SUBACK: `[MQTT] Subscription to cmd topic confirmed (SUBACK).`
   - Mỗi 30s xuất hiện 1 gói tin `HEARTBEAT`.

---

## 6. QUY TRÌNH TEST (DÀNH CHO AGENT / DEVELOPER TIẾP QUẢN)

### KỊCH BẢN A: TEST NHẢY RƠ-LE TRỰC TIẾP (KHÔNG CẦN QUÉT TIỀN)
Dùng để kiểm tra mạch phần cứng, tiếp điểm rơ-le và cảm biến đóng mở:

1. Đảm bảo chân **D13 đang chạm GND** (mô phỏng cửa đang đóng).
2. Chạy script test từ thư mục gốc dự án:
   ```powershell
   python FloraBot_BE/scripts/test_unlock.py
   ```
3. **Hiện tượng quan sát:**
   - Rơ-le 2 (nối chân GPIO 14) sẽ nhảy "TÁCH" trong 500ms rồi nhả.
   - Script Python nhận được gói tin sự kiện `ACK` từ ESP32.
4. **Test tiếp cảm biến cửa:**
   - Rút dây D13 khỏi GND (mô phỏng mở cửa) -> ESP32 gửi sự kiện `OPENED`.
   - Cắm lại dây D13 vào GND (mô phỏng đóng cửa) -> ESP32 gửi sự kiện `CLOSED`.

---

### KỊCH BẢN B: TEST LUỒNG THANH TOÁN TIỀN THẬT (PAYOS -> MỞ CỬA)

Quy trình kiểm thử toàn vẹn từ quét mã VietQR PayOS đến khi cửa mở và hoàn tất đơn hàng:

#### Bước 1: Reset trạng thái ô A02 trên Database VPS
Mở PowerShell trên VPS (hoặc kết nối SSH/RDP) và chạy:
```powershell
& "C:\Program Files\PostgreSQL\16\bin\psql.exe" -U florabot -d florabot -c "UPDATE kiosk_ops.slots SET status = 'STOCKED', hold_order_id = NULL, hold_until = NULL WHERE id = '40000000-0000-0001-000000000002'; UPDATE kiosk_ops.bouquets SET status = 'STOCKED', price_snapshot = 2000 WHERE id = '01a1530c-de28-7f4d-9f4f-bdbd374e169c';"
```
*(Lệnh này đặt lại ô hoa A02 thành `STOCKED` và giá thanh toán là `2.000 VNĐ`)*.

#### Bước 2: Tạo đơn hàng và lấy mã QR thanh toán
Tại máy tính đang kết nối Backend, chạy script:
```powershell
python FloraBot_BE/scripts/create_checkout_2k.py
```
Script sẽ in ra đường link thanh toán PayOS VietQR, ví dụ:
`https://pay.payos.vn/web/xxxxxx`

Đồng thời script sẽ in ra lệnh monitor đi kèm:
```powershell
python FloraBot_BE/scripts/monitor_paid_flow.py <CHECKOUT_ID>
```

#### Bước 3: Chạy Monitor & Quét thanh toán
1. Mở một cửa sổ terminal chạy lệnh monitor ở trên để xem log thời gian thực.
2. Mở app ngân hàng bất kỳ, quét mã VietQR 2.000 VNĐ trên link thanh toán.
3. **Diễn biến tự động:**
   - App ngân hàng báo chuyển tiền thành công.
   - Webhook PayOS bắn về Backend -> Monitor báo `DISPENSING`.
   - Backend phát lệnh MQTT -> Rơ-le 2 (GPIO 14) **nhảy mở khóa**!
   - Thao tác cảm biến: Rút D13 ra (mở cửa) -> Cắm lại D13 vào GND (đóng cửa).
   - Monitor báo:
     ```
     🎉🎉🎉 ĐƠN HÀNG ĐÃ HOÀN TẤT THÀNH CÔNG! HOA ĐÃ ĐƯỢC LẤY! 🎉🎉🎉
     Trạng thái đơn hàng: COMPLETED
     ```

---

## 7. BẢNG MÃ LỖI & CÁCH XỬ LÝ NHANH (TROUBLESHOOTING)

| Hiện tượng | Nguyên nhân | Cách xử lý |
| :--- | :--- | :--- |
| ESP32 không kết nối được Wi-Fi | Sai SSID hoặc mật khẩu Wi-Fi, hoặc đang dùng mạng 5GHz | Cập nhật lại `wifiSsid` và `wifiPassword` trong `FloraBot_IOT/include/config.local.h` (ESP32 chỉ hỗ trợ Wi-Fi 2.4GHz). |
| Kiosk trên Web báo `OFFLINE` | ESP32 chưa kết nối được MQTT Broker hoặc mất nguồn | Kiểm tra Serial Monitor xem có lỗi TLS handshake không. Đảm bảo cổng 8883 trên VPS đang mở. |
| Thanh toán xong nhưng rơ-le không nhảy, đơn bị `DISPENSE_FAILED` | 1. Dây cảm biến D13 chưa nối GND (cửa hở mạch).<br>2. Cửa bị kẹt recovery do chưa nạp firmware mới. | 1. Cắm chắc dây D13 vào chân GND.<br>2. Nạp bản firmware mới nhất đã có Auto-Clear.<br>3. Chạy lệnh SQL ở Bước 1 để reset slot A02 về `STOCKED`. |
| Lệnh `pio run -t upload` báo không tìm thấy cổng COM | Thiếu driver CP210x hoặc cáp USB chỉ là cáp sạc (không truyền dữ liệu) | Đổi sang cáp USB truyền data tốt; cài đặt driver CP210x VCP từ Silicon Labs nếu Device Manager hiện dấu chấm than vàng. |

