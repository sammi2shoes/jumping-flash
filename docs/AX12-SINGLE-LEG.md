# AX12 single-leg BLE test — APK v0.4.1 / Stick v0.1

This pair replaces the 8BitDo input path for the current **one-motor test rig**. Use M5StickC **Plus2**, MiniCAN TX32/RX33, RS05 CAN2.0 firmware0.5.0.14, motorID1/host126, 1Mbps extended CAN. MotorRecovery v0.16 remains available for the old gamepad path. This new BLE integration is compiled and simulated, **not yet verified on the physical AX12/Stick/motor combination**.

## Downloads and install

- [APK v0.4.1](../releases/JumpingFlash-AX12-v0.4.1.apk) updates the existing development-signed app in place.
- [Stick package v0.1](../releases/AX12SingleLeg-v0.1.zip) includes a ready-to-flash merged binary, Arduino sketch, and these instructions.

The easiest Stick install uses the included merged binary and Python (close Arduino Serial Monitor first). In PowerShell opened in the extracted package folder:

```powershell
py -m pip install esptool
py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x0 AX12SingleLeg-v0.1-factory.bin
```

Replace COM3 with the **Stick's** port, not the USB-CAN adapter. The merged image contains bootloader, huge-app partition table, boot selector and application at the correct offsets. Do not write this merged image at0x10000. Flashing/rebooting the Stick requires manually referencing the leg again; there is no automatic reference restoration from flash.

Arduino IDE alternative: open the included `AX12SingleLeg/AX12SingleLeg.ino`. Use **esp32 by Espressif Systems 2.0.17**, board **M5Stick-C**, partition **Huge APP (3MB No OTA/1MB SPIFFS)**, and M5Unified0.2.7 / M5GFX0.2.8. This BLE peripheral uses the official ESP32 core, **not the ESP32+Bluepad32 board package**. Bluepad32 is not required. The ZIP contains a self-contained sketch; do not paste it into the old MotorRecovery sketch.

## First connection

1. Keep SB at the position the dashboard labels **DISARMED**, gimbals centered, throttle off. Initialize the stock RadioMasterAX app once, then Force stop it in Android app settings. Remove the DSC-to-USB workaround cable. Do not reopen RadioMasterAX while our reader owns the port.
2. Power/flash the Stick; Serial Monitor115200 should identify **AX12 single-leg BLE v0.1**, with no boot enable. Its BLE name is **JumpingFlash-Leg**.
3. In the APK's Robot link tab, scan and select JumpingFlash-Leg. Complete Android pairing if prompted, then select Connect again. Pairing uses encrypted bonded BLE with Just Works, not passkey/MITM authentication or a first-owner whitelist. This is a local prototype connection policy, not a competition security design. If Android requests location permission/location services for BLE scanning, grant/enable them.
4. Live internal input is **ON by default** and starts automatically. Drive and Native inputs both have an explicit **Internal inputs ON • turn OFF** switch; OFF is remembered across app restarts. No repeated ownership checkbox is required. Keep RadioMasterAX/other serial readers stopped. If an actual port-open/read error occurs, Native inputs offers **Retry internal input connection**. The dashboard should show fresh internal inputs, SB **DISARMED**, centered CH1/CH2 and throttle0%. Defaults use the captured native ±500 ranges; Controls offers Reverse and endpoint/center capture. Verify physical SB orientation by its displayed state; do not infer it from “up/down.”
5. With the leg manually at fully retracted0°, SB DISARMED, centered gimbals and fresh input, release then hold physical **Stick B for2seconds**. Wait for valid reference and READY. Zero is captured once per boot, not on BLE reconnect.
6. Move SB to its middle **LEG ARMED** position. The leg should arm at its measured position. Move elevator a small amount to verify direction: positive normalized elevator extends, negative retracts. Neutral actively holds; fixed travel0..80°, **1A /1rad/s**. Use Controls → CH2 Reverse if necessary, while disarmed; dialogs do not require restarting live input; select SB DISARMED before rearming.
7. SB DISARMED disables. Confirm this, then confirm loss handling by switching off the AX12/closing the app. Reference should survive a confirmed ordinary stop; motor/CAN/reset uncertainty invalidates it. Reconnect/restart inputs explicitly, select SB DISARMED with centered gimbals, then deliberately rearm. Never replay an old target.

Pairing/permission dialogs and focus loss disarm the robot but keep the live reader running. STOP disarms without turning off live inputs. Actual backgrounding closes the port and BLE connection; returning automatically resumes live input reading, with SB DISARMED still required before rearming. BLE reconnect remains manual. Explicit input OFF, entering passive-probe mode, selecting an external gamepad, opening stock-app settings or restoring RadioMasterAX turns off automatic native reading. Turn internal inputs OFF before launching RadioMasterAX by any route. A stopped serial worker must finish closing before another reader starts. Restore RadioMasterAX only after stopping our reader; the existing Restore button waits for port close.

## Controls implemented for this rig

| Control | Single-leg behavior |
| --- | --- |
| SB DISARMED | Motor disabled |
| SB middle / LEG ARMED | Manual elevator control; neutral active hold |
| SB high / FULLY ARMED selection | Same leg-only behavior; throttle-low qualification retained; spinner always unavailable |
| CH2 elevator | Signed extension/retraction rate, bounded by reference0..80° |
| CH1 aileron | Must be centered when arming; ignored for leg motion |
| SC | No gait output; changing while armed disarms and requires SB DISARMED before rearming |
| S2 | Monitored, no standing-height motion |
| SD / CH3 | Monitored; no jump or spinner output |
| Stick A / serial x | Stop |
| Stick B held2s | One manual retracted reference per boot, disabled only |

There is no hidden fast cycle, second leg or ESC output in this transport. The existing out/return code is shared but has no input binding in AX12SingleLeg. In the APK, a single-leg capability bit distinguishes this bench controller from coordinated hop/gyro/ambulate modes; none of those gait capabilities is advertised. Right-leg telemetry is an unused zero slot.

## Deadlines and limits

The internal radio produced25Hz complete CRC8 source reports in the user's capture (781 reports,2091 valid frames total). The continuous reader accepts the observed exact A6/99byte route10/02/04/subtype1 layout, validates source ranges/switch states, and uses complete reports—not arbitrary serial bytes—as the input heartbeat. Invalid source reports are discarded and input reading continues; missing/corrupt input expires after120ms. Raw input frames are not logged continuously. On source loss the app submits at most one final disarmed packet, then suspends command heartbeats; stale input cannot sustain the Stick’s reference-input freshness indefinitely.

The APK sends20ms BLE heartbeats and only one acknowledged GATT write at a time. The Stick accepts valid v2,20byte CRC/session/forward-sequence commands, rejects unsupported flags/ranges, and uses a **100ms command deadline**. Loss of fresh commands clears enable and requires a fresh SB DISARMED observation before rearming. Disarmed/new-session packets cannot preserve old motion. No enabling on boot, reconnect or return of input. App telemetry expiry200ms and write-callback timeout150ms also disarm.

The existing verified20,000-count (~1second) **motor-side CAN watchdog** remains the independent fallback if the Stick dies or CAN stops working. A100ms BLE deadline is not a claim of100ms physical stopping time: CAN transaction/STOP confirmation and mechanical behavior add time. Neutral active hold consumes current; disabling releases motor torque and the ankle spring can extend the leg. Reference is in RAM; power loss/reset or uncertain motor state requires manual reference again. Existing motor-side position profile/host ramp are preserved.

## Build and validation

```sh
python scripts/setup.py                 # if shared tools are not installed
python scripts/setup_ax12_ble.py        # verified official ESP32 2.0.17 core
scripts/build.sh -e ax12-single-leg
scripts/check.sh
cd android/ax12
./gradlew assembleDebug lintDebug testDebugUnitTest
```

New host tests cover receiver CRC/length/sequence/session/deadline handling, neutral/off-edge arming, unsupported actuator requests, disconnects and time/sequence wrap; native input tests replay the actual captured frame and exercise source validation/reader cleanup; Activity tests verify native input arming without an Android gamepad and no automatic resume after stale input. Existing motor simulations remain in the checks. Hardware pairing, source direction, stop latency, reference retention and motion must be checked on the rig. No loaded launch claim follows from these builds.

## APK v0.4.1 usability update

Stick v0.1 is unchanged; no reflash is needed for this update. Native input is default-on, manually switchable OFF, automatically resumed after backgrounding, and retained across focus-only dialogs and operator STOP. OFF persists across app restarts. Transient invalid input reports do not permanently kill the serial reader. Source freshness, command deadlines, disarming and SB-off rearming rules are retained. Read-only input recovery is separate from actuator arming.
