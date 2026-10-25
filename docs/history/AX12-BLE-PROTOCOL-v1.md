# AX12 BLE transport v1

This is the implemented Android client contract, not a claim that current MotorRecovery supports it. Current firmware uses Bluepad32/BTstack HID. A compatible server must be implemented and validated on the Stick before motion is available. Do not combine Espressif BLE libraries with the Bluepad32 BTstack build without establishing stack compatibility. Keep the existing tested MotorRecovery build available.

The user's AX12 gimbals worked in an ORQA FPV simulator, supporting standard Android joystick input as the initial approach; exact switch representation is unverified. The app supports both buttons and axes, observes all reported motion ranges and logs raw values. Vendor SDK requirements have not been established. The official product page could not be retrieved from this development environment; no proprietary AX12 API or Bluetooth role is assumed.

## GATT

| Item | UUID | Properties |
| --- | --- | --- |
| Service | `de88c000-7865-4ce4-9a13-611fad7e0001` | Primary |
| Command | `de88c001-7865-4ce4-9a13-611fad7e0001` | Write with response |
| Telemetry | `de88c002-7865-4ce4-9a13-611fad7e0001` | Notify, CCCD required |

Frames are20bytes, fitting the default ATT MTU. All multibyte values are little endian. Client sends at nominal20ms intervals (50Hz), one acknowledged GATT write in flight, no pending application command queue. This is not measured radio/motor latency. Missing write callback150ms closes connection; telemetry age200ms disarms. Bluetooth setup times out10seconds. No auto-reconnect or enable. BLE characteristic access needs an authenticated/bonded peer policy in the future receiver; UUIDs, CRC and the random16bit session do **not** authenticate a controller.

| Offset | Bytes | Command (type1) | Telemetry (type0x81) |
| --- | --- | --- | --- |
| 0 | 2 | ASCII JF | ASCII JF |
| 2 | 1 | Version1 | Version1 |
| 3 | 1 | 1 | 0x81 |
| 4 | 2 | Independent sequence modulo65536 | Independent sequence modulo65536 |
| 6 | 1 | bit0 arm-request, bit1 one-shot cycle | bit0 ready, bit1 reference valid, bit2 actually armed, bit3 fault |
| 7 | 1 | mode:0manual,1jump,2gyro | receiver's selected mode |
| 8 | 2 | signed forward/extension -1000..1000 | left measured leg angle, signed centidegrees |
| 10 | 2 | signed turn -1000..1000 | right measured leg angle, signed centidegrees |
| 12 | 2 | reserved zero | battery millivolts |
| 14 | 1 | reserved zero | fault byte (0 means none) |
| 15 | 1 | reserved zero | supported mode bitmask:1manual,2jump,4gyro |
| 16 | 2 | random nonzero session | echo current command session |
| 18 | 2 | CRC16/CCITT-FALSE over bytes0..17 | same |

CRC polynomial0x1021, init0xffff, no reflection, no final xor; golden `123456789`→0x29b1. Session changes on explicit new BLE connection. Client rejects malformed/CRC/wrong-session frames, duplicate/backward sequence deltas (valid delta1..32767), and does not refresh freshness for rejected frames. Sequence wrap65535→0 is valid. A new receiver session must be established while disarmed; reconnect cannot restore previous motion/reference trust without robot-side verification.

Disarmed commands zero axes and cycle flag. Nonfinite control values cannot produce an arm bit. One-shot cycle is asserted in one submitted frame, not repeatedly retried or resumed. Receiver must deduplicate by session/sequence and reject unsupported/invalid mode requests. A lost one-shot can therefore be missed rather than replayed later. Arm-request is distinct from confirmed armed telemetry; the client waits at most500ms for arm confirmation. It disarms on loss of robot readiness/reference/mode capability, reported fault, held enable, input device, window focus, foreground or fresh telemetry. Changing mode or control mapping also disarms.

## Required companion behavior before any motor test

- No boot enable, auto-zero, calibration, fault clearing, erases or saves. Preserve the fixed0..80deg reference procedure and RS05-specific protocol, current/speed limits and already tested motor watchdog.
- Reject bad lengths, headers, versions, CRC, sequences, sessions and values without refreshing the command deadline. Reserve bits/bytes must be checked. Accept a new session only while stopped.
- Independently enforce a bounded command timeout, initially100ms, even if the Android process freezes or a STOP frame is lost. Link loss must stop every implemented actuator, discard cycle/target state and require neutral controls plus a deliberate new arm request. Keep the CAN watchdog as an additional independent fallback.
- Only advertise genuinely implemented modes. No fake ready, reference or armed telemetry. A single-motor rig must not claim coordinated two-leg jump or gyro gait support. Future spinner control needs an explicit protocol extension and its own output failsafe.
- Android joystick events may arrive only on change. Last input-event age is diagnostic, not a periodic controller heartbeat. The app checks controller presence/enable/focus; its heartbeat does not prove a stationary input driver is healthy. Receiver deadlines guard app/radio loss, not an undetectable frozen axis report.
- Confirm power/reset/reference behavior and link-loss stopping on the hardware before replacing the tested gamepad path.

No changes to the tested motor firmware were made for this APK.
