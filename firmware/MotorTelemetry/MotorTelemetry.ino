#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cmath>
#include <cstring>

constexpr uint8_t MOTOR_ID = 1, HOST_ID = 126;
struct Parameter { uint16_t index; const char* name; const char* unit; uint8_t kind; };
// kind: 0 = little-endian float32; 1 = signed int16 / 10; 2 = uint32 mask.
const Parameter params[] = {
    {0x7019, "position", "rad", 0},
    {0x701B, "velocity", "rad/s", 0},
    {0x701C, "bus_voltage", "V", 0}
};
constexpr size_t PARAM_COUNT = sizeof(params) / sizeof(params[0]);
static size_t nextParameter = 0, pendingParameter = 0;
static bool canReady = false, waiting = false, haveReply = false;
static uint32_t sentMs = 0, lastSendMs = 0, lastReplyMs = 0, lastDisplayMs = 0;
static uint32_t replies = 0, timeouts = 0;

void requestParameter();
void receiveParameter(const twai_message_t& frame);

void requestParameter() {
    const Parameter& p = params[nextParameter];
    twai_message_t request = {};
    request.identifier = (0x11UL << 24) | (uint32_t(HOST_ID) << 8) | MOTOR_ID;
    request.extd = 1;
    request.ss = 1;
    request.data_length_code = 8;
    request.data[0] = p.index & 0xFF;
    request.data[1] = p.index >> 8;
    const esp_err_t result = twai_transmit(&request, 0);
    lastSendMs = millis();
    if (result == ESP_OK) {
        pendingParameter = nextParameter;
        sentMs = lastSendMs;
        waiting = true;
    } else {
        Serial.printf("# TX error %s index=0x%04X\n", esp_err_to_name(result), p.index);
    }
    nextParameter = (nextParameter + 1) % PARAM_COUNT;
}

void receiveParameter(const twai_message_t& frame) {
    // Reject unrelated motors, addresses, types, lengths and register indices.
    if (!waiting || !frame.extd || frame.rtr || frame.data_length_code != 8 ||
        (frame.identifier >> 24) != 0x11 ||
        (frame.identifier & 0xFFFF) != ((uint32_t(MOTOR_ID) << 8) | HOST_ID)) return;
    const Parameter& p = params[pendingParameter];
    const uint16_t index = uint16_t(frame.data[0]) | (uint16_t(frame.data[1]) << 8);
    if (index != p.index) return;
    waiting = false;
    const unsigned result = (frame.identifier >> 16) & 0xFF;
    if (result != 0) {
        Serial.printf("# READ error index=0x%04X code=%u\n", index, result);
        return;
    }
    const uint32_t bits = uint32_t(frame.data[4]) | (uint32_t(frame.data[5]) << 8) |
                          (uint32_t(frame.data[6]) << 16) | (uint32_t(frame.data[7]) << 24);
    char value[40];
    if (p.kind == 0) {
        float decoded;
        memcpy(&decoded, &bits, sizeof(decoded));
        if (!std::isfinite(decoded)) {
            Serial.printf("# Invalid float index=0x%04X\n", index);
            return;
        }
        snprintf(value, sizeof(value), "%.5f", decoded);
    } else if (p.kind == 1) {
        const int32_t raw = int32_t(bits & 0xFFFF);
        const int32_t signedRaw = raw >= 32768 ? raw - 65536 : raw;
        snprintf(value, sizeof(value), "%.1f", signedRaw / 10.0);
    } else {
        snprintf(value, sizeof(value), "0x%08lX", (unsigned long)bits);
    }
    lastReplyMs = millis();
    haveReply = true;
    ++replies;
    Serial.printf("%lu,%s,%s,%s\n", (unsigned long)lastReplyMs, p.name, value, p.unit);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
    config.rx_queue_len = 32;
    config.tx_queue_len = 1;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    canReady = result == ESP_OK;
    Serial.printf("# CAN: %s; motor=1; read-only polling; no enable or motion commands\n", esp_err_to_name(result));
    Serial.println("time_ms,parameter,value,unit");
}

void loop() {
    M5.update();
    if (canReady) {
        twai_message_t frame;
        for (int i = 0; i < 16 && twai_receive(&frame, 0) == ESP_OK; ++i) receiveParameter(frame);
        if (waiting && uint32_t(millis() - sentMs) >= 200) {
            Serial.printf("# TIMEOUT index=0x%04X; no fresh value\n", params[pendingParameter].index);
            ++timeouts;
            waiting = false;
        }
        if (!waiting && uint32_t(millis() - lastSendMs) >= 50) requestParameter();
    }
    if (uint32_t(millis() - lastDisplayMs) >= 250) {
        lastDisplayMs = millis();
        twai_status_info_t status = {};
        const bool driverOk = canReady && twai_get_status_info(&status) == ESP_OK && status.state == TWAI_STATE_RUNNING;
        M5.Display.setCursor(0, 0);
        M5.Display.printf("MOTOR 1 READ ONLY \n%-18s\nReplies: %-8lu\nTimeout: %-8lu\n",
            !driverOk ? "CAN ERROR" : haveReply && uint32_t(millis() - lastReplyMs) < 1000 ? "RECEIVING" : "NO FRESH DATA",
            (unsigned long)replies, (unsigned long)timeouts);
    }
    delay(1);
}
