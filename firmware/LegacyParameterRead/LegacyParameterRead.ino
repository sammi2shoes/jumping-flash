#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>

constexpr uint8_t MOTOR = 1, HOST = 126;
static bool ready = false, finished = false;
static char summary[5][160] = {};
static size_t summaryCount = 0;

void readLegacy(uint16_t index, const char* name);

void readLegacy(uint16_t index, const char* name) {
    if (summaryCount >= 5) return;
    const size_t slot = summaryCount++;
    snprintf(summary[slot], sizeof(summary[slot]), "%s index=0x%04X: NO MATCHING REPLY", name, index);
    twai_message_t frame;
    for (int n = 0; n < 64 && twai_receive(&frame, 0) == ESP_OK; ++n) {}
    twai_message_t request = {};
    // Same type-9 request layout captured from MotorStudio; only host ID differs.
    request.identifier = (9UL << 24) | (uint32_t(HOST) << 8) | MOTOR;
    request.extd = 1;
    request.ss = 1;
    request.data_length_code = 8;
    request.data[0] = index & 255;
    request.data[1] = index >> 8;
    Serial.printf("\nREAD %s index=0x%04X\n", name, index);
    const esp_err_t result = twai_transmit(&request, pdMS_TO_TICKS(10));
    Serial.printf("TX id=%08lX result=%s\n", (unsigned long)request.identifier, esp_err_to_name(result));
    if (result != ESP_OK) {
        snprintf(summary[slot], sizeof(summary[slot]), "%s index=0x%04X: TX ERROR %s", name, index, esp_err_to_name(result));
        return;
    }
    bool matched = false;
    const uint32_t began = millis();
    while (uint32_t(millis() - began) < 400) {
        M5.update();
        for (int n = 0; n < 16 && twai_receive(&frame, 0) == ESP_OK; ++n) {
            Serial.printf("RX id=%08lX ext=%u rtr=%u dlc=%u data=",
                (unsigned long)frame.identifier, unsigned(frame.extd),
                unsigned(frame.rtr), unsigned(frame.data_length_code));
            if (!frame.rtr) {
                for (int b = 0; b < frame.data_length_code && b < 8; ++b)
                    Serial.printf("%02X", frame.data[b]);
            }
            Serial.println();
            if (!frame.extd || frame.rtr || frame.data_length_code != 8 ||
                frame.identifier != ((9UL << 24) | (uint32_t(MOTOR) << 8) | HOST)) continue;
            const uint16_t returnedIndex = uint16_t(frame.data[0]) | (uint16_t(frame.data[1]) << 8);
            if (returnedIndex != index) continue;
            matched = true;
            const uint32_t raw = uint32_t(frame.data[4]) | (uint32_t(frame.data[5]) << 8) |
                (uint32_t(frame.data[6]) << 16) | (uint32_t(frame.data[7]) << 24);
            if (frame.data[3] == 0) {
                snprintf(summary[slot], sizeof(summary[slot]),
                    "%s index=0x%04X header=%02X,%02X data=%02X%02X%02X%02X",
                    name, index, frame.data[2], frame.data[3],
                    frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
            }
            // Record the header without assuming type/status/scaling semantics.
            Serial.printf("MATCH index=0x%04X header=%02X,%02X raw=0x%08lX unsigned=%lu\n",
                index, frame.data[2], frame.data[3], (unsigned long)raw, (unsigned long)raw);
        }
        delay(1);
    }
    if (!matched) Serial.println("NO MATCHING TYPE-9 REPLY; no value assumed.");
}

void setup() {
    auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
    M5.Display.setRotation(1); M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK); M5.Display.setCursor(0, 0);
    M5.Display.println("LEGACY READ TEST\nNo motor enable");
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
    config.rx_queue_len = 64; config.tx_queue_len = 1;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    ready = result == ESP_OK;
    Serial.printf("CAN: %s; motor=1 host=126; begins in 3 seconds\n", esp_err_to_name(result));
}

void loop() {
    M5.update();
    if (ready && !finished && millis() >= 3000) {
        finished = true;
        readLegacy(0x200C, "CAN_TIMEOUT");
        readLegacy(0x2019, "current_limit");
        readLegacy(0x300C, "bus_voltage");
        readLegacy(0x301F, "board_temperature");
        readLegacy(0x3022, "fault_status");
        Serial.println("\n=== COPY THIS FULL SUMMARY ===");
        for (size_t i = 0; i < summaryCount; ++i) Serial.println(summary[i]);
        Serial.println("=== END SUMMARY ===");
        Serial.println("DONE. Only type-9 reads were sent; no writes or enables.");
    }
    delay(1);
}
