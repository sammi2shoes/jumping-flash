#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>

static bool ready = false;
static uint32_t frameCount = 0, lastDisplay = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(921600);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("PASSIVE CAPTURE\nNo CAN transmit");
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(
        GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_LISTEN_ONLY);
    config.tx_queue_len = 0;
    config.rx_queue_len = 128;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    ready = result == ESP_OK;
    Serial.printf("# Passive CAN capture: %s; 1 Mbps; TX32/RX33\n", esp_err_to_name(result));
    Serial.println("# Passive observation only. Any update is initiated separately in MotorStudio.");
    Serial.println("time_ms,id,extended,rtr,dlc,data");
}

void loop() {
    M5.update();
    if (ready) {
        twai_message_t frame;
        for (int n = 0; n < 16 && twai_receive(&frame, 0) == ESP_OK; ++n) {
            ++frameCount;
            char data[17] = {};
            if (!frame.rtr) {
                for (int i = 0; i < frame.data_length_code && i < 8; ++i)
                    snprintf(data + 2*i, 3, "%02X", frame.data[i]);
            }
            Serial.printf("%lu,%08lX,%u,%u,%u,%s\n",
                (unsigned long)millis(), (unsigned long)frame.identifier,
                unsigned(frame.extd), unsigned(frame.rtr),
                unsigned(frame.data_length_code), data);
        }
    }
    if (uint32_t(millis() - lastDisplay) >= 1000) {
        lastDisplay = millis();
        twai_status_info_t status = {};
        if (ready && twai_get_status_info(&status) == ESP_OK) {
            Serial.printf("# capture state=%d frames=%lu missed=%lu rx_err=%lu\n",
                int(status.state), (unsigned long)frameCount,
                (unsigned long)status.rx_missed_count,
                (unsigned long)status.rx_error_counter);
            M5.Display.setCursor(0, 0);
            M5.Display.printf("PASSIVE CAPTURE\nFrames: %-8lu\nMissed: %-8lu\n",
                (unsigned long)frameCount, (unsigned long)status.rx_missed_count);
        }
    }
    delay(1);
}
