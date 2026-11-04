# FloraBot_IOT - Firmware điều khiển Kiosk Tủ Bán Hoa Tự Động (ESP32)

Dự án IoT điều khiển phần cứng cho hệ thống **FloraBot - Automated Flower Kiosk** (Đồ án Capstone FA26SE164).

Module này chạy trên vi điều khiển **ESP32**, chịu trách nhiệm:
1. Kết nối Wi-Fi và giao tiếp thời gian thực với **FloraBot Backend** qua giao thức **MQTT TLS** (Port 8883, QoS 1).
2. Điều khiển đóng/ngắt các rơ-le (Relay) kích xung mở khóa điện từ (Solenoid Lock 12V) cho từng ô hộc tủ hoa.
3. Giám sát trạng thái cảm biến từ gắn cửa (Magnetic Reed Switch) để phát hiện chính xác thời điểm cửa mở và cửa đóng.
4. Gửi tín hiệu nhịp tim sống (Heartbeat) định kỳ 30 giây để cập nhật trạng thái `ONLINE` của Kiosk trên hệ thống quản trị.
5. Xác thực an toàn bảo mật cấp công nghiệp: chữ ký điện tử **HMAC-SHA256** chống giả mạo lệnh, lưu vết trạng thái chống mất điện vào bộ nhớ flash **NVS Journal (ADR 0021 & ADR 0022)**.

---

## Thay bo ESP32 classic 38 chân

Số chân 30/38 không quyết định loại chip. Profile `esp32dev` dùng cho **ESP32
classic**, mặc định flash 4 MB; cần đọc đúng model và dung lượng flash trước khi
nạp. Không dùng profile này cho ESP32-S3/C3 chỉ vì bo có 38 chân.

Khi thay bo sau sự cố quá nhiệt, chỉ cắm USB vào bo mới, tháo toàn bộ relay,
cảm biến và bo đế cũ trong lần kiểm tra Wi-Fi/MQTT đầu tiên. Cổng mặc định hiện
là **COM6**; nếu Windows cấp cổng khác, truyền cùng cổng cho các script:

```powershell
.\test-chip.ps1 -Port COM6
.\flash.ps1 -Port COM6
.\monitor.ps1 -Port COM6
```

Wi-Fi, MQTT TLS và HMAC nằm trong `include/config.local.h` riêng tại máy.
Nếu bo mới thay thế cùng kiosk, giữ định danh `hardwareId` và thông tin được cấp
cho kiosk đó; không đổi sang MAC chip và không bật đồng thời hai bo cùng định
danh. Việc đổi bo không cần đổi API backend nếu kiosk và thông tin MQTT vẫn giữ
nguyên. Firmware giao tiếp thiết bị qua MQTT TLS 8883.

Bảng GPIO trong `include/hardware.h` giữ nguyên trên bo classic 38 chân:
relay **26, 14, 25, 33**; cảm biến **27, 13, 32, 18**. Đây là số **GPIO**, không
phải vị trí chân thứ 26/14/... trên hàng header. Đối chiếu nhãn IO/GPIO in trên
đúng bo. Cấu hình mặc định kích HIGH; chỉ nối relay sau khi xác minh cực tính,
đầu vào tương thích logic 3,3 V và đường cấp nguồn. GPIO không nhận 5 V/12 V.

Chờ log Wi-Fi kết nối, đồng bộ NTP và MQTT `SUBACK` để xác nhận kết nối thiết bị.
`SUBACK` xác nhận broker nhận đăng ký; cần kiểm tra heartbeat ở backend riêng để
xác nhận backend đã xử lý sự kiện. Khi chưa nối cảm biến, trạng thái cửa là mở
và firmware chính từ chối lệnh kích khóa cho cửa đó.

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
| **7** | **4 bộ cảm biến từ MC-38** | Bản tiếp điểm khô 2 dây, mỗi cửa một công tắc và một nam châm | Nhận biết trạng thái cửa; đo thông mạch để xác nhận cực tính trước khi lắp |
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
   - Cọc **`IN3`** (Relay 3): Nối vào chân **GPIO 25 (`D25`)** của ESP32.
   - Cọc **`IN4`** (Relay 4): Nối vào chân **GPIO 33 (`D33`)** của ESP32.
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
| **Cảm biến từ Cửa 2** | **GPIO 13 (D13)** | Báo đóng/mở cửa Ô số 2 | Nối giữa GPIO 13 & GND; chuyển khỏi D12 cũ |
| **Relay Ô 3** | **GPIO 25 (D25)** | Mở khóa Ô số 3, kênh 2 | Nối D25 sang `IN3` |
| **Cảm biến từ Cửa 3** | **GPIO 32 (D32)** | Báo đóng/mở cửa Ô số 3 | Nối giữa GPIO 32 & GND |
| **Relay Ô 4** | **GPIO 33 (D33)** | Mở khóa Ô số 4, kênh 3 | Nối D33 sang `IN4` |
| **Cảm biến từ Cửa 4** | **GPIO 18 (D18)** | Báo đóng/mở cửa Ô số 4 | Nối giữa GPIO 18 & GND; chuyển khỏi D35 cũ |
| **Nút bấm BOOT** | **GPIO 0** | Thử khóa 1 trong diagnostics | Firmware chính không dùng BOOT để mở khóa |
| **LED trạng thái** | **GPIO 2 (D2)** | Nhấp nháy trong diagnostics | Không phải xác nhận Backend đã nhận heartbeat |

> **Cảm biến từ cửa (Reed Switch):** Firmware sử dụng chế độ kéo lên nội (`INPUT_PULLUP`):
> - **Cửa ĐÓNG** (nam châm áp sát công tắc): Khép mạch xuống `GND` $\rightarrow$ Đọc mức `LOW`.
> - **Cửa MỞ** (nam châm tách xa): Hở mạch $\rightarrow$ Đọc mức `HIGH`.

Các mức trên là cấu hình mặc định cho cảm biến thông mạch khi nam châm áp sát. Đo từng bộ MC-38 bằng đồng hồ trước khi lắp; nếu loại thực tế ngược lại, đổi `sensorClosedLow` của cửa đó thành `false` trong `include/hardware.h`. Vẫn nối cảm biến giữa GPIO và GND và dùng `INPUT_PULLUP`; không cấp 5V/12V vào GPIO.

`include/hardware.h` là bảng chân dùng chung cho diagnostics và cấu hình chính. Bốn ô vật lý tương ứng `relay_channel` **0, 1, 2, 3** trong Backend; `slot_id` vẫn là UUID của ô trong cơ sở dữ liệu. Mỗi khóa cần nhánh COM/NO và diode riêng như sơ đồ Ô 1. Nguồn phải đáp ứng tổng dòng của các khóa có thể kích đồng thời cùng ESP32 và relay; không mặc định nguồn thử một khóa đủ cho bốn khóa.

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
   - **Bấm nút `BOOT` trên bo ESP32**: Kích mở khóa Solenoid ngay lập tức (3000 ms).
   - Khi khởi động, tất cả relay ở trạng thái tắt; diagnostics không tự mở khóa.
   - Nhấn phím **`1`**, **`2`**, **`3`**, **`4`**: Kích khóa Ô 1 / Ô 2 / Ô 3 / Ô 4 trong 3000 ms.
   - Nhấn phím **`a`**: Bật/tắt thử lần lượt bốn khóa, mỗi lần bắt đầu cách nhau 3,5 giây (3 giây cấp điện + 0,5 giây nghỉ).
   - Nhấn phím **`w`**: Quét danh sách các mạng Wi-Fi 2.4GHz xung quanh.
   - Nhấn phím **`s`**: Đọc trạng thái tất cả cảm biến cửa.
   - Đưa nam châm lại gần/tách xa từng cảm biến: log `DOOR CLOSED` / `DOOR OPENED` sau khi tín hiệu ổn định 40 ms. Trạng thái cửa mở không chứng minh khách đã lấy hoa.
   - Cảm biến vẫn được theo dõi trong khi kích khóa; timer riêng giới hạn xung và quét Wi-Fi chạy bất đồng bộ.

---

### Thử 4 cảm biến vật cản LM393

Diagnostics hỗ trợ **4 LM393**, mỗi ô một cảm biến, bên cạnh 4 khóa và 4 MC-38.
Mỗi LM393 nối **VCC → 3V3**, **GND → GND**, còn OUT đi riêng theo bảng:

| Ô | OUT của LM393 → GPIO / cọc trên đế |
| :--- | :--- |
| 1 | GPIO21 / D21 / P21 |
| 2 | GPIO22 / D22 / P22 |
| 3 | GPIO19 / D19 / P19 |
| 4 | GPIO23 / D23 / P23 |

Có thể chỉ lắp một LM393 để thử; các chân OUT còn lại để hở. Do dùng
`INPUT_PULLUP`, các chân chưa cắm đọc HIGH, chương trình ghi rõ
`KHONG PHAT HIEN VAT (HOAC CHUA CAM CAM BIEN)`, không tự xác nhận ô trống.
Sau này lắp thêm đúng bảng chân là dùng được, không cần đổi số cảm biến trong code.
Chia nguồn và GND qua đầu nối phù hợp; không nối chung các dây OUT. Trên đế
terminal bắt vít, dùng đúng cọc tương ứng với chân ESP32; không mặc định nguồn
trên đế là 3,3 V. Không cấp 12 V cho module hoặc đưa OUT 5 V vào GPIO.

Nạp bản diagnostics và xem Serial 115200:

```powershell
.\flash-diagnostics.ps1 -Port COM6
```

Thay COM6 bằng cổng thực tế. Khi khởi động, từng LM393 báo `CHUA XAC DINH` cho đến
khi tín hiệu của ô đó ổn định đủ **1 giây**. Mỗi ô có bộ lọc riêng; ô đang nhiễu
không chặn cập nhật các ô khác. Log ghi số ô và GPIO, ví dụ:

```text
[LM393] O 1 | GPIO 21 | ON DINH 1000 ms: CO VAT tai vi tri dat hoa
[LM393] O 2 | GPIO 22 | ON DINH 1000 ms: KHONG PHAT HIEN VAT (HOAC CHUA CAM CAM BIEN)
```

- **`i`**: xem OUT tức thời (`raw=LOW/HIGH`) và kết quả đã lọc của cả 4 LM393.
- **`p`**: bật/tắt in trạng thái cả 4 LM393 mỗi 500 ms để chỉnh biến trở.
- **`s`**: xem cả 4 MC-38 và 4 LM393; **`h`**: trợ giúp và sơ đồ nối.

Mặc định OUT LOW là phát hiện vật. Nếu module thực tế ngược lại, đổi
`LM393_DETECTED_LEVEL` trong `src/diagnostics.cpp` rồi nạp lại. Để thử tủ ngang
15 cm: cố định cảm biến, đóng cửa khi ô trống, chỉnh biến trở để không nhận vách;
đặt hoa khoảng 5 cm trước cảm biến và kiểm tra có phát hiện. Thử cả vị trí lệch
và các loại giấy gói thực tế. Biến trở chỉnh độ nhạy, không đặt khoảng cách
chính xác theo cm; code không thể phân biệt vách và hoa nếu OUT giống nhau.

Đây là kiểm tra vật cản offline, không gửi MQTT hoặc xác nhận lấy hàng. Không
dùng cảm biến cửa để chặn log LM393, để có thể thử riêng khi chưa lắp MC-38.
OUT HIGH cũng có thể do dây OUT bị rút; `KHONG PHAT HIEN VAT` không xác nhận
cảm biến còn kết nối. Các lệnh relay và BOOT vẫn hoạt động như diagnostics cũ.

**Nếu OUT luôn LOW dù chỉnh biến trở:**

1. Tắt nguồn trước khi đổi dây. Thử bo cảm biến rời khỏi vít kim loại/giá đỡ;
   tránh đầu vít chạm chân linh kiện hoặc điểm hàn.
2. Rút riêng dây OUT khỏi cọc của ô cần kiểm tra (ô 1 là GPIO21), giữ đầu dây
   rời không chạm gì. Cấp nguồn lại, nhấn `i`: với `INPUT_PULLUP`, chân để hở phải
   đọc HIGH. Nếu vẫn LOW, kiểm tra đúng cọc theo bảng, dây chạm GND, đế hoặc
   chân ESP32 trước khi chỉnh cảm biến.
3. Nếu chân để hở đọc HIGH, tắt nguồn rồi nối lại OUT. Thử cảm biến hướng ra
   vùng trống, tránh nắng trực tiếp; nhấn `p` và đối chiếu LED phát hiện với
   `raw=LOW/HIGH`. Nếu LED thay đổi nhưng raw không đổi, kiểm tra dây OUT/cọc
   nối. Nếu cả LED và OUT luôn kích hoạt, cần kiểm tra nguồn thực tế VCC-GND,
   biến trở và module; chưa thể kết luận cảm biến hỏng chỉ từ log LOW.
4. Không đảo `LM393_DETECTED_LEVEL` chỉ để che OUT bị kẹt. Không đổi sang nguồn
   5 V khi OUT vẫn nối trực tiếp GPIO; phải xử lý mức logic phù hợp trước.

## 6. Cấu hình & Chạy chính thức với FloraBot Backend

Main hỗ trợ **4 LM393** theo cùng bảng OUT của diagnostics: ô 1..4 lần lượt
**GPIO21, GPIO22, GPIO19, GPIO23**, VCC 3V3 và GND chung. Bảng chân dùng chung
ở `include/hardware.h` (`flowerSensorPins`), không phải sửa thêm mật khẩu hay
thông tin thiết bị trong `config.local.h`. Main kiểm tra trùng chân relay,
MC-38, LM393 và nút bảo trì trước khi bật phần cứng. GPIO23 không còn dùng
đồng thời làm nút bảo trì; cấu hình compile-check dùng GPIO16 cho nút này.

Task cảm biến lấy mẫu cả khi Wi-Fi/MQTT mất kết nối. Với cửa đóng, tín hiệu
phải ổn định **1 giây** mới báo có/không có hoa; cửa đang mở hoặc tín hiệu đang
đổi là **chưa xác định**. HIGH cũng có thể do chưa lắp cảm biến. Serial tự báo
thay đổi từng ô, ví dụ `[LM393] O 1 | GPIO 21 | CO HOA (PHAT HIEN VAT)`.

Main gửi snapshot `FLOWER_PRESENCE` có chữ ký HMAC qua MQTT QoS 1 trên topic
`kiosk/{hardwareId}/evt`, khi đổi trạng thái, kết nối lại và định kỳ 15 giây.
Thứ tự `states` là relay 0..3; -1 chưa xác định, 0 chưa phát hiện, 1 có vật.
`occurred_at` của snapshot này dùng Unix mili giây; miền chữ ký riêng
`florabot.flower.v1` được mô tả trong ADR 0056 của BE. Bản tin cửa hiện tại
vẫn giữ giao thức cũ. LM393 chưa tự thay đổi tồn kho hoặc xác nhận lấy hàng.

Để xem trên web cần cập nhật BE/FE cùng tính năng và áp dụng migration
`20261104_flower_presence.sql` của BE theo quy trình triển khai. Web có mục
**Cảm biến hoa trong tủ** ở tủ của seller, chi tiết ô admin và kho tủ khả dụng
của manager. Web hiện Có hoa / Không có hoa / Chưa xác định; dữ liệu cũ quá
45 giây hoặc kiosk offline không được coi là trạng thái hiện tại.

Thời gian kích relay mặc định là **3000 ms (3 giây)** cho cả diagnostics và
firmware chính. `include/hardware.h` đặt thời gian chung và giới hạn tối đa
3000 ms; `config.example.h` và fixture compile-check dùng cùng giá trị này.
Cấu hình riêng `config.local.h` được ưu tiên: khi nâng cấp máy khác, cần đổi
`config::pulseMs` trong file đó thành 3000 hoặc `hardware::pulseMs` rồi nạp lại.
Firmware chính vẫn ngắt sớm khi đóng xong cửa, có lỗi hoặc cần kiểm tra tại chỗ;
3 giây là thời hạn tối đa của xung. Timer ngắt relay chạy độc lập với Wi-Fi/MQTT.
Chỉ dùng xung 3 giây với khóa có thông số thời gian cấp điện phù hợp. Nguồn
12 V–2 A không có đủ dự phòng cho ba khóa 0,6 A cùng ESP32, relay và cảm biến;
cần tính dòng của khóa thực tế trước khi thử nhiều khóa cùng lúc.

Sao chép [`include/config.example.h`](include/config.example.h) thành `include/config.local.h`, rồi điền thông số kết nối trong file riêng này:

`include/config.local.h` được Git và Docker bỏ qua để giữ riêng mật khẩu Wi-Fi, MQTT và khóa HMAC. Firmware ưu tiên file này; nếu không có, `config.h` dùng cấu hình mẫu chưa điền thông tin thiết bị. Bản firmware đã biên dịch từ cấu hình thật cũng chứa thông tin riêng và không đưa lên Git.

Kiểm tra `channels` trong file riêng cũng khai báo đủ 4 cửa; cấu hình mẫu đã dùng `using Channel = hardware::Channel;` và `inline constexpr auto channels = hardware::channels;` sau khi include `hardware.h`.

**Nâng cấp thiết bị đã lưu journal 1/2 cửa:** firmware dừng nếu tập kênh trong NVS khác cấu hình 4 cửa, để giữ dấu chống mở lặp. Cần quy trình bảo trì/migrate trạng thái có kiểm soát trước khi chạy chính thức; không tự xóa flash để bỏ qua kiểm tra. Bản cập nhật này không tự migrate hoặc nạp thiết bị.

```cpp
// 1. Tên và mật khẩu Wi-Fi
inline constexpr char wifiSsid[] = "Tên_WiFi_Của_Bạn";
inline constexpr char wifiPassword[] = "Mật_Khẩu_WiFi";

// 2. Địa chỉ IP máy tính đang chạy Backend / Mosquitto MQTT
// Dùng địa chỉ LAN hoặc tên DNS truy cập được từ ESP32, không dùng localhost.
// Địa chỉ này phải khớp SAN trên chứng chỉ TLS của MQTT broker.
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
│   ├── config.h               # Cấu hình công khai, sử dụng config.example.h
│   ├── config.local.h         # Cấu hình thật tại máy, không đưa lên Git hoặc Docker
│   ├── hardware.h             # Bảng chân chung: 4 khóa, 4 cảm biến MC-38, ghi chú đấu dây
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
