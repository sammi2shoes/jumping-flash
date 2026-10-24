# Jumping Flash AX12 Android controller

First substantial app build: `0.1.0`, Android8/API26 or later, landscape. APK: [download](https://github.com/sammi2shoes/jumping-flash/raw/refs/heads/main/releases/JumpingFlash-AX12-v0.1.0.apk).

Includes Android gamepad gimbals/buttons, selectable input device, per-device axis/button mappings, optional axis-based switches, inversion/deadzone, manual center/endpoints calibration, dashboard, BLE scan/connect, telemetry/command transport, capability-gated modes, foreground/focus/enable-loss disarm, local demo and diagnostic export. No account, Internet permission or cloud service is required at runtime.

**This build does not yet drive the robot.** MotorRecovery v0.16 speaks Bluepad32 gamepad HID, not this BLE protocol. The app includes the client for a future matching Stick firmware; it rejects devices without the service and keeps arming blocked without valid robot telemetry. Jump/gyro are requested mode selections, not implemented gaits. Spinner output is absent. The first AX12 install verifies real hardware input mapping; no further APK should be needed just to add the documented transport to the Stick.

## First installation

1. Download the APK onto the AX12, open it and permit installation from that source if Android requests it. Open **Jumping Flash AX12**.
2. Open **Controls** and move both gimbals. If necessary use **Choose input device**. Forward defaults to Y/inverted, turn to X; select different axes based on the raw monitor in Controls/Diagnostics.
3. Center both gimbals and tap **Center / start calibration**. Sweep both mapped axes to every endpoint and tap **Save endpoints**. Return to center. Calibration and mappings persist across normal app updates/restarts.
4. Map physical enable and one-shot buttons. For switches delivered as axes, choose the axis override and its active direction. The button dropdown is ignored for that action when an axis override is selected. Do not use a gimbal axis as enable or cycle, or reuse an axis/button for both actions; arming rejects overlapping mappings.
5. Use **Drive** to view normalized inputs. **Enter DEMO** clearly labels no-output operation and closes Bluetooth; it does not simulate successful robot arming or invented telemetry.
6. If any input is missing or unusual, move the controls and press the switches, then **Diagnostics → Export diagnostic log**. Send that text file. It contains device/model/Android information, raw input values, calibration and key codes, not passwords or pairing keys.

Hold-to-enable remains the control model. Future compatible firmware will require calibrated centered controls, held enable and a deliberate **Arm request**. Mode changes disarm. Connection, focus or foreground changes never resume an old arm request/cycle. Unimplemented modes remain visibly unavailable. No robot zero can be assigned from this app: manual referencing remains a robot-side operation.

## Development

Install JDK17+ (including javac), Android SDK platform35/build-tools35.0.0, and accept the SDK licenses. Set `ANDROID_HOME` or create ignored `local.properties` with `sdk.dir=...`.

```sh
cd android/ax12
./gradlew assembleDebug lintDebug testDebugUnitTest
```

Windows: `gradlew.bat assembleDebug lintDebug testDebugUnitTest`. Android Studio can open this directory directly. The wrapper pins Gradle8.10.2 plus distribution checksum; AGP8.8.2, Java17 bytecode, compile/target SDK35 and versionCode1 are pinned. Unit tests use JUnit4.13.2 and Robolectric4.14.1 with Android9/API28, including native-graphics screen renders. Android8–15 installation and AX12-specific inputs/BLE remain hardware checks, not claims from host tests.

The deliberately public **development-only** signing keystore/password (`android`) keeps debug installs update-compatible across build machines. It is not a production signing identity or a robot authentication credential. Keep the application ID and signing config for updates and increment versionCode/versionName. Future distribution outside this development project requires separate signing management.

Debug APK: `app/build/outputs/apk/debug/app-debug.apk`. Test renders: `app/build/screenshots/`. BLE contract and receiver requirements: [AX12-BLE-PROTOCOL.md](../../docs/AX12-BLE-PROTOCOL.md).
