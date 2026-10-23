> **Historical document.** This records an earlier project stage and includes superseded firmware/timeout information. Use the root README and docs/STATUS.md for current hardware, validated results, and supported workflows.

# Jumping Flash

Arduino C++ diagnostics for the original ESP32 M5StickC Plus SE. This first milestone reads the IMU and optionally a Bluetooth gamepad. It sends **no CAN frames or actuator commands**. Walking, attitude fusion, RobStride control, AX12 integration, and onboard storage are future work.

## Cloud / Linux x86_64 build

Python 3.12+, Git, curl, and about 3 GB free disk are required. Use the existing checkout; cloud tasks are already isolated and do not need another Git worktree.

```sh
cd /workspace/jumping-flash
python scripts/setup.py
scripts/build.sh
```

Setup downloads pinned upstream artifacts, checks their SHA-256 hashes against the vendors' package indices, and installs tools outside the checkout at `/workspace/tooling`. The build uses pinned platform and M5 library commits. No secrets, external services, or running server are needed. Required network hosts are GitHub and its release/CDN hosts, pypi.org, and files.pythonhosted.org. The bootstrap avoids the blocked PlatformIO registry. Change `JUMPING_FLASH_TOOLS` in the environment to use another tool directory; retain it for both setup and builds.

Two build targets:

- `m5stick-c-plus`: IMU/display/USB diagnostics, Bluetooth disabled by the sketch.
- `controller-test`: the same diagnostics plus Bluepad32 4.1.0 gamepad input.

Both use the upstream Bluepad32 Arduino core (its bundled package manifest reports 4.0.2). The bootstrap adapts only its package name metadata for PlatformIO. The upstream binary payload remains unchanged.

Build outputs are `.pio/build/<target>/firmware.bin`, `bootloader.bin`, and `partitions.bin`. They must be flashed at their appropriate offsets, not all at address zero. Use the upload commands below rather than flashing `firmware.bin` alone at zero.

## Flash from a computer connected by USB

Cloud builds cannot access your locally attached USB device. On a Linux x86_64 computer with the project and setup installed:

```sh
scripts/build.sh -e controller-test -t upload --upload-port /dev/ttyUSB0
```

Use the actual serial port. Close other serial programs before uploading. If automatic upload fails, follow M5Stack's documented boot/download procedure for your specific device.

On Windows/macOS, use Arduino IDE with the official [ESP32 + Bluepad32 board package](https://bluepad32.readthedocs.io/en/latest/plat_arduino/), version 4.1.0. Select ESP32 Dev Module and the Huge APP (3 MB, no OTA) partition scheme. Install M5Unified 0.2.7 and M5GFX 0.2.8. In a new sketch, add `src/main.cpp` as a separate tab, leave the `.ino` empty, and put `#define ENABLE_BLUEPAD32 1` before the includes in that copy. This IDE route is documented for hardware testing; the cloud validation uses PlatformIO.

## Bench acceptance checks

1. Power only the M5StickC by USB, with motor power disconnected. The display should report IMU OK. A missing or stale sensor is displayed explicitly.
2. Open USB serial at 115200 baud. The CSV header names the fields. `sample_us` is the actual most recent IMU sample time since boot, `imu_fresh` distinguishes new samples, acceleration is in g, and gyro rates are in degrees/second. Axes are the device-native axes; mounting transforms and calibration are not yet applied. There is no attitude estimate.
3. At rest, acceleration magnitude should be near 1 g and gyro rates near zero. Rotate the device and confirm the appropriate channels change. These are measurements to verify, not results obtained in the cloud.
4. For the 8BitDo Ultimate Bluetooth model, start with its Bluetooth/Switch mode per its manual, then enter pairing mode. The 2.4 GHz USB dongle is not supported by this firmware. Look for a model identification message and PAD DATA OK on the display. Move both sticks and press buttons; verify logged axes/buttons change. Compatibility depends on the controller revision and firmware and remains unverified until this test passes.
5. Switch the controller off. PAD must become STALE or DISCONNECTED and logged axes/buttons must return to zero. The sketch's 250 ms freshness flag uses received reports, not just connection presence. A stationary controller that suppresses reports may also display STALE; this is diagnostics, not a validated motion watchdog.
6. Button A clears Bluetooth pairing keys; restart the M5StickC afterwards and pair again. Keys are otherwise retained on reboot.

Capture logs on the attached computer (install `pyserial` there):

```sh
python scripts/log_serial.py /dev/ttyUSB0 bench.csv
# Windows example: python scripts/log_serial.py COM4 bench.csv
```

Ctrl+C closes the log. Existing output files are not overwritten. The firmware targets a 100 Hz sampling/logging loop and 5 Hz display updates; rates are best effort, not hard real time. Serial rows are dropped rather than blocking when the TX buffer is full, with a cumulative `skipped_logs` counter. Boot/debug lines are filtered by the logger. USB must stay connected for logging; there is no persistent onboard flight recorder yet.

## Next hardware milestone

Verify the Mini CAN Unit's actual connector pinout, logic/power ratings, CAN bitrate, termination, motor IDs, and the RobStride 05 protocol revision before wiring or enabling CAN. No CAN pins or motor parameters have been assumed here. The next stage is receive-only bus diagnostics followed by one unloaded motor. This starter does not implement an emergency-stop circuit or any motion controller.

## CAN identification bench sketch

`firmware/CanBench/CanBench.ino` is a separate diagnostics sketch. See [CAN-BENCH.md](docs/CAN-BENCH.md) for verified sources, unresolved connector details, expected signal mapping, and passive/ACK/ID-query modes. Build with `scripts/build.sh -e can-bench`. It contains no motor enable or movement command. Resolve the documented physical connector mapping before connecting motor power.

The next separate sketch is `firmware/MotorTelemetry/MotorTelemetry.ino` (build target `motor-telemetry`). It polls the three on-device verified registers for motor ID 1: position, velocity and bus voltage, using read requests only. Temperature and fault-register reads were rejected by this motor firmware and have been removed. See the telemetry section in [CAN-BENCH.md](docs/CAN-BENCH.md) for register availability, logging format and host-test instructions. Actual hardware has now been identified by photo as StickC Plus2; the user has verified IMU/controller operation and CAN ID exchange on it.

## Unloaded motor jog

The user confirmed motor ID 1 responds to position/velocity/voltage reads and its output is free with no linkage attached. `firmware/MotorJog/MotorJog.ino` is a separate, serial-triggered movement test; build with `scripts/build.sh -e motor-jog`. See [MOTOR-JOG.md](docs/MOTOR-JOG.md) before use. No movement occurs automatically at boot. A received `g` starts one low-current, low-speed jog; `x` or physical button A requests a CAN stop. This is an unloaded bench test, not walking firmware or a physical emergency-stop circuit.

The jog test currently aborts before enabling on the user's motor because it rejects the watchdog-register readback. `firmware/WatchdogDiagnostic/WatchdogDiagnostic.ino` (target `watchdog-diagnostic`) queries firmware version and both documented timeout-register addresses without writing settings or enabling motion. See [MOTOR-JOG.md](docs/MOTOR-JOG.md).

Official release notes explain the old motor firmware's limited parameter access: see [RS05 firmware compatibility findings](docs/RS05-FIRMWARE.md). Updating the motor, if chosen, requires a verified compatible flashing route and physical post-update checks; no motor update has been performed here.

To adapt the original motor firmware without upgrading, `firmware/LegacyParameterRead/LegacyParameterRead.ino` reproduces MotorStudio's captured type-9 read interface. Build target: `legacy-parameter-read`. It is read-only and logs raw replies for the timeout, current-limit, voltage, temperature and fault indices. Reply layout/availability on hardware must be established before using it for watchdog configuration or motion.

The user's legacy read replies now verify access to the watchdog (currently 0), current limit (11 A), voltage (~24.77 V), board temperature (33.3 C) and fault mask (0). `firmware/LegacySetup/LegacySetup.ino` (target `legacy-setup`) prepares a configuration-only type-8 write/type-9 readback test for a 0.30 A limit and 200 ms watchdog, triggered by serial `c`. It contains no enable or motion command; on-device write/readback and watchdog behavior still need verification. Details and host checks are in [RS05-FIRMWARE.md](docs/RS05-FIRMWARE.md).

LegacySetup has now passed on the user's motor: legacy writes/readbacks confirm 0.30 A and watchdog 4000 counts. `firmware/LegacyWatchdogTest/LegacyWatchdogTest.ino` (target `legacy-watchdog-test`) waits for serial `t` to perform a zero-commanded-speed enable/silence/disabled-state test before any rotating jog. The motor can energize; keep its body secured and shaft unloaded. See [RS05-FIRMWARE.md](docs/RS05-FIRMWARE.md) for the test's controls, limitations and host checks.
