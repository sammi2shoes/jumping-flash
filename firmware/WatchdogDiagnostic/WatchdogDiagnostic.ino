#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cstring>

constexpr uint8_t MOTOR = 1, HOST = 126;
static bool ready = false, finished = false;

void transaction(uint8_t type, const uint8_t* payload);
void probe(uint16_t index);

void transaction(uint8_t type, const uint8_t* payload) {
    twai_message_t incoming;
    for (int i = 0; i < 64 && twai_receive(&incoming, 0) == ESP_OK; ++i) {}
    twai_message_t request = {};
    request.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
    request.extd = 1; request.ss = 1; request.data_length_code = 8;
    if (payload) memcpy(request.data, payload, 8);
    const esp_err_t result = twai_transmit(&request, pdMS_TO_TICKS(10));
    Serial.printf("TX type=%u: %s\n", unsigned(type), esp_err_to_name(result));
    const uint32_t began = millis();
    bool received = false;
    while (uint32_t(millis() - began) < 300) {
        M5.update();
        for (int i = 0; i < 16 && twai_receive(&incoming, 0) == ESP_OK; ++i) {
            received = true;
            Serial.printf("RX id=%08lX ext=%u rtr=%u dlc=%u data=",
                (unsigned long)incoming.identifier, unsigned(incoming.extd),
                unsigned(incoming.rtr), unsigned(incoming.data_length_code));
            if (!incoming.rtr) {
                for (int b = 0; b < incoming.data_length_code && b < 8; ++b)
                    Serial.printf("%02X", incoming.data[b]);
            }
            Serial.println();
            if (!incoming.extd || incoming.rtr || incoming.data_length_code != 8) continue;
            const uint8_t kind = incoming.identifier >> 24;
            const bool fromMotor = ((incoming.identifier >> 8) & 255) == MOTOR;
            if (kind == 0x11 && fromMotor && (incoming.identifier & 255) == HOST) {
                const uint16_t index = uint16_t(incoming.data[0]) | (uint16_t(incoming.data[1]) << 8);
                const uint8_t status = (incoming.identifier >> 16) & 255;
                const uint32_t bits = uint32_t(incoming.data[4]) | (uint32_t(incoming.data[5]) << 8) |
                    (uint32_t(incoming.data[6]) << 16) | (uint32_t(incoming.data[7]) << 24);
                Serial.printf("READ index=0x%04X status=%u raw=0x%08lX uint32=%lu\n",
                    index, unsigned(status), (unsigned long)bits, (unsigned long)bits);
            }
            if (kind == 2 && fromMotor && incoming.data[0] == 0 &&
                incoming.data[1] == 0xC4 && incoming.data[2] == 0x56) {
                Serial.printf("VERSION bytes=%02X.%02X.%02X.%02X (raw, interpretation firmware-dependent)\n",
                    incoming.data[3], incoming.data[4], incoming.data[5], incoming.data[6]);
            }
        }
        delay(1);
    }
    if (!received) Serial.println("No received frames in this 300 ms window.");
}

void probe(uint16_t index) {
    Serial.printf("\nPROBE read 0x%04X\n", index);
    uint8_t payload[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
    transaction(0x11, payload);
}

void setup() {
    auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
    M5.Display.setRotation(1); M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK); M5.Display.setCursor(0, 0);
    M5.Display.println("READ DIAGNOSTIC\nNo motor enable");
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
    config.rx_queue_len = 32; config.tx_queue_len = 1;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    ready = result == ESP_OK;
    Serial.printf("CAN: %s. Diagnostics start in 3 seconds.\n", esp_err_to_name(result));
}

void loop() {
    M5.update();
    if (ready && !finished && millis() >= 3000) {
        finished = true;
        Serial.println("STOP request (no fault clearing)");
        transaction(4, nullptr);
        Serial.println("\nFirmware version request");
        uint8_t version[8] = {0, 0xC4, 0, 0, 0, 0, 0, 0};
        transaction(4, version);
        probe(0x7028);
        probe(0x200C);
        Serial.println("\nDONE. No enable or parameter-write commands were sent.");
    }
    delay(1);
}
