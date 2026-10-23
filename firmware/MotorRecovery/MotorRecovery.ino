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
constexpr float LAUNCH_CURRENT = 3.0f, LAUNCH_SPEED = 5.0f, LAUNCH_ACCELERATION = 40.0f;
float expectedCurrent = CURRENT, expectedSpeed = SPEED;
bool profilePending = false;
uint32_t lastControlMs = 0, burstAt = 0;
constexpr uint32_t MAX_BURST_MS = 1000;
constexpr float ACCELERATION = 5.0f; // Host target-rate ramp, rad/s^2.
constexpr float EXTEND_SIGN = -1.0f; // Extension decreases encoder position.
constexpr float TRAVEL = 80.0f * PI / 180.0f;
constexpr float POSITION_READBACK_TOL = 0.0005f; // rad, ~0.029 deg; target readback only.
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
float targetAtPosition = 0, motorIq = NAN, motorVelocity = NAN, motorVoltage = NAN;
uint32_t positionAt = 0, iqAt = 0, velocityAt = 0, voltageAt = 0, traceAt = 0;
bool iqAvailable = true, velocityAvailable = true, voltageAvailable = true;
uint8_t readStatus = 255;
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

// Preserve the distinction between an operator/input abort and transport failure.
// Only a confirmed STOP after an input abort may preserve reference trust.
enum OperationFailure { OP_OK, OP_INPUT_CANCEL, OP_CAN_FAILURE };
OperationFailure operationFailure = OP_OK;
enum CycleStage { MANUAL, RETRACT, SETTLE, EXTEND, COMPLETE, LAUNCH_SETTLE };
CycleStage cycleStage = MANUAL, cycleAtPosition = MANUAL;
bool cycleRequested = false, cycleButtonDown = true, fastButtonDown = true;
bool cycleRequestFast = false, fastCycle = false;
uint32_t cycleStageAt = 0, cycleSettledAt = 0;
constexpr float ARRIVAL = 0.5f * PI / 180.0f;
constexpr uint32_t CYCLE_TIMEOUT_MS = 6000, CYCLE_PAUSE_MS = 300;
void clearCycle() {
  cycleStage = MANUAL; cycleRequested = cycleRequestFast = fastCycle = false; cycleStageAt = cycleSettledAt = 0;
}
const char* cycleNameFor(unsigned stage) {
  const char* names[] = {"MANUAL", "RETRACT", "PAUSE", "EXTEND", "DONE/HOLD", "BRAKE/HOLD"};
  return names[stage];
}
const char* cycleName() { return cycleNameFor(unsigned(cycleStage)); }
bool fastExtension() { return fastCycle && (cycleStage == EXTEND || cycleStage == LAUNCH_SETTLE); }
uint32_t controlPeriodMs() { return fastExtension() ? 10 : 50; }
bool held();
bool inputCancelled() {
  return referenceValid && !motorFault &&
    (cancelled || stopRequested) && (!held() || stopRequested || M5.BtnA.isPressed());
}
void blockedOperation() {
  operationFailure = inputCancelled() ? OP_INPUT_CANCEL : OP_CAN_FAILURE;
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
    pad = candidate; padReport = 0; padReports = 0; cycleButtonDown = fastButtonDown = true;
    Serial.println("BT connected: waiting for gamepad reports. Release RB/R1.");
  }
}
void disconnected(ControllerPtr candidate) {
  if (candidate == pad) { pad = nullptr; padReport = 0; padReports = 0; cycleButtonDown = fastButtonDown = true; Serial.println("Bluetooth disconnected."); }
}

void service() {
  M5.update();
  const bool updated = BP32.update();
  if (updated && pad && pad->isConnected() && pad->isGamepad() && pad->hasData()) {
    padReport = millis(); ++padReports;
    const bool slowButton = pad->y(), fastButton = pad->x();
    const bool slowEdge = slowButton && !cycleButtonDown, fastEdge = fastButton && !fastButtonDown;
    if ((slowEdge || fastEdge) && phase == ACTIVE && held() &&
        (cycleStage == MANUAL || cycleStage == COMPLETE)) {
      if (centered()) { cycleRequested = true; cycleRequestFast = fastEdge && !slowEdge; }
      else Serial.println("Cycle ignored: center left stick, release the cycle button, then press again.");
    }
    cycleButtonDown = slowButton; fastButtonDown = fastButton;
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
      readDone = true; readStatus = (f.identifier >> 16) & 255; readOK = readStatus == 0;
      memcpy(&readBits, f.data + 4, 4);
    }
  }
}

bool sendPacket(uint8_t type, const uint8_t* bytes = nullptr) {
  operationFailure = OP_OK;
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
      operationFailure = OP_CAN_FAILURE;
      Serial.printf("TX type=%u blocked: driver=%s state=%u\n", type, esp_err_to_name(result), unsigned(status.state));
      return false;
    }
    if (!status.msgs_to_tx) break;
    if (uint32_t(millis() - began) >= 20) {
      operationFailure = OP_CAN_FAILURE;
      Serial.printf("TX type=%u blocked: mailbox busy; tx_err=%lu rx_err=%lu\n", type,
        (unsigned long)status.tx_error_counter, (unsigned long)status.rx_error_counter);
      return false;
    }
    service();
    if (type != 4 && (cancelled || motorFault || stopRequested)) { blockedOperation(); return false; }
    delay(1);
  }
  service();
  if (type != 4 && (cancelled || motorFault || stopRequested)) { blockedOperation(); return false; }
  const esp_err_t result = twai_transmit(&f, pdMS_TO_TICKS(10));
  if (result != ESP_OK) { operationFailure = OP_CAN_FAILURE; Serial.printf("TX type=%u failed: %s\n", type, esp_err_to_name(result)); }
  return result == ESP_OK;
}

bool readValue(uint16_t index, uint32_t& bits) {
  operationFailure = OP_OK;
  service();
  if (cancelled || motorFault || stopRequested) { blockedOperation(); return false; }
  pendingIndex = index; pending = true; readDone = readOK = false; readStatus = 255;
  const uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  if (!sendPacket(17, d)) { pending = false; return false; }
  const uint32_t began = millis();
  while (!readDone && !cancelled && !motorFault && uint32_t(millis() - began) < 150) { service(); delay(1); }
  pending = false;
  if (!readDone || !readOK || cancelled || motorFault || stopRequested) {
    if ((readDone && !readOK) || uint32_t(millis() - began) >= 150 || motorFault || !referenceValid)
      operationFailure = OP_CAN_FAILURE;
    else blockedOperation();
    Serial.printf(operationFailure == OP_INPUT_CANCEL ? "Input cancelled read 0x%04X\n" : "Read failed 0x%04X\n", index);
    return false;
  }
  bits = readBits; return true;
}
bool positionTargetMatches(uint32_t wantedBits, uint32_t actualBits) {
  const float wanted = floatOf(wantedBits), actual = floatOf(actualBits);
  return std::isfinite(wanted) && std::isfinite(actual) && fabsf(actual - wanted) <= POSITION_READBACK_TOL;
}
bool writeChecked(uint16_t index, uint32_t bits, bool byteValue = false) {
  operationFailure = OP_OK;
  service();
  if (cancelled || motorFault || stopRequested) { blockedOperation(); return false; }
  uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  memcpy(d + 4, &bits, 4);
  uint32_t actual;
  if (!sendPacket(18, d) || !readValue(index, actual)) {
    Serial.printf("WRITE/VERIFY failed index=0x%04X\n", index); return false;
  }
  bool matches = byteValue ? (actual & 255) == (bits & 255) : actual == bits;
  // Position-target readback can differ slightly from the requested float.
  // Only this parameter permits the bounded tolerance; settings stay exact.
  if (index == 0x7016 && !byteValue) {
    matches = positionTargetMatches(bits, actual);
  }
  if (!matches) {
    operationFailure = OP_CAN_FAILURE;
    Serial.printf("VERIFY mismatch index=0x%04X wanted=%08lX got=%08lX\n", index, (unsigned long)bits, (unsigned long)actual);
    if (index == 0x7016) Serial.printf("Target delta_rad=%.7f tolerance_rad=%.7f\n", floatOf(actual)-floatOf(bits), POSITION_READBACK_TOL);
  }
  return matches;
}
bool readPosition() {
  uint32_t bits;
  if (!readValue(0x7019, bits)) return false;
  position = floatOf(bits);
  if (!std::isfinite(position)) { operationFailure = OP_CAN_FAILURE; return false; }
  // The command has not advanced during this read: compare like with like.
  targetAtPosition = target; positionAt = millis(); cycleAtPosition = cycleStage;
  return true;
}
bool commandState(uint8_t type, uint8_t expected) {
  service();
  const uint32_t before = modeCount, began = millis();
  if (!sendPacket(type)) return false;
  while (modeCount == before && uint32_t(millis() - began) < 150) {
    service();
    if (type != 4 && (cancelled || motorFault || stopRequested)) { blockedOperation(); return false; }
    delay(1);
  }
  const bool confirmed = modeCount != before && motorMode == expected;
  if (!confirmed) operationFailure = OP_CAN_FAILURE;
  return confirmed;
}
bool stopMotor() {
  for (int i = 0; i < 2; ++i) if (commandState(4, 0)) return true;
  return false;
}

bool setMotorProfile(float amps, float speed) {
  profilePending = true;
  if (!writeChecked(0x7018, bitsOf(amps))) return false;
  expectedCurrent = amps;
  if (!writeChecked(0x7017, bitsOf(speed))) return false;
  expectedSpeed = speed; profilePending = false;
  burstAt = amps == LAUNCH_CURRENT && speed == LAUNCH_SPEED ? millis() : 0;
  Serial.printf("PROFILE verified current_A=%.1f speed_rad_s=%.1f\n", amps, speed);
  return true;
}

void loseControl(const char* reason, bool uncertain = false) {
  motorIq = motorVelocity = motorVoltage = NAN;
  iqAt = velocityAt = voltageAt = traceAt = 0;
  phase = OFFLINE; pending = false; releasedAt = 0; targetRate = 0; referenceHoldAt = 0; clearCycle();
  Serial.printf("DISARMED: %s; old target discarded.\n", reason);
  const bool stopped = stopMotor();
  Serial.println(stopped ? "Disabled confirmed." : "STOP UNCONFIRMED: motor watchdog is fallback; use power cutoff if needed.");
  cancelled = stopRequested = false;
  if (stopped && !uncertain && !motorFault &&
      (profilePending || expectedCurrent != CURRENT || expectedSpeed != SPEED)) {
    // Restore low limits only after confirmed disable. No enabling or re-zeroing.
    if (!setMotorProfile(CURRENT, SPEED)) {
      uncertain = true;
      Serial.println("Disabled profile restoration failed; reference remains blocked.");
    }
  }
  if (uncertain || !stopped || motorFault) invalidateReference();
  if (stopped && !uncertain && !motorFault) {
    phase = referenceCaptured && referenceValid ? WAIT_RELEASE : NEED_REFERENCE;
    pollAt = millis();
    return; // Confirmed disabled: keep monitoring CAN even without Bluetooth.
  }
  // No CAN traffic during this interval: allow the motor watchdog to expire
  // even if only the feedback path was lost. Recovery starts with STOP.
  retryAt = millis() + RETRY_MS;
}

void loseFailedOperation(const char* reason) {
  const bool inputAbort = operationFailure == OP_INPUT_CANCEL && referenceValid && !motorFault;
  loseControl(inputAbort ? "input cancelled transaction; stopping without changing zero" : reason, !inputAbort);
}

bool readTelemetry(uint16_t index, float& value, uint32_t& at, bool& available) {
  if (!available) return true;
  uint32_t bits;
  if (!readValue(index, bits)) {
    // Optional fields may be unsupported; never label an error payload as zero.
    // Transport failures and input cancellation still follow normal stop paths.
    if (readDone && readStatus == 1 && operationFailure == OP_CAN_FAILURE &&
        referenceValid && !motorFault && !cancelled && !stopRequested) {
      available = false; value = NAN; at = 0;
      Serial.printf("TELEMETRY unavailable index=0x%04X status=1; field disabled for this startup.\n", index);
      operationFailure = OP_OK;
      return true;
    }
    return false;
  }
  value = floatOf(bits);
  if (!std::isfinite(value)) {
    value = NAN; at = 0; available = false;
    Serial.printf("TELEMETRY invalid index=0x%04X; field disabled for this startup.\n", index);
    return true;
  }
  at = millis(); return true;
}

bool logMotorTrace() {
  if (uint32_t(millis() - traceAt) < (fastExtension() ? 50U : 100U)) return true;
  traceAt = millis(); // Schedule from request start, not completion of sequential reads.
  if (!readTelemetry(0x701A, motorIq, iqAt, iqAvailable) ||
      !readTelemetry(0x701B, motorVelocity, velocityAt, velocityAvailable)) return false;
  if ((!voltageAt || uint32_t(millis() - voltageAt) >= 500) &&
      !readTelemetry(0x701C, motorVoltage, voltageAt, voltageAvailable)) return false;
  service();
  if (cancelled || motorFault || stopRequested || !referenceValid) { blockedOperation(); return false; }
  const uint32_t loggedAt = millis();
  const float deg = 180.0f / PI;
  const float measured = EXTEND_SIGN * (position - origin) * deg;
  const float priorTarget = EXTEND_SIGN * (targetAtPosition - origin) * deg;
  Serial.printf("TRACE,%lu,%lu,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%lu,%.3f,%lu,%.4f,%lu,%.3f,%.1f,%.1f,%lu,%s\n",
    (unsigned long)loggedAt, (unsigned long)positionAt, cycleNameFor(unsigned(cycleAtPosition)), measured,
    priorTarget, priorTarget - measured, EXTEND_SIGN * (target - origin) * deg, targetRate * deg,
    (unsigned long)velocityAt, EXTEND_SIGN * motorVelocity * deg,
    (unsigned long)iqAt, motorIq, (unsigned long)voltageAt, motorVoltage, expectedCurrent, expectedSpeed,
    (unsigned long)lastControlMs, fastCycle ? "FAST" : "SLOW");
  return true;
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
      !writeChecked(0x7005, 5, true) ||
      !setMotorProfile(CURRENT, SPEED) || !readPosition()) return false;
  target = position;
  if (!writeChecked(0x7016, bitsOf(target))) return false;
  phase = referenceCaptured && referenceValid ? WAIT_RELEASE : NEED_REFERENCE;
  releasedAt = 0; referenceHoldAt = 0; referenceButtonReleased = false; pollAt = millis();
  if (referenceCaptured && !referenceValid)
    Serial.println("CAN ready / disabled; REF INVALID. Restart and manually reference at the known retracted pose. B cannot overwrite this session's zero.");
  else if (referenceCaptured)
    Serial.println("CAN ready / disabled; existing fixed zero preserved. Center stick and release R1 before enabling.");
  else
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
  const uint32_t expected[] = {5, bitsOf(expectedCurrent), bitsOf(expectedSpeed), WATCHDOG};
  for (int i = 0; i < 4; ++i) {
    if (!readValue(indices[i], value) || value != expected[i]) {
      if (operationFailure == OP_INPUT_CANCEL && !motorFault && referenceValid) return false;
      operationFailure = OP_CAN_FAILURE;
      invalidateReference();
      Serial.printf("Motor settings changed/unreadable: 0x%04X. No enable.\n", indices[i]); return false;
    }
  }
  return true;
}

bool armMotor() {
  operationFailure = OP_OK;
  phase = ARMING; cancelled = false;
  if (!held() || !centered() || motorFault || !referenceValid || !settingsIntact() || !readPosition() || !withinReference()) return false;
  target = position;
  if (!setMotorProfile(CURRENT, SPEED) || !writeChecked(0x7016, bitsOf(target)) || !commandState(3, 2) ||
      !setMotorProfile(CURRENT, SPEED)) return false;
  uint32_t timeout;
  if (!readValue(0x7028, timeout) || timeout != WATCHDOG || !readPosition()) return false;
  target = position;
  if (!withinReference() || !writeChecked(0x7016, bitsOf(target)) || !held() || cancelled || motorFault) return false;
  clearCycle(); targetRate = 0; phase = ACTIVE; pollAt = millis();
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
  Serial.printf("RS05 position hold v0.15 CAN=%s. No enable on boot.\n", esp_err_to_name(e));
  if (driverReady) Serial.println(stopMotor() ? "Startup disabled confirmed." : "Startup STOP unconfirmed; use motor power cutoff if needed.");
  Serial.println("Waiting for Bluetooth gamepad input. Reconnect/pair the controller; release RB/R1.");
  Serial.println("Extension direction: decreasing encoder position; right stick extends, left retracts.");
  Serial.println("TRACE_HEADER,t_ms,pos_ms,cycle_at_pos,pos_deg,target_at_pos_deg,error_deg,command_deg,command_rate_deg_s,vel_ms,vel_deg_s,iq_ms,iq_A,vbus_ms,vbus_V,profile_A,profile_rad_s,control_ms,cycle_mode");
  Serial.println("Controller Y: SLOW cycle (1 A/1 rad/s). Controller X: FAST extension (3 A/5 rad/s) after slow retract. R1 must stay held; stick overrides.");
  Serial.println("RB/R1=hold enable; left X=motion; neutral=hold position. Button A on Stick or serial x=stop. No spinner output.");
}

void loop() {
  service();
  const uint32_t now = millis();
  if (driverReady) {
    if (stopRequested || ((phase == ACTIVE || phase == ARMING) && (cancelled || motorFault || !referenceValid || !held()))) {
      loseControl("R1 released, stop, controller loss/stale data, or motor state change", motorFault || !referenceValid);
    } else if (phase == ACTIVE && burstAt && uint32_t(now - burstAt) >= MAX_BURST_MS) {
      loseControl("fast extension exceeded 1-second burst deadline");
    } else if (phase == OFFLINE && inputFresh() && int32_t(now - retryAt) >= 0) {
      if (!recoverDriver()) retryAt = millis() + RETRY_MS;
      else if (!configureDisabled()) loseControl("CAN/configuration not verified", true);
    } else if (phase == NEED_REFERENCE || phase == WAIT_RELEASE || phase == READY) {
      if (uint32_t(now - pollAt) >= 100) {
        pollAt = now;
        if (!readPosition()) { loseFailedOperation("CAN feedback lost while disabled"); return; }
      }
      if (referenceCaptured && !referenceValid) phase = NEED_REFERENCE;
      if (referenceValid && uint32_t(now - signatureAt) >= 500) {
        signatureAt = now;
        if (!settingsIntact()) { loseFailedOperation("disabled motor settings changed/unreadable"); return; }
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
        else if (!armMotor()) loseFailedOperation("arming interrupted or unverified");
      }
    } else if (phase == ACTIVE && uint32_t(now - pollAt) >= controlPeriodMs()) {
      lastControlMs = uint32_t(now - pollAt);
      float dt = float(lastControlMs) / 1000.0f;
      const float maxDt = fastExtension() ? .02f : .05f;
      if (dt > maxDt) dt = maxDt;
      pollAt = now;
      if (!readPosition()) { loseFailedOperation("CAN feedback lost"); return; }
      if (!referenceValid) { loseControl("reference invalidated", true); return; }
      if (uint32_t(now - signatureAt) >= 500) {
        signatureAt = now;
        if (!settingsIntact()) { loseFailedOperation("active motor settings changed/unreadable"); return; }
      }
      const float measuredTravel = EXTEND_SIGN * (position - origin);
      if (measuredTravel < -.035f || measuredTravel > TRAVEL + .035f) { loseControl("measured travel exceeded fixed reference limits", true); return; }
      const int axis = pad && inputFresh() ? pad->axisX() : 0;
      if (cycleRequested) {
        cycleRequested = false;
        if (centered() && held() && (cycleStage == MANUAL || cycleStage == COMPLETE)) {
          fastCycle = cycleRequestFast; cycleRequestFast = false;
          cycleStage = RETRACT; cycleStageAt = millis(); cycleSettledAt = 0;
          target = position; targetRate = 0;
          Serial.printf("CYCLE RETRACT (%s): 1 A / 1 rad/s; extension %s. Keep R1 held.\n",
            fastCycle ? "FAST" : "SLOW", fastCycle ? "3 A / 5 rad/s" : "1 A / 1 rad/s");
        }
      }
      if (cycleStage != MANUAL && abs(axis) > 60) {
        if (profilePending || expectedCurrent != CURRENT || expectedSpeed != SPEED) {
          if (!setMotorProfile(CURRENT, SPEED)) { loseFailedOperation("manual profile restoration failed"); return; }
        }
        clearCycle(); target = position; targetRate = 0;
        Serial.println("Cycle cancelled by joystick; low-speed 1 A manual control restored.");
      }
      if (cycleStage != MANUAL && cycleStage != COMPLETE &&
          uint32_t(millis() - cycleStageAt) >= CYCLE_TIMEOUT_MS) {
        loseControl("cycle failed to reach/hold endpoint within 6 seconds"); return;
      }
      float travel = fminf(TRAVEL, fmaxf(0.0f, EXTEND_SIGN * (target - origin)));
      float desiredRate = (abs(axis) <= 60 ? 0.0f : float(axis) / 512.0f) * SPEED;
      if (cycleStage == RETRACT) desiredRate = -fminf(SPEED, sqrtf(2 * ACCELERATION * travel));
      const float acceleration = fastCycle && cycleStage == EXTEND ? LAUNCH_ACCELERATION : ACCELERATION;
      if (cycleStage == EXTEND) desiredRate = fminf(fastCycle ? LAUNCH_SPEED : SPEED, sqrtf(2 * acceleration * (TRAVEL - travel)));
      if (cycleStage == SETTLE || cycleStage == COMPLETE || cycleStage == LAUNCH_SETTLE) {
        travel = cycleStage == SETTLE ? 0.0f : TRAVEL; desiredRate = 0; targetRate = 0;
      }
      const float change = acceleration * dt;
      if (targetRate < desiredRate) targetRate = fminf(targetRate + change, desiredRate);
      else targetRate = fmaxf(targetRate - change, desiredRate);
      travel += targetRate * dt;
      if (travel >= TRAVEL) { travel = TRAVEL; targetRate = 0; }
      if (travel <= 0) { travel = 0; targetRate = 0; }
      target = origin + EXTEND_SIGN * travel;
      if (!writeChecked(0x7016, bitsOf(target))) { loseFailedOperation("CAN target acknowledgement lost"); return; }
      if (cycleStage == RETRACT && travel <= .0002f && fabsf(measuredTravel) <= ARRIVAL) {
        cycleStage = SETTLE; cycleStageAt = millis(); cycleSettledAt = 0; targetRate = 0;
        Serial.println("CYCLE PAUSE: holding retracted position for 300 ms.");
      } else if (cycleStage == SETTLE) {
        if (fabsf(measuredTravel) > ARRIVAL) cycleSettledAt = 0;
        else {
          if (!cycleSettledAt) cycleSettledAt = millis();
          if (uint32_t(millis() - cycleSettledAt) >= CYCLE_PAUSE_MS) {
            if (fastCycle && !setMotorProfile(LAUNCH_CURRENT, LAUNCH_SPEED)) {
              loseFailedOperation("fast profile not verified; extension cancelled"); return;
            }
            cycleStage = EXTEND; cycleStageAt = millis();
            Serial.printf("CYCLE EXTEND (%s) t_ms=%lu: fixed 80-degree endpoint.\n", fastCycle ? "FAST" : "SLOW", (unsigned long)cycleStageAt);
          }
        }
      } else if (cycleStage == EXTEND && TRAVEL - travel <= .0002f && fabsf(measuredTravel - TRAVEL) <= ARRIVAL) {
        targetRate = 0;
        if (fastCycle) {
          Serial.printf("FAST endpoint arrived after %lu ms; holding burst profile until position settles.\n", (unsigned long)(millis() - cycleStageAt));
          cycleStage = LAUNCH_SETTLE; cycleStageAt = millis(); cycleSettledAt = 0;
        } else {
          cycleStage = COMPLETE;
          Serial.println("CYCLE COMPLETE: low-current extended hold; fresh Y/X press for another cycle.");
        }
      } else if (cycleStage == LAUNCH_SETTLE) {
        if (fabsf(measuredTravel - TRAVEL) > ARRIVAL) cycleSettledAt = 0;
        else {
          if (!cycleSettledAt) cycleSettledAt = millis();
          if (uint32_t(millis() - cycleSettledAt) >= 150) {
            if (!setMotorProfile(CURRENT, SPEED)) { loseFailedOperation("holding profile restoration failed"); return; }
            cycleStage = COMPLETE; targetRate = 0;
            Serial.println("FAST CYCLE COMPLETE: 1 A holding profile restored; fresh Y/X press for another cycle.");
          }
        }
      }
      if (!logMotorTrace()) { loseFailedOperation("motor telemetry transaction failed"); return; }
    }
  }
  if (uint32_t(millis() - lastPadLog) >= 1000) {
    lastPadLog = millis();
    const bool linked = pad && pad->isConnected();
    const bool gamepad = linked && pad->isGamepad();
    if (phase == ACTIVE) Serial.printf("MOTION measured_deg=%.2f target_at_pos_deg=%.2f rate_deg_s=%.2f limit=0..80 cycle=%s\n",
      EXTEND_SIGN*(position-origin)*180.0f/PI, EXTEND_SIGN*(targetAtPosition-origin)*180.0f/PI, targetRate*180.0f/PI, cycleNameFor(unsigned(cycleAtPosition)));
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
    M5.Display.printf("%-16s\nPAD: %-10s\n%.0fA / 0..80 deg\nR1: hold enable\nY slow / X fast\n%-9s\nA / x: STOP\n", phase == NEED_REFERENCE && referenceCaptured ? "REF INVALID" : phase == OFFLINE && !inputFresh() ? "CONNECT PAD" : names[phase], padState, expectedCurrent, cycleName());
  }
  delay(1);
}
