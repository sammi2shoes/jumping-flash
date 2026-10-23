# Current project status

## Verified on the user's bench

- Actual controller is M5StickC Plus2, not the originally discussed Plus/SE.
- IMU/display/Bluetooth diagnostic and controller-disconnect input zeroing passed.
- MiniCAN GPIO32TX/GPIO33RX; motor1, host126, classic1 Mbps CAN.
- RS05 position, velocity and voltage reads; a1 A, 5degree position move reached approximately4.98degrees and explicitly disabled afterward.
- Official motor update0.5.0.3 ->0.5.0.14 succeeded after using the OTA-panel Erase/update-entry step. This was not a factory reset or calibration.
- Standard watchdog A/B v4: timeout0 remained RUN after1,500 ms silence; timeout20000 became disabled. Explicit stops verified.
- Independent-observer physical disconnect run: disabled feedback1,028 ms after the last observed command, no explicit stop; fresh disabled feedback continued>20 seconds with no faults, missed frames, receive errors or invalid flag. User confirmed clean physical reconnection. No automatic restart was observed.

The tested watchdog setting is20,000 counts, nominal1second. This establishes the observed unloaded/zero-speed behavior and CAN-reported state; it does not measure loaded stopping distance, physical torque-removal time or certify competition compliance.

## MotorRecovery: bench motion verified; remaining failsafe validation

MotorRecovery: one-motor Bluetooth position bench control, RB/R1 hold plus left-stickX, 1 A, 1.0 rad/s,+/-20degrees around each bench arm. Disconnect/stale Bluetooth requests stop; CAN loss discards targets and recovers disabled; fresh release/press required after return. No old-target replay or automatic enable. See MOTOR-RECOVERY.md. Current v0.7 increases unloaded speed to1.0rad/s and host acceleration to5rad/s^2; its hardware behavior is not yet validated. The user's v0.6 log confirms Bluetooth-first startup, fresh gamepad input, deliberate arm, motion to both +/-20deg boundaries (approximately-20.02deg and+20.00deg), confirmed disable following R1 release, and disabled recovery to READY. No readback failure appears in that session. See bench-logs/motor-recovery-v0.6-motion.txt. Controller-loss/stale-input and CAN-loss behavior in this combined firmware still require hardware validation; earlier standalone watchdog results remain separate evidence. This firmware does not implement the calibrated leg reference.

## Available and planned hardware

One RobStride 05 is currently available (bench ID1); the second leg motor is planned. Spinner ESC: brushless AM32, 100 A; PWM versus DShot undecided. Left/right motor assignment is not established.

## Agreed but not implemented

Locked-retracted boot reference0degrees; physical CCW extension to80degrees; standing height TBD; two coordinated leg motors; jump mode; gyro-walk mode; spinner ESC output; global loss/recovery handling across every actuator; GUI/AX12 integration; calibrated IMU control and persistent onboard logging. See CONTROL-ARCHITECTURE.md and LEG-MECHANISM.md.

## Historical material

LegacySetup/LegacyParameterRead/MotorJog and the chronological RS05-FIRMWARE.md preserve the investigation of firmware0.5.0.3. Earlier4000=200 ms statements were not established on that firmware and are superseded by the tested20,000-count setting on0.5.0.14. Older sketches are not a deployment recipe. Raw uploaded logs include earlier invalid sessions followed by observer restarts; the final clean session is identified in WATCHDOG-DISCONNECT.md.

## Validation

scripts/check.sh runs all host simulations and Python checks. scripts/build.sh uses the pinned cloud toolchain; specify the firmware environment. Host simulations and builds do not replace device acceptance tests. No new mechanism motion should use the relative bench controller as an absolute leg-limit implementation.
