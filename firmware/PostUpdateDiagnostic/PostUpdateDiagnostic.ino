#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cstring>

constexpr uint8_t MOTOR = 1, HOST = 126;
bool ready = false;
bool stopped = false, timeoutVerified = false;
constexpr uint32_t DIAGNOSTIC_TIMEOUT = 20000;

uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void printFrame(const twai_message_t& f) {
  Serial.printf("RX id=%08lX ext=%u rtr=%u dlc=%u data=",
    (unsigned long)f.identifier, unsigned(f.extd),
    unsigned(f.rtr), unsigned(f.data_length_code));
  if (!f.rtr) {
    for (unsigned i = 0; i < f.data_length_code && i < 8; ++i)
      Serial.printf("%02X", f.data[i]);
  }
  Serial.println("");
  if (!f.extd || f.rtr || f.data_length_code != 8) return;
  unsigned type = (f.identifier >> 24) & 31;
  bool addressed = ((f.identifier >> 8) & 255) == MOTOR && (f.identifier & 255) == HOST;
  if (type == 2 && addressed)
    stopped = ((f.identifier >> 22) & 3) == 0 && ((f.identifier >> 16) & 63) == 0;
  if (type == 17 && addressed && ((f.identifier >> 16) & 255) == 0 &&
      f.data[0] == 0x28 && f.data[1] == 0x70 && le32(f.data + 4) == DIAGNOSTIC_TIMEOUT)
    timeoutVerified = true;
  if (type == 2 || type == 24) {
    Serial.printf("  FEEDBACK type=%u sender=%u dest=%u mode=%u flags=0x%02X\n",
      type, unsigned((f.identifier >> 8) & 255),
      unsigned(f.identifier & 255), unsigned((f.identifier >> 22) & 3),
      unsigned((f.identifier >> 16) & 63));
  }
  if (type == 21) {
    Serial.printf("  FAULT/WARNING fault=0x%08lX warning=0x%08lX\n",
      (unsigned long)le32(f.data), (unsigned long)le32(f.data + 4));
  }
  if (type == 9 || type == 17) {
    unsigned index = unsigned(f.data[0]) | (unsigned(f.data[1]) << 8);
    uint32_t bits = le32(f.data + 4);
    float value; memcpy(&value, &bits, 4);
    Serial.printf("  PARAM type=%u index=0x%04X header=%02X,%02X id_status=0x%02X bits=0x%08lX float_candidate=%.6f\n",
      type, index, f.data[2], f.data[3],
      unsigned((f.identifier >> 16) & 255), (unsigned long)bits, value);
  }
}

void listenFor(uint32_t duration) {
  uint32_t began = millis();
  while (uint32_t(millis() - began) < duration) {
    M5.update();
    twai_message_t f;
    for (int n = 0; n < 32 && twai_receive(&f, 0) == ESP_OK; ++n)
      printFrame(f);
    delay(1);
  }
}

void busStatus() {
  twai_status_info_t info = {};
  if (twai_get_status_info(&info) == ESP_OK)
    Serial.printf("BUS state=%u tx_err=%lu rx_err=%lu tx_failed=%lu missed=%lu\n",
      unsigned(info.state), (unsigned long)info.tx_error_counter,
      (unsigned long)info.rx_error_counter, (unsigned long)info.tx_failed_count,
      (unsigned long)info.rx_missed_count);
}

void request(uint8_t type, uint16_t index, const char* label) {
  if (type != 4 && type != 17 && !(type == 18 && index == 0x7028)) return;
  Serial.printf("\nREQUEST %s\n", label);
  twai_message_t f = {};
  f.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
  f.extd = 1; f.ss = 1; f.data_length_code = 8;
  if (type == 17 || type == 18) {
    f.data[0] = index & 255; f.data[1] = index >> 8;
  }
  if (type == 18) {
    f.data[4] = DIAGNOSTIC_TIMEOUT & 255;
    f.data[5] = (DIAGNOSTIC_TIMEOUT >> 8) & 255;
    f.data[6] = (DIAGNOSTIC_TIMEOUT >> 16) & 255;
    f.data[7] = (DIAGNOSTIC_TIMEOUT >> 24) & 255;
  }
  esp_err_t e = twai_transmit(&f, pdMS_TO_TICKS(10));
  Serial.printf("TX id=%08lX result=%s\n", (unsigned long)f.identifier, esp_err_to_name(e));
  listenFor(80);
  busStatus();
}

void diagnose() {
  Serial.println("STANDARD-ONLY DIAGNOSTIC v2. No enable or motion command.");
  stopped = false;
  request(4, 0, "STOP (no fault clear)");
  if (!stopped) {
    Serial.println("STOP NOT CONFIRMED: disconnect motor power. No settings changed.");
    return;
  }
  request(18, 0x7028, "Set timeout=20000 counts (manual: 1 second)");
  timeoutVerified = false;
  request(17, 0x7028, "VERIFY timeout=20000");
  if (!timeoutVerified) {
    Serial.println("Timeout readback NOT VERIFIED. Diagnostic aborted.");
  } else {
    request(17, 0x7005, "run_mode");
    request(17, 0x7018, "current_limit");
    request(17, 0x7019, "position");
    request(17, 0x701C, "bus_voltage");
    request(17, 0x3022, "fault_status (upper-table standard read)");
    request(17, 0x3023, "warning_status (upper-table standard read)");
    Serial.println("Read errors for upper-table entries are logged, not interpreted as zero.");
  }
  stopped = false;
  request(4, 0, "FINAL STOP");
  Serial.println(stopped ? "Final disabled feedback confirmed." : "FINAL STOP UNCONFIRMED: disconnect motor power.");
  Serial.println("DONE. Only timeout=20000 was written. No enable, save, fault-clear or calibration.");
  Serial.println("Timeout action remains unverified. Paste full output.");
}

void setup() {
  auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.setTextSize(2);
  M5.Display.println("STANDARD DIAG v2\nr: run\nNO ENABLE");
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64; g.tx_queue_len = 1;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e = twai_driver_install(&g, &t, &f);
  if (e == ESP_OK) e = twai_start();
  ready = e == ESP_OK;
  Serial.printf("CAN: %s. Send r for standard diagnostics; no enable on boot.\n", esp_err_to_name(e));
}

void loop() {
  M5.update();
  if (ready && Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') diagnose();
    if (c == 'x' || c == 'X') request(4, 0, "STOP");
  }
  if (ready && M5.BtnA.wasPressed()) request(4, 0, "BUTTON STOP");
  delay(1);
}
