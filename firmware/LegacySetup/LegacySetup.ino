#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cstring>

constexpr uint8_t MOTOR = 1, HOST = 126;
constexpr uint32_t WATCHDOG_TICKS = 4000; // Manual: 20000 ticks = 1 second.
static bool ready = false;

bool sendPacket(uint8_t type, const uint8_t* bytes);
bool readValue(uint16_t index, uint8_t expectedKind, uint32_t& value);
bool writeValue(uint16_t index, uint8_t kind, uint32_t value);
bool confirmStop();
void configure();

bool sendPacket(uint8_t type, const uint8_t* bytes) {
    twai_message_t frame = {};
    frame.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
    frame.extd = 1; frame.ss = 1; frame.data_length_code = 8;
    if (bytes) memcpy(frame.data, bytes, 8);
    const esp_err_t result = twai_transmit(&frame, pdMS_TO_TICKS(10));
    Serial.printf("TX %08lX ", (unsigned long)frame.identifier);
    for (int i = 0; i < 8; ++i) Serial.printf("%02X", frame.data[i]);
    Serial.printf(" %s\n", esp_err_to_name(result));
    return result == ESP_OK;
}

bool readValue(uint16_t index, uint8_t expectedKind, uint32_t& value) {
    twai_message_t frame;
    for (int n = 0; n < 64 && twai_receive(&frame, 0) == ESP_OK; ++n) {}
    const uint8_t data[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
    if (!sendPacket(9, data)) return false;
    const uint32_t began = millis();
    while (uint32_t(millis() - began) < 200) {
        M5.update();
        for (int n = 0; n < 16 && twai_receive(&frame, 0) == ESP_OK; ++n) {
            if (!frame.extd || frame.rtr || frame.data_length_code != 8 ||
                frame.identifier != ((9UL << 24) | (uint32_t(MOTOR) << 8) | HOST)) continue;
            if ((uint16_t(frame.data[0]) | (uint16_t(frame.data[1]) << 8)) != index) continue;
            if (frame.data[2] != expectedKind || frame.data[3] != 0) {
                Serial.printf("READ HEADER MISMATCH index=%04X header=%02X,%02X\n", index, frame.data[2], frame.data[3]);
                return false;
            }
            value = uint32_t(frame.data[4]) | (uint32_t(frame.data[5]) << 8) |
                    (uint32_t(frame.data[6]) << 16) | (uint32_t(frame.data[7]) << 24);
            Serial.printf("READ index=%04X kind=%u bits=%08lX\n", index, unsigned(expectedKind), (unsigned long)value);
            return true;
        }
        delay(1);
    }
    Serial.printf("READ TIMEOUT index=%04X\n", index);
    return false;
}

bool writeValue(uint16_t index, uint8_t kind, uint32_t value) {
    // Legacy MotorStudio type 8: index LE16, value kind, fragment 0, value LE32.
    const uint8_t bytes[8] = {uint8_t(index), uint8_t(index >> 8), kind, 0,
        uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
    if (!sendPacket(8, bytes)) return false;
    uint32_t actual;
    if (!readValue(index, kind, actual)) return false;
    const bool match = actual == value;
    Serial.printf("VERIFY index=%04X %s\n", index, match ? "OK" : "FAILED");
    return match;
}

bool confirmStop() {
    twai_message_t frame;
    for (int n = 0; n < 64 && twai_receive(&frame, 0) == ESP_OK; ++n) {}
    if (!sendPacket(4, nullptr)) return false;
    const uint32_t began = millis();
    while (uint32_t(millis() - began) < 200) {
        M5.update();
        for (int n = 0; n < 16 && twai_receive(&frame, 0) == ESP_OK; ++n) {
            if (!frame.extd || frame.rtr || frame.data_length_code != 8 ||
                (frame.identifier >> 24) != 2 ||
                (frame.identifier & 0xFFFF) != ((uint32_t(MOTOR) << 8) | HOST)) continue;
            const unsigned mode = (frame.identifier >> 22) & 3;
            const unsigned faults = (frame.identifier >> 16) & 0x3F;
            Serial.printf("STOP reply: mode=%u faults=0x%02X\n", mode, faults);
            return mode == 0 && faults == 0;
        }
        delay(1);
    }
    Serial.println("STOP NOT CONFIRMED; configuration cancelled.");
    return false;
}

void configure() {
    Serial.println("CONFIGURE ONLY: no enable or movement commands.");
    if (!confirmStop()) return;
    uint32_t currentBefore = 0, watchdogBefore = 0;
    if (!readValue(0x2019, 6, currentBefore) || !readValue(0x200C, 4, watchdogBefore)) return;
    const float limit = 0.30f;
    uint32_t currentTarget;
    memcpy(&currentTarget, &limit, 4);
    bool ok = writeValue(0x2019, 6, currentTarget);
    if (ok) ok = writeValue(0x200C, 4, WATCHDOG_TICKS);
    uint32_t currentFinal = 0, watchdogFinal = 0;
    if (ok) ok = readValue(0x2019, 6, currentFinal) && readValue(0x200C, 4, watchdogFinal);
    if (ok) ok = currentFinal == currentTarget && watchdogFinal == WATCHDOG_TICKS;
    const bool stopped = confirmStop();
    Serial.printf("CONFIGURATION %s\n", ok && stopped ? "READBACK VERIFIED" : "FAILED / INCOMPLETE");
    if (ok && stopped) {
        Serial.println("Current limit=0.30 A; CAN timeout=4000 ticks (200 ms).");
        Serial.println("Motor remains stopped. Watchdog STOP BEHAVIOR is not yet tested.");
    } else Serial.println("Do not enable. Partial settings may have been applied; retain this log.");
    Serial.println("No parameter-save, calibration, erase or enable command was sent.");
}

void setup() {
    auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
    M5.Display.setRotation(1); M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK); M5.Display.setCursor(0, 0);
    M5.Display.println("LEGACY SETUP\nNO MOTOR ENABLE\nSerial: c");
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
    config.rx_queue_len = 64; config.tx_queue_len = 1;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    ready = result == ESP_OK;
    Serial.printf("CAN: %s. Send c to configure/read back 0.30 A and 200 ms.\n", esp_err_to_name(result));
}

void loop() {
    M5.update();
    if (ready && Serial.available()) {
        const char c = Serial.read();
        if (c == 'c' || c == 'C') {
            configure();
            while (Serial.available()) Serial.read();
        }
    }
    delay(1);
}
