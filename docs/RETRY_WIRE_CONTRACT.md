# FloraBot IoT - Explicit Retry Wire Contract & Runtime Specification

## 1. Scope, Constraints & Architectural Principles

This document defines the explicit, authenticated retry wire contract and runtime dispatch logic for the FloraBot IoT kiosk firmware (ADR 0021 / ADR 0022 increment):

- **No Periodic or Presence-Triggered Pulses**: Solenoid relay pulses are never fired automatically by background timers, loops, or optical/LM393 presence sensors. Actuation occurs strictly upon explicit, cryptographically authenticated commands.
- **Explicit Authenticated Retry Timing**: If a solenoid pulse has fired but the door remains closed (`Phase::WaitingOpen`), an explicit authenticated retry command can be accepted only when:
  1. `requested_at >= lastPulseAt + 30` (signed at least 30s after the prior pulse).
  2. `now >= lastPulseAt + 30` (delivered / evaluated at least 30s after the prior pulse).
  3. `now < activeClaim.expires` (before the original unlock command expires).
- **Maximum 2 Total Pulses per Command**: Exactly 1 initial unlock pulse (`pulseCount == 1`) and at most 1 authenticated retry pulse (`pulseCount == 2`). Any further retry attempt is rejected.
- **Immediate Pulse Cut-Off on Door Opened**: When the magnetic reed switch detects that the door is opened (`!sensors[i].closed`), the firmware immediately cuts the relay pulse (`stopPulse(i)`) *before* invoking `journal->opened(...)`, ensuring power is dropped even if persistence encounters a flash write fault.
- **Configured Pulse Duration Unchanged**: Kiosk firmware maintains its configured pulse duration (`config::pulseMs`, defaulting to 3000 ms), not backend default (500 ms).
- **Durable Preparation Before Actuation**: The retry state transitions to `Phase::Prepared` and commits to NVS before the hardware relay is energized. The subsequent actuation in `doorTask` transitions to `Phase::WaitingOpen` and commits `pulseCount = 2`.

---

## 2. Runtime Wire Contract & Message Dispatch (`src/main.cpp`)

Commands arrive via MQTT on the existing kiosk command topic:
- **Topic**: `kiosk/{hardware_id}/cmd`
- **MQTT Transport Constraints**: QoS 1, `retained == false`, payload size <= 1024 bytes.
- **Pre-Gate Checks**: Requires device initialization, synchronized NTP clock (`clockReady()`), and active broker subscription.

### Distinct Message Payloads

The firmware differentiates messages by exact JSON object size and field schema:
1. **Unlock Command (Exact 9 Fields)**:
   - Fields: `version`, `relay_channel`, `issued_at`, `expires_at`, `hardware_id`, `cmd_id`, `slot_id`, `purpose`, `signature`.
   - Domain: `florabot.unlock.v1`.
2. **Retry Command (Exact 7 Fields)**:
   - Fields: `version`, `hardware_id`, `request_id`, `cmd_id`, `requested_at`, `expires_at`, `signature`.
   - Domain: `florabot.retry.v1`.

### Retry JSON Payload Example

```json
{
  "version": 1,
  "hardware_id": "ESP32-A1B2C3",
  "request_id": "a1b2c3d4-e5f6-4a1b-8c2d-3e4f5a6b7c8d",
  "cmd_id": "abcdef01-2345-4678-9abc-def012345678",
  "requested_at": 1800000030,
  "expires_at": 1800000060,
  "signature": "fd721badd2b45c686de98a41c02a70178ddd7f0aa831340ea060388ee4f319c7"
}
```

### Runtime Dispatch Flow

1. **Verify HMAC Before Lock**: The HMAC signature of `RetryCommand` is verified against the shared device key and hardware ID under zero lock contention.
2. **Lock Acquisition**: Firmware takes `Lock lock;`.
3. **Parent Command Resolution**: The firmware searches configured channel journals for an active claim matching the case-normalized `cmd_id` currently in `Phase::WaitingOpen`.
4. **Exact Relay Sensor Evaluation**: The matching channel index `i` is selected, and its debounced and raw sensor state is evaluated: `sensors[i].closed && closedNow(config::channels[i])`.
5. **Prepare Retry**: Firmware calls `doors.prepareRetry(retryCmd, retryIdentity, closedFlag, currentTime)`.
6. **No Indefinite Extension**: `cycleStarted[i]` is **not** reset. The original pickup timeout (`config::doorTimeoutMs`) continues counting from the initial unlock command, preventing timeout circumvention.
7. **Actuation Execution**: Normal `Prepared` phase logic in `doorTask` verifies sensor closed, executes `journal->beginPulse(currentTime)` (persisting `pulseCount = 2` and `lastPulseAt`), and actuates the relay.

---

## 3. Canonical Representation & Known HMAC Vector

- **Canonical Domain**: `florabot.retry.v1`
- **Format**:
```text
florabot.retry.v1\n
{hardware_id}\n
{lower(request_id)}\n
{lower(cmd_id)}\n
{requested_at}\n
{expires_at}
```
No trailing newline. All UUIDs normalized to lowercase.

### Independent Test Vector (Python 3 stdlib `hmac` + `hashlib`)
- **Key**: `0x00, 0x01, ..., 0x1f` (32 bytes `bytes(range(32))`)
- **Hardware**: `ESP32-A1B2C3`
- **Request ID**: `a1b2c3d4-e5f6-4a1b-8c2d-3e4f5a6b7c8d` (or uppercase `A1B2C3D4-...`)
- **Cmd ID**: `abcdef01-2345-4678-9abc-def012345678` (or uppercase `ABCDEF01-...`)
- **Requested At**: `1800000030`
- **Expires At**: `1800000060`
- **Canonical Payload**:
```text
florabot.retry.v1
ESP32-A1B2C3
a1b2c3d4-e5f6-4a1b-8c2d-3e4f5a6b7c8d
abcdef01-2345-4678-9abc-def012345678
1800000030
1800000060
```
- **HMAC-SHA256 Digest**:
`fd721badd2b45c686de98a41c02a70178ddd7f0aa831340ea060388ee4f319c7`

---

## 4. Verification Distinctions: Native Host vs Physical Target

To ensure safety and reliability across platforms, test verification is strictly layered:

1. **Native Host Verification (`test/native.cpp`)**:
   - Executes on host machine / isolated Docker container with `clang++`/`g++` under `-fsanitize=address,undefined`.
   - Validates state transitions, journal replay protection, snapshot v1->v2 migration, HMAC vectors, timing boundaries (29s vs 30s), global clock rollback bypass prevention, and multi-door sensor routing.
   - Run under both OpenSSL and MbedTLS cryptography backends.
2. **Compile-Check Target Verification (`pio run -e esp32dev-compilecheck`)**:
   - Compiles full ESP32 firmware image (FreeRTOS tasks, GPIO drivers, ArduinoJson, NVS preferences, PubSubClient, TLS client) without hardware upload or active network communication.
   - Verifies memory constraints, type signatures, and stack safety.
3. **Physical On-Chip Testing (Out of Scope for this increment)**:
   - Requires actual ESP32 hardware connected via COM port with 12V solenoid coils and MC-38 reed switches.
   - Must be verified only after formal staging sign-off per ADR 0022.
