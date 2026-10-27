# AX12 BLE transport v2 — named RC controls

Implemented Android client contract for APK0.2.0; AX12SingleLeg v0.1 now implements the single-leg bench subset; see AX12-SINGLE-LEG.md. MotorRecovery v0.16 accepts Bluepad32 HID, not this GATT protocol. Version1 is archived in history/AX12-BLE-PROTOCOL-v1.md. Version2 deliberately rejects version1 frames because style and payload meanings changed; never reinterpret old messages under the new layout.

## Operator profile

| Control | Meaning |
| --- | --- |
| CH3 throttle | Spinner speed request0..100%; zero outside qualified fully armed state |
| CH1 aileron | Steering -1..1 |
| CH2 elevator | Forward -1..1 |
| S2 knob | Standing-height request0..1; optional, default0.5 if unmapped |
| SB low/middle/high | Disarmed / drive armed with spinner disabled / fully armed |
| SC low/middle/high | Hop / gyro walk / ambulate |
| SD either state change | One full-power jump request, in either armed SB state, independent of SC |

RC channel names are labels, not Android axis IDs. Android mapping must be learned/selected and calibrated on the AX12. Switches may be axes, two keys for a three-position switch (neither key=middle), or one latching key for SD. Full-power jump is an action request, not current/torque/speed constants implemented in this APK. The receiver must provide the actual bounded launch/retraction behavior. No spinner ESC output or new motor motion code is included.

## GATT

Same UUIDs as v1; version is explicit in every frame. Primary service `de88c000-7865-4ce4-9a13-611fad7e0001`. Command characteristic `de88c001-7865-4ce4-9a13-611fad7e0001` supports write with response. Telemetry `de88c002-7865-4ce4-9a13-611fad7e0001` supports notify and standard CCCD.

All frames20bytes, default ATT MTU compatible. Multibyte values little endian. Nominal50Hz latest-command heartbeat, one acknowledged GATT write in flight; SB/SC changes and SD requests attempt an immediate write if idle. No queued stream of old targets. Missing write callback150ms closes link; telemetry age200ms disarms. These are app timing policies, not measured radio/motor latency. No automatic reconnect/arm.

| Offset | Bytes | Command (type1) | Telemetry (type0x81) |
| --- | --- | --- | --- |
| 0 | 2 | ASCII JF | ASCII JF |
| 2 | 1 | Version2 | Version2 |
| 3 | 1 | 1 | 0x81 |
| 4 | 2 | Sequence modulo65536 | Independent sequence modulo65536 |
| 6 | 1 | bit0 drive-arm request; bit1 full-power jump one-shot; bit2 spinner-arm request | bit0 ready; bit1 reference valid; bit2 actually drive-armed; bit3 fault; bit4 actually spinner-armed |
| 7 | 1 | style0hop,1gyro,2ambulate | receiver's selected style |
| 8 | 2 | signed forward -1000..1000 | left measured leg angle, signed centidegrees |
| 10 | 2 | signed steering -1000..1000 | right measured leg angle, signed centidegrees |
| 12 | 1 | throttle0..255; zero unless spinner-arm requested | battery millivolts low byte |
| 13 | 1 | height0..255 while drive-arm requested; zero otherwise | battery millivolts high byte |
| 14 | 1 | requested SB0/1/2; informational, never sufficient to arm | fault byte |
| 15 | 1 | reserved zero | supported capability bits: bit0hop, bit1gyro, bit2ambulate, bit3spinner, bit4full-power jump; bit5single-leg manual bench control (no gait capabilities implied) |
| 16 | 2 | random nonzero session | echo command session |
| 18 | 2 | CRC16/CCITT-FALSE over bytes0..17 | same |

CRC polynomial0x1021, init0xffff, no reflection/final xor. Golden `123456789`→0x29b1. Malformed, wrong-session, CRC-error and duplicate/backward sequence frames never refresh freshness; valid modulo delta1..32767, including65535→0. UUID/CRC/16bit session are not authentication; future receiver needs an authenticated/bonded peer policy.

## Arming and edge semantics

- SB0 zeroes all active requests and cancels pending jump. SB1 can drive/jump but always transmits spinner-arm off and throttle0. SB2 can additionally request spinner arm only with spinner capability and a throttle-low qualification on that entry; drive arm must already be acknowledged before spinner-arm is sent. A nonfinite throttle cannot request spinner arm.
- First arming requires an observed SB0 followed by a fresh SB transition to1/2, calibrated centered CH1/CH2, distinct mapped inputs, fresh ready/reference/fault-free telemetry and support for the selected SC style. Entering SB2 requires calibrated throttle endpoints and throttle≤3%. Dropping high throttle later does not silently complete a blocked SB2 entry: cycle the switch again. SB1→2 also requires neutral drive inputs and low calibrated throttle to qualify the spinner. SB2→1 preserves drive while immediately suppressing spinner request.
- STOP, focus/foreground/device/link loss, telemetry faults or expiry discard all pending actions and require SB0 again. An initial armed SB value is not an arm edge. Arm must be confirmed by telemetry within500ms. Selected/confirmed states are distinct on the dashboard.
- Changing SC while drive-armed disarms/cancels; cycle SB through0 to rearm in the new style. SC never changes the meaning of SD.
- SD's first known value is a baseline. Each later0↔1 transition can request one full-power jump only with confirmed drive arm, SB1/2, fresh telemetry and capability bit4. Edges while disarmed, reconnecting, unsupported, or simultaneous with SB/SC changes are discarded and not replayed. Repeated identical reports do not repeat a jump. A second edge while a jump is still pending is discarded; no jump queue.
- A jump flag appears in one submitted frame. Receiver must deduplicate by session/sequence and define busy-cycle behavior. A lost one-shot can be missed, never retried on reconnect. Nonfinite axes or height suppress drive arm; SB1/2 selector bytes alone cannot authorize output.

## Required companion implementation

Preserve existing manual0..80deg reference, no boot enable/auto-zero/save/calibration/fault-clear, RS05-specific scales and verified CAN watchdog. Validate every field/version/length/CRC/session/sequence and capability before refreshing a bounded command deadline (initial target100ms). Stop all implemented actuators on timeout/link loss and discard old motion/cycles. New sessions are accepted only while stopped. Advertise only physically implemented modes; one leg must not claim coordinated gait/jump validation. Spinner needs verified AM32 PWM/DShot output and its own failsafe. App-side CH1/CH2 differential mixing is a normalized preview, not actual gait planning or motor-angle targets.

Android may emit stationary joystick values only on change. Input age is diagnostic, not a guaranteed input-device heartbeat; controller presence/enable/focus checks cannot detect every frozen input driver. Independently test receiver deadlines, power/reset/reference handling, spinner suppression, SD edges and loaded stopping before replacing the tested gamepad path. No tested Stick motor firmware was changed by this APK update.

## Single-leg bench capability (APK v0.4.0 / Stick v0.1)

Telemetry capability32 advertises manual single-leg control separately from gait capabilities. The app permits manual arming without an SC gait capability when this bit is present. Receiver uses forward as signed leg rate, ignores steering after neutral arming, and never implements spinner/jump/height actuation. SC is echoed for consistency but changing it while armed disarms. All actuator capability bits0..4 remain clear. See AX12-SINGLE-LEG.md for native-input and pairing policy.

## Unloaded two-motor direction capability (APK v0.6.0)

Capability bit6 (`64`) is a separate velocity-only commissioning mode with no reference requirement, gait, jump or ESC action. SC selects right ID1 / left ID2 / both, forward selects spin direction, and runs are measured-rotation/time bounded. Command byte15 opts into a zero-speed watchdog check (`1`, disarmed/zero axes only) or STOP/cancel (`2`, never arm); the legacy single-leg receiver does not opt in. Telemetry bits5/6 report watchdog check pending/unverified, bit1 is unused, and angles are relative test rotation. [Complete contract and install](AX12-DIRECTION-TEST.md).


## Capability128: dual fixed-position bench

AX12DualPosition v0.1 uses capability128 exclusively; decode the full unsigned byte15. Both reference-valid bit1 and ready bit0 are required before arming. Left/right centidegrees at8/10 are measured travel from independent manual RAM origins, only meaningful while referenced. Watchdog-pending bit5 and failed bit6 retain the dual direction commissioning semantics, including explicit maintenance byte15 commands. SC is reserved but changing style still disarms. Both motors move together from elevator; neutral holds each target. No gait/jump/ESC semantics follow from this capability. See [procedure and limits](AX12-DUAL-POSITION.md).


Dual-position v0.2 retains capability128; readiness now gates checked watchdog configuration, not a newly run behavioral A/B trial. Setup is automatic while disabled. Byte14 packs motor ID/reason during uncertainty;0means no current fault report. Bit5 indicates configuration pending and bit6 explicit behavioral diagnostic failure. The APK must not label configuration verification as a behavioral PASS. [Recovery and diagnostic details](AX12-CAN-RECOVERY.md).
