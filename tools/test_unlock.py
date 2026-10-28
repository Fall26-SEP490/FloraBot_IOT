import os
import sys
import time
import uuid
import json
import hmac
import hashlib
import ssl
import paho.mqtt.client as mqtt

sys.stdout.reconfigure(encoding='utf-8')

# Cấu hình kết nối Broker Backend VPS
BROKER_HOST = "api.florabot.site"
BROKER_PORT = 8883
BACKEND_USER = "florabot-backend"
BACKEND_PASS = "58BD9CC5BDE987EC313DC9FA6921FD87EFBF2B2E97A5FFDA6AD25AEAB3A4A45F"

# Đường dẫn chứng chỉ CA nằm ngay trong thư mục tools/
CA_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ca.crt")

HARDWARE_ID = "ESP32-A1B2C3"
DEVICE_HMAC_KEY = bytes.fromhex("46EDF9F65A5CAE35E70C8C7D13B63AFAAE2CEA98FB2EDBC1406A4E837620B101")

# Kênh rơ-le kiểm thử: 1 = Hộc số 2 (Relay GPIO 14, Cảm biến GPIO 13)
RELAY_CHANNEL = 1 

def build_unlock_command(hardware_id, relay_channel, purpose="CUSTOMER_PICKUP"):
    cmd_id = str(uuid.uuid4()).lower()
    slot_id = str(uuid.uuid4()).lower()
    now = int(time.time())
    issued_at = now - 2
    expires_at = now + 120
    
    canonical = (
        f"florabot.unlock.v1\n"
        f"{hardware_id}\n"
        f"{cmd_id}\n"
        f"{slot_id}\n"
        f"{relay_channel}\n"
        f"{purpose}\n"
        f"{issued_at}\n"
        f"{expires_at}"
    )
    
    sig = hmac.new(DEVICE_HMAC_KEY, canonical.encode('utf-8'), hashlib.sha256).hexdigest().lower()
    
    payload = {
        "version": 1,
        "hardware_id": hardware_id,
        "cmd_id": cmd_id,
        "slot_id": slot_id,
        "relay_channel": relay_channel,
        "purpose": purpose,
        "issued_at": issued_at,
        "expires_at": expires_at,
        "signature": sig
    }
    return cmd_id, payload

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("[MQTT] Đã kết nối Broker an toàn qua TLS!")
        client.subscribe(f"kiosk/{HARDWARE_ID}/evt", qos=1)
        print(f"[MQTT] Đã lắng nghe kênh sự kiện: kiosk/{HARDWARE_ID}/evt")
    else:
        print(f"[MQTT] Lỗi kết nối Broker: rc={rc}")

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode('utf-8'))
        event_name = data.get("event")
        cmd = data.get("cmd_id")
        print(f"\n🔔 [ESP32 PHẢN HỒI SỰ KIỆN] -> Event: {event_name}")
        print(json.dumps(data, indent=2))
        if event_name == "ACK":
            print("\n=> ESP32 ĐÃ NHẬN LỆNH VÀ KÍCH RƠ-LE THÀNH CÔNG!")
            print("=> Bây giờ hãy THỬ RÚT DÂY CẢM BIẾN (D13) KHỎI GND để test sự kiện OPENED...")
        elif event_name == "OPENED":
            print("\n=> ESP32 XÁC NHẬN CỬA ĐÃ MỞ!")
            print("=> Bây giờ hãy CẮM LẠI DÂY CẢM BIẾN (D13) VÀO GND để test sự kiện CLOSED...")
        elif event_name == "CLOSED":
            print("\n=> ESP32 XÁC NHẬN CỬA ĐÃ ĐÓNG HOÀN TẤT CHU TRÌNH!")
    except Exception as e:
        print(f"[MQTT RAW] {msg.topic}: {msg.payload}")

def main():
    print(f"==================================================")
    print(f"  KIỂM THỬ MỞ KHÓA TRỰC TIẾP ESP32 FLORABOT")
    print(f"  Hộc tủ: Ô 2 | Relay Ch: {RELAY_CHANNEL} (GPIO 14) | Sensor: GPIO 13")
    print(f"==================================================")
    
    client = mqtt.Client(client_id=f"standalone-tester-{int(time.time())}", protocol=mqtt.MQTTv5)
    client.username_pw_set(BACKEND_USER, BACKEND_PASS)
    client.tls_set(ca_certs=CA_PATH, tls_version=ssl.PROTOCOL_TLSv1_2)
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"Đang kết nối Broker {BROKER_HOST}:{BROKER_PORT}...")
    client.connect(BROKER_HOST, BROKER_PORT, keepalive=30)
    client.loop_start()

    time.sleep(2)
    
    cmd_id, payload = build_unlock_command(HARDWARE_ID, RELAY_CHANNEL)
    topic = f"kiosk/{HARDWARE_ID}/cmd"
    print(f"\n[PHÁT LỆNH MỞ KHÓA] Gửi tới topic: {topic} ...")
    print(json.dumps(payload, indent=2))
    
    info = client.publish(topic, json.dumps(payload), qos=1)
    info.wait_for_publish(timeout=5)
    print(f"[ĐÃ GỬI] Đang chờ ESP32 phản hồi (trong 15 giây)...")
    
    # Lắng nghe 15 giây để quan sát ACK, OPENED, CLOSED
    for i in range(15):
        time.sleep(1)
        print(".", end="", flush=True)
    
    print("\nKết thúc phiên test.")
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()
