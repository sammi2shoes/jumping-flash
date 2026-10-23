#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cstring>
#include <cmath>

constexpr uint8_t MOTOR = 1, HOST = 126;
constexpr float CURRENT = 1.0f, SPEED = 0.10f;
constexpr float STEP = 5.0f * PI / 180.0f;
bool ready = false, cancelled = false;

bool abortRequested() {
  M5.update();
  if (M5.BtnA.wasPressed()) cancelled = true;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'x' || c == 'X') cancelled = true;
  }
  return cancelled;
}

void drainRx() {
  twai_message_t f;
  for (int n = 0; n < 128 && twai_receive(&f, 0) == ESP_OK; ++n) {}
}

bool sendFrame(uint8_t type, const uint8_t* data = nullptr) {
  twai_message_t f = {};
  f.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
  f.extd = 1; f.ss = 1; f.data_length_code = 8;
  if (data) memcpy(f.data, data, 8);
  return twai_transmit(&f, pdMS_TO_TICKS(10)) == ESP_OK;
}

bool fromMotor(const twai_message_t& f, uint8_t type) {
  return f.extd && !f.rtr && f.data_length_code == 8 &&
    ((f.identifier >> 24) & 31) == type &&
    (f.identifier & 0xFFFF) == ((uint32_t(MOTOR) << 8) | HOST);
}

bool stateCommand(uint8_t type, unsigned expectedMode) {
  drainRx();
  if (!sendFrame(type)) return false;
  uint32_t began = millis();
  while (millis() - began < 200) {
    if (type != 4 && abortRequested()) return false;
    twai_message_t f;
    while (twai_receive(&f, 0) == ESP_OK) {
      if (!fromMotor(f, 2)) continue;
      unsigned mode = (f.identifier >> 22) & 3;
      unsigned faults = (f.identifier >> 16) & 63;
      Serial.printf("STATE mode=%u faults=0x%02X\n", mode, faults);
      if (type == 4) return mode == 0; // A fault must not prevent disable confirmation.
      return mode == expectedMode && faults == 0;
    }
    delay(1);
  }
  return false;
}

// Legacy type-9 reads work on your motor's 0.5.0.3 firmware.
bool readLegacy(uint16_t index, uint8_t kind, uint32_t& bits) {
  if (abortRequested()) return false;
  drainRx();
  uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  if (!sendFrame(9, d)) return false;
  uint32_t began = millis();
  while (millis() - began < 150) {
    if (abortRequested()) return false;
    twai_message_t f;
    while (twai_receive(&f, 0) == ESP_OK) {
      if (fromMotor(f, 2) && ((f.identifier >> 16) & 63)) return false;
      if (!fromMotor(f, 9) || f.data[0] != d[0] || f.data[1] != d[1]) continue;
      if (f.data[2] != kind || f.data[3] != 0) return false;
      memcpy(&bits, f.data + 4, 4);
      return true;
    }
    delay(1);
  }
  Serial.printf("READ FAILED 0x%04X\n", index);
  return false;
}

bool readFloat(uint16_t index, float& value) {
  uint32_t bits;
  if (!readLegacy(index, 6, bits)) return false;
  memcpy(&value, &bits, 4);
  return std::isfinite(value);
}

// Standard writes with checked type-17 readback, matching the working GUI.
bool writeParameter(uint16_t index, uint32_t bits) {
  if (abortRequested()) return false;
  uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  memcpy(d + 4, &bits, 4);
  return sendFrame(18, d);
}

bool verifyStandard(uint16_t index, uint32_t expected, bool floating) {
  drainRx();
  uint8_t d[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
  if (!sendFrame(17, d)) return false;
  uint32_t began = millis();
  while (millis() - began < 150) {
    if (abortRequested()) return false;
    twai_message_t f;
    while (twai_receive(&f, 0) == ESP_OK) {
      if (fromMotor(f, 2) && ((f.identifier >> 16) & 63)) return false;
      if (!fromMotor(f, 17) || f.data[0] != d[0] || f.data[1] != d[1]) continue;
      if (((f.identifier >> 16) & 255) != 0) {
        Serial.printf("READ rejected 0x%04X\n", index);
        return false;
      }
      uint32_t bits; memcpy(&bits, f.data + 4, 4);
      bool ok = bits == expected;
      if (floating) {
        float actual, requested;
        memcpy(&actual, &bits, 4); memcpy(&requested, &expected, 4);
        ok = std::isfinite(actual) && fabsf(actual - requested) < 0.0001f;
      }
      Serial.printf("VERIFY 0x%04X: %s\n", index, ok ? "OK" : "FAILED");
      return ok;
    }
    delay(1);
  }
  Serial.printf("READ TIMEOUT 0x%04X\n", index);
  return false;
}

bool writeFloat(uint16_t index, float value) {
  uint32_t bits; memcpy(&bits, &value, 4);
  return writeParameter(index, bits) && verifyStandard(index, bits, true);
}

bool setPositionMode() {
  return writeParameter(0x7005, 5) && verifyStandard(0x7005, 5, false);
}

bool prepareAndMove() {
  if (!stateCommand(4, 0)) return false;
  uint32_t faults;
  float start;
  if (!readLegacy(0x3022, 4, faults) || faults || !readFloat(0x3016, start)) return false;
  float target = start + STEP;
  Serial.printf("Start %.5f rad; target %.5f rad (+5 deg)\n", start, target);
  if (!setPositionMode() ||
      !writeFloat(0x7018, CURRENT) ||
      !writeFloat(0x7017, SPEED) ||
      !writeFloat(0x7016, start)) return false;
  if (!stateCommand(3, 2)) return false;
  // Match the working GUI: reapply limits after enable before changing target.
  if (!writeFloat(0x7018, CURRENT) ||
      !writeFloat(0x7017, SPEED) ||
      !writeFloat(0x7016, target)) return false;
  uint32_t began = millis(), settled = 0;
  while (millis() - began < 2000) {
    float position;
    if (!readFloat(0x3016, position) ||
        !readLegacy(0x3022, 4, faults) || faults) return false;
    float delta = position - start;
    Serial.printf("delta=%.2f deg\n", delta * 180.0f / PI);
    if (delta < -0.02f || delta > STEP + 0.035f) return false;
    if (fabsf(position - target) < 0.005f) {
      if (!settled) settled = millis();
      if (millis() - settled >= 150) return true;
    } else settled = 0;
    for (int i = 0; i < 40; ++i) {
      if (abortRequested()) return false;
      delay(1);
    }
  }
  Serial.println("Movement timed out.");
  return false;
}

void runStep() {
  cancelled = false;
  Serial.println("One +5 degree step: 1 A, 0.10 rad/s.");
  bool ok = prepareAndMove();
  bool stopped = false;
  for (int i = 0; i < 3 && !stopped; ++i) stopped = stateCommand(4, 0);
  Serial.println(stopped ? "DISABLED confirmed." : "STOP UNCONFIRMED: disconnect motor power.");
  Serial.println(ok && stopped ? "STEP COMPLETE." : "STEP ABORTED / INCOMPLETE.");
  Serial.println("Idle. New g required. x / button A stops.");
}

void setup() {
  auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.setTextSize(2);
  M5.Display.println("POSITION STEP\n1A / +5 deg\ng: go\nx / A: stop");
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64; g.tx_queue_len = 1;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e = twai_driver_install(&g, &t, &f);
  if (e == ESP_OK) e = twai_start();
  ready = e == ESP_OK;
  Serial.printf("CAN: %s\n", esp_err_to_name(e));
  if (ready) {
    if (!stateCommand(4, 0)) Serial.println("Boot STOP unconfirmed; check motor power/connection.");
    Serial.println("Send g for one step. Watchdog behavior remains unverified.");
  }
}

void loop() {
  M5.update();
  if (ready && M5.BtnA.wasPressed()) stateCommand(4, 0);
  if (ready && Serial.available()) {
    char c = Serial.read();
    if (c == 'g' || c == 'G') runStep();
    if (c == 'x' || c == 'X') stateCommand(4, 0);
  }
  delay(1);
}
