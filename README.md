# Jumping Flash

Arduino C++ development for an operator-controlled, 3 lb NHRL robot with a spring-foot four-bar leg mechanism. The actual controller is **M5StickC Plus2**, with an M5 MiniCAN unit and **RobStride 05 CAN2.0 firmware0.5.0.14**. One leg motor is available now; two are planned. The spinner ESC is AM32, 100 A, with PWM/DShot undecided.

**Start with [current status](docs/STATUS.md).** The tested watchdog and bench movement are implemented. The complete walking/jumping/spinner controller is not. None of these builds should be treated as calibrated linkage control or certified competition failsafe behavior.

## What passed on hardware

- IMU, display and Bluetooth/controller-input diagnostic.
- CAN identification/telemetry and a1 A,+5degree unloaded position step.
- Motor firmware update via MotorStudio's OTA-panel Erase/update-entry sequence, then update to0.5.0.14.
- Standard watchdog A/B test:20,000 counts caused disabled feedback after command silence; timeout0 control stayed RUN.
- Independent observation of a physical command-link disconnect: **1,028 ms to disabled feedback**, no explicit stop; clean reconnection with no automatic restart. Details and limitations: [disconnect results](docs/WATCHDOG-DISCONNECT.md), [raw log](docs/bench-logs/watchdog-disconnect-clean.txt).

These measurements are for the unloaded, zero-speed bench setup. They do not establish loaded stopping distance, torque-removal latency, or behavior at a shorter timeout.

## Firmware and tools

Each folder under firmware is a separate Arduino sketch. Motor power should be disconnected while changing sketches. The firmware flashed to the Stick determines what its buttons do.

| Sketch | PlatformIO environment | Purpose/status |
| --- | --- | --- |
| src/main.cpp | m5stick-c-plus / controller-test | IMU/display logging, optionally Bluepad32 input. No CAN or actuator output. ButtonA clears pairing keys in this diagnostic. |
| CanBench | can-bench | CAN identification/bring-up. No enable. |
| MotorTelemetry | motor-telemetry | Position/velocity/voltage readout. |
| PositionStep | position-step | Serialg: unloaded1 A,+5degree position step; hardware passed. x/StickA requests stop. |
| LegacyWatchdogTest | legacy-watchdog-test | Current file prints **standard A/B v4**; standard interface only, 0vs20,000-count watchdog comparison. Hardware passed. Folder name is historical. |
| WatchdogObserver | watchdog-observer | Independent CAN observer. No application commands. **ButtonA cannot stop the motor.** |
| PostUpdateDiagnostic | post-update-diagnostic | Standard-only diagnosticv2: stop/reads and timeout20000 write; no enable. Hardware passed. |
| MotorRecovery | motor-recovery | Single-leg prototype: manual zero via StickB, fixed0–80degrees, held R1,1A. v0.12 simulated/compiled; hardware reference validation pending. Older versions reset zero per arm. |
| MotorJog / WatchdogDiagnostic / LegacyParameterRead / LegacySetup / MotorStudioCapture | corresponding kebab-case environments | Historical diagnostics retained for investigation. See historical notes before use. |

[scripts/watchdog_disconnect.py](scripts/watchdog_disconnect.py) drives the official AT-format USB-CAN adapter for the independently observed disconnect test. Windows: `py watchdog_disconnect.py COM3`, using the adapter port, not the Stick port. Requires pyserial. Follow the full [procedure](docs/WATCHDOG-DISCONNECT.md).

[reference/rs05_control_gui.py](reference/rs05_control_gui.py) is the earlier user-supplied working PC GUI, retained unchanged. Its parameter-read error-status and watchdog-verification limitations are documented in [reference/README.md](reference/README.md).

## Current design requirements

Power the Bluetooth controller, then the robot with leg locks installed. Capture the locked, retracted pose as session 0 degrees; intended CCW extension is0–80 degrees with a mechanical margin. Standing angle remains TBD. Lock removal and explicit arming precede motion. CAN recovery preserves the original calibrated zero; motor reset/reference uncertainty requires referencing again. Bluetooth loss/stale input must cancel actuator demands; recovery uses fresh input and deliberate release/press enable.

Planned modes are synchronized retract/launch jumping and alternating-leg gyro walking. Their speed/current limits, timing and interaction with the spinner are not implemented or validated. See [startup/modes](docs/CONTROL-ARCHITECTURE.md), [mechanism reference](docs/LEG-MECHANISM.md), and the [correct animation](docs/media/leg-motion.mp4). The withdrawn wrong video is not included.

## Build in the cloud/Linux

Python 3.12+, Git, curl and roughly 3 GB toolchain space are needed. The bootstrap uses pinned upstream artifacts and verifies published SHA256 values. Tools live outside the checkout; no credentials are required for builds.

```sh
python3 scripts/setup.py
scripts/build.sh -e motor-recovery
scripts/check.sh
```

Set JUMPING_FLASH_TOOLS to relocate the default /workspace/tooling directory; retain it for setup/build. Network access uses GitHub/release hosts and PyPI, avoiding the PlatformIO registry. platformio.ini pins the platform, M5Unified, M5GFX and local Bluepad32-core/toolchain inputs. The Bluepad32 upstream core is4.1.0;  its bundled manifest reports4.0.2 and only package-name metadata is adapted by setup.

Build outputs are under .pio/build/<environment>. Use the toolchain's upload action; firmware.bin alone does not belong at address0. Cloud execution cannot access the user's USB-attached Stick.

## Arduino IDE on Windows

Use the existing working **ESP32 + Bluepad32** board package for sketches that include Bluepad32; select ESP32 Dev Module with HugeAPP partition, as in the working diagnostic. Libraries used here: M5Unified0.2.7 and M5GFX0.2.8. A newer library/core combination has not been validated by this repository. Open firmware/<Sketch>/<Sketch>.ino. Serial is115200 except MotorStudioCapture, which uses921600.

To use src/main.cpp in Arduino IDE, add it as a .cpp tab to an otherwise empty sketch and define ENABLE_BLUEPAD32 as0or1 before its includes. This is the diagnostic route, not the robot controller.

## Tests and records

scripts/check.sh runs eight C++ host simulations, Python serial-protocol simulation, and syntax checks. Build/device results are explicitly distinguished in [STATUS.md](docs/STATUS.md). Tests exercise actual sketch code using simulated Arduino/CAN/controller interfaces; they do not simulate the mechanical linkage or prove physical safety.

[scripts/log_serial.py](scripts/log_serial.py) captures diagnostic CSV on the local computer. The IMU diagnostic logs native axes in g and degrees/second, best-effort100 Hz; no calibrated attitude estimator or persistent flight recorder is implemented.

The chronological [RS05 investigation](docs/RS05-FIRMWARE.md) and [historical README](docs/history/INITIAL-README.md) preserve superseded versions and troubleshooting. Earlier firmware0.5.0.3 watchdog assumptions are not current instructions. Generated build folders, toolchains, Python caches and local.env files are excluded from Git. The [MotorRecovery v0.12 source ZIP](releases/MotorRecovery-v0.12.zip) requires a manual reference using Stick buttonB, preserves fixed0–80degree limits across ordinary stops/Bluetooth return, retains held R1 enable and adds a controllerY-triggered single-motor cycle at test limits; see [current instructions](docs/MOTOR-RECOVERY.md). It is a convenience snapshot, not a hardware-validated release.
