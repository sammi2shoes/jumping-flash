#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>

constexpr uint8_t MOTOR = 1, HOST = 126;
bool ready = false, haveCommand = false, sawRun = false;
bool explicitStop = false, timedStop = false, invalid = false;
uint32_t lastCommand = 0, lastSummary = 0, lastFeedback = 0;
uint8_t lastMode = 255, lastFaults = 255;

void observe(const twai_message_t& f) {
  if (!f.extd || f.rtr || f.data_length_code != 8) return;
  const uint8_t type = (f.identifier >> 24) & 31;
  const uint8_t source = (f.identifier >> 8) & 255;
  const uint8_t dest = f.identifier & 255;
  const uint32_t now = millis();
  if (dest == MOTOR && source == HOST) {
    lastCommand = now; haveCommand = true;
    if (type == 4) explicitStop = true;
    if (type == 3) {
      Serial.println("EXPLICIT ENABLE observed.");
      if (timedStop) {
        invalid = true;
        Serial.println("ENABLE AFTER TIMEOUT: recovery test invalid. No restart was expected.");
      } else {
        sawRun = false; explicitStop = false; invalid = false;
      }
    }
    return;
  }
  if (dest == MOTOR && source != HOST && !(source == MOTOR && (type == 2 || type == 24))) {
    invalid = true;
    Serial.println("OTHER HOST TRAFFIC: timing result invalid.");
    return;
  }
  if (source == MOTOR && type == 21) {
    bool nonzero = false;
    for (unsigned i = 0; i < 8; ++i) if (f.data[i]) nonzero = true;
    if (nonzero) {
      invalid = true;
      Serial.println("NONZERO FAULT/WARNING: timing result invalid.");
    }
  }
  if (source != MOTOR || (type != 2 && type != 24)) return;
  lastFeedback = now;
  const uint8_t mode = (f.identifier >> 22) & 3;
  const uint8_t flags = (f.identifier >> 16) & 63;
  if (mode != lastMode || flags != lastFaults) {
    Serial.printf("MOTOR mode=%u flags=0x%02X at=%lu ms\n", unsigned(mode), unsigned(flags), (unsigned long)now);
    lastMode = mode; lastFaults = flags;
  }
  if (flags) invalid = true;
  if (mode == 2) {
    if (timedStop) {
      Serial.println("UNEXPECTED RUN AFTER TIMEOUT: DISCONNECT MOTOR POWER.");
      invalid = true;
    }
    sawRun = true;
  }
  if (mode == 0 && sawRun && !timedStop) {
    if (explicitStop) {
      Serial.println("Disabled after EXPLICIT STOP; not a watchdog observation.");
      sawRun = false;
    } else if (haveCommand) {
      const uint32_t elapsed = now - lastCommand;
      timedStop = true;
      Serial.printf("DISABLED feedback %lu ms after last observed host command.\n", (unsigned long)elapsed);
      Serial.println("No explicit stop observed since enable.");
      Serial.println(!invalid && elapsed >= 800 && elapsed <= 1400
        ? "TIMING consistent with the configured 1-second watchdog."
        : "TIMING NOT VERIFIED: inspect flags, traffic and delay.");
      Serial.println("Reconnect ONLY the USB-CAN branch after PC script exits. Leave script closed; watch 5 seconds.");
    }
  }
}

void setup() {
  auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.setTextSize(2);
  M5.Display.println("WATCHDOG OBSERVER\nNO CONTROL TX\nSTOP: power cut");
  // Normal CAN mode supplies ACKs so motor reports remain observable after
  // the other adapter is unplugged. No application frames are transmitted.
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
  g.rx_queue_len = 128; g.tx_queue_len = 0;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e = twai_driver_install(&g, &t, &f);
  if (e == ESP_OK) e = twai_start();
  ready = e == ESP_OK;
  Serial.printf("OBSERVER: %s. 1 Mbps. No application CAN transmissions.\n", esp_err_to_name(e));
  Serial.println("Button A does NOT stop the motor in this observer sketch. Use motor power cutoff.");
}

void loop() {
  M5.update();
  if (ready) {
    twai_message_t f;
    for (int n = 0; n < 64 && twai_receive(&f, 0) == ESP_OK; ++n) observe(f);
    if (uint32_t(millis() - lastSummary) >= 1000) {
      lastSummary = millis();
      twai_status_info_t s = {};
      if (twai_get_status_info(&s) == ESP_OK) {
        if (s.rx_missed_count || s.rx_error_counter || s.state != TWAI_STATE_RUNNING) invalid = true;
        const uint32_t feedbackAge = millis() - lastFeedback;
        if (timedStop && feedbackAge > 250) {
          invalid = true;
          Serial.println("STALE FEEDBACK: recovery state is NOT verified.");
        }
        Serial.printf("OBS mode=%u command_age=%lu ms feedback_age=%lu ms missed=%lu rx_err=%lu invalid=%u\n",
          unsigned(lastMode), (unsigned long)(haveCommand ? millis() - lastCommand : 0), (unsigned long)feedbackAge,
          (unsigned long)s.rx_missed_count, (unsigned long)s.rx_error_counter, unsigned(invalid));
      }
    }
  }
  delay(1);
}
