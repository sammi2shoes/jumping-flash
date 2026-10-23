#include <Arduino.h>
#include <M5Unified.h>
#include <Bluepad32.h>
#include <driver/twai.h>
#include <cmath>
#include <cstring>

// RS05 CAN2.0 firmware 0.5.0.14; single unloaded motor bench controller.
constexpr uint8_t MOTOR = 1, HOST = 126;
constexpr uint32_t WATCHDOG = 20000; // Tested nominal one-second motor timeout.
constexpr uint32_t PAD_STALE_MS = 200, RETRY_MS = 1250;
constexpr float CURRENT = 1.0f, SPEED = 0.10f;
constexpr float TRAVEL = 5.0f * PI / 180.0f;
enum Phase { OFFLINE, CONFIGURING, WAIT_RELEASE, READY, ARMING, ACTIVE };
Phase phase = OFFLINE;
ControllerPtr pad = nullptr;
bool driverReady = false, cancelled = false, stopRequested = false, motorFault = false;
uint32_t padReport = 0, retryAt = 0, pollAt = 0, releasedAt = 0, displayAt = 0;
uint32_t padReports = 0, lastPadLog = 0;
uint32_t modeCount = 0;
uint8_t motorMode = 255, motorFlags = 0;
uint16_t pendingIndex = 0;
bool pending = false, readDone = false, readOK = false;
uint32_t readBits = 0;
float position = 0, target = 0, origin = 0;

uint32_t bitsOf(float f) { uint32_t bits; memcpy(&bits, &f, 4); return bits; }
float floatOf(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }
bool inputFresh() {
  return pad && pad->isConnected() && pad->isGamepad() && padReport != 0 && uint32_t(millis() - padReport) < PAD_STALE_MS;
}
bool held() { return inputFresh() && pad->r1(); }
bool centered() { return inputFresh() && abs(pad->axisX()) <= 60; }
void connected(ControllerPtr candidate) {
  // Bluepad32 can connect before the first report supplies the controller class.
  // Retain the handle now; require actual gamepad reports before accepting input.
  if (!pad) {
    pad = candidate; padReport = 0; padReports = 0;
    Serial.println("BT connected: waiting for gamepad reports. Release RB/R1.");
  }
}
void disconnected(ControllerPtr candidate) {
  if (candidate == pad) { pad = nullptr; padReport = 0; padReports = 0; Serial.println("Bluetooth disconnected."); }
}

void service() {
  M5.update();
  const bool updated = BP32.update();
  if (updated && pad && pad->isConnected() && pad->isGamepad() && pad->hasData()) {
    padReport = millis(); ++padReports;
  }
  if (M5.BtnA.wasPressed()) stopRequested = true;
  while (Serial.available()) { char c = Serial.read(); if (c == 'x' || c == 'X') stopRequested = true; }
  if ((phase == ARMING || phase == ACTIVE) &&
      (!held() || stopRequested || M5.BtnA.isPressed() || (phase == ARMING && !centered()))) cancelled = true;
  twai_message_t f;
  for (int n = 0; n < 64 && twai_receive(&f, 0) == ESP_OK; ++n) {
    if (!f.extd || f.rtr || f.data_length_code != 8 || ((f.identifier >> 8) & 255) != MOTOR) continue;
    const uint8_t type = (f.identifier >> 24) & 31;
    if (type == 2 || type == 24) {
      ++modeCount; motorMode = (f.identifier >> 22) & 3; motorFlags = (f.identifier >> 16) & 63;
      if (motorFlags) motorFault = true;
      if (phase == ACTIVE && motorMode != 2) cancelled = true;
    }
    if (type == 21) {
      for (int i = 0; i < 8; ++i) if (f.data[i]) motorFault = true;
    }
    if (type == 17 && (f.identifier & 255) == HOST && pending &&
        (uint16_t(f.data[0]) | (uint16_t(f.data[1]) << 8)) == pendingIndex) {
      readDone = true; readOK = ((f.identifier >> 16) & 255) == 0;
      memcpy(&readBits, f.data + 4, 4);
    }
  }
}

bool sendPacket(uint8_t type, const uint8_t* bytes = nullptr) {
  twai_message_t f = {};
  f.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
  f.extd = 1; f.ss = 1; f.data_length_code = 8;
  if (bytes) memcpy(f.data, bytes, 8);
  return twai_transmit(&f, pdMS_TO_TICKS(10)) == ESP_OK;
}

bool readValue(uint16_t index, uint32_t& bits) {
  service();
  if (cancelled || motorFault) return false;
  pendingIndex = index; pending = true; readDone = readOK = false;
  const uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  if (!sendPacket(17, d)) { pending = false; return false; }
  const uint32_t began = millis();
  while (!readDone && !cancelled && !motorFault && uint32_t(millis() - began) < 150) { service(); delay(1); }
  pending = false;
  if (!readDone || !readOK || cancelled || motorFault) { Serial.printf("Read failed 0x%04X\n", index); return false; }
  bits = readBits; return true;
}
bool writeChecked(uint16_t index, uint32_t bits, bool byteValue = false) {
  service();
  if (cancelled || motorFault) return false;
  uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  memcpy(d + 4, &bits, 4);
  uint32_t actual;
  return sendPacket(18, d) && readValue(index, actual) &&
    (byteValue ? (actual & 255) == (bits & 255) : actual == bits);
}
bool readPosition() {
  uint32_t bits;
  if (!readValue(0x7019, bits)) return false;
  position = floatOf(bits); return std::isfinite(position);
}
bool commandState(uint8_t type, uint8_t expected) {
  service();
  const uint32_t before = modeCount, began = millis();
  if (!sendPacket(type)) return false;
  while (modeCount == before && uint32_t(millis() - began) < 150) {
    service();
    if (type != 4 && (cancelled || motorFault)) return false;
    delay(1);
  }
  return modeCount != before && motorMode == expected;
}
bool stopMotor() {
  for (int i = 0; i < 2; ++i) if (commandState(4, 0)) return true;
  return false;
}

void loseControl(const char* reason) {
  phase = OFFLINE; pending = false; releasedAt = 0;
  Serial.printf("DISARMED: %s; old target discarded.\n", reason);
  const bool stopped = stopMotor();
  Serial.println(stopped ? "Disabled confirmed." : "STOP UNCONFIRMED: motor watchdog is fallback; use power cutoff if needed.");
  cancelled = stopRequested = false;
  // No CAN traffic during this interval: allow the motor watchdog to expire
  // even if only the feedback path was lost. Recovery starts with STOP.
  retryAt = millis() + RETRY_MS;
}

bool recoverDriver() {
  twai_status_info_t s = {};
  if (twai_get_status_info(&s) != ESP_OK) return false;
  if (s.state == TWAI_STATE_BUS_OFF) { twai_initiate_recovery(); return false; }
  if (s.state == TWAI_STATE_RECOVERING) return false;
  if (s.state == TWAI_STATE_STOPPED) return twai_start() == ESP_OK;
  return s.state == TWAI_STATE_RUNNING;
}

bool configureDisabled() {
  phase = CONFIGURING; cancelled = motorFault = false;
  if (!stopMotor() || motorFlags || motorFault) return false;
  // Turn off the reporting left enabled by the observer test. Normal target
  // acknowledgements and standard position reads monitor this controller's link.
  const uint8_t reportingOff[8] = {1, 2, 3, 4, 5, 6, 0, 8};
  if (!sendPacket(24, reportingOff) ||
      !writeChecked(0x7028, WATCHDOG) ||
      !writeChecked(0x7018, bitsOf(CURRENT)) ||
      !writeChecked(0x7005, 5, true) ||
      !writeChecked(0x7017, bitsOf(SPEED)) || !readPosition()) return false;
  target = origin = position;
  if (!writeChecked(0x7016, bitsOf(target))) return false;
  phase = WAIT_RELEASE; releasedAt = 0; pollAt = millis();
  Serial.println("CAN ready / motor disabled. Center left stick; release RB/R1, then press and hold it.");
  return true;
}

bool armMotor() {
  phase = ARMING; cancelled = false;
  if (!held() || !centered() || motorFault || !readPosition()) return false;
  target = origin = position;
  if (!writeChecked(0x7016, bitsOf(target)) || !commandState(3, 2) ||
      !writeChecked(0x7018, bitsOf(CURRENT)) || !writeChecked(0x7017, bitsOf(SPEED))) return false;
  uint32_t timeout;
  if (!readValue(0x7028, timeout) || timeout != WATCHDOG || !readPosition()) return false;
  target = origin = position;
  if (!writeChecked(0x7016, bitsOf(target)) || !held() || cancelled || motorFault) return false;
  phase = ACTIVE; pollAt = millis();
  Serial.println("ARMED: RB/R1 hold + left X; bench travel +/-5 deg at 0.10 rad/s, 1 A.");
  return true;
}

void setup() {
  auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.setTextSize(2);
  BP32.setup(&connected, &disconnected); BP32.enableVirtualDevice(false);
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64; g.tx_queue_len = 0; // No software queue of stale targets.
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e = twai_driver_install(&g, &t, &f);
  if (e == ESP_OK) e = twai_start();
  driverReady = e == ESP_OK; retryAt = millis() + 1500;
  Serial.printf("RS05 recovery bench v0.2 CAN=%s. No enable on boot.\n", esp_err_to_name(e));
  Serial.println("RB/R1=hold enable; left X=slow motion. Button A on Stick or serial x=stop. No spinner output.");
}

void loop() {
  service();
  const uint32_t now = millis();
  if (driverReady) {
    if (stopRequested || ((phase == ACTIVE || phase == ARMING) && (cancelled || motorFault || !held()))) {
      loseControl("stop, released enable, controller loss/stale data, or motor state change");
    } else if (phase == OFFLINE && int32_t(now - retryAt) >= 0) {
      if (!recoverDriver()) retryAt = millis() + RETRY_MS;
      else if (!configureDisabled()) loseControl("CAN/configuration not verified");
    } else if (phase == WAIT_RELEASE || phase == READY) {
      if (uint32_t(now - pollAt) >= 100) {
        pollAt = now;
        if (!readPosition()) { loseControl("CAN feedback lost while disabled"); return; }
      }
      if (!inputFresh() || M5.BtnA.isPressed()) { phase = WAIT_RELEASE; releasedAt = 0; }
      else if (phase == WAIT_RELEASE) {
        if (!held() && centered()) {
          if (!releasedAt) releasedAt = now;
          if (uint32_t(now - releasedAt) >= 100) { phase = READY; Serial.println("Ready. Press/hold RB/R1 with left stick centered."); }
        } else releasedAt = 0;
      } else if (held()) {
        if (!centered()) { phase = WAIT_RELEASE; releasedAt = 0; Serial.println("Center stick and release RB/R1 before enabling."); }
        else if (!armMotor()) loseControl("arming interrupted or unverified");
      }
    } else if (phase == ACTIVE && uint32_t(now - pollAt) >= 50) {
      float dt = float(uint32_t(now - pollAt)) / 1000.0f;
      if (dt > .05f) dt = .05f;
      pollAt = now;
      if (!readPosition()) { loseControl("CAN feedback lost"); return; }
      if (fabsf(position - origin) > TRAVEL + .035f) { loseControl("measured bench travel exceeded"); return; }
      const int axis = pad && inputFresh() ? pad->axisX() : 0;
      const float rate = abs(axis) <= 60 ? 0.0f : float(axis) / 512.0f;
      target += rate * SPEED * dt;
      if (target > origin + TRAVEL) target = origin + TRAVEL;
      if (target < origin - TRAVEL) target = origin - TRAVEL;
      if (!writeChecked(0x7016, bitsOf(target))) loseControl("CAN target acknowledgement lost");
    }
  }
  if (uint32_t(millis() - lastPadLog) >= 1000) {
    lastPadLog = millis();
    const bool linked = pad && pad->isConnected();
    const bool gamepad = linked && pad->isGamepad();
    Serial.printf("PAD link=%u gamepad=%u fresh=%u reports=%lu age_ms=%ld leftX=%d RB_R1=%u phase=%u\n",
      unsigned(linked), unsigned(gamepad), unsigned(inputFresh()), (unsigned long)padReports,
      padReport ? long(uint32_t(millis() - padReport)) : -1L,
      gamepad ? pad->axisX() : 0, unsigned(gamepad && pad->r1()), unsigned(phase));
  }
  if (uint32_t(now - displayAt) >= 250) {
    displayAt = now;
    const char* names[] = {"CAN OFFLINE", "CONFIGURING", "RELEASE RB/R1", "READY", "ARMING", "ACTIVE"};
    M5.Display.setCursor(0, 0);
    const char* padState = !pad || !pad->isConnected() ? "NO LINK" : !pad->isGamepad() ? "WAIT TYPE" : !padReport ? "NO REPORTS" : inputFresh() ? "FRESH" : "STALE";
    M5.Display.printf("%-16s\nPAD: %-10s\n1A / +/-5 deg\nRB/R1: enable\nLeft X: move\nA / x: STOP\n", names[phase], padState);
  }
  delay(1);
}
