# RS05 CAN bench bring-up

## Sources actually inspected

- [RobStride official product repository](https://github.com/RobStride/Product_Information), commit `384cc561e1ff079c22f68b19908526094430febd`.
- [RS05 CAN 2.0 English manual, 260713](https://github.com/RobStride/Product_Information/blob/384cc561e1ff079c22f68b19908526094430febd/Product%20Literature/CAN2.0/RS05/RS05User%20Manual260713.pdf), printed pp. 12, 43–44, 49 and parameter CAN_ID at 0x200a.
- [Manufacturer product specifications](https://github.com/RobStride/Product_Information/blob/384cc561e1ff079c22f68b19908526094430febd/README.md): RS05 operating voltage 15–60 V; 48 V rated. 24 V is within the operating range; 48 V performance specifications do not establish the same performance at 24 V.
- [M5StickC Plus Grove pin map](https://github.com/m5stack/m5-docs/blob/e5354c5dcc61d16fdc1dff9d92c2c760728d0b60/docs/en/core/m5stickc_plus.md): external Grove GPIO32/33, power and ground. The internal IMU uses GPIO21/22, so it does not share this external port.
- [M5 official TWAI examples](https://github.com/m5stack/M5Unit-CAN/tree/4656a2adc0d586f3904a96b34334ce6fd5b0b0ad/examples). These establish the driver approach, not the Mini CAN connector ordering.

The RS05 documentation specifies 1 Mbps classical CAN, 29-bit extended identifiers and 8-byte messages for the private protocol. Factory CAN ID is listed as 1, but may have been changed. Other protocol modes and a distinct CAN-FD product variant exist: do not assume this sketch applies to CAN-FD mode. ESP32 TWAI does not implement CAN FD.

The English motor manual names the XT30(2+2) connector but its interface-definition section does not show a usable connector-contact diagram. M5Stack's live Mini CAN page and shop were denied by the environment's egress proxy. Therefore Mini CAN Grove ordering, onboard termination, and physical motor connector contact assignments are **not verified**. Do not energize a guessed wiring arrangement. Resolve these with manufacturer drawings or clearly marked actual hardware before connection.

## Signal mapping expected by this sketch

These are signal requirements, not verified connector positions or cable colors:

| Endpoint | Required connection |
|---|---|
| ESP32 GPIO32 | Mini CAN transceiver TXD input |
| ESP32 GPIO33 | Mini CAN transceiver RXD output |
| M5 logic supply | Mini CAN's specified module supply; verify its marking/schematic |
| Mini CAN CANH | Motor CANH |
| Mini CAN CANL | Motor CANL |
| M5/module ground | Motor power-supply negative / signal reference, for this nonisolated interface |
| 24 V supply positive | Motor power positive only |

Never connect the 24 V rail to the M5/Grove/module logic supply. Verify supply polarity and connector orientation with actual labels/drawings. Keep the first motor mechanically unloaded, without the spring-loaded linkage attached.

With every device unpowered and supplies disconnected, check CANH-to-CANL resistance. A correctly terminated two-ended bus with two 120 ohm terminations reads approximately 60 ohms. Account for existing resistors before adding any. A resistance reading is not proof of correct polarity, supply wiring, or connector mapping.

## Firmware / Arduino IDE

Keep the known-good IMU/controller sketch separately. Open a new Arduino sketch named CanBench and paste `firmware/CanBench/CanBench.ino` into it. It uses the same M5Unified/M5GFX installation and board settings, plus the ESP32's bundled `driver/twai.h` (no new CAN library). This test intentionally replaces the IMU/Bluetooth diagnostics while running.

Cloud compile: `scripts/build.sh -e can-bench`.

1. After wiring is verified, upload with motor power off. Serial Monitor: 115200 baud; select Newline or Both NL & CR.
2. It starts in true LISTEN_ONLY mode: no outgoing data frames and no ACKs. Status RUNNING establishes driver initialization only. It does not establish bus connectivity.
3. Power the one motor from 24 V. Silence is expected on a factory-configured motor: active reporting is disabled by default. An isolated transmitter may also retry or fault because a passive listener does not ACK. Do not diagnose silence as a broken bus.
4. Send `a` and Enter to enable normal-mode ACKs. This sends no application command by itself. It allows this node to acknowledge received CAN traffic.
5. On a known RS05 using the private protocol, send `q 1` and Enter for a one-shot type-0 identification request to ID 1 (use its actual ID if changed). No enable, motion, reporting-mode change, stop, or parameter-write command is present in this sketch. It does not clear faults or automatically recover bus-off.
6. A successful exchange should print a received extended type-0 reply with low address byte FE and a decoded `motor_id`. The eight data bytes carry the UID. A successful queue operation alone is not delivery or a response.
7. Send `l` and Enter to return to passive mode. Status exposes driver state, received frame count, error counters, dropped frames and failed transmissions. Unknown/protocol-mismatched frames are logged raw, not interpreted as RS05 torque/position.

If there is no reply, save the raw log and check wiring, termination, exact model/protocol mode, ID and bitrate before changing motor settings. Physical operation remains untested in the cloud. There is no motor-control or emergency-stop implementation in this diagnostic sketch.

## Confirmed hardware bring-up

The user's actual photo identifies a **StickC Plus2**, with Grove GPIO32 (yellow) and GPIO33 (white). The Mini CAN label identifies yellow T/TXD and white R/RXD, matching this firmware. The bus-terminal label reads H, L, HV, G. The function/rating of HV has not been verified; no change to the user's previously working power wiring is requested.

The user reports a successful exchange: `RX id=000001FE ext=1 rtr=0 dlc=8 data=54063B8A00743013`, decoded motor ID 1; status `state=1` with zero RX/TX errors, missed frames and failed transmissions. This establishes the user's existing bus wiring and identification exchange; motor motion remains untested.

## Read-only motor telemetry

Open `firmware/MotorTelemetry/MotorTelemetry.ino` as a separate Arduino IDE sketch, retaining the known-good controller and CAN bench sketches. Use the same working board/core/library settings. Build target: `scripts/build.sh -e motor-telemetry`.

It automatically polls motor ID 1 using only type-17 parameter reads, with one outstanding request and a 200 ms response timeout. Up to 20 requests/s are sent, cycling through the verified position (0x7019, float32 rad), velocity (0x701B, float32 rad/s), and bus voltage (0x701C, float32 V) registers. These three registers are in manual section 4.1.14. The [official Python SDK](https://github.com/RobStride/Python_Sample/blob/cbf977e56c842d57a65f3f17c1b1ecaef002c424/robstride_dynamics/bus.py) confirms the type-17 little-endian register/data format. The initial test also tried temperature (0x301F) and fault status (0x3022) from the upper-computer parameter table. This motor rejected them with read status 1; they are not polled now.

No enable, stop, control-mode change, reporting switch, or parameter-write command is sent. The sketch does not independently verify or enforce a disabled motor state; use the existing ID-only bench setup with the unloaded motor. It replaces the IMU/Bluetooth sketch while running and does not log IMU or controller channels.

Serial at 115200 baud emits `time_ms,parameter,value,unit`, one timestamped line per successful response. These are sequential measurements, not one synchronized sample. Temperature and fault status are not currently measured; successful position/voltage reads do not establish absence of motor faults. Errors/timeouts begin with `#`; the display shows whether any valid parameter replies are arriving, not freshness of every parameter. Motor disconnect should produce timeout messages, not fresh readings. No electrical fault test or live motor operation has been performed in the cloud.

Host checks exercise the actual sketch with simulated Arduino/CAN interfaces:

```sh
g++ -std=c++17 -Itests/telemetry-host tests/telemetry-host/test.cpp -o /tmp/jumping-flash-telemetry-test
/tmp/jumping-flash-telemetry-test
```

These verify read-only request encoding, response filtering, little-endian float32 decoding, invalid/error replies and response timeouts. They do not validate the actual hardware's register availability.

### On-device telemetry result

The user verified position (0x7019), velocity (0x701B), and bus voltage (0x701C) reads. Motor firmware rejected reads of 0x301F and 0x3022 with response status 1. Those two indices have been removed from polling. The telemetry sketch now polls only the three verified registers. Temperature and fault state remain unknown; read-status errors are not a reported motor fault.
