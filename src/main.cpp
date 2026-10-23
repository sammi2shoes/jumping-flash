#include <Arduino.h>
#include <M5Unified.h>
#include <esp_timer.h>
#if ENABLE_BLUEPAD32
#include <Bluepad32.h>
ControllerPtr pad = nullptr;
uint32_t lastReportMs = 0;
void connected(ControllerPtr candidate) {
    if (!pad && candidate->isGamepad()) {
        pad = candidate;
        lastReportMs = 0;
        Serial.printf("# controller connected: %s\n", candidate->getModelName().c_str());
    }
}
void disconnected(ControllerPtr candidate) {
    if (pad == candidate) { pad = nullptr; lastReportMs = 0; }
}
#endif

// Diagnostics only: no CAN driver, motor commands, or weapon output.
static uint32_t lastSampleMs = 0, lastDisplayMs = 0;
static uint64_t sampleUs = 0;
static float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
static bool haveSample = false;
static uint32_t skippedLogs = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    Serial.println("# jumping-flash diagnostics; motors disabled");
#if ENABLE_BLUEPAD32
    BP32.setup(&connected, &disconnected);
    BP32.enableVirtualDevice(false);
    // Retain pairing keys across boots; use button A to clear explicitly.
#endif
    Serial.println("# units: accel=g, gyro=deg/s; device-native axes; no attitude estimate");
    Serial.println("sample_us,imu_fresh,ax,ay,az,gx,gy,gz,pad_connected,pad_fresh,lx,ly,rx,ry,buttons,skipped_logs");
}

void loop() {
    M5.update();
    const uint32_t now = millis();
    bool padConnected = false, padFresh = false;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    unsigned buttons = 0;
#if ENABLE_BLUEPAD32
    const bool controllerUpdated = BP32.update();
    if (M5.BtnA.wasPressed()) {
        BP32.forgetBluetoothKeys();
        Serial.println("# pairing keys erased; restart device and put controller in pairing mode");
    }
    padConnected = pad && pad->isConnected();
    if (controllerUpdated && padConnected && pad->hasData()) lastReportMs = now;
    padFresh = padConnected && lastReportMs != 0 && uint32_t(now - lastReportMs) < 250;
    if (padFresh) {
        lx = pad->axisX(); ly = pad->axisY();
        rx = pad->axisRX(); ry = pad->axisRY(); buttons = pad->buttons();
    }
#endif
    if (uint32_t(now - lastSampleMs) >= 10) {
        lastSampleMs = now;
        bool fresh = M5.Imu.isEnabled() && M5.Imu.update();
        if (fresh) {
            const auto data = M5.Imu.getImuData();
            ax = data.accel.x; ay = data.accel.y; az = data.accel.z;
            gx = data.gyro.x; gy = data.gyro.y; gz = data.gyro.z;
            sampleUs = esp_timer_get_time(); haveSample = true;
        }
        char row[240];
        const int n = snprintf(row, sizeof(row),
            "%llu,%d,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%u,%lu\n",
            (unsigned long long)sampleUs, fresh, ax, ay, az, gx, gy, gz,
            padConnected, padFresh, lx, ly, rx, ry, buttons, (unsigned long)skippedLogs);
        // Avoid blocking sensor/controller servicing when the serial link is full.
        if (n > 0 && n < int(sizeof(row)) && Serial.availableForWrite() >= n) Serial.write((uint8_t*)row, n);
        else ++skippedLogs;
    }
    if (uint32_t(now - lastDisplayMs) >= 200) {
        lastDisplayMs = now;
        M5.Display.setCursor(0, 0);
        M5.Display.printf("DIAGNOSTICS ONLY   \nIMU: %-12s\n",
            !M5.Imu.isEnabled() ? "MISSING" : haveSample && uint64_t(esp_timer_get_time()) - sampleUs < 100000 ? "OK" : "STALE");
        M5.Display.printf("PAD: %-12s\n", padFresh ? "DATA OK" : padConnected ? "STALE" : "DISCONNECTED");
        M5.Display.printf("aZ: %7.3fg    \ngZ: %7.2fd/s  \n", az, gz);
        M5.Display.printf("CAN: OFF          \n");
    }
    delay(1);
}
