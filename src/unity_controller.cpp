#if defined(UNITY_CONTROLLER)
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_BNO055.h>
#include <NeoPixelBus.h>
#include <math.h>
#include <atomic>
#include <Preferences.h>
#include "controller_pairing.h"
#include "unity_protocol.h"
#include "controller_axes.h"

#ifndef KJ_GROUP
#define KJ_GROUP 0
#endif
#ifndef KJ_SLOT
#define KJ_SLOT 0
#endif
#ifndef KJ_UDP_PORT
#define KJ_UDP_PORT 42042
#endif
#ifndef KJ_GUARD_PIN
#define KJ_GUARD_PIN 1
#endif
#ifndef KJ_RECENTER_PIN
#define KJ_RECENTER_PIN 0
#endif
#ifndef KJ_BUTTON_ACTIVE_LOW
#define KJ_BUTTON_ACTIVE_LOW 1
#endif
#ifndef KJ_SDA_PIN
#define KJ_SDA_PIN 5
#endif
#ifndef KJ_SCL_PIN
#define KJ_SCL_PIN 4
#endif
#ifndef KJ_IMU_RESET_PIN
#define KJ_IMU_RESET_PIN -1
#endif
#ifndef KJ_CONNECTED_LED_PIN
#define KJ_CONNECTED_LED_PIN 3
#endif
#ifndef KJ_DISCONNECTED_LED_PIN
#define KJ_DISCONNECTED_LED_PIN 2
#endif
#ifndef KJ_MOTOR_PIN
#define KJ_MOTOR_PIN 7
#endif
#ifndef KJ_MOTOR_DUTY
#define KJ_MOTOR_DUTY 128
#endif
#ifndef KJ_RGB_PIN
#define KJ_RGB_PIN 20
#endif

static_assert(KJ_UDP_PORT > 0 && KJ_UDP_PORT < 65535, "UDP port must be 1..65534");

#include "kj_wifi_config.h"

namespace {
bool wireReady = false;
bool networkReady = false;
bool otaStarted = false;
uint32_t lastWifiAttemptAt = 0;
std::atomic<uint8_t> lastWifiDisconnectReason{0};
IPAddress connectedAddress;
WiFiUDP controllerUdp;
kojo::PairingClient pairing;
kojo::PairingButtons pairingButtons;
Preferences preferences;
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> rgb(1, KJ_RGB_PIN);
uint64_t nonce() { return (static_cast<uint64_t>(esp_random()) << 32) | esp_random(); }
void savePairing() {
  if (!pairing.changed) return;
  kojo::seal(pairing.binding);
  preferences.putBytes("binding", &pairing.binding, sizeof(pairing.binding));
  preferences.putUInt("ip", pairing.hostIp);
  rgb.SetPixelColor(0, RgbColor(pairing.binding.red, pairing.binding.green, pairing.binding.blue));
  rgb.Show();
  pairing.changed = false;
}
uint32_t vibrationUntil = 0, feedbackCommands = 0;
uint16_t lastVibrationMs = 0;
bool motorRunning = false;
void startVibration(uint16_t duration, uint32_t now) {
  if (duration == 0) return;
  ++feedbackCommands;
  lastVibrationMs = duration;
  vibrationUntil = now + duration;
}
void serviceVibration(uint32_t now) {
  const bool running = static_cast<int32_t>(vibrationUntil - now) > 0;
  if (running == motorRunning) return;
  motorRunning = running;
  analogWrite(KJ_MOTOR_PIN, running ? KJ_MOTOR_DUTY : 0);
}
void beginWifi() {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE)
      lastWifiDisconnectReason.store(info.wifi_sta_disconnected.reason, std::memory_order_relaxed);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiAttemptAt = millis();
}
void serviceNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    if (networkReady) controllerUdp.stop();
    if (otaStarted) {
      ArduinoOTA.end();
      otaStarted = false;
    }
    networkReady = false;
    // AutoReconnect does not retry every initial association/auth failure.
    // A hotspot enabled after controller startup must also become usable.
    const uint32_t now = millis();
    if (now - lastWifiAttemptAt >= 10000) {
      lastWifiAttemptAt = now;
      WiFi.reconnect();
    }
    return;
  }
  const IPAddress address = WiFi.localIP();
  if (!networkReady || address != connectedAddress) {
    if (otaStarted && address != connectedAddress) {
      ArduinoOTA.end();
      otaStarted = false;
    }
    controllerUdp.stop();
    networkReady = controllerUdp.begin(KJ_UDP_PORT + 1) == 1;
    connectedAddress = address;
  }
  if (!otaStarted) {
    ArduinoOTA.begin();
    otaStarted = true;
  }
  ArduinoOTA.handle();
}
void sendPc(const kojo::Packet& packet) {
  if (!networkReady || !pairing.bound() || !pairing.hostIp) return;
  if (controllerUdp.beginPacket(IPAddress(pairing.hostIp), KJ_UDP_PORT) != 1) return;
  kojo::Envelope<kojo::Packet> frame;
  frame.device = pairing.device; frame.session = pairing.binding.session; frame.payload = packet;
  kojo::seal(frame);
  controllerUdp.write(reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
  controllerUdp.endPacket();
}
void sendUsb(const kojo::Packet& packet) {
  if (!pairing.bound()) return;
  kojo::Envelope<kojo::Packet> frame;
  frame.device = pairing.device; frame.session = pairing.binding.session; frame.payload = packet;
  kojo::seal(frame);
  if (Serial && Serial.availableForWrite() >= static_cast<int>(sizeof(frame)))
    Serial.write(reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
}

// A one-shot USB status request helps distinguish Wi-Fi association failures
// from APs that do not forward broadcast. Unity's stream decoder skips text.
void serviceUsbStatus();

static_assert(KJ_GROUP >= 0 && KJ_GROUP < 3, "group must be 0..2");
static_assert(KJ_SLOT >= 0 && KJ_SLOT < 2, "slot must be 0..1");
// An OTA restart resets the host, but may leave the sensor running.
// Close the old SH-2 session and request its channel map if startup data is gone.
class ControllerImu final : public Adafruit_BNO08x {
public:
  using Adafruit_BNO08x::Adafruit_BNO08x;
protected:
  bool _init(int32_t sensorId) override {
    if (sessionOpen) sh2_close();
    sessionOpen = true;
    if (Adafruit_BNO08x::_init(sensorId)) return true;
    sh2_close();
    originalOpen = _HAL.open;
    originalWrite = _HAL.write;
    _HAL.open = recoverOpen;
    const bool ready = Adafruit_BNO08x::_init(sensorId);
    _HAL.open = originalOpen;
    return ready;
  }
private:
  bool sessionOpen = false;
  static int (*originalOpen)(sh2_Hal_t*);
  static int (*originalWrite)(sh2_Hal_t*, uint8_t*, unsigned);
  static int recoverOpen(sh2_Hal_t* hal) {
    const int result = originalOpen(hal);
    if (result != 0) return result;
    uint8_t advertisement[] = {6, 0, 0, 0, 0, 1};
    return originalWrite(hal, advertisement, sizeof(advertisement)) == sizeof(advertisement) ? 0 : -1;
  }
};
int (*ControllerImu::originalOpen)(sh2_Hal_t*) = nullptr;
int (*ControllerImu::originalWrite)(sh2_Hal_t*, uint8_t*, unsigned) = nullptr;
ControllerImu sensor(KJ_IMU_RESET_PIN);
Adafruit_BNO055 sensor055a(55, 0x29, &Wire), sensor055b(55, 0x28, &Wire);
Adafruit_BNO055* sensor055 = nullptr;
uint32_t sampled055At = 0;
kojo::Packet packet;
bool sensorReady = false;
bool previousRecenter = false;
bool guard = false, guardCandidate = false, recenter = false, recenterCandidate = false;
uint32_t guardChangedAt = 0, recenterChangedAt = 0;
uint32_t recenterPressedAt = 0;
uint32_t poseAt = 0, accelerationAt = 0, lastProbeAt = 0;
constexpr uint32_t SendIntervalUs = 10000; // 100 Hz.
uint32_t lastSentAtUs = 0;
uint32_t lastReportRequestAt = 0, lastAccelerationRequestAt = 0, sensorEventCount = 0;
bool havePose = false, poseSeen = false, haveAcceleration = false, accelerationRequested = false;
bool haveGameAck = false;
uint32_t gameAckAt = 0;

void updateConnectionLed(uint32_t now) {
  for (int count = 0; networkReady && count < 8; ++count) {
    const int length = controllerUdp.parsePacket();
    if (length <= 0) break;
    const uint32_t sender = static_cast<uint32_t>(controllerUdp.remoteIP());
    if (controllerUdp.remotePort() != KJ_UDP_PORT) { controllerUdp.flush(); continue; }
    if (length == sizeof(kojo::PairFrame)) {
      kojo::PairFrame frame;
      if (controllerUdp.read(reinterpret_cast<uint8_t*>(&frame), sizeof(frame)) == sizeof(frame)) {
        if (pairing.receive(frame, sender, now)) {
          haveGameAck = false;
          packet.slot = pairing.binding.slot; packet.group = pairing.binding.group;
        }
        savePairing();
      }
      continue;
    }
    kojo::Envelope<kojo::GameAcknowledgement> frame;
    if (sender == pairing.hostIp && length == sizeof(frame)
        && controllerUdp.read(reinterpret_cast<uint8_t*>(&frame), sizeof(frame)) == sizeof(frame)
        && kojo::valid(frame, pairing.device, pairing.binding.session) && kojo::valid(frame.payload, packet)) {
      haveGameAck = true;
      gameAckAt = now;
      startVibration(frame.payload.vibrationMs, now);
    } else controllerUdp.flush();
  }
  const bool connected = (networkReady || Serial) && haveGameAck && now - gameAckAt < 1200;
  if (networkReady) {
    kojo::PairFrame frame; uint32_t destination;
    if (pairing.next(now, connected, nonce(), frame, destination)) {
      if (controllerUdp.beginPacket(destination ? IPAddress(destination) : WiFi.broadcastIP(), KJ_UDP_PORT) == 1) {
        controllerUdp.write(reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
        controllerUdp.endPacket();
      }
    }
  }
  digitalWrite(KJ_CONNECTED_LED_PIN, connected ? HIGH : LOW);
  digitalWrite(KJ_DISCONNECTED_LED_PIN, connected ? LOW : HIGH);
}


// The USB path uses the same CRC-protected 16-byte feedback frame as Wi-Fi.
void serviceUsbStatus() {
  static uint8_t pending[sizeof(kojo::Envelope<kojo::GameAcknowledgement>)];
  static size_t used = 0;
  for (int count = 0; Serial && Serial.available() && count < 64; ++count) {
    const uint8_t byte = static_cast<uint8_t>(Serial.read());
    if (used == 0 && byte == '?') {
      const uint32_t now = millis();
      const long ackAge = haveGameAck ? static_cast<long>(now - gameAckAt) : -1;
      Serial.printf("\nKJ_STATUS wifi=%d reason=%u ip=%s udp=%d slot=%d imu=%s feedback=%lu lastVibration=%u ssid=%s rssi=%d host=%s paired=%d pairState=%d conflict=%d ackAgeMs=%ld\n",
        static_cast<int>(WiFi.status()), lastWifiDisconnectReason.load(std::memory_order_relaxed),
        WiFi.localIP().toString().c_str(), networkReady ? 1 : 0, packet.slot,
        sensor055 != nullptr ? "BNO055" : sensorReady ? "BNO08x" : "none",
        static_cast<unsigned long>(feedbackCommands), lastVibrationMs,
        WiFi.SSID().c_str(), static_cast<int>(WiFi.RSSI()), IPAddress(pairing.hostIp).toString().c_str(),
        pairing.bound() ? 1 : 0, static_cast<int>(pairing.state), pairing.conflict ? 1 : 0, ackAge);
      continue;
    }
    if (used == 0 && byte != 0x4b) continue;
    if (used == 1 && byte != 0x44) { used = byte == 0x4b ? 1 : 0; continue; }
    pending[used++] = byte;
    if (used == sizeof(pending)) {
      kojo::Envelope<kojo::GameAcknowledgement> frame;
      memcpy(&frame, pending, sizeof(frame));
      used = 0;
      if (kojo::valid(frame, pairing.device, pairing.binding.session) && kojo::valid(frame.payload, packet)) {
        haveGameAck = true;
        gameAckAt = millis();
        startVibration(frame.payload.vibrationMs, gameAckAt);
      }
    }
  }
}

void onSensorEvent(void*, sh2_SensorEvent_t* event) {
  ++sensorEventCount;
  sh2_SensorValue_t value = {};
  if (sh2_decodeSensorEvent(&value, event) != SH2_OK) return;
  const uint32_t now = millis();
  if (value.sensorId == SH2_ARVR_STABILIZED_RV) {
    const auto& q = value.un.arvrStabilizedRV;
    const float norm = q.i*q.i + q.j*q.j + q.k*q.k + q.real*q.real;
    havePose = isfinite(norm) && norm > 0.25f && norm < 2.25f;
    if (!havePose) return;
    poseSeen = true;
    const float inverseNorm = 1.0f / sqrtf(norm);
    kojo::controllerRotation(q.i, q.j, q.k, q.real, inverseNorm, packet.rotation);
    poseAt = now;
  } else if (value.sensorId == SH2_LINEAR_ACCELERATION) {
    const auto& a = value.un.linearAcceleration;
    haveAcceleration = isfinite(a.x) && isfinite(a.y) && isfinite(a.z);
    if (!haveAcceleration) return;
    kojo::controllerAcceleration(a.x, a.y, a.z, packet.linearAcceleration);
    accelerationAt = now;
  }
}

void reports() {
  // Configure pose first; the callback handles every report in a shared packet.
  sensor.enableReport(SH2_ARVR_STABILIZED_RV, 5000);
  lastReportRequestAt = millis();
}
void probeSensor() {
  havePose = poseSeen = haveAcceleration = accelerationRequested = false;
  lastAccelerationRequestAt = 0;
  sensor055 = nullptr;
  if (wireReady) {
    for (Adafruit_BNO055* candidate : {&sensor055a, &sensor055b}) {
      if (candidate->begin()) { sensor055 = candidate; sensor055->setExtCrystalUse(true); break; }
    }
  }
  sensorReady = sensor055 != nullptr || (wireReady && (sensor.begin_I2C(0x4b, &Wire) || sensor.begin_I2C(0x4a, &Wire)));
  if (sensorReady && sensor055 == nullptr) {
    sh2_setSensorCallback(onSensorEvent, nullptr);
    sensor.wasReset();
    reports();
  }
  lastProbeAt = millis();
}
bool button(int pin) { return digitalRead(pin) == (KJ_BUTTON_ACTIVE_LOW ? LOW : HIGH); }
void debounce(bool raw, bool& candidate, bool& stable, uint32_t& changedAt, uint32_t now) {
  if (candidate != raw) { candidate = raw; changedAt = now; }
  if (now - changedAt >= 15) stable = candidate;
}
void readSensors(uint32_t now) {
  if (!sensorReady) {
    if (now - lastProbeAt >= 10000) probeSensor();
    return;
  }
  if (sensor055 != nullptr) {
    if (now - sampled055At < 10) return;
    sampled055At = now;
    const auto q = sensor055->getQuat();
    const float norm = q.x()*q.x() + q.y()*q.y() + q.z()*q.z() + q.w()*q.w();
    havePose = isfinite(norm) && norm > .25f && norm < 2.25f;
    if (havePose) {
      kojo::controllerRotation(q.x(), q.y(), q.z(), q.w(), 1.f / sqrtf(norm), packet.rotation);
      poseAt = now;
    }
    const auto a = sensor055->getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    haveAcceleration = isfinite(a.x()) && isfinite(a.y()) && isfinite(a.z());
    if (haveAcceleration) {
      kojo::controllerAcceleration(a.x(), a.y(), a.z(), packet.linearAcceleration);
      accelerationAt = now;
    }
    return;
  }
  if (sensor.wasReset()) {
    havePose = poseSeen = haveAcceleration = accelerationRequested = false;
    lastAccelerationRequestAt = 0;
    reports();
    lastProbeAt = millis();
    now = lastProbeAt;
  }
  // A service call can deliver several reports; preserve each in the callback.
  for (int count = 0; count < 12; ++count) {
    const uint32_t before = sensorEventCount;
    sh2_service();
    if (sensorEventCount == before) break;
  }
  now = millis();
  if (havePose && now - poseAt < 150 &&
      now - lastReportRequestAt >= 100 &&
      (!accelerationRequested || !haveAcceleration || now - accelerationAt >= 100) &&
      (lastAccelerationRequestAt == 0 || now - lastAccelerationRequestAt >= 3000)) {
    accelerationRequested = sensor.enableReport(SH2_LINEAR_ACCELERATION, 10000);
    lastAccelerationRequestAt = millis();
  }
  // A short gap only needs the report requested again. Give the sensor time
  // to recover before reopening SH-2; repeated warm resets can prolong a stall.
  const uint32_t quietSince = poseSeen ? poseAt : lastProbeAt;
  if (now - quietSince >= 10000 && now - lastProbeAt >= 10000) probeSensor();
  else if (now - quietSince >= 1000 && now - lastReportRequestAt >= 3000) reports();
}
}

void setup() {
  // Match the sensor startup timing verified by the gyro_test target.
  delay(300);
  Serial.begin(115200);
  pinMode(KJ_GUARD_PIN, KJ_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(KJ_RECENTER_PIN, KJ_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(KJ_DISCONNECTED_LED_PIN, OUTPUT);
  pinMode(KJ_CONNECTED_LED_PIN, OUTPUT);
  pinMode(KJ_MOTOR_PIN, OUTPUT);
  analogWriteFrequency(20000);
  analogWrite(KJ_MOTOR_PIN, 0);
  rgb.Begin();
  preferences.begin("kj-pairing", false);
  pairing.device = ESP.getEfuseMac();
  if (preferences.getBytesLength("binding") == sizeof(pairing.binding)) {
    preferences.getBytes("binding", &pairing.binding, sizeof(pairing.binding));
    if (!kojo::valid(pairing.binding) || pairing.binding.device != pairing.device) pairing.binding = kojo::PairFrame{};
  }
  pairing.hostIp = preferences.getUInt("ip", 0);
  rgb.SetPixelColor(0, pairing.bound() ? RgbColor(pairing.binding.red, pairing.binding.green, pairing.binding.blue) : RgbColor(0, 0, 0));
  rgb.Show();
  digitalWrite(KJ_DISCONNECTED_LED_PIN, HIGH);
  digitalWrite(KJ_CONNECTED_LED_PIN, LOW);
  wireReady = Wire.begin(KJ_SDA_PIN, KJ_SCL_PIN);
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  packet.group = pairing.bound() ? pairing.binding.group : 0;
  packet.slot = pairing.bound() ? pairing.binding.slot : 0;
  probeSensor();
  beginWifi();
  lastSentAtUs = micros();
  ArduinoOTA.setPassword("admin");  // platformio.iniのOTA設定と合わせる
}

void loop() {
  serviceNetwork();
  serviceUsbStatus();
  readSensors(millis());
  const uint32_t now = millis();
  updateConnectionLed(now);
  serviceVibration(now);
  debounce(button(KJ_GUARD_PIN), guardCandidate, guard, guardChangedAt, now);
  debounce(button(KJ_RECENTER_PIN), recenterCandidate, recenter, recenterChangedAt, now);
  const bool chordWasMasked = pairingButtons.masked;
  if (pairingButtons.update(guard, recenter, now)) pairing.begin(nonce(), now);
  if (recenter && !previousRecenter) recenterPressedAt = now;
  // Unity opens pause after 500 ms; only a shorter release recenters the sword.
  if (!chordWasMasked && !pairingButtons.masked && !guard && !recenter && previousRecenter && now - recenterPressedAt < 500) ++packet.recenterSequence;
  previousRecenter = recenter;
  const uint32_t nowUs = micros();
  if (nowUs - lastSentAtUs >= SendIntervalUs) {
    lastSentAtUs += SendIntervalUs;
    // Preserve the phase through normal loop jitter, but never catch up in bursts.
    if (nowUs - lastSentAtUs >= SendIntervalUs) lastSentAtUs = nowUs;
    packet.flags = guard && !pairingButtons.masked ? kojo::Guard : 0;
    if (recenter && !guard && !pairingButtons.masked) packet.flags |= kojo::RecenterHeld;
    if (havePose && now - poseAt < 150) packet.flags |= kojo::OrientationValid;
    if (haveAcceleration && now - accelerationAt < 100) packet.flags |= kojo::AccelerationValid;
    packet.timestampMs = now;
    ++packet.sequence;
    kojo::seal(packet);
    sendUsb(packet);
    sendPc(packet);
  }
  delay(1);
}

#endif
