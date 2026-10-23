#include <Arduino.h>
#include <M5Unified.h>
#include <Bluepad32.h>
#include <driver/twai.h>
#include <cmath>
#include <cstring>

// RS05 CAN2.0 firmware 0.5.0.14; single-leg prototype position hold.
constexpr uint8_t MOTOR = 1, HOST = 126;
constexpr uint32_t WATCHDOG = 20000; // Tested nominal one-second motor timeout.
constexpr uint32_t PAD_STALE_MS = 200, RETRY_MS = 1250;
constexpr float CURRENT = 1.0f, SPEED = 1.0f;
constexpr float ACCELERATION = 5.0f; // Host target-rate ramp, rad/s^2.
constexpr float EXTEND_SIGN = -1.0f; // Extension decreases encoder position.
constexpr float TRAVEL = 80.0f * PI / 180.0f;
enum Phase { OFFLINE, CONFIGURING, WAIT_RELEASE, READY, ARMING, ACTIVE, NEED_REFERENCE };
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
float position = 0, target = 0, origin = 0, targetRate = 0;
bool referenceCaptured = false, referenceValid = false;
bool referenceButtonReleased = false;
uint32_t referenceHoldAt = 0, signatureAt = 0;
void invalidateReference() {
  if (referenceValid) Serial.println("REFERENCE INVALID: motor/CAN state uncertain. Restart and manually reference at retracted pose.");
  referenceValid = false;
}
bool withinReference() {
  const float travel = EXTEND_SIGN * (position - origin);
  return referenceCaptured && referenceValid && travel >= -.002f && travel <= TRAVEL + .002f;
}

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
      (!referenceValid || !held() || stopRequested || M5.BtnA.isPressed() || (phase == ARMING && !centered()))) cancelled = true;
  twai_message_t f;
  for (int n = 0; n < 64 && twai_receive(&f, 0) == ESP_OK; ++n) {
    if (!f.extd || f.rtr || f.data_length_code != 8 || ((f.identifier >> 8) & 255) != MOTOR) continue;
    const uint8_t type = (f.identifier >> 24) & 31;
    // An identification broadcast after reference capture can indicate restart.
    // Conservatively invalidate rather than infer that the old coordinate survived.
    if (type == 0 && referenceCaptured) invalidateReference();
    if ((type == 2 || type == 24) && (f.identifier & 255) == HOST) {
      ++modeCount; motorMode = (f.identifier >> 22) & 3; motorFlags = (f.identifier >> 16) & 63;
      if (motorFlags) motorFault = true;
      if (phase == ACTIVE && motorMode != 2) { cancelled = true; invalidateReference(); }
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
  // With no TX queue, the driver rejects another frame while its mailbox is
  // occupied. Wait for the preceding frame, bounded well below the watchdog.
  const uint32_t began = millis();
  twai_status_info_t status = {};
  for (;;) {
    const esp_err_t result = twai_get_status_info(&status);
    if (result != ESP_OK || status.state != TWAI_STATE_RUNNING) {
      Serial.printf("TX type=%u blocked: driver=%s state=%u\n", type, esp_err_to_name(result), unsigned(status.state));
      return false;
    }
    if (!status.msgs_to_tx) break;
    if (uint32_t(millis() - began) >= 20) {
      Serial.printf("TX type=%u blocked: mailbox busy; tx_err=%lu rx_err=%lu\n", type,
        (unsigned long)status.tx_error_counter, (unsigned long)status.rx_error_counter);
      return false;
    }
    service();
    if (type != 4 && (cancelled || motorFault || stopRequested)) return false;
    delay(1);
  }
  service();
  if (type != 4 && (cancelled || motorFault || stopRequested)) return false;
  const esp_err_t result = twai_transmit(&f, pdMS_TO_TICKS(10));
  if (result != ESP_OK) Serial.printf("TX type=%u failed: %s\n", type, esp_err_to_name(result));
  return result == ESP_OK;
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
  if (!sendPacket(18, d) || !readValue(index, actual)) {
    Serial.printf("WRITE/VERIFY failed index=0x%04X\n", index); return false;
  }
  bool matches = byteValue ? (actual & 255) == (bits & 255) : actual == bits;
  // Position-target readback can differ slightly from the requested float.
  // Only this parameter permits a bounded absolute error (about 0.0115 deg).
  if (index == 0x7016 && !byteValue) {
    const float wanted = floatOf(bits), returned = floatOf(actual);
    matches = std::isfinite(wanted) && std::isfinite(returned) &&
      fabsf(returned - wanted) <= 0.0002f;
  }
  if (!matches) Serial.printf("VERIFY mismatch index=0x%04X wanted=%08lX got=%08lX\n",
    index, (unsigned long)bits, (unsigned long)actual);
  return matches;
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

void loseControl(const char* reason, bool uncertain = false) {
  phase = OFFLINE; pending = false; releasedAt = 0; targetRate = 0; referenceHoldAt = 0;
  Serial.printf("DISARMED: %s; old target discarded.\n", reason);
  const bool stopped = stopMotor();
  Serial.println(stopped ? "Disabled confirmed." : "STOP UNCONFIRMED: motor watchdog is fallback; use power cutoff if needed.");
  if (uncertain || !stopped || motorFault) invalidateReference();
  cancelled = stopRequested = false;
  if (stopped && !uncertain && !motorFault) {
    phase = referenceCaptured && referenceValid ? WAIT_RELEASE : NEED_REFERENCE;
    pollAt = millis();
    return; // Confirmed disabled: keep monitoring CAN even without Bluetooth.
  }
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
  target = position;
  if (!writeChecked(0x7016, bitsOf(target))) return false;
  phase = referenceCaptured && referenceValid ? WAIT_RELEASE : NEED_REFERENCE;
  releasedAt = 0; referenceHoldAt = 0; referenceButtonReleased = false; pollAt = millis();
  Serial.println("CAN ready / disabled. Reference required: position fully retracted; release then hold Stick B for 2 seconds.");
  return true;
}

bool captureReference() {
  // This is a manual pose declaration, not automatic homing or a motor-zero command.
  if (referenceCaptured || !inputFresh() || !centered() || held() || !M5.BtnB.isPressed()) return false;
  if (!stopMotor() || motorFlags || motorFault || !readPosition()) return false;
  const float first = position;
  const uint32_t began = millis();
  while (uint32_t(millis() - began) < 100) {
    service();
    if (stopRequested || !inputFresh() || !centered() || held() || !M5.BtnB.isPressed()) return false;
    delay(1);
  }
  if (!readPosition() || fabsf(position - first) > .002f || stopRequested || !inputFresh() || !centered() || held() || !M5.BtnB.isPressed()) {
    Serial.println("Reference rejected: leg must be stationary at the known retracted pose."); return false;
  }
  origin = position; referenceCaptured = referenceValid = true;
  target = position; targetRate = 0; releasedAt = 0; phase = WAIT_RELEASE;
  Serial.printf("ZERO CAPTURED encoder=%.6f rad. Fixed 0..80 deg limits; hold R1 for active position hold.\n", origin);
  return true;
}

bool settingsIntact() {
  uint32_t value;
  const uint16_t indices[] = {0x7005, 0x7018, 0x7017, 0x7028};
  const uint32_t expected[] = {5, bitsOf(CURRENT), bitsOf(SPEED), WATCHDOG};
  for (int i = 0; i < 4; ++i) {
    if (!readValue(indices[i], value) || value != expected[i]) {
      if (cancelled && !motorFault) return false;
      invalidateReference();
      Serial.printf("Pre-arm settings changed/unreadable: 0x%04X. No enable.\n", indices[i]); return false;
    }
  }
  return true;
}

bool armMotor() {
  phase = ARMING; cancelled = false;
  if (!held() || !centered() || motorFault || !referenceValid || !settingsIntact() || !readPosition() || !withinReference()) return false;
  target = position;
  if (!writeChecked(0x7016, bitsOf(target)) || !commandState(3, 2) ||
      !writeChecked(0x7018, bitsOf(CURRENT)) || !writeChecked(0x7017, bitsOf(SPEED))) return false;
  uint32_t timeout;
  if (!readValue(0x7028, timeout) || timeout != WATCHDOG || !readPosition()) return false;
  target = position;
  if (!withinReference() || !writeChecked(0x7016, bitsOf(target)) || !held() || cancelled || motorFault) return false;
  targetRate = 0; phase = ACTIVE; pollAt = millis();
  Serial.println("ARMED HOLD: keep R1 held; neutral holds; release R1 to DISABLE. Fixed travel 0..80 deg at 1.0 rad/s, 1 A.");
  return true;
}

void setup() {
  auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.setTextSize(2);
  BP32.setup(&connected, &disconnected); BP32.enableVirtualDevice(false);
  BP32.enableNewBluetoothConnections(true);
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64; g.tx_queue_len = 0; // No software queue of stale targets.
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e = twai_driver_install(&g, &t, &f);
  if (e == ESP_OK) e = twai_start();
  driverReady = e == ESP_OK; retryAt = millis() + 1500;
  Serial.printf("RS05 position hold v0.10 CAN=%s. No enable on boot.\n", esp_err_to_name(e));
  if (driverReady) Serial.println(stopMotor() ? "Startup disabled confirmed." : "Startup STOP unconfirmed; use motor power cutoff if needed.");
  Serial.println("Waiting for Bluetooth gamepad input. Reconnect/pair the controller; release RB/R1.");
  Serial.println("Extension direction: decreasing encoder position; right stick extends, left retracts.");
  Serial.println("RB/R1=hold enable; left X=motion; neutral=hold position. Button A on Stick or serial x=stop. No spinner output.");
}

void loop() {
  service();
  const uint32_t now = millis();
  if (driverReady) {
    if (stopRequested || ((phase == ACTIVE || phase == ARMING) && (cancelled || motorFault || !referenceValid || !held()))) {
      loseControl("R1 released, stop, controller loss/stale data, or motor state change", motorFault || !referenceValid);
    } else if (phase == OFFLINE && inputFresh() && int32_t(now - retryAt) >= 0) {
      if (!recoverDriver()) retryAt = millis() + RETRY_MS;
      else if (!configureDisabled()) loseControl("CAN/configuration not verified", true);
    } else if (phase == NEED_REFERENCE || phase == WAIT_RELEASE || phase == READY) {
      if (uint32_t(now - pollAt) >= 100) {
        pollAt = now;
        if (!readPosition()) { loseControl("CAN feedback lost while disabled", true); return; }
      }
      if (referenceCaptured && !referenceValid) phase = NEED_REFERENCE;
      if (referenceValid && uint32_t(now - signatureAt) >= 500) {
        signatureAt = now;
        if (!settingsIntact()) { loseControl("disabled motor settings changed/unreadable", true); return; }
      }
      if (phase == NEED_REFERENCE) {
        if (!M5.BtnB.isPressed()) { referenceButtonReleased = true; referenceHoldAt = 0; }
        else if (!referenceCaptured && referenceButtonReleased && inputFresh() && centered() && !held() && !M5.BtnA.isPressed()) {
          if (!referenceHoldAt) referenceHoldAt = now;
          if (uint32_t(now - referenceHoldAt) >= 2000) {
            referenceHoldAt = 0; referenceButtonReleased = false;
            if (!captureReference()) loseControl("manual reference failed", true);
          }
        } else referenceHoldAt = 0;
      } else if (!inputFresh() || M5.BtnA.isPressed()) { phase = WAIT_RELEASE; releasedAt = 0; }
      else if (phase == WAIT_RELEASE) {
        if (!held() && centered()) {
          if (!releasedAt) releasedAt = now;
          if (uint32_t(now - releasedAt) >= 100) { phase = READY; Serial.println("Ready. Press/hold R1 with left stick centered for active position hold."); }
        } else releasedAt = 0;
      } else if (held()) {
        if (!centered()) { phase = WAIT_RELEASE; releasedAt = 0; Serial.println("Center stick and release RB/R1 before enabling."); }
        else if (!armMotor()) loseControl("arming interrupted or unverified", !cancelled || motorFault || !referenceValid);
      }
    } else if (phase == ACTIVE && uint32_t(now - pollAt) >= 50) {
      float dt = float(uint32_t(now - pollAt)) / 1000.0f;
      if (dt > .05f) dt = .05f;
      pollAt = now;
      if (!readPosition()) { loseControl("CAN feedback lost", true); return; }
      if (!referenceValid) { loseControl("reference invalidated", true); return; }
      if (uint32_t(now - signatureAt) >= 500) {
        signatureAt = now;
        if (!settingsIntact()) { loseControl("active motor settings changed/unreadable", !cancelled || motorFault); return; }
      }
      const float measuredTravel = EXTEND_SIGN * (position - origin);
      if (measuredTravel < -.035f || measuredTravel > TRAVEL + .035f) { loseControl("measured travel exceeded fixed reference limits", true); return; }
      const int axis = pad && inputFresh() ? pad->axisX() : 0;
      const float rate = abs(axis) <= 60 ? 0.0f : float(axis) / 512.0f;
      const float desiredRate = rate * SPEED;
      const float change = ACCELERATION * dt;
      if (targetRate < desiredRate) targetRate = fminf(targetRate + change, desiredRate);
      else targetRate = fmaxf(targetRate - change, desiredRate);
      float travel = EXTEND_SIGN * (target - origin) + targetRate * dt;
      if (travel >= TRAVEL) { travel = TRAVEL; targetRate = 0; }
      if (travel <= 0) { travel = 0; targetRate = 0; }
      target = origin + EXTEND_SIGN * travel;
      if (!writeChecked(0x7016, bitsOf(target))) loseControl("CAN target acknowledgement lost", true);
    }
  }
  if (uint32_t(millis() - lastPadLog) >= 1000) {
    lastPadLog = millis();
    const bool linked = pad && pad->isConnected();
    const bool gamepad = linked && pad->isGamepad();
    if (phase == ACTIVE) Serial.printf("MOTION measured_deg=%.2f target_deg=%.2f rate_deg_s=%.2f limit=0..80\n",
      EXTEND_SIGN*(position-origin)*180.0f/PI, EXTEND_SIGN*(target-origin)*180.0f/PI, targetRate*180.0f/PI);
    Serial.printf("PAD link=%u gamepad=%u fresh=%u reports=%lu age_ms=%ld leftX=%d RB_R1=%u phase=%u\n",
      unsigned(linked), unsigned(gamepad), unsigned(inputFresh()), (unsigned long)padReports,
      padReport ? long(uint32_t(millis() - padReport)) : -1L,
      gamepad ? pad->axisX() : 0, unsigned(gamepad && pad->r1()), unsigned(phase));
  }
  if (uint32_t(now - displayAt) >= 250) {
    displayAt = now;
    const char* names[] = {"CAN OFFLINE", "CONFIGURING", "RELEASE RB/R1", "READY", "ARMING", "ACTIVE HOLD", "SET ZERO: B 2s"};
    M5.Display.setCursor(0, 0);
    const char* padState = !pad || !pad->isConnected() ? "NO LINK" : !pad->isGamepad() ? "WAIT TYPE" : !padReport ? "NO REPORTS" : inputFresh() ? "FRESH" : "STALE";
    M5.Display.printf("%-16s\nPAD: %-10s\n1A / 0..80 deg\nR1: hold enable\nLeft X: move\nA / x: STOP\n", phase == NEED_REFERENCE && referenceCaptured ? "REF INVALID" : phase == OFFLINE && !inputFresh() ? "CONNECT PAD" : names[phase], padState);
  }
  delay(1);
}
