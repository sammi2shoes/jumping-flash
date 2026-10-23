#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cmath>
#include <cstring>

constexpr uint8_t MOTOR = 1, HOST = 126;
constexpr uint32_t QUIET_MS = 1500;
constexpr uint32_t TIMEOUT_COUNTS = 20000;
static bool ready = false, busy = false, requested = false, aborted = false;
static bool quiet = false, foreignTraffic = false;
static uint32_t modeCount = 0;
static uint8_t mode = 255, faults = 0;
static uint32_t reportCount = 0, resetObservedAt = 0, quietBegan = 0;
static bool sawQuietRun = false;
static uint8_t wantedType = 0, wantedKind = 0;
static uint16_t wantedIndex = 0;
static bool readDone = false, readOK = false;
static uint32_t readBits = 0;

uint32_t floatBits(float value);
float bitsFloat(uint32_t bits);
void pump();
bool sendPacket(uint8_t type, const uint8_t* payload);
bool readParam(uint8_t type, uint16_t index, uint8_t kind, uint32_t& bits);
bool writeChecked(uint8_t type, uint16_t index, uint8_t kind, uint32_t bits);
bool awaitMode(uint32_t oldCount, uint8_t expected);
bool stopMotor();

void testWatchdog();

uint32_t floatBits(float value) { uint32_t bits; memcpy(&bits, &value, 4); return bits; }
float bitsFloat(uint32_t bits) { float value; memcpy(&value, &bits, 4); return value; }

void pump() {
    M5.update();
    if (M5.BtnA.wasPressed()) aborted = true;
    for (int n = 0; n < 32 && Serial.available(); ++n) {
        const char c = Serial.read();
        if (c == 'x' || c == 'X') aborted = true;
        if ((c == 't' || c == 'T') && !busy) requested = true;
    }
    twai_message_t f;
    for (int n = 0; n < 32 && twai_receive(&f, 0) == ESP_OK; ++n) {
        if (!f.extd || f.rtr || f.data_length_code != 8) {
            if (quiet) foreignTraffic = true;
            continue;
        }
        const uint8_t type = f.identifier >> 24;
        const bool senderIsMotor = ((f.identifier >> 8) & 255) == MOTOR;
        // Active reports may use the configured master address, not HOST.
        if (senderIsMotor && type == 2) {
            ++modeCount; ++reportCount;
            mode = (f.identifier >> 22) & 3;
            faults = (f.identifier >> 16) & 0x3F;
            if (faults) aborted = true;
            if (quiet) {
                if (mode == 2) sawQuietRun = true;
                if (mode == 0 && resetObservedAt == 0) resetObservedAt = millis();
            }
            continue;
        }
        const bool fromMotor = senderIsMotor && (f.identifier & 255) == HOST;
        if (!fromMotor) { if (quiet) foreignTraffic = true; continue; }
        if (type == 0x15) {
            const uint32_t faultMask = uint32_t(f.data[0]) | (uint32_t(f.data[1]) << 8) |
                (uint32_t(f.data[2]) << 16) | (uint32_t(f.data[3]) << 24);
            const uint32_t warningMask = uint32_t(f.data[4]) | (uint32_t(f.data[5]) << 8) |
                (uint32_t(f.data[6]) << 16) | (uint32_t(f.data[7]) << 24);
            Serial.printf("FAULT/WARNING fault=%08lX warning=%08lX\n", (unsigned long)faultMask, (unsigned long)warningMask);
            if (faultMask || warningMask) aborted = true;
        }
        if (type != wantedType || (uint16_t(f.data[0]) | (uint16_t(f.data[1]) << 8)) != wantedIndex) continue;
        readDone = true;
        readOK = type == 9 ? f.data[2] == wantedKind && f.data[3] == 0 : ((f.identifier >> 16) & 255) == 0;
        readBits = uint32_t(f.data[4]) | (uint32_t(f.data[5]) << 8) |
                   (uint32_t(f.data[6]) << 16) | (uint32_t(f.data[7]) << 24);
    }
}

bool sendPacket(uint8_t type, const uint8_t* payload) {
    if (quiet) { aborted = true; return false; }
    if (type != 3 && type != 4 && type != 17 && type != 18) return false;
    twai_message_t f = {};
    f.identifier = (uint32_t(type) << 24) | (uint32_t(HOST) << 8) | MOTOR;
    f.extd = 1; f.ss = 1; f.data_length_code = 8;
    if (payload) memcpy(f.data, payload, 8);
    const esp_err_t result = twai_transmit(&f, pdMS_TO_TICKS(10));
    if (result != ESP_OK) Serial.printf("TX ERROR: %s\n", esp_err_to_name(result));
    return result == ESP_OK;
}

bool readParam(uint8_t type, uint16_t index, uint8_t kind, uint32_t& bits) {
    pump();
    if (aborted) return false;
    wantedType = type; wantedIndex = index; wantedKind = kind;
    readDone = readOK = false;
    const uint8_t data[8] = {uint8_t(index), uint8_t(index >> 8), 0, 0, 0, 0, 0, 0};
    if (!sendPacket(type, data)) return false;
    const uint32_t began = millis();
    while (!readDone && !aborted && uint32_t(millis() - began) < 120) { pump(); delay(1); }
    wantedType = 0;
    if (aborted || !readDone || !readOK) { Serial.printf("READ FAILED type=%u index=%04X\n", type, index); return false; }
    bits = readBits;
    return true;
}

bool writeChecked(uint8_t type, uint16_t index, uint8_t kind, uint32_t bits) {
    if (aborted) return false;
    const uint8_t data[8] = {uint8_t(index), uint8_t(index >> 8), uint8_t(type == 8 ? kind : 0), 0,
        uint8_t(bits), uint8_t(bits >> 8), uint8_t(bits >> 16), uint8_t(bits >> 24)};
    if (!sendPacket(type, data)) return false;
    uint32_t actual;
    if (!readParam(type == 8 ? 9 : 0x11, index, kind, actual)) return false;
    const bool match = kind == 1 ? (actual & 255) == (bits & 255) : actual == bits;
    Serial.printf("VERIFY %04X: %s\n", index, match ? "OK" : "FAILED");
    return match;
}

bool awaitMode(uint32_t oldCount, uint8_t expected) {
    const uint32_t began = millis();
    while (modeCount == oldCount && uint32_t(millis() - began) < 120) { pump(); delay(1); }
    return modeCount != oldCount && mode == expected && faults == 0;
}

bool stopMotor() {
    pump();
    for (int n = 0; n < 3; ++n) {
        const uint32_t oldCount = modeCount;
        if (sendPacket(4, nullptr) && awaitMode(oldCount, 0)) {
            Serial.println("STOP confirmed."); return true;
        }
    }
    Serial.println("STOP NOT CONFIRMED: disconnect motor power.");
    return false;
}

bool zeroProbe(uint8_t expected, const char* label) {
    pump();
    if (aborted) return false;
    const uint8_t zero[8] = {0x0A, 0x70, 0, 0, 0, 0, 0, 0};
    const uint32_t oldCount = modeCount;
    const bool ok = sendPacket(0x12, zero) && awaitMode(oldCount, expected) && !aborted;
    Serial.printf("%s: %s (mode=%u faults=0x%02X)\n", label, ok ? "OK" : "FAILED", unsigned(mode), unsigned(faults));
    return ok;
}

bool trial(uint32_t timeoutCounts, uint8_t expectedAfterSilence) {
    Serial.printf("\nTRIAL timeout=%lu counts; expected post-gap mode=%u\n", (unsigned long)timeoutCounts, unsigned(expectedAfterSilence));
    bool ok = stopMotor();
    if (ok) ok = writeChecked(0x12, 0x7018, 6, floatBits(1.0f));
    if (ok) ok = writeChecked(0x12, 0x7028, 4, timeoutCounts);
    if (ok) ok = writeChecked(0x12, 0x7005, 1, 2);
    // The same probe must neither enable a disabled motor nor stop a running one.
    if (ok) ok = zeroProbe(0, "Disabled zero-probe control");
    uint32_t bits = 0;
    if (ok) ok = readParam(0x11, 0x700A, 6, bits) && std::isfinite(bitsFloat(bits)) && fabsf(bitsFloat(bits)) < 0.000001f;
    if (ok) ok = readParam(0x11, 0x7019, 6, bits) && std::isfinite(bitsFloat(bits));
    const float start = bitsFloat(bits);
    if (ok && !aborted) {
        pump(); const uint32_t oldCount = modeCount;
        ok = sendPacket(3, nullptr) && awaitMode(oldCount, 2) && !aborted;
        Serial.printf("Zero-speed enable: %s\n", ok ? "RUN confirmed" : "FAILED");
    } else ok = false;
    if (ok) ok = writeChecked(0x12, 0x7018, 6, floatBits(1.0f));
    if (ok) {
        ok = readParam(0x11, 0x7028, 4, bits) && bits == timeoutCounts;
        Serial.printf("Timeout retained after enable: %s\n", ok ? "YES" : "NO / READ FAILED");
    }
    if (ok) ok = zeroProbe(2, "RUN zero-probe control");
    if (ok) {
        Serial.println("1500 ms silence: no host CAN commands. x / A still requests stop.");
        quietBegan = millis(); resetObservedAt = 0; sawQuietRun = false;
        quiet = true;
        while (!aborted && uint32_t(millis() - quietBegan) < QUIET_MS) { pump(); delay(1); }
        quiet = false;
        ok = !aborted && !foreignTraffic;
        // No explicit stop has been sent yet. Solicit state with the validated zero probe.
        if (ok) ok = zeroProbe(expectedAfterSilence, "Post-gap zero-probe state");
    }
    if (ok) {
        ok = readParam(0x11, 0x7019, 6, bits) && std::isfinite(bitsFloat(bits));
        if (ok) {
            const float delta = bitsFloat(bits) - start;
            Serial.printf("Measured position change: %.6f rad\n", delta);
            ok = fabsf(delta) < 0.03f;
        }
    }
    quiet = false;
    const bool stopped = stopMotor();
    return ok && stopped && !aborted && !foreignTraffic;
}

void testWatchdog() {
    busy = true; requested = false; aborted = false; foreignTraffic = false;
    Serial.println("STANDARD WATCHDOG A/B v4: zero speed, 1 A limit.");
    Serial.println("Briefly tests timeout=0, then timeout=20000. No nonzero speed command.");
    bool controlOK = trial(0, 2);
    bool timeoutOK = false;
    if (controlOK) timeoutOK = trial(TIMEOUT_COUNTS, 0);
    // Leave a nonzero timeout configured even if the control trial fails.
    quiet = false;
    const bool stopped = stopMotor();
    const bool cancelled = aborted;
    bool restored = false;
    if (stopped) {
        // Honor cancellation by skipping every further enable; configuration only here.
        aborted = false;
        restored = writeChecked(0x12, 0x7028, 4, TIMEOUT_COUNTS);
    }
    const bool stoppedAgain = stopMotor();
    Serial.printf("CONTROL (timeout=0 remains RUN): %s\n", controlOK ? "PASS" : "NOT VERIFIED");
    Serial.printf("TIMEOUT (20000 counts becomes disabled): %s\n", timeoutOK ? "PASS" : "NOT VERIFIED");
    Serial.printf("WATCHDOG A/B RESULT: %s\n", controlOK && timeoutOK && stopped && restored && stoppedAgain && !cancelled && !aborted ? "PASS" : "NOT VERIFIED / ABORTED");
    if (foreignTraffic) Serial.println("Other CAN traffic invalidated this test; disconnect the USB-CAN adapter.");
    Serial.println("Final timeout requested: 20000 counts. Exact timing and loaded stopping remain unverified.");
    Serial.println(stoppedAgain ? "Final disabled feedback confirmed. Paste complete output." : "FINAL STOP UNCONFIRMED: disconnect motor power. Paste complete output.");
    busy = false; requested = false; aborted = false;
}

void setup() {
    auto cfg = M5.config(); M5.begin(cfg); Serial.begin(115200);
    M5.Display.setRotation(1); M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK); M5.Display.setCursor(0, 0);
    M5.Display.println("WATCHDOG A/B v4\nSerial: t\nx / A: STOP");
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32, GPIO_NUM_33, TWAI_MODE_NORMAL);
    config.rx_queue_len = 64; config.tx_queue_len = 1;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) result = twai_start();
    ready = result == ESP_OK;
    Serial.printf("CAN: %s. No enable on boot. Send t for zero-speed watchdog test.\n", esp_err_to_name(result));
}

void loop() {
    if (!ready) { delay(10); return; }
    pump();
    if (aborted) { stopMotor(); aborted = false; requested = false; }
    if (requested) testWatchdog();
    delay(1);
}
