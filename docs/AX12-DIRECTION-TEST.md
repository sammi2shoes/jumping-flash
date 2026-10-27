# Two-RS05 unloaded direction test

**Purpose:** verify direction on bare motor outputs while the printed feet are being rebuilt. There is **no mechanical zero, reference capture, homing, position target or position hold** in this build. Stick B is unused. Do not use this firmware with leg mechanisms or springs attached.

Hardware: M5StickC Plus2 + Mini CAN TX32/RX33, 1Mbps extended CAN; RS05 CAN2.0 firmware0.5.0.14 on both motors. **ID1=right, ID2=left.** Right's provisional extension sign is negative encoder rotation; mirrored left's is positive. Each motor has its own battery and direct power return; CAN/signal ground is shared with Mini CAN. Leave the USB-CAN adapter disconnected during control testing.

The previous single-leg position controller is still available. This separate direction build does not implement walking, jumps, springs, weapon/ESC output, standing height or leg-limit calibration.

## Downloads

- [AX12 APK v0.6.1](../releases/JumpingFlash-AX12-v0.6.1.apk) — update the installed app in place.
- [Direction-test Stick package v0.1](../releases/AX12DirectionTest-v0.1.zip) — application update binary, self-contained Arduino sketch and instructions.

## Flash the Stick

This is an **application-only update at0x10000**, for a Stick already running AX12SingleLeg v0.1 with its huge-app partition table. The packaging check verifies the new table matches the existing single-leg factory image. Unlike writing a merged factory image at0x0, this update does not overwrite the NVS region containing Bluetooth pairing keys. It should preserve the existing bond; if Android cannot authenticate after reconnect, forget/re-pair the Stick using CONNECT.

1. Turn **both motor batteries off**; connect the Stick to the PC by USB.
2. Extract the ZIP. Open PowerShell in the folder containing `AX12DirectionTest-v0.1-app.bin`.
3. Close Arduino Serial Monitor. Use the Stick's actual COM port (COM3 below is an example):

```powershell
py -3.13 -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x10000 AX12DirectionTest-v0.1-app.bin
```

Python3.13/esptool were installed during the previous flash. If needed: `py -3.13 -m pip install esptool`. Do not write this application binary at0x0. The Stick should restart with **DIRECTION TEST**, R1(-)/L2(+), and NO ZERO REQUIRED on its screen. BLE name remains JumpingFlash-Leg.

Arduino alternative: open `AX12DirectionTest/AX12DirectionTest.ino` in the ZIP; official ESP32 core2.0.17, M5Stick-C board, Huge APP3MB partition, M5Unified0.2.7/M5GFX0.2.8. No Bluepad32 board package/library is required. Uploading/restarting never enables a motor.

## Run the test

1. Install APK v0.6.1. Keep both outputs unloaded, secure the motor bodies and keep SB at the displayed DISARMED position. Power both motor batteries.
2. CONNECT → Reconnect last Stick (or Find my Stick). Internal inputs remain default-on. Both motor IDs must respond before commissioning can proceed.
3. **Pit Tools → Check BOTH motor watchdogs • zero speed.** Keep SB DISARMED and both gimbals centered for about6seconds. This is a zero-**speed** test, not a zero-position operation. It briefly enables each motor at1A, runs a timeout-disabled A/B control and a20,000-count timeout trial, then stops both and restores the nonzero timeout. No nonzero velocity is commanded during this check.
4. Wait for **DIRECTION TEST READY** / WD PASS on the Stick. An interrupted or failed check remains unverified and blocks normal movement. STOP, Stick A, Bluetooth/input loss or leaving SB DISARMED cancels the check; if a STOP cannot be confirmed, cut both motor supplies. As in the previously tested A/B procedure, the timeout-disabled control trial temporarily has no motor-side watchdog fallback.
5. With SB DISARMED, choose **RIGHT ID1** using SC (its low normalized position). Center gimbals, select SB armed, then move elevator. Neutral commands zero speed; SB DISARMED disables both motors. Verify only the right motor moves.
6. Select SB DISARMED, choose **LEFT ID2** using SC (middle), rearm with centered gimbals and move elevator. Verify only the left motor moves.
7. Select SB DISARMED, choose **BOTH** using SC (high), rearm and compare rotation. The same positive elevator request commands **right negative / left positive encoder velocity**; reverse elevator reverses both. Confirm viewing orientation before drawing a physical linkage-direction conclusion.

There is **no B-button reference step**. Each arm merely records current encoder readings to measure that test's relative rotation; it does not set or save a motor zero. No parameter-save, fault-clear, calibration, motor-ID change or position-target command is sent.

## Run limits and recovery

- Velocity mode2,1A current limit and up to1rad/s, with a5rad/s² host ramp. Neutral immediately requests zero velocity, not position hold.
- A run disables **both** motors once any selected motor accumulates about80° of measured rotation, or2.5seconds elapse after the first nonzero request. The first guard can overshoot with sampling, stopping latency and inertia; this is not a calibrated mechanical endpoint. The deadline still stops a run if position is frozen or motion stalls. Cycle SB through DISARMED to start another run.
- SC selects right, left or both **only in this commissioning firmware**. Changing it while armed disarms both. CH1 steering must be centered when arming but does not control motion. S2, SD and CH3 produce no height/jump/spinner outputs.
- Both motors are supervised even in right-only/left-only mode. Loss of either motor, rejected settings/commands, fault feedback or failed partial arming stops both. There is no automatic restart or continuation on return of CAN/Bluetooth/input.
- Both motors must pass the zero-speed watchdog check after each Stick restart. Confirmed ordinary stops preserve that qualification. CAN/motor uncertainty blocks movement until communications/settings are restored and checks pass again. Motor-side timeout is restored to20,000 counts (~1second nominal) after successful testing and verified on both before normal enable.
- BLE command deadline100ms; internal input expiry120ms and app telemetry/write deadlines are retained. CAN failure triggers a quiet interval so diagnostic traffic cannot sustain a failed active motor's watchdog. The TWAI controller can recover from bus-off while stopped; recovery cannot arm a motor.
- Telemetry reports left/right **relative test rotation**, not absolute leg pose. The displayed voltage is the lower of the two battery readings; Serial Monitor115200 logs each battery separately. No two-leg state depends on an unverified mechanical reference.

## Protocol and validation

BLE v2 capability bit6 (`64`) explicitly identifies this unloaded two-motor velocity test. It advertises no gait, spinner or jump capabilities. Telemetry flags: bit0ready, bit1unused/no reference, bit2any motor reports RUN, bit3fault/uncertainty, bit5watchdog check pending, bit6check unverified/interrupted. Angle offsets8/10 are left/right relative centidegrees; battery12/13 is the lower pack voltage. Other v2 session/CRC/sequence/expiry rules are unchanged.

Command byte15 is0normally,1for one explicit disarmed, zero-input watchdog request, or2for explicit STOP/cancel. Maintenance is opt-in in the direction receiver; the single-leg receiver rejects these packets. Requests are session/sequence checked, consumed once, never replayed after reconnect, and discarded while an existing test is running. STOP also cancels a watchdog test even though SB was already disarmed.

Build: `scripts/build.sh -e ax12-direction-test` after the shared tool/core setup. `scripts/check.sh` includes the new real-sketch CAN simulation and existing suites. Tests verify independent ID routing and mirrored commands, both-mode operation, no zero/position commands, watchdog qualification/failure/abort, global STOP, neutral zero velocity, rotation/time guards, partial-arming cleanup, loss of an unselected motor, BLE loss/no resume and bus-off recovery without enable. Android tests cover capability64 arming without a reference, zero-speed maintenance packets, fresh/stale telemetry and the existing UI/connection/input behavior.

Builds and simulations are not physical acceptance: this exact two-motor direction build still needs testing on the user's unloaded hardware. Existing single-leg AX12 connection, input, motion and four powered failsafe checks were reported passed by the user **with the ankle spring disconnected**; that does not validate the new left motor or this two-motor firmware.

## Input reliability and monitoring update

APK v0.6.1 initializes the local UART independently and adds TOOLS → Live input monitor. [Details and cold-start acceptance](AX12-INPUT-STARTUP.md). Stick v0.1, velocity signs, limits and watchdog check are unchanged. Both bare motor directions have been reported correct by the user.
