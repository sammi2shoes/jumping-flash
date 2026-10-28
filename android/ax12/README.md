# Jumping Flash AX12 Android controller

[Serial startup fix and Tools input monitor](../../docs/AX12-INPUT-STARTUP.md). Live input configures/verifies921600/raw8N1 itself; no Stock-app baud bootstrap is assumed. Fresh valid reports remain mandatory.

[Two-motor unloaded direction test — no reference step](../../docs/AX12-DIRECTION-TEST.md). APK v0.6.0 supports this commissioning mode and the previous single-leg firmware; only the direction capability bypasses the mechanical reference requirement.

[New interface and guided Bluetooth connection](../../docs/AX12-UI-v0.5.md).

Current: **v0.7.2** ([rapid-SB / reference recovery](../../docs/AX12-SB-RECOVERY.md)) ([automatic setup / CAN recovery](../../docs/AX12-CAN-RECOVERY.md)) ([matched two-leg position Stick build](../../docs/AX12-DUAL-POSITION.md)) ([temporary-overlay connection fix](../../docs/AX12-OVERLAY-CONNECTION.md)) ([cold-start probe scope and test](../../docs/AX12-COLD-START-PROBE.md)), default-on cable-free AX12 internal inputs (explicit remembered OFF switch, automatic foreground resume) plus matching single-leg Stick firmware. See [AX12 single-leg setup](../../docs/AX12-SINGLE-LEG.md). Older gamepad/probe instructions below remain historical context. The live internal reader and BLE outputs are now integrated; physical acceptance of this pair is pending.

Current app build: `0.3.0`, Android9/API28 or later, ARM64, landscape. APK: [download](https://github.com/sammi2shoes/jumping-flash/raw/refs/heads/main/releases/JumpingFlash-AX12-v0.3.0.apk).

Includes Android gamepad gimbals/buttons, selectable input device, per-device axis/button mappings, optional axis-based switches, inversion/deadzone, manual center/endpoints calibration, dashboard, BLE scan/connect, telemetry/command transport, capability-gated modes, foreground/focus/enable-loss disarm, local demo and diagnostic export. No account, Internet permission or cloud service is required at runtime.

**This build does not yet drive the robot.** MotorRecovery v0.16 speaks Bluepad32 gamepad HID, not this BLE protocol. The app includes the client for a future matching Stick firmware; it rejects devices without the service and keeps arming blocked without valid robot telemetry. Jump/gyro are requested mode selections, not implemented gaits. Spinner output is absent. The first AX12 install verifies real hardware input mapping; no further APK should be needed just to add the documented transport to the Stick.

## Install / configure the AX12 profile

1. Install/update [v0.3.0 APK](https://github.com/sammi2shoes/jumping-flash/raw/refs/heads/main/releases/JumpingFlash-AX12-v0.3.0.apk). Same app ID/signing key; no uninstall required. Open Controls, move the gimbals and choose the input device if needed.
2. The seven named rows are CH3 throttle, CH1 aileron, CH2 elevator, S2, SB, SC and SD. **Learn axis** detects the largest changed axis: move only that physical control. **Choose input** also supports manual axes, paired low/high keys for SB/SC, or a single latching key for SD. RC channel numbers do not identify Android axes. Verify mappings in the live monitor.
3. For CH1/CH2 capture physical low, centered and high positions. For CH3 capture OFF/full endpoints. S2 and switches can initially use Android's reported range; capture low/high if their range differs. Reverse controls as needed. Low/mid/high SB means disarmed/drive-only/full; SC means hop/gyro/ambulate. Each physical input must be distinct. Calibrations/mappings save per Android device descriptor.
4. S2 is a normalized height request; unmapped defaults to50%. CH1/CH2 mix preview is normalized forward±steering, not a gait or motor-angle command. Standing angles and real movement mixing remain robot-side work.
5. SD requests a full-power jump on **both** state changes, in either armed SB state and regardless of SC. Initial switch states and edges while disarmed are never queued. SB arming requires a fresh transition from disarmed; reconnect never arms. Enter full arm with throttle low; SB drive-only always blocks spinner throttle. SC changes while armed disarm and require a fresh SB cycle.
6. Diagnostics → Export diagnostic log includes Android/device details, named bindings/calibration, raw axes/key events and connection state. Use this if a switch reports unexpected inputs. Confirm that SD's Android input really latches with physical state rather than emitting momentary press pulses before enabling any receiver action.

The APK cannot drive the old Bluepad32 MotorRecovery v0.16: matching BLE **v2** firmware is needed. The client carries separate drive/spinner arm requests, throttle, height, style and one-shot jump intent; actual spinner output, gait and full-power jump implementation remain absent. Robot reference remains a robot-side manual operation. Local DEMO closes Bluetooth and labels no-output operation.

## Development

Install JDK17+ (including javac), Android SDK platform35/build-tools35.0.0, and accept the SDK licenses. Set `ANDROID_HOME` or create ignored `local.properties` with `sdk.dir=...`.

```sh
cd android/ax12
./gradlew assembleDebug lintDebug testDebugUnitTest
```

Windows: `gradlew.bat assembleDebug lintDebug testDebugUnitTest`. Android Studio can open this directory directly. The wrapper pins Gradle8.10.2 plus distribution checksum; AGP8.8.2, Java17 bytecode, compile/target SDK35 and versionCode9 are pinned. Unit tests use JUnit4.13.2 and Robolectric4.14.1 with Android9/API28, including native-graphics screen renders. Android9–15 installation and AX12-specific inputs/BLE remain hardware checks, not claims from host tests.

The deliberately public **development-only** signing keystore/password (`android`) keeps debug installs update-compatible across build machines. It is not a production signing identity or a robot authentication credential. Keep the application ID and signing config for updates and increment versionCode/versionName. Future distribution outside this development project requires separate signing management.

Debug APK: `app/build/outputs/apk/debug/app-debug.apk`. Test renders: `app/build/screenshots/`. BLE contract and receiver requirements: [AX12-BLE-PROTOCOL.md](../../docs/AX12-BLE-PROTOCOL.md).

## Dial diagnostic patch v0.2.1

The AX12 recognized gimbals/switches but the user observed no dial changes in v0.2.0 Diagnostics. That build filtered input to ACTION_MOVE from the selected joystick device, so absence there did not establish that Android exposes no dial events. v0.2.1 adds a separate, observational all-device/source generic-motion/key trace (including rotary ACTION_SCROLL), plus the full Android input-device/motion-range inventory in exports. Trace events never update robot input freshness, mappings or arming.

Install over the existing APK; saved bindings/calibration are unchanged. In Diagnostics tap **Reset input trace / probe dials**, turn only S1/S2 through their travel, then **Export diagnostic log**. No need to relearn working controls. If the trace remains empty for the dials, a vendor input/firmware configuration path may be needed; no dial support is claimed from this patch alone. Relative scroll values are recorded, not automatically treated as absolute standing height.

## Internal-input prototype v0.3.0

**Native inputs** adds a passive, bounded UART reader with access/error reporting, raw-byte/timestamp export and candidate S2/switch displays. It is not yet a robot input backend. See [the full test procedure and ownership limits](../../docs/AX12-NATIVE-PROBE.md). It requires stock-app ownership to be released for the capture and restored afterward; the APK does not stop that app automatically. Probe mode blocks BLE/robot commands persistently until explicit exit. No UART writes, baud changes, root or permission changes occur. Existing gamepad mappings remain available outside probe mode.

Normal Gradle builds include a prebuilt ARM64 JNI reader and verify its source/binary hash stamp. C changes require rebuilding with the pinned NDK using scripts/build_ax12_uart_probe.sh. Android8 is no longer a claimed target for this native prototype; the actual AX12 is Android9.
