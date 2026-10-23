#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>

// Wire by signal name; verify the Mini CAN connector order before connecting.
constexpr gpio_num_t CAN_TX = GPIO_NUM_32; // ESP32 output -> transceiver TXD
constexpr gpio_num_t CAN_RX = GPIO_NUM_33; // ESP32 input  <- transceiver RXD
constexpr uint8_t HOST_ID = 126;
static bool installed = false, running = false, ackMode = false;
static uint32_t frames = 0, lastStatus = 0;
static char command[24];
static size_t commandLength = 0;
static bool commandOverflow = false;

bool startCan(bool normal);
void handleCommand(const char* line);
void printFrame(const twai_message_t& frame);

bool startCan(bool normal) {
    running = false;
    if (installed) {
        twai_stop();
        const esp_err_t removed = twai_driver_uninstall();
        if (removed != ESP_OK) {
            Serial.printf("Uninstall failed: %s\n", esp_err_to_name(removed));
            return false;
        }
        installed = false;
    }
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(
        CAN_TX, CAN_RX, normal ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
    config.rx_queue_len = 32;
    config.tx_queue_len = normal ? 1 : 0;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t result = twai_driver_install(&config, &timing, &filter);
    if (result == ESP_OK) {
        installed = true;
        result = twai_start();
    }
    running = result == ESP_OK;
    ackMode = normal && running;
    Serial.printf("CAN start: %s; mode=%s\n", esp_err_to_name(result),
                  normal ? "NORMAL (ACK enabled)" : "PASSIVE (no ACK)");
    return running;
}

void handleCommand(const char* line) {
    if (strcmp(line, "l") == 0) { startCan(false); return; }
    if (strcmp(line, "a") == 0) { startCan(true); return; }
    int id = -1;
    char extra;
    if (sscanf(line, "q %d %c", &id, &extra) == 1 && id >= 0 && id <= 127) {
        if (!ackMode || !running) {
            Serial.println("Select a (ACK mode) before an identification query.");
            return;
        }
        twai_message_t query = {};
        // RobStride private protocol type 0: get ID/UID, all payload bytes zero.
        // No enable, movement, parameter write, or reporting-mode commands exist here.
        query.identifier = (uint32_t(HOST_ID) << 8) | uint32_t(id);
        query.extd = 1;
        query.ss = 1; // Single shot: do not retry forever if no node acknowledges.
        query.data_length_code = 8;
        const esp_err_t result = twai_transmit(&query, 0);
        Serial.printf("Identification request to ID %d queued: %s\n",
                      id, esp_err_to_name(result));
        Serial.println("Queued is not proof of delivery; look for an ID/UID reply.");
        return;
    }
    Serial.println("Commands: l = passive; a = ACK mode; q 1 = one ID/UID query to motor 1.");
}

void printFrame(const twai_message_t& frame) {
    ++frames;
    Serial.printf("RX id=%08lX ext=%u rtr=%u dlc=%u data=",
                  (unsigned long)frame.identifier, unsigned(frame.extd),
                  unsigned(frame.rtr), unsigned(frame.data_length_code));
    if (!frame.rtr) {
        for (uint8_t i = 0; i < frame.data_length_code && i < 8; ++i) {
            Serial.printf("%02X", frame.data[i]);
        }
    }
    Serial.println();
    // Decode only the unambiguous type-0 private-protocol response envelope.
    if (frame.extd && !frame.rtr && frame.data_length_code == 8 &&
        (frame.identifier >> 24) == 0 && (frame.identifier & 0xFF) == 0xFE) {
        Serial.printf("ID/UID reply: motor_id=%u (UID bytes printed above)\n",
                      unsigned((frame.identifier >> 8) & 0xFF));
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    Serial.println("CAN bench: 1 Mbps; TX=32 RX=33; motor-control commands absent.");
    Serial.println("Starts passive. Silence is expected if the motor is not reporting.");
    handleCommand("help");
    startCan(false);
}

void loop() {
    M5.update();
    // Bounded work per loop so a busy bus cannot starve other servicing.
    for (int i = 0; i < 32 && Serial.available(); ++i) {
        const char c = char(Serial.read());
        if (c == '\r' || c == '\n') {
            if (commandOverflow) Serial.println("Command too long; ignored.");
            else if (commandLength) {
                command[commandLength] = 0;
                handleCommand(command);
            }
            commandLength = 0;
            commandOverflow = false;
        } else if (commandLength < sizeof(command) - 1) {
            command[commandLength++] = c;
        } else commandOverflow = true;
    }
    if (running) {
        twai_message_t frame;
        for (int i = 0; i < 8 && twai_receive(&frame, 0) == ESP_OK; ++i) printFrame(frame);
    }
    if (uint32_t(millis() - lastStatus) >= 1000) {
        lastStatus = millis();
        twai_status_info_t status = {};
        const esp_err_t result = installed ? twai_get_status_info(&status) : ESP_FAIL;
        if (result == ESP_OK) {
            Serial.printf("STATUS state=%d rx=%lu rx_err=%lu tx_err=%lu missed=%lu tx_failed=%lu\n",
                          int(status.state), (unsigned long)frames,
                          (unsigned long)status.rx_error_counter,
                          (unsigned long)status.tx_error_counter,
                          (unsigned long)status.rx_missed_count,
                          (unsigned long)status.tx_failed_count);
        }
        M5.Display.setCursor(0, 0);
        M5.Display.printf("CAN BENCH          \n%s              \nRX: %-10lu\nDriver: %-8s\n",
                          ackMode ? "ACK MODE" : "PASSIVE", (unsigned long)frames,
                          result == ESP_OK ? status.state == TWAI_STATE_RUNNING ? "RUNNING" : "FAULT" : "ERROR");
    }
    delay(1);
}
