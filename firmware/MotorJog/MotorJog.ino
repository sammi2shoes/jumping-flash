#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cmath>
#include <cstring>

constexpr uint8_t MOTOR = 1, HOST = 126;
constexpr float SPEED = 0.10f, CURRENT_LIMIT = 0.30f;
constexpr float MAX_TRAVEL = 0.15f; // rad, measured position bound
constexpr uint32_t DURATION_MS = 1000, WATCHDOG = 4000; // 4000 * 50 us = 200 ms
static bool ready = false, busy = false, startRequested = false, abortRequested = false;
static bool responseReady = false, responseOK = false;
static uint16_t wantedIndex = 0;
static uint32_t responseBits = 0;
static uint32_t modeReplyCount = 0;
static uint8_t lastMode = 255;

uint32_t floatBits(float value);
float bitsFloat(uint32_t bits);
bool sendFrame(uint8_t type, const uint8_t* data);
void service();
bool readRegister(uint16_t index, uint32_t& bits);
bool writeVerified(uint16_t index, uint32_t bits, bool floating);
bool stopMotor();
void runJog();

uint32_t floatBits(float value) { uint32_t bits; memcpy(&bits, &value, 4); return bits; }
float bitsFloat(uint32_t bits) { float value; memcpy(&value, &bits, 4); return value; }

bool sendFrame(uint8_t type, const uint8_t* data) {
    twai_message_t frame = {};
    frame.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
    frame.extd = 1; frame.ss = 1; frame.data_length_code = 8;
    if (data) memcpy(frame.data, data, 8);
    const esp_err_t result = twai_transmit(&frame, pdMS_TO_TICKS(10));
    if (result != ESP_OK) Serial.printf("TX ERROR: %s\n", esp_err_to_name(result));
    return result == ESP_OK;
}

void service() {
    M5.update();
    if (M5.BtnA.wasPressed()) abortRequested = true;
    for (int n = 0; n < 32 && Serial.available(); ++n) {
        const char c = Serial.read();
        if (c == 'x' || c == 'X') abortRequested = true;
        if ((c == 'g' || c == 'G') && !busy) startRequested = true;
    }
    twai_message_t f;
    for (int n = 0; n < 32 && twai_receive(&f, 0) == ESP_OK; ++n) {
        if (!f.extd || f.rtr || f.data_length_code != 8 ||
            (f.identifier & 0xFFFF) != ((uint32_t(MOTOR) << 8) | HOST)) continue;
        const uint8_t type = f.identifier >> 24;
        if (type == 2) {
            lastMode = (f.identifier >> 22) & 3;
            ++modeReplyCount;
            const uint8_t faults = (f.identifier >> 16) & 0x3F;
            if (faults) {
                Serial.printf("MOTOR FEEDBACK FAULT bits=0x%02X\n", faults);
                abortRequested = true;
            }
        }
        if (type == 0x15) {
            Serial.println("MOTOR FAULT FRAME received; aborting.");
            abortRequested = true;
        }
        const uint16_t index = uint16_t(f.data[0]) | (uint16_t(f.data[1]) << 8);
        if (type == 0x11 && index == wantedIndex) {
            responseOK = ((f.identifier >> 16) & 255) == 0;
            responseBits = uint32_t(f.data[4]) | (uint32_t(f.data[5]) << 8) |
                           (uint32_t(f.data[6]) << 16) | (uint32_t(f.data[7]) << 24);
            responseReady = true;
        }
    }
}

bool readRegister(uint16_t index, uint32_t& bits) {
    service();
    if (abortRequested) return false;
    wantedIndex = index; responseReady = false; responseOK = false;
    uint8_t data[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
    if (!sendFrame(0x11, data)) return false;
    const uint32_t began = millis();
    while (!responseReady && !abortRequested && uint32_t(millis() - began) < 120) {
        service(); delay(1);
    }
    if (abortRequested || !responseReady || !responseOK) {
        Serial.printf("READ FAILED index=0x%04X response=%u accepted=%u aborted=%u\n",
                      index, unsigned(responseReady), unsigned(responseOK), unsigned(abortRequested));
        return false;
    }
    bits = responseBits;
    return true;
}

bool writeVerified(uint16_t index, uint32_t bits, bool floating) {
    if (abortRequested) return false;
    uint8_t data[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0,
                      uint8_t(bits), uint8_t(bits >> 8), uint8_t(bits >> 16), uint8_t(bits >> 24)};
    if (!sendFrame(0x12, data)) return false;
    uint32_t actual;
    if (!readRegister(index, actual)) return false;
    const bool match = floating ? std::isfinite(bitsFloat(actual)) &&
        fabsf(bitsFloat(actual) - bitsFloat(bits)) < 0.0001f : actual == bits;
    Serial.printf("VERIFY 0x%04X: %s\n", index, match ? "OK" : "FAILED");
    return match;
}

bool stopMotor() {
    service(); // Drain already queued feedback before waiting for a stop reply.
    const uint32_t oldCount = modeReplyCount;
    for (int n = 0; n < 3; ++n) {
        sendFrame(4, nullptr); // Stop, all bytes zero: do not clear faults.
        const uint32_t began = millis();
        while (uint32_t(millis() - began) < 40) { service(); delay(1); }
        if (modeReplyCount != oldCount && lastMode == 0) {
            Serial.println("STOP confirmed: reset/disabled feedback."); return true;
        }
    }
    Serial.println("STOP NOT CONFIRMED: disconnect motor power.");
    return false;
}

void runJog() {
    busy = true; startRequested = false; abortRequested = false;
    Serial.println("Preparing one +0.10 rad/s jog for 1 second.");
    bool ok = stopMotor();
    // Configure and verify while disabled. Never save parameters to flash.
    if (ok) ok = writeVerified(0x7005, 2, false); // Velocity mode
    if (ok) ok = writeVerified(0x7018, floatBits(CURRENT_LIMIT), true);
    if (ok) ok = writeVerified(0x7028, WATCHDOG, false);
    if (ok) ok = writeVerified(0x700A, floatBits(0.0f), true);
    uint32_t bits = 0;
    if (ok) ok = readRegister(0x7019, bits);
    const float start = bitsFloat(bits);
    if (!std::isfinite(start)) ok = false;
    float position = start;
    uint32_t lastPosition = millis();
    if (ok && !abortRequested) {
        const uint32_t oldCount = modeReplyCount;
        ok = sendFrame(3, nullptr); // Enable with verified zero speed target.
        const uint32_t began = millis();
        while (ok && !abortRequested && modeReplyCount == oldCount && uint32_t(millis() - began) < 120) {
            service(); delay(1);
        }
        ok = ok && modeReplyCount != oldCount && lastMode == 2 && !abortRequested;
    } else ok = false;
    uint32_t began = millis(), lastCommand = began - 20, lastRead = began - 50;
    while (ok && !abortRequested && uint32_t(millis() - began) < DURATION_MS) {
        service();
        if (abortRequested) break;
        if (uint32_t(millis() - lastPosition) > 150) { ok = false; break; }
        if (uint32_t(millis() - lastRead) >= 50) {
            lastRead = millis();
            ok = readRegister(0x7019, bits);
            if (!ok || abortRequested) break;
            position = bitsFloat(bits); lastPosition = millis();
            Serial.printf("position=%.5f delta=%.5f rad\n", position, position - start);
            if (!std::isfinite(position) || fabsf(position - start) > MAX_TRAVEL) { ok = false; break; }
        }
        if (uint32_t(millis() - began) >= DURATION_MS) break;
        if (uint32_t(millis() - lastCommand) >= 20) {
            lastCommand = millis();
            const uint32_t speed = floatBits(SPEED);
            uint8_t data[8] = {0x0A, 0x70, 0, 0, uint8_t(speed), uint8_t(speed >> 8), uint8_t(speed >> 16), uint8_t(speed >> 24)};
            ok = sendFrame(0x12, data);
        }
        delay(1);
    }
    const bool stopped = stopMotor();
    Serial.printf("JOG %s; last measured delta=%.5f rad\n",
                  ok && !abortRequested && stopped ? "FINISHED" : "ABORTED", position - start);
    Serial.println("Idle. A new g is required for another jog. x or button A requests stop.");
    busy = false; startRequested = false;
}

void setup() {
    auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
    M5.Display.setRotation(1); M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK); M5.Display.setCursor(0, 0);
    M5.Display.println("UNLOADED JOG\nUSB: g = GO\nx / A = STOP");
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
    config.rx_queue_len = 32; config.tx_queue_len = 1;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    ready = result == ESP_OK;
    Serial.printf("CAN: %s. No motion on boot. g = one jog; x / button A = stop.\n", esp_err_to_name(result));
    if (ready) stopMotor();
}

void loop() {
    if (!ready) { delay(10); return; }
    service();
    if (abortRequested && !busy) { stopMotor(); abortRequested = false; startRequested = false; }
    if (startRequested) runJog();
    delay(1);
}
