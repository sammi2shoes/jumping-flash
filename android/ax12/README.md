# Jumping Flash AX12 Android controller

First substantial app build: `0.2.0`, Android8/API26 or later, landscape. APK: [download](https://github.com/sammi2shoes/jumping-flash/raw/refs/heads/main/releases/JumpingFlash-AX12-v0.2.0.apk).

Includes Android gamepad gimbals/buttons, selectable input device, per-device axis/button mappings, optional axis-based switches, inversion/deadzone, manual center/endpoints calibration, dashboard, BLE scan/connect, telemetry/command transport, capability-gated modes, foreground/focus/enable-loss disarm, local demo and diagnostic export. No account, Internet permission or cloud service is required at runtime.

**This build does not yet drive the robot.** MotorRecovery v0.16 speaks Bluepad32 gamepad HID, not this BLE protocol. The app includes the client for a future matching Stick firmware; it rejects devices without the service and keeps arming blocked without valid robot telemetry. Jump/gyro are requested mode selections, not implemented gaits. Spinner output is absent. The first AX12 install verifies real hardware input mapping; no further APK should be needed just to add the documented transport to the Stick.

## Install / configure the AX12 profile

1. Install/update [v0.2.0 APK](https://github.com/sammi2shoes/jumping-flash/raw/refs/heads/main/releases/JumpingFlash-AX12-v0.2.0.apk). Same app ID/signing key; no uninstall required. Open Controls, move the gimbals and choose the input device if needed.
2. The seven named rows are CH3 throttle, CH1 aileron, CH2 elevator, S2, SB, SC and SD. **Learn axis** detects the largest changed axis: move only that physical control. **Choose input** also supports manual axes, paired low/high keys for SB/SC, or a single latching key for SD. RC channel numbers do not identify Android axes. Verify mappings in the live monitor.
3. For CH1/CH2 capture physical low, centered and high positions. For CH3 capture OFF/full endpoints. S2 and switches can initially use Android's reported range; capture low/high if their range differs. Reverse controls as needed. Low/mid/high SB means disarmed/drive-only/full; SC means hop/gyro/ambulate. Each physical input must be distinct. Calibrations/mappings save per Android device descriptor.
4. S2 is a normalized height request; unmapped defaults to50%. CH1/CH2 mix preview is normalized forward±steering, not a gait or motor-angle command. Standing angles and real movement mixing remain robot-side work.
5. SD requests a full-power jump on **both** state changes, in either armed SB state and regardless of SC. Initial switch states and edges while disarmed are never queued. SB arming requires a fresh transition from disarmed; reconnect never arms. Enter full arm with throttle low; SB drive-only always blocks spinner throttle. SC changes while armed disarm and require a fresh SB cycle.
6. Diagnostics → Export diagnostic log includes Android/device details, named bindings/calibration, raw axes/key events and connection state. Use this if a switch reports unexpected inputs. Confirm that SD's Android input really latches with physical state rather than emitting momentary press pulses before enabling any receiver action.

The APK still cannot drive MotorRecovery v0.16: matching BLE **v2** firmware is needed. The client carries separate drive/spinner arm requests, throttle, height, style and one-shot jump intent; actual spinner output, gait and full-power jump implementation remain absent. Robot reference remains a robot-side manual operation. Local DEMO closes Bluetooth and labels no-output operation.

## Development

Install JDK17+ (including javac), Android SDK platform35/build-tools35.0.0, and accept the SDK licenses. Set `ANDROID_HOME` or create ignored `local.properties` with `sdk.dir=...`.

```sh
cd android/ax12
./gradlew assembleDebug lintDebug testDebugUnitTest
```

Windows: `gradlew.bat assembleDebug lintDebug testDebugUnitTest`. Android Studio can open this directory directly. The wrapper pins Gradle8.10.2 plus distribution checksum; AGP8.8.2, Java17 bytecode, compile/target SDK35 and versionCode2 are pinned. Unit tests use JUnit4.13.2 and Robolectric4.14.1 with Android9/API28, including native-graphics screen renders. Android8–15 installation and AX12-specific inputs/BLE remain hardware checks, not claims from host tests.

The deliberately public **development-only** signing keystore/password (`android`) keeps debug installs update-compatible across build machines. It is not a production signing identity or a robot authentication credential. Keep the application ID and signing config for updates and increment versionCode/versionName. Future distribution outside this development project requires separate signing management.

Debug APK: `app/build/outputs/apk/debug/app-debug.apk`. Test renders: `app/build/screenshots/`. BLE contract and receiver requirements: [AX12-BLE-PROTOCOL.md](../../docs/AX12-BLE-PROTOCOL.md).
