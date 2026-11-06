#include "Journal.h"
#include "Storage.h"
#include "DoorBank.h"
#include <openssl/hmac.h>
#ifdef FLORA_MBEDTLS
#include "MbedHmac.h"
#endif
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #condition); } while (false)

using namespace flora;
constexpr int64_t now = 1800000000;
Digest key() { Digest result{}; for (size_t i = 0; i < result.size(); ++i) result[i] = i; return result; }
bool hmac(const Digest& key, const std::string& message, Digest& result) {
#ifdef FLORA_MBEDTLS
  return mbedHmac(key, message, result);
#else
  unsigned size = 0;
  return HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const uint8_t*>(message.data()),
              message.size(), result.data(), &size) && size == result.size();
#endif
}
std::string id(unsigned number) {
  char value[37];
  std::snprintf(value, sizeof(value), "00000000-0000-4000-8000-%012u", number);
  return value;
}
Command command() {
  return {1, "ESP32-A1B2C3", "11111111-1111-1111-1111-111111111111",
          "22222222-2222-2222-2222-222222222222", 7, "CUSTOMER_PICKUP", now, now + 60,
          "c8795c2a6674c0de6c04d8823b5d53b5e606a493e358a3fffa14c475b6b20d29"};
}
Digest signedIdentity(Command& c) {
  std::string payload; Digest result{};
  CHECK(canonical(c, payload)); CHECK(hmac(key(), payload, result)); c.signature = hex(result);
  return result;
}
RetryCommand retryCommand(const std::string& cmdId, int64_t reqAt = now, int64_t expAt = now + 30) {
  return {1, "ESP32-A1B2C3", "33333333-3333-3333-3333-333333333333", cmdId, reqAt, expAt, ""};
}
Digest signedRetryIdentity(RetryCommand& r) {
  std::string payload; Digest result{};
  CHECK(canonical(r, payload)); CHECK(hmac(key(), payload, result)); r.signature = hex(result);
  return result;
}
static uint32_t testCrc(const uint8_t* bytes, size_t size) {
  uint32_t value = 0xffffffff;
  for (size_t i = 0; i < size; ++i) {
    value ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320 & (0u - (value & 1)));
  }
  return ~value;
}
static void testInt(std::vector<uint8_t>& out, uint64_t val, unsigned count) {
  for (unsigned i = count; i > 0; --i) out.push_back(val >> ((i - 1) * 8));
}
static void copyId(std::array<char, 37>& destination, const std::string& str) {
  std::string lower = str;
  for (char& c : lower) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
  std::copy(lower.begin(), lower.end(), destination.begin());
  destination[36] = 0;
}
static std::vector<uint8_t> encodeV1Snapshot(const Snapshot& s, uint32_t versionOverride = 1) {
  std::vector<uint8_t> out{'F', 'L', 'R', 'J'};
  out.reserve(4096);
  testInt(out, versionOverride, 4);
  testInt(out, s.clockFloor, 8);
  testInt(out, s.eventCount, 1);
  testInt(out, s.active, 1);
  testInt(out, static_cast<uint8_t>(s.phase), 1);
  for (const auto& c : s.claims) {
    out.insert(out.end(), c.id.begin(), c.id.end());
    out.insert(out.end(), c.identity.begin(), c.identity.end());
    testInt(out, c.expires, 8);
  }
  for (const auto& e : s.events) {
    out.insert(out.end(), e.id.begin(), e.id.end());
    out.insert(out.end(), e.command.begin(), e.command.end());
    testInt(out, e.at, 8);
    testInt(out, static_cast<uint8_t>(e.kind), 1);
  }
  testInt(out, testCrc(out.data(), out.size()), 4);
  return out;
}
struct Storage {
  Snapshot saved{};
  bool fail = false;
  unsigned writes = 0;
  Journal open() { return Journal(saved, [this](const Snapshot& candidate) {
    ++writes; if (fail) return false; saved = candidate; return true;
  }); }
};

void vectorAndFields() {
  auto c = command(); Digest identity{};
  CHECK(verify(c, c.hardware, key(), now, hmac, identity)); CHECK(hex(identity) == c.signature);
  for (unsigned field = 0; field < 9; ++field) {
    auto changed = c;
    switch (field) {
      case 0: changed.version = 2; break;
      case 1: changed.hardware = "FOREIGN"; break;
      case 2: changed.id = id(99); break;
      case 3: changed.slot = id(100); break;
      case 4: changed.relay++; break;
      case 5: changed.purpose = "SELLER_ACCESS"; break;
      case 6: changed.issued--; break;
      case 7: changed.expires++; break;
      case 8: changed.signature[0] = '0'; break;
    }
    CHECK(!verify(changed, c.hardware, key(), now, hmac, identity));
  }
  auto wrongKey = key(); wrongKey[0] ^= 1;
  CHECK(!verify(c, c.hardware, wrongKey, now, hmac, identity));
  CHECK(!verify(c, c.hardware, key(), now - 1, hmac, identity));
  CHECK(!verify(c, c.hardware, key(), now + 60, hmac, identity));
  CHECK(!verify(c, c.hardware, key(), 0, hmac, identity));
}

void canonicalValidation() {
  auto c = command(); std::string payload;
  for (const auto& invalid : {"", "../device", "device\nfield", "device/other"}) CHECK(!hardwareId(invalid));
  CHECK(!hardwareId(std::string(65, 'a')));
  CHECK(!uuid("00000000-0000-0000-0000-000000000000"));
  c.relay = 64; CHECK(!canonical(c, payload)); c = command();
  c.expires = c.issued; CHECK(!canonical(c, payload)); c = command();
  c.purpose = "OTHER"; CHECK(!canonical(c, payload)); c = command();
  c.id = "ABCDEFAB-1111-1111-1111-111111111111"; CHECK(canonical(c, payload));
  CHECK(payload.find("abcdefab-1111") != std::string::npos);
  CHECK(payload.back() != '\n');
  Digest bytes{}; CHECK(!decodeHex(std::string(64, 'g'), bytes));
  CHECK(!decodeHex(std::string(63, '0'), bytes));
}

void eventDomain() {
  std::string payload;
  CHECK(eventCanonical("ESP32-A1B2C3", id(1), command().id, "ACK", now, payload));
  CHECK(payload.rfind("florabot.event.v1\n", 0) == 0);
  CHECK(!eventCanonical("ESP32-A1B2C3", id(1), command().id, "SENT", now, payload));
  CHECK(!eventCanonical("ESP32-A1B2C3", id(1), command().id, "HEARTBEAT", now, payload));
  const std::string zero = "00000000-0000-0000-0000-000000000000";
  CHECK(eventCanonical("ESP32-A1B2C3", id(1), zero, "HEARTBEAT", now, payload));
  CHECK(!eventCanonical("ESP32-A1B2C3", zero, command().id, "ACK", now, payload));
  CHECK(!eventCanonical("ESP32-A1B2C3", id(1), zero, "ACK", now, payload));
}

void claimBeforePulseAndReplay() {
  Storage storage; auto journal = storage.open(); auto c = command(); auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(storage.saved.phase == Phase::Prepared && storage.saved.eventCount == 1);
  CHECK(journal.beginPulse(now)); CHECK(storage.saved.phase == Phase::WaitingOpen);
  CHECK(!journal.beginPulse(now));
  CHECK(journal.claim(c, digest, id(2), now) == ClaimResult::Duplicate);
  c.relay++; auto changed = signedIdentity(c);
  CHECK(journal.claim(c, changed, id(2), now) == ClaimResult::Conflict);
}

void crashWindows() {
  for (unsigned phase = 0; phase < 3; ++phase) {
    Storage storage; auto journal = storage.open(); auto c = command(); auto digest = signedIdentity(c);
    CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
    if (phase >= 1) CHECK(journal.beginPulse(now));
    if (phase >= 2) CHECK(journal.opened(id(2), now + 1));
    auto reboot = storage.open();
    CHECK(reboot.recoveryRequired()); CHECK(!reboot.beginPulse(now + 2));
    CHECK(reboot.claim(c, digest, id(3), now + 2) == ClaimResult::Duplicate);
    auto fresh = command(); fresh.id = id(20); auto newDigest = signedIdentity(fresh);
    CHECK(reboot.claim(fresh, newDigest, id(3), now + 2) == ClaimResult::Busy);
    CHECK(!reboot.closed(id(3), now + 2));
    CHECK(!reboot.clearRecoveryAfterInspection(false));
    CHECK(reboot.clearRecoveryAfterInspection(true));
    CHECK(reboot.claim(c, digest, id(3), now + 2) == ClaimResult::Duplicate);
  }
}

void storageFailure() {
  Storage storage; auto journal = storage.open(); auto c = command(); auto digest = signedIdentity(c);
  storage.fail = true;
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::StorageError);
  CHECK(!journal.healthy() && !journal.beginPulse(now)); CHECK(storage.saved.phase == Phase::Idle);
  storage.fail = false; auto retry = storage.open();
  CHECK(retry.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  storage.fail = true; CHECK(!retry.beginPulse(now)); CHECK(storage.saved.phase == Phase::Prepared);
  CHECK(!retry.healthy()); CHECK(storage.open().recoveryRequired());
}

void sensorOrderAndDurableEvents() {
  Storage storage; auto journal = storage.open(); auto c = command(); auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(!journal.closed(id(3), now)); CHECK(!journal.opened(id(2), now));
  CHECK(journal.beginPulse(now)); CHECK(!journal.closed(id(3), now));
  CHECK(journal.opened(id(2), now + 1)); CHECK(!journal.opened(id(4), now + 1));
  CHECK(journal.closed(id(3), now + 2)); CHECK(!journal.closed(id(4), now + 2));
  CHECK(storage.saved.eventCount == 3);
  auto reboot = storage.open(); CHECK(!reboot.recoveryRequired());
  CHECK(!reboot.acknowledge(id(2))); CHECK(reboot.acknowledge(id(1)));
  CHECK(storage.saved.events[0].kind == EventKind::Opened);
  storage.fail = true; CHECK(!reboot.acknowledge(id(2))); CHECK(storage.saved.eventCount == 2);
  storage.fail = false; auto again = storage.open();
  CHECK(again.acknowledge(id(2))); CHECK(again.acknowledge(id(3))); CHECK(storage.saved.eventCount == 0);
}

void pendingQueueCapacity() {
  Storage storage; auto journal = storage.open();
  for (unsigned i = 0; i < 5; ++i) {
    auto c = command(); c.id = id(i + 1); auto digest = signedIdentity(c);
    CHECK(journal.claim(c, digest, id(100 + 3 * i), now) == ClaimResult::Accepted);
    CHECK(journal.beginPulse(now)); CHECK(journal.opened(id(101 + 3 * i), now));
    CHECK(journal.closed(id(102 + 3 * i), now));
  }
  auto c = command(); auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(999), now) == ClaimResult::Full);
  CHECK(storage.saved.eventCount == 15 && storage.saved.phase == Phase::Idle);
}

void claimsCapacityAndClockRollback() {
  Storage storage; auto journal = storage.open();
  for (unsigned i = 0; i < 32; ++i) {
    auto c = command(); c.id = id(i + 1); auto digest = signedIdentity(c);
    CHECK(journal.claim(c, digest, id(100 + 3 * i), now) == ClaimResult::Accepted);
    CHECK(journal.beginPulse(now)); CHECK(journal.opened(id(101 + 3 * i), now));
    CHECK(journal.closed(id(102 + 3 * i), now));
    CHECK(journal.acknowledge(id(100 + 3 * i))); CHECK(journal.acknowledge(id(101 + 3 * i)));
    CHECK(journal.acknowledge(id(102 + 3 * i)));
  }
  auto c = command(); auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(999), now) == ClaimResult::Full);
  c.issued = now + 60; c.expires = now + 120; digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(999), now + 60) == ClaimResult::Accepted);
  auto reboot = storage.open(); CHECK(reboot.clearRecoveryAfterInspection(true));
  auto old = command(); old.id = id(1); auto oldDigest = signedIdentity(old);
  CHECK(reboot.claim(old, oldDigest, id(998), now + 1) == ClaimResult::Rejected);
}

void corruptedStateAndExpiry() {
  Snapshot invalid{}; invalid.eventCount = 17; CHECK(!Journal::valid(invalid));
  invalid = {}; invalid.phase = Phase::Prepared; CHECK(!Journal::valid(invalid));
  invalid = {}; invalid.version = 3; CHECK(!Journal::valid(invalid));
  invalid = {}; invalid.version = 0; CHECK(!Journal::valid(invalid));
  Snapshot bad999{}; bad999.version = 999;
  CHECK(!Journal::valid(bad999));
  CHECK(encodeSnapshot(bad999).empty());
  Journal broken(invalid, [](const Snapshot&) { return true; }); CHECK(!broken.healthy());
  Storage storage; auto journal = storage.open(); auto c = command(); auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(!journal.beginPulse(now + 60)); CHECK(!journal.beginPulse(now - 1));
}

void storageEncoding() {
  Storage storage; auto journal = storage.open(); auto c = command(); auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now)); CHECK(journal.opened(id(2), now + 1));
  const auto encoded = encodeSnapshot(storage.saved);
  CHECK(encoded.size() < 4096);
  Snapshot restored{};
  CHECK(decodeSnapshot(encoded.data(), encoded.size(), restored));
  CHECK(restored.phase == Phase::WaitingClose && restored.eventCount == 2);
  CHECK(restored.claims[restored.active].identity == digest);
  CHECK(restored.events[0].id == storage.saved.events[0].id);
  CHECK(encodeSnapshot(restored) == encoded);
  for (size_t offset = 0; offset < encoded.size(); ++offset) {
    auto corrupted = encoded; corrupted[offset] ^= 1;
    CHECK(!decodeSnapshot(corrupted.data(), corrupted.size(), restored));
  }
  CHECK(!decodeSnapshot(encoded.data(), encoded.size() - 1, restored));
  CHECK(!decodeSnapshot(nullptr, 0, restored));
}

void parallelDoorsAndGlobalReplay() {
  Storage first, second; DoorBank bank;
  CHECK(bank.add(7, first.saved, [&](const Snapshot& s) { first.saved = s; return true; }));
  CHECK(bank.add(8, second.saved, [&](const Snapshot& s) { second.saved = s; return true; }));
  CHECK(!bank.add(7, {}, [](const Snapshot&) { return true; }));
  auto a = command(); auto digestA = signedIdentity(a);
  auto b = command(); b.id = id(20); b.relay = 8; auto digestB = signedIdentity(b);
  CHECK(bank.claim(a, digestA, id(1), now) == ClaimResult::Accepted);
  CHECK(bank.claim(b, digestB, id(4), now) == ClaimResult::Accepted);
  CHECK(bank.door(7)->beginPulse(now)); CHECK(bank.door(8)->beginPulse(now));
  CHECK(bank.door(7)->opened(id(2), now)); CHECK(bank.door(8)->opened(id(5), now));
  CHECK(bank.door(8)->closed(id(6), now + 1));
  CHECK(bank.door(7)->snapshot().phase == Phase::WaitingClose);
  CHECK(bank.door(7)->closed(id(3), now + 2));
  auto rerouted = a; rerouted.relay = 8; auto changed = signedIdentity(rerouted);
  CHECK(bank.claim(rerouted, changed, id(7), now + 2) == ClaimResult::Conflict);
  CHECK(bank.claim(a, digestA, id(7), now + 2) == ClaimResult::Duplicate);
  size_t cursor = 0; Event event{}; uint8_t relay = 0;
  for (unsigned i = 0; i < 6; ++i) {
    CHECK(bank.nextEvent(cursor, event, relay));
    CHECK(relay == (i % 2 ? 8 : 7));
    CHECK(static_cast<unsigned>(event.kind) == i / 2);
    CHECK(!bank.acknowledge(relay == 7 ? 8 : 7, event.id.data()));
    CHECK(bank.acknowledge(relay, event.id.data()));
  }
  CHECK(!bank.nextEvent(cursor, event, relay));
  DoorBank reboot;
  CHECK(reboot.add(7, first.saved, [](const Snapshot&) { return true; }));
  CHECK(reboot.add(8, second.saved, [](const Snapshot&) { return true; }));
  CHECK(reboot.claim(rerouted, changed, id(7), now + 2) == ClaimResult::Conflict);
  CHECK(reboot.claim(a, digestA, id(7), now + 1) == ClaimResult::Rejected);
}

void independentRecoveryAndGlobalStorageFailure() {
  Storage first, second; DoorBank bank;
  CHECK(bank.add(7, first.saved, [&](const Snapshot& s) { first.saved = s; return !first.fail; }));
  CHECK(bank.add(8, second.saved, [&](const Snapshot& s) { second.saved = s; return true; }));
  auto a = command(); auto digestA = signedIdentity(a);
  CHECK(bank.claim(a, digestA, id(1), now) == ClaimResult::Accepted);
  CHECK(bank.door(7)->beginPulse(now));
  DoorBank reboot;
  CHECK(reboot.add(7, first.saved, [&](const Snapshot& s) { if (first.fail) return false; first.saved = s; return true; }));
  CHECK(reboot.add(8, second.saved, [&](const Snapshot& s) { second.saved = s; return true; }));
  CHECK(reboot.door(7)->recoveryRequired()); CHECK(!reboot.door(8)->recoveryRequired());
  auto b = command(); b.id = id(20); b.relay = 8; auto digestB = signedIdentity(b);
  CHECK(reboot.claim(b, digestB, id(4), now) == ClaimResult::Accepted);
  CHECK(reboot.door(8)->beginPulse(now)); CHECK(!reboot.door(7)->beginPulse(now));
  first.fail = true; CHECK(!reboot.acknowledge(7, id(1))); CHECK(!reboot.healthy());
  auto c = command(); c.id = id(21); c.relay = 8; auto digestC = signedIdentity(c);
  CHECK(reboot.claim(c, digestC, id(9), now) == ClaimResult::StorageError);
}

void retryHmacVectorTamperStaleCrossHardware() {
  auto r = retryCommand(command().id, now, now + 30);
  Digest identity{};
  std::string payload;
  CHECK(canonical(r, payload));
  CHECK(payload == "florabot.retry.v1\nESP32-A1B2C3\n33333333-3333-3333-3333-333333333333\n11111111-1111-1111-1111-111111111111\n1800000000\n1800000030");
  auto expectedDigest = signedRetryIdentity(r);
  CHECK(verify(r, r.hardware, key(), now, hmac, identity));
  CHECK(hex(identity) == r.signature);

  for (unsigned field = 0; field < 7; ++field) {
    auto changed = r;
    switch (field) {
      case 0: changed.version = 2; break;
      case 1: changed.hardware = "FOREIGN"; break;
      case 2: changed.request_id = id(99); break;
      case 3: changed.cmd_id = id(100); break;
      case 4: changed.requested_at--; break;
      case 5: changed.expires_at++; break;
      case 6: changed.signature[0] ^= 1; break;
    }
    CHECK(!verify(changed, r.hardware, key(), now, hmac, identity));
  }

  auto wrongKey = key(); wrongKey[0] ^= 1;
  CHECK(!verify(r, r.hardware, wrongKey, now, hmac, identity));

  CHECK(!verify(r, r.hardware, key(), now - 1, hmac, identity));
  CHECK(!verify(r, r.hardware, key(), now + 30, hmac, identity));
  CHECK(!verify(r, r.hardware, key(), 0, hmac, identity));

  CHECK(!verify(r, "ESP32-OTHER", key(), now, hmac, identity));

  auto wide = r; wide.expires_at = wide.requested_at + 31;
  std::string widePayload; CHECK(!canonical(wide, widePayload));
  CHECK(!verify(wide, r.hardware, key(), now, hmac, identity));

  auto inv = r; inv.expires_at = inv.requested_at;
  std::string invPayload; CHECK(!canonical(inv, invPayload));
  CHECK(!verify(inv, r.hardware, key(), now, hmac, identity));

  auto upper = r;
  upper.request_id = "33333333-3333-3333-3333-333333333333";
  for (char& c : upper.request_id) if (c >= 'a' && c <= 'f') c += 'A' - 'a';
  upper.cmd_id = "11111111-1111-1111-1111-111111111111";
  for (char& c : upper.cmd_id) if (c >= 'a' && c <= 'f') c += 'A' - 'a';
  CHECK(verify(upper, r.hardware, key(), now, hmac, identity));
  CHECK(identity == expectedDigest);
}

void independentKnownRetryHmacVector() {
  RetryCommand r{1, "ESP32-A1B2C3", "a1b2c3d4-e5f6-4a1b-8c2d-3e4f5a6b7c8d",
                 "abcdef01-2345-4678-9abc-def012345678", 1800000030, 1800000060,
                 "fd721badd2b45c686de98a41c02a70178ddd7f0aa831340ea060388ee4f319c7"};
  Digest identity{};
  std::string payload;
  CHECK(canonical(r, payload));
  CHECK(payload == "florabot.retry.v1\nESP32-A1B2C3\na1b2c3d4-e5f6-4a1b-8c2d-3e4f5a6b7c8d\nabcdef01-2345-4678-9abc-def012345678\n1800000030\n1800000060");
  CHECK(verify(r, r.hardware, key(), 1800000030, hmac, identity));
  CHECK(hex(identity) == "fd721badd2b45c686de98a41c02a70178ddd7f0aa831340ea060388ee4f319c7");

  // Uppercase UUID test with actual hex letters abcdef:
  RetryCommand upper = r;
  upper.request_id = "A1B2C3D4-E5F6-4A1B-8C2D-3E4F5A6B7C8D";
  upper.cmd_id = "ABCDEF01-2345-4678-9ABC-DEF012345678";
  Digest upperIdentity{};
  CHECK(verify(upper, r.hardware, key(), 1800000030, hmac, upperIdentity));
  CHECK(upperIdentity == identity);
}

void retryCooldown29And30() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));
  CHECK(storage.saved.phase == Phase::WaitingOpen && storage.saved.pulseCount == 1);
  CHECK(storage.saved.lastPulseAt == now);

  auto r29 = retryCommand(c.id, now + 29, now + 50); auto d29 = signedRetryIdentity(r29);
  CHECK(!journal.prepareRetry(r29, d29, true, now + 29));
  CHECK(storage.saved.phase == Phase::WaitingOpen && storage.saved.pulseCount == 1);

  // Signed at 29s, delivered at 30s: requested_at must be >= lastPulseAt + 30
  auto rSigned29 = retryCommand(c.id, now + 29, now + 55); auto dSigned29 = signedRetryIdentity(rSigned29);
  CHECK(!journal.prepareRetry(rSigned29, dSigned29, true, now + 30));
  CHECK(storage.saved.phase == Phase::WaitingOpen && storage.saved.pulseCount == 1);

  auto r30 = retryCommand(c.id, now + 30, now + 55); auto d30 = signedRetryIdentity(r30);
  auto malformed = r30;
  malformed.requested_at = std::numeric_limits<int64_t>::min();
  CHECK(!journal.prepareRetry(malformed, d30, true, now + 30));
  CHECK(journal.prepareRetry(r30, d30, true, now + 30));
  CHECK(storage.saved.phase == Phase::Prepared);
  CHECK(storage.saved.pulseCount == 1);

  CHECK(journal.beginPulse(now + 30));
  CHECK(storage.saved.phase == Phase::WaitingOpen);
  CHECK(storage.saved.pulseCount == 2);
  CHECK(storage.saved.lastPulseAt == now + 30);
}

void retryCountCap() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));
  auto r30 = retryCommand(c.id, now + 30, now + 55); auto d30 = signedRetryIdentity(r30);
  CHECK(journal.prepareRetry(r30, d30, true, now + 30));
  CHECK(journal.beginPulse(now + 30));
  CHECK(storage.saved.pulseCount == 2);

  auto r65 = retryCommand(c.id, now + 65, now + 90); auto d65 = signedRetryIdentity(r65);
  CHECK(!journal.prepareRetry(r65, d65, true, now + 65));
  CHECK(!journal.beginPulse(now + 65));
  CHECK(storage.saved.pulseCount == 2);
}

void retryOpenedWaitingCloseRejection() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));

  CHECK(journal.opened(id(2), now + 5));
  CHECK(storage.saved.phase == Phase::WaitingClose);

  auto r = retryCommand(c.id, now + 35, now + 60); auto d = signedRetryIdentity(r);
  CHECK(!journal.prepareRetry(r, d, true, now + 35));

  CHECK(journal.closed(id(3), now + 40));
  CHECK(storage.saved.phase == Phase::Idle);

  auto r2 = retryCommand(c.id, now + 45, now + 70); auto d2 = signedRetryIdentity(r2);
  CHECK(!journal.prepareRetry(r2, d2, true, now + 45));
}

void retryIdleCrossCmdSensorOpenRecoveryRejection() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);

  auto r = retryCommand(c.id, now, now + 30); auto d = signedRetryIdentity(r);
  CHECK(!journal.prepareRetry(r, d, true, now));

  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));

  auto rCross = retryCommand(id(999), now + 30, now + 55); auto dCross = signedRetryIdentity(rCross);
  CHECK(!journal.prepareRetry(rCross, dCross, true, now + 30));

  auto rValid = retryCommand(c.id, now + 30, now + 55); auto dValid = signedRetryIdentity(rValid);
  CHECK(!journal.prepareRetry(rValid, dValid, false, now + 30));

  journal.requireInspection();
  CHECK(journal.recoveryRequired());
  CHECK(!journal.prepareRetry(rValid, dValid, true, now + 30));
}

void retryExpiryAndClockRollback() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 50; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));

  auto rExpired = retryCommand(c.id, now + 50, now + 70); auto dExpired = signedRetryIdentity(rExpired);
  CHECK(!journal.prepareRetry(rExpired, dExpired, true, now + 50));
  auto rExpired2 = retryCommand(c.id, now + 51, now + 70); auto dExpired2 = signedRetryIdentity(rExpired2);
  CHECK(!journal.prepareRetry(rExpired2, dExpired2, true, now + 51));

  Storage storage2; auto journal2 = storage2.open();
  auto c2 = command(); c2.expires = now + 120; auto digest2 = signedIdentity(c2);
  CHECK(journal2.claim(c2, digest2, id(1), now + 10) == ClaimResult::Accepted);
  CHECK(journal2.beginPulse(now + 10));

  auto rRollback = retryCommand(c2.id, now + 5, now + 25); auto dRollback = signedRetryIdentity(rRollback);
  CHECK(!journal2.prepareRetry(rRollback, dRollback, true, now + 5));
}

void retryStorageFailureBeforeActuation() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));
  CHECK(storage.saved.phase == Phase::WaitingOpen && storage.saved.pulseCount == 1);

  storage.fail = true;
  auto r = retryCommand(c.id, now + 30, now + 55); auto d = signedRetryIdentity(r);
  CHECK(!journal.prepareRetry(r, d, true, now + 30));
  CHECK(!journal.healthy());
  CHECK(!journal.beginPulse(now + 30));
  CHECK(storage.saved.phase == Phase::WaitingOpen && storage.saved.pulseCount == 1);
}

void retryDuplicateAndReplay() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));

  auto r = retryCommand(c.id, now + 30, now + 55); auto d = signedRetryIdentity(r);
  CHECK(journal.prepareRetry(r, d, true, now + 30));
  CHECK(storage.saved.phase == Phase::Prepared);

  CHECK(!journal.prepareRetry(r, d, true, now + 30));

  CHECK(journal.beginPulse(now + 30));
  CHECK(storage.saved.phase == Phase::WaitingOpen && storage.saved.pulseCount == 2);

  auto rDup = retryCommand(c.id, now + 31, now + 55); auto dDup = signedRetryIdentity(rDup);
  CHECK(!journal.prepareRetry(rDup, dDup, true, now + 31));

  CHECK(journal.claim(c, digest, id(50), now + 31) == ClaimResult::Duplicate);
  CHECK(storage.saved.pulseCount == 2);
}

void reloadCounterAndInspection() {
  Storage storage; auto journal = storage.open();
  auto c = command(); c.expires = now + 120; auto digest = signedIdentity(c);
  CHECK(journal.claim(c, digest, id(1), now) == ClaimResult::Accepted);
  CHECK(journal.beginPulse(now));
  CHECK(storage.saved.pulseCount == 1 && storage.saved.lastPulseAt == now);

  auto reboot = storage.open();
  CHECK(reboot.recoveryRequired());
  CHECK(reboot.snapshot().pulseCount == 1);
  CHECK(reboot.snapshot().lastPulseAt == now);
  CHECK(!reboot.beginPulse(now + 30));

  auto r = retryCommand(c.id, now + 30, now + 55); auto d = signedRetryIdentity(r);
  CHECK(!reboot.prepareRetry(r, d, true, now + 30));

  CHECK(!reboot.clearRecoveryAfterInspection(false));
  CHECK(reboot.clearRecoveryAfterInspection(true));
  CHECK(!reboot.recoveryRequired());
  CHECK(reboot.snapshot().phase == Phase::Idle);

  CHECK(reboot.claim(c, digest, id(100), now + 30) == ClaimResult::Duplicate);
}

void backwardsV1SnapshotMigration() {
  Snapshot activeV1{};
  activeV1.version = 1;
  activeV1.clockFloor = now;
  activeV1.eventCount = 1;
  activeV1.active = 0;
  activeV1.phase = Phase::WaitingOpen;
  auto c = command(); auto digest = signedIdentity(c);
  copyId(activeV1.claims[0].id, c.id);
  activeV1.claims[0].identity = digest;
  activeV1.claims[0].expires = c.expires;
  copyId(activeV1.events[0].id, id(1));
  copyId(activeV1.events[0].command, c.id);
  activeV1.events[0].at = now;
  activeV1.events[0].kind = EventKind::Ack;

  const auto v1ActiveBytes = encodeV1Snapshot(activeV1, 1);
  CHECK(v1ActiveBytes.size() == 3815);

  Snapshot restored{};
  CHECK(decodeSnapshot(v1ActiveBytes.data(), v1ActiveBytes.size(), restored));
  CHECK(restored.version == 2);
  CHECK(restored.clockFloor == now);
  CHECK(restored.phase == Phase::WaitingOpen);
  CHECK(restored.eventCount == 1);
  CHECK(restored.claims[0].id == activeV1.claims[0].id);
  CHECK(restored.claims[0].identity == digest);
  CHECK(restored.events[0].id == activeV1.events[0].id);
  CHECK(restored.pulseCount == 2);
  CHECK(restored.lastPulseAt == now);

  const auto v2Encoded = encodeSnapshot(restored);
  CHECK(v2Encoded.size() == 3824);
  Snapshot v2Restored{};
  CHECK(decodeSnapshot(v2Encoded.data(), v2Encoded.size(), v2Restored));
  CHECK(v2Restored.version == 2);
  CHECK(v2Restored.pulseCount == 2);

  Snapshot idleV1{};
  idleV1.version = 1;
  idleV1.phase = Phase::Idle;
  const auto v1IdleBytes = encodeV1Snapshot(idleV1, 1);
  CHECK(decodeSnapshot(v1IdleBytes.data(), v1IdleBytes.size(), restored));
  CHECK(restored.pulseCount == 0);
  CHECK(restored.lastPulseAt == 0);

  const auto v0Bytes = encodeV1Snapshot(idleV1, 0);
  CHECK(!decodeSnapshot(v0Bytes.data(), v0Bytes.size(), restored));
  const auto v3Bytes = encodeV1Snapshot(idleV1, 3);
  CHECK(!decodeSnapshot(v3Bytes.data(), v3Bytes.size(), restored));

  auto corruptV1 = v1ActiveBytes;
  corruptV1[10] ^= 1;
  CHECK(!decodeSnapshot(corruptV1.data(), corruptV1.size(), restored));
}

void doorBankRetryIntegration() {
  Storage first, second; DoorBank bank;
  CHECK(bank.add(7, first.saved, [&](const Snapshot& s) { first.saved = s; return true; }));
  CHECK(bank.add(8, second.saved, [&](const Snapshot& s) { second.saved = s; return true; }));

  auto a = command(); a.relay = 7; a.expires = now + 120; auto digestA = signedIdentity(a);
  CHECK(bank.claim(a, digestA, id(1), now) == ClaimResult::Accepted);
  CHECK(bank.door(7)->beginPulse(now));

  auto r = retryCommand(a.id, now + 30, now + 55); auto d = signedRetryIdentity(r);
  CHECK(bank.prepareRetry(r, d, true, now + 30));
  CHECK(bank.door(7)->snapshot().phase == Phase::Prepared);
  CHECK(bank.door(7)->beginPulse(now + 30));
  CHECK(bank.door(7)->snapshot().pulseCount == 2);

  auto r2 = retryCommand(a.id, now + 65, now + 90); auto d2 = signedRetryIdentity(r2);
  CHECK(!bank.prepareRetry(r2, d2, true, now + 65));
}

void globalClockRollbackAndSensorRouting() {
  Storage first, second; DoorBank bank;
  CHECK(bank.add(7, first.saved, [&](const Snapshot& s) { first.saved = s; return true; }));
  CHECK(bank.add(8, second.saved, [&](const Snapshot& s) { second.saved = s; return true; }));

  auto a = command(); a.relay = 7; a.expires = now + 120; auto digestA = signedIdentity(a);
  CHECK(bank.claim(a, digestA, id(1), now) == ClaimResult::Accepted);
  CHECK(bank.door(7)->beginPulse(now));

  // Case 1: Wrong relay sensor routing.
  // Channel 7 sensor is open (false), even if channel 8 is closed (true):
  auto r = retryCommand(a.id, now + 30, now + 55); auto d = signedRetryIdentity(r);
  CHECK(!bank.prepareRetry(r, d, false, now + 30));
  CHECK(bank.door(7)->snapshot().phase == Phase::WaitingOpen);

  // When channel 7 sensor is confirmed closed (true):
  CHECK(bank.prepareRetry(r, d, true, now + 30));
  CHECK(bank.door(7)->snapshot().phase == Phase::Prepared);
  CHECK(bank.door(7)->beginPulse(now + 30));
  CHECK(bank.door(7)->snapshot().pulseCount == 2);

  // Case 2: Global clock rollback bypass across doors.
  // Reset Door 7 to WaitingOpen with new claim, and advance Door 8 to now + 100
  Storage s7, s8; DoorBank bank2;
  CHECK(bank2.add(7, s7.saved, [&](const Snapshot& s) { s7.saved = s; return true; }));
  CHECK(bank2.add(8, s8.saved, [&](const Snapshot& s) { s8.saved = s; return true; }));

  auto cmd7 = command(); cmd7.id = id(70); cmd7.relay = 7; cmd7.expires = now + 200; auto d7 = signedIdentity(cmd7);
  CHECK(bank2.claim(cmd7, d7, id(71), now) == ClaimResult::Accepted);
  CHECK(bank2.door(7)->beginPulse(now));

  // Door 8 advances the device clockFloor to now + 100
  auto cmd8 = command(); cmd8.id = id(80); cmd8.relay = 8; cmd8.issued = now + 100; cmd8.expires = now + 200;
  auto d8 = signedIdentity(cmd8);
  CHECK(bank2.claim(cmd8, d8, id(81), now + 100) == ClaimResult::Accepted);
  CHECK(bank2.door(8)->beginPulse(now + 100));
  CHECK(bank2.door(8)->snapshot().clockFloor == now + 100);

  // Attempt retry for Door 7 at now + 40 (< Door 8 clockFloor now + 100).
  // DoorBank must reject this global clock rollback attempt!
  auto rRollback = retryCommand(cmd7.id, now + 40, now + 65); auto dRollback = signedRetryIdentity(rRollback);
  CHECK(!bank2.prepareRetry(rRollback, dRollback, true, now + 40));
  CHECK(bank2.door(7)->snapshot().phase == Phase::WaitingOpen);
}

int main() {
  const std::vector<std::pair<const char*, void (*)()>> tests = {
    {"shared HMAC vector and field tampering", vectorAndFields}, {"canonical validation", canonicalValidation},
    {"event domain separation", eventDomain}, {"durable claim before pulse and replay", claimBeforePulseAndReplay},
    {"restart at each uncertain phase", crashWindows}, {"storage failures fail closed", storageFailure},
    {"sensor ordering and durable event queue", sensorOrderAndDurableEvents}, {"event capacity reserves full cycle", pendingQueueCapacity},
    {"unexpired claims and clock rollback", claimsCapacityAndClockRollback}, {"corruption and pulse expiry", corruptedStateAndExpiry},
    {"portable snapshot encoding and byte corruption", storageEncoding},
    {"parallel doors, fair events and global replay guard", parallelDoorsAndGlobalReplay},
    {"independent recovery and global storage failure", independentRecoveryAndGlobalStorageFailure},
    {"independent known retry HMAC vector from Python stdlib", independentKnownRetryHmacVector},
    {"retry HMAC vector, tampering, stale, and cross-hardware", retryHmacVectorTamperStaleCrossHardware},
    {"retry cooldown 29s reject and 30s accept", retryCooldown29And30},
    {"retry count cap at two total pulses", retryCountCap},
    {"opened door in WaitingClose rejects retry", retryOpenedWaitingCloseRejection},
    {"idle, cross-cmd, sensor open, and recovery reject retry", retryIdleCrossCmdSensorOpenRecoveryRejection},
    {"original expiry and clock rollback reject retry", retryExpiryAndClockRollback},
    {"storage failure rejects retry before actuation", retryStorageFailureBeforeActuation},
    {"duplicate retry and original claim replay prevention", retryDuplicateAndReplay},
    {"reload counter and inspection requirements", reloadCounterAndInspection},
    {"backwards V1 snapshot migration and corrupt rejection", backwardsV1SnapshotMigration},
    {"doorbank retry integration", doorBankRetryIntegration},
    {"global clock rollback bypass and wrong-relay sensor routing", globalClockRollbackAndSensorRouting}
  };
  for (const auto& test : tests) {
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
  }
  std::cout << tests.size() << " firmware core tests passed\n";
}
